#ifndef SEARCH_PARAMS_H
#define SEARCH_PARAMS_H

namespace SearchParams {

    // Time Management
    constexpr int MOVE_OVERHEAD_DEFAULT = 10;

    // Pruning Margins
    constexpr int RFP_MARGIN = 64;
    constexpr int RAZORING_MARGIN = 339;
    constexpr int FUTILITY_MARGIN = 97;
    constexpr int DELTA_MARGIN = 345;
    constexpr int TP_MARGIN = 35;
    constexpr int RANGE_REDUCTION_MARGIN = 74;
    constexpr int LMR_CAPTURE_MARGIN = 84;

    constexpr int SINGULAR_MARGIN = 2; // Used as (3 * depth / 2) in logic, or we can adjust logic

    // NMP
    constexpr int NMP_DEPTH_LIMIT = 2; // Zahak: depthLeft >= 2
    constexpr int NMP_BASE_REDUCTION = 4; // Zahak: 4 + min(d/4, 3)
    constexpr int NMP_DIVISOR = 4;

    // History
    constexpr int HISTORY_MAX = 16384; // Zahak uses standard history? Code says 1024 weight * something. Aether uses 16384. Zahak: `AddHistory` not fully visible, but `QuietHistory` usage divides by 10649.
    // Zahak `search.go`: `e.searchHistory.QuietHistory(gpMove, currentMove, move) / 10649`
    // I will keep Aether's history params for now unless I see Zahak's implementation of History being radically different.
    constexpr int HISTORY_DECAY = 16;
    constexpr int HISTORY_PRUNE_DEPTH = 3;
    constexpr int HISTORY_PRUNE_THRESHOLD = -800; // Zahak: historyThreashold := int32(depthLeft) * -1024. This is dynamic!
    // I will handle dynamic threshold in search.cpp

    // See
    constexpr int SEE_GOOD_CAPTURE = 0;

}

#endif // SEARCH_PARAMS_H
