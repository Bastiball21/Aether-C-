#ifndef SEE_H
#define SEE_H

#include "position.h"

// Static Exchange Evaluation
// Returns the approximate value gained by making the move.
// Positive values are good, negative are bad.
int see(const Position& pos, uint16_t move);

#endif // SEE_H
