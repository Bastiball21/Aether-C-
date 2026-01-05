#ifndef PERFT_H
#define PERFT_H

#include "position.h"

namespace Perft {
    void go(Position& pos, int depth);
    void divide(Position& pos, int depth);
    uint64_t run(Position& pos, int depth);
}

#endif // PERFT_H
