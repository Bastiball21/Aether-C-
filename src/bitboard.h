#ifndef BITBOARD_H
#define BITBOARD_H

#include "types.h"
#include <bit>

namespace Bitboards {

    inline Square pop_lsb(Bitboard& b) {
#ifndef NDEBUG
        assert(b != 0);
#endif
        if (!b) return SQ_NONE;
        Square idx = (Square)std::countr_zero(b);
        b &= b - 1;
        return idx;
    }

    inline Square lsb(Bitboard b) {
#ifndef NDEBUG
        assert(b != 0);
#endif
        if (!b) return SQ_NONE;
        return (Square)std::countr_zero(b);
    }

    inline int count(Bitboard b) {
        return std::popcount(b);
    }

    inline bool more_than_one(Bitboard b) {
        return (b & (b - 1));
    }

    inline void set_bit(Bitboard& b, Square s) {
        b |= (1ULL << s);
    }

    inline void clear_bit(Bitboard& b, Square s) {
        b &= ~(1ULL << s);
    }

    inline bool check_bit(Bitboard b, Square s) {
        return b & (1ULL << s);
    }

    void init();
    Bitboard get_pawn_attacks(Square sq, Color side);
    Bitboard get_knight_attacks(Square sq);
    Bitboard get_king_attacks(Square sq);

    // Sliding attacks
    Bitboard get_bishop_attacks(Square sq, Bitboard occ);
    Bitboard get_rook_attacks(Square sq, Bitboard occ);
    Bitboard get_queen_attacks(Square sq, Bitboard occ);

    constexpr Bitboard FileA = 0x0101010101010101ULL;
    constexpr Bitboard FileH = 0x8080808080808080ULL;
    constexpr Bitboard Rank1 = 0x00000000000000FFULL;
    constexpr Bitboard Rank8 = 0xFF00000000000000ULL;

} // namespace Bitboards

#endif // BITBOARD_H
