#include "bitboard.h"
#include "magics.h"
#include <vector>
#include <iostream>

namespace Bitboards {

    Bitboard PawnAttacks[2][64];
    Bitboard KnightAttacks[64];
    Bitboard KingAttacks[64];

    // Magic Bitboards Tables
    // Total size ~ 800KB + small overhead
    std::vector<Bitboard> RookTable;
    std::vector<Bitboard> BishopTable;

    // Offsets into the table
    int RookOffsets[64];
    int BishopOffsets[64];

    // Helper to generate attacks for init (same as generator)
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

    Bitboard bishop_attacks_slow(Square sq, Bitboard block) {
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

    Bitboard rook_attacks_slow(Square sq, Bitboard block) {
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

    void init_magics() {
        int r_idx = 0;
        int b_idx = 0;

        // Calculate offsets
        for (int s = 0; s < 64; s++) {
            RookOffsets[s] = r_idx;
            r_idx += (1 << (64 - RookShifts[s]));

            BishopOffsets[s] = b_idx;
            b_idx += (1 << (64 - BishopShifts[s]));
        }

        RookTable.resize(r_idx);
        BishopTable.resize(b_idx);

        for (int s = 0; s < 64; s++) {
            Bitboard mask = mask_rook_attacks((Square)s);
            int bits = 64 - RookShifts[s]; // The number of set bits in mask should match this? No.
            // bits = number of relevant bits in occupancy.
            // 64 - shift = bits.
            // The mask should have 'bits' number of set bits.
            // Actually popcount(mask) == bits.

            int combinations = (1 << bits);
            for (int i = 0; i < combinations; i++) {
                 Bitboard occ = 0;
                 int idx = 0;
                 Bitboard m = mask;
                 while(m) {
                     int bit = std::countr_zero(m); // or __builtin_ctzll
                     m &= m - 1;
                     if (i & (1 << idx)) {
                         occ |= (1ULL << bit);
                     }
                     idx++;
                 }

                 // Magic Index
                 uint64_t magic_idx = (occ * RookMagics[s]) >> RookShifts[s];
                 RookTable[RookOffsets[s] + magic_idx] = rook_attacks_slow((Square)s, occ);
            }
        }

        for (int s = 0; s < 64; s++) {
            Bitboard mask = mask_bishop_attacks((Square)s);
            int bits = 64 - BishopShifts[s];
            int combinations = (1 << bits);
            for (int i = 0; i < combinations; i++) {
                 Bitboard occ = 0;
                 int idx = 0;
                 Bitboard m = mask;
                 while(m) {
                     int bit = std::countr_zero(m);
                     m &= m - 1;
                     if (i & (1 << idx)) {
                         occ |= (1ULL << bit);
                     }
                     idx++;
                 }

                 // Magic Index
                 uint64_t magic_idx = (occ * BishopMagics[s]) >> BishopShifts[s];
                 BishopTable[BishopOffsets[s] + magic_idx] = bishop_attacks_slow((Square)s, occ);
            }
        }
    }

    void init() {
        // Leapers
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

        init_magics();
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

    Bitboard get_bishop_attacks(Square sq, Bitboard occ) {
        occ &= mask_bishop_attacks(sq);
        occ *= BishopMagics[sq];
        occ >>= BishopShifts[sq];
        return BishopTable[BishopOffsets[sq] + occ];
    }

    Bitboard get_rook_attacks(Square sq, Bitboard occ) {
        occ &= mask_rook_attacks(sq);
        occ *= RookMagics[sq];
        occ >>= RookShifts[sq];
        return RookTable[RookOffsets[sq] + occ];
    }

    Bitboard get_queen_attacks(Square sq, Bitboard occ) {
        return get_bishop_attacks(sq, occ) | get_rook_attacks(sq, occ);
    }

} // namespace Bitboards
