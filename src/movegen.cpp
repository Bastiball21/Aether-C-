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
        constexpr Color Them = (Us == WHITE) ? BLACK : WHITE;
        Bitboard occ = pos.pieces();
        Square king_from = (Square)Bitboards::lsb(pos.pieces(KING, Us));

        auto try_castle = [&](int right_mask, int side_index, Square king_to, Square rook_to) {
            if (!(rights & right_mask)) return;

            Square rook_from = pos.castling_rook_from(Us, side_index);
            if (rook_from == SQ_NONE) return;

            if (rank_of(king_from) != rank_of(rook_from)) return;

            Piece expected_rook = (Us == WHITE) ? W_ROOK : B_ROOK;
            if (pos.piece_on(rook_from) != expected_rook) return;

            int king_file = file_of(king_from);
            int rook_file = file_of(rook_from);
            int step = (rook_file > king_file) ? 1 : -1;
            for (int f = king_file + step; f != rook_file; f += step) {
                Square sq = square_of((File)f, rank_of(king_from));
                if (Bitboards::check_bit(occ, sq)) return;
            }

            if (king_from != king_to) {
                int king_step = (file_of(king_to) > king_file) ? 1 : -1;
                for (int f = king_file + king_step;; f += king_step) {
                    Square sq = square_of((File)f, rank_of(king_from));
                    if (sq != rook_from && Bitboards::check_bit(occ, sq)) return;
                    if (pos.is_attacked(sq, Them)) return;
                    if (sq == king_to) break;
                }
            }

            if (rook_to != rook_from && rook_to != king_from && Bitboards::check_bit(occ, rook_to)) return;

            list.add(encode(king_from, king_to, side_index == 0 ? 2 : 3));
        };

        if (Us == WHITE) {
            try_castle(1, 0, SQ_G1, SQ_F1);
            try_castle(2, 1, SQ_C1, SQ_D1);
        } else {
            try_castle(4, 0, SQ_G8, SQ_F8);
            try_castle(8, 1, SQ_C8, SQ_D8);
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
        if (move == 0) return false;
        Square from = (Square)((move >> 6) & 0x3F);
        Square to = (Square)(move & 0x3F);
        int flags = (move >> 12);

        Piece p = pos.piece_on(from);
        if (p == NO_PIECE) return false;

        Color stm = pos.side_to_move();
        if ((p / 6) != stm) return false;

        PieceType pt = (PieceType)(p % 6);
        Piece target = pos.piece_on(to);

        // Check target occupancy for non-castling (Castling handles it separately usually, or verifies empty)
        // Castling moves (flags 2,3) destination is King's destination.
        // Actually for King moves, target check applies.
        // But Castling is special.
        if (flags == 2 || flags == 3) {
            if (pt != KING) return false;
            if (pos.in_check()) return false;

            int rights = pos.castling_rights_mask();
            int side_index = (flags == 3) ? 1 : 0; // 2=King(0), 3=Queen(1)
            int right_mask = (stm == WHITE) ? (side_index == 0 ? 1 : 2) : (side_index == 0 ? 4 : 8);

            if (!(rights & right_mask)) return false;

            Square rook_from = pos.castling_rook_from(stm, side_index);
            if (rook_from == SQ_NONE) return false;

            // Replicate generate_castling logic
            if (rank_of(from) != rank_of(rook_from)) return false;

            Piece expected_rook = (stm == WHITE) ? W_ROOK : B_ROOK;
            if (pos.piece_on(rook_from) != expected_rook) return false;

            // Target check
            Square expected_king_to = SQ_NONE;
            Square expected_rook_to = SQ_NONE;

            if (stm == WHITE) {
                if (side_index == 0) { expected_king_to = SQ_G1; expected_rook_to = SQ_F1; }
                else { expected_king_to = SQ_C1; expected_rook_to = SQ_D1; }
            } else {
                if (side_index == 0) { expected_king_to = SQ_G8; expected_rook_to = SQ_F8; }
                else { expected_king_to = SQ_C8; expected_rook_to = SQ_D8; }
            }

            if (to != expected_king_to) return false;

            int king_file = file_of(from);
            int rook_file = file_of(rook_from);
            Bitboard occ = pos.pieces();

            int step = (rook_file > king_file) ? 1 : -1;
            for (int f = king_file + step; f != rook_file; f += step) {
                Square sq = square_of((File)f, rank_of(from));
                if (Bitboards::check_bit(occ, sq)) return false;
            }

            Color Them = (stm == WHITE) ? BLACK : WHITE;
            if (from != to) {
                int king_step = (file_of(to) > king_file) ? 1 : -1;
                for (int f = king_file + king_step;; f += king_step) {
                    Square sq = square_of((File)f, rank_of(from));
                    if (sq != rook_from && Bitboards::check_bit(occ, sq)) return false;
                    if (pos.is_attacked(sq, Them)) return false;
                    if (sq == to) break;
                }
            }

            if (expected_rook_to != rook_from && expected_rook_to != from && Bitboards::check_bit(occ, expected_rook_to)) return false;

            return true;
        }

        // Standard Target Check
        if (target != NO_PIECE && (target / 6) == stm) return false;

        if (pt == PAWN) {
            Direction up = (stm == WHITE) ? NORTH : SOUTH;
            Rank rank7 = (stm == WHITE) ? RANK_7 : RANK_2;
            Rank start_rank = (stm == WHITE) ? RANK_2 : RANK_7;

            // Quiet (0)
            if (flags == 0) {
                 if (to != from + up) return false;
                 if (target != NO_PIECE) return false;
                 if (rank_of(from) == rank7) return false; // Should be promo
                 return true;
            }
            // Double Push (1)
            if (flags == 1) {
                 if (rank_of(from) != start_rank) return false;
                 if (to != from + up + up) return false;
                 if (target != NO_PIECE) return false;
                 if (pos.piece_on(from + up) != NO_PIECE) return false;
                 return true;
            }
            // Capture (4)
            if (flags == 4) {
                 Bitboard att = Bitboards::get_pawn_attacks(from, stm);
                 if (!Bitboards::check_bit(att, to)) return false;
                 if (target == NO_PIECE || (target / 6) == stm) return false;
                 if (rank_of(from) == rank7) return false; // Should be promo cap
                 return true;
            }
            // EP (5)
            if (flags == 5) {
                 if (to != pos.en_passant_square()) return false;
                 Bitboard att = Bitboards::get_pawn_attacks(from, stm);
                 if (!Bitboards::check_bit(att, to)) return false;
                 if (target != NO_PIECE) return false;
                 return true;
            }
            // Promos (8-11)
            if (flags >= 8 && flags <= 11) {
                 if (rank_of(from) != rank7) return false;
                 if (to != from + up) return false;
                 if (target != NO_PIECE) return false;
                 return true;
            }
            // Promo Caps (12-15)
            if (flags >= 12 && flags <= 15) {
                 if (rank_of(from) != rank7) return false;
                 Bitboard att = Bitboards::get_pawn_attacks(from, stm);
                 if (!Bitboards::check_bit(att, to)) return false;
                 if (target == NO_PIECE || (target / 6) == stm) return false;
                 return true;
            }
            return false;
        }

        // Piece moves
        Bitboard attacks = 0;
        if (pt == KNIGHT) attacks = Bitboards::get_knight_attacks(from);
        else if (pt == BISHOP) attacks = Bitboards::get_bishop_attacks(from, pos.pieces());
        else if (pt == ROOK) attacks = Bitboards::get_rook_attacks(from, pos.pieces());
        else if (pt == QUEEN) attacks = Bitboards::get_queen_attacks(from, pos.pieces());
        else if (pt == KING) attacks = Bitboards::get_king_attacks(from);

        // Capture flag consistency
        bool is_capture = (target != NO_PIECE);
        if (is_capture && flags != 4) return false; // Capturing but flag says quiet?
        if (!is_capture && flags != 0) return false; // Not capturing but flag says capture?

        return Bitboards::check_bit(attacks, to);
    }

} // namespace MoveGen
