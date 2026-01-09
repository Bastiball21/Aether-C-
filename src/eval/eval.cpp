#include "eval.h"
#include "eval_params.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Eval {

    // Contempt
    int GlobalContempt = 0;

    const std::pair<int, int> MOBILITY_BONUS[4] = {
        {0, 6}, // Knight
        {1, 6}, // Bishop
        {2, 6}, // Rook
        {4, 6}, // Queen
    };

    // Pawn Hash
    PawnEntry PawnHash[16384];

    // Helper Functions
    inline int get_pst(int piece, int sq, int side, bool is_mg) {
        int index = (side == WHITE) ? (sq ^ 56) : sq;
        switch (piece) {
            case PAWN: return is_mg ? Params.MG_PAWN_TABLE[index] : Params.EG_PAWN_TABLE[index];
            case KNIGHT: return is_mg ? Params.MG_KNIGHT_TABLE[index] : Params.EG_KNIGHT_TABLE[index];
            case BISHOP: return is_mg ? Params.MG_BISHOP_TABLE[index] : Params.EG_BISHOP_TABLE[index];
            case ROOK: return is_mg ? Params.MG_ROOK_TABLE[index] : Params.EG_ROOK_TABLE[index];
            case QUEEN: return is_mg ? Params.MG_QUEEN_TABLE[index] : Params.EG_QUEEN_TABLE[index];
            case KING: return is_mg ? Params.MG_KING_TABLE[index] : Params.EG_KING_TABLE[index];
            default: return 0;
        }
    }

    // Pawn Eval
    PawnEntry evaluate_pawns(const Position& pos) {
        Key key = pos.pawn_key();
        int idx = key & 16383;

        if (PawnHash[idx].key == key) {
            return PawnHash[idx];
        }

        PawnEntry entry;
        entry.key = key;
        entry.score_mg = 0;
        entry.score_eg = 0;
        entry.passed_pawns[WHITE] = 0;
        entry.passed_pawns[BLACK] = 0;
        entry.pawn_attacks[WHITE] = 0;
        entry.pawn_attacks[BLACK] = 0;
        entry.passed_front_mask[WHITE] = 0;
        entry.passed_front_mask[BLACK] = 0;

        // Calc Attacks and Passed Pawns
        for (Color c : {WHITE, BLACK}) {
            int us_sign = (c == WHITE) ? 1 : -1;
            Bitboard pawns = pos.pieces(PAWN, c);
            Bitboard original_pawns = pawns;
            Bitboard them_pawns = pos.pieces(PAWN, ~c);

            while (pawns) {
                Square s = (Square)Bitboards::pop_lsb(pawns);
                Bitboard att = Bitboards::get_pawn_attacks(s, c);
                entry.pawn_attacks[c] |= att;

                File f = file_of(s);
                Rank r = rank_of(s);

                // Isolated
                Bitboard file_mask = (Bitboards::FileA << f);
                Bitboard adj_mask = 0;
                if (f > FILE_A) adj_mask |= (Bitboards::FileA << (f - 1));
                if (f < FILE_H) adj_mask |= (Bitboards::FileA << (f + 1));

                if ((original_pawns & adj_mask) == 0) {
                    entry.score_mg -= Params.PAWN_ISOLATED_MG * us_sign;
                    entry.score_eg -= Params.PAWN_ISOLATED_EG * us_sign;
                }

                // Doubled (if another pawn on same file)
                if (Bitboards::more_than_one(original_pawns & file_mask)) {
                    entry.score_mg -= Params.PAWN_DOUBLED_MG * us_sign;
                    entry.score_eg -= Params.PAWN_DOUBLED_EG * us_sign;
                }

                // Passed
                bool passed = true;
                Bitboard forward_mask = 0;
                if (c == WHITE) {
                     for (int ri = r + 1; ri < 8; ri++) forward_mask |= (0xFFULL << (ri*8));
                } else {
                     for (int ri = r - 1; ri >= 0; ri--) forward_mask |= (0xFFULL << (ri*8));
                }

                Bitboard span = (file_mask | adj_mask) & forward_mask;

                if (span & them_pawns) passed = false;

                if (passed) {
                    Bitboards::set_bit(entry.passed_pawns[c], s);
                    entry.score_mg += Params.PASSED_PAWN_RANK_BONUS_MG[r] * us_sign;
                    entry.score_eg += Params.PASSED_PAWN_RANK_BONUS_EG[r] * us_sign;

                    Square front_s = (c == WHITE) ? (Square)(s + 8) : (Square)(s - 8);
                    if (front_s >= 0 && front_s < 64) {
                        Bitboards::set_bit(entry.passed_front_mask[c], front_s);
                    }
                }
            }

            Bitboard passed = entry.passed_pawns[c];
            Bitboard east = (passed << 1) & ~Bitboards::FileA;
            Bitboard west = (passed >> 1) & ~Bitboards::FileH;
            Bitboard connected = passed & (east | west);

            int conn_cnt = Bitboards::count(connected);
            entry.score_mg += conn_cnt * Params.PASSED_PAWN_CONNECTED_BONUS_MG * us_sign;
            entry.score_eg += conn_cnt * Params.PASSED_PAWN_CONNECTED_BONUS_EG * us_sign;
        }

        PawnHash[idx] = entry;
        return entry;
    }

    int get_scale_factor(const Position&, int) {
        return 128;
    }

    int evaluate_lazy(const Position& pos) {
        int mg = 0, eg = 0;
        int phase = 0;

        PawnEntry pawn_entry = evaluate_pawns(pos);
        mg += pawn_entry.score_mg;
        eg += pawn_entry.score_eg;

        for (Color side : {WHITE, BLACK}) {
            int us_sign = (side == WHITE) ? 1 : -1;
            for (int pt = 0; pt < 6; pt++) {
                Bitboard bb = pos.pieces((PieceType)pt, side);
                int count = Bitboards::count(bb);
                phase += count * Params.PHASE_WEIGHTS[pt];

                int base_mg = Params.MG_VALS[pt];
                int base_eg = Params.EG_VALS[pt];

                while (bb) {
                    Square sq = (Square)Bitboards::pop_lsb(bb);
                    mg += (base_mg + get_pst(pt, sq, side, true)) * us_sign;
                    eg += (base_eg + get_pst(pt, sq, side, false)) * us_sign;
                }
            }
        }

        int phase_clamped = std::clamp(phase, 0, 24);
        int score = (mg * phase_clamped + eg * (24 - phase_clamped)) / 24;

        score = (score * get_scale_factor(pos, score)) / 128;

        return (pos.side_to_move() == BLACK) ? -score : score;
    }

    int evaluate_light(const Position& pos) {
        return evaluate_lazy(pos);
    }

    // Helper to get piece value
    int piece_value(Piece p) {
        if (p == NO_PIECE) return 0;
        switch(p % 6) {
            case PAWN: return 100;
            case KNIGHT: return 320;
            case BISHOP: return 330;
            case ROOK: return 500;
            case QUEEN: return 900;
            case KING: return 20000;
            default: return 0;
        }
    }

    void set_contempt(int c) {
        GlobalContempt = c;
    }

    int evaluate_hce(const Position& state, int alpha, int beta) {
        int mg = 0;
        int eg = 0;
        int phase = 0;

        PawnEntry pawn_entry = evaluate_pawns(state);
        mg += pawn_entry.score_mg;
        eg += pawn_entry.score_eg;

        for (Color side : {WHITE, BLACK}) {
            int us_sign = (side == WHITE) ? 1 : -1;
            for (int pt = 0; pt < 6; pt++) {
                Bitboard bb = state.pieces((PieceType)pt, side);
                int count = Bitboards::count(bb);
                phase += count * Params.PHASE_WEIGHTS[pt];
                int base_mg = Params.MG_VALS[pt];
                int base_eg = Params.EG_VALS[pt];
                while (bb) {
                    Square sq = (Square)Bitboards::pop_lsb(bb);
                    mg += (base_mg + get_pst(pt, sq, side, true)) * us_sign;
                    eg += (base_eg + get_pst(pt, sq, side, false)) * us_sign;
                }
            }
        }

        int phase_clamped = std::clamp(phase, 0, 24);
        int score = (mg * phase_clamped + eg * (24 - phase_clamped)) / 24;
        score = (score * get_scale_factor(state, score)) / 128;

        int score_perspective = (state.side_to_move() == BLACK) ? -score : score;

        const int LAZY_EVAL_MARGIN = 250;
        if (score_perspective + LAZY_EVAL_MARGIN <= alpha) return (state.side_to_move() == BLACK ? -alpha : alpha);
        if (score_perspective - LAZY_EVAL_MARGIN >= beta) return (state.side_to_move() == BLACK ? -beta : beta);

        // Complex Terms
        Bitboard king_rings[2] = {0, 0};
        Square king_sqs[2] = {SQ_A1, SQ_A8};

        for (Color side : {WHITE, BLACK}) {
            Bitboard k_bb = state.pieces(KING, side);
            Square k_sq = (Square)Bitboards::lsb(k_bb);
            king_sqs[side] = k_sq;
            king_rings[side] = Bitboards::get_king_attacks(k_sq);
        }

        Bitboard attacks_by_side[2] = {0, 0};
        int king_attack_units[2] = {0, 0};
        int king_attackers_count[2] = {0, 0};

        int coordination_score[2] = {0, 0};

        // Track restricted pieces for Phase 2
        Bitboard restricted_pieces[2] = {0, 0};

        Bitboard occ = state.pieces();

        for (Color us : {WHITE, BLACK}) {
            Color them = ~us;
            int us_sign = (us == WHITE) ? 1 : -1;
            Bitboard my_bishops = state.pieces(BISHOP, us);
            Bitboard my_pawns = state.pieces(PAWN, us);
            Bitboard enemy_pawns = state.pieces(PAWN, them);

            // Bishop Pair
            if (Bitboards::count(my_bishops) >= 2) {
                mg += Params.BISHOP_PAIR_BONUS_MG * us_sign;
                eg += Params.BISHOP_PAIR_BONUS_EG * us_sign;
            }

            // Passed Pawn Logic
            Bitboard passed = pawn_entry.passed_pawns[us];
            while (passed) {
                Square sq = (Square)Bitboards::pop_lsb(passed);
                int rank = rank_of(sq);

                // Supported Bonus
                if (Bitboards::check_bit(pawn_entry.pawn_attacks[us], sq)) {
                    int bonus_rank_idx = (us == WHITE) ? rank : 7 - rank;
                    if (bonus_rank_idx >= 3) {
                        mg += Params.PASSED_PAWN_SUPPORTED_BONUS_MG * us_sign;
                        eg += Params.PASSED_PAWN_SUPPORTED_BONUS_EG * us_sign;
                    }
                }
            }

            // Passed Pawn Blocker (Bitwise)
            Bitboard blocked_passed = pawn_entry.passed_front_mask[us] & occ;
            int blocked_count = Bitboards::count(blocked_passed);
            mg += blocked_count * Params.PASSED_PAWN_BLOCKER_PENALTY_MG * us_sign;
            eg += blocked_count * Params.PASSED_PAWN_BLOCKER_PENALTY_EG * us_sign;

            for (int pt = 0; pt < 6; pt++) {
                Bitboard bb = state.pieces((PieceType)pt, us);
                while (bb) {
                    Square sq = (Square)Bitboards::pop_lsb(bb);
                    Bitboard attacks = 0;
                    if (pt == KNIGHT) attacks = Bitboards::get_knight_attacks(sq);
                    else if (pt == BISHOP) attacks = Bitboards::get_bishop_attacks(sq, occ);
                    else if (pt == ROOK) attacks = Bitboards::get_rook_attacks(sq, occ);
                    else if (pt == QUEEN) attacks = Bitboards::get_queen_attacks(sq, occ);
                    else if (pt == KING) attacks = Bitboards::get_king_attacks(sq);
                    else if (pt == PAWN) attacks = Bitboards::get_pawn_attacks(sq, us);

                    attacks_by_side[us] |= attacks;

                    if (pt != PAWN && pt != KING) {
                        Bitboard safe_mob = attacks & ~state.pieces(us);
                        int mob_cnt = Bitboards::count(safe_mob);

                        // Existing Mobility Bonus
                        int mob_idx = 0;
                        if (pt == BISHOP) mob_idx = 1;
                        if (pt == ROOK) mob_idx = 2;
                        if (pt == QUEEN) mob_idx = 3;

                        int offset = MOBILITY_BONUS[mob_idx].first;
                        int weight = MOBILITY_BONUS[mob_idx].second;
                        int s = (mob_cnt - offset) * weight;
                        mg += s * us_sign;
                        eg += s * us_sign;

                        // --- Phase 1: Restricted Piece Penalty ---
                        Bitboard pawn_safe_attacks = safe_mob & ~pawn_entry.pawn_attacks[them];
                        int safe_mob_val = Bitboards::count(pawn_safe_attacks);

                        if (safe_mob_val <= 3) {
                            if (safe_mob_val <= 1) {
                                mg -= Params.RESTRICTED_STRICT_PENALTY_MG[pt] * us_sign;
                                eg -= Params.RESTRICTED_STRICT_PENALTY_EG[pt] * us_sign;
                            } else {
                                mg -= Params.RESTRICTED_PENALTY_MG[pt] * us_sign;
                                eg -= Params.RESTRICTED_PENALTY_EG[pt] * us_sign;
                            }
                        }

                        if (safe_mob_val <= 2) {
                            Bitboards::set_bit(restricted_pieces[us], sq);
                        }

                        // --- Phase 3: Inactive Penalty ---
                        if ((pt == KNIGHT || pt == BISHOP) && mob_cnt <= 2) {
                            mg -= Params.INACTIVE_PENALTY_MG * us_sign;
                            eg -= Params.INACTIVE_PENALTY_EG * us_sign;
                        }

                        // Bad Bishop
                        if (pt == BISHOP) {
                             Bitboard light_sq_mask = 0x55AA55AA55AA55AAULL;
                             bool bishop_is_light = (Bitboards::check_bit(light_sq_mask, sq));
                             Bitboard my_pawns_same_color = my_pawns & (bishop_is_light ? light_sq_mask : ~light_sq_mask);
                             if (Bitboards::count(my_pawns_same_color) >= 3) {
                                  mg += Params.BAD_BISHOP_PENALTY_MG * us_sign;
                                  eg += Params.BAD_BISHOP_PENALTY_EG * us_sign;
                             }
                        }

                        // Knight Outpost
                        if (pt == KNIGHT) {
                             int r = rank_of(sq);
                             int rel_r = (us == WHITE) ? r : 7 - r;
                             if (rel_r >= 3 && rel_r <= 5) {
                                  if (Bitboards::check_bit(pawn_entry.pawn_attacks[us], sq)) {
                                      mg += Params.KNIGHT_OUTPOST_BONUS_MG * us_sign;
                                      eg += Params.KNIGHT_OUTPOST_BONUS_EG * us_sign;
                                  }
                             }
                        }
                    }

                    if (pt == ROOK) {
                        int f = file_of(sq);
                        int r = rank_of(sq);
                        Bitboard file_mask = (Bitboards::FileA << f);
                        bool my_pawns_on_file = (my_pawns & file_mask);
                        bool enemy_pawns_on_file = (enemy_pawns & file_mask);

                        if (!my_pawns_on_file) {
                            if (!enemy_pawns_on_file) {
                                mg += Params.ROOK_OPEN_FILE_BONUS_MG * us_sign;
                                eg += Params.ROOK_OPEN_FILE_BONUS_EG * us_sign;
                            } else {
                                mg += Params.ROOK_SEMI_OPEN_FILE_BONUS_MG * us_sign;
                                eg += Params.ROOK_SEMI_OPEN_FILE_BONUS_EG * us_sign;
                            }
                        }

                        // Rook on 7th
                        int rel_r = (us == WHITE) ? r : 7 - r;
                        if (rel_r == 6) {
                            mg += Params.ROOK_ON_SEVENTH_MG * us_sign;
                            eg += Params.ROOK_ON_SEVENTH_EG * us_sign;
                        }

                        // Rook behind passed pawn
                        Bitboard my_passed_file = (pawn_entry.passed_pawns[us] & file_mask);
                        if (my_passed_file) {
                            Square relevant_pawn = (Square)Bitboards::lsb(my_passed_file);
                            if ((us == WHITE && sq < relevant_pawn) || (us == BLACK && sq > relevant_pawn)) {
                                mg += Params.ROOK_BEHIND_PASSED_MG * us_sign;
                                eg += Params.ROOK_BEHIND_PASSED_EG * us_sign;
                            }
                        }
                    }

                    // King Tropism
                    if (pt != KING && pt != PAWN) {
                        int k_file = file_of(king_sqs[them]);
                        int k_rank = rank_of(king_sqs[them]);
                        int s_file = file_of(sq);
                        int s_rank = rank_of(sq);
                        int dist = std::max(std::abs(k_file - s_file), std::abs(k_rank - s_rank));
                        int pen = Params.KING_TROPISM_PENALTY[dist];
                        mg += pen * us_sign;
                        eg += (pen / 2) * us_sign;
                    }

                    if (pt != KING) {
                        Bitboard att_on_ring = attacks & king_rings[them];
                        if (att_on_ring) {
                            king_attack_units[them] += Params.KING_ZONE_ATTACK_WEIGHTS[pt] * Bitboards::count(att_on_ring);
                            king_attackers_count[them]++;
                        }
                    }
                }
            }
        }

        // --- Phase 2: Pressure on Restricted Pieces ---
        for (Color us : {WHITE, BLACK}) {
            Color them = ~us;
            int us_sign = (us == WHITE) ? 1 : -1;

            Bitboard targets = restricted_pieces[them];
            while (targets) {
                Square sq = (Square)Bitboards::pop_lsb(targets);
                if (Bitboards::check_bit(attacks_by_side[us], sq)) {
                    if (!Bitboards::check_bit(pawn_entry.pawn_attacks[them], sq)) {
                        PieceType pt = (PieceType)(state.piece_on(sq) % 6);
                        if (pt != NO_PIECE_TYPE && pt != KING && pt != PAWN) {
                             mg += Params.PRESSURE_BONUS_MG[pt] * us_sign;
                             eg += Params.PRESSURE_BONUS_EG[pt] * us_sign;
                        }
                    }
                }
            }
        }

        // King Safety Pass
        for (Color side : {WHITE, BLACK}) {
            int us_sign = (side == WHITE) ? 1 : -1;
            Square k_sq = king_sqs[side];

            // 1. File Safety
            int file_pen = 0;
            Bitboard my_pawns = state.pieces(PAWN, side);
            Bitboard enemy_pawns = state.pieces(PAWN, ~side);
            File k_file = file_of(k_sq);

            for (int f_offset = -1; f_offset <= 1; f_offset++) {
                int f = k_file + f_offset;
                if (f >= 0 && f <= 7) {
                    Bitboard mask = (Bitboards::FileA << f);
                    bool friendly_pawn = (my_pawns & mask);
                    bool enemy_pawn = (enemy_pawns & mask);

                    if (!friendly_pawn) {
                        file_pen += Params.KING_SEMI_OPEN_FILE_PENALTY;
                        if (!enemy_pawn) file_pen += Params.KING_OPEN_FILE_PENALTY;
                    }
                }
            }
            mg -= file_pen * us_sign;

            // 2. King Attack Safety
            if (king_attackers_count[side] >= 2) {
                int units = king_attack_units[side];
                if (units > 99) units = 99;
                int safety_score = Params.KING_SAFETY_TABLE[units];
                mg -= safety_score * us_sign;
                eg -= (safety_score / 4) * us_sign;
            }

            if (Bitboards::check_bit(pawn_entry.pawn_attacks[~side], k_sq)) {
                mg -= 50 * us_sign;
            }
        }

        mg += coordination_score[WHITE] - coordination_score[BLACK];
        eg += (coordination_score[WHITE] - coordination_score[BLACK]) / 2;

        // --- Phase 4: Endgame Knowledge ---
        for (Color side : {WHITE, BLACK}) {
             int us_sign = (side == WHITE) ? 1 : -1;
             Square k = king_sqs[side];
             int f2 = 2 * file_of(k);
             int r2 = 2 * rank_of(k);
             int dist = std::max(std::abs(f2 - 7), std::abs(r2 - 7));
             eg += ((7 - dist) * 5) * us_sign;
        }

        // Hanging Pieces Logic
        int hanging_val = 0;
        const bool ENABLE_HANGING_EVAL = true;
        if (ENABLE_HANGING_EVAL) {
            Color us = state.side_to_move();
            Color them = ~us;
            int us_sign = (us == WHITE) ? 1 : -1;

            Bitboard us_occ = state.pieces(us);
            Bitboard attacked_by_them = us_occ & attacks_by_side[them];

            while (attacked_by_them) {
                Square sq = (Square)Bitboards::pop_lsb(attacked_by_them);
                Piece p = state.piece_on(sq);
                if ((p % 6) == KING) continue;

                bool defended = Bitboards::check_bit(attacks_by_side[us], sq);
                int val = piece_value(p);

                if (!defended) {
                    hanging_val += val;
                } else {
                    if (Bitboards::check_bit(pawn_entry.pawn_attacks[them], sq)) {
                        if (val > 100) hanging_val += val - 100;
                    }
                }
            }

            mg -= hanging_val * us_sign;
            eg -= (hanging_val / 2) * us_sign;
        }

        // Clamp and Scale (Final calculation)
        phase_clamped = std::clamp(phase, 0, 24);
        score = (mg * phase_clamped + eg * (24 - phase_clamped)) / 24;

        // Apply General Scale
        int scale = get_scale_factor(state, score); // Default 128

        // OCB Scaling
        if (Bitboards::count(state.pieces(QUEEN)) == 0 &&
            state.non_pawn_material(WHITE) <= 1000 && state.non_pawn_material(BLACK) <= 1000) {

             if (Bitboards::count(state.pieces(BISHOP, WHITE)) == 1 &&
                 Bitboards::count(state.pieces(BISHOP, BLACK)) == 1 &&
                 Bitboards::count(state.pieces(KNIGHT)) == 0 &&
                 Bitboards::count(state.pieces(ROOK)) == 0) {

                 Square w_b = (Square)Bitboards::lsb(state.pieces(BISHOP, WHITE));
                 Square b_b = (Square)Bitboards::lsb(state.pieces(BISHOP, BLACK));

                 // Check colors
                 bool w_light = Bitboards::check_bit(0x55AA55AA55AA55AAULL, w_b);
                 bool b_light = Bitboards::check_bit(0x55AA55AA55AA55AAULL, b_b);

                 if (w_light != b_light) {
                     scale = (scale * 96) / 128;
                 }
             }
        }

        score = (score * scale) / 128;

        // Tempo Bonus
        if (std::abs(score) < 15000) {
            int tempo = (Params.TEMPO_BONUS * phase_clamped) / 24;
            if (state.side_to_move() == WHITE) score += tempo;
            else score -= tempo;
        }

        score_perspective = (state.side_to_move() == BLACK) ? -score : score;

        // Apply Contempt
        if (GlobalContempt != 0) {
             const int MATE_TH = 30000; // Match search.cpp
             if (std::abs(score_perspective) < MATE_TH) {
                 int a = std::abs(score_perspective);
                 if (a < 200) {
                     int t = 200 - a;
                     score_perspective += (GlobalContempt * t) / 200;
                 }
             }
        }

        return score_perspective;
    }

    int evaluate(const Position& pos, int alpha, int beta) {
        return evaluate_hce(pos, alpha, beta);
    }

    void trace_eval(const Position& pos) {
        int mg = 0, eg = 0, phase = 0;
        PawnEntry pawn_entry = evaluate_pawns(pos);
        mg += pawn_entry.score_mg;
        eg += pawn_entry.score_eg;

        // Blockers
        Bitboard occ = pos.pieces();
        for (Color c : {WHITE, BLACK}) {
            int us_sign = (c == WHITE) ? 1 : -1;
            Bitboard blocked_passed = pawn_entry.passed_front_mask[c] & occ;
            int blocked_count = Bitboards::count(blocked_passed);
            mg += blocked_count * Params.PASSED_PAWN_BLOCKER_PENALTY_MG * us_sign;
            eg += blocked_count * Params.PASSED_PAWN_BLOCKER_PENALTY_EG * us_sign;
        }

        // Material/PST
        for (Color side : {WHITE, BLACK}) {
            int us_sign = (side == WHITE) ? 1 : -1;
            for (int pt = 0; pt < 6; pt++) {
                Bitboard bb = pos.pieces((PieceType)pt, side);
                int count = Bitboards::count(bb);
                phase += count * Params.PHASE_WEIGHTS[pt];
                int base_mg = Params.MG_VALS[pt];
                int base_eg = Params.EG_VALS[pt];
                while (bb) {
                    Square sq = (Square)Bitboards::pop_lsb(bb);
                    mg += (base_mg + get_pst(pt, sq, side, true)) * us_sign;
                    eg += (base_eg + get_pst(pt, sq, side, false)) * us_sign;
                }
            }
        }

        int phase_clamped = std::clamp(phase, 0, 24);
        int final_score = (mg * phase_clamped + eg * (24 - phase_clamped)) / 24;

        std::cout << "trace," << final_score << "," << phase_clamped
                  << "," << mg << "," << eg << "\n";
    }
}
