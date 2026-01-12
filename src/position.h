#ifndef POSITION_H
#define POSITION_H

#include "types.h"
#include "bitboard.h"
#include <string>
#include <vector>

// Forward declaration
struct Move;

class Position {
public:
    struct StateInfo {
        Key key;
        Key pawn_key;
        int castling;
        Square ep_square;
        int rule50;
        Piece captured;
        int eval_mg;
        int eval_eg;
        int eval_phase;
    };

    Position();

    // Setup
    void set(const std::string& fen);
    void set_startpos();

    // Accessors
    Bitboard pieces(PieceType pt, Color c) const { return piece_bb[pt] & color_bb[c]; }
    Bitboard pieces(PieceType pt) const { return piece_bb[pt]; }
    Bitboard pieces(Color c) const { return color_bb[c]; }
    Bitboard pieces() const { return color_bb[WHITE] | color_bb[BLACK]; }

    Piece piece_on(Square s) const { return board[s]; }
    Color side_to_move() const { return side; }
    Key key() const { return st_key; }
    Key pawn_key() const { return p_key; }
    Square en_passant_square() const { return ep_square; }
    int castling_rights_mask() const { return castling; }
    int rule50_count() const { return rule50; }
    int eval_mg() const { return eval_mg_acc; }
    int eval_eg() const { return eval_eg_acc; }
    int eval_phase() const { return eval_phase_acc; }

    // Material Helper
    int non_pawn_material(Color c) const;

    // Modification
    void make_move(uint16_t move); // Move is uint16_t encoded
    void unmake_move(uint16_t move);

    // Null Move
    void make_null_move();
    void unmake_null_move();

    // Helpers
    bool is_attacked(Square sq, Color by_side) const;
    bool in_check() const;
    bool is_repetition() const;

    const StateInfo* state() const { return &history.back(); }

    // Debug
#ifndef NDEBUG
    void debug_validate() const;
#endif

    // FEN
    std::string fen() const;

private:
    void put_piece(Piece p, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);

    // Data
    Bitboard piece_bb[PIECE_TYPE_NB];
    Bitboard color_bb[COLOR_NB];
    Piece board[64];

    Color side;
    Square ep_square;
    int castling; // Format: 1=WK, 2=WQ, 4=BK, 8=BQ
    int rule50;
    int halfmove_clock; // Total plies

    Key st_key;
    Key p_key;
    int eval_mg_acc;
    int eval_eg_acc;
    int eval_phase_acc;

    std::vector<StateInfo> history;
};

#endif // POSITION_H
