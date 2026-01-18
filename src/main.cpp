#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <optional>
#include "position.h"
#include "search.h"
#include "tt.h"
#include "movegen.h"
#include "perft.h"
#include "eval/eval.h"
#include "eval/eval_tune.h"
#include "datagen.h"
#include "packed_board_io.h"
#include "syzygy.h"

// Parse move string to uint16_t
uint16_t parse_move(const Position& pos, const std::string& str) {
    // Expect UCI move like "e2e4" or "e7e8q"
    if (str.length() < 4 || str.length() > 5) return 0;

    auto in_range = [](char c, char lo, char hi) { return c >= lo && c <= hi; };
    if (!in_range(str[0], 'a', 'h') || !in_range(str[2], 'a', 'h') ||
        !in_range(str[1], '1', '8') || !in_range(str[3], '1', '8')) {
        return 0;
    }

    Square from = (Square)((str[0] - 'a') + (str[1] - '1') * 8);
    Square to   = (Square)((str[2] - 'a') + (str[3] - '1') * 8);
    char promo  = (str.length() > 4) ? str[4] : ' ';

    // Validate promotion char if present
    if (str.length() > 4) {
        char p = (promo >= 'A' && promo <= 'Z') ? (promo - 'A' + 'a') : promo;
        if (p != 'n' && p != 'b' && p != 'r' && p != 'q') return 0;
    }

    MoveGen::MoveList list;
    MoveGen::generate_all(pos, list);

    for (int i = 0; i < list.count; i++) {
        uint16_t m = list.moves[i];
        Square f = (Square)((m >> 6) & 0x3F);
        Square t = (Square)(m & 0x3F);

        if (f == from && t == to) {
            int flag = (m >> 12);

            // Promotion handling
            if (flag & 8) {
                // promo char could be 'n','b','r','q' (UCI is lowercase), allow uppercase too
                char p = (promo >= 'A' && promo <= 'Z') ? (promo - 'A' + 'a') : promo;
                if (p == 'n' && ((flag & 3) == 0)) return m;
                if (p == 'b' && ((flag & 3) == 1)) return m;
                if (p == 'r' && ((flag & 3) == 2)) return m;
                if (p == 'q' && ((flag & 3) == 3)) return m;
                continue; // wrong promotion piece
            }

            // Non-promotion move: accept even if promo char is present
            return m;
        }
    }

    return 0;
}

// Global thread
std::thread search_thread;

// UCI Options
int OptHash = 64;
int OptThreads = 1;
int OptMoveOverhead = 10;
int OptContempt = 0;
std::string OptSyzygyPath = "";
bool OptChess960 = false;
bool OptNullMove = true;
bool OptProbCut = true;
bool OptSingularExt = true;
bool OptUseHistory = true;
bool OptLargePages = false;

void join_search() {
    Search::stop();
    if (search_thread.joinable()) {
        search_thread.join();
    }
}

std::optional<PackedFormat> parse_packed_format(const std::string& value) {
    if (value == "v1") {
        return PackedFormat::V1;
    }
    if (value == "v2") {
        return PackedFormat::V2;
    }
    return std::nullopt;
}

bool parse_bool_value(const std::string& value, bool& out) {
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        out = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        out = false;
        return true;
    }
    return false;
}

int main(int argc, char* argv[]) {
    // I/O Speedup
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    // Initialize Eval Params
    Eval::init_params();

    // CLI Mode Check
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "tuneepd" && i + 2 < argc) {
            Eval::tune_epd(argv[i+1], argv[i+2]);
            return 0;
        }
        if (arg == "pack-convert" && i + 2 < argc) {
            std::string input_path = argv[i + 1];
            std::string output_path = argv[i + 2];
            i += 2;
            std::optional<PackedFormat> forced_format;
            for (; i + 1 < argc; i++) {
                std::string opt = argv[i];
                if (opt == "--format" && i + 1 < argc) {
                    forced_format = parse_packed_format(argv[i + 1]);
                    if (!forced_format.has_value()) {
                        std::cerr << "invalid format (expected v1 or v2)\n";
                        return 1;
                    }
                    i += 1;
                } else {
                    break;
                }
            }

            if (forced_format.has_value() && forced_format.value() != PackedFormat::V1) {
                std::cerr << "pack-convert expects a v1 input format\n";
                return 1;
            }

            std::string error;
            if (!convert_packed_v1_to_v2(input_path, output_path, true, error)) {
                std::cerr << "conversion failed: " << error << "\n";
                return 1;
            }
            return 0;
        }
        if (arg == "pack-verify" && i + 1 < argc) {
            std::string input_path = argv[i + 1];
            i += 1;
            std::optional<PackedFormat> forced_format;
            for (; i + 1 < argc; i++) {
                std::string opt = argv[i];
                if (opt == "--format" && i + 1 < argc) {
                    forced_format = parse_packed_format(argv[i + 1]);
                    if (!forced_format.has_value()) {
                        std::cerr << "invalid format (expected v1 or v2)\n";
                        return 1;
                    }
                    i += 1;
                } else {
                    break;
                }
            }

            std::string error;
            if (!verify_packed_board_file(input_path, forced_format, error)) {
                std::cerr << "verify failed: " << error << "\n";
                return 1;
            }
            return 0;
        }
        if (arg == "datagen") {
            DatagenConfig cfg;
            bool has_games = false;
            bool has_threads = false;
            bool has_out = false;
            std::string syzygy_path;

            int j = i + 1;
            if (j + 2 < argc && argv[j][0] != '-' && argv[j + 1][0] != '-'
                && argv[j + 2][0] != '-') {
                cfg.num_games = std::stoll(argv[j]);
                cfg.num_threads = std::stoi(argv[j + 1]);
                cfg.output_path = argv[j + 2];
                has_games = true;
                has_threads = true;
                has_out = true;
                j += 3;
            }

            for (; j < argc; ++j) {
                std::string opt = argv[j];
                if (opt == "--format" && j + 1 < argc) {
                    auto parsed = parse_packed_format(argv[j + 1]);
                    if (!parsed.has_value()) {
                        std::cerr << "invalid format (expected v1 or v2)\n";
                        return 1;
                    }
                    cfg.output_format = parsed.value();
                    j += 1;
                } else if (opt == "--threads" && j + 1 < argc) {
                    cfg.num_threads = std::stoi(argv[j + 1]);
                    has_threads = true;
                    j += 1;
                } else if (opt == "--games" && j + 1 < argc) {
                    cfg.num_games = std::stoll(argv[j + 1]);
                    has_games = true;
                    j += 1;
                } else if (opt == "--out" && j + 1 < argc) {
                    cfg.output_path = argv[j + 1];
                    has_out = true;
                    j += 1;
                } else if (opt == "--seed" && j + 1 < argc) {
                    cfg.seed = std::stoull(argv[j + 1]);
                    j += 1;
                } else if (opt == "--book" && j + 1 < argc) {
                    cfg.opening_book_path = argv[j + 1];
                    j += 1;
                } else if (opt == "--random-plies" && j + 1 < argc) {
                    cfg.opening_random_plies = std::stoi(argv[j + 1]);
                    j += 1;
                } else if (opt == "--book-random-walk-pct" && j + 1 < argc) {
                    cfg.book_random_walk_pct = std::stoi(argv[j + 1]);
                    j += 1;
                } else if ((opt == "--nodes" || opt == "--nodes-per-move") && j + 1 < argc) {
                    cfg.search_nodes = std::stoll(argv[j + 1]);
                    j += 1;
                } else if (opt == "--nodes-jitter" && j + 1 < argc) {
                    cfg.search_nodes_jitter = std::stod(argv[j + 1]);
                    j += 1;
                } else if (opt == "--depth" && j + 1 < argc) {
                    cfg.search_depth = std::stoi(argv[j + 1]);
                    j += 1;
                } else if (opt == "--min-depth" && j + 1 < argc) {
                    cfg.min_depth = std::stoi(argv[j + 1]);
                    j += 1;
                } else if (opt == "--min-nodes" && j + 1 < argc) {
                    cfg.min_nodes = std::stoll(argv[j + 1]);
                    j += 1;
                } else if (opt == "--record-every" && j + 1 < argc) {
                    cfg.record_every = std::stoi(argv[j + 1]);
                    j += 1;
                } else if (opt == "--sample-top-n" && j + 1 < argc) {
                    cfg.sample_top_n = std::stoi(argv[j + 1]);
                    j += 1;
                } else if (opt == "--sample-top-k" && j + 1 < argc) {
                    cfg.sample_top_k = std::stoi(argv[j + 1]);
                    j += 1;
                } else if (opt == "--topk" && j + 1 < argc) {
                    int topk = std::stoi(argv[j + 1]);
                    cfg.sample_top_k = topk;
                    cfg.sample_top_n = topk;
                    j += 1;
                } else if (opt == "--temp-start" && j + 1 < argc) {
                    cfg.temp_start = std::stod(argv[j + 1]);
                    j += 1;
                } else if (opt == "--temp" && j + 1 < argc) {
                    cfg.temp_start = std::stod(argv[j + 1]);
                    j += 1;
                } else if (opt == "--temp-end" && j + 1 < argc) {
                    cfg.temp_end = std::stod(argv[j + 1]);
                    j += 1;
                } else if (opt == "--temp-plies" && j + 1 < argc) {
                    cfg.temp_schedule_plies = std::stoi(argv[j + 1]);
                    j += 1;
                } else if (opt == "--epsilon" && j + 1 < argc) {
                    cfg.epsilon = std::stod(argv[j + 1]);
                    j += 1;
                } else if (opt == "--epsilon-greedy") {
                    cfg.use_epsilon_greedy = true;
                } else if (opt == "--chess960") {
                    cfg.chess960 = true;
                } else if (opt == "--adjudicate") {
                    bool value = true;
                    if (j + 1 < argc && argv[j + 1][0] != '-') {
                        if (!parse_bool_value(argv[j + 1], value)) {
                            std::cerr << "invalid adjudicate value (expected true/false)\n";
                            return 1;
                        }
                        j += 1;
                    }
                    cfg.adjudicate = value;
                } else if (opt == "--syzygy" && j + 1 < argc) {
                    syzygy_path = argv[j + 1];
                    j += 1;
                } else if (opt == "--balance-equal-cp" && j + 1 < argc) {
                    cfg.balance_equal_cp = std::stoi(argv[j + 1]);
                    j += 1;
                } else if (opt == "--balance-moderate-cp" && j + 1 < argc) {
                    cfg.balance_moderate_cp = std::stoi(argv[j + 1]);
                    j += 1;
                } else if (opt == "--balance-equal-keep" && j + 1 < argc) {
                    cfg.balance_equal_keep = std::stoi(argv[j + 1]);
                    j += 1;
                } else if (opt == "--balance-moderate-keep" && j + 1 < argc) {
                    cfg.balance_moderate_keep = std::stoi(argv[j + 1]);
                    j += 1;
                } else if (opt == "--balance-extreme-keep" && j + 1 < argc) {
                    cfg.balance_extreme_keep = std::stoi(argv[j + 1]);
                    j += 1;
                } else if (opt == "--gap-skip-cp" && j + 1 < argc) {
                    cfg.gap_skip_cp = std::stoi(argv[j + 1]);
                    j += 1;
                } else if (opt == "--writer-lru-size" && j + 1 < argc) {
                    cfg.writer_lru_size = static_cast<size_t>(std::stoull(argv[j + 1]));
                    j += 1;
                } else {
                    std::cerr << "unknown datagen option: " << opt << "\n";
                    return 1;
                }
            }

            if (!has_games || !has_threads || !has_out) {
                std::cerr << "datagen requires --games, --threads, and --out\n";
                return 1;
            }
            if (cfg.num_games <= 0 || cfg.num_threads <= 0) {
                std::cerr << "games and threads must be positive\n";
                return 1;
            }
            if (cfg.output_path.empty()) {
                std::cerr << "output path is required\n";
                return 1;
            }
            if (cfg.search_depth < 1) {
                std::cerr << "depth must be at least 1\n";
                return 1;
            }
            if (cfg.min_depth < 0) {
                std::cerr << "min-depth must be >= 0\n";
                return 1;
            }
            if (cfg.record_every <= 0) {
                std::cerr << "record-every must be >= 1\n";
                return 1;
            }
            if (cfg.sample_top_n <= 0 || cfg.sample_top_k <= 0) {
                std::cerr << "topk must be >= 1\n";
                return 1;
            }
            if (cfg.temp_start <= 0.0 || cfg.temp_end <= 0.0) {
                std::cerr << "temperature values must be > 0\n";
                return 1;
            }
            if (cfg.temp_schedule_plies < 0) {
                std::cerr << "temp-plies must be >= 0\n";
                return 1;
            }
            if (cfg.search_nodes_jitter < 0.0) {
                std::cerr << "nodes-jitter must be >= 0\n";
                return 1;
            }
            if (cfg.gap_skip_cp < 0) {
                std::cerr << "gap-skip-cp must be >= 0\n";
                return 1;
            }
            if (cfg.book_random_walk_pct < 0 || cfg.book_random_walk_pct > 100) {
                std::cerr << "book-random-walk-pct must be between 0 and 100\n";
                return 1;
            }

            if (!syzygy_path.empty()) {
                Syzygy::set_path(syzygy_path);
            }

            run_datagen(cfg);
            return 0;
        }
        if ((arg == "--weights" || arg == "-w") && i + 1 < argc) {
            if (Eval::load_params(argv[i+1])) {
                std::cout << "Weights loaded from " << argv[i+1] << "\n";
            } else {
                std::cerr << "Failed to load weights from " << argv[i+1] << "\n";
            }
            i++;
        } else if (arg == "--largepages") {
            OptLargePages = true;
        }
    }

    Position pos;
    pos.set_startpos();

    // Initialize TT with default
    TTable.set_large_pages(OptLargePages);
    TTable.resize(OptHash);

    Eval::set_contempt(OptContempt);

    std::string line;
    std::string token;

    while (std::getline(std::cin, line)) {
        std::stringstream ss(line);
        if (!(ss >> token)) continue;

        if (token == "uci") {
            std::cout << "id name Aether-C Version 1.0.0\n";
            std::cout << "id author Basti Dangca\n";
            std::cout << "option name Hash type spin default 64 min 1 max 65536\n";
            std::cout << "option name Threads type spin default 1 min 1 max 64\n";
            std::cout << "option name MoveOverhead type spin default 10 min 0 max 5000\n";
            std::cout << "option name Contempt type spin default 0 min -200 max 200\n";
            std::cout << "option name SyzygyPath type string default <empty>\n";
            std::cout << "option name UCI_Chess960 type check default false\n";
            std::cout << "option name NullMove type check default true\n";
            std::cout << "option name ProbCut type check default true\n";
            std::cout << "option name SingularExt type check default true\n";
            std::cout << "option name UseHistory type check default true\n";
            std::cout << "option name LargePages type check default false\n";
            std::cout << "uciok\n" << std::flush;
        } else if (token == "isready") {
            std::cout << "readyok\n" << std::flush;
        } else if (token == "setoption") {
            std::string name, value;
            ss >> token; // name
            if (token == "name") {
                ss >> name;
                while (ss >> token && token != "value") name += " " + token; // Handle spaces in name if any
                if (token == "value") {
                    // Consume remaining line for value to support paths with spaces
                    std::string remainder;
                    std::getline(ss, remainder);
                    // Trim leading whitespace
                    size_t first = remainder.find_first_not_of(" \t");
                    if (first != std::string::npos) value = remainder.substr(first);
                    else value = "";
                    // Trim trailing whitespace (optional but good practice)
                    size_t last = value.find_last_not_of(" \t");
                    if (last != std::string::npos) value = value.substr(0, last + 1);

                    if (name == "Hash") {
                        OptHash = std::stoi(value);
                        join_search();
                        TTable.resize(OptHash);
                    } else if (name == "Threads") {
                        OptThreads = std::stoi(value);
                    } else if (name == "MoveOverhead") {
                        OptMoveOverhead = std::stoi(value);
                    } else if (name == "Contempt") {
                        OptContempt = std::stoi(value);
                        Eval::set_contempt(OptContempt);
                    } else if (name == "SyzygyPath") {
                        OptSyzygyPath = value;
                        join_search();
                        Syzygy::set_path(OptSyzygyPath);
                    } else if (name == "UCI_Chess960") {
                        OptChess960 = (value == "true");
                        pos.set_chess960(OptChess960);
                    } else if (name == "NullMove") {
                        OptNullMove = (value == "true");
                    } else if (name == "ProbCut") {
                        OptProbCut = (value == "true");
                    } else if (name == "SingularExt") {
                        OptSingularExt = (value == "true");
                    } else if (name == "UseHistory") {
                        OptUseHistory = (value == "true");
                    } else if (name == "LargePages") {
                        OptLargePages = (value == "true");
                        join_search();
                        TTable.set_large_pages(OptLargePages);
                        TTable.resize(OptHash);
                    }
                }
            }
        } else if (token == "ucinewgame") {
            join_search();
            Search::clear();
            TTable.clear();
        } else if (token == "position") {
            join_search(); // Safety
            ss >> token;
            if (token == "startpos") {
                pos.set_chess960(OptChess960);
                pos.set_startpos();
                ss >> token; // Check for 'moves'
            } else if (token == "fen") {
                std::string fen = "";
                while (ss >> token && token != "moves") {
                    fen += token + " ";
                }
                pos.set_chess960(OptChess960);
                pos.set(fen);
            }

            if (token == "moves") {
                while (ss >> token) {
                    uint16_t m = parse_move(pos, token);
                    if (m != 0) pos.make_move(m);
                }
            }
        } else if (token == "go") {
            join_search(); // Ensure prev search stopped
            SearchLimits limits;
            limits.move_overhead_ms = OptMoveOverhead;
            limits.use_nmp = OptNullMove;
            limits.use_probcut = OptProbCut;
            limits.use_singular = OptSingularExt;
            limits.use_history = OptUseHistory;

            while (ss >> token) {
                if (token == "wtime") ss >> limits.time[WHITE];
                else if (token == "btime") ss >> limits.time[BLACK];
                else if (token == "winc") ss >> limits.inc[WHITE];
                else if (token == "binc") ss >> limits.inc[BLACK];
                else if (token == "depth") ss >> limits.depth;
                else if (token == "nodes") ss >> limits.nodes;
                else if (token == "movetime") ss >> limits.move_time;
                else if (token == "movestogo") ss >> limits.movestogo;
                else if (token == "infinite") limits.infinite = true;
            }

            search_thread = std::thread(Search::start, std::ref(pos), limits);
        } else if (token == "stop") {
            Search::stop();
            // Thread will print bestmove and exit loop
        } else if (token == "quit") {
            join_search();
            break;
        } else if (token == "perft") {
             int depth;
             ss >> depth;
             Perft::go(pos, depth);
        } else if (token == "divide") {
             int depth;
             ss >> depth;
             Perft::divide(pos, depth);
        } else if (token == "bench") {
             join_search();
             // Simple bench
             // Startpos + 3 tactical positions
             std::vector<std::string> fens = {
                 "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", // Kiwipete
                 "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                 "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"
             };

             long long total_nodes = 0;
             auto bench_start = std::chrono::steady_clock::now();

             for (const auto& f : fens) {
                 pos.set(f);
                 SearchLimits limits;
                 limits.depth = 10;
                 // Use globals
                 limits.use_nmp = OptNullMove;
                 limits.use_probcut = OptProbCut;
                 limits.use_singular = OptSingularExt;
                 limits.use_history = OptUseHistory;

                 // Run in this thread
                 Search::start(pos, limits); // This loops over depths.
                 total_nodes += Search::get_node_count();
             }

             auto bench_end = std::chrono::steady_clock::now();
             long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(bench_end - bench_start).count();

             std::cout << "Bench: " << total_nodes << " nodes " << ms << " ms " << (ms > 0 ? total_nodes * 1000 / ms : 0) << " nps\n";
        } else if (token == "tune") {
             ss >> token; // "fen" or subcommand
             if (token == "fen") {
                 std::string fen = "";
                 while (ss >> token) fen += token + " ";
                 pos.set(fen);
                 Eval::trace_eval(pos);
             }
        } else if (token == "tuneepd") {
             // ./Aether-C tuneepd input.epd output.csv
             // This is usually a CLI arg, not UCI.
             // But user said: "./Aether-C tuneepd <input> <output>"
             // This implies checking argc/argv in main, NOT in UCI loop.
             // However, if we are in UCI loop, we can support it too.
             std::string infile, outfile;
             if (ss >> infile >> outfile) {
                 Eval::tune_epd(infile, outfile);
             } else {
                 std::cout << "Usage: tuneepd <input.epd> <output.csv>\n";
             }
        }
    }

    join_search();

    return 0;
}
