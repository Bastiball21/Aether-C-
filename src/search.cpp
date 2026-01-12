#include "worker.h"
#include "eval/eval.h"
#include "movegen.h"
#include "tt.h"
#include "see.h"
#include "syzygy.h"
#include "search_params.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <cstring>
#include <cmath>

using namespace std::chrono;

// ----------------------------------------------------------------------------
// Globals & Constants
// ----------------------------------------------------------------------------

std::atomic<bool> stop_flag(false);
int64_t allocated_time_limit = 0;
int64_t nodes_limit_count = 0;
steady_clock::time_point start_time;

// LMR Table
int LMRTable[64][64];
bool lmr_init = false;

void init_lmr() {
    if (lmr_init) return;
    for (int d = 0; d < 64; d++) {
        for (int m = 0; m < 64; m++) {
            if (d < 3 || m < 2) LMRTable[d][m] = 0;
            else {
                // Tuned LMR formula
                LMRTable[d][m] = (int)(SearchParams::LMR_BASE + log(d) * log(m) / SearchParams::LMR_DIVISOR);
            }
        }
    }
    lmr_init = true;
}

// Search Options
bool Search::UseNMP = true;
bool Search::UseProbCut = true;
bool Search::UseSingular = true;
bool Search::UseHistory = true;

const int INFINITY_SCORE = 32000;
const int MATE_SCORE = 31000;
const int MAX_HISTORY = SearchParams::HISTORY_MAX;

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

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

    if (flag & 8) {
        int p = (flag & 3); // 0=N, 1=B, 2=R, 3=Q
        char pchar = 'q';
        if (p == 0) pchar = 'n';
        if (p == 1) pchar = 'b';
        if (p == 2) pchar = 'r';
        s += pchar;
    }
    return s;
}

// ----------------------------------------------------------------------------
// MovePicker
// ----------------------------------------------------------------------------

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
            if (flag == 5) victim_val = 1; // En Passant
            else if (victim != NO_PIECE) {
                static const int val[] = {1, 3, 3, 5, 9, 0}; // P,N,B,R,Q,K
                victim_val = val[victim % 6];
            }
            if (flag & 8) { // Promotion
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
            if (Search::UseHistory) {
                int v_pt = (flag == 5) ? 0 : ((victim != NO_PIECE) ? victim % 6 : 0);
                int a_pt = attacker % 6;
                capture_history = worker.CaptureHistory[pos.side_to_move()][a_pt][m & 0x3F][v_pt];
            }

            if (see_score >= 0) {
                // Use MVV-LVA primarily for sorting good captures
                scores[i] = SCORE_GOOD_CAPTURE_BASE + mvv_lva * 1024 + capture_history;
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
                    if (tt_move != 0 && MoveGen::is_pseudo_legal(pos, tt_move)) return tt_move;
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
                                if (!is_cap) return m;
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

// ----------------------------------------------------------------------------
// SearchWorker Implementation
// ----------------------------------------------------------------------------

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
    if (thread_id != 0 && !worker_thread.joinable()) {
        worker_thread = std::thread(&SearchWorker::search_loop, this);
    } else if (thread_id != 0) {
        cv.notify_one();
    }
}

void SearchWorker::wait_for_completion() {
    if (thread_id == 0) return;
    while (searching) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void SearchWorker::stop() {}

void SearchWorker::search_loop() {
    if (thread_id == 0) {
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

        node_count = 0;
        decay_history();
        lk.unlock();

        iter_deep();

        lk.lock();
        searching = false;
    }
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

void SearchWorker::clear_history() {
    std::memset(History, 0, sizeof(History));
    std::memset(CaptureHistory, 0, sizeof(CaptureHistory));
    std::memset(ContHistory, 0, sizeof(ContHistory));
    std::memset(CounterMove, 0, sizeof(CounterMove));
    std::memset(KillerMoves, 0, sizeof(KillerMoves));
}

void SearchWorker::decay_history() {
    // Decay History (1/16 decay factor)
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 6; ++j) {
            for (int k = 0; k < 64; ++k) {
                History[i][j][k] = (History[i][j][k] * (SearchParams::HISTORY_DECAY - 1)) / SearchParams::HISTORY_DECAY;
                for (int l = 0; l < 6; ++l) CaptureHistory[i][j][k][l] = (CaptureHistory[i][j][k][l] * (SearchParams::HISTORY_DECAY - 1)) / SearchParams::HISTORY_DECAY;
            }
        }
    }
}

void SearchWorker::update_history(int side, int pt, int to, int bonus) {
    if (std::abs(bonus) > 1200) bonus = (bonus > 0) ? 1200 : -1200;
    int& h = History[side][pt][to];
    h += bonus - (h * std::abs(bonus)) / MAX_HISTORY;
}

void SearchWorker::update_capture_history(int side, int pt, int to, int captured_pt, int bonus) {
    if (std::abs(bonus) > 1200) bonus = (bonus > 0) ? 1200 : -1200;
    int& h = CaptureHistory[side][pt][to][captured_pt];
    h += bonus - (h * std::abs(bonus)) / MAX_HISTORY;
}

void SearchWorker::update_continuation(int side, int prev_pt, int prev_to, int pt, int to, int bonus) {
    if (std::abs(bonus) > 1200) bonus = (bonus > 0) ? 1200 : -1200;
    int16_t& h = ContHistory[side][prev_pt][prev_to][pt][to];
    h += bonus - (h * std::abs(bonus)) / MAX_HISTORY;
}

void SearchWorker::update_counter_move(int side, int prev_from, int prev_to, uint16_t move) {
    int key = (prev_from << 6) | prev_to;
    CounterMove[side][key] = move;
}

// ----------------------------------------------------------------------------
// Search Algorithms
// ----------------------------------------------------------------------------

int SearchWorker::quiescence(Position& pos, int alpha, int beta, int ply) {
    if ((node_count & 1023) == 0 && thread_id == 0) check_limits();
    if (stop_flag) return 0;
    node_count++;

    if (ply >= MAX_PLY - 1) return Eval::evaluate(pos);

    bool in_check = pos.in_check();
    int stand_pat = -INFINITY_SCORE;

    if (!in_check) {
        stand_pat = Eval::evaluate_light(pos);
        if (stand_pat >= beta) return beta;

        // Delta Pruning
        const int DELTA = SearchParams::DELTA_MARGIN;
        if (stand_pat < alpha - DELTA) {
             // Promos? We should check if we have promos.
             // If stand_pat is VERY low, even a queen capture won't help.
             return alpha;
        }

        if (stand_pat > alpha) alpha = stand_pat;
    }

    // Syzygy TB Probe in QSearch? Usually only in main search.
    // But if we are in endgame, maybe?
    // Usually Syzygy is probed in main search loop.

    MovePicker mp(pos, *this, !in_check, !in_check);
    uint16_t move;
    int moves_searched = 0;

    while ((move = mp.next())) {
        if (pos.piece_on((Square)((move >> 6) & 0x3F)) == NO_PIECE) continue;

        // Futility / SEE pruning in QSearch
        // Only if not in check
        if (!in_check) {
             // SEE check for captures
             int see_val = see(pos, move);
             if (see_val < 0) continue;
        }

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
    if (thread_id != 0 && (node_count & 1023) == 0 && stop_flag) return 0;
    if (stop_flag) return 0;

    node_count++;
    int original_alpha = alpha;

    // Mate Distance Pruning
    int mate_val = MATE_SCORE - ply;
    if (alpha < -mate_val) alpha = -mate_val;
    if (beta > mate_val - 1) beta = mate_val - 1;
    if (alpha >= beta) return alpha;

    if (ply >= MAX_PLY - 1) return Eval::evaluate(pos);
    // Draw Detection
    if (ply > 0 && (pos.rule50_count() >= 100 || pos.is_repetition())) return 0;

    bool is_pv = (beta - alpha > 1);
    bool in_check = pos.in_check();

    // Check Extension
    if (in_check) depth++;

    if (depth <= 0) return quiescence(pos, alpha, beta, ply);

    // Syzygy Probing
    if (Syzygy::enabled() && !excluded_move) {
         int tb_score;
         if (Syzygy::probe_wdl(pos, tb_score, ply)) {
             // If we get a valid TB score, we can use it.
             // TB return is like mate score.
             if (tb_score > 0) { // Win
                 if (tb_score >= beta) return tb_score;
                 if (tb_score > alpha) alpha = tb_score;
             } else if (tb_score < 0) { // Loss
                 if (tb_score <= alpha) return tb_score;
                 if (tb_score < beta) beta = tb_score;
             } else { // Draw
                 if (0 <= alpha) return 0;
                 if (0 >= beta) return 0;
                 alpha = 0; beta = 0; // Exact 0
                 return 0;
             }
         }
    }

    // TT Probe
    TTEntry tte;
    uint16_t tt_move = 0;
    bool tt_hit = TTable.probe(pos.key(), tte);

    if (tt_hit) {
        tt_move = tte.move;
        int tt_score = score_from_tt(tte.score, ply);
        if (!is_pv && tte.depth >= depth && excluded_move == 0) {
            if (tte.bound() == 1) return tt_score; // Exact
            if (tte.bound() == 2 && tt_score <= alpha) return alpha; // Upper
            if (tte.bound() == 3 && tt_score >= beta) return tt_score; // Lower
        }
    }

    // IID (Internal Iterative Deepening)
    if (depth >= 5 && tt_move == 0 && is_pv) {
        // Reduced depth search to find a move
        negamax(pos, depth - 2, alpha, beta, ply, false, prev_move, 0);
        if (TTable.probe(pos.key(), tte)) {
            tt_move = tte.move;
            tt_hit = true;
        }
    }

    int static_eval = Eval::evaluate(pos);

    // Singular Extensions
    int singular_ext = 0;
    // Modified: removed !is_pv to allow SE in PV nodes
    if (Search::UseSingular && depth >= 8 && tt_move != 0 && tte.bound() == 3 && tte.depth >= depth - 3 && excluded_move == 0) {
        int tt_score = score_from_tt(tte.score, ply);
        int margin = SearchParams::SINGULAR_MARGIN * depth;
        int singular_beta = tt_score - margin;
        int score = negamax(pos, (depth - 1) / 2, singular_beta - 1, singular_beta, ply, false, prev_move, tt_move);
        if (score < singular_beta) {
             singular_ext = 1;
        }
    }

    // Pruning (Non-PV)
    if (!is_pv && !in_check) {
        // Reverse Futility (Static Null Move)
        if (depth <= 7 && static_eval - SearchParams::RFP_MARGIN * depth >= beta) return static_eval;

        // Null Move Pruning
        if (Search::UseNMP && null_allowed && depth >= SearchParams::NMP_DEPTH_LIMIT && static_eval >= beta && pos.non_pawn_material(pos.side_to_move()) >= 300) {
            int R = SearchParams::NMP_BASE_REDUCTION + depth / SearchParams::NMP_DIVISOR;
            pos.make_null_move();
            int score = -negamax(pos, depth - R, -beta, -beta + 1, ply + 1, false, 0, 0);
            pos.unmake_null_move();
            if (stop_flag) return 0;
            if (score >= beta) {
                // Modified: Verification search for deep null moves
                if (depth >= 12 && score < MATE_SCORE - MAX_PLY) {
                    score = -negamax(pos, depth - R, -beta, -beta + 1, ply, false, prev_move, 0);
                    if (score < beta) goto skip_nmp;
                }
                return beta;
            }
        }
    }
    skip_nmp:

        // Razoring (depth <= 2)
        if (depth <= 2 && static_eval + SearchParams::RAZORING_MARGIN < alpha) {
             int qscore = quiescence(pos, alpha, beta, ply);
             if (qscore < alpha) return alpha;
        }


    MovePicker mp(pos, *this, tt_move, ply, prev_move);
    uint16_t move;
    int moves_searched = 0;
    int best_score = -INFINITY_SCORE;
    uint16_t best_move = 0;

    // PV Search Loop
    while ((move = mp.next())) {
        if (move == excluded_move) continue;
        if (pos.piece_on((Square)((move >> 6) & 0x3F)) == NO_PIECE) continue; // Safety

        bool is_cap = ((move >> 12) & 4) || ((move >> 12) == 5) || ((move >> 12) & 8);
        bool is_quiet = !is_cap;
        bool is_killer = (move == KillerMoves[ply][0] || move == KillerMoves[ply][1]);

        // Late Move Pruning (Quiet only)
        if (!is_pv && !in_check && is_quiet) {
            int lmp_threshold = (3 + depth * depth) / (2 - (depth > 5));
            if (moves_searched > lmp_threshold) break;
        }

        // Futility Pruning (Quiet only)
        if (!is_pv && !in_check && is_quiet && depth < 5 && moves_searched > 0) {
             int fmargin = SearchParams::FUTILITY_MARGIN * depth;
             if (static_eval + fmargin <= alpha) break; // Prune remaining quiets
        }

        pos.make_move(move);
        if (pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, ~pos.side_to_move())), pos.side_to_move())) {
            pos.unmake_move(move);
            continue;
        }

        moves_searched++;
        bool gives_check = pos.in_check(); // Added check status after move

        int score;
        if (moves_searched == 1) {
            score = -negamax(pos, depth - 1 + singular_ext, -beta, -alpha, ply + 1, true, move, 0);
        } else {
            // LMR
            int reduction = 0;
            // Modified: Don't reduce if move gives check
            if (depth >= 3 && moves_searched > 1 && !in_check && is_quiet && !gives_check) {
                int d = std::min(depth, 63);
                int m = std::min(moves_searched, 63);
                reduction = LMRTable[d][m];
                if (is_killer) reduction -= 1;
                if (!is_pv) reduction += 1;
                // Reduce less if we have good history
                // if (history > ...) reduction--;
                if (reduction < 0) reduction = 0;
            }

            score = -negamax(pos, depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, true, move, 0);

            if (score > alpha && reduction > 0) {
                 score = -negamax(pos, depth - 1 + singular_ext, -alpha - 1, -alpha, ply + 1, true, move, 0);
            }
            if (score > alpha && score < beta) {
                 score = -negamax(pos, depth - 1 + singular_ext, -beta, -alpha, ply + 1, true, move, 0);
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
            if (score >= beta) { // Cutoff
                if (is_quiet) {
                    KillerMoves[ply][1] = KillerMoves[ply][0];
                    KillerMoves[ply][0] = move;
                    int bonus = depth * depth;
                    if (bonus > 400) bonus = 400;
                    Square f = (Square)((move >> 6) & 0x3F);
                    Square t = (Square)(move & 0x3F);
                    Piece pc = pos.piece_on(f);
                    int pt = pc % 6;
                    update_history(pos.side_to_move(), pt, t, bonus);
                    // update counter, cont...
                }
                break;
            }
        }
    }

    if (moves_searched == 0) {
        if (in_check) return -MATE_SCORE + ply;
        return 0; // Stalemate
    }

    int bound = (best_score >= beta) ? 3 : ((best_score > original_alpha) ? 1 : 2); // 3=Lower, 1=Exact, 2=Upper
    TTable.store(pos.key(), best_move, score_to_tt(best_score, ply), static_eval, depth, bound);

    return best_score;
}

// ----------------------------------------------------------------------------
// Iterative Deepening
// ----------------------------------------------------------------------------

// Helper to retrieve PV from TT
std::string get_pv(Position& pos, uint16_t root_move) {
    std::string res = "";
    std::vector<uint16_t> moves;
    std::vector<uint64_t> hashes;

    uint16_t m = root_move;
    int limit = 0;

    while (limit < 64 && m != 0) {
        if (!MoveGen::is_pseudo_legal(pos, m)) break;

        pos.make_move(m);
        // Verify legality
        if (pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, ~pos.side_to_move())), pos.side_to_move())) {
            pos.unmake_move(m);
            break;
        }

        // Cycle check
        bool cycle = false;
        for (auto h : hashes) if (h == pos.key()) cycle = true;
        if (cycle) {
            pos.unmake_move(m);
            break;
        }

        hashes.push_back(pos.key());
        moves.push_back(m);
        res += move_to_uci(m) + " ";
        limit++;

        TTEntry tte;
        if (TTable.probe(pos.key(), tte)) {
            m = tte.move;
        } else {
            m = 0;
        }
    }

    // Unmake
    while (!moves.empty()) {
        pos.unmake_move(moves.back());
        moves.pop_back();
    }

    if (!res.empty()) res.pop_back();
    return res;
}

struct RootMove {
    uint16_t move;
    int score;
    int prev_score;
};

void SearchWorker::iter_deep() {
    Position& pos = root_pos;

    // Syzygy Root Probe (Master Only)
    if (thread_id == 0 && Syzygy::enabled()) {
        uint16_t tb_move = 0;
        int tb_score = 0;
        if (Syzygy::probe_root(pos, tb_move, tb_score)) {
             // Found a winning/losing/drawing move from TB
             std::string score_str = "cp " + std::to_string(tb_score);
             if (std::abs(tb_score) > 30000) {
                 int mate = (MATE_SCORE - std::abs(tb_score) + 1) / 2;
                 score_str = "mate " + std::to_string(tb_score > 0 ? mate : -mate);
             }

             std::cout << "info depth 1 score " << score_str << " nodes 0 time 0 pv " << get_pv(pos, tb_move) << "\n";
             std::cout << "bestmove " << move_to_uci(tb_move) << "\n";
             return;
        }
    }

    // 1. Generate Root Moves
    MoveGen::MoveList ml;
    MoveGen::generate_all(pos, ml);
    std::vector<RootMove> root_moves;
    for (int i = 0; i < ml.count; ++i) {
        uint16_t m = ml.moves[i];
        if (MoveGen::is_pseudo_legal(pos, m)) {
            pos.make_move(m);
            bool legal = !pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, ~pos.side_to_move())), pos.side_to_move());
            pos.unmake_move(m);
            if (legal) {
                root_moves.push_back({m, -INFINITY_SCORE, -INFINITY_SCORE});
            }
        }
    }

    // Modified: Check if empty
    if (root_moves.empty()) {
        return;
    }

    // 2. Iterative Loop
    int max_depth = limits.depth > 0 ? limits.depth : MAX_PLY;
    int best_val = -INFINITY_SCORE;
    uint16_t best_move = 0;

    for (int depth = 1; depth <= max_depth; depth++) {
        if (stop_flag) break;

        // Sorting
        std::stable_sort(root_moves.begin(), root_moves.end(), [](const RootMove& a, const RootMove& b) {
            return a.score > b.score;
        });

        int alpha = -INFINITY_SCORE;
        int beta = INFINITY_SCORE;

        // Aspiration Windows
        int delta = 20;
        if (depth >= 5) {
            alpha = std::max(-INFINITY_SCORE, best_val - delta);
            beta = std::min(INFINITY_SCORE, best_val + delta);
        }

        while (true) {
            if (stop_flag) break;

            int score_max = -INFINITY_SCORE;
            int best_move_idx = -1;

            for (size_t i = 0; i < root_moves.size(); i++) {
                 // Smp distribution
                 if (thread_id != 0 && ((int)i % OptThreads != thread_id)) continue;

                 uint16_t m = root_moves[i].move;
                 pos.make_move(m);

                 int score;
                 if (i == 0 && thread_id == 0) {
                     score = -negamax(pos, depth - 1, -beta, -alpha, 1, true, m);
                 } else {
                     score = -negamax(pos, depth - 1, -alpha - 1, -alpha, 1, true, m);
                     if (score > alpha && score < beta) {
                         score = -negamax(pos, depth - 1, -beta, -alpha, 1, true, m);
                     }
                 }

                 pos.unmake_move(m);
                 if (stop_flag) break;

                 root_moves[i].score = score;
                 if (score > score_max) {
                     score_max = score;
                     best_move_idx = i;
                 }

                 if (score > alpha) alpha = score;
                 if (score >= beta) break;
            }

            if (stop_flag) break;

            // Check Aspiration Bounds (Master only usually, but let's sync)
            if (thread_id == 0) {
                if (score_max <= alpha && delta < 2000) {
                    beta = (alpha + beta) / 2;
                    alpha = std::max(-INFINITY_SCORE, alpha - delta);
                    delta *= 1.5;
                    continue;
                }
                if (score_max >= beta && delta < 2000) {
                    alpha = (alpha + beta) / 2;
                    beta = std::min(INFINITY_SCORE, beta + delta);
                    delta *= 1.5;
                    continue;
                }
                best_val = score_max;
                if (best_move_idx != -1) best_move = root_moves[best_move_idx].move;
            }
            break;
        }

        if (stop_flag) break;

        // Output Info (Master)
        if (thread_id == 0) {
             auto now = steady_clock::now();
             long long ms = duration_cast<milliseconds>(now - start_time).count();
             long long us = duration_cast<microseconds>(now - start_time).count();
             long long nps = (us > 0) ? (GlobalPool.get_total_nodes() * 1000000LL / us) : 0;

             std::string score_str = "cp " + std::to_string(best_val);
             if (std::abs(best_val) > 30000) {
                 int mate = (MATE_SCORE - std::abs(best_val) + 1) / 2;
                 score_str = "mate " + std::to_string(best_val > 0 ? mate : -mate);
             }

             std::cout << "info depth " << depth << " score " << score_str
                       << " time " << ms << " nodes " << GlobalPool.get_total_nodes()
                       << " nps " << nps << " pv " << get_pv(pos, best_move) << "\n";
        }
    }

    if (thread_id == 0) {
        std::cout << "bestmove " << move_to_uci(best_move) << std::endl;
    }
}

// ----------------------------------------------------------------------------
// ThreadPool
// ----------------------------------------------------------------------------

ThreadPool GlobalPool;

void ThreadPool::init(int thread_count) {
    for (auto* w : workers) delete w;
    workers.clear();
    if (master) { delete master; master = nullptr; }

    master = new SearchWorker(0);
    for (int i = 1; i < thread_count; i++) {
        workers.push_back(new SearchWorker(i));
    }
}

void ThreadPool::start_search(const Position& pos, const SearchLimits& limits) {
    if (master) master->start_search(pos, limits);
    for (auto* w : workers) w->start_search(pos, limits);
}

void ThreadPool::wait_for_completion() {
    if (master) master->wait_for_completion();
    for (auto* w : workers) w->wait_for_completion();
}

long long ThreadPool::get_total_nodes() const {
    long long t = 0;
    if (master) t += master->get_nodes();
    for (auto* w : workers) t += w->get_nodes();
    return t;
}

ThreadPool::~ThreadPool() {
    for (auto* w : workers) delete w;
    if (master) delete master;
}

// ----------------------------------------------------------------------------
// Search Static API
// ----------------------------------------------------------------------------

void Search::start(Position& pos, const SearchLimits& limits) {
    static bool init = false;
    if (!init) { init_lmr(); init = true; }

    if (!GlobalPool.master || (int)GlobalPool.workers.size() + 1 != OptThreads) {
        GlobalPool.init(OptThreads);
    }

    UseNMP = limits.use_nmp;
    UseProbCut = limits.use_probcut;
    UseSingular = limits.use_singular;
    UseHistory = limits.use_history;

    stop_flag = false;
    start_time = steady_clock::now();
    nodes_limit_count = limits.nodes;

    // Time Management
    allocated_time_limit = 0;
    if (!limits.infinite && limits.move_time == 0) {
         int color = pos.side_to_move();
         int t = limits.time[color];
         int i = limits.inc[color];
         int m = limits.movestogo > 0 ? limits.movestogo : 30;
         if (t > 0) {
             allocated_time_limit = (t / m) + (i * 3 / 4);
             if (allocated_time_limit > t - 50) allocated_time_limit = t - 50;
         }
    } else if (limits.move_time > 0) {
        // Modified: Use move_overhead_ms
        allocated_time_limit = limits.move_time - limits.move_overhead_ms;
    }

    TTable.new_search();
    GlobalPool.start_search(pos, limits);
    GlobalPool.master->search_loop();

    stop_flag = true;
    GlobalPool.wait_for_completion();
}

void Search::stop() { stop_flag = true; }
void Search::clear() { TTable.clear(); if(GlobalPool.master) GlobalPool.master->clear_history(); }
long long Search::get_node_count() { return GlobalPool.get_total_nodes(); }
