#ifndef SEARCH_H
#define SEARCH_H

#include "position.h"
#include <atomic>
#include <vector>

// Forward declarations
struct Move;
namespace TT { class TranspositionTable; }

struct SearchLimits {
    int depth = 0;
    int nodes = 0;
    int move_time = 0;
    bool infinite = false;
    // Time management fields
    int time[2] = {0, 0}; // wtime, btime
    int inc[2] = {0, 0};  // winc, binc
    int movestogo = 0;
};

class Search {
public:
    static void start(Position& pos, const SearchLimits& limits);
    static void stop();
    static void clear(); // Clear TT/Hist

private:
    static void iter_deep(Position& pos, const SearchLimits& limits);
};

#endif // SEARCH_H
