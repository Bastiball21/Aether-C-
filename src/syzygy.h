#ifndef SYZYGY_WRAPPER_H
#define SYZYGY_WRAPPER_H

#include "position.h"
#include <string>

namespace Syzygy {

    // Initialize TB with path(s)
    void set_path(const std::string& path);

    // Probe TB at root.
    // Returns true if a TB hit was found and a bestmove was determined.
    // 'score' will be populated with a mate score or 0 (draw).
    // 'best_move' will be populated.
    bool probe_root(const Position& pos, uint16_t& best_move, int& score);

    // Probe WDL (Win/Draw/Loss) during search.
    // Returns a value suitable for alpha/beta (e.g. -MATE to MATE range, or specific TB win scores).
    // Returns 'false' if no probe result (not in TB, or disabled).
    bool probe_wdl(const Position& pos, int& score, int ply);

    // Check if TB is initialized
    bool enabled();

}

#endif // SYZYGY_WRAPPER_H
