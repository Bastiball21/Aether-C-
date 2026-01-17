#ifndef EVAL_UTIL_H
#define EVAL_UTIL_H

#include <cstdint>

namespace EvalUtil {

struct WdlParams {
    float win_scale;
    float win_prob_threshold;
};

extern const WdlParams kDefaultWdlParams;

int clamp_score_cp(int score_cp, int clamp_abs = 2000, int mate_threshold = 20000,
    int mate_cap = 2000);

uint8_t wdl_from_cp(int score_cp, const WdlParams& params = kDefaultWdlParams);

} // namespace EvalUtil

#endif // EVAL_UTIL_H
