#include "feature_transformer.h"
#include "../position.h"
#include <iostream>

namespace NNUE {

    FeatureTransformer* g_feature_transformer = nullptr;

    int FeatureTransformer::get_bucket(const Position& pos, Color c) {
        Square ksq = (Square)Bitboards::lsb(pos.pieces(KING, c));
        // Rank-based bucketing 0..7
        return (int)ksq / 8;
    }

    void FeatureTransformer::refresh_accumulators(NNUEState& state, const Position& pos) {
        // Compute buckets
        state.buckets[WHITE] = get_bucket(pos, WHITE);
        state.buckets[BLACK] = get_bucket(pos, BLACK);
        state.computed[WHITE] = true;
        state.computed[BLACK] = true;

        for (Color c : {WHITE, BLACK}) {
            int bucket = state.buckets[c];
            // Init with bias
            state.accumulators[c].init(weights.biases[bucket]);
        }

        // Add features
        for (int sq = 0; sq < 64; ++sq) {
            Piece p = pos.piece_on((Square)sq);
            if (p != NO_PIECE) {
                PieceType pt = (PieceType)(p % 6);
                Color pc = (Color)(p / 6);

                // Feature Index
                for (Color perspective : {WHITE, BLACK}) {
                     int bucket = state.buckets[perspective];
                     int idx;
                     if (pc == perspective) {
                         idx = 64 * pt + sq;
                     } else {
                         idx = 384 + 64 * pt + (sq ^ 56);
                     }

                     // Add weights
                     const int16_t* w = weights.weights[bucket][idx];
                     for (int i = 0; i < HIDDEN_SIZE; ++i) {
                         state.accumulators[perspective].values[i] += w[i];
                     }
                }
            }
        }
    }

    void FeatureTransformer::update_accumulators(NNUEState& next, const NNUEState& prev, const Position& pos, const std::vector<FeatureUpdate>& updates) {
        // Calculate new buckets
        // pos is already updated.
        next.buckets[WHITE] = get_bucket(pos, WHITE);
        next.buckets[BLACK] = get_bucket(pos, BLACK);
        next.computed[WHITE] = true;
        next.computed[BLACK] = true;

        for (Color c : {WHITE, BLACK}) {
            if (next.buckets[c] != prev.buckets[c] || !prev.computed[c]) {
                // Refresh this side
                int bucket = next.buckets[c];
                next.accumulators[c].init(weights.biases[bucket]);

                // Scan board
                for (int sq = 0; sq < 64; ++sq) {
                    Piece p = pos.piece_on((Square)sq);
                    if (p != NO_PIECE) {
                        PieceType pt = (PieceType)(p % 6);
                        Color pc = (Color)(p / 6);
                        int idx;
                        if (pc == c) {
                             idx = 64 * pt + sq;
                        } else {
                             idx = 384 + 64 * pt + (sq ^ 56);
                        }
                        const int16_t* w = weights.weights[bucket][idx];
                        for (int i = 0; i < HIDDEN_SIZE; ++i) {
                             next.accumulators[c].values[i] += w[i];
                        }
                    }
                }
            } else {
                // Incremental Update
                next.accumulators[c].copy_from(prev.accumulators[c]);
                int bucket = next.buckets[c];

                for (const auto& u : updates) {
                    PieceType pt = (PieceType)(u.piece % 6);
                    Color pc = (Color)(u.piece / 6);
                    int idx;
                    if (pc == c) {
                        idx = 64 * pt + u.sq;
                    } else {
                        idx = 384 + 64 * pt + (u.sq ^ 56);
                    }

                    const int16_t* w = weights.weights[bucket][idx];
                    if (u.add) {
                        for (int i = 0; i < HIDDEN_SIZE; ++i) {
                            next.accumulators[c].values[i] += w[i];
                        }
                    } else {
                        for (int i = 0; i < HIDDEN_SIZE; ++i) {
                            next.accumulators[c].values[i] -= w[i];
                        }
                    }
                }
            }
        }
    }
}
