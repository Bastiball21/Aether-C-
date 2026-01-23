#ifndef ACCUMULATOR_H
#define ACCUMULATOR_H

#include "nnue_common.h"
#include <cstring>

namespace NNUE {

    struct alignas(64) Accumulator {
        int16_t values[HIDDEN_SIZE];

        void init(const int16_t* bias) {
            std::memcpy(values, bias, HIDDEN_SIZE * sizeof(int16_t));
        }

        // Helper to copy from another accumulator
        void copy_from(const Accumulator& other) {
            std::memcpy(values, other.values, HIDDEN_SIZE * sizeof(int16_t));
        }
    };

    struct NNUEState {
        Accumulator accumulators[2];
        int buckets[2];
        bool computed[2];
    };

}

#endif
