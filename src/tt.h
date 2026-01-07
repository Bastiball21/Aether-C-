#ifndef TT_H
#define TT_H

#include "types.h"
#include <vector>
#include <cstdint>

struct TTEntry {
    Key key;            // 8 bytes
    uint16_t move;      // 2 bytes
    int16_t score;      // 2 bytes
    int16_t eval;       // 2 bytes
    uint8_t depth;      // 1 byte
    uint8_t gen_bound;  // 1 byte (6 bits gen, 2 bits bound)

    // Helpers
    // Gen is stored in the upper 6 bits? No, let's use the layout:
    // gen: 6 bits, bound: 2 bits.
    // Let's store gen in upper 6 bits and bound in lower 2 bits.
    // This allows bound to be retrieved with a simple mask.

    uint8_t relative_age(uint8_t current_gen) const {
        return (current_gen - gen()) & 0x3F;
    }

    uint8_t gen() const {
        return (gen_bound >> 2) & 0x3F;
    }

    uint8_t bound() const {
        return gen_bound & 0x3;
    }

    void update(Key k, uint16_t m, int s, int e, int d, int b, int g) {
        key = k;
        move = m;
        score = (int16_t)s;
        eval = (int16_t)e;
        depth = (uint8_t)d;
        // Pack gen (6 bits) and bound (2 bits)
        // gen << 2 | bound
        gen_bound = (uint8_t)(((g & 0x3F) << 2) | (b & 0x3));
    }

    void set_gen(int g) {
        // Keep bound, update gen
        gen_bound = (gen_bound & 0x3) | ((g & 0x3F) << 2);
    }
};

struct TTBucket {
    TTEntry entries[2];
};

class TranspositionTable {
public:
    TranspositionTable(size_t size_mb = 16);
    void resize(size_t size_mb);
    void clear();
    void new_search(); // Increment generation

    bool probe(Key key, TTEntry& entry); // Not const because we update gen/age
    void store(Key key, uint16_t move, int score, int eval, int depth, int bound);
    void prefetch(Key key) const;

    int hashfull() const;

private:
    std::vector<TTBucket> buckets;
    size_t num_buckets;
    uint8_t current_gen; // 0-255, but we only use 6 bits
};

extern TranspositionTable TTable;

#endif // TT_H
