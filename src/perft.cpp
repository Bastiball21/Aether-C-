#include "perft.h"
#include "movegen.h"
#include "bitboard.h"
#include <iostream>
#include <chrono>
#include <iomanip>

using namespace std::chrono;

namespace Perft {

uint64_t run(Position& pos, int depth) {
    if (depth == 0) return 1;

    MoveGen::MoveList list;
    MoveGen::generate_all(pos, list);

    uint64_t nodes = 0;
    for (int i = 0; i < list.count; i++) {
        uint16_t move = list.moves[i];
        pos.make_move(move);

        // Legal check
        // Check if king of side to move (which is now opponent) is attacked?
        // No, we just made a move for `pos.side_to_move() ^ 1`.
        // We need to check if the side that JUST moved left their king in check.
        // `pos.side_to_move()` is the next player.
        // So we check if `pos.side_to_move() ^ 1`'s king is attacked.
        Color us = (Color)(pos.side_to_move() ^ 1);
        if (pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, us)), (Color)(us ^ 1))) {
            pos.unmake_move(move);
            continue;
        }

        nodes += run(pos, depth - 1);
        pos.unmake_move(move);
    }
    return nodes;
}

void go(Position& pos, int depth) {
    auto start = steady_clock::now();
    uint64_t nodes = run(pos, depth);
    auto end = steady_clock::now();
    long long ms = duration_cast<milliseconds>(end - start).count();

    std::cout << "perft depth " << depth
              << " nodes " << nodes
              << " time " << ms
              << " nps " << (ms > 0 ? (nodes * 1000 / ms) : 0) << std::endl;
}

// Helper to print move in UCI
std::string move_to_uci_perft(uint16_t m) {
    if (m == 0) return "0000";
    Square f = (Square)((m >> 6) & 0x3F);
    Square t = (Square)(m & 0x3F);
    int flag = (m >> 12);

    std::string s = "";
    s += (char)('a' + file_of(f));
    s += (char)('1' + rank_of(f));
    s += (char)('a' + file_of(t));
    s += (char)('1' + rank_of(t));

    if (flag & 8) { // Promo
        int p = (flag & 3);
        char pchar = 'q';
        if (p == 0) pchar = 'n';
        if (p == 1) pchar = 'b';
        if (p == 2) pchar = 'r';
        s += pchar;
    }
    return s;
}

void divide(Position& pos, int depth) {
    auto start = steady_clock::now();

    MoveGen::MoveList list;
    MoveGen::generate_all(pos, list);

    uint64_t total_nodes = 0;

    for (int i = 0; i < list.count; i++) {
        uint16_t move = list.moves[i];
        pos.make_move(move);

        Color us = (Color)(pos.side_to_move() ^ 1);
        if (pos.is_attacked((Square)Bitboards::lsb(pos.pieces(KING, us)), (Color)(us ^ 1))) {
            pos.unmake_move(move);
            continue;
        }

        uint64_t n = run(pos, depth - 1);
        std::cout << move_to_uci_perft(move) << ": " << n << std::endl;
        total_nodes += n;

        pos.unmake_move(move);
    }

    auto end = steady_clock::now();
    long long ms = duration_cast<milliseconds>(end - start).count();

    std::cout << std::endl;
    std::cout << "Nodes: " << total_nodes << std::endl;
    std::cout << "Time: " << ms << " ms" << std::endl;
    std::cout << "NPS: " << (ms > 0 ? (total_nodes * 1000 / ms) : 0) << std::endl;
}

}
