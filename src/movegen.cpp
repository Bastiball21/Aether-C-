#include "movegen.h"

namespace MoveGen {

    // Helper to encode move
    // 0-5: to, 6-11: from, 12-15: flags
    inline uint16_t encode(Square from, Square to, int flags) {
        return (uint16_t)(to | (from << 6) | (flags << 12));
    }

    template<Color Us, bool GenQuiet, bool GenCapture>
    void generate_pawn_moves(const Position& pos, MoveList& list) {
        constexpr Color Them = (Us == WHITE) ? BLACK : WHITE;
        constexpr Direction Up = (Us == WHITE) ? NORTH : SOUTH;
        constexpr Direction UpRight = (Us == WHITE) ? NORTH_EAST : SOUTH_WEST;
        constexpr Direction UpLeft = (Us == WHITE) ? NORTH_WEST : SOUTH_EAST;
        constexpr Rank Rank7 = (Us == WHITE) ? RANK_7 : RANK_2;
        constexpr Rank StartRank = (Us == WHITE) ? RANK_2 : RANK_7;

        Bitboard pawns = pos.pieces(PAWN, Us);
        Bitboard enemies = pos.pieces(Them);
        Bitboard empty = ~pos.pieces();

        // Single Push
        if (GenQuiet) {
            Bitboard push_one = 0;
            if (Us == WHITE) push_one = (pawns << 8) & empty;
            else push_one = (pawns >> 8) & empty;

            Bitboard b = push_one;
            while (b) {
                Square to = (Square)Bitboards::pop_lsb(b);
                Square from = to - Up;
                if (rank_of(from) == Rank7) { // Promotion
                    list.add(encode(from, to, 8)); // N
                    list.add(encode(from, to, 9)); // B
                    list.add(encode(from, to, 10)); // R
                    list.add(encode(from, to, 11)); // Q
                } else {
                    list.add(encode(from, to, 0));
                    // Double Push
                    if (rank_of(from) == StartRank) {
                        Square to2 = to + Up;
                        if (Bitboards::check_bit(empty, to2)) {
                            list.add(encode(from, to2, 1));
                        }
                    }
                }
            }
        }

        // Captures
        if (GenCapture) {
            // Left/Right attacks
            auto gen_caps = [&](Direction dir) {
                Bitboard attacks = 0;
                if (Us == WHITE) {
                     // Check file wrap.
                     if (dir == NORTH_WEST) attacks = (pawns & ~Bitboards::FileA) << 7;
                     else attacks = (pawns & ~Bitboards::FileH) << 9;
                } else {
                     if (dir == SOUTH_WEST) attacks = (pawns & ~Bitboards::FileA) >> 9;
                     else attacks = (pawns & ~Bitboards::FileH) >> 7;
                }

                Bitboard common = attacks & enemies;
                while (common) {
                    Square to = (Square)Bitboards::pop_lsb(common);
                    Square from = to - dir;

                     if (rank_of(from) == Rank7) { // Promo Capture
                        list.add(encode(from, to, 12));
                        list.add(encode(from, to, 13));
                        list.add(encode(from, to, 14));
                        list.add(encode(from, to, 15));
                    } else {
                        list.add(encode(from, to, 4));
                    }
                }

                // EP
                if (pos.en_passant_square() != SQ_NONE) {
                    Bitboard ep_bb = (1ULL << pos.en_passant_square());
                    if (attacks & ep_bb) {
                        Square to = pos.en_passant_square();
                        Square from = to - dir;
                        list.add(encode(from, to, 5));
                    }
                }
            };

            gen_caps(UpLeft);
            gen_caps(UpRight);
        }
    }

    template<Color Us, bool GenQuiet, bool GenCapture>
    void generate_piece_moves(const Position& pos, MoveList& list) {
        constexpr Color Them = (Us == WHITE) ? BLACK : WHITE;
        Bitboard enemies = pos.pieces(Them);
        Bitboard occ = pos.pieces();

        auto gen = [&](PieceType pt) {
            Bitboard pieces = pos.pieces(pt, Us);
            while (pieces) {
                Square from = (Square)Bitboards::pop_lsb(pieces);
                Bitboard attacks = 0;
                if (pt == KNIGHT) attacks = Bitboards::get_knight_attacks(from);
                else if (pt == BISHOP) attacks = Bitboards::get_bishop_attacks(from, occ);
                else if (pt == ROOK) attacks = Bitboards::get_rook_attacks(from, occ);
                else if (pt == QUEEN) attacks = Bitboards::get_queen_attacks(from, occ);
                else if (pt == KING) attacks = Bitboards::get_king_attacks(from);

                Bitboard captures = attacks & enemies;
                Bitboard quiets = attacks & ~occ;

                if (GenCapture) {
                    while (captures) {
                        Square to = (Square)Bitboards::pop_lsb(captures);
                        list.add(encode(from, to, 4));
                    }
                }
                if (GenQuiet) {
                    while (quiets) {
                        Square to = (Square)Bitboards::pop_lsb(quiets);
                        list.add(encode(from, to, 0));
                    }
                }
            }
        };

        gen(KNIGHT);
        gen(BISHOP);
        gen(ROOK);
        gen(QUEEN);
        gen(KING);
    }

    template<Color Us>
    void generate_castling(const Position& pos, MoveList& list) {
        if (pos.in_check()) return;

        int rights = pos.castling_rights_mask();
        Bitboard occ = pos.pieces();

        if (Us == WHITE) {
            if (rights & 1) { // K
                if (!Bitboards::check_bit(occ, SQ_F1) && !Bitboards::check_bit(occ, SQ_G1)) {
                    if (!pos.is_attacked(SQ_F1, BLACK) && !pos.is_attacked(SQ_G1, BLACK)) {
                        list.add(encode(SQ_E1, SQ_G1, 2));
                    }
                }
            }
            if (rights & 2) { // Q
                if (!Bitboards::check_bit(occ, SQ_D1) && !Bitboards::check_bit(occ, SQ_C1) && !Bitboards::check_bit(occ, SQ_B1)) {
                    if (!pos.is_attacked(SQ_D1, BLACK) && !pos.is_attacked(SQ_C1, BLACK)) {
                         list.add(encode(SQ_E1, SQ_C1, 3));
                    }
                }
            }
        } else {
            if (rights & 4) { // k
                if (!Bitboards::check_bit(occ, SQ_F8) && !Bitboards::check_bit(occ, SQ_G8)) {
                     if (!pos.is_attacked(SQ_F8, WHITE) && !pos.is_attacked(SQ_G8, WHITE)) {
                        list.add(encode(SQ_E8, SQ_G8, 2));
                    }
                }
            }
            if (rights & 8) { // q
                if (!Bitboards::check_bit(occ, SQ_D8) && !Bitboards::check_bit(occ, SQ_C8) && !Bitboards::check_bit(occ, SQ_B8)) {
                    if (!pos.is_attacked(SQ_D8, WHITE) && !pos.is_attacked(SQ_C8, WHITE)) {
                        list.add(encode(SQ_E8, SQ_C8, 3));
                    }
                }
            }
        }
    }

    void generate_all(const Position& pos, MoveList& list) {
        list.count = 0;
        if (pos.side_to_move() == WHITE) {
            generate_pawn_moves<WHITE, true, true>(pos, list);
            generate_piece_moves<WHITE, true, true>(pos, list);
            generate_castling<WHITE>(pos, list);
        } else {
            generate_pawn_moves<BLACK, true, true>(pos, list);
            generate_piece_moves<BLACK, true, true>(pos, list);
            generate_castling<BLACK>(pos, list);
        }
    }

    void generate_captures(const Position& pos, MoveList& list) {
         list.count = 0;
         if (pos.side_to_move() == WHITE) {
            generate_pawn_moves<WHITE, false, true>(pos, list);
            generate_piece_moves<WHITE, false, true>(pos, list);
        } else {
            generate_pawn_moves<BLACK, false, true>(pos, list);
            generate_piece_moves<BLACK, false, true>(pos, list);
        }
    }

    void generate_quiets(const Position& pos, MoveList& list) {
         list.count = 0;
         if (pos.side_to_move() == WHITE) {
            generate_pawn_moves<WHITE, true, false>(pos, list);
            generate_piece_moves<WHITE, true, false>(pos, list);
            generate_castling<WHITE>(pos, list);
        } else {
            generate_pawn_moves<BLACK, true, false>(pos, list);
            generate_piece_moves<BLACK, true, false>(pos, list);
            generate_castling<BLACK>(pos, list);
        }
    }

    bool is_pseudo_legal(const Position& pos, uint16_t move) {
        // Fallback: Generate all moves and check
        // This is slow but 100% robust
        MoveList list;
        generate_all(pos, list);
        for(int i=0; i<list.count; i++) {
            if (list.moves[i] == move) return true;
        }
        return false;
    }

} // namespace MoveGen
