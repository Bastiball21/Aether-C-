#include "worker.h"
#include "eval/eval.h"
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

// Global Signals/Data
std::atomic<bool> stop_flag(false);
int64_t allocated_time_limit = 0;
int64_t nodes_limit_count = 0;
steady_clock::time_point start_time;
// int OptThreads = 1; // Defined in main.cpp

// LMR Table (Static, shared)
int LMRTable[64][64];
bool lmr_init = false;

void init_lmr() {
    if (lmr_init) return;
    for (int d = 0; d < 64; d++) {
        for (int m = 0; m < 64; m++) {
            if (d < 3 || m < 2) LMRTable[d][m] = 0;
            else {
                LMRTable[d][m] = (int)(1.0 + log(d) * log(m) / 2.0);
            }
        }
    }
    lmr_init = true;
}

// Search Options (Static in Search class, used by Workers)
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

// Constants
const int INFINITY_SCORE = 32000;
const int MATE_SCORE = 31000;
const int MAX_HISTORY = 16384;

// SearchWorker Implementation

SearchWorker::SearchWorker(int id) : thread_id(id), node_count(0), exit_thread(false), searching(false) {
    clear_history();
}

SearchWorker::~SearchWorker() {
    stop();
    {
        std::lock_guard<std::mutex> lk(mutex);
        exit_thread = true;
    }
    cv.notify_one();
    if (worker_thread.joinable()) worker_thread.join();
}

void SearchWorker::start_search(const Position& pos, const SearchLimits& lm) {
    {
        std::lock_guard<std::mutex> lk(mutex);
        root_pos = pos;
        limits = lm;
        searching = true;
    }

    // Lazy initialization of thread
    if (thread_id != 0 && !worker_thread.joinable()) {
        worker_thread = std::thread(&SearchWorker::search_loop, this);
    } else if (thread_id != 0) {
        cv.notify_one();
    }
}

void SearchWorker::wait_for_completion() {
    // Only for workers
    if (thread_id == 0) return;

    while (searching) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void SearchWorker::stop() {
    // Just sets flag? The global stop_flag handles the search loop.
}

void SearchWorker::search_loop() {
    // If master (id 0), we don't loop waiting on CV. We run once.
    if (thread_id == 0) {
        // Master runs directly
        node_count = 0;
        decay_history();
        iter_deep();
        searching = false;
        return;
    }

    while (true) {
        std::unique_lock<std::mutex> lk(mutex);
        cv.wait(lk, [this] { return searching || exit_thread; });

        if (exit_thread) return;

        Position pos = root_pos;
        // SearchLimits lim = limits; // Unused
        lk.unlock();

        node_count = 0;
        decay_history();

        iter_deep();

        lk.lock();
        searching = false;
    }
}

void SearchWorker::clear_history() {
    std::memset(History, 0, sizeof(History));
    std::memset(CaptureHistory, 0, sizeof(CaptureHistory));
    std::memset(ContHistory, 0, sizeof(ContHistory));
    std::memset(CounterMove, 0, sizeof(CounterMove));
    std::memset(KillerMoves, 0, sizeof(KillerMoves));
}

void SearchWorker::decay_history() {
    // Decay History
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 6; ++j) {
            for (int k = 0; k < 64; ++k) {
                History[i][j][k] = (History[i][j][k] * 3) / 4;
            }
        }
    }
    // Decay CaptureHistory
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 6; ++j) {
            for (int k = 0; k < 64; ++k) {
                for (int l = 0; l < 6; ++l) {
                    CaptureHistory[i][j][k][l] = (CaptureHistory[i][j][k][l] * 3) / 4;
                }
            }
        }
    }
    // Decay ContHistory
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 6; ++j) {
            for (int k = 0; k < 64; ++k) {
                for (int l = 0; l < 6; ++l) {
                    for (int m = 0; m < 64; ++m) {
                        ContHistory[i][j][k][l][m] = (ContHistory[i][j][k][l][m] * 3) / 4;
                    }
                }
            }
        }
    }
}

void SearchWorker::update_history(int side, int pt, int to, int bonus) {
    if (side < 0 || side > 1 || pt < 0 || pt > 5 || to < 0 || to > 63) return;
    int& h = History[side][pt][to];
    h += bonus - (h * std::abs(bonus)) / MAX_HISTORY;
}

void SearchWorker::update_capture_history(int side, int pt, int to, int captured_pt, int bonus) {
    if (side < 0 || side > 1 || pt < 0 || pt > 5 || to < 0 || to > 63 || captured_pt < 0 || captured_pt > 5) return;
    int& h = CaptureHistory[side][pt][to][captured_pt];
    h += bonus - (h * std::abs(bonus)) / MAX_HISTORY;
}

void SearchWorker::update_continuation(int side, int prev_pt, int prev_to, int pt, int to, int bonus) {
    if (side < 0 || side > 1 || prev_pt < 0 || prev_pt > 5 || prev_to < 0 || prev_to > 63 || pt < 0 || pt > 5 || to < 0 || to > 63) return;
    int16_t& h = ContHistory[side][prev_pt][prev_to][pt][to];
    h += bonus - (h * std::abs(bonus)) / MAX_HISTORY;
}

void SearchWorker::update_counter_move(int side, int prev_from, int prev_to, uint16_t move) {
    if (side < 0 || side > 1 || prev_from < 0 || prev_from > 63 || prev_to < 0 || prev_to > 63) return;
    int key = (prev_from << 6) | prev_to;
    CounterMove[side][key] = move;
}

void SearchWorker::check_limits() {
    if (stop_flag) return;
    if (nodes_limit_count > 0 && GlobalPool.get_total_nodes() >= nodes_limit_count) {
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

// MovePicker
class MovePicker {
    const Position& pos;
    SearchWorker& worker;
    MoveGen::MoveList list;
    MoveGen::MoveList bad_captures;

    int current_idx = 0;
    int bad_current_idx = 0;

    int scores[256];
    bool is_bad_capture[256];
    int bad_scores[256];

    uint16_t tt_move;
    uint16_t prev_move;
    uint16_t killers[2];
    int ply;
    int stage;
    bool captures_only;
    bool skip_bad_captures;
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
    MovePicker(const Position& p, SearchWorker& w, uint16_t tm, int pl, uint16_t pm)
        : pos(p), worker(w), tt_move(tm), prev_move(pm), ply(pl), stage(STAGE_TT_MOVE), captures_only(false), skip_bad_captures(false), killer_idx(0) {
        if (ply < MAX_PLY) {
            killers[0] = worker.KillerMoves[ply][0];
            killers[1] = worker.KillerMoves[ply][1];
        } else {
            killers[0] = killers[1] = 0;
        }
    }

    MovePicker(const Position& p, SearchWorker& w, bool caps_only, bool skip_bad = false)
        : pos(p), worker(w), tt_move(0), prev_move(0), ply(0), stage(STAGE_GEN_CAPTURES), captures_only(caps_only), skip_bad_captures(skip_bad), killer_idx(0) {
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

            int capture_history = 0;
            if (attacker != NO_PIECE && victim != NO_PIECE) {
                int victim_pt = (flag == 5) ? 0 : (victim % 6);
                int attacker_pt = attacker % 6;
                capture_history = worker.CaptureHistory[pos.side_to_move()][attacker_pt][m & 0x3F][victim_pt];
            } else if (flag == 5) {
                 int victim_pt = 0;
                 int attacker_pt = attacker % 6;
                 capture_history = worker.CaptureHistory[pos.side_to_move()][attacker_pt][m & 0x3F][victim_pt];
            } else if ((flag & 8) && victim != NO_PIECE) {
                int victim_pt = victim % 6;
                int attacker_pt = attacker % 6;
                capture_history = worker.CaptureHistory[pos.side_to_move()][attacker_pt][m & 0x3F][victim_pt];
            }

            if (see_score >= 0) {
                scores[i] = SCORE_GOOD_CAPTURE_BASE + mvv_lva + see_score + capture_history;
                is_bad_capture[i] = false;
            } else {
                scores[i] = SCORE_BAD_CAPTURE_BASE + mvv_lva + see_score + capture_history;
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
                score = worker.History[pos.side_to_move()][pt][t];
                if (prev_pc != NO_PIECE && prev_to != SQ_NONE) {
                    score += worker.ContHistory[pos.side_to_move()][prev_pc % 6][prev_to][pt][t];
                }
                if (prev_move != 0) {
                    Square pf = (Square)((prev_move >> 6) & 0x3F);
                    Square pt_sq = (Square)(prev_move & 0x3F);
                    if (worker.CounterMove[pos.side_to_move()][(pf << 6) | pt_sq] == m) {
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
        while (true) {
            switch (stage) {
                case STAGE_TT_MOVE:
                    stage = STAGE_GEN_CAPTURES;
                    if (tt_move != 0) return tt_move;
                    break;

                case STAGE_GEN_CAPTURES: {
                    MoveGen::generate_captures(pos, list);
                    score_captures();
                    bad_captures.count = 0;
                    int good_count = 0;
                    for (int i = 0; i < list.count; i++) {
                        if (is_bad_capture[i]) {
                            bad_captures.add(list.moves[i]);
                            bad_scores[bad_captures.count - 1] = scores[i];
                        } else {
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
                    uint16_t m = pick_best(-2000000000);
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
                                if (is_cap) { continue; }
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
                    if (m == 0) { stage = STAGE_BAD_CAPTURES; break; }
                    if (m == tt_move) continue;
                    if (m == killers[0] || m == killers[1]) continue;
                    return m;
                }
                case STAGE_BAD_CAPTURES: {
                    if (skip_bad_captures) { stage = STAGE_FINISHED; break; }
                    uint16_t m = pick_best_bad();
                    if (m == 0) { stage = STAGE_FINISHED; break; }
                    if (m == tt_move) continue;
                    if (m == killers[0] || m == killers[1]) continue;
                    return m;
                }
                case STAGE_FINISHED:
                    return 0;
            }
        }
    }
};

int SearchWorker::quiescence(Position& pos, int alpha, int beta, int ply) {
    if ((node_count & 1023) == 0 && thread_id == 0) check_limits();
    if (stop_flag) return 0;
    node_count++;

    if (ply >= MAX_PLY - 1) return Eval::evaluate(pos);

    bool in_check = pos.in_check();

    if (!in_check) {
        int stand_pat = Eval::evaluate_light(pos);
        if (stand_pat >= beta) return beta;
        const int DELTA_MARGIN = 975;
        if (stand_pat < alpha - DELTA_MARGIN) return alpha;
        if (alpha < stand_pat) alpha = stand_pat;
    }

    MovePicker mp(pos, *this, true, !in_check);
    uint16_t move;
    int moves_searched = 0;

    while ((move = mp.next())) {
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

int SearchWorker::negamax(Position& pos, int depth, int alpha, int beta, int ply, bool null_allowed, uint16_t prev_move, uint16_t excluded_move) {
    if ((node_count & 1023) == 0 && thread_id == 0) check_limits();
    // Workers also need to check stop flag regularly!
    if (thread_id != 0 && (node_count & 1023) == 0 && stop_flag) return 0;

    if (stop_flag) return 0;
    node_count++;

    if (ply >= MAX_PLY - 1) return Eval::evaluate(pos);
    if (ply > 0 && (pos.rule50_count() >= 100 || pos.is_repetition())) return 0;

    int original_alpha = alpha;
    int mate_val = MATE_SCORE - ply;
    if (alpha < -mate_val) alpha = -mate_val;
    if (beta > mate_val - 1) beta = mate_val - 1;
    if (alpha >= beta) return alpha;

    bool in_check = pos.in_check();
    bool is_pv = (beta - alpha > 1);
    if (in_check) depth++;

    if (depth <= 0) return quiescence(pos, alpha, beta, ply);

    int static_eval = Eval::evaluate(pos);

    if (!is_pv && !in_check && depth <= 2) {
        int razor_margin = (depth == 1) ? 150 : 250;
        if (static_eval + razor_margin < alpha) {
             int v = quiescence(pos, alpha, beta, ply);
             if (v < alpha) return alpha;
        }
    }

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

    if (!in_check) {
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

        if (Search::UseNMP && null_allowed && depth >= 3 && static_eval >= beta && !in_check) {
            if (pos.non_pawn_material(pos.side_to_move()) >= 330) {
                int reduction = 2 + (depth >= 8 ? 1 : 0);
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
    }

    MovePicker mp(pos, *this, tt_move, ply, prev_move);
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

        if (!in_check && is_quiet && depth < 6 && static_eval + 150 * depth <= alpha) continue;
        if (is_quiet && !in_check && !is_pv && depth <= 4) {
             int fut_margin = 100 * depth + 50;
             if (static_eval + fut_margin <= alpha) continue;
        }

        int see_score = 200000;
        bool is_promo = (move >> 12) & 8;
        if (is_cap && !in_check && !is_promo) {
             if (depth <= 5) see_score = see(pos, move);
             if (depth >= 4 && depth <= 5) {
                  int margin = (depth - 1) * -50;
                  if (see_score < margin) continue;
             }
        }

        pos.make_move(move);
        if (pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, ~pos.side_to_move())), pos.side_to_move())) {
            pos.unmake_move(move);
            continue;
        }

        bool gives_check = pos.in_check();
        if (depth <= 3 && is_cap && !is_promo && !in_check && !gives_check) {
             if (see_score < 0) {
                 pos.unmake_move(move);
                 continue;
             }
        }

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
            if (ply < MAX_PLY) {
                bool is_capture = ((flag & 4) || (flag == 5) || (flag & 8));
                int depth_bonus = depth * depth;
                if (depth_bonus > 400) depth_bonus = 400;
                if (score < beta) {
                    if (is_capture) {
                        Piece victim = pos.piece_on(to_sq);
                        if (flag == 5) victim = (pos.side_to_move() == WHITE) ? B_PAWN : W_PAWN;
                        if (victim != NO_PIECE || (flag == 5)) {
                             int victim_pt = (flag == 5) ? 0 : (victim % 6);
                             int attacker_pt = moved_piece % 6;
                             update_capture_history(pos.side_to_move(), attacker_pt, to_sq, victim_pt, depth_bonus / 2);
                        }
                    }
                }
            }
            if (alpha >= beta) {
                if (ply < MAX_PLY) {
                     bool is_capture = ((flag & 4) || (flag == 5) || (flag & 8));
                     int depth_bonus = depth * depth;
                     if (depth_bonus > 400) depth_bonus = 400;
                     if (!is_capture) {
                         KillerMoves[ply][1] = KillerMoves[ply][0];
                         KillerMoves[ply][0] = move;
                         Square f = (Square)((move >> 6) & 0x3F);
                         Square t = (Square)(move & 0x3F);
                         Piece pc = pos.piece_on(f);
                         int pt = pc % 6;
                         if (Search::UseHistory) {
                             update_history(pos.side_to_move(), pt, t, depth_bonus);
                             if (prev_move != 0) {
                                 Square prev_to = (Square)(prev_move & 0x3F);
                                 Piece prev_pc = pos.piece_on(prev_to);
                                 if (prev_pc != NO_PIECE) {
                                     update_continuation(pos.side_to_move(), prev_pc % 6, prev_to, pt, t, depth_bonus);
                                 }
                                 Square prev_from = (Square)((prev_move >> 6) & 0x3F);
                                 update_counter_move(pos.side_to_move(), prev_from, prev_to, move);
                             }
                             for (int i=0; i<tried_quiets_cnt; i++) {
                                 uint16_t bad_move = tried_quiets[i];
                                 if (bad_move != move) {
                                     Square bf = (Square)((bad_move >> 6) & 0x3F);
                                     Square bt = (Square)(bad_move & 0x3F);
                                     Piece bpc = pos.piece_on(bf);
                                     update_history(pos.side_to_move(), bpc % 6, bt, -depth_bonus);
                                     if (prev_move != 0) {
                                         Square prev_to = (Square)(prev_move & 0x3F);
                                         Piece prev_pc = pos.piece_on(prev_to);
                                         if (prev_pc != NO_PIECE) {
                                             update_continuation(pos.side_to_move(), prev_pc % 6, prev_to, bpc % 6, bt, -depth_bonus);
                                         }
                                     }
                                 }
                             }
                         }
                     } else {
                         Piece victim = pos.piece_on(to_sq);
                         if (flag == 5) victim = (pos.side_to_move() == WHITE) ? B_PAWN : W_PAWN;
                         if (victim != NO_PIECE || flag == 5) {
                             int victim_pt = (flag == 5) ? 0 : (victim % 6);
                             int attacker_pt = moved_piece % 6;
                             update_capture_history(pos.side_to_move(), attacker_pt, to_sq, victim_pt, depth_bonus);
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

struct RootMove {
    uint16_t move;
    int score;
};

bool compare_root_moves(const RootMove& a, const RootMove& b) {
    return a.score > b.score;
}

void SearchWorker::iter_deep() {
    int max_depth = limits.depth > 0 ? limits.depth : MAX_PLY;
    int prev_score = 0;
    Position& pos = root_pos;

    // Generate Root Moves Once
    std::vector<RootMove> root_moves;
    MoveGen::MoveList ml;
    MoveGen::generate_all(pos, ml);
    for (int i = 0; i < ml.count; ++i) {
        uint16_t m = ml.moves[i];
        pos.make_move(m);
        bool legal = !pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, ~pos.side_to_move())), pos.side_to_move());
        pos.unmake_move(m);
        if (legal) {
            root_moves.push_back({m, -INFINITY_SCORE});
        }
    }

    TTEntry tte;
    if (TTable.probe(pos.key(), tte)) {
        for (auto& rm : root_moves) {
            if (rm.move == tte.move) {
                rm.score = INFINITY_SCORE;
                break;
            }
        }
    }
    std::stable_sort(root_moves.begin(), root_moves.end(), compare_root_moves);

    for (int depth = 1; depth <= max_depth; depth++) {

        // Sort based on previous iteration scores
        if (depth > 1) {
            std::stable_sort(root_moves.begin(), root_moves.end(), compare_root_moves);
        }

        int best_score = -INFINITY_SCORE;
        int alpha = -INFINITY_SCORE;
        int beta = INFINITY_SCORE;

        bool use_aspiration = (thread_id == 0 && depth >= 2);
        int delta = 15;

        if (use_aspiration) {
             alpha = std::max(-INFINITY_SCORE, prev_score - delta);
             beta = std::min(INFINITY_SCORE, prev_score + delta);
        }

        while (true) {
            if (stop_flag) break;

            // Snapshot the window for aspiration comparisons.
            const int alpha0 = alpha;
            const int beta0  = beta;

            // Working alpha for PVS updates inside this root search.
            int a = alpha;

            best_score = -INFINITY_SCORE;

            for (size_t i = 0; i < root_moves.size(); ++i) {
                if (thread_id != 0) {
                    if ((int)i % OptThreads != thread_id) continue;
                }

                uint16_t move = root_moves[i].move;
                pos.make_move(move);

                int val;
                if (i == 0 && thread_id == 0) {
                    // Full-window search for the first move.
                    val = -negamax(pos, depth - 1, -beta, -a, 1, true, move, 0);
                } else {
                    // Null-window PVS search for later moves.
                    val = -negamax(pos, depth - 1, -a - 1, -a, 1, true, move, 0);
                    if (val > a && val < beta) {
                        val = -negamax(pos, depth - 1, -beta, -a, 1, true, move, 0);
                    }
                }

                pos.unmake_move(move);
                if (stop_flag) break;

                root_moves[i].score = val;

                if (val > best_score) best_score = val;
                if (val > a) a = val;

                // Beta cutoff at root (master only)
                if (a >= beta && thread_id == 0) break;
            }

            if (stop_flag) break;

            if (!use_aspiration) {
                // Window is good: accept result
                break;
            }

            // Aspiration checks MUST compare against the original window (alpha0/beta0),
            // not the mutated working alpha.
            if (best_score <= alpha0) {
                // Fail-low: widen the window and retry
                delta = delta + delta / 2; // or delta *= 2;
                alpha = std::max(-INFINITY_SCORE, best_score - delta);
                beta  = std::min(INFINITY_SCORE, best_score + delta);

                if (delta > 2000) {
                    alpha = -INFINITY_SCORE;
                    beta  = INFINITY_SCORE;
                    use_aspiration = false;
                }
                continue;
            }

            if (best_score >= beta0) {
                // Fail-high: widen the window and retry
                delta = delta + delta / 2; // or delta *= 2;
                alpha = std::max(-INFINITY_SCORE, best_score - delta);
                beta  = std::min(INFINITY_SCORE, best_score + delta);

                if (delta > 2000) {
                    alpha = -INFINITY_SCORE;
                    beta  = INFINITY_SCORE;
                    use_aspiration = false;
                }
                continue;
            }

            // Window is good: accept result
            break;
        }

        if (stop_flag) break;

        // Reporting (Master Only)
        if (thread_id == 0) {
            auto now = steady_clock::now();
            long long ms = duration_cast<milliseconds>(now - start_time).count();
            long long us = duration_cast<microseconds>(now - start_time).count();

            uint16_t best_move = 0;
            int best_val = -INFINITY_SCORE;
            for (const auto& rm : root_moves) {
                if (rm.score > best_val) {
                    best_val = rm.score;
                    best_move = rm.move;
                }
            }

            // Use the best root value for reporting/storing (never a stale 'score').
            const int display_score = best_val;
            prev_score = best_val;

            std::string pv_str = "";
            if (best_move != 0) {
                 pv_str += move_to_uci(best_move) + " ";
                 Position temp_pos = pos;
                 temp_pos.make_move(best_move);
                 int pv_depth = 0;
                 std::vector<Key> pv_keys;
                 pv_keys.push_back(temp_pos.key());
                 while (pv_depth < depth - 1) {
                     TTEntry tte;
                     if (!TTable.probe(temp_pos.key(), tte) || tte.move == 0) break;
                     MoveGen::MoveList ml;
                     MoveGen::generate_all(temp_pos, ml);
                     bool found = false;
                     for (int i = 0; i < ml.count; ++i) if (ml.moves[i] == tte.move) found = true;
                     if (!found) break;
                     temp_pos.make_move(tte.move);
                     if (temp_pos.is_attacked((Square)Bitboards::lsb(temp_pos.pieces(KING, ~temp_pos.side_to_move())), temp_pos.side_to_move())) {
                         temp_pos.unmake_move(tte.move);
                         break;
                     }
                     pv_str += move_to_uci(tte.move) + " ";
                     pv_depth++;
                     bool cycled = false;
                     for (Key k : pv_keys) if (k == temp_pos.key()) cycled = true;
                     if (cycled) break;
                     pv_keys.push_back(temp_pos.key());
                 }
            }

            std::string score_str;
            if (std::abs(display_score) > 30000) {
                int mate_in_moves = (MATE_SCORE - std::abs(display_score) + 1) / 2;
                if (display_score < 0) score_str = "mate -" + std::to_string(mate_in_moves);
                else score_str = "mate " + std::to_string(mate_in_moves);
            } else {
                score_str = "cp " + std::to_string(display_score);
            }

            if (!limits.silent) {
                std::cout << "info depth " << depth
                          << " score " << score_str
                          << " nodes " << GlobalPool.get_total_nodes()
                          << " time " << ms
                          << " nps " << (us > 0 ? (GlobalPool.get_total_nodes() * 1000000LL / us) : 0)
                          << " pv " << pv_str << "\n";
            }

            TTable.store(pos.key(), best_move, score_to_tt(display_score, 0), Eval::evaluate(pos), depth, 3);
        }
    }

    if (thread_id == 0 && !limits.silent) {
        uint16_t best_move = 0;
        int best_val = -INFINITY_SCORE;
        for (const auto& rm : root_moves) {
            if (rm.score > best_val) {
                best_val = rm.score;
                best_move = rm.move;
            }
        }
        std::cout << "bestmove " << move_to_uci(best_move) << "\n" << std::flush;
    }
}

// ThreadPool Implementation

ThreadPool GlobalPool;

void ThreadPool::init(int thread_count) {
    // thread_count is Total Threads. Workers = thread_count - 1.

    // Clear existing workers
    for (auto* w : workers) {
        delete w;
    }
    workers.clear();

    if (master) {
        delete master;
        master = nullptr;
    }

    master = new SearchWorker(0);

    for (int i = 1; i < thread_count; i++) {
        workers.push_back(new SearchWorker(i));
    }
}

void ThreadPool::start_search(const Position& pos, const SearchLimits& limits) {
    if (master) master->start_search(pos, limits);
    for (auto* w : workers) {
        w->start_search(pos, limits);
    }
}

void ThreadPool::wait_for_completion() {
    for (auto* w : workers) {
        w->wait_for_completion();
    }
}

long long ThreadPool::get_total_nodes() const {
    long long total = 0;
    if (master) total += master->get_nodes();
    for (auto* w : workers) {
        total += w->get_nodes();
    }
    return total;
}

ThreadPool::~ThreadPool() {
    for (auto* w : workers) {
        delete w;
    }
    if (master) delete master;
}

// Search Static API

void Search::start(Position& pos, const SearchLimits& limits) {
    static bool init = false;
    if (!init) {
        init_lmr();
        init = true;
    }

    // Initialize/Resize Pool if OptThreads changed
    // We check if (Workers + 1) == OptThreads, AND if master is initialized.
    if (!GlobalPool.master || (int)GlobalPool.workers.size() + 1 != OptThreads) {
        GlobalPool.init(OptThreads);
    }

    Search::UseNMP = limits.use_nmp;
    Search::UseProbCut = limits.use_probcut;
    Search::UseSingular = limits.use_singular;
    Search::UseHistory = limits.use_history;

    stop_flag = false;
    start_time = steady_clock::now();
    nodes_limit_count = limits.nodes;

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

    // Start Workers (including Master state init)
    GlobalPool.start_search(pos, limits);

    // Run Master Search Loop
    GlobalPool.master->search_loop();

    // Master returned (finished or time up). Signal stop.
    stop_flag = true;

    // Wait for workers to stop
    GlobalPool.wait_for_completion();
}

void Search::stop() {
    stop_flag = true;
}

void Search::clear() {
    TTable.clear();
    // Use public access via ThreadPool if possible, or friend.
    // ThreadPool has master and workers public.
    if (GlobalPool.master) GlobalPool.master->clear_history();
    for (auto* w : GlobalPool.workers) {
        w->clear_history();
    }
}

long long Search::get_node_count() {
    return GlobalPool.get_total_nodes();
}

// Helpers for Search::* static history updates (removed)
void Search::update_history(int, int, int, int) {}
void Search::update_capture_history(int, int, int, int, int) {}
void Search::update_continuation(int, int, int, int, int, int) {}
void Search::update_counter_move(int, int, int, uint16_t) {}
void Search::clear_for_new_search() {}
