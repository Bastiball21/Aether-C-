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
int64_t allocated_time_limit = 0;
int64_t nodes_limit_count = 0;

// Constants
const int INFINITY_SCORE = 32000;
const int MATE_SCORE = 31000;
const int MAX_PLY = 128;

// History Tables
int History[2][64][64]; // [side][from][to]
int ContHistory[6][64][6][64]; // [prev_piece][prev_to][curr_piece][curr_to]
int KillerMoves[MAX_PLY][2];

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

void check_limits() {
    if (stop_flag) return;
    if (nodes_limit_count > 0 && node_count >= nodes_limit_count) {
        stop_flag = true;
        return;
    }
    if (allocated_time_limit > 0) {
        auto now = steady_clock::now();
        long long ms = duration_cast<milliseconds>(now - start_time).count();
        if (ms >= allocated_time_limit) {
            stop_flag = true;
        }
    }
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

        for (int i=0; i<list.count; i++) {
             uint16_t m = list.moves[i];
             int flag = (m >> 12);
             if ((flag & 4) || (flag == 5) || (flag & 8)) { // Capture or Promo
                Piece victim = pos.piece_on((Square)(m & 0x3F));
                int victim_val = 0;
                if (victim != NO_PIECE) {
                    int v_type = victim % 6;
                    static const int val[] = {1, 3, 3, 5, 9, 0};
                    victim_val = val[v_type];
                }
                Piece attacker = pos.piece_on((Square)((m >> 6) & 0x3F));
                int attacker_val = 1;
                if (attacker != NO_PIECE) {
                    int a_type = attacker % 6;
                    static const int val[] = {1, 3, 3, 5, 9, 0};
                    attacker_val = val[a_type];
                }
                scores[i] = 10000 + (victim_val * 10) - attacker_val;
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
                bool is_promo = (flag & 8);

                if (is_capture || is_promo) {
                     Square from_sq = (Square)((m >> 6) & 0x3F);
                     Square to_sq = (Square)(m & 0x3F);
                     Piece attacker = pos.piece_on(from_sq);
                     Piece victim = pos.piece_on(to_sq);

                     int victim_val = 0;
                     if (flag == 5) {
                         victim_val = 1;
                     } else if (victim != NO_PIECE) {
                         int v_type = victim % 6;
                         static const int val[] = {1, 3, 3, 5, 9, 0};
                         victim_val = val[v_type];
                     }

                     if ((flag & 8)) {
                         int p = (flag & 3);
                         static const int promo_vals[] = {3, 3, 5, 9};
                         victim_val += promo_vals[p];
                     }

                     int attacker_val = 1;
                     if (attacker != NO_PIECE) {
                        int a_type = attacker % 6;
                        static const int val[] = {1, 3, 3, 5, 9, 0};
                        attacker_val = val[a_type];
                     }

                     score = 100000 + (victim_val * 10) - attacker_val;

                } else {
                    if (ply < MAX_PLY && KillerMoves[ply][0] == m) score = 90000;
                    else if (ply < MAX_PLY && KillerMoves[ply][1] == m) score = 80000;
                    else {
                        Square f = (Square)((m >> 6) & 0x3F);
                        Square t = (Square)(m & 0x3F);
                        score = History[pos.side_to_move()][f][t];

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
        int best_idx = current_idx;
        int best_score = scores[current_idx];
        for (int i = current_idx + 1; i < list.count; i++) {
            if (scores[i] > best_score) {
                best_score = scores[i];
                best_idx = i;
            }
        }
        std::swap(list.moves[current_idx], list.moves[best_idx]);
        std::swap(scores[current_idx], scores[best_idx]);
        return list.moves[current_idx++];
    }
};

int quiescence(Position& pos, int alpha, int beta, int ply) {
    if ((node_count & 1023) == 0) check_limits();
    if (stop_flag) return 0;
    node_count++;

    int stand_pat = Eval::evaluate_light(pos);
    if (stand_pat >= beta) return beta;

    const int DELTA_MARGIN = 975;
    if (stand_pat < alpha - DELTA_MARGIN) return alpha;

    if (alpha < stand_pat) alpha = stand_pat;

    MovePicker mp(pos, true);
    uint16_t move;
    while ((move = mp.next())) {
        pos.make_move(move);
        if (pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, ~pos.side_to_move())), pos.side_to_move())) {
            pos.unmake_move(move);
            continue;
        }

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
    if ((node_count & 1023) == 0) check_limits();
    if (stop_flag) return 0;
    node_count++;

    if (ply >= MAX_PLY) return Eval::evaluate(pos);
    if (pos.rule50_count() >= 100 || pos.is_repetition()) return 0;

    int original_alpha = alpha;
    int mate_val = MATE_SCORE - ply;
    if (alpha < -mate_val) alpha = -mate_val;
    if (beta > mate_val - 1) beta = mate_val - 1;
    if (alpha >= beta) return alpha;

    bool in_check = pos.in_check();
    if (in_check) depth++;

    if (depth <= 0) return quiescence(pos, alpha, beta, ply);

    TTEntry tte;
    uint16_t tt_move = 0;
    // Probe (not const because gen update)
    if (TTable.probe(pos.key(), tte)) {
        tt_move = tte.move;
        if (tte.depth >= depth) {
            if (tte.bound == 1) return tte.score;
            if (tte.bound == 2 && tte.score <= alpha) return alpha;
            if (tte.bound == 3 && tte.score >= beta) return tte.score;
        }
    }

    int static_eval = 0;
    if (!in_check) {
        static_eval = Eval::evaluate(pos);
        if (depth <= 3 && static_eval - 120 * depth >= beta) return static_eval;
        if (null_allowed && depth >= 3 && static_eval >= beta && (pos.pieces(pos.side_to_move()) & ~pos.pieces(PAWN, pos.side_to_move()))) {
            int reduction = 3 + depth / 4;
            pos.make_null_move();
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
        if (pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, ~pos.side_to_move())), pos.side_to_move())) {
            pos.unmake_move(move);
            continue;
        }

        moves_searched++;
        TTable.prefetch(pos.key());

        int score;
        if (moves_searched == 1) {
            score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, true, move);
        } else {
            int reduction = 0;
            if (depth >= 3 && moves_searched > 4 && !in_check) {
                reduction = 1 + (depth * moves_searched) / 48;
                if (reduction > depth - 1) reduction = depth - 1;
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
                if (ply < MAX_PLY) {
                     int flag = (move >> 12);
                     if (!((flag & 4) || (flag == 5) || (flag & 8))) {
                         KillerMoves[ply][1] = KillerMoves[ply][0];
                         KillerMoves[ply][0] = move;
                         Square f = (Square)((move >> 6) & 0x3F);
                         Square t = (Square)(move & 0x3F);
                         History[pos.side_to_move()][f][t] += depth * depth;
                         if (prev_move != 0) {
                             Square prev_to = (Square)(prev_move & 0x3F);
                             Piece prev_pc = pos.piece_on(prev_to);
                             if (prev_pc != NO_PIECE) {
                                 Piece curr_pc = pos.piece_on(f);
                                 if (curr_pc != NO_PIECE) {
                                     int pt_prev = prev_pc % 6;
                                     int pt_curr = curr_pc % 6;
                                     ContHistory[pt_prev][prev_to][pt_curr][t] += depth * depth;
                                 }
                             }
                         }
                     }
                }
                break;
            }
        }
    }

    if (moves_searched == 0) {
        if (in_check) return -MATE_SCORE + ply;
        else return 0;
    }

    int type = (best_score <= original_alpha) ? 2 : (best_score >= beta ? 3 : 1);
    // Store
    TTable.store(pos.key(), best_move, best_score, static_eval, depth, type);

    return best_score;
}

void Search::start(Position& pos, const SearchLimits& limits) {
    stop_flag = false;
    node_count = 0;
    start_time = steady_clock::now();
    nodes_limit_count = limits.nodes;

    allocated_time_limit = 0;
    if (!limits.infinite) {
        if (limits.move_time > 0) {
            allocated_time_limit = limits.move_time - limits.move_overhead_ms;
        } else if (limits.time[pos.side_to_move()] > 0) {
            int remaining = limits.time[pos.side_to_move()];
            int inc = limits.inc[pos.side_to_move()];
            int m_to_go = (limits.movestogo > 0) ? limits.movestogo : 30;

            // Basic time management formula
            int base_time = remaining / m_to_go;
            allocated_time_limit = base_time + (int)(inc * 0.8);

            // Safety clamp
            if (allocated_time_limit > remaining - limits.move_overhead_ms) {
                allocated_time_limit = remaining - limits.move_overhead_ms;
            }
        }

        // Ensure at least 1ms if not infinite
        if (allocated_time_limit < 1) allocated_time_limit = 1;
    }

    // Explicitly set very large if infinite, so checks pass but never triggers
    if (limits.infinite) allocated_time_limit = 2000000000;

    TTable.new_search(); // New generation

    iter_deep(pos, limits);
}

void Search::iter_deep(Position& pos, const SearchLimits& limits) {
    int max_depth = limits.depth > 0 ? limits.depth : MAX_PLY;

    int alpha = -INFINITY_SCORE;
    int beta = INFINITY_SCORE;
    int delta = 25; // Aspiration window size

    for (int depth = 1; depth <= max_depth; depth++) {
        int score = 0;

        if (depth < 2) {
             score = negamax(pos, depth, -INFINITY_SCORE, INFINITY_SCORE, 0, true, 0);
        } else {
             // Aspiration window
             alpha = std::max(-INFINITY_SCORE, score - delta);
             beta = std::min(INFINITY_SCORE, score + delta);

             while (true) {
                 if (stop_flag) break;
                 score = negamax(pos, depth, alpha, beta, 0, true, 0);

                 if (score <= alpha) {
                     beta = (alpha + beta) / 2;
                     alpha = std::max(-INFINITY_SCORE, alpha - delta);
                     delta += delta / 2;
                 } else if (score >= beta) {
                     beta = std::min(INFINITY_SCORE, beta + delta);
                     delta += delta / 2;
                 } else {
                     break; // Window okay
                 }

                 // Cap delta?
                 if (delta > 3000) {
                     alpha = -INFINITY_SCORE;
                     beta = INFINITY_SCORE;
                 }
             }
        }

        // If stopped during search, we can break.
        // We still print bestmove at the end.
        // Usually engines don't print info for incomplete depths unless it found a mate or something useful.
        // But if stop_flag is true, 'score' might be garbage (0 returned) or alpha.
        if (stop_flag) break;

        // Reset delta for next depth? Or keep it?
        // Usually reset or slightly adjust.
        delta = 25;

        auto now = steady_clock::now();
        long long ms = duration_cast<milliseconds>(now - start_time).count();

        std::string pv_str = "";
        Position temp_pos = pos;
        int pv_depth = 0;
        std::vector<Key> pv_keys;
        pv_keys.push_back(temp_pos.key());

        while (pv_depth < depth) {
             TTEntry tte;
             if (TTable.probe(temp_pos.key(), tte) && tte.move != 0) {
                 pv_str += move_to_uci(tte.move) + " ";
                 temp_pos.make_move(tte.move);
                 pv_depth++;
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

    }

    TTEntry tte;
    uint16_t best_move = 0;
    if (TTable.probe(pos.key(), tte)) best_move = tte.move;

    if (best_move == 0) {
        MovePicker mp(pos, 0, 0, 0);
        best_move = mp.next();
    }

    std::cout << "bestmove " << move_to_uci(best_move) << std::endl;
}
