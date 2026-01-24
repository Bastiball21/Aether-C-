#include "position.h"
#include "nnue/feature_transformer.h"
#include "eval/eval_params.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cassert>

// Zobrist Keys (Placeholder)
// In a real engine, these should be initialized with random numbers.
namespace Zobrist {
    Key psq[12][64];
    Key side;
    Key castle[16];
    Key castle_rook[COLOR_NB][2][65];
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

        for (int c = 0; c < COLOR_NB; c++)
            for (int s = 0; s < 2; s++)
                for (int i = 0; i < 65; i++)
                    castle_rook[c][s][i] = rand64();

        for (int i = 0; i < 65; i++)
            enpassant[i] = rand64();
    }
}

namespace {
    int pst_value(PieceType pt, Square sq, Color side, bool is_mg) {
        int index = (side == WHITE) ? (sq ^ 56) : sq;
        switch (pt) {
            case PAWN: return is_mg ? Eval::Params.MG_PAWN_TABLE[index] : Eval::Params.EG_PAWN_TABLE[index];
            case KNIGHT: return is_mg ? Eval::Params.MG_KNIGHT_TABLE[index] : Eval::Params.EG_KNIGHT_TABLE[index];
            case BISHOP: return is_mg ? Eval::Params.MG_BISHOP_TABLE[index] : Eval::Params.EG_BISHOP_TABLE[index];
            case ROOK: return is_mg ? Eval::Params.MG_ROOK_TABLE[index] : Eval::Params.EG_ROOK_TABLE[index];
            case QUEEN: return is_mg ? Eval::Params.MG_QUEEN_TABLE[index] : Eval::Params.EG_QUEEN_TABLE[index];
            case KING: return is_mg ? Eval::Params.MG_KING_TABLE[index] : Eval::Params.EG_KING_TABLE[index];
            default: return 0;
        }
    }

    int piece_mg_value(Piece p, Square sq) {
        if (p == NO_PIECE) return 0;
        Color side = (Color)(p / 6);
        PieceType pt = (PieceType)(p % 6);
        int sign = (side == WHITE) ? 1 : -1;
        return sign * (Eval::Params.MG_VALS[pt] + pst_value(pt, sq, side, true));
    }

    int piece_eg_value(Piece p, Square sq) {
        if (p == NO_PIECE) return 0;
        Color side = (Color)(p / 6);
        PieceType pt = (PieceType)(p % 6);
        int sign = (side == WHITE) ? 1 : -1;
        return sign * (Eval::Params.EG_VALS[pt] + pst_value(pt, sq, side, false));
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
    if (s < 0 || s >= 64) {
        std::cerr << "CRITICAL: Invalid square " << s << " in put_piece" << std::endl;
        return;
    }
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
    if (s < 0 || s >= 64) {
        std::cerr << "CRITICAL: Invalid square " << s << " in remove_piece" << std::endl;
        return;
    }
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
    bool use_chess960 = chess960;
    // Clear
    std::memset(piece_bb, 0, sizeof(piece_bb));
    std::memset(color_bb, 0, sizeof(color_bb));
    for (int i=0; i<64; i++) board[i] = NO_PIECE;
    side = WHITE;
    ep_square = SQ_NONE;
    castling = 0;
    for (auto& row : castle_rook_from)
        for (auto& entry : row)
            entry = SQ_NONE;
    chess960 = use_chess960;
    rule50 = 0;
    halfmove_clock = 0;
    st_key = 0;
    p_key = 0;
    eval_mg_acc = 0;
    eval_eg_acc = 0;
    eval_phase_acc = 0;
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
    bool has_file_rights = false;
    if (token != "-") {
        for (char c : token) {
            if ((c >= 'A' && c <= 'H') || (c >= 'a' && c <= 'h')) {
                has_file_rights = true;
                break;
            }
        }
    }
    chess960 = chess960 || has_file_rights;

    auto king_square_for = [&](Color c) -> Square {
        Bitboard king_bb = pieces(KING, c);
        if (!king_bb) return SQ_NONE;
        return (Square)Bitboards::lsb(king_bb);
    };

    if (token != "-") {
        if (chess960) {
            auto find_rook_on_side = [&](Color color, int side_index) -> Square {
                Square king_sq = king_square_for(color);
                if (king_sq == SQ_NONE) return SQ_NONE;
                int king_file = file_of(king_sq);
                Rank king_rank = rank_of(king_sq);
                int step = (side_index == 0) ? 1 : -1;
                for (int f = king_file + step; f >= 0 && f < 8; f += step) {
                    Square sq = square_of((File)f, king_rank);
                    Piece expected_rook = (color == WHITE) ? W_ROOK : B_ROOK;
                    if (board[sq] == expected_rook) {
                        return sq;
                    }
                }
                return SQ_NONE;
            };

            auto add_castle_right = [&](Color color, int side_index, Square rook_sq) {
                if (rook_sq == SQ_NONE) return;
                castle_rook_from[color][side_index] = rook_sq;
                if (color == WHITE) castling |= (side_index == 0) ? 1 : 2;
                else castling |= (side_index == 0) ? 4 : 8;
            };

            for (char c : token) {
                if ((c >= 'A' && c <= 'H') || (c >= 'a' && c <= 'h')) {
                    Color color = (c >= 'A' && c <= 'H') ? WHITE : BLACK;
                    File rook_file = (File)((c >= 'A' && c <= 'H') ? (c - 'A') : (c - 'a'));
                    Square king_sq = king_square_for(color);
                    if (king_sq == SQ_NONE) continue;
                    int side_index = (rook_file > file_of(king_sq)) ? 0 : 1;
                    Square rook_sq = square_of(rook_file, rank_of(king_sq));
                    Piece expected_rook = (color == WHITE) ? W_ROOK : B_ROOK;
                    if (board[rook_sq] != expected_rook) continue;
                    add_castle_right(color, side_index, rook_sq);
                    continue;
                }

                if (c == 'K') add_castle_right(WHITE, 0, find_rook_on_side(WHITE, 0));
                else if (c == 'Q') add_castle_right(WHITE, 1, find_rook_on_side(WHITE, 1));
                else if (c == 'k') add_castle_right(BLACK, 0, find_rook_on_side(BLACK, 0));
                else if (c == 'q') add_castle_right(BLACK, 1, find_rook_on_side(BLACK, 1));
            }
        } else {
            for (char c : token) {
                if (c == 'K') castling |= 1;
                else if (c == 'Q') castling |= 2;
                else if (c == 'k') castling |= 4;
                else if (c == 'q') castling |= 8;
            }

            auto assign_standard_rook = [&](Color c, int side_index, int right_mask, File rook_file) {
                if (!(castling & right_mask)) {
                    castle_rook_from[c][side_index] = SQ_NONE;
                    return;
                }
                Square king_sq = king_square_for(c);
                Rank rook_rank = (king_sq == SQ_NONE) ? (c == WHITE ? RANK_1 : RANK_8) : rank_of(king_sq);
                castle_rook_from[c][side_index] = square_of(rook_file, rook_rank);
            };

            assign_standard_rook(WHITE, 0, 1, FILE_H);
            assign_standard_rook(WHITE, 1, 2, FILE_A);
            assign_standard_rook(BLACK, 0, 4, FILE_H);
            assign_standard_rook(BLACK, 1, 8, FILE_A);
        }
    }
    st_key ^= castling_key();

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

    // Initialize incremental eval accumulators
    for (int pt = 0; pt < 6; pt++) {
        for (Color c : {WHITE, BLACK}) {
            Bitboard bb = pieces((PieceType)pt, c);
            int count = Bitboards::count(bb);
            eval_phase_acc += count * Eval::Params.PHASE_WEIGHTS[pt];
            while (bb) {
                Square sq = (Square)Bitboards::pop_lsb(bb);
                Piece piece = (Piece)(pt + (c == WHITE ? 0 : 6));
                eval_mg_acc += piece_mg_value(piece, sq);
                eval_eg_acc += piece_eg_value(piece, sq);
            }
        }
    }

    // Initial StateInfo
    StateInfo si;
    si.key = st_key;
    si.pawn_key = p_key;
    si.castling = castling;
    std::memcpy(si.castle_rook_from, castle_rook_from, sizeof(castle_rook_from));
    si.ep_square = ep_square;
    si.rule50 = rule50;
    si.captured = NO_PIECE;
    si.eval_mg = eval_mg_acc;
    si.eval_eg = eval_eg_acc;
    si.eval_phase = eval_phase_acc;

    history.push_back(si);

    if (NNUE::g_feature_transformer) {
        NNUE::g_feature_transformer->refresh_accumulators(nnue_state, *this);
        // Copy to history so unmake restores it correctly (actually set pushes initial state)
        history.back().nnue = nnue_state;
    }
}

int Position::non_pawn_material(Color c) const {
    int mat = 0;
    mat += Bitboards::count(pieces(KNIGHT, c)) * 320;
    mat += Bitboards::count(pieces(BISHOP, c)) * 330;
    mat += Bitboards::count(pieces(ROOK, c)) * 500;
    mat += Bitboards::count(pieces(QUEEN, c)) * 900;
    return mat;
}

void Position::make_move(uint16_t move) {
#ifndef NDEBUG
    debug_validate();
#endif
    Square to = (Square)(move & 0x3F);
    Square from = (Square)((move >> 6) & 0x3F);
    int flag = (move >> 12);

    StateInfo si;
    si.key = st_key;
    si.pawn_key = p_key;
    si.castling = castling;
    std::memcpy(si.castle_rook_from, castle_rook_from, sizeof(castle_rook_from));
    si.ep_square = ep_square;
    si.rule50 = rule50;
    si.captured = NO_PIECE; // Will fill if capture
    si.eval_mg = eval_mg_acc;
    si.eval_eg = eval_eg_acc;
    si.eval_phase = eval_phase_acc;

    // Save current NNUE state to history (prev state)
    si.nnue = nnue_state;

    // Gather updates for NNUE
    std::vector<NNUE::FeatureUpdate> nnue_updates;
    if (NNUE::g_feature_transformer) {
        nnue_updates.reserve(4);
    }

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
        eval_mg_acc -= piece_mg_value(captured_piece, capture_sq);
        eval_eg_acc -= piece_eg_value(captured_piece, capture_sq);
        eval_phase_acc -= Eval::Params.PHASE_WEIGHTS[captured_piece % 6];

        if (NNUE::g_feature_transformer) {
            nnue_updates.push_back({captured_piece, capture_sq, false});
        }

        remove_piece(capture_sq);
    }

    Piece promo_piece = NO_PIECE;
    if (flag & 8) {
        PieceType promo_pt = (PieceType)((flag & 3) + 1); // N=1, B=2, R=3, Q=4
        promo_piece = (Piece)(promo_pt + (side == WHITE ? 0 : 6));
        eval_mg_acc -= piece_mg_value(p, from);
        eval_eg_acc -= piece_eg_value(p, from);
        eval_phase_acc -= Eval::Params.PHASE_WEIGHTS[pt];
        eval_mg_acc += piece_mg_value(promo_piece, to);
        eval_eg_acc += piece_eg_value(promo_piece, to);
        eval_phase_acc += Eval::Params.PHASE_WEIGHTS[promo_pt];

        if (NNUE::g_feature_transformer) {
            nnue_updates.push_back({p, from, false});
            nnue_updates.push_back({promo_piece, to, true});
        }
    } else {
        eval_mg_acc -= piece_mg_value(p, from);
        eval_eg_acc -= piece_eg_value(p, from);
        eval_mg_acc += piece_mg_value(p, to);
        eval_eg_acc += piece_eg_value(p, to);

        if (NNUE::g_feature_transformer) {
            nnue_updates.push_back({p, from, false});
            nnue_updates.push_back({p, to, true});
        }
    }

    // Move Piece
    move_piece(from, to);

    // Promotion
    if (flag & 8) {
        remove_piece(to);
        put_piece(promo_piece, to);
    }

    // Castling
    Square rook_from = SQ_NONE, rook_to = SQ_NONE;
    if (flag == 2) { // King Side
        rook_from = castle_rook_from[side][0];
        rook_to = (side == WHITE) ? SQ_F1 : SQ_F8;
    } else if (flag == 3) { // Queen Side
        rook_from = castle_rook_from[side][1];
        rook_to = (side == WHITE) ? SQ_D1 : SQ_D8;
    }
    if (rook_from != SQ_NONE) {
        Piece rook = (side == WHITE) ? W_ROOK : B_ROOK;
        eval_mg_acc -= piece_mg_value(rook, rook_from);
        eval_eg_acc -= piece_eg_value(rook, rook_from);
        eval_mg_acc += piece_mg_value(rook, rook_to);
        eval_eg_acc += piece_eg_value(rook, rook_to);
        move_piece(rook_from, rook_to);

        if (NNUE::g_feature_transformer) {
            nnue_updates.push_back({rook, rook_from, false});
            nnue_updates.push_back({rook, rook_to, true});
        }
    }

    // Update Castling Rights
    st_key ^= castling_key();
    if (pt == KING) {
        if (side == WHITE) castling &= ~3;
        else castling &= ~12;
    }
    // Rooks moved or captured
    auto check_rook = [&](Square sq) {
        if (sq == castle_rook_from[WHITE][0]) castling &= ~1;
        else if (sq == castle_rook_from[WHITE][1]) castling &= ~2;
        else if (sq == castle_rook_from[BLACK][0]) castling &= ~4;
        else if (sq == castle_rook_from[BLACK][1]) castling &= ~8;
    };
    check_rook(from);
    check_rook(to);
    st_key ^= castling_key();

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

    if (NNUE::g_feature_transformer) {
        // Update current NNUE state based on prev (si.nnue) and updates
        NNUE::g_feature_transformer->update_accumulators(nnue_state, si.nnue, *this, nnue_updates);
    }
}

void Position::make_null_move() {
    StateInfo si;
    si.key = st_key;
    si.pawn_key = p_key;
    si.castling = castling;
    std::memcpy(si.castle_rook_from, castle_rook_from, sizeof(castle_rook_from));
    si.ep_square = ep_square;
    si.rule50 = rule50;
    si.captured = NO_PIECE;
    si.eval_mg = eval_mg_acc;
    si.eval_eg = eval_eg_acc;
    si.eval_phase = eval_phase_acc;

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
    std::memcpy(castle_rook_from, si.castle_rook_from, sizeof(castle_rook_from));
    eval_mg_acc = si.eval_mg;
    eval_eg_acc = si.eval_eg;
    eval_phase_acc = si.eval_phase;
    nnue_state = si.nnue;
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
    if (flag == 2 || flag == 3) {
        Square r_from = (flag == 2) ? castle_rook_from[side][0] : castle_rook_from[side][1];
        Square r_to = (side == WHITE) ? ((flag == 2) ? SQ_F1 : SQ_D1) : ((flag == 2) ? SQ_F8 : SQ_D8);
        if (r_from != SQ_NONE) {
            move_piece(r_to, r_from);
        }
    }

    // Restore State
    castling = si.castling;
    std::memcpy(castle_rook_from, si.castle_rook_from, sizeof(castle_rook_from));
    ep_square = si.ep_square;
    rule50 = si.rule50;
    st_key = si.key;
    p_key = si.pawn_key;
    eval_mg_acc = si.eval_mg;
    eval_eg_acc = si.eval_eg;
    eval_phase_acc = si.eval_phase;
    nnue_state = si.nnue;
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
        if (chess960) {
            auto append_file = [&](Color c, int side_index, int mask, char fallback) {
                if (!(castling & mask)) return;
                Square rook_sq = castle_rook_from[c][side_index];
                int file = (rook_sq == SQ_NONE) ? (fallback - 'a') : file_of(rook_sq);
                char letter = char('a' + file);
                if (c == WHITE) letter = char(letter - 'a' + 'A');
                out += letter;
            };

            append_file(WHITE, 0, 1, 'h');
            append_file(WHITE, 1, 2, 'a');
            append_file(BLACK, 0, 4, 'h');
            append_file(BLACK, 1, 8, 'a');
        } else {
            if (castling & 1) out += 'K';
            if (castling & 2) out += 'Q';
            if (castling & 4) out += 'k';
            if (castling & 8) out += 'q';
        }
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

Key Position::castling_key() const {
    Key key = Zobrist::castle[castling];
    if (castling & 1) key ^= Zobrist::castle_rook[WHITE][0][castle_rook_from[WHITE][0]];
    if (castling & 2) key ^= Zobrist::castle_rook[WHITE][1][castle_rook_from[WHITE][1]];
    if (castling & 4) key ^= Zobrist::castle_rook[BLACK][0][castle_rook_from[BLACK][0]];
    if (castling & 8) key ^= Zobrist::castle_rook[BLACK][1][castle_rook_from[BLACK][1]];
    return key;
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
