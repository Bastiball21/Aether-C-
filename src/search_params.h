#ifndef SEARCH_PARAMS_H
#define SEARCH_PARAMS_H

namespace SearchParams {

    // Time Management
    constexpr int MOVE_OVERHEAD_DEFAULT = 10;

    // Pruning Margins
    constexpr int RFP_MARGIN = 75;
    constexpr int RAZORING_MARGIN = 300;
    constexpr int FUTILITY_MARGIN = 100;
    constexpr int DELTA_MARGIN = 1000;
    constexpr int PROBCUT_MARGIN = 200;
    constexpr int SINGULAR_MARGIN = 2; // Multiplier

    // NMP
    constexpr int NMP_DEPTH_LIMIT = 3;
    constexpr int NMP_BASE_REDUCTION = 3;
    constexpr int NMP_DIVISOR = 4;

    // LMR
    constexpr double LMR_BASE = 0.75;
    constexpr double LMR_DIVISOR = 2.25;

    // History
    constexpr int HISTORY_MAX = 16384;
    constexpr int HISTORY_DECAY = 16; // 1/16
    constexpr int HISTORY_PRUNE_DEPTH = 3;
    constexpr int HISTORY_PRUNE_THRESHOLD = -800;

    // See
    constexpr int SEE_GOOD_CAPTURE = 0;

}

#endif // SEARCH_PARAMS_H
