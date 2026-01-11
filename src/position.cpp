#include "position.h"
#include "nnue/nnue.h"
#include "nnue/features.h"
#include <sstream>
#include <cstring>
#include <cassert>

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
#ifndef NDEBUG
    assert(s >= 0 && s < 64);
#endif
    if (p == NO_PIECE) {
        remove_piece(s);
        return;
    }
    board[s] = p;
    Bitboards::set_bit(piece_bb[p % 6], s);
    Bitboards::set_bit(color_bb[p / 6], s);
    st_key ^= Zobrist::psq[p][s];
    if ((p % 6) == PAWN) p_key ^= Zobrist::psq[p][s];
}

void Position::remove_piece(Square s) {
#ifndef NDEBUG
    assert(s >= 0 && s < 64);
#endif
    Piece p = board[s];
    if (p == NO_PIECE) return;

    board[s] = NO_PIECE;
    Bitboards::clear_bit(piece_bb[p % 6], s);
    Bitboards::clear_bit(color_bb[p / 6], s);
    st_key ^= Zobrist::psq[p][s];
    if ((p % 6) == PAWN) p_key ^= Zobrist::psq[p][s];
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
    p_key = 0;
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

    // Initial StateInfo
    StateInfo si;
    si.key = st_key;
    si.pawn_key = p_key;
    si.castling = castling;
    si.ep_square = ep_square;
    si.rule50 = rule50;
    si.captured = NO_PIECE;

    // Refresh NNUE accumulators
    if (NNUE::IsLoaded) {
        NNUE::refresh_accumulator(*this, WHITE, si.accumulators[WHITE]);
        NNUE::refresh_accumulator(*this, BLACK, si.accumulators[BLACK]);
    }

    history.push_back(si);
}

int Position::non_pawn_material(Color c) const {
    int mat = 0;
    mat += Bitboards::count(pieces(KNIGHT, c)) * 320;
    mat += Bitboards::count(pieces(BISHOP, c)) * 330;
    mat += Bitboards::count(pieces(ROOK, c)) * 500;
    mat += Bitboards::count(pieces(QUEEN, c)) * 900;
    return mat;
}

// Internal helper to update accumulator
static void update_acc(NNUE::Accumulator& acc, int16_t* weights,
                       Piece p, Square from, Square to, Square k_sq, Color perspective) {
    int idx_rem = NNUE::feature_index(k_sq, p, from, perspective);
    int idx_add = NNUE::feature_index(k_sq, p, to, perspective);

    if (idx_rem != -1) {
        const int16_t* w_rem = weights + (idx_rem * NNUE::kFeatureTransformerOutput);
        for (int i = 0; i < NNUE::kFeatureTransformerOutput; ++i) {
            acc.values[i] -= w_rem[i];
        }
    }

    if (idx_add != -1) {
        const int16_t* w_add = weights + (idx_add * NNUE::kFeatureTransformerOutput);
        for (int i = 0; i < NNUE::kFeatureTransformerOutput; ++i) {
            acc.values[i] += w_add[i];
        }
    }
}

static void remove_acc(NNUE::Accumulator& acc, int16_t* weights,
                       Piece p, Square sq, Square k_sq, Color perspective) {
    int idx = NNUE::feature_index(k_sq, p, sq, perspective);
    if (idx == -1) return;

    const int16_t* w = weights + (idx * NNUE::kFeatureTransformerOutput);
    for (int i = 0; i < NNUE::kFeatureTransformerOutput; ++i) {
        acc.values[i] -= w[i];
    }
}

static void add_acc(NNUE::Accumulator& acc, int16_t* weights,
                    Piece p, Square sq, Square k_sq, Color perspective) {
    int idx = NNUE::feature_index(k_sq, p, sq, perspective);
    if (idx == -1) return;

    const int16_t* w = weights + (idx * NNUE::kFeatureTransformerOutput);
    for (int i = 0; i < NNUE::kFeatureTransformerOutput; ++i) {
        acc.values[i] += w[i];
    }
}

void Position::make_move(uint16_t move) {
#ifndef NDEBUG
    debug_validate();
#endif
    Square to = (Square)(move & 0x3F);
    Square from = (Square)((move >> 6) & 0x3F);
    int flag = (move >> 12);

    // Save current accumulators before modification
    NNUE::Accumulator prev_acc[2];
    bool use_nnue = NNUE::IsLoaded;
    if (use_nnue) {
        prev_acc[WHITE] = history.back().accumulators[WHITE];
        prev_acc[BLACK] = history.back().accumulators[BLACK];
    }

    StateInfo si;
    si.key = st_key;
    si.pawn_key = p_key;
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
    Piece captured_piece = NO_PIECE;
    Square capture_sq = to;
    if ((flag & 4) || (flag == 5)) { // Capture or EP
        rule50 = 0;
        if (flag == 5) { // EP
            capture_sq = (side == WHITE) ? (to + SOUTH) : (to + NORTH);
        }
        captured_piece = board[capture_sq];
        si.captured = captured_piece;
        remove_piece(capture_sq);
    }

    // Move Piece
    move_piece(from, to);

    // Promotion
    Piece promo_piece = NO_PIECE;
    if (flag & 8) {
        PieceType promo_pt = (PieceType)((flag & 3) + 1); // N=1, B=2, R=3, Q=4
        promo_piece = (Piece)(promo_pt + (side == WHITE ? 0 : 6));
        remove_piece(to);
        put_piece(promo_piece, to);
    }

    // Castling
    bool castling_move = false;
    Square rook_from = SQ_NONE, rook_to = SQ_NONE;
    Piece rook_piece = NO_PIECE;
    if (flag == 2) { // King Side
        castling_move = true;
        rook_from = (side == WHITE) ? SQ_H1 : SQ_H8;
        rook_to = (side == WHITE) ? SQ_F1 : SQ_F8;
        rook_piece = (side == WHITE) ? W_ROOK : B_ROOK;
        move_piece(rook_from, rook_to);
    } else if (flag == 3) { // Queen Side
        castling_move = true;
        rook_from = (side == WHITE) ? SQ_A1 : SQ_A8;
        rook_to = (side == WHITE) ? SQ_D1 : SQ_D8;
        rook_piece = (side == WHITE) ? W_ROOK : B_ROOK;
        move_piece(rook_from, rook_to);
    }

    // Update Castling Rights
    st_key ^= Zobrist::castle[castling];
    if (board[from] == W_KING || board[from] == B_KING) {
        if (side == WHITE) castling &= ~3;
        else castling &= ~12;
    }
    // Rooks moved or captured
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

    // --- NNUE Update ---
    if (use_nnue) {
        // Copy base accumulators
        si.accumulators[WHITE] = prev_acc[WHITE];
        si.accumulators[BLACK] = prev_acc[BLACK];

        // Apply updates
        auto do_update = [&](Color perspective) {
            NNUE::Accumulator& acc = si.accumulators[perspective];

            bool is_king_move = (pt == KING);
            bool is_own_king_move = (is_king_move && (perspective == ~side));

            if (is_own_king_move) {
                // Full Refresh
                NNUE::refresh_accumulator(*this, perspective, acc);
                return;
            }

            Square k_sq = Bitboards::lsb(pieces(KING, perspective));
            int16_t* weights = NNUE::GlobalNetwork.ft_weights.data();

            // 2. Remove Captured Piece
            if (captured_piece != NO_PIECE) {
                remove_acc(acc, weights, captured_piece, capture_sq, k_sq, perspective);
            }

            // 3. Move Piece (Remove From, Add To)
            if (flag & 8) { // Promotion
                // Remove Pawn
                remove_acc(acc, weights, p, from, k_sq, perspective);
                // Add Promo Piece
                add_acc(acc, weights, promo_piece, to, k_sq, perspective);
            } else {
                update_acc(acc, weights, p, from, to, k_sq, perspective);
            }

            // Castling Rook Logic
            if (castling_move) {
                update_acc(acc, weights, rook_piece, rook_from, rook_to, k_sq, perspective);
            }
        };

        do_update(WHITE);
        do_update(BLACK);
    }

    // Push history
    history.push_back(si);
}

void Position::make_null_move() {
    StateInfo si;
    si.key = st_key;
    si.pawn_key = p_key;
    si.castling = castling;
    si.ep_square = ep_square;
    si.rule50 = rule50;
    si.captured = NO_PIECE;

    // Copy NNUE accumulators
    if (NNUE::IsLoaded && !history.empty()) {
        si.accumulators[WHITE] = history.back().accumulators[WHITE];
        si.accumulators[BLACK] = history.back().accumulators[BLACK];
    }

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
    p_key = si.pawn_key;
}

void Position::unmake_move(uint16_t move) {
#ifndef NDEBUG
    debug_validate();
#endif
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
    p_key = si.pawn_key;
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
    int end = (int)history.size() - 1;
    int start = end - rule50;
    if (start < 0) start = 0;

    for (int i = end; i >= start; i--) {
        if (history[i].key == st_key) {
            return true;
        }
    }
    return false;
}

std::string Position::fen() const {
    // Piece placement
    std::string out;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            Square sq = square_of((File)f, (Rank)r);
            Piece p = board[sq];
            if (p == NO_PIECE) {
                empty++;
                continue;
            }
            if (empty) {
                out += char('0' + empty);
                empty = 0;
            }

            char c = '?';
            switch (p) {
                case W_PAWN:   c = 'P'; break;
                case W_KNIGHT: c = 'N'; break;
                case W_BISHOP: c = 'B'; break;
                case W_ROOK:   c = 'R'; break;
                case W_QUEEN:  c = 'Q'; break;
                case W_KING:   c = 'K'; break;
                case B_PAWN:   c = 'p'; break;
                case B_KNIGHT: c = 'n'; break;
                case B_BISHOP: c = 'b'; break;
                case B_ROOK:   c = 'r'; break;
                case B_QUEEN:  c = 'q'; break;
                case B_KING:   c = 'k'; break;
                default:       c = '?'; break;
            }
            out += c;
        }
        if (empty) out += char('0' + empty);
        if (r) out += '/';
    }

    // Side to move
    out += (side == WHITE) ? " w " : " b ";

    // Castling rights
    if (castling == 0) {
        out += "- ";
    } else {
        if (castling & 1) out += 'K';
        if (castling & 2) out += 'Q';
        if (castling & 4) out += 'k';
        if (castling & 8) out += 'q';
        out += ' ';
    }

    // En passant
    if (ep_square == SQ_NONE) {
        out += "- ";
    } else {
        out += char('a' + file_of(ep_square));
        out += char('1' + rank_of(ep_square));
        out += ' ';
    }

    // Halfmove clock (rule50) and fullmove number
    int fullmove = halfmove_clock / 2 + 1;
    out += std::to_string(rule50);
    out += ' ';
    out += std::to_string(fullmove);

    return out;
}


#ifndef NDEBUG
void Position::debug_validate() const {
    // 1. Check Kings
    Bitboard wk = pieces(KING, WHITE);
    Bitboard bk = pieces(KING, BLACK);
    assert(Bitboards::count(wk) == 1 && "White king count must be 1");
    assert(Bitboards::count(bk) == 1 && "Black king count must be 1");

    // 2. Overlap check
    Bitboard occ = pieces();
    Bitboard accum = 0;
    for (int c = 0; c < COLOR_NB; ++c) {
        for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
            Bitboard bb = pieces((PieceType)pt, (Color)c);
            assert(!(accum & bb) && "Piece overlap detected");
            accum |= bb;
        }
    }
    assert(accum == occ && "Occupancy mismatch");

    // 3. Side check
    assert((side == WHITE || side == BLACK) && "Invalid side to move");

    // 4. En Passant check
    if (ep_square != SQ_NONE) {
        assert(ep_square >= SQ_A1 && ep_square <= SQ_H8 && "Invalid EP square");
        Rank r = rank_of(ep_square);
        if (side == WHITE) {
            assert(r == RANK_6 && "EP square rank invalid for White to move");
        } else {
            assert(r == RANK_3 && "EP square rank invalid for Black to move");
        }
    }

    // 5. Board vs Bitboard consistency
    for (int s = 0; s < 64; ++s) {
        Piece p = board[s];
        if (p == NO_PIECE) {
            if (Bitboards::check_bit(occ, (Square)s)) {
                 std::cerr << "Sync Error: Bitboard set at " << s << " but Board empty" << std::endl;
                 assert(false);
            }
        } else {
            if (!Bitboards::check_bit(piece_bb[p % 6], (Square)s)) {
                 std::cerr << "Sync Error: Board has piece " << p << " at " << s << " but Bitboard missing" << std::endl;
                 assert(false);
            }
            if (!Bitboards::check_bit(color_bb[p / 6], (Square)s)) {
                 std::cerr << "Sync Error: Board has piece " << p << " at " << s << " but Color Bitboard missing" << std::endl;
                 assert(false);
            }
        }
    }
}
#endif
