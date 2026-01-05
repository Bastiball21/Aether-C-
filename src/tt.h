#ifndef TT_H
#define TT_H

#include "types.h"
#include <vector>
#include <cstdint>

struct TTEntry {
    Key key;          // 8 bytes
    int16_t score;    // 2 bytes
    int16_t eval;     // 2 bytes
    uint16_t move;    // 2 bytes
    uint8_t depth;    // 1 byte
    uint8_t bound;    // 1 byte (EXACT=1, UPPER=2, LOWER=3)
    uint8_t gen;      // 1 byte
    uint8_t pad;      // 1 byte (for alignment)
    // Total: 18 bytes? No.
    // Key (8) + score (2) + eval (2) + move (2) + depth (1) + bound (1) + gen (1) + pad (? needed for 16?)
    // 8 + 2 + 2 + 2 + 1 + 1 + 1 = 17 bytes.
    // Wait, structure padding will make it 24 probably if not careful.
    // But we are in a bucket of 2.
    // Let's try to fit in 16 bytes if possible.
    // Key (8).
    // Move (2).
    // Score (2).
    // Eval (2).
    // Depth (1).
    // Bound/Gen (1).
    // 8 + 2 + 2 + 2 + 1 + 1 = 16 bytes.
    // We can pack Bound and Gen into one byte.
    // Bound: 2 bits (0-3). Gen: 6 bits (0-63).
};

// Actually, let's just make it a struct and check size.
// If it is 24 bytes, a bucket of 2 is 48 bytes. That's fine for now, user said "Change TT layout".
// The user spec said:
// uint64_t key
// int16_t score
// int16_t eval
// uint8_t depth
// uint8_t bound
// uint16_t move
// uint8_t gen
// uint8_t pad
// This sums to 8+2+2+1+1+2+1+1 = 18 bytes.
// With padding it will align to 8 bytes -> 24 bytes.
// Bucket of 2 entries -> 48 bytes.
// Or we can pack it.
// Let's stick to the list provided by user, but maybe reorder to minimize padding.
// Key (8)
// Score (2)
// Eval (2)
// Move (2)
// Depth (1)
// Bound (1)
// Gen (1)
// Pad (1) -> Total 18 bytes? No, 8+2+2+2+1+1+1+1 = 18.
// Still 24 bytes with alignment.
// Whatever, correctness first.

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
    uint8_t current_gen;
};

extern TranspositionTable TTable;

#endif // TT_H
