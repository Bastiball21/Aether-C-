#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "tt.h"
#include "see.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <cstring>
#include <cmath>

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

// LMR Table
int LMRTable[64][64];

// History Tables
int History[2][6][64];
int16_t ContHistory[2][6][64][6][64];
uint16_t CounterMove[2][4096];
int KillerMoves[MAX_PLY][2];

// Constants for History
const int MAX_HISTORY = 16384;

void init_lmr() {
    for (int d = 0; d < 64; d++) {
        for (int m = 0; m < 64; m++) {
            if (d < 3 || m < 2) LMRTable[d][m] = 0;
            else {
                LMRTable[d][m] = (int)(1.0 + log(d) * log(m) / 2.0);
            }
        }
    }
}

// Options
bool Search::UseNMP = true;
bool Search::UseProbCut = true;
bool Search::UseSingular = true;
bool Search::UseHistory = true;

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

long long Search::get_node_count() {
    return node_count;
}

void Search::clear() {
    TTable.clear();
    std::memset(History, 0, sizeof(History));
    std::memset(ContHistory, 0, sizeof(ContHistory));
    std::memset(CounterMove, 0, sizeof(CounterMove));
    std::memset(KillerMoves, 0, sizeof(KillerMoves));
    init_lmr();
}

// History Update Helpers
void Search::update_history(int side, int pt, int to, int bonus) {
    if (side < 0 || side > 1 || pt < 0 || pt > 5 || to < 0 || to > 63) return;
    int& h = History[side][pt][to];
    h += bonus - (h * std::abs(bonus)) / MAX_HISTORY;
}

void Search::update_continuation(int side, int prev_pt, int prev_to, int pt, int to, int bonus) {
    if (side < 0 || side > 1 || prev_pt < 0 || prev_pt > 5 || prev_to < 0 || prev_to > 63 || pt < 0 || pt > 5 || to < 0 || to > 63) return;
    int16_t& h = ContHistory[side][prev_pt][prev_to][pt][to];
    h += bonus - (h * std::abs(bonus)) / MAX_HISTORY;
}

void Search::update_counter_move(int side, int prev_from, int prev_to, uint16_t move) {
    if (side < 0 || side > 1 || prev_from < 0 || prev_from > 63 || prev_to < 0 || prev_to > 63) return;
    int key = (prev_from << 6) | prev_to;
    CounterMove[side][key] = move;
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
class MovePicker {
    const Position& pos;
    MoveGen::MoveList list;
    MoveGen::MoveList bad_captures; // To store bad captures without regenerating

    int current_idx = 0;
    int bad_current_idx = 0;

    int scores[256];
    bool is_bad_capture[256]; // Explicit classification
    int bad_scores[256];

    uint16_t tt_move;
    uint16_t prev_move;
    uint16_t killers[2];
    int ply;
    int stage;
    bool captures_only;
    bool skip_bad_captures; // New flag
    int killer_idx = 0;

    enum Stage {
        STAGE_TT_MOVE,
        STAGE_GEN_CAPTURES,
        STAGE_GOOD_CAPTURES,
        STAGE_KILLERS,
        STAGE_GEN_QUIETS,
        STAGE_QUIETS,
        STAGE_BAD_CAPTURES,
        STAGE_FINISHED
    };

    static const int SCORE_GOOD_CAPTURE_BASE = 200000;
    static const int SCORE_BAD_CAPTURE_BASE = -200000;

public:
    MovePicker(const Position& p, uint16_t tm, int pl, uint16_t pm)
        : pos(p), tt_move(tm), prev_move(pm), ply(pl), stage(STAGE_TT_MOVE), captures_only(false), skip_bad_captures(false), killer_idx(0) {
        if (ply < MAX_PLY) {
            killers[0] = KillerMoves[ply][0];
            killers[1] = KillerMoves[ply][1];
        } else {
            killers[0] = killers[1] = 0;
        }
    }

    MovePicker(const Position& p, bool caps_only, bool skip_bad = false)
        : pos(p), tt_move(0), prev_move(0), ply(0), stage(STAGE_GEN_CAPTURES), captures_only(caps_only), skip_bad_captures(skip_bad), killer_idx(0) {
        killers[0] = killers[1] = 0;
    }

    void score_captures() {
        for (int i = 0; i < list.count; i++) {
            uint16_t m = list.moves[i];
            int flag = (m >> 12);
            Piece victim = pos.piece_on((Square)(m & 0x3F));
            int victim_val = 0;
            if (flag == 5) victim_val = 1;
            else if (victim != NO_PIECE) {
                static const int val[] = {1, 3, 3, 5, 9, 0};
                victim_val = val[victim % 6];
            }
            if (flag & 8) {
                static const int promo_vals[] = {3, 3, 5, 9};
                victim_val += promo_vals[flag & 3];
            }
            Piece attacker = pos.piece_on((Square)((m >> 6) & 0x3F));
            int attacker_val = 1;
            if (attacker != NO_PIECE) {
                static const int val[] = {1, 3, 3, 5, 9, 0};
                attacker_val = val[attacker % 6];
            }

            int see_score = see(pos, m);
            int mvv_lva = (victim_val * 10) - attacker_val;

            if (see_score >= 0) {
                scores[i] = SCORE_GOOD_CAPTURE_BASE + mvv_lva + see_score;
                is_bad_capture[i] = false;
            } else {
                scores[i] = SCORE_BAD_CAPTURE_BASE + mvv_lva + see_score;
                is_bad_capture[i] = true;
            }
        }
    }

    void score_quiets() {
        Square prev_to = SQ_NONE;
        Piece prev_pc = NO_PIECE;
        if (prev_move != 0) {
             prev_to = (Square)(prev_move & 0x3F);
             prev_pc = pos.piece_on(prev_to);
        }

        for (int i = 0; i < list.count; i++) {
            uint16_t m = list.moves[i];
            Square t = (Square)(m & 0x3F);
            Piece pc = pos.piece_on((Square)((m >> 6) & 0x3F));
            int pt = pc % 6;

            int score = 0;
            if (Search::UseHistory) {
                score = History[pos.side_to_move()][pt][t];
                if (prev_pc != NO_PIECE && prev_to != SQ_NONE) {
                    score += ContHistory[pos.side_to_move()][prev_pc % 6][prev_to][pt][t];
                }
                if (prev_move != 0) {
                    Square pf = (Square)((prev_move >> 6) & 0x3F);
                    Square pt_sq = (Square)(prev_move & 0x3F);
                    if (CounterMove[pos.side_to_move()][(pf << 6) | pt_sq] == m) {
                        score += 2000;
                    }
                }
            }
            scores[i] = score;
        }
    }

    uint16_t pick_best(int min_score) {
        if (current_idx >= list.count) return 0;
        int best_idx = -1;
        int best_score = -2147483647;
        for (int i = current_idx; i < list.count; i++) {
             if (scores[i] > best_score) {
                 best_score = scores[i];
                 best_idx = i;
             }
        }
        if (best_idx != -1 && best_score >= min_score) {
             std::swap(list.moves[current_idx], list.moves[best_idx]);
             std::swap(scores[current_idx], scores[best_idx]);
             return list.moves[current_idx++];
        }
        return 0;
    }

    // Pick best for Bad Captures list
    uint16_t pick_best_bad() {
        if (bad_current_idx >= bad_captures.count) return 0;
        int best_idx = -1;
        int best_score = -2147483647;
        for (int i = bad_current_idx; i < bad_captures.count; i++) {
             if (bad_scores[i] > best_score) {
                 best_score = bad_scores[i];
                 best_idx = i;
             }
        }
        if (best_idx != -1) {
             std::swap(bad_captures.moves[bad_current_idx], bad_captures.moves[best_idx]);
             std::swap(bad_scores[bad_current_idx], bad_scores[best_idx]);
             return bad_captures.moves[bad_current_idx++];
        }
        return 0;
    }

    uint16_t next() {
        // if (node_count > 1000000) return 0; // Safety brake
        while (true) {
            switch (stage) {
                case STAGE_TT_MOVE:
                    stage = STAGE_GEN_CAPTURES;
                    if (tt_move != 0) return tt_move;
                    break;

                case STAGE_GEN_CAPTURES: {
                    MoveGen::generate_captures(pos, list);
                    score_captures();

                    // Partition into Good (in list) and Bad (in bad_captures)
                    bad_captures.count = 0;
                    int good_count = 0;

                    for (int i = 0; i < list.count; i++) {
                        if (is_bad_capture[i]) { // Explicit SEE check
                            bad_captures.add(list.moves[i]);
                            bad_scores[bad_captures.count - 1] = scores[i];
                        } else { // Good Capture
                            list.moves[good_count] = list.moves[i];
                            scores[good_count] = scores[i];
                            good_count++;
                        }
                    }
                    list.count = good_count;

                    current_idx = 0;
                    stage = STAGE_GOOD_CAPTURES;
                    break;
                }

                case STAGE_GOOD_CAPTURES: {
                    uint16_t m = pick_best(-2000000000); // Pick any from Good list
                    if (m == 0) {
                        if (captures_only) stage = STAGE_BAD_CAPTURES;
                        else stage = STAGE_KILLERS;
                        break;
                    }
                    if (m == tt_move) continue;
                    return m;
                }

                case STAGE_KILLERS:
                    if (killer_idx < 2) {
                        uint16_t m = killers[killer_idx++];
                        if (m != 0 && m != tt_move) {
                            if (MoveGen::is_pseudo_legal(pos, m)) {
                                int flag = (m >> 12);
                                bool is_cap = ((flag & 4) || (flag == 5) || (flag & 8));
                                if (is_cap) {
                                     continue;
                                }
                                return m;
                            }
                        }
                        continue;
                    }
                    stage = STAGE_GEN_QUIETS;
                    break;

                case STAGE_GEN_QUIETS:
                    MoveGen::generate_quiets(pos, list);
                    score_quiets();
                    current_idx = 0;
                    stage = STAGE_QUIETS;
                    break;

                case STAGE_QUIETS: {
                    uint16_t m = pick_best(-2000000000);
                    if (m == 0) {
                         stage = STAGE_BAD_CAPTURES;
                         break;
                    }
                    if (m == tt_move) continue;
                    if (m == killers[0] || m == killers[1]) continue;
                    return m;
                }

                case STAGE_BAD_CAPTURES: {
                    if (skip_bad_captures) { // Optimization
                         stage = STAGE_FINISHED;
                         break;
                    }
                    uint16_t m = pick_best_bad();
                    if (m == 0) {
                        stage = STAGE_FINISHED;
                        break;
                    }
                    if (m == tt_move) continue;
                    if (m == killers[0] || m == killers[1]) continue;
                    // Already filtered Good Captures out
                    return m;
                }

                case STAGE_FINISHED:
                    return 0;
            }
        }
    }
};

int quiescence(Position& pos, int alpha, int beta, int ply) {
    if ((node_count & 1023) == 0) check_limits();
    if (stop_flag) return 0;
    node_count++;

#ifndef NDEBUG
    if ((node_count & 4095) == 0) pos.debug_validate();
#endif

    if (ply >= MAX_PLY - 1) return Eval::evaluate(pos);

    bool in_check = pos.in_check();

    if (!in_check) {
        int stand_pat = Eval::evaluate_light(pos);
        if (stand_pat >= beta) return beta;

        const int DELTA_MARGIN = 975;
        if (stand_pat < alpha - DELTA_MARGIN) return alpha;

        if (alpha < stand_pat) alpha = stand_pat;
    }

    // Skip bad captures if not in check
    MovePicker mp(pos, true, !in_check);
    uint16_t move;
    int moves_searched = 0;

    while ((move = mp.next())) {
        if (!in_check) {
            // MovePicker now handles bad capture skipping via skip_bad_captures=true
            // So we don't need to check see() here.
            // if (see(pos, move) < 0) continue;
        }

        if (pos.piece_on((Square)((move >> 6) & 0x3F)) == NO_PIECE) continue;

        pos.make_move(move);
        if (pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, ~pos.side_to_move())), pos.side_to_move())) {
            pos.unmake_move(move);
            continue;
        }

        moves_searched++;
        TTable.prefetch(pos.key());

        int score = -quiescence(pos, -beta, -alpha, ply + 1);
        pos.unmake_move(move);

        if (stop_flag) return 0;

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    if (in_check && moves_searched == 0) {
        return -MATE_SCORE + ply;
    }

    return alpha;
}

int negamax(Position& pos, int depth, int alpha, int beta, int ply, bool null_allowed, uint16_t prev_move, uint16_t excluded_move) {
    if ((node_count & 1023) == 0) check_limits();
    if (stop_flag) return 0;
    node_count++;

    if (ply >= MAX_PLY - 1) return Eval::evaluate(pos);
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
    bool tt_hit = TTable.probe(pos.key(), tte);
    if (tt_hit) {
        tt_move = tte.move;
        if (tt_move != 0) {
             Square f = (Square)((tt_move >> 6) & 0x3F);
             if (f < 0 || f >= 64 || pos.piece_on(f) == NO_PIECE || pos.piece_on(f) / 6 != pos.side_to_move()) {
                  tt_move = 0;
             }
        }
        if (tte.depth >= depth) {
            int tt_score = score_from_tt(tte.score, ply);
            if (tte.bound() == 1) return tt_score;
            if (tte.bound() == 2 && tt_score <= alpha) return alpha;
            if (tte.bound() == 3 && tt_score >= beta) return tt_score;
        }
    }

    if (depth >= 5 && tt_move == 0) {
        int iid_depth = depth - 2;
        negamax(pos, iid_depth, alpha, beta, ply, false, prev_move, 0);
        if (TTable.probe(pos.key(), tte)) {
            tt_move = tte.move;
        }
    }

    int singular_ext = 0;
    if (Search::UseSingular && depth >= 6 && tt_move != 0 && tte.bound() == 1 && tte.depth >= depth - 1 && excluded_move == 0) {
        int tt_score = score_from_tt(tte.score, ply);
        int singular_margin = 60;
        int singular_beta = tt_score - singular_margin;
        int alt_score = negamax(pos, depth - 2, singular_beta - 1, singular_beta, ply, false, prev_move, tt_move);
        if (alt_score < singular_beta) {
            singular_ext = 1;
        }
    }

    int static_eval = 0;
    if (!in_check) {
        static_eval = Eval::evaluate(pos);
        if (depth <= 3 && static_eval - 120 * depth >= beta) return static_eval;

        if (Search::UseProbCut && depth >= 5 && std::abs(beta) < MATE_SCORE - 100) {
            int prob_margin = 120;
            MoveGen::MoveList cap_list;
            MoveGen::generate_captures(pos, cap_list);
            int prob_cut_count = 0;
            for(int i=0; i<cap_list.count && prob_cut_count < 6; i++) {
                uint16_t m = cap_list.moves[i];
                if (see(pos, m) > 0) {
                     pos.make_move(m);
                     if (pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, ~pos.side_to_move())), pos.side_to_move())) {
                        pos.unmake_move(m);
                        continue;
                     }
                     int score = -negamax(pos, depth - 4, -beta - prob_margin, -beta - prob_margin + 1, ply + 1, false, 0, 0);
                     pos.unmake_move(m);
                     if (score >= beta + prob_margin) return beta + prob_margin;
                     prob_cut_count++;
                }
            }
        }

        if (Search::UseNMP && null_allowed && depth >= 3 && static_eval >= beta && (pos.non_pawn_material(pos.side_to_move()) >= 320 + 330)) {
            int reduction = 2;
            if (depth >= 7) reduction = 3;
            pos.make_null_move();
            int score = -negamax(pos, depth - 1 - reduction, -beta, -beta + 1, ply + 1, false, 0, 0);
            pos.unmake_null_move();
            if (stop_flag) return 0;
            if (score >= beta) {
                if (depth >= 6) {
                     int verify_score = negamax(pos, depth - 1, alpha, beta, ply, false, prev_move, 0);
                     if (verify_score >= beta) return beta;
                } else {
                     return beta;
                }
            }
        }
    }

    MovePicker mp(pos, tt_move, ply, prev_move);
    uint16_t move;
    int moves_searched = 0;
    int best_score = -INFINITY_SCORE;
    uint16_t best_move = 0;

    uint16_t tried_quiets[512];
    int tried_quiets_cnt = 0;

    while ((move = mp.next())) {
        if (move == excluded_move) continue;
        if (pos.piece_on((Square)((move >> 6) & 0x3F)) == NO_PIECE) continue;

        bool is_cap = ((move >> 12) & 4) || ((move >> 12) == 5) || ((move >> 12) & 8);
        bool is_quiet = !is_cap;

        if (is_quiet && tried_quiets_cnt < 512) {
            tried_quiets[tried_quiets_cnt++] = move;
        }

        if (is_quiet && !in_check && depth <= 5) {
             static const int lmp_table[] = {0, 3, 5, 8, 12, 20};
             if (moves_searched >= lmp_table[depth]) {
                  bool is_pv = (beta - alpha > 1);
                  if (!is_pv) break;
             }
        }

        if (!in_check && is_quiet && depth < 6 && static_eval + 150 * depth <= alpha) {
             continue;
        }

        if (depth == 1 && !in_check && is_quiet) {
             int fut_margin = 150;
             if (static_eval + fut_margin <= alpha) continue;
        }

        if (is_cap && !in_check && depth <= 5) {
            bool is_promo = (move >> 12) & 8;
            if (!is_promo) {
                int margin = (depth - 1) * -50;
                if (see(pos, move) < margin) continue;
            }
        }

        pos.make_move(move);
        if (pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, ~pos.side_to_move())), pos.side_to_move())) {
            pos.unmake_move(move);
            continue;
        }

        bool gives_check = pos.in_check();

        moves_searched++;
        TTable.prefetch(pos.key());

        int ext = 0;
        int flag = (move >> 12);
        if (prev_move != 0) {
             Square prev_to = (Square)(prev_move & 0x3F);
             Square to_sq = (Square)(move & 0x3F);
             if (to_sq == prev_to) {
                 if ((flag & 4) || (flag == 5) || (flag & 8)) ext = 1;
             }
        }
        if (move == tt_move) ext += singular_ext;

        // Square from_sq = (Square)((move >> 6) & 0x3F); // Unused
        Square to_sq = (Square)(move & 0x3F);
        Piece moved_piece = pos.piece_on(to_sq);

        if (ext == 0 && (moved_piece == W_PAWN || moved_piece == B_PAWN)) {
             int r = rank_of(to_sq);
             if (pos.side_to_move() == BLACK && r == RANK_7) ext = 1;
             else if (pos.side_to_move() == WHITE && r == RANK_2) ext = 1;
             if ((flag & 8)) ext = 1;
        }

        int score;
        if (moves_searched == 1) {
            score = -negamax(pos, depth - 1 + ext, -beta, -alpha, ply + 1, true, move, 0);
        } else {
            int reduction = 0;
            if (depth >= 3 && moves_searched > 1 && !in_check) {
                int d = std::min(depth, 63);
                int m = std::min(moves_searched, 63);
                reduction = LMRTable[d][m];

                if (is_quiet) reduction += 1;
                if (ext > 0) reduction = 0;
                if ((flag & 4) || (flag == 5) || (flag & 8)) reduction = 0;
                if (gives_check) reduction = 0;

                if (reduction < 0) reduction = 0;
                if (reduction > depth - 1) reduction = depth - 1;
            }

            score = -negamax(pos, depth - 1 - reduction + ext, -alpha - 1, -alpha, ply + 1, true, move, 0);
            if (score > alpha && reduction > 0) {
                 score = -negamax(pos, depth - 1 + ext, -alpha - 1, -alpha, ply + 1, true, move, 0);
            }
            if (score > alpha && score < beta) {
                score = -negamax(pos, depth - 1 + ext, -beta, -alpha, ply + 1, true, move, 0);
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
                     bool is_capture = ((flag & 4) || (flag == 5) || (flag & 8));
                     if (!is_capture) {
                         KillerMoves[ply][1] = KillerMoves[ply][0];
                         KillerMoves[ply][0] = move;

                         int depth_bonus = depth * depth;
                         if (depth_bonus > 400) depth_bonus = 400;

                         Square f = (Square)((move >> 6) & 0x3F);
                         Square t = (Square)(move & 0x3F);
                         Piece pc = pos.piece_on(f);
                         int pt = pc % 6;

                         if (Search::UseHistory) {
                             Search::update_history(pos.side_to_move(), pt, t, depth_bonus);

                             if (prev_move != 0) {
                                 Square prev_to = (Square)(prev_move & 0x3F);
                                 Piece prev_pc = pos.piece_on(prev_to);
                                 if (prev_pc != NO_PIECE) {
                                     Search::update_continuation(pos.side_to_move(), prev_pc % 6, prev_to, pt, t, depth_bonus);
                                 }
                                 Square prev_from = (Square)((prev_move >> 6) & 0x3F);
                                 Search::update_counter_move(pos.side_to_move(), prev_from, prev_to, move);
                             }

                             for (int i=0; i<tried_quiets_cnt; i++) {
                                 uint16_t bad_move = tried_quiets[i];
                                 if (bad_move != move) {
                                     Square bf = (Square)((bad_move >> 6) & 0x3F);
                                     Square bt = (Square)(bad_move & 0x3F);
                                     Piece bpc = pos.piece_on(bf);
                                     Search::update_history(pos.side_to_move(), bpc % 6, bt, -depth_bonus);
                                     if (prev_move != 0) {
                                         Square prev_to = (Square)(prev_move & 0x3F);
                                         Piece prev_pc = pos.piece_on(prev_to);
                                         if (prev_pc != NO_PIECE) {
                                             Search::update_continuation(pos.side_to_move(), prev_pc % 6, prev_to, bpc % 6, bt, -depth_bonus);
                                         }
                                     }
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
    TTable.store(pos.key(), best_move, score_to_tt(best_score, ply), static_eval, depth, type);

    return best_score;
}

void Search::start(Position& pos, const SearchLimits& limits) {
    static bool init = false;
    if (!init) {
        init_lmr();
        init = true;
    }
    stop_flag = false;
    node_count = 0;
    start_time = steady_clock::now();
    nodes_limit_count = limits.nodes;

    Search::UseNMP = limits.use_nmp;
    Search::UseProbCut = limits.use_probcut;
    Search::UseSingular = limits.use_singular;
    Search::UseHistory = limits.use_history;

    allocated_time_limit = 0;
    if (!limits.infinite) {
        if (limits.move_time > 0) {
            allocated_time_limit = limits.move_time - limits.move_overhead_ms;
            if (allocated_time_limit < 1) allocated_time_limit = 1;
        } else if (limits.time[pos.side_to_move()] > 0) {
            int remaining = limits.time[pos.side_to_move()];
            int inc = limits.inc[pos.side_to_move()];
            int m_to_go = (limits.movestogo > 0) ? limits.movestogo : 30;
            int base_time = remaining / m_to_go;
            allocated_time_limit = base_time + (int)(inc * 0.8);
            if (allocated_time_limit > remaining - limits.move_overhead_ms) {
                allocated_time_limit = remaining - limits.move_overhead_ms;
            }
            if (allocated_time_limit < 1) allocated_time_limit = 1;
        }
    }
    if (limits.infinite) allocated_time_limit = 0;

    TTable.new_search();
    iter_deep(pos, limits);
}

void Search::iter_deep(Position& pos, const SearchLimits& limits) {
    int max_depth = limits.depth > 0 ? limits.depth : MAX_PLY;
    int prev_score = 0;

    for (int depth = 1; depth <= max_depth; depth++) {
        int score = 0;
        if (depth < 2) {
             score = negamax(pos, depth, -INFINITY_SCORE, INFINITY_SCORE, 0, true, 0, 0);
        } else {
             int delta = 25;
             int alpha = std::max(-INFINITY_SCORE, prev_score - delta);
             int beta = std::min(INFINITY_SCORE, prev_score + delta);
             while (true) {
                 if (stop_flag) break;
                 score = negamax(pos, depth, alpha, beta, 0, true, 0, 0);
                 if (score <= alpha) {
                     alpha = std::max(-INFINITY_SCORE, alpha - delta);
                     delta *= 2;
                 } else if (score >= beta) {
                     beta = std::min(INFINITY_SCORE, beta + delta);
                     delta *= 2;
                 } else {
                     break;
                 }
                 if (delta > 400) {
                     alpha = -INFINITY_SCORE;
                     beta = INFINITY_SCORE;
                 }
             }
        }
        prev_score = score;
        if (stop_flag) break;

        auto now = steady_clock::now();
        long long ms = duration_cast<milliseconds>(now - start_time).count();

        std::string pv_str = "";
        Position temp_pos = pos;
        int pv_depth = 0;
        std::vector<Key> pv_keys;
        pv_keys.push_back(temp_pos.key());
        while (pv_depth < depth) {
             TTEntry tte;
             if (!TTable.probe(temp_pos.key(), tte) || tte.move == 0) break;

             MoveGen::MoveList ml;
             MoveGen::generate_all(temp_pos, ml);
             bool found = false;
             for (int i = 0; i < ml.count; ++i) {
                 if (ml.moves[i] == tte.move) { found = true; break; }
             }
             if (!found) break;

             temp_pos.make_move(tte.move);
             Color us = (Color)(temp_pos.side_to_move() ^ 1);
             if (temp_pos.is_attacked((Square)Bitboards::lsb(temp_pos.pieces(KING, us)), (Color)(us ^ 1))) {
                 temp_pos.unmake_move(tte.move);
                 break;
             }

             pv_str += move_to_uci(tte.move) + " ";
             pv_depth++;

             bool cycled = false;
             for (Key k : pv_keys) if (k == temp_pos.key()) { cycled = true; break; }
             if (cycled) break;
             pv_keys.push_back(temp_pos.key());
        }

        std::cout << "info depth " << depth
                  << " score cp " << score
                  << " nodes " << node_count
                  << " time " << ms
                  << " nps " << (ms > 0 ? (node_count * 1000 / ms) : 0)
                  << " pv " << pv_str << "\n";
    }

    TTEntry tte;
    uint16_t best_move = 0;
    if (TTable.probe(pos.key(), tte)) best_move = tte.move;
    if (best_move == 0) {
        MovePicker mp(pos, 0, 0, 0);
        best_move = mp.next();
    }
    std::cout << "bestmove " << move_to_uci(best_move) << "\n" << std::flush;
}
