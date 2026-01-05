#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "tt.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <iomanip>

using namespace std::chrono;

// Global Signals
std::atomic<bool> stop_flag(false);
long long node_count = 0;
steady_clock::time_point start_time;

// Constants
const int INFINITY_SCORE = 32000;
const int MATE_SCORE = 31000;
const int MAX_PLY = 128;

// History Tables
int History[2][64][64]; // [side][from][to]
int KillerMoves[MAX_PLY][2];

// Stack
struct Stack {
    uint16_t current_move;
    int eval;
    bool in_check;
};

// Helpers
std::string move_to_uci(uint16_t m) {
    if (m == 0) return "0000";
    Square f = (Square)((m >> 6) & 0x3F);
    Square t = (Square)(m & 0x3F);
    int flag = (m >> 12);

    std::string s = "";
    s += (char)('a' + file_of(f));
    s += (char)('1' + rank_of(f));
    s += (char)('a' + file_of(t));
    s += (char)('1' + rank_of(t));

    if (flag & 8) { // Promo
        int p = (flag & 3); // 0=N, 1=B, 2=R, 3=Q
        char pchar = 'q';
        if (p == 0) pchar = 'n';
        if (p == 1) pchar = 'b';
        if (p == 2) pchar = 'r';
        s += pchar;
    }
    return s;
}

void Search::stop() {
    stop_flag = true;
}

void Search::clear() {
    TTable.clear();
    // Clear history
    for(int i=0; i<2; i++)
        for(int j=0; j<64; j++)
            for(int k=0; k<64; k++)
                History[i][j][k] = 0;
}

// Move Picker
struct MovePicker {
    const Position& pos;
    MoveGen::MoveList list;
    int current_idx = 0;
    int scores[256];

    MovePicker(const Position& p, uint16_t tt_move, int ply) : pos(p) {
        MoveGen::generate_all(pos, list);
        score_moves(tt_move, ply);
    }

    MovePicker(const Position& p, bool captures_only) : pos(p) {
        if (captures_only) MoveGen::generate_captures(pos, list);
        else MoveGen::generate_all(pos, list);
        // Score captures: MVV/LVA
        for (int i=0; i<list.count; i++) {
             uint16_t m = list.moves[i];
             int flag = (m >> 12);
             if ((flag & 4) || (flag == 5) || (flag & 8)) { // Capture or Promo
                 // Simple MVV-LVA
                 int victim = 0;
                 // We don't have victim in move. Need to lookup.
                 // This is slow. But standard.
                 // For now assign high score.
                 scores[i] = 10000;
             } else {
                 scores[i] = 0;
             }
        }
    }

    void score_moves(uint16_t tt_move, int ply) {
        for (int i=0; i<list.count; i++) {
            uint16_t m = list.moves[i];
            int score = 0;

            if (m == tt_move) {
                score = 2000000;
            } else {
                int flag = (m >> 12);
                bool is_capture = (flag & 4) || (flag == 5);

                if (is_capture) {
                    // MVV/LVA placeholder
                    score = 100000;
                } else {
                    // Killers
                    if (ply < MAX_PLY && KillerMoves[ply][0] == m) score = 90000;
                    else if (ply < MAX_PLY && KillerMoves[ply][1] == m) score = 80000;
                    else {
                        // History
                        Square f = (Square)((m >> 6) & 0x3F);
                        Square t = (Square)(m & 0x3F);
                        score = History[pos.side_to_move()][f][t];
                    }
                }
            }
            scores[i] = score;
        }
    }

    uint16_t next() {
        if (current_idx >= list.count) return 0;

        // Selection Sort
        int best_idx = current_idx;
        int best_score = scores[current_idx];

        for (int i = current_idx + 1; i < list.count; i++) {
            if (scores[i] > best_score) {
                best_score = scores[i];
                best_idx = i;
            }
        }

        // Swap
        std::swap(list.moves[current_idx], list.moves[best_idx]);
        std::swap(scores[current_idx], scores[best_idx]);

        return list.moves[current_idx++];
    }
};

int quiescence(Position& pos, int alpha, int beta, int ply) {
    if ((node_count & 1023) == 0 && stop_flag) return 0;
    node_count++;

    int stand_pat = Eval::evaluate_light(pos);
    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;

    MovePicker mp(pos, true); // Captures only
    uint16_t move;
    while ((move = mp.next())) {
        pos.make_move(move);
        // Illegal check?
        if (pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, ~pos.side_to_move())), pos.side_to_move())) {
            pos.unmake_move(move);
            continue;
        }

        int score = -quiescence(pos, -beta, -alpha, ply + 1);
        pos.unmake_move(move);

        if (stop_flag) return 0;

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    return alpha;
}

int negamax(Position& pos, int depth, int alpha, int beta, int ply, bool null_allowed) {
    if ((node_count & 1023) == 0 && stop_flag) return 0;
    node_count++;

    if (ply >= MAX_PLY) return Eval::evaluate(pos);
    if (pos.rule50_count() >= 100) return 0; // Draw

    int original_alpha = alpha; // Save original alpha for TT type

    // Mate distance pruning
    int mate_val = MATE_SCORE - ply;
    if (alpha < -mate_val) alpha = -mate_val;
    if (beta > mate_val - 1) beta = mate_val - 1;
    if (alpha >= beta) return alpha;

    bool in_check = pos.in_check();
    if (in_check) depth++; // Check extension

    if (depth <= 0) return quiescence(pos, alpha, beta, ply);

    // TT Probe
    TTEntry tte;
    uint16_t tt_move = 0;
    if (TTable.probe(pos.key(), tte)) {
        tt_move = tte.move;
        if (tte.depth >= depth) {
            if (tte.type == 1) return tte.score; // Exact
            if (tte.type == 2 && tte.score <= alpha) return alpha; // Alpha (Upper bound <= alpha? No, Alpha flag means score <= bound)
            // Wait. Alpha flag (Upper Bound) means node value <= tte.score.
            // If tte.score <= alpha, then node value <= alpha. Fail low. Return alpha (or tte.score).
            if (tte.type == 2 && tte.score <= alpha) return tte.score;
            if (tte.type == 3 && tte.score >= beta) return tte.score; // Beta flag (Lower Bound)
        }
    }

    int static_eval = 0;
    if (!in_check) {
        static_eval = Eval::evaluate(pos);

        // RFP (Reverse Futility Pruning)
        if (depth <= 3 && static_eval - 120 * depth >= beta) return static_eval;

        // NMP (Null Move Pruning)
        if (null_allowed && depth >= 3 && static_eval >= beta && (pos.pieces(pos.side_to_move()) & ~pos.pieces(PAWN, pos.side_to_move()))) {
            int reduction = 3 + depth / 4;
            pos.make_null_move();
            int score = -negamax(pos, depth - reduction, -beta, -beta + 1, ply + 1, false);
            pos.unmake_null_move();

            if (stop_flag) return 0;
            if (score >= beta) return beta;
        }
    }

    MovePicker mp(pos, tt_move, ply);
    uint16_t move;
    int moves_searched = 0;
    int best_score = -INFINITY_SCORE;
    uint16_t best_move = 0;

    while ((move = mp.next())) {
        pos.make_move(move);
        // Legality check
        if (pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, ~pos.side_to_move())), pos.side_to_move())) {
            pos.unmake_move(move);
            continue;
        }

        moves_searched++;

        int score;
        // PVS / LMR
        if (moves_searched == 1) {
            score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, true);
        } else {
            // LMR
            int reduction = 0;
            if (depth >= 3 && moves_searched > 4 && !in_check) {
                // Formula-based LMR to approximate table: R = 1 + ln(depth) * ln(moves) / 2
                // Or simplified: R = depth/8 + moves/16 ? No.
                // Rust code uses a table.
                // Let's use a standard formula approximation:
                // R = 0.77 + ln(depth) * ln(moves) / 2.66
                // Integer approximation:
                reduction = 1 + (depth * moves_searched) / 32;
                if (reduction > depth - 1) reduction = depth - 1;

                // Checks for captures/promotions (flags) needed to avoid reduction
                int flag = (move >> 12);
                if ((flag & 4) || (flag == 5) || (flag & 8)) reduction = 0;

                // PV node reduction is usually less, but here we are in non-PV search (moves_searched > 1)
            }

            score = -negamax(pos, depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, true);

            if (score > alpha && reduction > 0) {
                 score = -negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1, true);
            }

            if (score > alpha && score < beta) {
                score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, true);
            }
        }

        pos.unmake_move(move);

        if (stop_flag) return 0;

        if (score > best_score) {
            best_score = score;
            best_move = move;
        }

        if (score > alpha) {
            alpha = score;
            if (alpha >= beta) {
                // Killer / History
                if (ply < MAX_PLY) {
                     int flag = (move >> 12);
                     if (!((flag & 4) || (flag == 5) || (flag & 8))) { // Quiet
                         KillerMoves[ply][1] = KillerMoves[ply][0];
                         KillerMoves[ply][0] = move;
                         Square f = (Square)((move >> 6) & 0x3F);
                         Square t = (Square)(move & 0x3F);
                         History[pos.side_to_move()][f][t] += depth * depth;
                     }
                }
                break; // Beta Cutoff
            }
        }
    }

    if (moves_searched == 0) {
        if (in_check) return -MATE_SCORE + ply;
        else return 0; // Stalemate
    }

    // Store TT
    int type = (best_score <= original_alpha) ? 2 : (best_score >= beta ? 3 : 1);
    TTable.store(pos.key(), best_move, best_score, static_eval, depth, type);

    return best_score;
}

void Search::start(Position& pos, const SearchLimits& limits) {
    stop_flag = false;
    node_count = 0;
    start_time = steady_clock::now();

    iter_deep(pos, limits);
}

void Search::iter_deep(Position& pos, const SearchLimits& limits) {
    int max_depth = limits.depth > 0 ? limits.depth : MAX_PLY;

    for (int depth = 1; depth <= max_depth; depth++) {
        int score = negamax(pos, depth, -INFINITY_SCORE, INFINITY_SCORE, 0, true);

        if (stop_flag) break;

        auto now = steady_clock::now();
        long long ms = duration_cast<milliseconds>(now - start_time).count();

        // Retrieve PV
        std::string pv_str = "";
        // Basic PV retrieval from TT
        Position temp_pos = pos; // Copy to walk PV (this is slow if pos copy is slow, but pos is small)
        // Position copy ctor is default, should be fine (POD mostly)
        // Wait, Position has std::vector<StateInfo>. That's a deep copy. Fine.

        int pv_depth = 0;
        while (pv_depth < depth) {
             TTEntry tte;
             if (TTable.probe(temp_pos.key(), tte) && tte.move != 0) {
                 pv_str += move_to_uci(tte.move) + " ";
                 temp_pos.make_move(tte.move);
                 pv_depth++;
             } else {
                 break;
             }
        }

        std::cout << "info depth " << depth
                  << " score cp " << score
                  << " nodes " << node_count
                  << " time " << ms
                  << " nps " << (ms > 0 ? (node_count * 1000 / ms) : 0)
                  << " pv " << pv_str << std::endl;

        // Time Check
        if (limits.time[pos.side_to_move()] > 0) {
            // Simple allocation: 5% of time remaining
            int time_alloc = limits.time[pos.side_to_move()] / 20;
            if (ms > time_alloc) stop_flag = true;
        }
        if (limits.move_time > 0 && ms >= limits.move_time) stop_flag = true;
    }

    // Best move
    TTEntry tte;
    uint16_t best_move = 0;
    if (TTable.probe(pos.key(), tte)) best_move = tte.move;

    // If no best move (failed low at depth 1?), pick first legal
    if (best_move == 0) {
        MovePicker mp(pos, 0, 0);
        best_move = mp.next();
    }

    std::cout << "bestmove " << move_to_uci(best_move) << std::endl;
}
