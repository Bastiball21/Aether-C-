#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "position.h"

namespace MoveGen {

    struct MoveList {
        uint16_t moves[256];
        int count = 0;

        void add(uint16_t m) {
#ifndef NDEBUG
            assert(count < 256);
#endif
            if (count >= 256) return;
            moves[count++] = m;
        }
    };

    void generate_all(const Position& pos, MoveList& list);
    void generate_captures(const Position& pos, MoveList& list);
    void generate_quiets(const Position& pos, MoveList& list);

    // Check if a move is pseudo-legal (valid piece, valid destination, ignoring pins/checks)
    bool is_pseudo_legal(const Position& pos, uint16_t move);

} // namespace MoveGen

#endif // MOVEGEN_H
