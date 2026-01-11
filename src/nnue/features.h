#ifndef NNUE_FEATURES_H
#define NNUE_FEATURES_H

#include "../types.h"
#include <algorithm>

namespace NNUE {

    // Mirroring for Black perspective
    // Flip rank of square. Invert color of piece.
    // Piece: W_PAWN(0) <-> B_PAWN(6)
    inline Piece mirror_piece(Piece p) {
        int pi = static_cast<int>(p);
        return static_cast<Piece>(pi < 6 ? pi + 6 : pi - 6);
    }

    inline Square mirror_square(Square s) {
        return static_cast<Square>(s ^ 56); // Flip vertical
    }

    // HalfKP piece index:
    // WP..WQ => 0..4
    // BP..BQ => 5..9
    // Kings => -1
    inline int halfkp_piece_index(Piece p) {
        switch (p) {
            case W_PAWN:   return 0;
            case W_KNIGHT: return 1;
            case W_BISHOP: return 2;
            case W_ROOK:   return 3;
            case W_QUEEN:  return 4;
            case W_KING:   return -1;
            case B_PAWN:   return 5;
            case B_KNIGHT: return 6;
            case B_BISHOP: return 7;
            case B_ROOK:   return 8;
            case B_QUEEN:  return 9;
            case B_KING:   return -1;
            default:       return -1;
        }
    }

    // idx = kingSqTrans * 640 + pieceIndex * 64 + sqTrans
    inline int feature_index(Square king_sq, Piece p, Square sq, Color perspective) {
        int k_sq_trans, sq_trans, p_idx;

        if (perspective == WHITE) {
            k_sq_trans = static_cast<int>(king_sq);
            sq_trans = static_cast<int>(sq);
            // Piece index is already correct for white perspective?
            // "For BLACK perspective: mirror kingSq, mirror piece, mirror square."
            // For White perspective, we use the piece index as is?
            // Logic:
            // if p is W_PAWN(0), idx should be 0.
            // if p is B_PAWN(6), idx should be 5.
            p_idx = halfkp_piece_index(p);
        } else {
            k_sq_trans = static_cast<int>(mirror_square(king_sq));
            sq_trans = static_cast<int>(mirror_square(sq));
            // Mirror piece
            Piece mirrored_p = mirror_piece(p);
            p_idx = halfkp_piece_index(mirrored_p);
        }

        if (p_idx == -1) return -1; // Should not happen for non-king pieces

        return k_sq_trans * 640 + p_idx * 64 + sq_trans;
    }

    // Bias feature index: 40960 + kingSqTrans
    inline int bias_index(Square king_sq, Color perspective) {
         int k_sq_trans = (perspective == WHITE) ? static_cast<int>(king_sq) : static_cast<int>(mirror_square(king_sq));
         return 40960 + k_sq_trans;
    }

} // namespace NNUE

#endif // NNUE_FEATURES_H
