#include "network.h"
#include <fstream>
#include <iostream>
#include <cmath>
#include <cstring>
#include <vector>

#ifdef __AVX2__
#include <immintrin.h>
#endif

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
        for (int b = 0; b < NUM_BUCKETS; ++b) {
            file.read(reinterpret_cast<char*>(weights.weights[b]), sizeof(int16_t) * FEATURE_SIZE * HIDDEN_SIZE);
            file.read(reinterpret_cast<char*>(weights.biases[b]), sizeof(int16_t) * HIDDEN_SIZE);
        }

        // Read Heads
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
    void linear_layer_imp(const int16_t* input, const int8_t* weights, const int32_t* biases, int32_t* output, int input_size, int output_size) {
        for (int j = 0; j < output_size; ++j) {
            int32_t sum = biases[j];
            const int8_t* row = weights + j * input_size;

#ifdef __AVX2__
            __m256i sum_vec = _mm256_setzero_si256();
            for (int i = 0; i < input_size; i += 16) {
                 __m256i in = _mm256_loadu_si256((const __m256i*)&input[i]);
                 __m128i w8 = _mm_loadu_si128((const __m128i*)&row[i]);
                 __m256i w16 = _mm256_cvtepi8_epi16(w8);
                 __m256i prod = _mm256_madd_epi16(in, w16);
                 sum_vec = _mm256_add_epi32(sum_vec, prod);
            }
            // Horizontal sum
            __m128i sum_low = _mm256_castsi256_si128(sum_vec);
            __m128i sum_high = _mm256_extracti128_si256(sum_vec, 1);
            sum_low = _mm_add_epi32(sum_low, sum_high);
            sum_low = _mm_hadd_epi32(sum_low, sum_low);
            sum_low = _mm_hadd_epi32(sum_low, sum_low);
            sum += _mm_cvtsi128_si32(sum_low);
#else
            for (int i = 0; i < input_size; ++i) {
                sum += (int32_t)input[i] * (int32_t)row[i];
            }
#endif
            output[j] = sum;
        }
    }

    int Network::evaluate(const Position& pos, const NNUEState& state) {
        Color stm = pos.side_to_move();
        int bucket = state.buckets[stm];

        // 1. Trunk Activation
        int16_t trunk[HIDDEN_SIZE]; // Stack allocated, likely unaligned for AVX2 aligned load
        const int16_t* acc = state.accumulators[stm].values; // Aligned

#ifdef __AVX2__
        __m256i zero = _mm256_setzero_si256();
        __m256i qa_vec = _mm256_set1_epi16(QA);
        for (int i = 0; i < HIDDEN_SIZE; i += 16) {
             __m256i v = _mm256_load_si256((const __m256i*)&acc[i]);
             v = _mm256_max_epi16(v, zero);
             v = _mm256_min_epi16(v, qa_vec);
             _mm256_storeu_si256((__m256i*)&trunk[i], v);
        }
#else
        for (int i = 0; i < HIDDEN_SIZE; ++i) {
            trunk[i] = crelu(acc[i]);
        }
#endif

        // 2. Head A
        int32_t ha_l1[HEAD_HIDDEN_SIZE];
        linear_layer_imp(trunk, heads.head_a_weights[bucket][0], heads.head_a_biases[bucket], ha_l1, HIDDEN_SIZE, HEAD_HIDDEN_SIZE);

        int16_t ha_l1_act[HEAD_HIDDEN_SIZE];
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
        double gate = 1.0 / (1.0 + std::exp(-(double)gate_raw / 64.0));

        // 5. Blend
        double da = (double)score_a_raw;
        double db = (double)score_b_raw;

        double final_score = gate * da + (1.0 - gate) * db;

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

        if (crelu(300) != 255) std::cout << "FAIL: CReLU(300)" << std::endl;
        if (crelu(-10) != 0) std::cout << "FAIL: CReLU(-10)" << std::endl;
        if (crelu(100) != 100) std::cout << "FAIL: CReLU(100)" << std::endl;

        double s0 = 1.0 / (1.0 + std::exp(0.0));
        if (std::abs(s0 - 0.5) > 0.0001) std::cout << "FAIL: Sigmoid(0)" << std::endl;

        std::cout << "NNUE unit tests completed." << std::endl;
    }
}
