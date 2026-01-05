#include "bitboard.h"
#include <vector>

namespace Bitboards {

    Bitboard PawnAttacks[2][64];
    Bitboard KnightAttacks[64];
    Bitboard KingAttacks[64];

    // Simple magic/plain lookup implementation for now to save complexity
    // For a serious engine, Magics are better, but for portability in this constraints,
    // I'll implement "Plain Magic" (or just on-the-fly for now if I want to be super safe,
    // but looking at `eval.rs` usage in tight loops, I need speed).
    // Let's implement a basic sliding attack generator used for initialization.

    Bitboard mask_bishop_attacks(Square sq) {
        Bitboard attacks = 0;
        int r, f;
        int tr = rank_of(sq);
        int tf = file_of(sq);
        for (r = tr + 1, f = tf + 1; r < 7 && f < 7; r++, f++) attacks |= (1ULL << square_of((File)f, (Rank)r));
        for (r = tr + 1, f = tf - 1; r < 7 && f > 0; r++, f--) attacks |= (1ULL << square_of((File)f, (Rank)r));
        for (r = tr - 1, f = tf + 1; r > 0 && f < 7; r--, f++) attacks |= (1ULL << square_of((File)f, (Rank)r));
        for (r = tr - 1, f = tf - 1; r > 0 && f > 0; r--, f--) attacks |= (1ULL << square_of((File)f, (Rank)r));
        return attacks;
    }

    Bitboard mask_rook_attacks(Square sq) {
        Bitboard attacks = 0;
        int r, f;
        int tr = rank_of(sq);
        int tf = file_of(sq);
        for (r = tr + 1; r < 7; r++) attacks |= (1ULL << square_of((File)tf, (Rank)r));
        for (r = tr - 1; r > 0; r--) attacks |= (1ULL << square_of((File)tf, (Rank)r));
        for (f = tf + 1; f < 7; f++) attacks |= (1ULL << square_of((File)f, (Rank)tr));
        for (f = tf - 1; f > 0; f--) attacks |= (1ULL << square_of((File)f, (Rank)tr));
        return attacks;
    }

    Bitboard bishop_attacks_on_the_fly(Square sq, Bitboard block) {
        Bitboard attacks = 0;
        int r, f;
        int tr = rank_of(sq);
        int tf = file_of(sq);
        for (r = tr + 1, f = tf + 1; r <= 7 && f <= 7; r++, f++) {
            Bitboard b = (1ULL << square_of((File)f, (Rank)r));
            attacks |= b;
            if (block & b) break;
        }
        for (r = tr + 1, f = tf - 1; r <= 7 && f >= 0; r++, f--) {
            Bitboard b = (1ULL << square_of((File)f, (Rank)r));
            attacks |= b;
            if (block & b) break;
        }
        for (r = tr - 1, f = tf + 1; r >= 0 && f <= 7; r--, f++) {
            Bitboard b = (1ULL << square_of((File)f, (Rank)r));
            attacks |= b;
            if (block & b) break;
        }
        for (r = tr - 1, f = tf - 1; r >= 0 && f >= 0; r--, f--) {
            Bitboard b = (1ULL << square_of((File)f, (Rank)r));
            attacks |= b;
            if (block & b) break;
        }
        return attacks;
    }

    Bitboard rook_attacks_on_the_fly(Square sq, Bitboard block) {
        Bitboard attacks = 0;
        int r, f;
        int tr = rank_of(sq);
        int tf = file_of(sq);
        for (r = tr + 1; r <= 7; r++) {
            Bitboard b = (1ULL << square_of((File)tf, (Rank)r));
            attacks |= b;
            if (block & b) break;
        }
        for (r = tr - 1; r >= 0; r--) {
            Bitboard b = (1ULL << square_of((File)tf, (Rank)r));
            attacks |= b;
            if (block & b) break;
        }
        for (f = tf + 1; f <= 7; f++) {
            Bitboard b = (1ULL << square_of((File)f, (Rank)tr));
            attacks |= b;
            if (block & b) break;
        }
        for (f = tf - 1; f >= 0; f--) {
            Bitboard b = (1ULL << square_of((File)f, (Rank)tr));
            attacks |= b;
            if (block & b) break;
        }
        return attacks;
    }

    // Leaper Initialization
    void init() {
        for (int sq = 0; sq < 64; sq++) {
            // Pawns
            Bitboard b = (1ULL << sq);
            // White
            PawnAttacks[WHITE][sq] = 0;
            if ((b & FileA) == 0) PawnAttacks[WHITE][sq] |= (b << 7);
            if ((b & FileH) == 0) PawnAttacks[WHITE][sq] |= (b << 9);
            // Black
            PawnAttacks[BLACK][sq] = 0;
            if ((b & FileA) == 0) PawnAttacks[BLACK][sq] |= (b >> 9);
            if ((b & FileH) == 0) PawnAttacks[BLACK][sq] |= (b >> 7);

            // Knight
            Bitboard k = 0;
            File f = file_of((Square)sq);
            Rank r = rank_of((Square)sq);
            int moves[8][2] = {{1, 2}, {1, -2}, {-1, 2}, {-1, -2}, {2, 1}, {2, -1}, {-2, 1}, {-2, -1}};
            for (auto& m : moves) {
                int nf = f + m[0];
                int nr = r + m[1];
                if (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7) {
                    k |= (1ULL << square_of((File)nf, (Rank)nr));
                }
            }
            KnightAttacks[sq] = k;

            // King
            Bitboard ki = 0;
            int kmoves[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
            for (auto& m : kmoves) {
                int nf = f + m[0];
                int nr = r + m[1];
                if (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7) {
                    ki |= (1ULL << square_of((File)nf, (Rank)nr));
                }
            }
            KingAttacks[sq] = ki;
        }
    }

    Bitboard get_pawn_attacks(Square sq, Color side) {
        return PawnAttacks[side][sq];
    }

    Bitboard get_knight_attacks(Square sq) {
        return KnightAttacks[sq];
    }

    Bitboard get_king_attacks(Square sq) {
        return KingAttacks[sq];
    }

    // Using on-the-fly for now. It is slow (function call + loops) but correct.
    // Given the task is "Port logic" and "High Performance", I should ideally use Magics.
    // But Magics require large tables and init code.
    // I'll stick to on-the-fly for this step to ensure I get the logic ported, then optimize if needed.
    // NOTE: On modern CPUs, on-the-fly with PEXT is fast, but standard loop is slow.
    // However, I can't easily paste 2MB of magic numbers here.

    Bitboard get_bishop_attacks(Square sq, Bitboard occ) {
        return bishop_attacks_on_the_fly(sq, occ);
    }

    Bitboard get_rook_attacks(Square sq, Bitboard occ) {
        return rook_attacks_on_the_fly(sq, occ);
    }

    Bitboard get_queen_attacks(Square sq, Bitboard occ) {
        return bishop_attacks_on_the_fly(sq, occ) | rook_attacks_on_the_fly(sq, occ);
    }

} // namespace Bitboards
