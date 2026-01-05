#include "eval.h"
#include <algorithm>
#include <cmath>

namespace Eval {

    // --- TUNED PARAMETERS ---
    const int MG_VALS[6] = { 100, 320, 330, 500, 900, 0 };
    const int EG_VALS[6] = { 100, 320, 330, 500, 900, 0 };
    const int PHASE_WEIGHTS[6] = { 0, 1, 1, 2, 4, 0 };

    const int KING_TROPISM_PENALTY[8] = { 10, 8, 5, 2, 0, 0, 0, 0 };
    const int SHIELD_MISSING_PENALTY = -40;
    const int SHIELD_OPEN_FILE_PENALTY = -50;

    const int LAZY_EVAL_MARGIN = 250;

    const int BISHOP_PAIR_BONUS_MG = 30;
    const int BISHOP_PAIR_BONUS_EG = 50;
    const int ROOK_OPEN_FILE_BONUS_MG = 30;
    const int ROOK_OPEN_FILE_BONUS_EG = 15;
    const int ROOK_SEMI_OPEN_FILE_BONUS_MG = 15;
    const int ROOK_SEMI_OPEN_FILE_BONUS_EG = 10;
    const int PASSED_PAWN_SUPPORTED_BONUS_MG = 10;
    const int PASSED_PAWN_SUPPORTED_BONUS_EG = 20;

    const std::pair<int, int> MOBILITY_BONUS[4] = {
        {0, 6}, // Knight
        {1, 6}, // Bishop
        {2, 6}, // Rook
        {4, 6}, // Queen
    };

    // Phase 1: Restricted Piece Penalties (Safe Mob <= 3)
    // Indexes: 0=Knight, 1=Bishop, 2=Rook, 3=Queen (matching MOBILITY_BONUS logic roughly)
    // Note: My piece types are KNIGHT=1, BISHOP=2, etc. I'll use a helper or switch.
    // Arrays for direct lookup: [PieceType]
    const int RESTRICTED_PENALTY_MG[6] = { 0, 20, 20, 12, 8, 0 }; // P, N, B, R, Q, K
    const int RESTRICTED_PENALTY_EG[6] = { 0, 10, 10, 6, 4, 0 };

    // Severely Restricted (Safe Mob <= 1)
    const int RESTRICTED_STRICT_PENALTY_MG[6] = { 0, 40, 40, 24, 16, 0 };
    const int RESTRICTED_STRICT_PENALTY_EG[6] = { 0, 20, 20, 12, 8, 0 };

    // Phase 2: Pressure Bonus (Attacking a restricted piece)
    // Bonus applied if restricted (safe mob <= 2) and attacked.
    const int PRESSURE_BONUS_MG[6] = { 0, 10, 10, 6, 4, 0 };
    const int PRESSURE_BONUS_EG[6] = { 0, 10, 10, 6, 4, 0 };

    // Phase 3: Inactive Penalty (Total Mob <= 2, Minor Pieces only)
    const int INACTIVE_PENALTY_MG = 15;
    const int INACTIVE_PENALTY_EG = 15;


    // PeSTO Tables (Flattened for brevity, but I will include full tables as requested)
    // Actually, to save space here I will use the provided values but formatting might be compact.
    // I will copy-paste the arrays provided in `eval.rs`.

    // Helper macro to make array init cleaner
    #define A(...) { __VA_ARGS__ }

    const int MG_PAWN_TABLE[64] = A(
        0,   0,   0,   0,   0,   0,   0,   0,
        50,  50,  50,  50,  50,  50,  50,  50,
        10,  10,  20,  30,  30,  20,  10,  10,
         5,   5,  10,  25,  25,  10,   5,   5,
         0,   0,  20,  50,  50,  20,   0,   0,
         5,   5,  10,  20,  20,  10,   5,   5,
         0,   0,   0, -10, -10,   0,   0,   0,
         0,   0,   0,   0,   0,   0,   0,   0
    );
    const int EG_PAWN_TABLE[64] = A( 0, 0, 0, 0, 0, 0, 0, 0, 139, 140, 135, 114, 130, 114, 147, 164, 56, 61, 53, 35, 36, 21, 55, 60, 19, 5, 4, -10, -10, 10, -3, 19, 25, 10, 14, 24, 19, 11, 21, 21, 14, 20, 12, 31, 28, 20, 21, 20, 20, 16, 2, 18, 16, 2, 2, 14, 0, 0, 0, 0, 0, 0, 0, 0 );

    const int MG_KNIGHT_TABLE[64] = A( -168, -89, -34, -49, 59, -97, -14, -108, -71, -42, 67, 32, 20, 57, 6, -17, -47, 55, 27, 52, 75, 122, 69, 43, -8, 3, 8, 28, 4, 59, 3, 19, -7, 1, 1, 0, 9, 1, 19, -7, -18, -2, -12, -1, 12, -15, 24, -11, -28, -52, -9, 0, 3, 12, -13, -19, -103, 13, -52, -28, -11, -22, 5, -22 );
    const int EG_KNIGHT_TABLE[64] = A( -57, -38, -15, -29, -33, -28, -60, -99, -24, -11, -29, -9, -12, -29, -24, -50, -26, -25, -4, -7, -12, -20, -23, -41, -14, -4, 1, -2, 0, -3, 0, -18, -18, -10, -1, 7, -1, 5, 0, -17, -22, -6, -10, 1, 3, -13, -18, -21, -41, -21, -8, -7, -4, -21, -22, -41, -27, -45, -18, -13, -19, -11, -42, -61 );

    const int MG_BISHOP_TABLE[64] = A( -32, 2, -82, -37, -24, -43, 6, -10, -24, 11, -19, -15, 28, 55, 13, -45, -18, 33, 35, 33, 30, 46, 33, -6, -3, 4, 13, 37, 22, 29, 6, -2, -6, 9, -1, 12, 19, -7, 6, 2, -2, 10, 4, -16, 0, 11, 7, 5, 3, -9, 7, 20, -3, 15, -7, 0, -30, -1, 39, -17, -8, 33, -39, -22 );
    const int EG_BISHOP_TABLE[64] = A( -16, -21, -11, -10, -5, -10, -16, -25, -8, -8, 1, -13, -5, -17, -4, -12, 0, -15, -11, -8, -10, 0, -4, 1, -6, -2, 0, -8, -3, -1, 1, 0, -11, -3, -2, -2, -8, -1, -5, -10, -14, -9, -6, -11, -1, -20, -11, -14, -16, -7, -9, -4, -11, -11, -22, -21, -12, -18, 1, -2, -2, -3, -9, -20 );

    const int MG_ROOK_TABLE[64] = A( 25, 36, 26, 44, 58, 6, 29, 39, 18, 20, 47, 52, 71, 61, 20, 40, -9, 13, 21, 28, 11, 41, 57, 15, -23, -11, 1, 20, 19, 32, -9, -20, -37, -26, -16, -4, 6, -7, 4, -27, -44, -26, -18, -17, -2, 0, -6, -35, -44, -18, -22, -14, -2, 9, -4, -71, -1, -12, -14, -11, -12, -3, -25, 0 );
    const int EG_ROOK_TABLE[64] = A( -6, -5, 2, -1, 0, 3, 2, -3, -11, -12, -14, -12, -22, -13, -5, -6, -4, -6, -7, -12, -8, -12, -12, -6, 2, -1, -2, -9, -8, -4, -4, 1, 0, 0, 0, -4, -8, -9, -11, -11, -6, -3, -11, -3, -12, -16, -11, -16, -9, -13, -7, -5, -12, -13, -12, -3, -13, -2, -7, -20, -20, -10, 5, -13 );

    const int MG_QUEEN_TABLE[64] = A( -30, -2, 26, 10, 56, 42, 41, 41, -21, -39, -5, -1, -17, 53, 24, 52, -12, -17, 3, 5, 24, 52, 41, 54, -25, -25, -18, -20, -4, 13, -3, 1, -9, -28, -10, -10, -7, -5, 0, -5, -11, 2, -13, -3, -7, 0, 10, 6, -30, -6, 7, 1, 0, 13, 0, 2, 2, -9, -2, 29, -8, -17, -28, -48 );
    const int EG_QUEEN_TABLE[64] = A( -9, 19, 18, 24, 24, 16, 8, 17, -15, 19, 31, 38, 56, 22, 27, 0, -17, 6, 6, 46, 42, 32, 15, 7, 4, 22, 21, 41, 37, 37, 34, 13, -17, 26, 16, 41, 27, 32, 38, 22, -14, -26, 12, 3, 5, 15, 9, 5, -19, -23, -30, -15, -16, -24, -34, -31, -30, -24, -19, -37, -2, -27, -18, -40 );

    const int MG_KING_TABLE[64] = A( -64, 22, 15, -15, -56, -34, 1, 12, 28, -1, -20, -8, -8, -4, -38, -29, -9, 23, 0, -17, -21, 4, 19, -22, -16, -21, -13, -28, -32, -28, -17, -36, -47, 0, -27, -41, -49, -48, -35, -51, -12, -11, -21, -46, -46, -32, -18, -24, 3, 8, -4, -55, -33, -19, -2, 0, -10, 29, 11, -34, 26, -7, -9, 29 );
    const int EG_KING_TABLE[64] = A( -73, -36, -19, -20, -12, 12, 2, -18, -12, 15, 10, 11, 12, 33, 18, 7, 9, 13, 16, 8, 13, 36, 31, 11, -6, 17, 15, 16, 14, 17, 12, 2, -12, 0, 18, 15, 8, 2, -2, -11, -11, 9, 15, 23, 9, 7, -3, -4, -20, 0, 13, 19, 10, 4, -7, -8, -45, -25, -8, 20, 2, 6, -4, -7 );

    // Helper Functions
    int get_pst(int piece, int sq, int side, bool is_mg) {
        int index = (side == WHITE) ? (sq ^ 56) : sq;
        switch (piece) {
            case PAWN: return is_mg ? MG_PAWN_TABLE[index] : EG_PAWN_TABLE[index];
            case KNIGHT: return is_mg ? MG_KNIGHT_TABLE[index] : EG_KNIGHT_TABLE[index];
            case BISHOP: return is_mg ? MG_BISHOP_TABLE[index] : EG_BISHOP_TABLE[index];
            case ROOK: return is_mg ? MG_ROOK_TABLE[index] : EG_ROOK_TABLE[index];
            case QUEEN: return is_mg ? MG_QUEEN_TABLE[index] : EG_QUEEN_TABLE[index];
            case KING: return is_mg ? MG_KING_TABLE[index] : EG_KING_TABLE[index];
            default: return 0;
        }
    }

    // Pawn Eval (Simplified)
    PawnEntry evaluate_pawns(const Position& pos) {
        PawnEntry entry;
        entry.score_mg = 0;
        entry.score_eg = 0;
        entry.passed_pawns[WHITE] = 0;
        entry.passed_pawns[BLACK] = 0;
        entry.pawn_attacks[WHITE] = 0;
        entry.pawn_attacks[BLACK] = 0;

        // Calc Attacks and Passed Pawns
        for (Color c : {WHITE, BLACK}) {
            Bitboard pawns = pos.pieces(PAWN, c);
            Bitboard them_pawns = pos.pieces(PAWN, ~c);

            while (pawns) {
                Square s = (Square)Bitboards::pop_lsb(pawns);
                Bitboard att = Bitboards::get_pawn_attacks(s, c);
                entry.pawn_attacks[c] |= att;

                File f = file_of(s);
                Rank r = rank_of(s);
                bool passed = true;

                Bitboard forward_mask = 0;
                if (c == WHITE) {
                     for (int ri = r + 1; ri < 8; ri++) forward_mask |= (0xFFULL << (ri*8));
                } else {
                     for (int ri = r - 1; ri >= 0; ri--) forward_mask |= (0xFFULL << (ri*8));
                }

                Bitboard file_mask = (Bitboards::FileA << f);
                Bitboard adj_mask = 0;
                if (f > FILE_A) adj_mask |= (Bitboards::FileA << (f - 1));
                if (f < FILE_H) adj_mask |= (Bitboards::FileA << (f + 1));

                Bitboard span = (file_mask | adj_mask) & forward_mask;

                if (span & them_pawns) passed = false;

                if (passed) Bitboards::set_bit(entry.passed_pawns[c], s);
            }
        }

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
                phase += count * PHASE_WEIGHTS[pt];

                int base_mg = MG_VALS[pt];
                int base_eg = EG_VALS[pt];

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
                phase += count * PHASE_WEIGHTS[pt];
                int base_mg = MG_VALS[pt];
                int base_eg = EG_VALS[pt];
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
        int king_attack_weight[2] = {0, 0};
        int king_attack_count[2] = {0, 0};
        int ring_attack_counts[2][64]; // Zero init
        for(int i=0; i<64; i++) { ring_attack_counts[0][i]=0; ring_attack_counts[1][i]=0; }

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
                mg += BISHOP_PAIR_BONUS_MG * us_sign;
                eg += BISHOP_PAIR_BONUS_EG * us_sign;
            }

            // Passed Pawn Supported
            Bitboard passed = pawn_entry.passed_pawns[us];
            while (passed) {
                Square sq = (Square)Bitboards::pop_lsb(passed);
                int rank = rank_of(sq);
                if (Bitboards::check_bit(pawn_entry.pawn_attacks[us], sq)) {
                    int bonus_rank_idx = (us == WHITE) ? rank : 7 - rank;
                    if (bonus_rank_idx >= 3) {
                        mg += PASSED_PAWN_SUPPORTED_BONUS_MG * us_sign;
                        eg += PASSED_PAWN_SUPPORTED_BONUS_EG * us_sign;
                    }
                }
            }

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
                        // "Safe" mobility: exclude squares attacked by enemy pawns.
                        // We also exclude our own pieces (already done in safe_mob above).
                        // Note: safe_mob is currently (attacks & ~us_pieces).
                        Bitboard pawn_safe_attacks = safe_mob & ~pawn_entry.pawn_attacks[them];
                        int safe_mob_val = Bitboards::count(pawn_safe_attacks);

                        if (safe_mob_val <= 3) {
                            if (safe_mob_val <= 1) {
                                mg -= RESTRICTED_STRICT_PENALTY_MG[pt] * us_sign;
                                eg -= RESTRICTED_STRICT_PENALTY_EG[pt] * us_sign;
                            } else {
                                mg -= RESTRICTED_PENALTY_MG[pt] * us_sign;
                                eg -= RESTRICTED_PENALTY_EG[pt] * us_sign;
                            }
                        }

                        // Mark for Phase 2 (Pressure)
                        if (safe_mob_val <= 2) {
                            Bitboards::set_bit(restricted_pieces[us], sq);
                        }

                        // --- Phase 3: Inactive Penalty ---
                        // Minor pieces with very low TOTAL mobility.
                        if ((pt == KNIGHT || pt == BISHOP) && mob_cnt <= 2) {
                            mg -= INACTIVE_PENALTY_MG * us_sign;
                            eg -= INACTIVE_PENALTY_EG * us_sign;
                        }
                    }

                    if (pt == ROOK) {
                        int f = file_of(sq);
                        Bitboard file_mask = (Bitboards::FileA << f);
                        bool my_pawns_on_file = (my_pawns & file_mask);
                        bool enemy_pawns_on_file = (enemy_pawns & file_mask);

                        if (!my_pawns_on_file) {
                            if (!enemy_pawns_on_file) {
                                mg += ROOK_OPEN_FILE_BONUS_MG * us_sign;
                                eg += ROOK_OPEN_FILE_BONUS_EG * us_sign;
                            } else {
                                mg += ROOK_SEMI_OPEN_FILE_BONUS_MG * us_sign;
                                eg += ROOK_SEMI_OPEN_FILE_BONUS_EG * us_sign;
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
                        int pen = KING_TROPISM_PENALTY[dist];
                        mg += pen * us_sign;
                        eg += (pen / 2) * us_sign;
                    }

                    if (pt != KING) {
                        Bitboard att_on_ring = attacks & king_rings[them];
                        if (att_on_ring) {
                            int weight = 0;
                            if (pt == PAWN) weight = 10;
                            else if (pt == KNIGHT) weight = 25;
                            else if (pt == BISHOP) weight = 25;
                            else if (pt == ROOK) weight = 50;
                            else if (pt == QUEEN) weight = 75;

                            king_attack_weight[them] += weight;
                            king_attack_count[them] += 1;

                            while (att_on_ring) {
                                Square s = (Square)Bitboards::pop_lsb(att_on_ring);
                                ring_attack_counts[them][s]++;
                            }
                        }
                    }
                }
            }
        }

        // --- Phase 2: Pressure on Restricted Pieces ---
        // Reward attacking enemy restricted pieces.
        for (Color us : {WHITE, BLACK}) {
            Color them = ~us;
            int us_sign = (us == WHITE) ? 1 : -1;

            Bitboard targets = restricted_pieces[them];
            while (targets) {
                Square sq = (Square)Bitboards::pop_lsb(targets);

                // If we attack it
                if (Bitboards::check_bit(attacks_by_side[us], sq)) {
                    // Safety Gate: Skip if defended by enemy pawn
                    // (Note: 'them' is the side owning the piece, so check 'them' pawn attacks)
                    if (!Bitboards::check_bit(pawn_entry.pawn_attacks[them], sq)) {
                        PieceType pt = (PieceType)(state.piece_on(sq) % 6); // Get type (0-5)
                        if (pt != NO_PIECE_TYPE && pt != KING && pt != PAWN) {
                             mg += PRESSURE_BONUS_MG[pt] * us_sign;
                             eg += PRESSURE_BONUS_EG[pt] * us_sign;
                        }
                    }
                }
            }
        }

        // King Safety Pass
        for (Color side : {WHITE, BLACK}) {
            int us_sign = (side == WHITE) ? 1 : -1;
            Square k_sq = king_sqs[side];

            int shield_pen = 0;
            Rank k_rank = rank_of(k_sq);
            if ((side == WHITE && k_rank < RANK_4) || (side == BLACK && k_rank > RANK_5)) {
                Bitboard my_pawns = state.pieces(PAWN, side);
                Bitboard enemy_pawns = state.pieces(PAWN, ~side);
                File k_file = file_of(k_sq);
                for (int f_offset = -1; f_offset <= 1; f_offset++) {
                    int f = k_file + f_offset;
                    if (f >= 0 && f <= 7) {
                        Bitboard mask = (Bitboards::FileA << f);
                        if ((my_pawns & mask) == 0) {
                            shield_pen += SHIELD_MISSING_PENALTY;
                            if ((enemy_pawns & mask) == 0) shield_pen += SHIELD_OPEN_FILE_PENALTY;
                        }
                    }
                }
            }
            mg += shield_pen * us_sign;

            if (Bitboards::check_bit(pawn_entry.pawn_attacks[~side], k_sq)) {
                mg -= 50 * us_sign;
            }

            int danger = king_attack_weight[side];
            if (king_attack_count[side] >= 2) {
                danger += king_attack_count[side] * 10;
            }

            Bitboard ring = king_rings[side];
            Bitboard undefended = ring & ~attacks_by_side[side];
            Bitboard attacked = ring & attacks_by_side[~side];
            Bitboard danger_zone = undefended & attacked;
            danger += Bitboards::count(danger_zone) * 10;

            int cluster_pen = 0;
            while (ring) {
                Square s = (Square)Bitboards::pop_lsb(ring);
                int c = ring_attack_counts[side][s];
                if (c >= 2) cluster_pen += (c - 1) * 20;
            }

            if (danger > 80) mg -= danger * us_sign;
            mg -= cluster_pen * us_sign;
            eg -= (cluster_pen / 2) * us_sign;
        }

        mg += coordination_score[WHITE] - coordination_score[BLACK];
        eg += (coordination_score[WHITE] - coordination_score[BLACK]) / 2;

        // Hanging Pieces Logic (Simplified port)
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

                // Exclude King
                if ((p % 6) == KING) continue;

                bool defended = Bitboards::check_bit(attacks_by_side[us], sq);
                int val = piece_value(p);

                if (!defended) {
                    hanging_val += val;
                } else {
                    // Optimization: if attacked by pawn and val > 100
                    if (Bitboards::check_bit(pawn_entry.pawn_attacks[them], sq)) {
                        if (val > 100) hanging_val += val - 100;
                    }
                }
            }

            mg -= hanging_val * us_sign;
            eg -= (hanging_val / 2) * us_sign;
        }

        // Clamp and Scale again
        phase_clamped = std::clamp(phase, 0, 24);
        score = (mg * phase_clamped + eg * (24 - phase_clamped)) / 24;
        score = (score * get_scale_factor(state, score)) / 128;

        return (state.side_to_move() == BLACK) ? -score : score;
    }

    int evaluate(const Position& pos, int alpha, int beta) {
        return evaluate_hce(pos, alpha, beta);
    }
}
