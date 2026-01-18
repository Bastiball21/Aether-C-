#ifndef SEARCH_H
#define SEARCH_H

#include "position.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

// Forward declarations
struct Move;
namespace TT { class TranspositionTable; }

extern int OptThreads; // Global thread count option

struct SearchLimits {
    int depth = 0;
    int64_t nodes = 0;
    int move_time = 0;
    bool infinite = false;
    bool silent = false;
    uint64_t seed = 0;

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
    bool use_tt_new_search = true;
    bool use_global_context = true;
};

struct SearchResult {
    uint16_t best_move = 0;
    int best_score_cp = 0;
    int depth_reached = 0;
    int pv_length = 0;
    struct RootScore {
        uint16_t move = 0;
        int score = 0;
    };
    std::vector<RootScore> root_scores;
};

class ThreadPool;

struct SearchContext {
    SearchLimits options;
    std::atomic<bool> stop_flag{false};
    int64_t soft_time_limit = 0;
    int64_t hard_time_limit = 0;
    int64_t nodes_limit_count = 0;
    std::atomic<bool> unstable_iteration{false};
    std::chrono::steady_clock::time_point start_time;
    int lmr_table[64][64]{};
    std::once_flag lmr_once;
    std::unique_ptr<ThreadPool> pool;

    SearchContext();
    ~SearchContext();

    long long get_node_count() const;
    void init_lmr();

    static std::atomic<SearchContext*> active_context;
    static SearchContext default_context;
};

// History Tables Size
const int MAX_PLY = 256;

class Search {
public:
    static void start(Position& pos, const SearchLimits& limits);
    static SearchResult search(Position& pos, const SearchLimits& limits);
    static SearchResult search(Position& pos, const SearchLimits& limits, SearchContext& context);
    static void stop();
    static void clear();

    static long long get_node_count();
};

#endif // SEARCH_H
