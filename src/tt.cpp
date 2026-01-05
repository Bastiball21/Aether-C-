#include "tt.h"
#include <cstring>
#include <iostream>

TranspositionTable TTable;

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
    current_gen++;
}

bool TranspositionTable::probe(Key key, TTEntry& entry) {
    size_t idx = key & (num_buckets - 1); // Power of 2 mask
    TTBucket& bucket = buckets[idx];

    for (int i = 0; i < 2; i++) {
        if (bucket.entries[i].key == key) {
            entry = bucket.entries[i];
            // Refresh generation
            bucket.entries[i].gen = current_gen;
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
        // We always update if key matches?
        // Or only if depth is better?
        // Usually, we always update generation.
        // If depth is higher, update everything.
        // If depth is lower but it is exact, maybe?
        // Standard: replace if depth >= e.depth or e.gen != current_gen (old) ?
        // User said: "if any slot matches key -> replace/update it."
        e.key = key; // Redundant but safe
        e.gen = current_gen;
        if (depth >= e.depth || bound == 1) { // Prefer deeper or exact
             e.move = move;
             e.score = (int16_t)score;
             e.eval = (int16_t)eval;
             e.depth = (uint8_t)depth;
             e.bound = (uint8_t)bound;
        }
        return;
    }

    // 2. Not found, choose victim
    // User said:
    // - Prefer oldest generation (largest age = current_gen - entry.gen)
    // - tie-breaker: shallower depth
    // - but never replace EXACT with non-EXACT if depths are similar; keep EXACT preference.

    int best_score = -10000;

    for (int i = 0; i < 2; i++) {
        const TTEntry& e = bucket.entries[i];
        int age = (uint8_t)(current_gen - e.gen);

        // Scoring replacement suitability
        // Higher score = better candidate to replace (victim)
        int victim_score = age * 1000;
        victim_score -= e.depth; // Deeper = keep (lower victim score)

        // Penalty for replacing exact entry?
        // "never replace EXACT with non-EXACT if depths are similar"
        // Meaning if victim is EXACT, we should be reluctant to replace it.
        // If the new entry is EXACT, we can replace anything easier?
        // The rule "never replace EXACT with non-EXACT" usually applies when deciding to overwrite
        // based on the new entry's properties vs old.
        // But here we are just picking a slot.
        // Let's protect EXACT entries by lowering their victim score.
        if (e.bound == 1) victim_score -= 5000;

        if (victim_score > best_score) {
            best_score = victim_score;
            replace_idx = i;
        }
    }

    // Replace at replace_idx
    TTEntry& e = bucket.entries[replace_idx];
    e.key = key;
    e.move = move;
    e.score = (int16_t)score;
    e.eval = (int16_t)eval;
    e.depth = (uint8_t)depth;
    e.bound = (uint8_t)bound;
    e.gen = current_gen;
    e.pad = 0;
}

int TranspositionTable::hashfull() const {
    int sample = (num_buckets < 1000) ? num_buckets : 1000;
    int count = 0;
    for (int i=0; i<sample; i++) {
        if (buckets[i].entries[0].key != 0) count++;
        if (buckets[i].entries[1].key != 0) count++;
    }
    // count is number of entries (max sample * 2)
    // We want permill (0-1000)
    // filled_fraction = count / (sample * 2)
    // permill = filled_fraction * 1000
    // = count * 1000 / (sample * 2)
    // = count * 500 / sample
    return (count * 500) / sample;
}
