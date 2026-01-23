#ifndef NETWORK_H
#define NETWORK_H

#include "feature_transformer.h"
#include <string>

namespace NNUE {

    class Network : public FeatureTransformer {
    public:
        // Heads Weights
        struct alignas(64) HeadWeights {
            // Layout: [Output][Input] for better SIMD (if implemented later)
            // Head A: 256 -> 32
            int8_t head_a_weights[NUM_BUCKETS][HEAD_HIDDEN_SIZE][HIDDEN_SIZE];
            int32_t head_a_biases[NUM_BUCKETS][HEAD_HIDDEN_SIZE];

            // 32 -> 1
            int8_t head_a_out_weights[NUM_BUCKETS][1][HEAD_HIDDEN_SIZE];
            int32_t head_a_out_bias[NUM_BUCKETS][1];

            // Head B: 256 -> 32
            int8_t head_b_weights[NUM_BUCKETS][HEAD_HIDDEN_SIZE][HIDDEN_SIZE];
            int32_t head_b_biases[NUM_BUCKETS][HEAD_HIDDEN_SIZE];

            // 32 -> 1
            int8_t head_b_out_weights[NUM_BUCKETS][1][HEAD_HIDDEN_SIZE];
            int32_t head_b_out_bias[NUM_BUCKETS][1];

            // Gate: 256 -> 8
            int8_t gate_weights[NUM_BUCKETS][GATE_HIDDEN_SIZE][HIDDEN_SIZE];
            int32_t gate_biases[NUM_BUCKETS][GATE_HIDDEN_SIZE];

            // 8 -> 1
            int8_t gate_out_weights[NUM_BUCKETS][1][GATE_HIDDEN_SIZE];
            int32_t gate_out_bias[NUM_BUCKETS][1];
        };

        HeadWeights heads;

        bool load(const std::string& filename);

        // Evaluate position
        int evaluate(const Position& pos, const NNUEState& state);

        // Debug
        void debug(const Position& pos, const NNUEState& state);

        // Test
        static void test();

    private:
        int32_t linear(const int16_t* input, const int8_t* weights, int32_t bias, int input_size);
        int32_t linear_layer(const int16_t* input, const int8_t* weights, const int32_t* biases, int16_t* output, int input_size, int output_size);
    };

    extern Network* g_network;
}

#endif
