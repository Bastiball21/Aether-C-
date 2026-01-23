#ifndef FEATURE_TRANSFORMER_H
#define FEATURE_TRANSFORMER_H

#include "../position.h"
#include "nnue_common.h"
#include <vector>

namespace NNUE {

    struct FeatureUpdate {
        Piece piece;
        Square sq;
        bool add;
    };

    class FeatureTransformer {
    public:
        // Input Weights: 8 buckets
        struct alignas(64) Weights {
            int16_t weights[NUM_BUCKETS][FEATURE_SIZE][HIDDEN_SIZE];
            int16_t biases[NUM_BUCKETS][HIDDEN_SIZE];
        };

        Weights weights;

        void refresh_accumulators(NNUEState& state, const Position& pos);
        void update_accumulators(NNUEState& next, const NNUEState& prev, const Position& pos, const std::vector<FeatureUpdate>& updates);

        // Helper to determine bucket for a position
        int get_bucket(const Position& pos, Color c);
    };

    extern FeatureTransformer* g_feature_transformer;

}

#endif
