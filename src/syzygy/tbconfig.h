#ifndef TBCONFIG_H
#define TBCONFIG_H

#ifdef __cplusplus
#include "../bitboard.h"
// Override Fathom's popcount/lsb with Aether's
#define TB_CUSTOM_POP_COUNT(b) (Bitboards::count(b))
#define TB_CUSTOM_LSB(b) (Bitboards::lsb(b))
// Use atomic if available
#define TB_USE_ATOMIC
#else
// C Mode (GCC builtins)
#define TB_CUSTOM_POP_COUNT(b) __builtin_popcountll(b)
#define TB_CUSTOM_LSB(b) __builtin_ctzll(b)
#endif

#endif
