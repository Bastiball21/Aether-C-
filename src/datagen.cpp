#include "datagen.h"
#include "bitboard.h"
#include "packed_board.h"
#include "eval/eval.h"
#include "movegen.h"
#include "eval/eval_util.h"
#include "position.h"
#include "search.h"
#include "syzygy.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <mutex>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

// Rust-aligned constants
constexpr int MERCY_CP = 1000;
constexpr int MERCY_PLIES = 8;
constexpr int WIN_CP = 700;
constexpr int WIN_STABLE_PLIES = 6;
constexpr int DRAW_CP = 50;
constexpr int DRAW_PLIES = 20;
constexpr int DRAW_START_PLY = 30;
constexpr int MIN_ADJUDICATE_DEPTH = 10;
constexpr int STABLE_SCORE_DELTA = 40;
constexpr int STABLE_SCORE_PLIES = 6;
constexpr int MAX_PLIES = 200;
constexpr int OPENING_SKIP_PLIES = 10;
constexpr int MATE_THRESHOLD = 20000;
std::atomic<bool> stop_flag(false);

std::string trim_copy(const std::string& input) {
    const auto start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = input.find_last_not_of(" \t\r\n");
    return input.substr(start, end - start + 1);
}

std::string format_count(double value) {
    const char* suffix = "";
    if (value >= 1e9) {
        value /= 1e9;
        suffix = "B";
    } else if (value >= 1e6) {
        value /= 1e6;
        suffix = "M";
    } else if (value >= 1e3) {
        value /= 1e3;
        suffix = "K";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value << suffix;
    return out.str();
}

uint64_t splitmix64(uint64_t v) {
    uint64_t z = v + 0x9e3779b97f4a7c15ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

uint64_t mix_seed(uint64_t base, uint64_t salt) {
    return splitmix64(base + 0x9e3779b97f4a7c15ULL * (salt + 1));
}

void handle_sigint(int) {
    stop_flag.store(true);
}

struct Rng {
    uint64_t state;

    explicit Rng(uint64_t seed) : state(seed) {}

    uint64_t next_u64() {
        state += 0x9e3779b97f4a7c15ULL;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    size_t range(size_t min, size_t max) {
        const uint64_t range_val = static_cast<uint64_t>(max - min);
        if (range_val == 0) {
            return min;
        }
        uint64_t x = next_u64();
        __uint128_t m = static_cast<__uint128_t>(x) * range_val;
        uint64_t l = static_cast<uint64_t>(m);
        if (l < range_val) {
            uint64_t t = (-range_val) % range_val;
            while (l < t) {
                x = next_u64();
                m = static_cast<__uint128_t>(x) * range_val;
                l = static_cast<uint64_t>(m);
            }
        }
        return static_cast<size_t>((m >> 64) + min);
    }

    uint64_t splitmix(uint64_t v) {
        return splitmix64(v);
    }

    double uniform_01() {
        constexpr double denom = 1.0 / static_cast<double>(UINT64_MAX);
        return static_cast<double>(next_u64()) * denom;
    }
};

int64_t jitter_search_nodes(const DatagenConfig& config, Rng& rng) {
    if (config.search_nodes <= 0) {
        return 0;
    }

    double jitter = std::max(0.0, config.search_nodes_jitter);
    if (jitter <= 0.0) {
        return std::max<int64_t>(1, config.search_nodes);
    }

    double offset = (rng.uniform_01() * 2.0 - 1.0) * jitter;
    double factor = std::max(0.0, 1.0 + offset);
    double adjusted = static_cast<double>(config.search_nodes) * factor;
    int64_t nodes = static_cast<int64_t>(std::llround(adjusted));
    return std::max<int64_t>(1, nodes);
}

class LruKeySet {
public:
    explicit LruKeySet(size_t capacity) : capacity_(capacity) {}

    bool contains(Key key) const {
        return lookup_.find(key) != lookup_.end();
    }

    void insert(Key key) {
        auto it = lookup_.find(key);
        if (it != lookup_.end()) {
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        order_.push_front(key);
        lookup_[key] = order_.begin();
        if (capacity_ == 0) {
            lookup_.erase(key);
            order_.clear();
            return;
        }
        if (lookup_.size() > capacity_) {
            auto last = order_.end();
            --last;
            lookup_.erase(*last);
            order_.pop_back();
        }
    }

private:
    size_t capacity_;
    std::list<Key> order_;
    std::unordered_map<Key, std::list<Key>::iterator> lookup_;
};

struct DatagenRecord {
    PackedBoardV1 board_v1{};
    PackedBoardV2 board_v2{};
};

struct QueueItem {
    uint64_t rolling_hash = 0;
    std::vector<DatagenRecord> records;
    PackedFormat format = PackedFormat::V1;
};

struct OpeningBook {
    std::vector<std::string> fens;
};

bool is_trivial_endgame(const Position& pos) {
    Bitboard pawns = pos.pieces(PAWN, WHITE) | pos.pieces(PAWN, BLACK);
    if (pawns == 0) {
        Bitboard white_major = pos.pieces(ROOK, WHITE) | pos.pieces(QUEEN, WHITE);
        Bitboard black_major = pos.pieces(ROOK, BLACK) | pos.pieces(QUEEN, BLACK);
        if (white_major == 0 && black_major == 0) {
            return true;
        }
    }

    Bitboard white_pieces = pos.pieces(WHITE) & ~pos.pieces(KING, WHITE);
    Bitboard black_pieces = pos.pieces(BLACK) & ~pos.pieces(KING, BLACK);

    if (white_pieces == 0 && black_pieces == 0) {
        return true;
    }

    int white_count = Bitboards::count(white_pieces);
    int black_count = Bitboards::count(black_pieces);

    auto is_minor = [&](Bitboard bb) {
        Bitboard minors = pos.pieces(KNIGHT, WHITE) | pos.pieces(BISHOP, WHITE)
            | pos.pieces(KNIGHT, BLACK) | pos.pieces(BISHOP, BLACK);
        return (bb & minors) != 0;
    };

    if ((white_count == 1 && black_count == 0) || (white_count == 0 && black_count == 1)) {
        if (white_count == 1 && is_minor(white_pieces)) {
            return true;
        }
        if (black_count == 1 && is_minor(black_pieces)) {
            return true;
        }
    }

    if (white_count == 1 && black_count == 1) {
        if (is_minor(white_pieces) && is_minor(black_pieces)) {
            return true;
        }
    }

    return false;
}

void write_record(std::ofstream& out, const DatagenRecord& record, PackedFormat format) {
    if (format == PackedFormat::V1) {
        out.write(reinterpret_cast<const char*>(&record.board_v1), sizeof(record.board_v1));
    } else {
        out.write(reinterpret_cast<const char*>(&record.board_v2), sizeof(record.board_v2));
    }
}

OpeningBook load_epd_book(const std::string& path) {
    OpeningBook book;
    if (path.empty()) {
        return book;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        return book;
    }

    std::string line;
    while (std::getline(input, line)) {
        std::string trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        std::stringstream ss(trimmed);
        std::string p1;
        std::string p2;
        std::string p3;
        std::string p4;
        if (!(ss >> p1 >> p2 >> p3 >> p4)) {
            continue;
        }
        std::string fen = p1 + " " + p2 + " " + p3 + " " + p4;
        book.fens.push_back(fen);
    }

    return book;
}

bool is_legal_move(Position& pos, uint16_t move) {
    Color mover = pos.side_to_move();
    pos.make_move(move);
    Bitboard king_bb = pos.pieces(KING, mover);
    bool legal = false;
    if (king_bb) {
        Square ksq = Bitboards::lsb(king_bb);
        legal = !pos.is_attacked(ksq, pos.side_to_move());
    }
    pos.unmake_move(move);
    return legal;
}

double temperature_for_ply(const DatagenConfig& config, int ply) {
    if (config.temp_schedule_plies <= 0) {
        return std::max(0.01, config.temp_start);
    }
    double t = static_cast<double>(std::min(ply, config.temp_schedule_plies));
    double span = static_cast<double>(config.temp_schedule_plies);
    double temp = config.temp_start + (config.temp_end - config.temp_start) * (t / span);
    return std::max(0.01, temp);
}

uint16_t pick_random_opening_move(Position& pos, const MoveGen::MoveList& list, Rng& rng,
    const std::unordered_set<Key>& seen_positions) {
    std::vector<uint16_t> legal_moves;
    std::vector<uint16_t> fresh_moves;
    legal_moves.reserve(list.count);
    fresh_moves.reserve(list.count);

    for (int i = 0; i < list.count; ++i) {
        uint16_t move = list.moves[i];
        if (!is_legal_move(pos, move)) {
            continue;
        }
        legal_moves.push_back(move);
        pos.make_move(move);
        if (seen_positions.find(pos.key()) == seen_positions.end()) {
            fresh_moves.push_back(move);
        }
        pos.unmake_move(move);
    }

    const auto& pool = !fresh_moves.empty() ? fresh_moves : legal_moves;
    if (pool.empty()) {
        return 0;
    }
    size_t idx = rng.range(0, pool.size());
    return pool[idx];
}

uint16_t pick_softmax_move(const std::vector<SearchResult::RootScore>& scores, Rng& rng,
    int ply, const DatagenConfig& config) {
    if (scores.empty()) {
        return 0;
    }

    size_t top_n = std::min(scores.size(), static_cast<size_t>(std::max(1, config.sample_top_n)));
    int max_score = scores[0].score;
    double temp = temperature_for_ply(config, ply);

    std::vector<double> weights;
    weights.reserve(top_n);
    double total = 0.0;
    for (size_t i = 0; i < top_n; ++i) {
        double w = std::exp((scores[i].score - max_score) / temp);
        weights.push_back(w);
        total += w;
    }
    if (total <= 0.0) {
        return scores[0].move;
    }

    double r = rng.uniform_01() * total;
    double acc = 0.0;
    for (size_t i = 0; i < top_n; ++i) {
        acc += weights[i];
        if (r <= acc) {
            return scores[i].move;
        }
    }
    return scores[0].move;
}

uint16_t pick_epsilon_greedy_move(const std::vector<SearchResult::RootScore>& scores, Rng& rng,
    const DatagenConfig& config) {
    if (scores.empty()) {
        return 0;
    }

    size_t top_k = std::min(scores.size(), static_cast<size_t>(std::max(1, config.sample_top_k)));
    if (top_k <= 1 || config.epsilon <= 0.0) {
        return scores[0].move;
    }

    if (rng.uniform_01() < config.epsilon) {
        size_t idx = rng.range(0, top_k);
        return scores[idx].move;
    }
    return scores[0].move;
}

uint16_t pick_policy_move(const SearchResult& result, Rng& rng, int ply,
    const DatagenConfig& config) {
    if (!result.root_scores.empty()) {
        if (config.use_epsilon_greedy) {
            return pick_epsilon_greedy_move(result.root_scores, rng, config);
        }
        return pick_softmax_move(result.root_scores, rng, ply, config);
    }
    return result.best_move;
}

void writer_thread(std::ofstream& out, std::mutex& out_mutex, std::condition_variable& cv,
    std::queue<QueueItem>& queue, std::atomic<bool>& done, std::atomic<long>& games_written,
    std::atomic<long>& positions_total, std::atomic<long>& duplicates_total,
    size_t writer_lru_size) {
    LruKeySet seen(writer_lru_size);

    while (true) {
        std::unique_lock<std::mutex> lock(out_mutex);
        cv.wait(lock, [&] { return done.load() || !queue.empty(); });
        if (queue.empty() && done.load()) {
            break;
        }

        QueueItem item = std::move(queue.front());
        queue.pop();
        lock.unlock();

        Key rolling_key = static_cast<Key>(item.rolling_hash);
        if (seen.contains(rolling_key)) {
            duplicates_total.fetch_add(1);
            continue;
        }
        seen.insert(rolling_key);

        for (const auto& record : item.records) {
            write_record(out, record, item.format);
            positions_total.fetch_add(1);
        }

        games_written.fetch_add(1);
    }
}

std::string move_to_uci(uint16_t move) {
    if (move == 0) {
        return "0000";
    }
    Square from = static_cast<Square>((move >> 6) & 0x3F);
    Square to = static_cast<Square>(move & 0x3F);
    int flag = move >> 12;

    std::string s;
    s += static_cast<char>('a' + file_of(from));
    s += static_cast<char>('1' + rank_of(from));
    s += static_cast<char>('a' + file_of(to));
    s += static_cast<char>('1' + rank_of(to));

    if (flag & 8) {
        int promo = flag & 3;
        char pchar = 'q';
        if (promo == 0) pchar = 'n';
        if (promo == 1) pchar = 'b';
        if (promo == 2) pchar = 'r';
        s += pchar;
    }

    return s;
}

bool apply_uci_move(Position& pos, const std::string& token) {
    if (token.size() < 4) {
        return false;
    }

    MoveGen::MoveList list;
    MoveGen::generate_all(pos, list);

    for (int i = 0; i < list.count; ++i) {
        uint16_t move = list.moves[i];
        if (!is_legal_move(pos, move)) {
            continue;
        }
        if (move_to_uci(move) == token) {
            pos.make_move(move);
            return true;
        }
    }

    return false;
}

} // namespace

void run_datagen(const DatagenConfig& config) {
    if (config.num_games <= 0 || config.num_threads <= 0) {
        return;
    }

    stop_flag.store(false);
    std::signal(SIGINT, handle_sigint);
    OpeningBook book = load_epd_book(config.opening_book_path);

    std::ofstream out(config.output_path, std::ios::binary);
    if (!out.is_open()) {
        return;
    }

    if (config.output_format == PackedFormat::V2) {
        PackedBoardFileHeader header = make_packed_board_header(kPackedBoardFlagHasPly);
        if (!write_packed_board_header(out, header)) {
            return;
        }
    }

    std::mutex out_mutex;
    std::condition_variable cv;
    std::queue<QueueItem> queue;
    std::atomic<bool> done(false);
    std::atomic<long> games_completed(0);
    std::atomic<long> games_written(0);
    std::atomic<long> nodes_total(0);
    std::atomic<long> positions_total(0);
    std::atomic<long> duplicates_total(0);
    size_t writer_lru_size = config.writer_lru_size;
    if (writer_lru_size == 0) {
        writer_lru_size = config.record_lru_size;
    }

    std::thread writer([&] {
        writer_thread(out, out_mutex, cv, queue, done, games_written, positions_total,
            duplicates_total, writer_lru_size);
    });

    std::thread status([&] {
        auto start_time = std::chrono::steady_clock::now();
        while (!done.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            long games = games_completed.load();
            long written = games_written.load();
            long nodes = nodes_total.load();
            long positions = positions_total.load();
            long dups = duplicates_total.load();

            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - start_time).count();
            double nps = elapsed > 0.0 ? nodes / elapsed : 0.0;
            double pps = elapsed > 0.0 ? positions / elapsed : 0.0;
            double gps = elapsed > 0.0 ? games / elapsed : 0.0;
            double eta_secs = gps > 0.0 ? (config.num_games - games) / gps : 0.0;

            std::ostringstream eta_stream;
            eta_stream << std::fixed << std::setprecision(1);
            if (eta_secs > 3600.0) {
                eta_stream << (eta_secs / 3600.0) << "h";
            } else if (eta_secs > 60.0) {
                eta_stream << (eta_secs / 60.0) << "m";
            } else {
                eta_stream << eta_secs << "s";
            }

            std::cout << "[Datagen] Games: " << games << "/" << config.num_games
                      << " | Written: " << written
                      << " | Nodes: " << format_count(nodes)
                      << " | NPS: " << format_count(nps)
                      << " | FPS: " << static_cast<long>(pps)
                      << " | ETA: " << eta_stream.str()
                      << " | Dups: " << dups
                      << "\n";
        }
    });

    std::vector<std::thread> workers;
    for (int t = 0; t < config.num_threads; ++t) {
        workers.emplace_back([&, t] {
            uint64_t thread_seed = mix_seed(config.seed, static_cast<uint64_t>(t));
            Rng rng(thread_seed);
            SearchContext search_context;

            for (;;) {
                if (done.load() || stop_flag.load()) {
                    break;
                }

                Position pos;
                pos.set_chess960(config.chess960);
                bool use_book = !book.fens.empty();
                bool use_random_walk = false;
                if (use_book && config.book_random_walk_pct > 0) {
                    use_random_walk = rng.range(0, 100) < config.book_random_walk_pct;
                }
                if (use_book && !use_random_walk) {
                    const size_t idx = rng.range(0, book.fens.size());
                    pos.set(book.fens[idx]);
                } else {
                    pos.set_startpos();
                }

                int64_t game_search_nodes = jitter_search_nodes(config, rng);
                uint64_t rolling_hash = mix_seed(thread_seed, pos.key());
                std::vector<DatagenRecord> records;
                records.reserve(256);
                std::unordered_map<Key, int> repetition_counts;
                repetition_counts[pos.key()] = 1;
                std::unordered_set<Key> seen_positions;
                seen_positions.insert(pos.key());
                LruKeySet recent_positions(config.record_lru_size);

                int ply = 0;
                int mercy_counter = 0;
                int win_counter = 0;
                int draw_counter = 0;
                int stable_score_counter = 0;
                int last_eval = 0;
                bool has_last_eval = false;
                float result = 0.5f;
                bool finished = false;
                bool last_move_interesting = false;

                // Random Walk (8-9 plies if strict_rust_mode, otherwise config)
                int opening_plies = config.opening_random_plies;
                if (config.strict_rust_mode) {
                    opening_plies = 8 + rng.range(0, 2);
                }

                if (use_random_walk && opening_plies > 0) {
                    for (int i = 0; i < opening_plies; ++i) {
                        MoveGen::MoveList list;
                        MoveGen::generate_all(pos, list);
                        if (list.count == 0) {
                            break;
                        }
                        uint16_t move = pick_random_opening_move(pos, list, rng, seen_positions);
                        if (move == 0) {
                            break;
                        }
                        rolling_hash = rng.splitmix(rolling_hash ^ pos.key() ^ move);
                        pos.make_move(move);
                        seen_positions.insert(pos.key());
                        repetition_counts[pos.key()] += 1;
                        ply += 1;
                    }
                }

                // Filtering thresholds (Override if strict mode)
                int balance_equal_cp = config.balance_equal_cp;
                int balance_moderate_cp = config.balance_moderate_cp;
                int balance_equal_keep = config.balance_equal_keep;
                int balance_moderate_keep = config.balance_moderate_keep;
                int balance_extreme_keep = config.balance_extreme_keep;

                if (config.strict_rust_mode) {
                    balance_equal_cp = 200;
                    balance_moderate_cp = 600;
                    balance_equal_keep = 100;
                    balance_moderate_keep = 50;
                    balance_extreme_keep = 25;
                }

                while (!finished && ply < MAX_PLIES) {
                    if (pos.rule50_count() >= 100 || repetition_counts[pos.key()] >= 3) {
                        result = 0.5f;
                        break;
                    }

                    if (is_trivial_endgame(pos)) {
                        result = 0.5f;
                        break;
                    }

                    if (Syzygy::enabled() && Bitboards::count(pos.pieces()) <= 7) {
                        int tb_score = 0;
                        if (Syzygy::probe_wdl(pos, tb_score, 0)) {
                            if (tb_score > 0) {
                                result = (pos.side_to_move() == WHITE) ? 1.0f : 0.0f;
                            } else if (tb_score < 0) {
                                result = (pos.side_to_move() == WHITE) ? 0.0f : 1.0f;
                            } else {
                                result = 0.5f;
                            }
                            break;
                        }
                    }

                    MoveGen::MoveList list;
                    MoveGen::generate_all(pos, list);
                    if (list.count == 0) {
                        if (pos.in_check()) {
                            result = (pos.side_to_move() == WHITE) ? 0.0f : 1.0f;
                        } else {
                            result = 0.5f;
                        }
                        break;
                    }

                    SearchLimits limits;
                    limits.silent = true;
                    limits.seed = rng.next_u64();
                    limits.use_tt_new_search = false;
                    limits.use_global_context = false;

                    // Rust: FixedNodes(50_000)
                    if (config.search_nodes > 0) {
                        limits.nodes = game_search_nodes;
                        limits.depth = 0; // Let nodes drive
                    } else {
                        limits.depth = std::max(1, config.search_depth);
                        limits.nodes = 0;
                    }

                    SearchResult search_result = Search::search(pos, limits, search_context);
                    nodes_total.fetch_add(static_cast<long>(search_context.get_node_count()));
                    int64_t search_nodes = search_context.get_node_count();

                    int eval_stm = search_result.best_score_cp;
                    int clamped_eval = EvalUtil::clamp_score_cp(
                        eval_stm, 2000, MATE_THRESHOLD, 2000);
                    int16_t clamped = static_cast<int16_t>(clamped_eval);
                    uint8_t wdl = EvalUtil::wdl_from_cp(clamped, EvalUtil::kDefaultWdlParams);
                    bool gap_skip = false;
                    if (config.gap_skip_cp > 0 && search_result.root_scores.size() >= 2) {
                        int gap_cp = std::abs(search_result.root_scores[0].score
                            - search_result.root_scores[1].score);
                        gap_skip = gap_cp > config.gap_skip_cp;
                    }

                    if (config.adjudicate) {
                        bool depth_ok = search_result.depth_reached >= MIN_ADJUDICATE_DEPTH;
                        bool stability_ok = false;

                        if (config.strict_rust_mode) {
                            // Rust: No stability check, just checks score thresholds
                            // We assume depth is sufficient due to FixedNodes(50k)
                            stability_ok = depth_ok;
                        } else {
                            // Legacy Stability Logic
                            if (depth_ok) {
                                if (has_last_eval && std::abs(eval_stm - last_eval) <= STABLE_SCORE_DELTA) {
                                    stable_score_counter += 1;
                                } else {
                                    stable_score_counter = 0;
                                }
                                last_eval = eval_stm;
                                has_last_eval = true;
                            } else {
                                stable_score_counter = 0;
                                has_last_eval = false;
                            }
                            stability_ok = depth_ok && stable_score_counter >= STABLE_SCORE_PLIES;
                        }

                        if (stability_ok) {
                            if (std::abs(clamped) >= MERCY_CP) {
                                mercy_counter += 1;
                            } else {
                                mercy_counter = 0;
                            }
                            if (mercy_counter >= MERCY_PLIES) {
                                result = eval_stm > 0 ? (pos.side_to_move() == WHITE ? 1.0f : 0.0f)
                                                      : (pos.side_to_move() == WHITE ? 0.0f : 1.0f);
                                break;
                            }

                            if (std::abs(clamped) >= WIN_CP) {
                                win_counter += 1;
                            } else {
                                win_counter = 0;
                            }
                            if (win_counter >= WIN_STABLE_PLIES) {
                                result = eval_stm > 0 ? (pos.side_to_move() == WHITE ? 1.0f : 0.0f)
                                                      : (pos.side_to_move() == WHITE ? 0.0f : 1.0f);
                                break;
                            }

                            if (ply >= DRAW_START_PLY) {
                                if (std::abs(clamped) <= DRAW_CP) {
                                    draw_counter += 1;
                                } else {
                                    draw_counter = 0;
                                }
                                if (draw_counter >= DRAW_PLIES) {
                                    result = 0.5f;
                                    break;
                                }
                            }
                        } else {
                            if (!config.strict_rust_mode) {
                                mercy_counter = 0;
                                win_counter = 0;
                                draw_counter = 0;
                            } else {
                                // In strict mode, if depth_ok is false, we might want to reset?
                                // Rust doesn't check depth so it never resets due to depth.
                                // But if we miss MIN_ADJUDICATE_DEPTH (e.g. mate found at d1), we should probably not adjudicate based on score unless it's mate.
                                // Mate scores are handled separately below usually?
                                // Actually mate scores are clamped in `clamped_eval`.
                                // Let's stick to safe reset if not deep enough to avoid noise.
                                if (!depth_ok) {
                                    mercy_counter = 0;
                                    win_counter = 0;
                                    draw_counter = 0;
                                }
                            }
                        }
                    }

                    bool depth_or_nodes_ok = true;
                    if (config.min_depth > 0 || config.min_nodes > 0) {
                        depth_or_nodes_ok = (search_result.depth_reached >= config.min_depth)
                            || (search_nodes >= config.min_nodes);
                    }

                    bool pv_ok = search_result.pv_length > 0;
                    bool should_keep = false;
                    if (ply >= OPENING_SKIP_PLIES) {
                        bool record_due_to_ply = config.record_every <= 1
                            || (ply % config.record_every == 0);
                        if (record_due_to_ply || last_move_interesting) {
                            int abs_score = std::abs(clamped);
                            if (abs_score <= balance_equal_cp) {
                                should_keep = rng.range(0, 100) < balance_equal_keep;
                            } else if (abs_score <= balance_moderate_cp) {
                                should_keep = rng.range(0, 100) < balance_moderate_keep;
                            } else {
                                should_keep = rng.range(0, 100) < balance_extreme_keep;
                            }
                        }
                    }

                    if (should_keep && depth_or_nodes_ok && pv_ok && !gap_skip
                        && !recent_positions.contains(pos.key())) {
                        recent_positions.insert(pos.key());
                        DatagenRecord record{};

                        // Rust Compatibility: Write White-Relative Score if strict mode is on
                        int16_t score_to_write = clamped;
                        if (config.strict_rust_mode) {
                             score_to_write = (pos.side_to_move() == WHITE) ? clamped : -clamped;
                        }

                        if (config.output_format == PackedFormat::V1) {
                            pack_position_v1(pos, score_to_write, wdl, 0.5f, record.board_v1);
                        } else {
                            uint8_t depth = static_cast<uint8_t>(
                                std::min(255, search_result.depth_reached));
                            uint16_t ply_value = static_cast<uint16_t>(
                                std::min(65535, ply));
                            pack_position_v2(pos, score_to_write, wdl, 0.5f, depth,
                                search_result.best_move, ply_value, record.board_v2);
                        }
                        records.push_back(record);
                    }

                    uint16_t move = 0;
                    if (ply < opening_plies) {
                        move = pick_random_opening_move(pos, list, rng, seen_positions);
                    } else {
                        move = pick_policy_move(search_result, rng, ply, config);
                    }

                    if (move == 0) {
                        move = search_result.best_move;
                    }
                    if (move == 0) {
                        break;
                    }

                    Square move_from = static_cast<Square>((move >> 6) & 0x3F);
                    int move_flag = move >> 12;
                    bool is_capture = (move_flag & 4) || (move_flag == 5);
                    bool is_pawn_push = pos.piece_on(move_from) % 6 == PAWN;

                    rolling_hash = rng.splitmix(rolling_hash ^ pos.key() ^ move);
                    pos.make_move(move);
                    bool gives_check = pos.in_check();
                    last_move_interesting = is_capture || is_pawn_push || gives_check;
                    seen_positions.insert(pos.key());
                    repetition_counts[pos.key()] += 1;

                    ply += 1;
                }

                if (!records.empty()) {
                    for (auto& record : records) {
                        if (config.output_format == PackedFormat::V1) {
                            set_packed_result(record.board_v1, result);
                        } else {
                            set_packed_result(record.board_v2, result);
                        }
                    }

                    QueueItem item;
                    item.rolling_hash = rolling_hash;
                    item.records = std::move(records);
                    item.format = config.output_format;

                    {
                        std::lock_guard<std::mutex> lock(out_mutex);
                        queue.push(std::move(item));
                    }
                    cv.notify_one();
                }

                long completed = games_completed.fetch_add(1) + 1;
                if (completed >= config.num_games) {
                    done.store(true);
                    cv.notify_one();
                    break;
                }
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    done.store(true);
    cv.notify_one();
    writer.join();
    status.join();
    out.flush();
}

void convert_pgn(const std::string& pgn_path, const std::string& output_path,
    PackedFormat format) {
    std::ifstream input(pgn_path);
    if (!input.is_open()) {
        return;
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output.is_open()) {
        return;
    }

    if (format == PackedFormat::V2) {
        PackedBoardFileHeader header = make_packed_board_header(kPackedBoardFlagHasPly);
        if (!write_packed_board_header(output, header)) {
            return;
        }
    }

    Position pos;
    std::string line;
    std::string move_text;

    auto flush_game = [&]() {
        if (move_text.empty()) {
            return;
        }

        pos.set_startpos();
        std::vector<std::string> tokens;
        std::string token;
        std::string spaced;
        spaced.reserve(move_text.size() + 16);
        for (char c : move_text) {
            if (c == '.') {
                spaced.push_back(' ');
                spaced.push_back('.');
                spaced.push_back(' ');
            } else {
                spaced.push_back(c);
            }
        }

        std::stringstream ss(spaced);
        while (ss >> token) {
            tokens.push_back(token);
        }

        if (tokens.empty()) {
            move_text.clear();
            return;
        }

        float result = 0.5f;
        std::string last = tokens.back();
        if (last == "1-0") {
            result = 1.0f;
        } else if (last == "0-1") {
            result = 0.0f;
        } else if (last == "1/2-1/2") {
            result = 0.5f;
        } else {
            move_text.clear();
            return;
        }

        int ply_index = 0;
        for (const auto& tok : tokens) {
            if (tok == "." || tok.back() == '.' || tok == "1-0" || tok == "0-1"
                || tok == "1/2-1/2" || tok == "*") {
                continue;
            }

            int eval_stm = Eval::evaluate(pos);
            int clamped_eval = EvalUtil::clamp_score_cp(
                eval_stm, 2000, MATE_THRESHOLD, 2000);
            int16_t clamped = static_cast<int16_t>(clamped_eval);
            uint8_t wdl = EvalUtil::wdl_from_cp(clamped, EvalUtil::kDefaultWdlParams);
            DatagenRecord record{};
            if (format == PackedFormat::V1) {
                pack_position_v1(pos, clamped, wdl, result, record.board_v1);
            } else {
                uint16_t ply_value = static_cast<uint16_t>(std::min(65535, ply_index));
                pack_position_v2(pos, clamped, wdl, result, 0, 0, ply_value, record.board_v2);
            }
            write_record(output, record, format);

            if (!apply_uci_move(pos, tok)) {
                break;
            }
            ply_index += 1;
        }

        move_text.clear();
    };

    while (std::getline(input, line)) {
        std::string trimmed = trim_copy(line);

        if (trimmed.empty()) {
            continue;
        }

        if (!trimmed.empty() && trimmed[0] == '[') {
            flush_game();
            continue;
        }

        move_text.append(trimmed);
        move_text.push_back(' ');
    }

    flush_game();
}
