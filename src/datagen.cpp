#include "datagen.h"
#include "bitboard.h"
#include "eval/eval.h"
#include "movegen.h"
#include "position.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
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

constexpr int MERCY_CP = 1000;
constexpr int MERCY_PLIES = 8;
constexpr int WIN_CP = 700;
constexpr int WIN_STABLE_PLIES = 6;
constexpr int DRAW_CP = 50;
constexpr int DRAW_PLIES = 20;
constexpr int DRAW_START_PLY = 30;
constexpr int MAX_PLIES = 200;
constexpr int OPENING_SKIP_PLIES = 10;
constexpr int MATE_THRESHOLD = 20000;
constexpr int MATE_CAP = 3000;
constexpr int MATE_SCORE = 31000;

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
        uint64_t z = v + 0x9e3779b97f4a7c15ULL;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
};

struct DatagenRecord {
    std::string fen;
    int16_t score_white = 0;
    float result = 0.5f;
};

struct QueueItem {
    uint64_t rolling_hash = 0;
    std::vector<DatagenRecord> records;
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

void write_record(std::ofstream& out, const DatagenRecord& record) {
    uint16_t fen_len = static_cast<uint16_t>(record.fen.size());
    out.write(reinterpret_cast<const char*>(&record.result), sizeof(record.result));
    out.write(reinterpret_cast<const char*>(&record.score_white), sizeof(record.score_white));
    out.write(reinterpret_cast<const char*>(&fen_len), sizeof(fen_len));
    out.write(record.fen.data(), fen_len);
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

int negamax(Position& pos, int depth, int alpha, int beta, int64_t node_limit,
    int64_t& nodes, bool& stop) {
    if (stop) {
        return Eval::evaluate(pos);
    }
    if (node_limit > 0 && nodes >= node_limit) {
        stop = true;
        return Eval::evaluate(pos);
    }
    if (depth <= 0) {
        nodes += 1;
        return Eval::evaluate(pos);
    }

    MoveGen::MoveList list;
    MoveGen::generate_all(pos, list);

    int best = -MATE_SCORE;
    bool has_move = false;
    for (int i = 0; i < list.count; ++i) {
        uint16_t move = list.moves[i];
        if (!is_legal_move(pos, move)) {
            continue;
        }
        has_move = true;
        pos.make_move(move);
        int score = -negamax(pos, depth - 1, -beta, -alpha, node_limit, nodes, stop);
        pos.unmake_move(move);

        if (score > best) {
            best = score;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta || stop) {
            break;
        }
    }

    if (!has_move) {
        if (pos.in_check()) {
            return -MATE_SCORE;
        }
        return 0;
    }

    return best;
}

uint16_t search_best_move(Position& pos, Rng& rng, int depth, int64_t node_limit,
    int64_t& nodes_used, int& best_score_out) {
    MoveGen::MoveList list;
    MoveGen::generate_all(pos, list);

    std::vector<uint16_t> candidates;
    int best_score = -MATE_SCORE;
    int64_t local_nodes = 0;
    bool stop = false;

    for (int i = 0; i < list.count; ++i) {
        uint16_t move = list.moves[i];
        if (!is_legal_move(pos, move)) {
            continue;
        }
        pos.make_move(move);
        int score = -negamax(pos, depth - 1, -MATE_SCORE, MATE_SCORE, node_limit, local_nodes, stop);
        pos.unmake_move(move);

        if (score > best_score) {
            best_score = score;
            candidates.clear();
            candidates.push_back(move);
        } else if (score == best_score) {
            candidates.push_back(move);
        }

        if (stop) {
            break;
        }
    }

    nodes_used += local_nodes;
    if (candidates.empty()) {
        best_score_out = -MATE_SCORE;
        return 0;
    }

    size_t idx = rng.range(0, candidates.size());
    best_score_out = best_score;
    return candidates[idx];
}

void apply_random_opening(Position& pos, Rng& rng, int plies) {
    if (plies <= 0) {
        return;
    }

    std::unordered_set<Key> seen;
    seen.insert(pos.key());

    for (int ply = 0; ply < plies; ++ply) {
        MoveGen::MoveList list;
        MoveGen::generate_all(pos, list);
        if (list.count == 0) {
            return;
        }

        bool moved = false;
        int attempts = list.count;
        while (attempts-- > 0 && list.count > 0) {
            int idx = static_cast<int>(rng.range(0, static_cast<size_t>(list.count)));
            uint16_t move = list.moves[idx];
            list.moves[idx] = list.moves[list.count - 1];
            list.count -= 1;

            if (!is_legal_move(pos, move)) {
                continue;
            }

            pos.make_move(move);
            if (seen.insert(pos.key()).second) {
                moved = true;
                break;
            }
            pos.unmake_move(move);
        }

        if (!moved) {
            return;
        }
    }
}

void writer_thread(std::ofstream& out, std::mutex& out_mutex, std::condition_variable& cv,
    std::queue<QueueItem>& queue, std::atomic<bool>& done, int64_t total_games,
    std::atomic<long>& games_total, std::atomic<long>& positions_total,
    std::atomic<long>& duplicates_total) {
    std::unordered_set<uint64_t> seen;

    while (true) {
        std::unique_lock<std::mutex> lock(out_mutex);
        cv.wait(lock, [&] { return done.load() || !queue.empty(); });
        if (queue.empty() && done.load()) {
            break;
        }

        QueueItem item = std::move(queue.front());
        queue.pop();
        lock.unlock();

        if (seen.find(item.rolling_hash) != seen.end()) {
            duplicates_total.fetch_add(1);
            continue;
        }
        seen.insert(item.rolling_hash);

        for (const auto& record : item.records) {
            write_record(out, record);
            positions_total.fetch_add(1);
        }

        long games_written = games_total.fetch_add(1) + 1;
        if (games_written >= total_games) {
            done.store(true);
        }
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

int16_t clamp_score(int score) {
    int clamped = std::clamp(score, -32000, 32000);
    if (std::abs(clamped) >= MATE_THRESHOLD) {
        clamped = clamped > 0 ? MATE_CAP : -MATE_CAP;
    }
    return static_cast<int16_t>(clamped);
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

    std::mutex out_mutex;
    std::condition_variable cv;
    std::queue<QueueItem> queue;
    std::atomic<bool> done(false);
    std::atomic<long> games_total(0);
    std::atomic<long> nodes_total(0);
    std::atomic<long> positions_total(0);
    std::atomic<long> duplicates_total(0);

    std::thread writer([&] {
        writer_thread(out, out_mutex, cv, queue, done, config.num_games, games_total,
            positions_total, duplicates_total);
    });

    std::thread status([&] {
        auto start_time = std::chrono::steady_clock::now();
        while (!done.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            long games = games_total.load();
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

            std::cout << "[Datagen] Games: " << games
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
            Rng rng(config.seed + static_cast<uint64_t>(t) * 0xDEADBEEF);

            for (;;) {
                if (done.load() || stop_flag.load()) {
                    break;
                }

                Position pos;
                if (!book.fens.empty()) {
                    const size_t idx = rng.range(0, book.fens.size());
                    pos.set(book.fens[idx]);
                } else {
                    pos.set_startpos();
                }
                apply_random_opening(pos, rng, config.opening_random_plies);

                uint64_t rolling_hash = rng.splitmix(config.seed ^ pos.key());
                std::vector<DatagenRecord> records;
                records.reserve(256);

                int ply = 0;
                int mercy_counter = 0;
                int win_counter = 0;
                int draw_counter = 0;
                float result = 0.5f;
                bool finished = false;

                while (!finished && ply < MAX_PLIES) {
                    if (pos.rule50_count() >= 100 || pos.is_repetition()) {
                        result = 0.5f;
                        break;
                    }

                    if (is_trivial_endgame(pos)) {
                        result = 0.5f;
                        break;
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

                    int eval_stm = Eval::evaluate(pos);
                    int16_t clamped = clamp_score(eval_stm);

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

                    bool should_keep = false;
                    if (ply >= OPENING_SKIP_PLIES) {
                        int abs_score = std::abs(clamped);
                        if (abs_score <= 200) {
                            should_keep = true;
                        } else if (abs_score <= 600) {
                            should_keep = rng.range(0, 100) < 50;
                        } else {
                            should_keep = rng.range(0, 100) < 25;
                        }
                    }

                    if (should_keep) {
                        int16_t score_white =
                            (pos.side_to_move() == WHITE) ? clamped : static_cast<int16_t>(-clamped);
                        records.push_back({pos.fen(), score_white, 0.5f});
                    }

                    int best_score = 0;
                    int64_t nodes_used = 0;
                    uint16_t move = search_best_move(
                        pos,
                        rng,
                        std::max(1, config.search_depth),
                        config.search_nodes,
                        nodes_used,
                        best_score);
                    nodes_total.fetch_add(static_cast<long>(nodes_used));
                    if (move == 0) {
                        break;
                    }

                    rolling_hash = rng.splitmix(rolling_hash ^ pos.key() ^ move);
                    pos.make_move(move);

                    ply += 1;
                }

                if (!records.empty()) {
                    for (auto& record : records) {
                        record.result = result;
                    }

                    QueueItem item;
                    item.rolling_hash = rolling_hash;
                    item.records = std::move(records);

                    {
                        std::lock_guard<std::mutex> lock(out_mutex);
                        queue.push(std::move(item));
                    }
                    cv.notify_one();
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

void convert_pgn(const std::string& pgn_path, const std::string& output_path) {
    std::ifstream input(pgn_path);
    if (!input.is_open()) {
        return;
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output.is_open()) {
        return;
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

        for (const auto& tok : tokens) {
            if (tok == "." || tok.back() == '.' || tok == "1-0" || tok == "0-1"
                || tok == "1/2-1/2" || tok == "*") {
                continue;
            }

            int eval_stm = Eval::evaluate(pos);
            int16_t clamped = clamp_score(eval_stm);
            int16_t score_white =
                (pos.side_to_move() == WHITE) ? clamped : static_cast<int16_t>(-clamped);

            DatagenRecord record{pos.fen(), score_white, result};
            write_record(output, record);

            if (!apply_uci_move(pos, tok)) {
                break;
            }
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
