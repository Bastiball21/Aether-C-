#include "tt.h"
#include <cstring>

TranspositionTable TTable;

TranspositionTable::TranspositionTable(size_t size_mb) {
    resize(size_mb);
}

void TranspositionTable::resize(size_t size_mb) {
    size_t size_bytes = size_mb * 1024 * 1024;
    num_entries = size_bytes / sizeof(TTEntry);
    table.resize(num_entries);
    clear();
}

void TranspositionTable::clear() {
    std::memset(table.data(), 0, num_entries * sizeof(TTEntry));
}

bool TranspositionTable::probe(Key key, TTEntry& entry) const {
    size_t idx = key % num_entries;
    const TTEntry& e = table[idx];
    if (e.key == key) {
        entry = e;
        return true;
    }
    return false;
}

void TranspositionTable::prefetch(Key key) const {
    size_t idx = key % num_entries;
    __builtin_prefetch(&table[idx]);
}

void TranspositionTable::store(Key key, uint16_t move, int score, int eval, int depth, int type) {
    size_t idx = key % num_entries;
    TTEntry& e = table[idx];

    // Simple replacement scheme: replace if different key or depth is greater
    // Or if type is exact
    if (e.key != key || depth >= e.depth || type == 1) {
        e.key = key;
        e.move = move;
        e.score = (int16_t)score;
        e.eval = (int16_t)eval;
        e.depth = (uint8_t)depth;
        e.type = (uint8_t)type;
    }
}

int TranspositionTable::hashfull() const {
    int count = 0;
    for (int i=0; i<1000; i++) {
        if (table[i].key != 0) count++;
    }
    return count; // permill
}
