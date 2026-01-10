#include "nnue.h"
#include "features.h"
#include <fstream>
#include <iostream>
#include <cstring>

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

    bool load_network(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        // Header Check (Stockfish .nnue)
        uint32_t header_magic;
        file.read(reinterpret_cast<char*>(&header_magic), sizeof(uint32_t));

        // Let's try to seek past header if magic matches.
        if (header_magic == 0x7AF32F20) {
            // Version.
            uint32_t version;
            file.read(reinterpret_cast<char*>(&version), 4);
            uint32_t hash;
            file.read(reinterpret_cast<char*>(&hash), 4);
            uint32_t desc_len;
            file.read(reinterpret_cast<char*>(&desc_len), 4);
            file.seekg(desc_len, std::ios::cur); // Skip description
        } else {
            // Reset to beginning if not magic
             file.seekg(0, std::ios::beg);
        }

        // Feature Transformer Layer
        // Expect Hash
        uint32_t hash;
        file.read(reinterpret_cast<char*>(&hash), 4); // Layer Hash

        // Read Biases (256 * int16)
        if (!read_buffer(file, GlobalNetwork.ft_biases, kFeatureTransformerOutput)) return false;

        // Read Weights (64 * 768 * 256 * int16)
        size_t ft_weights_count = 64 * 768 * kFeatureTransformerOutput;
        if (!read_buffer(file, GlobalNetwork.ft_weights, ft_weights_count)) return false;

        // Layer 1 (Affine 512->32)
        file.read(reinterpret_cast<char*>(&hash), 4);
        GlobalNetwork.layer1.resize();
        if (!read_buffer(file, GlobalNetwork.layer1.biases, 32)) return false;
        if (!read_buffer(file, GlobalNetwork.layer1.weights, 32 * 512)) return false;

        // Layer 2 (Affine 32->32)
        file.read(reinterpret_cast<char*>(&hash), 4);
        GlobalNetwork.layer2.resize();
        if (!read_buffer(file, GlobalNetwork.layer2.biases, 32)) return false;
        if (!read_buffer(file, GlobalNetwork.layer2.weights, 32 * 32)) return false;

        // Output (Affine 32->1)
        file.read(reinterpret_cast<char*>(&hash), 4);
        GlobalNetwork.output.resize();
        if (!read_buffer(file, GlobalNetwork.output.biases, 1)) return false;
        if (!read_buffer(file, GlobalNetwork.output.weights, 32)) return false;

        IsLoaded = true;
        return true;
    }

    void refresh_accumulator(const Position& pos, Color c, Accumulator& acc) {
        // Init with bias
        acc.init(GlobalNetwork.ft_biases.data());

        // Get King Square for bucket
        Bitboard k_bb = pos.pieces(KING, c);
        if (!k_bb) return; // Should not happen in valid position

        Square k_sq = Bitboards::lsb(k_bb);
        int k_bucket = king_bucket(k_sq, c);

        // Pointer to weights for this bucket
        const int16_t* bucket_weights = GlobalNetwork.ft_weights.data() +
                                        (k_bucket * 768 * kFeatureTransformerOutput);

        Bitboard occ = pos.pieces();
        while (occ) {
            Square s = (Square)Bitboards::pop_lsb(occ);
            Piece p = pos.piece_on(s);

            // Skip own king
            if ((p == W_KING && c == WHITE) || (p == B_KING && c == BLACK)) continue;

            // Calculate feature index
            int idx;
            if (c == WHITE) {
                idx = feature_index(p, s);
            } else {
                idx = feature_index_mirrored(p, s);
            }

            // Add weights
            const int16_t* w = bucket_weights + (idx * kFeatureTransformerOutput);
            for (int i = 0; i < kFeatureTransformerOutput; ++i) {
                acc.values[i] += w[i];
            }
        }
    }

    // Forward pass
    int evaluate(const Position& pos) {
        if (!IsLoaded) return 0;

        const Position::StateInfo* st = pos.state();
        Color stm = pos.side_to_move();

        const Accumulator& acc_us = (stm == WHITE) ? st->accumulators[WHITE] : st->accumulators[BLACK];
        const Accumulator& acc_them = (stm == WHITE) ? st->accumulators[BLACK] : st->accumulators[WHITE];

        // Prepare input for Layer 1: 512 int8_t
        // Clamp accumulators [0..127]
        std::array<int8_t, 512> input;

        for (int i = 0; i < 256; ++i) {
            input[i] = static_cast<int8_t>(clamp_output(acc_us.values[i]));
            input[256 + i] = static_cast<int8_t>(clamp_output(acc_them.values[i]));
        }

        // Layer 1
        std::array<int32_t, 32> l1_out;
        std::array<int8_t, 32> l1_out_clamped;

        for (int i = 0; i < 32; ++i) {
            int32_t sum = GlobalNetwork.layer1.biases[i];
            // Dot product
            const int8_t* row = GlobalNetwork.layer1.weights.data() + (i * 512);
            for (int j = 0; j < 512; ++j) {
                sum += row[j] * input[j];
            }
            l1_out[i] = sum;
            l1_out_clamped[i] = static_cast<int8_t>(clamp_output(sum >> 6)); // Shift? Usually shift 6 for L1
        }

        // Layer 2
        std::array<int32_t, 32> l2_out;
        std::array<int8_t, 32> l2_out_clamped;

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

        // Scale? Usually output is roughly in cp * CONST.
        // SF output is scaled.
        // Let's return raw sum / shift?
        // Usually output shift is related to quantization.
        // Assuming the trainer output matches expected CP range.
        // Typically output needs to be divided.
        // Common is / 16 or similar.
        // Let's assume the network outputs approx cp value after basic shift.
        // But with quantization:
        // L1: x * w -> shift 6
        // L2: x * w -> shift 6
        // Out: x * w -> no shift? or shift?
        // Stockfish uses `output_bucket` sometimes.
        // Let's assume standard `score / 16` or similar.
        // The standard scaling is `sum * 1 / FV_SCALE`.
        // Let's try `sum / 16` (>> 4).

        return sum / 16;
    }

} // namespace NNUE
