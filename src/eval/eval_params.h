#ifndef EVAL_PARAMS_H
#define EVAL_PARAMS_H

#include <utility>
#include <cstdint>

namespace Eval {

    struct EvalParams {
        int MG_VALS[6];
        int EG_VALS[6];
        int PHASE_WEIGHTS[6];

        int KING_TROPISM_PENALTY[8];
        int SHIELD_MISSING_PENALTY;
        int SHIELD_OPEN_FILE_PENALTY;

        int BISHOP_PAIR_BONUS_MG;
        int BISHOP_PAIR_BONUS_EG;
        int ROOK_OPEN_FILE_BONUS_MG;
        int ROOK_OPEN_FILE_BONUS_EG;
        int ROOK_SEMI_OPEN_FILE_BONUS_MG;
        int ROOK_SEMI_OPEN_FILE_BONUS_EG;
        int PASSED_PAWN_SUPPORTED_BONUS_MG;
        int PASSED_PAWN_SUPPORTED_BONUS_EG;
        int PASSED_PAWN_RANK_BONUS_MG[8];
        int PASSED_PAWN_RANK_BONUS_EG[8];
        int PASSED_PAWN_CONNECTED_BONUS_MG;
        int PASSED_PAWN_CONNECTED_BONUS_EG;
        int PASSED_PAWN_BLOCKER_PENALTY_MG;
        int PASSED_PAWN_BLOCKER_PENALTY_EG;
        int CANDIDATE_PASSED_PAWN_BONUS_MG;
        int CANDIDATE_PASSED_PAWN_BONUS_EG;
        int PAWN_MAJORITY_BONUS_MG;
        int PAWN_MAJORITY_BONUS_EG;
        int PAWN_BACKWARD_MG;
        int PAWN_BACKWARD_EG;
        int PAWN_CHAIN_BONUS_MG;
        int PAWN_CHAIN_BONUS_EG;
        int PAWN_LEVER_PENALTY_MG;
        int PAWN_LEVER_PENALTY_EG;
        int DOUBLED_OPEN_FILE_BONUS_MG;
        int DOUBLED_OPEN_FILE_BONUS_EG;

        int TEMPO_BONUS;

        int MOBILITY_OFFSET[4];
        int MOBILITY_WEIGHT_MG[4];
        int MOBILITY_WEIGHT_EG[4];

        int BAD_BISHOP_PENALTY_MG;
        int BAD_BISHOP_PENALTY_EG;
        int TRAPPED_BISHOP_CORNER_MG;
        int TRAPPED_BISHOP_CORNER_EG;
        int ROOK_ON_SEVENTH_MG;
        int ROOK_ON_SEVENTH_EG;
        int ROOK_BEHIND_PASSED_MG;
        int ROOK_BEHIND_PASSED_EG;
        int ROOK_OPEN_FILE_CLEAR_BONUS_MG;
        int ROOK_OPEN_FILE_CLEAR_BONUS_EG;
        int TRAPPED_ROOK_BEHIND_KING_MG;
        int TRAPPED_ROOK_BEHIND_KING_EG;
        int TRAPPED_ROOK_BLOCKED_PAWNS_MG;
        int TRAPPED_ROOK_BLOCKED_PAWNS_EG;
        int KNIGHT_OUTPOST_BONUS_MG;
        int KNIGHT_OUTPOST_BONUS_EG;
        int BISHOP_LONG_DIAGONAL_BONUS_MG;
        int BISHOP_LONG_DIAGONAL_BONUS_EG;
        int BISHOP_BLOCKED_CENTER_PENALTY_MG;
        int BISHOP_BLOCKED_CENTER_PENALTY_EG;

        int KING_ZONE_ATTACK_WEIGHTS[6];
        int KING_SAFETY_TABLE[100];
        int KING_OPEN_FILE_PENALTY;
        int KING_SEMI_OPEN_FILE_PENALTY;
        int KING_PAWN_SHIELD_BONUS_MG;
        int KING_PAWN_SHIELD_BONUS_EG;
        int KING_PAWN_STORM_PENALTY_MG;
        int KING_PAWN_STORM_PENALTY_EG;
        int KING_ATTACKER_BONUS;
        int KING_QUEEN_ATTACKER_BONUS;
        int KING_SAFETY_CLAMP;

        int RESTRICTED_PENALTY_MG[6];
        int RESTRICTED_PENALTY_EG[6];
        int RESTRICTED_STRICT_PENALTY_MG[6];
        int RESTRICTED_STRICT_PENALTY_EG[6];
        int PRESSURE_BONUS_MG[6];
        int PRESSURE_BONUS_EG[6];

        int INACTIVE_PENALTY_MG;
        int INACTIVE_PENALTY_EG;

        int PAWN_ISOLATED_MG;
        int PAWN_ISOLATED_EG;
        int PAWN_DOUBLED_MG;
        int PAWN_DOUBLED_EG;

        int SCALE_PAWNLESS_DRAW;
        int SCALE_KRP_KR;
        int SCALE_FORTRESS;
        int SPACE_PAWN_BONUS_MG;
        int SPACE_PAWN_BONUS_EG;
        int SPACE_PIECE_BONUS_MG;
        int SPACE_PIECE_BONUS_EG;
        int INITIATIVE_BONUS_MG;
        int INITIATIVE_BONUS_EG;
        int BISHOP_PAIR_OPEN_SCALE_MG;
        int BISHOP_PAIR_OPEN_SCALE_EG;
        int PASSED_PAWN_DISTANCE_BONUS_EG[8];
        int PASSED_PAWN_DISTANCE_BONUS_MG[8];
        int PASSED_PAWN_KING_CLOSER_BONUS_MG;
        int PASSED_PAWN_KING_CLOSER_BONUS_EG;
        int PASSED_PAWN_BLOCKER_BY_PIECE_MG[6];
        int PASSED_PAWN_BLOCKER_BY_PIECE_EG[6];
        int CLAMP_MG;
        int CLAMP_EG;

        // PSTs
        int MG_PAWN_TABLE[64];
        int EG_PAWN_TABLE[64];
        int MG_KNIGHT_TABLE[64];
        int EG_KNIGHT_TABLE[64];
        int MG_BISHOP_TABLE[64];
        int EG_BISHOP_TABLE[64];
        int MG_ROOK_TABLE[64];
        int EG_ROOK_TABLE[64];
        int MG_QUEEN_TABLE[64];
        int EG_QUEEN_TABLE[64];
        int MG_KING_TABLE[64];
        int EG_KING_TABLE[64];
    };

    extern EvalParams Params;
    void init_params(); // Set defaults
    bool load_params(const char* filename); // Load from JSON/file

}

#endif // EVAL_PARAMS_H
