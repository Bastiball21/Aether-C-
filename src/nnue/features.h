#ifndef NNUE_FEATURES_H
#define NNUE_FEATURES_H

#include "../types.h"
#include <algorithm>

namespace NNUE {

    constexpr int kHalfKPDimensions = 768; // 12 pieces * 64 squares

    // Feature Index: PieceType(0..11) * 64 + Square(0..63)
    // Order from Piece enum: W_PAWN, W_KNIGHT, ..., B_KING
    inline int feature_index(Piece p, Square s) {
        return static_cast<int>(p) * 64 + static_cast<int>(s);
    }

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

    inline int feature_index_mirrored(Piece p, Square s) {
        return feature_index(mirror_piece(p), mirror_square(s));
    }

    // King Bucket is just the square of the king (0..63)
    // For White: square of White King
    // For Black: mirrored square of Black King
    inline int king_bucket(Square k_sq, Color c) {
        return (c == WHITE) ? static_cast<int>(k_sq) : static_cast<int>(mirror_square(k_sq));
    }

} // namespace NNUE

#endif // NNUE_FEATURES_H
