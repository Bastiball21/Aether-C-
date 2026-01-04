#ifndef TT_H
#define TT_H

#include "types.h"
#include <vector>

struct TTEntry {
    Key key;
    uint16_t move;
    int16_t score;
    int16_t eval;
    uint8_t depth;
    uint8_t type; // 1=Exact, 2=Alpha, 3=Beta
};

class TranspositionTable {
public:
    TranspositionTable(size_t size_mb = 16);
    void resize(size_t size_mb);
    void clear();

    bool probe(Key key, TTEntry& entry) const;
    void store(Key key, uint16_t move, int score, int eval, int depth, int type);

    int hashfull() const;

private:
    std::vector<TTEntry> table;
    size_t num_entries;
};

extern TranspositionTable TTable;

#endif // TT_H
