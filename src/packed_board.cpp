#include "packed_board.h"
#include "bitboard.h"
#include <algorithm>
#include <cstring>

namespace {

constexpr uint8_t kSideToMoveBit = 0x80;
constexpr uint8_t kEpMask = 0x7F;

uint8_t encode_piece(Piece p) {
    int type = static_cast<int>(p) % 6;
    int color = static_cast<int>(p) / 6;
    return static_cast<uint8_t>((type + 1) | (color << 3));
}

uint8_t encode_result(float game_result, bool stm_is_black) {
    uint8_t wdl = 1;
    if (game_result > 0.9f) {
        wdl = stm_is_black ? 0 : 2;
    } else if (game_result < 0.1f) {
        wdl = stm_is_black ? 2 : 0;
    }
    return wdl;
}

} // namespace

void pack_position(const Position& pos, int16_t score_stm, float game_result, PackedBoard& dest) {
    Bitboard occ = pos.pieces(WHITE) | pos.pieces(BLACK);
    dest.occupancy = occ;

    std::memset(dest.pieces, 0, sizeof(dest.pieces));

    int piece_idx = 0;
    Bitboard temp_occ = occ;
    while (temp_occ) {
        Square sq = Bitboards::lsb(temp_occ);
        Piece p = pos.piece_on(sq);
        uint8_t encoded = encode_piece(p);
        if ((piece_idx & 1) == 0) {
            dest.pieces[piece_idx / 2] = encoded;
        } else {
            dest.pieces[piece_idx / 2] |= static_cast<uint8_t>(encoded << 4);
        }
        ++piece_idx;
        Bitboards::pop_lsb(temp_occ);
    }

    uint8_t stm_bit = (pos.side_to_move() == BLACK) ? kSideToMoveBit : 0;
    uint8_t ep_square = (pos.en_passant_square() == SQ_NONE)
        ? static_cast<uint8_t>(SQ_NONE)
        : static_cast<uint8_t>(pos.en_passant_square());
    dest.stm_ep = static_cast<uint8_t>(stm_bit | (ep_square & kEpMask));

    dest.halfmove = static_cast<uint8_t>(std::min(255, pos.rule50_count()));
    dest.fullmove = static_cast<uint16_t>(std::min(65535, pos.fullmove_number()));
    dest.score = score_stm;

    dest.result = encode_result(game_result, pos.side_to_move() == BLACK);
    dest.pad = 0;
}

void set_packed_result(PackedBoard& dest, float game_result) {
    bool stm_is_black = (dest.stm_ep & kSideToMoveBit) != 0;
    dest.result = encode_result(game_result, stm_is_black);
}
