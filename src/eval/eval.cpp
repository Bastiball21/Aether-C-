#include "eval.h"
#include "eval_params.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace Eval {

    // Global Contempt Setting
    int GlobalContempt = 0;

    // Pawn Hash Table
    PawnEntry PawnHash[16384];

    // ----------------------------------------------------------------------------
    // Helper Functions
    // ----------------------------------------------------------------------------

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

    int piece_value(Piece p) {
        if (p == NO_PIECE) return 0;
        int pt = p % 6;
        // Simple fixed values for heuristic checks (Hanging pieces, MVV/LVA)
        // Actual eval uses Tunable Params.
        static const int vals[] = {100, 320, 330, 500, 900, 20000};
        return vals[pt];
    }

    void set_contempt(int c) {
        GlobalContempt = c;
    }

    // ----------------------------------------------------------------------------
    // Pawn Evaluation
    // ----------------------------------------------------------------------------

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

        for (Color c : {WHITE, BLACK}) {
            int us_sign = (c == WHITE) ? 1 : -1;
            Bitboard pawns = pos.pieces(PAWN, c);
            Bitboard original_pawns = pawns;
            Bitboard them_pawns = pos.pieces(PAWN, ~c);

            // Calculate attacks first for use in passed pawn logic?
            // Actually, we can do it in the loop or pre-calc.
            // Let's pre-calc attacks.
            Bitboard our_attacks = 0;
            Bitboard p = pawns;
            while (p) {
                Square s = (Square)Bitboards::pop_lsb(p);
                our_attacks |= Bitboards::get_pawn_attacks(s, c);
            }
            entry.pawn_attacks[c] = our_attacks;

            while (pawns) {
                Square s = (Square)Bitboards::pop_lsb(pawns);
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

                // Doubled
                if (Bitboards::more_than_one(original_pawns & file_mask)) {
                    entry.score_mg -= Params.PAWN_DOUBLED_MG * us_sign;
                    entry.score_eg -= Params.PAWN_DOUBLED_EG * us_sign;
                }

                // Passed
                // A pawn is passed if no enemy pawns are in front of it on the same file or adjacent files.
                Bitboard forward_mask = 0;
                if (c == WHITE) {
                     for (int ri = r + 1; ri < 8; ri++) forward_mask |= (0xFFULL << (ri*8));
                } else {
                     for (int ri = r - 1; ri >= 0; ri--) forward_mask |= (0xFFULL << (ri*8));
                }

                Bitboard span = (file_mask | adj_mask) & forward_mask;

                bool is_passed = (span & them_pawns) == 0;
                if (is_passed) {
                    Bitboards::set_bit(entry.passed_pawns[c], s);
                    int rel_r = (c == WHITE) ? r : 7 - r;
                    entry.score_mg += Params.PASSED_PAWN_RANK_BONUS_MG[rel_r] * us_sign;
                    entry.score_eg += Params.PASSED_PAWN_RANK_BONUS_EG[rel_r] * us_sign;

                    if (Bitboards::check_bit(entry.pawn_attacks[c], s) && rel_r >= 3) {
                        entry.score_mg += Params.PASSED_PAWN_SUPPORTED_BONUS_MG * us_sign;
                        entry.score_eg += Params.PASSED_PAWN_SUPPORTED_BONUS_EG * us_sign;
                    }

                    // Front squares mask (for blocker penalty later)
                    Square front_s = (c == WHITE) ? (Square)(s + 8) : (Square)(s - 8);
                    if (front_s >= 0 && front_s < 64) {
                        Bitboards::set_bit(entry.passed_front_mask[c], front_s);
                    }
                } else {
                    Bitboard same_file_forward = file_mask & forward_mask;
                    Bitboard adj_forward = adj_mask & forward_mask;
                    Bitboard enemy_same_file = them_pawns & same_file_forward;
                    if (enemy_same_file == 0) {
                        Bitboard enemy_adj = them_pawns & adj_forward;
                        Bitboard capturable = Bitboards::get_pawn_attacks(s, c);
                        if (enemy_adj != 0 && (enemy_adj & ~capturable) == 0) {
                            entry.score_mg += Params.CANDIDATE_PASSED_PAWN_BONUS_MG * us_sign;
                            entry.score_eg += Params.CANDIDATE_PASSED_PAWN_BONUS_EG * us_sign;
                        }
                    }
                }
            }

            // Connected Passed Pawns
            Bitboard passed = entry.passed_pawns[c];
            Bitboard east = (passed << 1) & ~Bitboards::FileH;
            Bitboard west = (passed >> 1) & ~Bitboards::FileA;
            Bitboard connected = passed & (east | west);
            int conn_cnt = Bitboards::count(connected);

            // Bonus is per pawn that is connected?
            // "conn_cnt" counts pawns that have a neighbor.
            // If A4 and B4 are passed, both are connected.
            entry.score_mg += conn_cnt * Params.PASSED_PAWN_CONNECTED_BONUS_MG * us_sign;
            entry.score_eg += conn_cnt * Params.PASSED_PAWN_CONNECTED_BONUS_EG * us_sign;
        }

        Bitboard white_pawns = pos.pieces(PAWN, WHITE);
        Bitboard black_pawns = pos.pieces(PAWN, BLACK);
        Bitboard queenside_mask =
            (Bitboards::FileA << FILE_A) | (Bitboards::FileA << FILE_B) |
            (Bitboards::FileA << FILE_C) | (Bitboards::FileA << FILE_D);
        Bitboard kingside_mask =
            (Bitboards::FileA << FILE_E) | (Bitboards::FileA << FILE_F) |
            (Bitboards::FileA << FILE_G) | (Bitboards::FileA << FILE_H);

        int queen_diff = Bitboards::count(white_pawns & queenside_mask)
            - Bitboards::count(black_pawns & queenside_mask);
        if (queen_diff != 0) {
            entry.score_mg += queen_diff * Params.PAWN_MAJORITY_BONUS_MG;
            entry.score_eg += queen_diff * Params.PAWN_MAJORITY_BONUS_EG;
        }

        int king_diff = Bitboards::count(white_pawns & kingside_mask)
            - Bitboards::count(black_pawns & kingside_mask);
        if (king_diff != 0) {
            entry.score_mg += king_diff * Params.PAWN_MAJORITY_BONUS_MG;
            entry.score_eg += king_diff * Params.PAWN_MAJORITY_BONUS_EG;
        }

        PawnHash[idx] = entry;
        return entry;
    }

    // ----------------------------------------------------------------------------
    // Scaling
    // ----------------------------------------------------------------------------

    int get_scale_factor(const Position&, int) {
        return 128; // Default scale
    }

    // ----------------------------------------------------------------------------
    // Main Evaluation
    // ----------------------------------------------------------------------------

    struct EvalInfo {
        Bitboard attacked_by[2]; // All squares attacked by side
        Bitboard king_ring[2];   // Squares around king
        int king_attack_units[2]; // Accumulated attack units
        int king_attackers_count[2]; // Number of pieces attacking king zone
        Square king_sq[2];
        Bitboard mobility_area[2]; // Safe squares for mobility
        Bitboard restricted_pieces[2];
    };

    int evaluate_hce(const Position& pos, int alpha, int beta) {
        // 1. Setup & Pawn Eval
        int mg = 0, eg = 0;
        int phase = 0;

        PawnEntry pawn_entry = evaluate_pawns(pos);
        mg += pawn_entry.score_mg;
        eg += pawn_entry.score_eg;

        EvalInfo info;
        info.attacked_by[WHITE] = info.attacked_by[BLACK] = 0;
        info.king_attack_units[WHITE] = info.king_attack_units[BLACK] = 0;
        info.king_attackers_count[WHITE] = info.king_attackers_count[BLACK] = 0;
        info.restricted_pieces[WHITE] = info.restricted_pieces[BLACK] = 0;

        // Init King Info
        for (Color c : {WHITE, BLACK}) {
            Bitboard k_bb = pos.pieces(KING, c);
            info.king_sq[c] = (Square)Bitboards::lsb(k_bb);
            info.king_ring[c] = Bitboards::get_king_attacks(info.king_sq[c]);
        }

        Bitboard occ = pos.pieces();

        // 2. Main Loop: Material, PST, Attacks, Mobility
        for (Color us : {WHITE, BLACK}) {
            Color them = ~us;
            int us_sign = (us == WHITE) ? 1 : -1;

            // Mobility Area: Exclude friendly pawns, king, and maybe blocked pieces?
            // Simple definition: ~pieces(us) union (pawn_attacks(them)) ?
            // Standard: ~pieces(us) (can capture own pieces? no).
            // Usually we exclude squares attacked by enemy pawns for "Safe Mobility".
            info.mobility_area[us] = ~(pos.pieces(us) | pawn_entry.pawn_attacks[them]);

            // Material & PST Loop
            for (int pt = 0; pt < 6; pt++) {
                Bitboard bb = pos.pieces((PieceType)pt, us);
                int count = Bitboards::count(bb);

                // Phase & Material
                phase += count * Params.PHASE_WEIGHTS[pt];
                int base_mg = Params.MG_VALS[pt];
                int base_eg = Params.EG_VALS[pt];

                // PST
                while (bb) {
                    Square sq = (Square)Bitboards::pop_lsb(bb);
                    mg += (base_mg + get_pst(pt, sq, us, true)) * us_sign;
                    eg += (base_eg + get_pst(pt, sq, us, false)) * us_sign;

                    // Generate Attacks
                    Bitboard attacks = 0;
                    if (pt == KNIGHT) attacks = Bitboards::get_knight_attacks(sq);
                    else if (pt == BISHOP) attacks = Bitboards::get_bishop_attacks(sq, occ);
                    else if (pt == ROOK) attacks = Bitboards::get_rook_attacks(sq, occ);
                    else if (pt == QUEEN) attacks = Bitboards::get_queen_attacks(sq, occ);
                    else if (pt == KING) attacks = Bitboards::get_king_attacks(sq);
                    else if (pt == PAWN) attacks = Bitboards::get_pawn_attacks(sq, us);

                    info.attacked_by[us] |= attacks;

                    // King Safety Accumulation
                    if (pt != KING && pt != PAWN) {
                         Bitboard zone_attacks = attacks & info.king_ring[them];
                         if (zone_attacks) {
                             info.king_attack_units[them] += Params.KING_ZONE_ATTACK_WEIGHTS[pt] * Bitboards::count(zone_attacks);
                             info.king_attackers_count[them]++;
                         }
                    }

                    if (pt != PAWN && pt != KING) {
                        Bitboard safe_mob = attacks & ~pos.pieces(us);
                        int mob_cnt = Bitboards::count(safe_mob);
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
                            Bitboards::set_bit(info.restricted_pieces[us], sq);
                        }

                        if ((pt == KNIGHT || pt == BISHOP) && mob_cnt <= 2) {
                            mg -= Params.INACTIVE_PENALTY_MG * us_sign;
                            eg -= Params.INACTIVE_PENALTY_EG * us_sign;
                        }
                    }

                    // Mobility (Safe)
                    if (pt != PAWN && pt != KING) {
                        Bitboard safe_moves = attacks & info.mobility_area[us];
                        // Bonus for rook on 7th?
                        // Just raw count for mobility formula
                        int mob_cnt = Bitboards::count(safe_moves);

                        // Look up table
                        // Map pt to index: N=0, B=1, R=2, Q=3
                        int mob_idx = -1;
                        if (pt == KNIGHT) mob_idx = 0;
                        else if (pt == BISHOP) mob_idx = 1;
                        else if (pt == ROOK) mob_idx = 2;
                        else if (pt == QUEEN) mob_idx = 3;

                        if (mob_idx != -1) {
                            int offset = Params.MOBILITY_OFFSET[mob_idx];
                            int weight_mg = Params.MOBILITY_WEIGHT_MG[mob_idx];
                            int weight_eg = Params.MOBILITY_WEIGHT_EG[mob_idx];
                            int delta = mob_cnt - offset;
                            mg += delta * weight_mg * us_sign;
                            eg += delta * weight_eg * us_sign;
                        }
                    }

                    // Piece Specific Evaluations
                    if (pt == BISHOP) {
                        // Bad Bishop: blocked by own pawns
                        // Check if center pawns are on same color?
                        Bitboard light_sq_mask = 0x55AA55AA55AA55AAULL;
                        bool bishop_is_light = Bitboards::check_bit(light_sq_mask, sq);
                        Bitboard my_pawns = pos.pieces(PAWN, us);
                        Bitboard my_pawns_same_color = my_pawns & (bishop_is_light ? light_sq_mask : ~light_sq_mask);
                        if (Bitboards::count(my_pawns_same_color) >= 3) {
                            mg -= Params.BAD_BISHOP_PENALTY_MG * us_sign;
                            eg -= Params.BAD_BISHOP_PENALTY_EG * us_sign;
                        }
                    }
                    if (pt == ROOK) {
                        // Open File
                        File f = file_of(sq);
                        Bitboard file_mask = Bitboards::FileA << f;
                        bool my_pawn = (pos.pieces(PAWN, us) & file_mask);
                        bool enemy_pawn = (pos.pieces(PAWN, them) & file_mask);

                        if (!my_pawn) {
                            if (!enemy_pawn) {
                                mg += Params.ROOK_OPEN_FILE_BONUS_MG * us_sign;
                                eg += Params.ROOK_OPEN_FILE_BONUS_EG * us_sign;
                            } else {
                                mg += Params.ROOK_SEMI_OPEN_FILE_BONUS_MG * us_sign;
                                eg += Params.ROOK_SEMI_OPEN_FILE_BONUS_EG * us_sign;
                            }
                        }

                        // On 7th
                        int r = rank_of(sq);
                        int rel_r = (us == WHITE) ? r : 7 - r;
                        if (rel_r == 6) { // 7th rank
                             // Strict: Only if enemy king is on back rank? Or general?
                             // Standard is usually always, sometimes conditional.
                             // Let's keep it simple.
                             mg += Params.ROOK_ON_SEVENTH_MG * us_sign;
                             eg += Params.ROOK_ON_SEVENTH_EG * us_sign;
                        }

                        Bitboard passed_on_file = pawn_entry.passed_pawns[us] & file_mask;
                        if (passed_on_file) {
                            Square relevant_pawn = (Square)Bitboards::lsb(passed_on_file);
                            if ((us == WHITE && sq < relevant_pawn) || (us == BLACK && sq > relevant_pawn)) {
                                mg += Params.ROOK_BEHIND_PASSED_MG * us_sign;
                                eg += Params.ROOK_BEHIND_PASSED_EG * us_sign;
                            }
                        }
                    }
                    if (pt == KNIGHT) {
                        // Outpost
                        // Rank 4-6, supported by pawn.
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
            }

            // Bishop Pair
            if (Bitboards::count(pos.pieces(BISHOP, us)) >= 2) {
                mg += Params.BISHOP_PAIR_BONUS_MG * us_sign;
                eg += Params.BISHOP_PAIR_BONUS_EG * us_sign;
            }

            // Passed Pawn Blockers
            Bitboard blocked_passed = pawn_entry.passed_front_mask[us] & occ;
            int blocked_count = Bitboards::count(blocked_passed);
            mg += blocked_count * Params.PASSED_PAWN_BLOCKER_PENALTY_MG * us_sign;
            eg += blocked_count * Params.PASSED_PAWN_BLOCKER_PENALTY_EG * us_sign;
        }

        for (Color us : {WHITE, BLACK}) {
            Color them = ~us;
            int us_sign = (us == WHITE) ? 1 : -1;
            Bitboard targets = info.restricted_pieces[them];
            while (targets) {
                Square sq = (Square)Bitboards::pop_lsb(targets);
                if (Bitboards::check_bit(info.attacked_by[us], sq)) {
                    if (!Bitboards::check_bit(pawn_entry.pawn_attacks[them], sq)) {
                        PieceType pt = (PieceType)(pos.piece_on(sq) % 6);
                        if (pt != NO_PIECE_TYPE && pt != KING && pt != PAWN) {
                            mg += Params.PRESSURE_BONUS_MG[pt] * us_sign;
                            eg += Params.PRESSURE_BONUS_EG[pt] * us_sign;
                        }
                    }
                }
            }
        }

        // 3. King Safety
        for (Color us : {WHITE, BLACK}) {
            int us_sign = (us == WHITE) ? 1 : -1;

            int penalty = 0;
            // Attack Units
            if (info.king_attackers_count[us] >= 2) { // Only if multiple attackers
                 int units = info.king_attack_units[us];
                 if (units > 99) units = 99;
                 penalty += Params.KING_SAFETY_TABLE[units];
            }

            // Pawn Shield / Storm
            // (Simplified: just file openness near king)
            File kf = file_of(info.king_sq[us]);
            for (int f_off = -1; f_off <= 1; f_off++) {
                int f = kf + f_off;
                if (f >= 0 && f <= 7) {
                     Bitboard file_mask = Bitboards::FileA << f;
                     if (!(pos.pieces(PAWN, us) & file_mask)) {
                         penalty += Params.KING_SEMI_OPEN_FILE_PENALTY;
                         if (!(pos.pieces(PAWN, ~us) & file_mask)) {
                             penalty += Params.KING_OPEN_FILE_PENALTY;
                         }
                     }
                }
            }

            mg -= penalty * us_sign;
            // EG safety is usually much less relevant or negative (king needs to activate)
            eg -= (penalty / 8) * us_sign;
        }

        // 4. Interpolate and Scale
        int phase_clamped = std::clamp(phase, 0, 24);
        int score = (mg * phase_clamped + eg * (24 - phase_clamped)) / 24;

        // OCB (Opposite Colored Bishops) Scaling
        // If endgame, and bishops are opposite colors.
        if (phase_clamped < 12) { // Endgame-ish
             if (Bitboards::count(pos.pieces(BISHOP, WHITE)) == 1 &&
                 Bitboards::count(pos.pieces(BISHOP, BLACK)) == 1 &&
                 Bitboards::count(pos.pieces(KNIGHT)) == 0 &&
                 Bitboards::count(pos.pieces(ROOK)) == 0 &&
                 Bitboards::count(pos.pieces(QUEEN)) == 0) {

                 Square wb = (Square)Bitboards::lsb(pos.pieces(BISHOP, WHITE));
                 Square bb = (Square)Bitboards::lsb(pos.pieces(BISHOP, BLACK));
                 bool wb_light = Bitboards::check_bit(0x55AA55AA55AA55AAULL, wb);
                 bool bb_light = Bitboards::check_bit(0x55AA55AA55AA55AAULL, bb);

                 if (wb_light != bb_light) {
                     score = score / 2; // Reduce score advantage in OCB
                 }
            }
        }

        // 5. Perspective & Tempo
        int score_stm = (pos.side_to_move() == BLACK) ? -score : score;

        // Tempo
        if (std::abs(score_stm) < 15000) {
             score_stm += (Params.TEMPO_BONUS * phase_clamped) / 24;
        }

        // Contempt
        if (GlobalContempt != 0 && std::abs(score_stm) < 30000) {
             // If drawish, penalize/bonus
             if (std::abs(score_stm) < 200) {
                 int t = 200 - std::abs(score_stm);
                 score_stm += (GlobalContempt * t) / 200;
             }
        }

        return score_stm;
    }

    int evaluate_light(const Position& pos) {
        // Simple eval for QSearch stand-pat
        // Usually just Material + PST
        int mg = 0, eg = 0;
        int phase = 0;

        for (int pt = 0; pt < 6; pt++) {
            for (Color c : {WHITE, BLACK}) {
                int sign = (c == WHITE) ? 1 : -1;
                Bitboard bb = pos.pieces((PieceType)pt, c);
                int count = Bitboards::count(bb);
                phase += count * Params.PHASE_WEIGHTS[pt];

                int base_mg = Params.MG_VALS[pt];
                int base_eg = Params.EG_VALS[pt];

                while(bb) {
                    Square sq = (Square)Bitboards::pop_lsb(bb);
                    mg += (base_mg + get_pst(pt, sq, c, true)) * sign;
                    eg += (base_eg + get_pst(pt, sq, c, false)) * sign;
                }
            }
        }
        int phase_clamped = std::clamp(phase, 0, 24);
        int score = (mg * phase_clamped + eg * (24 - phase_clamped)) / 24;
        return (pos.side_to_move() == BLACK) ? -score : score;
    }

    int evaluate(const Position& pos, int alpha, int beta) {
        return evaluate_hce(pos, alpha, beta);
    }

    void trace_eval(const Position& pos) {
        int s = evaluate(pos);
        std::cout << "trace," << s << "\n";
    }

}
