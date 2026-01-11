#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include "position.h"
#include "search.h"
#include "tt.h"
#include "movegen.h"
#include "perft.h"
#include "eval/eval.h"
#include "eval/eval_tune.h"
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

void join_search() {
    Search::stop();
    if (search_thread.joinable()) {
        search_thread.join();
    }
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
        if ((arg == "--weights" || arg == "-w") && i + 1 < argc) {
            if (Eval::load_params(argv[i+1])) {
                std::cout << "Weights loaded from " << argv[i+1] << "\n";
            } else {
                std::cerr << "Failed to load weights from " << argv[i+1] << "\n";
            }
            i++;
        }
    }

    Position pos;
    pos.set_startpos();

    // Initialize TT with default
    TTable.resize(OptHash);

    Eval::set_contempt(OptContempt);

    std::string line;
    std::string token;

    while (std::getline(std::cin, line)) {
        std::stringstream ss(line);
        if (!(ss >> token)) continue;

        if (token == "uci") {
            std::cout << "id name Aether-C\n";
            std::cout << "id author Bastiball21\n";
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
                    } else if (name == "NullMove") {
                        OptNullMove = (value == "true");
                    } else if (name == "ProbCut") {
                        OptProbCut = (value == "true");
                    } else if (name == "SingularExt") {
                        OptSingularExt = (value == "true");
                    } else if (name == "UseHistory") {
                        OptUseHistory = (value == "true");
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
                pos.set_startpos();
                ss >> token; // Check for 'moves'
            } else if (token == "fen") {
                std::string fen = "";
                while (ss >> token && token != "moves") {
                    fen += token + " ";
                }
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
