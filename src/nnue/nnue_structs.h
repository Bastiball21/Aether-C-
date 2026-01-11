#ifndef NNUE_STRUCTS_H
#define NNUE_STRUCTS_H

#include <array>
#include <cstdint>

namespace NNUE {

    // Constants for NNUE Architecture
    constexpr int kFeatureTransformerOutput = 256;
    constexpr int kFeatureDim = 41024;
    constexpr int kHiddenLayer1Size = 32;
    constexpr int kHiddenLayer2Size = 32;

    constexpr int kHalfKP_KingSqBias = 40960;

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
