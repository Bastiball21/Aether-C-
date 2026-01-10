#ifndef NNUE_LAYERS_H
#define NNUE_LAYERS_H

#include <cstdint>
#include <algorithm>
#include <vector>

namespace NNUE {

    // Clipped ReLU: [0, 127]
    // Stockfish uses CRelu(x) = max(0, min(127, x >> shift))
    // Usually the shift happens after the affine transform.
    // The "input" to the next layer is int8_t.

    inline int32_t clamp_output(int32_t sum) {
        return std::max(0, std::min(127, sum));
    }

    // Affine Transform Layer
    // Input: int8_t vector
    // Weights: int8_t matrix (flattened)
    // Bias: int32_t vector
    // Output: int32_t (before activation)
    template<int InDim, int OutDim>
    struct AffineLayer {
        std::vector<int8_t> weights; // Row-major: [OutDim][InDim] or Col-major? usually [Out][In] for cache loc.
                                     // Stockfish: [Out][In] (32x512)
        std::vector<int32_t> biases;

        void resize() {
            weights.resize(OutDim * InDim);
            biases.resize(OutDim);
        }
    };

    // Output Layer
    // Input: int8_t
    // Weights: int8_t
    // Bias: int32_t
    // Output: int32_t (final score)
    template<int InDim>
    struct OutputLayer {
        std::vector<int8_t> weights; // [1][InDim]
        std::vector<int32_t> biases; // [1] (scalar actually)

        void resize() {
            weights.resize(InDim);
            biases.resize(1);
        }
    };

} // namespace NNUE

#endif // NNUE_LAYERS_H
