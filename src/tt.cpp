#include "tt.h"
#include <cstring>
#include <iostream>

TranspositionTable TTable;

// Static assert to ensure packing
static_assert(sizeof(TTEntry) == 16, "TTEntry must be 16 bytes");

TranspositionTable::TranspositionTable(size_t size_mb) {
    current_gen = 0;
    resize(size_mb);
}

void TranspositionTable::resize(size_t size_mb) {
    size_t size_bytes = size_mb * 1024 * 1024;
    size_t bucket_size = sizeof(TTBucket);
    size_t target_buckets = size_bytes / bucket_size;

    // Power of two
    num_buckets = 1;
    while (num_buckets * 2 <= target_buckets) num_buckets *= 2;

    buckets.resize(num_buckets);
    clear();
}

void TranspositionTable::clear() {
    std::memset(buckets.data(), 0, num_buckets * sizeof(TTBucket));
    current_gen = 0;
}

void TranspositionTable::new_search() {
    current_gen++; // wraps 0-255 naturally
}

bool TranspositionTable::probe(Key key, TTEntry& entry) {
    size_t idx = key & (num_buckets - 1); // Power of 2 mask
    TTBucket& bucket = buckets[idx];

    for (int i = 0; i < 2; i++) {
        if (bucket.entries[i].key == key) {
            entry = bucket.entries[i];
            // Refresh generation
            bucket.entries[i].set_gen(current_gen);
            return true;
        }
    }
    return false;
}

void TranspositionTable::prefetch(Key key) const {
    size_t idx = key & (num_buckets - 1);
    __builtin_prefetch(&buckets[idx]);
}

void TranspositionTable::store(Key key, uint16_t move, int score, int eval, int depth, int bound) {
    size_t idx = key & (num_buckets - 1);
    TTBucket& bucket = buckets[idx];

    int replace_idx = -1;

    // 1. Check if key is already in bucket
    for (int i = 0; i < 2; i++) {
        if (bucket.entries[i].key == key) {
            replace_idx = i;
            break;
        }
    }

    if (replace_idx != -1) {
        // Update existing
        TTEntry& e = bucket.entries[replace_idx];

        // Replace if deeper OR entry is from different generation
        // Note: old logic was e.gen != current_gen.
        // With modular aging, we might want "if older than current".
        // But for exact replacement, strict "not equal" is usually fine to refresh.
        // Or if we want to be strict about "better" entry:
        // if (depth >= e.depth || (e.gen() != (current_gen & 0x3F)))
        // Let's stick to the logic provided: replacement if deeper or older.

        bool replace = (depth >= e.depth || e.gen() != (current_gen & 0x3F));

        e.key = key; // Redundant but safe
        e.set_gen(current_gen);

        if (replace) {
             e.update(key, move, score, eval, depth, bound, current_gen);
        }
        return;
    }

    // 2. Not found, choose victim
    int best_score = -10000;

    for (int i = 0; i < 2; i++) {
        const TTEntry& e = bucket.entries[i];

        // Age calculation using modular arithmetic
        int age = e.relative_age(current_gen & 0x3F);

        // Scoring replacement suitability
        // Higher score = better candidate to replace (victim)
        int victim_score = age * 1000;
        victim_score -= e.depth; // Deeper = keep (lower victim score)

        // Penalty for replacing exact entry?
        // "never replace EXACT with non-EXACT if depths are similar"
        // EXACT is 1.
        if (e.bound() == 1) victim_score -= 5000;

        if (victim_score > best_score) {
            best_score = victim_score;
            replace_idx = i;
        }
    }

    // Replace at replace_idx
    TTEntry& e = bucket.entries[replace_idx];
    e.update(key, move, score, eval, depth, bound, current_gen);
}

int TranspositionTable::hashfull() const {
    int sample = (num_buckets < 1000) ? num_buckets : 1000;
    if (sample == 0) return 0;
    int count = 0;
    for (int i=0; i<sample; i++) {
        if (buckets[i].entries[0].key != 0) count++;
        if (buckets[i].entries[1].key != 0) count++;
    }
    return (count * 500) / sample;
}
