#include "eval_params.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>

namespace Eval {

    EvalParams Params;

    // Default Values (Tuned Baseline)
    void init_params() {
        // Material
        const int mg_vals[] = { 100, 320, 330, 500, 900, 0 };
        const int eg_vals[] = { 100, 320, 330, 500, 900, 0 };
        // Phase: P=0, N=1, B=1, R=2, Q=4
        const int phase[] = { 0, 1, 1, 2, 4, 0 };

        std::memcpy(Params.MG_VALS, mg_vals, sizeof(mg_vals));
        std::memcpy(Params.EG_VALS, eg_vals, sizeof(eg_vals));
        std::memcpy(Params.PHASE_WEIGHTS, phase, sizeof(phase));

        // Bonuses/Penalties
        Params.BISHOP_PAIR_BONUS_MG = 30;
        Params.BISHOP_PAIR_BONUS_EG = 50;
        Params.TEMPO_BONUS = 20;

        // Pawns
        Params.PAWN_ISOLATED_MG = 10;
        Params.PAWN_ISOLATED_EG = 20;
        Params.PAWN_DOUBLED_MG = 10;
        Params.PAWN_DOUBLED_EG = 20;

        Params.PASSED_PAWN_CONNECTED_BONUS_MG = 10;
        Params.PASSED_PAWN_CONNECTED_BONUS_EG = 20;
        Params.PASSED_PAWN_BLOCKER_PENALTY_MG = -10;
        Params.PASSED_PAWN_BLOCKER_PENALTY_EG = -20;

        int passed_rank_mg[] = { 0, 10, 10, 20, 40, 60, 100, 0 };
        int passed_rank_eg[] = { 0, 20, 20, 40, 80, 120, 200, 0 };
        std::memcpy(Params.PASSED_PAWN_RANK_BONUS_MG, passed_rank_mg, sizeof(passed_rank_mg));
        std::memcpy(Params.PASSED_PAWN_RANK_BONUS_EG, passed_rank_eg, sizeof(passed_rank_eg));

        // Pieces
        Params.ROOK_OPEN_FILE_BONUS_MG = 20;
        Params.ROOK_OPEN_FILE_BONUS_EG = 10;
        Params.ROOK_SEMI_OPEN_FILE_BONUS_MG = 10;
        Params.ROOK_SEMI_OPEN_FILE_BONUS_EG = 5;
        Params.ROOK_ON_SEVENTH_MG = 40;
        Params.ROOK_ON_SEVENTH_EG = 20;
        Params.TRAPPED_ROOK_BEHIND_KING_MG = 35;
        Params.TRAPPED_ROOK_BEHIND_KING_EG = 10;
        Params.TRAPPED_ROOK_BLOCKED_PAWNS_MG = 25;
        Params.TRAPPED_ROOK_BLOCKED_PAWNS_EG = 8;
        Params.KNIGHT_OUTPOST_BONUS_MG = 30;
        Params.KNIGHT_OUTPOST_BONUS_EG = 20;

        Params.BAD_BISHOP_PENALTY_MG = 10;
        Params.BAD_BISHOP_PENALTY_EG = 10;
        Params.TRAPPED_BISHOP_CORNER_MG = 40;
        Params.TRAPPED_BISHOP_CORNER_EG = 10;
        Params.INACTIVE_PENALTY_MG = 20;
        Params.INACTIVE_PENALTY_EG = 20;

        // King Safety
        Params.KING_OPEN_FILE_PENALTY = 20;
        Params.KING_SEMI_OPEN_FILE_PENALTY = 10;

        int zone_weights[] = { 0, 2, 2, 3, 5, 0 };
        std::memcpy(Params.KING_ZONE_ATTACK_WEIGHTS, zone_weights, sizeof(zone_weights));

        for (int i = 0; i < 100; i++) {
            // Sigmoid-like or linear scaling of attack units
            Params.KING_SAFETY_TABLE[i] = (i * i) / 4;
            if (Params.KING_SAFETY_TABLE[i] > 400) Params.KING_SAFETY_TABLE[i] = 400;
        }

        // PSTs (Simple Center Bonus)
        const int center_bonus[64] = {
             0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0,
             0,  5, 10, 15, 15, 10,  5,  0,
             0,  5, 15, 25, 25, 15,  5,  0,
             0,  5, 15, 25, 25, 15,  5,  0,
             0,  5, 10, 15, 15, 10,  5,  0,
             0,  0,  0,  0,  0,  0,  0,  0,
             0,  0,  0,  0,  0,  0,  0,  0
        };
        const int pawn_pst[64] = {
             0,  0,  0,  0,  0,  0,  0,  0,
            50, 50, 50, 50, 50, 50, 50, 50, // Rank 2 (for White) mapped to Rank 7
            10, 10, 20, 30, 30, 20, 10, 10,
             5,  5, 10, 25, 25, 10,  5,  5,
             0,  0,  0, 20, 20,  0,  0,  0,
             5, -5,-10,  0,  0,-10, -5,  5,
             5, 10, 10,-20,-20, 10, 10,  5,
             0,  0,  0,  0,  0,  0,  0,  0
        };

        for (int i = 0; i < 64; i++) {
            // Use same PST for MG/EG for simplicity, scaled
            Params.MG_KNIGHT_TABLE[i] = center_bonus[i];
            Params.EG_KNIGHT_TABLE[i] = center_bonus[i];
            Params.MG_BISHOP_TABLE[i] = center_bonus[i];
            Params.EG_BISHOP_TABLE[i] = center_bonus[i];
            Params.MG_ROOK_TABLE[i] = center_bonus[i] / 2;
            Params.EG_ROOK_TABLE[i] = center_bonus[i] / 2;
            Params.MG_QUEEN_TABLE[i] = center_bonus[i] / 2;
            Params.EG_QUEEN_TABLE[i] = center_bonus[i] / 2;

            // Pawns need specific structure (advancement)
            // Note: Indices in Eval::get_pst are flipped for White (sq^56).
            // So index 0 is A8? No, get_pst usage:
            // "int index = (side == WHITE) ? (sq ^ 56) : sq;"
            // If sq=A1 (0), White index=56.
            // My array is 0..63. Standard representation (Rank 1 to 8).
            // Aether Position usually: A1=0, H1=7.
            // If I want A1 to be index 0:
            // But `index = sq ^ 56` flips rank.
            // If sq=A1(0), index=56 (A8).
            // So the table must be defined from White's perspective, Rank 8 to Rank 1?
            // Or Rank 1 to Rank 8?
            // If table[0] is A8?
            // Usually engines define PST from Rank 8 down to Rank 1 (visual).
            // If I define it Rank 1 to Rank 8 (index 0 to 63):
            // Then `sq ^ 56` means for White, we flip it.
            // If I have a bonus on E4 (index 28).
            // White E4 is 28. `28 ^ 56` = `28`? No.
            // `28 = 011100`. `56 = 111000`. XOR = `100100` = 36 (E5).
            // So `sq ^ 56` flips the board vertically.
            // So if I define table visually as Rank 8 to Rank 1 (0..63),
            // Then for White (sq), we want to read "Rank 1" values for A1.
            // If table[56] is A1.
            // Then `0 ^ 56` = 56. Correct.
            // So I should define table as Rank 8 (top) to Rank 1 (bottom).

            // My `center_bonus` above is symmetric so it doesn't matter much.
            // My `pawn_pst` above:
            // Rows 0-7.
            // Row 0 (index 0-7): Rank 8?
            // If Rank 8, pawns can't be there.
            // Row 1 (8-15): Rank 7.
            // Row 7 (56-63): Rank 1.
            // So `pawn_pst` above is defined Rank 8 to Rank 1.

            // Let's ensure my `pawn_pst` logic is good.
            // Index 8-15 is Rank 7. "50". This is promotion rank for White?
            // No, White promotes at Rank 8. Rank 7 is "almost there".
            // So `pawn_pst` above rewards Rank 7.
            // It seems correct for "Rank 8 to Rank 1" layout.

            Params.MG_PAWN_TABLE[i] = pawn_pst[i];
            Params.EG_PAWN_TABLE[i] = pawn_pst[i];

            // King Safety (Stay back)
            // Penalty for being in center or open?
            // Just use a simple shield logic in code, PST can be 0 or corner bonus.
            Params.MG_KING_TABLE[i] = (i < 16 || i >= 48) ? 0 : -10; // Penalize center slightly
            Params.EG_KING_TABLE[i] = center_bonus[i]; // Centralize in endgame
        }

        // Pressure / Restricted
        for(int i=0; i<6; ++i) {
            Params.RESTRICTED_PENALTY_MG[i] = 5;
            Params.RESTRICTED_PENALTY_EG[i] = 5;
            Params.RESTRICTED_STRICT_PENALTY_MG[i] = 10;
            Params.RESTRICTED_STRICT_PENALTY_EG[i] = 10;
            Params.PRESSURE_BONUS_MG[i] = 5;
            Params.PRESSURE_BONUS_EG[i] = 5;
        }
    }

    bool load_params(const char* filename) {
        // Placeholder for JSON loader
        // Not strictly required for the patch to work, as defaults are set.
        return false;
    }

}
