#ifndef NNUE_H
#define NNUE_H

#include "nnue_structs.h"
#include "layers.h"
#include "../position.h"
#include <string>
#include <vector>

namespace NNUE {

    // Network Weights Structure
    struct Network {
        // Feature Transformer
        // 256 biases
        std::vector<int16_t> ft_biases;
        // 64 buckets * 768 features * 256 outputs
        // Layout: [KingBucket][FeatureIdx][OutputIdx]
        // This is large (~25MB).
        std::vector<int16_t> ft_weights;

        // Layer 1: 512 -> 32
        AffineLayer<512, 32> layer1;

        // Layer 2: 32 -> 32
        AffineLayer<32, 32> layer2;

        // Output: 32 -> 1
        OutputLayer<32> output;
    };

    // Global Network Instance
    extern Network GlobalNetwork;
    extern bool IsLoaded;

    // API
    bool load_network(const std::string& path);
    void init(); // fallback or empty init

    // Evaluation
    int evaluate(const Position& pos);

    // Incremental Updates
    // We need to access Position's accumulators.
    // Helper to refresh one side's accumulator from scratch.
    void refresh_accumulator(const Position& pos, Color c, Accumulator& acc);

    // Helper to update accumulators incrementally
    // Returns true if update was possible, false if full refresh needed (e.g. King moved)
    // Actually, logic is usually handled in Position::make_move.
    // This function calculates the diff.
    // However, given the Position structure, we will implement the logic inside Position methods
    // utilizing these helpers or directly.
    // For now, expose basic access.

} // namespace NNUE

#endif // NNUE_H
