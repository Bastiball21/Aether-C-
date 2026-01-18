#ifndef PACKED_BOARD_H
#define PACKED_BOARD_H

#include "position.h"
#include "types.h"
#include <cstdint>

#pragma pack(push, 1)

struct PackedBoardV1 {
    uint64_t occupancy;
    uint8_t pieces[16];
    uint8_t stm_ep;
    uint8_t halfmove;
    uint16_t fullmove;
    int16_t score_cp;
    uint8_t wdl;
    uint8_t result;
};

struct PackedBoardV2 {
    uint64_t occupancy;
    uint8_t pieces[16];
    uint8_t stm_ep;
    uint8_t halfmove;
    uint16_t fullmove;
    int16_t score_cp;
    uint8_t wdl;
    uint8_t result;
    uint8_t depth_reached;
    uint16_t bestmove;
    uint16_t ply;
};

struct PackedBoardV2NoPly {
    uint64_t occupancy;
    uint8_t pieces[16];
    uint8_t stm_ep;
    uint8_t halfmove;
    uint16_t fullmove;
    int16_t score_cp;
    uint8_t wdl;
    uint8_t result;
    uint8_t depth_reached;
    uint16_t bestmove;
};

#pragma pack(pop)

static_assert(sizeof(PackedBoardV1) == 32, "PackedBoardV1 must be exactly 32 bytes");
static_assert(sizeof(PackedBoardV2) == 37, "PackedBoardV2 must be exactly 37 bytes");
static_assert(sizeof(PackedBoardV2NoPly) == 35, "PackedBoardV2NoPly must be exactly 35 bytes");

void pack_position_v1(const Position& pos, int16_t score_stm, uint8_t wdl, float game_result,
    PackedBoardV1& dest);
void pack_position_v2(const Position& pos, int16_t score_stm, uint8_t wdl, float game_result,
    uint8_t depth_reached, uint16_t bestmove, uint16_t ply, PackedBoardV2& dest);
void set_packed_result(PackedBoardV1& dest, float game_result);
void set_packed_result(PackedBoardV2& dest, float game_result);

#endif // PACKED_BOARD_H
