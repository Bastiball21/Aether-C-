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

        // HalfKP weights: 41024 * 256
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

    // Update accumulators for a single perspective
    void update_accumulator(const Position& pos, Color perspective, const Position::StateInfo* old_st, Position::StateInfo* new_st);

} // namespace NNUE

#endif // NNUE_H
