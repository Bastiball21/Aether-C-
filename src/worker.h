#ifndef WORKER_H
#define WORKER_H

#include "position.h"
#include "search.h" // For SearchLimits, MAX_PLY
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

// Forward decl
class MovePicker;

// Per-thread search state
class SearchWorker {
public:
    SearchWorker(int id);
    ~SearchWorker();

    void start_search(const Position& root_pos, const SearchLimits& limits);
    void wait_for_completion(); // For master to wait for this worker
    void stop();

    // The main search loop for this worker
    void search_loop();

    // Getters
    long long get_nodes() const { return node_count; }
    int get_id() const { return thread_id; }

    // Public for MovePicker (or make MovePicker a friend)
    int History[2][6][64];
    int CaptureHistory[2][6][64][6];
    int16_t ContHistory[2][6][64][6][64];
    uint16_t CounterMove[2][4096];
    int KillerMoves[MAX_PLY][2];

    // Helpers
    void update_history(int side, int pt, int to, int bonus);
    void update_capture_history(int side, int pt, int to, int captured_pt, int bonus);
    void update_continuation(int side, int prev_pt, int prev_to, int pt, int to, int bonus);
    void update_counter_move(int side, int prev_from, int prev_to, uint16_t move);

    // Exposed for MovePicker
    friend class MovePicker;
    friend class Search; // So Search::clear can call clear_history

    // Internal Search Functions made public for now (or friend MovePicker)
    int quiescence(Position& pos, int alpha, int beta, int ply);
    int negamax(Position& pos, int depth, int alpha, int beta, int ply, bool null_allowed, uint16_t prev_move, uint16_t excluded_move);
    void iter_deep();

    // Helper to clear history
    void clear_history();

private:
    int thread_id;
    long long node_count;

    // Thread management
    std::thread worker_thread;
    std::mutex mutex;
    std::condition_variable cv;
    bool exit_thread;
    bool searching;

    // Search Context
    Position root_pos;
    SearchLimits limits;

    // Time management helper (only for master, or shared check?)
    // Workers just check stop_flag.
    void check_limits();

    void decay_history();
};

// Thread Pool Manager
class ThreadPool {
public:
    void init(int thread_count);
    void start_search(const Position& pos, const SearchLimits& limits);
    void wait_for_completion();
    long long get_total_nodes() const;

    std::vector<SearchWorker*> workers; // Helpers (ID 1..N)
    SearchWorker* master; // Master (ID 0)

    ThreadPool() : master(nullptr) {}
    ~ThreadPool();
};

extern ThreadPool GlobalPool;

#endif // WORKER_H
