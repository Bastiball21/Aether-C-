#ifndef EVAL_H
#define EVAL_H

#include "position.h"
#include "eval_params.h" // For load_params visibility or just declare it

namespace Eval {

    // Helper structs
    struct PawnEntry {
        Key key;
        int score_mg;
        int score_eg;
        Bitboard passed_pawns[2];
        Bitboard pawn_attacks[2];
        Bitboard passed_front_mask[2]; // Squares in front of passed pawns (one rank)
    };

    // Constants
    extern const int MG_VALS[6];
    extern const int EG_VALS[6];
    extern const int PHASE_WEIGHTS[6];

    // Main Eval
    void set_contempt(int c);
    int evaluate(const Position& pos, int alpha = -32000, int beta = 32000);
    int evaluate_light(const Position& pos);
    void trace_eval(const Position& pos);

    // Internal
    PawnEntry evaluate_pawns(const Position& pos);

}

#endif // EVAL_H
