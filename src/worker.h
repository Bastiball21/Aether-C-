#ifndef WORKER_H
#define WORKER_H

#include "position.h"
#include "search.h"
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

class MovePicker;

// Per-thread search state
class SearchWorker {
public:
    SearchWorker(int id);
    ~SearchWorker();

    void start_search(const Position& root_pos, const SearchLimits& limits);
    void wait_for_completion();
    void stop();

    void search_loop();

    long long get_nodes() const { return node_count; }
    int get_id() const { return thread_id; }

    // History Tables
    int History[2][6][64];
    int CaptureHistory[2][6][64][6];
    int16_t ContHistory[2][6][64][6][64];
    uint16_t CounterMove[2][4096];
    int KillerMoves[MAX_PLY][2];

    void update_history(int side, int pt, int to, int bonus);
    void update_capture_history(int side, int pt, int to, int captured_pt, int bonus);
    void update_continuation(int side, int prev_pt, int prev_to, int pt, int to, int bonus);
    void update_counter_move(int side, int prev_from, int prev_to, uint16_t move);

    friend class MovePicker;
    friend class Search;

    // Search Functions
    int quiescence(Position& pos, int alpha, int beta, int ply);
    int negamax(Position& pos, int depth, int alpha, int beta, int ply, bool null_allowed, uint16_t prev_move = 0, uint16_t excluded_move = 0);

    // Root & Iterative Deepening
    void search_root(int depth, int alpha, int beta, std::vector<uint16_t>& root_moves, std::vector<int>& root_scores);
    void iter_deep();

    void clear_history();

private:
    int thread_id;
    long long node_count;

    std::thread worker_thread;
    std::mutex mutex;
    std::condition_variable cv;
    bool exit_thread;
    bool searching;

    Position root_pos;
    SearchLimits limits;

    void check_limits();
    void decay_history();
};

class ThreadPool {
public:
    void init(int thread_count);
    void start_search(const Position& pos, const SearchLimits& limits);
    void wait_for_completion();
    long long get_total_nodes() const;

    std::vector<SearchWorker*> workers;
    SearchWorker* master;

    ThreadPool() : master(nullptr) {}
    ~ThreadPool();
};

extern ThreadPool GlobalPool;

#endif // WORKER_H
