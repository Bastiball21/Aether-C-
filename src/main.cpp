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

// Parse move string to uint16_t
uint16_t parse_move(const Position& pos, const std::string& str) {
    Square from = (Square)((str[0] - 'a') + (str[1] - '1') * 8);
    Square to = (Square)((str[2] - 'a') + (str[3] - '1') * 8);
    char promo = (str.length() > 4) ? str[4] : ' ';

    MoveGen::MoveList list;
    MoveGen::generate_all(pos, list);

    for (int i = 0; i < list.count; i++) {
        uint16_t m = list.moves[i];
        Square f = (Square)((m >> 6) & 0x3F);
        Square t = (Square)(m & 0x3F);

        if (f == from && t == to) {
            int flag = (m >> 12);
            bool is_promo = (flag & 8);
            if (is_promo) {
                int p = (flag & 3);
                if (promo == 'n' && p == 0) return m;
                if (promo == 'b' && p == 1) return m;
                if (promo == 'r' && p == 2) return m;
                if (promo == 'q' && p == 3) return m;
            } else {
                return m;
            }
        }
    }
    return 0;
}

// Global thread
std::thread search_thread;

void join_search() {
    Search::stop();
    if (search_thread.joinable()) {
        search_thread.join();
    }
}

int main() {
    Position pos;
    pos.set_startpos();

    std::string line;
    std::string token;

    std::cout << "id name Aether-C" << std::endl;
    std::cout << "id author Jules" << std::endl;
    std::cout << "uciok" << std::endl;

    while (std::getline(std::cin, line)) {
        std::stringstream ss(line);
        ss >> token;

        if (token == "uci") {
            std::cout << "id name Aether-C" << std::endl;
            std::cout << "id author Jules" << std::endl;
            std::cout << "uciok" << std::endl;
        } else if (token == "isready") {
            std::cout << "readyok" << std::endl;
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
            while (ss >> token) {
                if (token == "wtime") ss >> limits.time[WHITE];
                else if (token == "btime") ss >> limits.time[BLACK];
                else if (token == "winc") ss >> limits.inc[WHITE];
                else if (token == "binc") ss >> limits.inc[BLACK];
                else if (token == "depth") ss >> limits.depth;
                else if (token == "nodes") ss >> limits.nodes;
                else if (token == "movetime") ss >> limits.move_time;
                else if (token == "infinite") limits.infinite = true;
            }

            // Launch thread
            // IMPORTANT: `pos` is passed by ref. Since `main` loops, `pos` stays valid.
            // On exit, we join().
            search_thread = std::thread(Search::start, std::ref(pos), limits);
        } else if (token == "stop") {
            Search::stop();
            // Do NOT join here immediately usually in UCI, but since Search::stop sets flag,
            // the thread will exit soon. We can join on next command or quit.
            // But to be synchronous with "bestmove", we can wait?
            // UCI: "stop" -> engine stops -> engine sends "bestmove".
            // So we just signal stop. The thread will print bestmove and exit.
            // We join before starting new search.
        } else if (token == "quit") {
            join_search();
            break;
        } else if (token == "d") {
            // debug
        }
    }

    // Ensure joined
    join_search();

    return 0;
}
