#include "position.h"
#include <sstream>
#include <cstring>

// Zobrist Keys (Placeholder)
// In a real engine, these should be initialized with random numbers.
namespace Zobrist {
    Key psq[12][64];
    Key side;
    Key castle[16];
    Key enpassant[65]; // 64 + none

    void init() {
        // Init with pseudo-random numbers
        uint64_t seed = 1070372;
        auto rand64 = [&]() {
            seed ^= seed >> 12;
            seed ^= seed << 25;
            seed ^= seed >> 27;
            return seed * 2685821657736338717LL;
        };

        for (int p = 0; p < 12; p++)
            for (int s = 0; s < 64; s++)
                psq[p][s] = rand64();

        side = rand64();

        for (int i = 0; i < 16; i++)
            castle[i] = rand64();

        for (int i = 0; i < 65; i++)
            enpassant[i] = rand64();
    }
}

Position::Position() {
    // Ensure Zobrist is init
    static bool init = false;
    if (!init) {
        Zobrist::init();
        Bitboards::init();
        init = true;
    }
    set_startpos();
}

void Position::set_startpos() {
    set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

void Position::put_piece(Piece p, Square s) {
    board[s] = p;
    Bitboards::set_bit(piece_bb[p % 6], s);
    Bitboards::set_bit(color_bb[p / 6], s);
    st_key ^= Zobrist::psq[p][s];
}

void Position::remove_piece(Square s) {
    Piece p = board[s];
    board[s] = NO_PIECE;
    Bitboards::clear_bit(piece_bb[p % 6], s);
    Bitboards::clear_bit(color_bb[p / 6], s);
    st_key ^= Zobrist::psq[p][s];
}

void Position::move_piece(Square from, Square to) {
    Piece p = board[from];
    remove_piece(from);
    put_piece(p, to);
}

void Position::set(const std::string& fen) {
    // Clear
    std::memset(piece_bb, 0, sizeof(piece_bb));
    std::memset(color_bb, 0, sizeof(color_bb));
    for (int i=0; i<64; i++) board[i] = NO_PIECE;
    side = WHITE;
    ep_square = SQ_NONE;
    castling = 0;
    rule50 = 0;
    halfmove_clock = 0;
    st_key = 0;
    history.clear();

    std::stringstream ss(fen);
    std::string token;

    // 1. Placement
    ss >> token;
    int rank = 7;
    int file = 0;
    for (char c : token) {
        if (c == '/') {
            rank--;
            file = 0;
        } else if (isdigit(c)) {
            file += c - '0';
        } else {
            Piece p = NO_PIECE;
            switch (c) {
                case 'P': p = W_PAWN; break;
                case 'N': p = W_KNIGHT; break;
                case 'B': p = W_BISHOP; break;
                case 'R': p = W_ROOK; break;
                case 'Q': p = W_QUEEN; break;
                case 'K': p = W_KING; break;
                case 'p': p = B_PAWN; break;
                case 'n': p = B_KNIGHT; break;
                case 'b': p = B_BISHOP; break;
                case 'r': p = B_ROOK; break;
                case 'q': p = B_QUEEN; break;
                case 'k': p = B_KING; break;
            }
            put_piece(p, square_of((File)file, (Rank)rank));
            file++;
        }
    }

    // 2. Side
    ss >> token;
    if (token == "b") {
        side = BLACK;
        st_key ^= Zobrist::side;
    }

    // 3. Castling
    ss >> token;
    if (token != "-") {
        for (char c : token) {
            if (c == 'K') castling |= 1;
            else if (c == 'Q') castling |= 2;
            else if (c == 'k') castling |= 4;
            else if (c == 'q') castling |= 8;
        }
    }
    st_key ^= Zobrist::castle[castling];

    // 4. EP
    ss >> token;
    if (token != "-") {
        File f = (File)(token[0] - 'a');
        Rank r = (Rank)(token[1] - '1');
        ep_square = square_of(f, r);
        st_key ^= Zobrist::enpassant[ep_square];
    } else {
        ep_square = SQ_NONE;
        st_key ^= Zobrist::enpassant[SQ_NONE];
    }

    // 5. Rule50
    ss >> rule50;

    // 6. Fullmove
    int fullmove;
    ss >> fullmove;
    halfmove_clock = (fullmove - 1) * 2 + (side == BLACK);
}

// Basic move encoding:
// 0-5: from, 6-11: to, 12-13: type (0=norm, 1=promo, 2=enpassant, 3=castling), 14-15: promo type (0=N,1=B,2=R,3=Q)
// Actually standard is:
// bits 0-5: dest
// bits 6-11: src
// bits 12-13: promotion piece type - 2 (so 0=N, 1=B, 2=R, 3=Q)
// bits 14-15: flags?
// Let's use a simpler 16-bit encoding:
// 0-5: to
// 6-11: from
// 12-15: flags
// Flags:
// 0: quiet
// 1: double pawn push
// 2: king castle
// 3: queen castle
// 4: capture
// 5: ep capture
// 8+promo: promotion (8=N, 9=B, 10=R, 11=Q)
// 12+promo: promo capture
//
// But wait, the Rust code uses: `pub struct Move { uint16_t data }`
// And `eval.rs` / `search.rs` don't show the encoding.
// I will implement a standard encoding.
// Bits 0-5: To
// Bits 6-11: From
// Bits 12-15: Flag
// Flag:
// 0000: Quiet
// 0001: Double Pawn Push
// 0010: King Castle
// 0011: Queen Castle
// 0100: Capture
// 0101: EP Capture
// 1000: Promo N
// 1001: Promo B
// 1010: Promo R
// 1011: Promo Q
// 1100: Promo Capture N
// 1101: Promo Capture B
// 1110: Promo Capture R
// 1111: Promo Capture Q

void Position::make_move(uint16_t move) {
    Square to = (Square)(move & 0x3F);
    Square from = (Square)((move >> 6) & 0x3F);
    int flag = (move >> 12);

    StateInfo si;
    si.key = st_key;
    si.castling = castling;
    si.ep_square = ep_square;
    si.rule50 = rule50;
    si.captured = NO_PIECE; // Will fill if capture

    // Update rule50
    rule50++;

    Piece p = board[from];
    PieceType pt = (PieceType)(p % 6);

    if (pt == PAWN) rule50 = 0;

    // Handle Capture
    if ((flag & 4) || (flag == 5)) { // Capture or EP
        rule50 = 0;
        if (flag == 5) { // EP
            Square cap_sq = (side == WHITE) ? (to + SOUTH) : (to + NORTH);
            si.captured = board[cap_sq];
            remove_piece(cap_sq);
        } else {
            si.captured = board[to];
            remove_piece(to);
        }
    }

    // Move Piece
    move_piece(from, to);

    // Promotion
    if (flag & 8) {
        PieceType promo_pt = (PieceType)((flag & 3) + 1); // N=1, B=2, R=3, Q=4
        Piece promo_p = (Piece)(promo_pt + (side == WHITE ? 0 : 6));
        remove_piece(to);
        put_piece(promo_p, to);
    }

    // Castling
    if (flag == 2) { // King Side
        Square r_from = (side == WHITE) ? SQ_H1 : SQ_H8;
        Square r_to = (side == WHITE) ? SQ_F1 : SQ_F8;
        move_piece(r_from, r_to);
    } else if (flag == 3) { // Queen Side
        Square r_from = (side == WHITE) ? SQ_A1 : SQ_A8;
        Square r_to = (side == WHITE) ? SQ_D1 : SQ_D8;
        move_piece(r_from, r_to);
    }

    // Update Castling Rights
    st_key ^= Zobrist::castle[castling];
    if (board[from] == W_KING || board[from] == B_KING) {
        if (side == WHITE) castling &= ~3;
        else castling &= ~12;
    }
    // Rooks moved or captured
    // Simplest: check from and to for Rooks starting squares
    auto check_rook = [&](Square sq) {
        if (sq == SQ_H1) castling &= ~1;
        else if (sq == SQ_A1) castling &= ~2;
        else if (sq == SQ_H8) castling &= ~4;
        else if (sq == SQ_A8) castling &= ~8;
    };
    check_rook(from);
    check_rook(to);
    st_key ^= Zobrist::castle[castling];

    // Update EP
    st_key ^= Zobrist::enpassant[ep_square];
    ep_square = SQ_NONE;
    if (flag == 1) { // Double Push
        ep_square = (Square)((from + to) / 2);
        st_key ^= Zobrist::enpassant[ep_square];
    } else {
        st_key ^= Zobrist::enpassant[SQ_NONE];
    }

    // Update Side
    side = ~side;
    st_key ^= Zobrist::side;

    // Push history
    history.push_back(si);
}

void Position::make_null_move() {
    StateInfo si;
    si.key = st_key;
    si.castling = castling;
    si.ep_square = ep_square;
    si.rule50 = rule50;
    si.captured = NO_PIECE;

    // Update rule50 (null move doesn't reset it? usually not)
    // Actually, usually it does NOT reset rule50.
    rule50++;

    // Clear EP
    if (ep_square != SQ_NONE) {
        st_key ^= Zobrist::enpassant[ep_square];
        ep_square = SQ_NONE;
        st_key ^= Zobrist::enpassant[SQ_NONE];
    }

    // Switch Side
    side = ~side;
    st_key ^= Zobrist::side;

    history.push_back(si);
}

void Position::unmake_null_move() {
    StateInfo si = history.back();
    history.pop_back();

    side = ~side;
    ep_square = si.ep_square;
    rule50 = si.rule50;
    st_key = si.key;
}

void Position::unmake_move(uint16_t move) {
    Square to = (Square)(move & 0x3F);
    Square from = (Square)((move >> 6) & 0x3F);
    int flag = (move >> 12);

    StateInfo si = history.back();
    history.pop_back();

    side = ~side; // Revert side

    // Move piece back
    // If promo, revert to Pawn
    if (flag & 8) {
        remove_piece(to);
        Piece pawn = (side == WHITE) ? W_PAWN : B_PAWN;
        put_piece(pawn, from);
    } else {
        move_piece(to, from);
    }

    // Restore Captured
    if ((flag & 4) || (flag == 5)) {
        if (flag == 5) { // EP
            Square cap_sq = (side == WHITE) ? (to + SOUTH) : (to + NORTH);
            put_piece(si.captured, cap_sq);
        } else {
            put_piece(si.captured, to);
        }
    }

    // Revert Castling Move
    if (flag == 2) {
        Square r_from = (side == WHITE) ? SQ_H1 : SQ_H8;
        Square r_to = (side == WHITE) ? SQ_F1 : SQ_F8;
        move_piece(r_to, r_from);
    } else if (flag == 3) {
        Square r_from = (side == WHITE) ? SQ_A1 : SQ_A8;
        Square r_to = (side == WHITE) ? SQ_D1 : SQ_D8;
        move_piece(r_to, r_from);
    }

    // Restore State
    castling = si.castling;
    ep_square = si.ep_square;
    rule50 = si.rule50;
    st_key = si.key;
}

bool Position::is_attacked(Square sq, Color by_side) const {
    // Pawn
    if (Bitboards::get_pawn_attacks(sq, ~by_side) & pieces(PAWN, by_side)) return true;
    // Knight
    if (Bitboards::get_knight_attacks(sq) & pieces(KNIGHT, by_side)) return true;
    // King
    if (Bitboards::get_king_attacks(sq) & pieces(KING, by_side)) return true;

    Bitboard occ = pieces();
    // Bishop/Queen
    if (Bitboards::get_bishop_attacks(sq, occ) & (pieces(BISHOP, by_side) | pieces(QUEEN, by_side))) return true;
    // Rook/Queen
    if (Bitboards::get_rook_attacks(sq, occ) & (pieces(ROOK, by_side) | pieces(QUEEN, by_side))) return true;

    return false;
}

bool Position::in_check() const {
    Square ksq = (Square)Bitboards::lsb(pieces(KING, side));
    return is_attacked(ksq, ~side);
}

bool Position::is_repetition() const {
    // Check current hash against history
    // Rule: if same position appears 3 times, it's a draw.
    // However, in search we often prune on *first* repetition (i.e. count >= 1 previous occurence)
    // because returning draw score (0) avoids cycles.
    // Standard practice: check backwards.
    // Also check rule50 for efficiency (irreversible moves reset repetition list effectively)
    // But we have full history.

    // We iterate backwards.
    int count = 0;
    // We start from history.size() - 2 because back() is current state pushed?
    // Wait, history only contains *previous* states. `make_move` pushes current state (key, rule50, etc) *before* update?
    // No. `make_move`:
    // si.key = st_key; ... history.push_back(si);
    // So history contains states *before* the move.
    // The current position state is in `st_key`.

    // So we check against history.
    // We only need to check up to rule50 plies back?
    // Irreversible moves (pawn move, capture) reset rule50 to 0.
    // Any position before an irreversible move cannot be repeated (because pawn structure change or piece count change).
    // So we only check `rule50` elements.

    int end = (int)history.size() - 1;
    int start = end - rule50;
    if (start < 0) start = 0;

    for (int i = end; i >= start; i--) {
        if (history[i].key == st_key) {
            return true; // Found 1 repetition -> 2nd occurrence.
            // Usually returns draw score immediately to avoid loops.
        }
    }
    return false;
}

std::string Position::fen() const {
    // Placeholder logic for FEN generation if needed, skipping for speed
    return "";
}
