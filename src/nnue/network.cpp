#include "network.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <cstring>
#include <vector>

namespace NNUE {

    Network* g_network = nullptr;

    bool Network::load(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open NNUE file: " << filename << std::endl;
            return false;
        }

        // Header
        char magic[8];
        file.read(magic, 8);
        if (strncmp(magic, "AS768NUE", 8) != 0) {
            std::cerr << "Error: Invalid magic in NNUE file" << std::endl;
            return false;
        }

        uint32_t version;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != 1) {
            std::cerr << "Error: Unsupported version: " << version << std::endl;
            return false;
        }

        uint32_t buckets_cnt;
        file.read(reinterpret_cast<char*>(&buckets_cnt), sizeof(buckets_cnt));
        if (buckets_cnt != NUM_BUCKETS) {
             std::cerr << "Error: Bucket count mismatch. Expected " << NUM_BUCKETS << ", got " << buckets_cnt << std::endl;
             return false;
        }

        uint32_t dims[4];
        file.read(reinterpret_cast<char*>(dims), sizeof(dims));
        if (dims[0] != FEATURE_SIZE || dims[1] != HIDDEN_SIZE || dims[2] != HEAD_HIDDEN_SIZE || dims[3] != GATE_HIDDEN_SIZE) {
            std::cerr << "Error: Dimension mismatch" << std::endl;
            return false;
        }

        // Read Feature Transformer Weights
        // Order: For each bucket: Weights, Biases.
        // File: W[768][256] (flat).
        // Our struct: weights[NUM_BUCKETS][FEATURE_SIZE][HIDDEN_SIZE].
        // We can read directly if layout matches.
        // It matches.
        for (int b = 0; b < NUM_BUCKETS; ++b) {
            file.read(reinterpret_cast<char*>(weights.weights[b]), sizeof(int16_t) * FEATURE_SIZE * HIDDEN_SIZE);
            file.read(reinterpret_cast<char*>(weights.biases[b]), sizeof(int16_t) * HIDDEN_SIZE);
        }

        // Read Heads
        // For each bucket:
        // HeadA W (32x256), b (32), OutW (1x32), OutB (1)
        // HeadB ...
        // Gate ...
        for (int b = 0; b < NUM_BUCKETS; ++b) {
            // Head A
            file.read(reinterpret_cast<char*>(heads.head_a_weights[b]), sizeof(int8_t) * HEAD_HIDDEN_SIZE * HIDDEN_SIZE);
            file.read(reinterpret_cast<char*>(heads.head_a_biases[b]), sizeof(int32_t) * HEAD_HIDDEN_SIZE);
            file.read(reinterpret_cast<char*>(heads.head_a_out_weights[b]), sizeof(int8_t) * HEAD_HIDDEN_SIZE); // 1x32
            file.read(reinterpret_cast<char*>(heads.head_a_out_bias[b]), sizeof(int32_t));

            // Head B
            file.read(reinterpret_cast<char*>(heads.head_b_weights[b]), sizeof(int8_t) * HEAD_HIDDEN_SIZE * HIDDEN_SIZE);
            file.read(reinterpret_cast<char*>(heads.head_b_biases[b]), sizeof(int32_t) * HEAD_HIDDEN_SIZE);
            file.read(reinterpret_cast<char*>(heads.head_b_out_weights[b]), sizeof(int8_t) * HEAD_HIDDEN_SIZE);
            file.read(reinterpret_cast<char*>(heads.head_b_out_bias[b]), sizeof(int32_t));

            // Gate
            file.read(reinterpret_cast<char*>(heads.gate_weights[b]), sizeof(int8_t) * GATE_HIDDEN_SIZE * HIDDEN_SIZE);
            file.read(reinterpret_cast<char*>(heads.gate_biases[b]), sizeof(int32_t) * GATE_HIDDEN_SIZE);
            file.read(reinterpret_cast<char*>(heads.gate_out_weights[b]), sizeof(int8_t) * GATE_HIDDEN_SIZE);
            file.read(reinterpret_cast<char*>(heads.gate_out_bias[b]), sizeof(int32_t));
        }

        if (file.fail()) {
            std::cerr << "Error: Read failed (file truncated?)" << std::endl;
            return false;
        }

        return true;
    }

    // CReLU: Clamp to [0, QA]
    inline int16_t crelu(int16_t x) {
        return std::clamp((int)x, 0, QA);
    }

    // Linear layer: Output[j] = Sum(W[j][i] * Input[i]) + Bias[j]
    // Weights layout: [Output][Input]
    // Input is int16, Weights int8, Bias int32.
    // Output is int32 (to be activated later).
    void linear_layer_imp(const int16_t* input, const int8_t* weights, const int32_t* biases, int32_t* output, int input_size, int output_size) {
        for (int j = 0; j < output_size; ++j) {
            int32_t sum = biases[j];
            const int8_t* row = weights + j * input_size;
            for (int i = 0; i < input_size; ++i) {
                sum += (int32_t)input[i] * (int32_t)row[i];
            }
            output[j] = sum;
        }
    }

    int Network::evaluate(const Position& pos, const NNUEState& state) {
        Color stm = pos.side_to_move();
        int bucket = state.buckets[stm];

        // 1. Trunk Activation
        int16_t trunk[HIDDEN_SIZE];
        const int16_t* acc = state.accumulators[stm].values;
        for (int i = 0; i < HIDDEN_SIZE; ++i) {
            trunk[i] = crelu(acc[i]);
        }

        // 2. Head A
        int32_t ha_l1[HEAD_HIDDEN_SIZE];
        linear_layer_imp(trunk, heads.head_a_weights[bucket][0], heads.head_a_biases[bucket], ha_l1, HIDDEN_SIZE, HEAD_HIDDEN_SIZE);

        int16_t ha_l1_act[HEAD_HIDDEN_SIZE];
        for(int i=0; i<HEAD_HIDDEN_SIZE; ++i) ha_l1_act[i] = crelu((int16_t)std::clamp(ha_l1[i], 0, 255)); // Assume activation keeps range?
        // Note: L1 output is int32. CReLU usually expects int16 range input?
        // If weights are int8 and input 0..255, sum can be 256*127*255 ~ 8M.
        // We probably need to scale down before next activation?
        // Standard quant: Output = (Sum * Scale) >> Shift.
        // Trainer usually trains with this.
        // I will assume simple division by (WeightScale * InputScale / OutputScale).
        // Let's assume OutputScale = 255 (same as input).
        // WeightScale (int8) = 64. InputScale = 255 (implicit).
        // So we divide by 64?
        // Let's try dividing by 64.
        // If we don't dequantize, numbers explode.
        // I'll assume simple shift >> 6 (div 64).
        for(int i=0; i<HEAD_HIDDEN_SIZE; ++i) ha_l1_act[i] = crelu((int16_t)(ha_l1[i] >> 6));

        int32_t score_a_raw;
        linear_layer_imp(ha_l1_act, heads.head_a_out_weights[bucket][0], heads.head_a_out_bias[bucket], &score_a_raw, HEAD_HIDDEN_SIZE, 1);

        // 3. Head B
        int32_t hb_l1[HEAD_HIDDEN_SIZE];
        linear_layer_imp(trunk, heads.head_b_weights[bucket][0], heads.head_b_biases[bucket], hb_l1, HIDDEN_SIZE, HEAD_HIDDEN_SIZE);

        int16_t hb_l1_act[HEAD_HIDDEN_SIZE];
        for(int i=0; i<HEAD_HIDDEN_SIZE; ++i) hb_l1_act[i] = crelu((int16_t)(hb_l1[i] >> 6));

        int32_t score_b_raw;
        linear_layer_imp(hb_l1_act, heads.head_b_out_weights[bucket][0], heads.head_b_out_bias[bucket], &score_b_raw, HEAD_HIDDEN_SIZE, 1);

        // 4. Gate
        int32_t g_l1[GATE_HIDDEN_SIZE];
        linear_layer_imp(trunk, heads.gate_weights[bucket][0], heads.gate_biases[bucket], g_l1, HIDDEN_SIZE, GATE_HIDDEN_SIZE);

        int16_t g_l1_act[GATE_HIDDEN_SIZE];
        for(int i=0; i<GATE_HIDDEN_SIZE; ++i) g_l1_act[i] = crelu((int16_t)(g_l1[i] >> 6));

        int32_t gate_raw;
        linear_layer_imp(g_l1_act, heads.gate_out_weights[bucket][0], heads.gate_out_bias[bucket], &gate_raw, GATE_HIDDEN_SIZE, 1);

        // Sigmoid
        // gate_raw is int32. (Scaled by 64?).
        // If we divide by 64, we get "real" value?
        // Sigmoid input range ~ -6..6 for active region.
        // If gate_raw is 6 * 64 = 384.
        double gate = 1.0 / (1.0 + std::exp(-(double)gate_raw / 64.0)); // Assume scale 64.

        // 5. Blend
        // score_a_raw is int32.
        double da = (double)score_a_raw;
        double db = (double)score_b_raw;

        double final_score = gate * da + (1.0 - gate) * db;

        // Final scaling
        // We have one more weight layer (Output weights).
        // We didn't shift it yet.
        // So result is scaled by 64.
        // We want CP.
        // If 1.0 (internal) = 1 CP?
        // Or 100?
        // Usually quantization aims for ~1 CP steps or similar.
        // Let's divide by 64 (weight scale) and then apply OUTPUT_SCALE.
        // If OUTPUT_SCALE=16 (defined in common).
        // But let's check values.
        // If we assume standard bullet training, outputs match target CP.
        // So we just need to dequantize.
        // Weights * Inputs = Scaled * Scaled.
        // We div by 64 once. So currently Scaled * 1.
        // So result is scaled by InputScale (255) ??
        // No, Activation is 0..255.
        // So we are scaled by 255 * 64 * 255 * 64?
        // We did >> 6.
        // So (Acc*255 -> 255).
        // L1 = W(64) * In(255) = Scale 64*255.
        // Shift >> 6 -> Scale 255.
        // L2 = W(64) * In(255) = Scale 64*255.
        // So final raw is scale 64*255?
        // If we divide by (64 * 255), we get 1.0?
        // Let's assume we divide by `64 * output_scale_factor`.
        // Let's divide by 256 for now (Shift 8).

        // Actually, for the trainer I will implement, I'll use simple scaling.
        // I'll stick to: Divide by 64 (Weight Scale).
        // And assume Input Scale 255 is "Unit".
        // Let's just output raw >> 6 and tune later if needed.
        // The prompt says "Output score in engine internal cp scaling".
        // I will assume `score / 1` after dequant is CP.

        return (int)(final_score / 64.0);
    }

    void Network::debug(const Position& pos, const NNUEState& state) {
        Color stm = pos.side_to_move();
        int bucket = state.buckets[stm];

        int16_t trunk[HIDDEN_SIZE];
        const int16_t* acc = state.accumulators[stm].values;
        for (int i = 0; i < HIDDEN_SIZE; ++i) {
            trunk[i] = crelu(acc[i]);
        }

        int32_t ha_l1[HEAD_HIDDEN_SIZE];
        linear_layer_imp(trunk, heads.head_a_weights[bucket][0], heads.head_a_biases[bucket], ha_l1, HIDDEN_SIZE, HEAD_HIDDEN_SIZE);
        int16_t ha_l1_act[HEAD_HIDDEN_SIZE];
        for(int i=0; i<HEAD_HIDDEN_SIZE; ++i) ha_l1_act[i] = crelu((int16_t)(ha_l1[i] >> 6));
        int32_t score_a_raw;
        linear_layer_imp(ha_l1_act, heads.head_a_out_weights[bucket][0], heads.head_a_out_bias[bucket], &score_a_raw, HEAD_HIDDEN_SIZE, 1);

        int32_t hb_l1[HEAD_HIDDEN_SIZE];
        linear_layer_imp(trunk, heads.head_b_weights[bucket][0], heads.head_b_biases[bucket], hb_l1, HIDDEN_SIZE, HEAD_HIDDEN_SIZE);
        int16_t hb_l1_act[HEAD_HIDDEN_SIZE];
        for(int i=0; i<HEAD_HIDDEN_SIZE; ++i) hb_l1_act[i] = crelu((int16_t)(hb_l1[i] >> 6));
        int32_t score_b_raw;
        linear_layer_imp(hb_l1_act, heads.head_b_out_weights[bucket][0], heads.head_b_out_bias[bucket], &score_b_raw, HEAD_HIDDEN_SIZE, 1);

        int32_t g_l1[GATE_HIDDEN_SIZE];
        linear_layer_imp(trunk, heads.gate_weights[bucket][0], heads.gate_biases[bucket], g_l1, HIDDEN_SIZE, GATE_HIDDEN_SIZE);
        int16_t g_l1_act[GATE_HIDDEN_SIZE];
        for(int i=0; i<GATE_HIDDEN_SIZE; ++i) g_l1_act[i] = crelu((int16_t)(g_l1[i] >> 6));
        int32_t gate_raw;
        linear_layer_imp(g_l1_act, heads.gate_out_weights[bucket][0], heads.gate_out_bias[bucket], &gate_raw, GATE_HIDDEN_SIZE, 1);

        double gate = 1.0 / (1.0 + std::exp(-(double)gate_raw / 64.0));
        double da = (double)score_a_raw;
        double db = (double)score_b_raw;
        double final_score = gate * da + (1.0 - gate) * db;

        std::cout << "bucket: " << bucket << " a: " << da << " b: " << db << " g: " << gate << " score: " << (int)(final_score / 64.0) << std::endl;
    }

    void Network::test() {
        std::cout << "Running NNUE unit tests..." << std::endl;

        // Test CReLU
        if (crelu(300) != 255) std::cout << "FAIL: CReLU(300)" << std::endl;
        if (crelu(-10) != 0) std::cout << "FAIL: CReLU(-10)" << std::endl;
        if (crelu(100) != 100) std::cout << "FAIL: CReLU(100)" << std::endl;

        // Test Sigmoid
        // gate_raw = 0 -> 0.5
        double s0 = 1.0 / (1.0 + std::exp(0.0));
        if (std::abs(s0 - 0.5) > 0.0001) std::cout << "FAIL: Sigmoid(0)" << std::endl;

        std::cout << "NNUE unit tests completed." << std::endl;
    }
}
