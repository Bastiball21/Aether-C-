#ifndef SEARCH_H
#define SEARCH_H

#include "position.h"
#include <atomic>
#include <vector>
#include <cstdint>

// Forward declarations
struct Move;
namespace TT { class TranspositionTable; }

struct SearchLimits {
    int depth = 0;
    int64_t nodes = 0;
    int move_time = 0;
    bool infinite = false;

    // Time management fields
    int time[2] = {0, 0}; // wtime, btime
    int inc[2] = {0, 0};  // winc, binc
    int movestogo = 0;

    // Internal time management
    int move_overhead_ms = 10;
    int64_t allocated_time_ms = 0;

    // Options
    bool use_nmp = true;
    bool use_probcut = true;
    bool use_singular = true;
    bool use_history = true;
};

// History Tables Size
const int MAX_PLY = 256;

class Search {
public:
    static void start(Position& pos, const SearchLimits& limits);
    static void stop();
    static void clear(); // Clear TT/Hist

    static long long get_node_count();

    // History Helpers
    static void update_history(int side, int pt, int to, int bonus);
    static void update_capture_history(int side, int att, int to, int victim, int bonus);
    static void update_continuation(int side, int prev_pt, int prev_to, int pt, int to, int bonus);
    static void update_counter_move(int side, int prev_from, int prev_to, uint16_t move);

    // Options
    static bool UseNMP;
    static bool UseProbCut;
    static bool UseSingular;
    static bool UseHistory;

private:
    static void iter_deep(Position& pos, const SearchLimits& limits);
};

#endif // SEARCH_H
