#ifndef TBCONFIG_H
#define TBCONFIG_H

#include "../bitboard.h"

// Override Fathom's popcount/lsb with Aether's
#define TB_CUSTOM_POP_COUNT(b) (Bitboards::count(b))
#define TB_CUSTOM_LSB(b) (Bitboards::lsb(b))

// Use atomic if available
#define TB_USE_ATOMIC

#endif
