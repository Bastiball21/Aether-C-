#include "eval_util.h"
#include <algorithm>
#include <cmath>

namespace EvalUtil {

const WdlParams kDefaultWdlParams{400.0f, 0.70f};

int clamp_score_cp(int score_cp, int clamp_abs, int mate_threshold, int mate_cap) {
    int clamped = std::clamp(score_cp, -clamp_abs, clamp_abs);
    if (std::abs(score_cp) >= mate_threshold) {
        clamped = score_cp > 0 ? mate_cap : -mate_cap;
    }
    return clamped;
}

uint8_t wdl_from_cp(int score_cp, const WdlParams& params) {
    float scaled = static_cast<float>(score_cp) / params.win_scale;
    float win_prob = 1.0f / (1.0f + std::exp(-scaled));
    if (win_prob >= params.win_prob_threshold) {
        return 2;
    }
    if (win_prob <= 1.0f - params.win_prob_threshold) {
        return 0;
    }
    return 1;
}

} // namespace EvalUtil
