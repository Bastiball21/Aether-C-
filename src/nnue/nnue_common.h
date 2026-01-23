#ifndef NNUE_COMMON_H
#define NNUE_COMMON_H

#include <cstdint>
#include <algorithm>

namespace NNUE {

    using int16_t = std::int16_t;
    using int32_t = std::int32_t;
    using int8_t = std::int8_t;
    using uint8_t = std::uint8_t;

    constexpr int NUM_BUCKETS = 8;
    constexpr int FEATURE_SIZE = 768;
    constexpr int HIDDEN_SIZE = 256;
    constexpr int HEAD_HIDDEN_SIZE = 32;
    constexpr int GATE_HIDDEN_SIZE = 8;

    // Activation range for CReLU
    constexpr int QA = 255;

    // Output Scale (internal cp scaling)
    // Trainer output usually is scaled.
    // If output is raw logits, we need to know the scale.
    // The prompt says "Output score in engine internal cp scaling".
    // Engine uses 100cp = 1 pawn.
    // We will assume the network outputs directly in cp (or close to it).
    // Usually NNUE outputs are scaled by some factor.
    // We'll define a constant SCALE.
    constexpr int OUTPUT_SCALE = 16; // Standard SF scale is usually around this or 1.
    // We will clarify later or make it a variable.
    // For now, assume 1.0 mapping or similar.

}

#endif
