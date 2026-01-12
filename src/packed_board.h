#ifndef PACKED_BOARD_H
#define PACKED_BOARD_H

#include "position.h"
#include "types.h"
#include <cstdint>

#pragma pack(push, 1)

struct PackedBoard {
    uint64_t occupancy;
    uint8_t pieces[16];
    uint8_t stm_ep;
    uint8_t halfmove;
    uint16_t fullmove;
    int16_t score;
    uint8_t result;
    uint8_t pad;
};

#pragma pack(pop)

static_assert(sizeof(PackedBoard) == 32, "PackedBoard must be exactly 32 bytes");

void pack_position(const Position& pos, int16_t score_stm, float game_result, PackedBoard& dest);
void set_packed_result(PackedBoard& dest, float game_result);

#endif // PACKED_BOARD_H
