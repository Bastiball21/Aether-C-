#ifndef NNUE_STRUCTS_H
#define NNUE_STRUCTS_H

#include <array>
#include <cstdint>

namespace NNUE {

    // Constants for NNUE Architecture
    constexpr int kFeatureTransformerOutput = 256;
    constexpr int kHiddenLayer1Size = 32;
    constexpr int kHiddenLayer2Size = 32;

    // Quantization
    // Accumulators are int16. ClippedReLU clamps to [0, 127] (QA=255?).
    // Usually Stockfish uses shift=6 for input?
    // Let's assume standard behavior:
    // FT Weights: int16
    // Accumulators: int16
    // Layer1 Weights: int8
    // Layer1 Biases: int32
    // Output of Layer1: int8 (clamped)

    struct Accumulator {
        std::array<int16_t, kFeatureTransformerOutput> values;

        void init(const int16_t* bias) {
            for (int i = 0; i < kFeatureTransformerOutput; ++i) {
                values[i] = bias[i];
            }
        }
    };

} // namespace NNUE

#endif // NNUE_STRUCTS_H
