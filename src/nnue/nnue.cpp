#include "nnue.h"
#include "features.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <vector>

namespace NNUE {

    Network GlobalNetwork;
    bool IsLoaded = false;

    void init() {
        IsLoaded = false;
    }

    // Helper: Read buffer
    template<typename T>
    bool read_buffer(std::ifstream& file, std::vector<T>& buffer, size_t count) {
        buffer.resize(count);
        file.read(reinterpret_cast<char*>(buffer.data()), count * sizeof(T));
        return file.good();
    }

    // Helper: Read buffer for Array (fixed size)
    template<typename T, size_t N>
    bool read_array(std::ifstream& file, std::array<T, N>& buffer) {
        file.read(reinterpret_cast<char*>(buffer.data()), N * sizeof(T));
        return file.good();
    }

    bool load_network(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "NNUE: Could not open file " << path << std::endl;
            return false;
        }

        // Header
        uint32_t magic;
        file.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));

        if (magic != 0x7AF32F16 && magic != 0x7AF32F20) {
            std::cerr << "NNUE: Invalid Magic: " << std::hex << magic << std::dec << std::endl;
            return false;
        }

        uint32_t version;
        file.read(reinterpret_cast<char*>(&version), 4);

        uint32_t desc_len;
        file.read(reinterpret_cast<char*>(&desc_len), 4);

        // Skip Description
        file.seekg(desc_len, std::ios::cur);

        uint32_t net_hash;
        file.read(reinterpret_cast<char*>(&net_hash), 4);

        // Transformer Hash (extra u32 that was missing in previous code)
        uint32_t trans_hash;
        file.read(reinterpret_cast<char*>(&trans_hash), 4);

        // Feature Transformer Blob
        // Order: Biases then Weights (Standard HalfKP)

        // Read Biases (256 * int16)
        if (!read_buffer(file, GlobalNetwork.ft_biases, kFeatureTransformerOutput)) {
            std::cerr << "NNUE: Failed to read FT biases" << std::endl;
            return false;
        }

        // Read Weights (41024 * 256 * int16)
        size_t ft_weights_count = kFeatureDim * kFeatureTransformerOutput;
        if (!read_buffer(file, GlobalNetwork.ft_weights, ft_weights_count)) {
            std::cerr << "NNUE: Failed to read FT weights" << std::endl;
            return false;
        }

        // Layers - No hashes between layers

        // Layer 1 (Affine 512->32)
        // Biases (32 int32)
        if (!read_buffer(file, GlobalNetwork.layer1.biases, 32)) {
            std::cerr << "NNUE: Failed to read L1 biases" << std::endl;
            return false;
        }
        // Weights (32 * 512 int8)
        if (!read_buffer(file, GlobalNetwork.layer1.weights, 32 * 512)) {
            std::cerr << "NNUE: Failed to read L1 weights" << std::endl;
            return false;
        }

        // Layer 2 (Affine 32->32)
        // Biases (32 int32)
        if (!read_buffer(file, GlobalNetwork.layer2.biases, 32)) {
            std::cerr << "NNUE: Failed to read L2 biases" << std::endl;
            return false;
        }
        // Weights (32 * 32 int8)
        if (!read_buffer(file, GlobalNetwork.layer2.weights, 32 * 32)) {
             std::cerr << "NNUE: Failed to read L2 weights" << std::endl;
             return false;
        }

        // Output (Affine 32->1)
        // Biases (1 int32)
        if (!read_buffer(file, GlobalNetwork.output.biases, 1)) {
            std::cerr << "NNUE: Failed to read Output biases" << std::endl;
            return false;
        }
        // Weights (1 * 32 int8)
        if (!read_buffer(file, GlobalNetwork.output.weights, 32)) {
            std::cerr << "NNUE: Failed to read Output weights" << std::endl;
            return false;
        }

        IsLoaded = true;
        std::cout << "info string NNUE Loaded: " << path << " Features=HalfKP(41024) L1=32 L2=32" << std::endl;
        return true;
    }

    void refresh_accumulator(const Position& pos, Color perspective, Accumulator& acc) {
        if (!IsLoaded) return;

        // Init with FT Biases
        acc.init(GlobalNetwork.ft_biases.data());

        Square king_sq = Bitboards::lsb(pos.pieces(KING, perspective));

        // 1. Add King Bias Feature
        int bias_idx = bias_index(king_sq, perspective);
        const int16_t* bias_w = GlobalNetwork.ft_weights.data() + (bias_idx * kFeatureTransformerOutput);
        for (int i = 0; i < kFeatureTransformerOutput; ++i) {
            acc.values[i] += bias_w[i];
        }

        // 2. Add Piece Features
        Bitboard occ = pos.pieces();
        while (occ) {
            Square s = (Square)Bitboards::pop_lsb(occ);
            Piece p = pos.piece_on(s);

            // Skip Kings (kings are not features in HalfKP(Friend), except via the bias)
            if (p == W_KING || p == B_KING) continue;

            // Calculate feature index
            int idx = feature_index(king_sq, p, s, perspective);

            if (idx != -1) {
                 const int16_t* w = GlobalNetwork.ft_weights.data() + (idx * kFeatureTransformerOutput);
                 for (int i = 0; i < kFeatureTransformerOutput; ++i) {
                     acc.values[i] += w[i];
                 }
            }
        }
    }

    // Helper to add/remove feature
    inline void update_feature(Accumulator& acc, int idx, bool add) {
         const int16_t* w = GlobalNetwork.ft_weights.data() + (idx * kFeatureTransformerOutput);
         if (add) {
             for (int i = 0; i < kFeatureTransformerOutput; ++i) {
                 acc.values[i] += w[i];
             }
         } else {
             for (int i = 0; i < kFeatureTransformerOutput; ++i) {
                 acc.values[i] -= w[i];
             }
         }
    }

    void update_accumulator(const Position&, Color, const Position::StateInfo*, Position::StateInfo*) {
        // Deprecated/Unused - Logic moved to Position::make_move
    }

    // Forward pass
    int evaluate(const Position& pos) {
        if (!IsLoaded) return 0;

        const Position::StateInfo* st = pos.state();
        Color stm = pos.side_to_move();

        const Accumulator& acc_us = st->accumulators[stm];
        const Accumulator& acc_them = st->accumulators[~stm];

        // Prepare input for Layer 1: 512 int8_t
        // Clamp accumulators [0..127]
        alignas(64) std::array<int8_t, 512> input;

        for (int i = 0; i < 256; ++i) {
            input[i] = static_cast<int8_t>(clamp_output(acc_us.values[i]));
            input[256 + i] = static_cast<int8_t>(clamp_output(acc_them.values[i]));
        }

        // Layer 1
        alignas(64) std::array<int32_t, 32> l1_out;
        alignas(64) std::array<int8_t, 32> l1_out_clamped;

        for (int i = 0; i < 32; ++i) {
            int32_t sum = GlobalNetwork.layer1.biases[i];
            const int8_t* row = GlobalNetwork.layer1.weights.data() + (i * 512);
            // Vectorize? Compiler -O3 should handle simple loops.
            for (int j = 0; j < 512; ++j) {
                sum += row[j] * input[j];
            }
            l1_out[i] = sum;
            l1_out_clamped[i] = static_cast<int8_t>(clamp_output(sum >> 6));
        }

        // Layer 2
        alignas(64) std::array<int32_t, 32> l2_out;
        alignas(64) std::array<int8_t, 32> l2_out_clamped;

        for (int i = 0; i < 32; ++i) {
            int32_t sum = GlobalNetwork.layer2.biases[i];
            const int8_t* row = GlobalNetwork.layer2.weights.data() + (i * 32);
            for (int j = 0; j < 32; ++j) {
                sum += row[j] * l1_out_clamped[j];
            }
            l2_out[i] = sum;
            l2_out_clamped[i] = static_cast<int8_t>(clamp_output(sum >> 6));
        }

        // Output
        int32_t sum = GlobalNetwork.output.biases[0];
        const int8_t* row = GlobalNetwork.output.weights.data();
        for (int j = 0; j < 32; ++j) {
            sum += row[j] * l2_out_clamped[j];
        }

        // Scale Output
        return sum / 16;
    }

} // namespace NNUE
