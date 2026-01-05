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
int ContHistory[6][64][6][64]; // [prev_piece][prev_to][curr_piece][curr_to]
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

    // Clear ContHistory
    for(int i=0; i<6; i++)
        for(int j=0; j<64; j++)
            for(int k=0; k<6; k++)
                for(int l=0; l<64; l++)
                    ContHistory[i][j][k][l] = 0;
}

// Helper to get piece type on board, safely handling NO_PIECE
int get_piece_val(Piece p) {
    if (p == NO_PIECE) return 0;
    return (p % 6) + 1;
}

// Move Picker
struct MovePicker {
    const Position& pos;
    MoveGen::MoveList list;
    int current_idx = 0;
    int scores[256];
    uint16_t prev_move;

    MovePicker(const Position& p, uint16_t tt_move, int ply, uint16_t prev_m) : pos(p), prev_move(prev_m) {
        MoveGen::generate_all(pos, list);
        score_moves(tt_move, ply);
    }

    MovePicker(const Position& p, bool captures_only) : pos(p), prev_move(0) {
        if (captures_only) MoveGen::generate_captures(pos, list);
        else MoveGen::generate_all(pos, list);
        // Score captures: MVV/LVA
        for (int i=0; i<list.count; i++) {
             uint16_t m = list.moves[i];
             int flag = (m >> 12);
             if ((flag & 4) || (flag == 5) || (flag & 8)) { // Capture or Promo
                 Square from_sq = (Square)((m >> 6) & 0x3F);
                 Square to_sq = (Square)(m & 0x3F);

                 Piece attacker = pos.piece_on(from_sq);
                 Piece victim = pos.piece_on(to_sq);

                 // MVV/LVA formula: (Victim * 10) - Attacker
                 int victim_score = 0;

                 // Handle En Passant
                 if (flag == 5) {
                     victim_score = 1; // Pawn
                 } else if (victim != NO_PIECE) {
                     victim_score = (victim % 6) + 1; // 1..6
                 } else {
                     // Quiet Promotion (flag >= 8 && flag <= 11) or other?
                     // If it's a promotion without capture, victim is NO_PIECE.
                     // But we entered this block for flag 8 (promo).
                     // We should probably score promotions highly.
                     // A queen promo is like capturing a queen or better.
                     if (flag >= 8) {
                         // Promo piece type: N=0, B=1, R=2, Q=3
                         int p = (flag & 3);
                         int promo_val = 0;
                         if (p == 0) promo_val = 2; // N
                         else if (p == 1) promo_val = 3; // B
                         else if (p == 2) promo_val = 4; // R
                         else if (p == 3) promo_val = 5; // Q
                         victim_score = promo_val + 1; // Boost
                     }
                 }

                 int attacker_score = 0;
                 if (attacker != NO_PIECE) attacker_score = (attacker % 6) + 1;
                 else attacker_score = 1; // Should not happen for legal move

                 scores[i] = 10000 + (victim_score * 10) - attacker_score;
             } else {
                 scores[i] = 0;
             }
        }
    }

    void score_moves(uint16_t tt_move, int ply) {
        Square prev_to = SQ_NONE;
        Piece prev_pc = NO_PIECE;
        if (prev_move != 0) {
             prev_to = (Square)(prev_move & 0x3F);
             // We can't easily get prev_piece from just move without board state before move.
             // But we can approximate or ignore if piece info is missing.
             // Actually, we need 'prev_piece' (the piece that moved to prev_to).
             // On current board 'pos', the piece is at 'prev_to'.
             prev_pc = pos.piece_on(prev_to);
        }

        for (int i=0; i<list.count; i++) {
            uint16_t m = list.moves[i];
            int score = 0;

            if (m == tt_move) {
                score = 2000000;
            } else {
                int flag = (m >> 12);
                bool is_capture = (flag & 4) || (flag == 5);
                bool is_promo = (flag & 8); // Covers 8-15

                if (is_capture || is_promo) {
                    // MVV/LVA
                     Square from_sq = (Square)((m >> 6) & 0x3F);
                     Square to_sq = (Square)(m & 0x3F);

                     Piece attacker = pos.piece_on(from_sq);
                     Piece victim = pos.piece_on(to_sq);

                     int victim_score = 0;
                     if (flag == 5) {
                         victim_score = 1;
                     } else if (victim != NO_PIECE) {
                         victim_score = (victim % 6) + 1;
                     }

                     // Boost for promotions
                     if ((flag & 8)) {
                         int p = (flag & 3);
                         int promo_score = 0;
                         if (p == 0) promo_score = 3; // N
                         if (p == 1) promo_score = 3; // B
                         if (p == 2) promo_score = 5; // R
                         if (p == 3) promo_score = 9; // Q
                         victim_score += promo_score;
                     }

                     int attacker_score = (attacker != NO_PIECE) ? ((attacker % 6) + 1) : 1;

                     score = 100000 + (victim_score * 10) - attacker_score;

                } else {
                    // Killers
                    if (ply < MAX_PLY && KillerMoves[ply][0] == m) score = 90000;
                    else if (ply < MAX_PLY && KillerMoves[ply][1] == m) score = 80000;
                    else {
                        // History
                        Square f = (Square)((m >> 6) & 0x3F);
                        Square t = (Square)(m & 0x3F);
                        score = History[pos.side_to_move()][f][t];

                        // Continuation History
                        if (prev_pc != NO_PIECE && prev_to != SQ_NONE) {
                            Piece curr_pc = pos.piece_on(f);
                            if (curr_pc != NO_PIECE) {
                                int pt_prev = prev_pc % 6;
                                int pt_curr = curr_pc % 6;
                                score += ContHistory[pt_prev][prev_to][pt_curr][t];
                            }
                        }
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

    // DELTA PRUNING
    const int DELTA_MARGIN = 975;
    if (stand_pat < alpha - DELTA_MARGIN) {
        // Strictly check for promotion moves here?
        // For now, we assume if we are down a Queen, we probably won't recover without one.
        // We return alpha.
        return alpha;
    }

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

        // Prefetch TT
        TTable.prefetch(pos.key());

        int score = -quiescence(pos, -beta, -alpha, ply + 1);
        pos.unmake_move(move);

        if (stop_flag) return 0;

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    return alpha;
}

int negamax(Position& pos, int depth, int alpha, int beta, int ply, bool null_allowed, uint16_t prev_move) {
    if ((node_count & 1023) == 0 && stop_flag) return 0;
    node_count++;

    if (ply >= MAX_PLY) return Eval::evaluate(pos);

    // Repetition detection (and 50 move rule)
    if (pos.rule50_count() >= 100 || pos.is_repetition()) return 0;

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
            if (tte.type == 2 && tte.score <= alpha) return alpha; // Alpha (Upper bound <= alpha)
            if (tte.type == 3 && tte.score >= beta) return tte.score; // Beta (Lower bound >= beta)
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
            // Pass 0 as prev_move for null move, or handle appropriately?
            // Null move doesn't have a 'move' u16.
            int score = -negamax(pos, depth - reduction, -beta, -beta + 1, ply + 1, false, 0);
            pos.unmake_null_move();

            if (stop_flag) return 0;
            if (score >= beta) return beta;
        }
    }

    MovePicker mp(pos, tt_move, ply, prev_move);
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

        // Prefetch TT for next position
        TTable.prefetch(pos.key());

        int score;
        // PVS / LMR
        if (moves_searched == 1) {
            score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, true, move);
        } else {
            // LMR
            int reduction = 0;
            if (depth >= 3 && moves_searched > 4 && !in_check) {
                // Formula-based LMR
                reduction = 1 + (depth * moves_searched) / 32;
                if (reduction > depth - 1) reduction = depth - 1;

                // Checks for captures/promotions (flags) needed to avoid reduction
                int flag = (move >> 12);
                if ((flag & 4) || (flag == 5) || (flag & 8)) reduction = 0;
            }

            score = -negamax(pos, depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, true, move);

            if (score > alpha && reduction > 0) {
                 score = -negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1, true, move);
            }

            if (score > alpha && score < beta) {
                score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, true, move);
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

                         // Continuation History
                         if (prev_move != 0) {
                             Square prev_to = (Square)(prev_move & 0x3F);
                             Piece prev_pc = pos.piece_on(prev_to); // Note: pos is already unmade, so piece is back at prev_to?
                             // Wait. `negamax` takes `pos` at current state. `prev_move` happened BEFORE `pos`.
                             // So `pos` has the piece at `prev_to`.
                             if (prev_pc != NO_PIECE) {
                                 Piece curr_pc = pos.piece_on(f); // Piece at 'from' square of current move 'move'
                                 if (curr_pc != NO_PIECE) {
                                     int pt_prev = prev_pc % 6;
                                     int pt_curr = curr_pc % 6;
                                     ContHistory[pt_prev][prev_to][pt_curr][t] += depth * depth;
                                 }
                             }
                         }
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
        int score = negamax(pos, depth, -INFINITY_SCORE, INFINITY_SCORE, 0, true, 0);

        if (stop_flag) break;

        auto now = steady_clock::now();
        long long ms = duration_cast<milliseconds>(now - start_time).count();

        // Retrieve PV
        std::string pv_str = "";
        // Basic PV retrieval from TT
        Position temp_pos = pos;

        int pv_depth = 0;
        // Check for infinite loop in PV (if hash collision or bug)
        std::vector<Key> pv_keys;
        pv_keys.push_back(temp_pos.key());

        while (pv_depth < depth) {
             TTEntry tte;
             if (TTable.probe(temp_pos.key(), tte) && tte.move != 0) {
                 pv_str += move_to_uci(tte.move) + " ";
                 temp_pos.make_move(tte.move);
                 pv_depth++;

                 // Simple cycle check for PV printing
                 bool cycled = false;
                 for (Key k : pv_keys) if (k == temp_pos.key()) cycled = true;
                 if (cycled) break;
                 pv_keys.push_back(temp_pos.key());
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
            // Improved Time Management
            // Standard: (time / 20) + (inc / 2)
            int time_alloc = (limits.time[pos.side_to_move()] / 20) + (limits.inc[pos.side_to_move()] / 2);
            if (ms > time_alloc) stop_flag = true;
        }

        if (limits.move_time > 0 && ms >= limits.move_time) stop_flag = true;

        // Stop early if mate found
        if (score > MATE_SCORE - 100) stop_flag = true; // Found mate
    }

    // Best move
    TTEntry tte;
    uint16_t best_move = 0;
    if (TTable.probe(pos.key(), tte)) best_move = tte.move;

    // If no best move (failed low at depth 1?), pick first legal
    if (best_move == 0) {
        MovePicker mp(pos, 0, 0, 0);
        best_move = mp.next();
    }

    std::cout << "bestmove " << move_to_uci(best_move) << std::endl;
}
