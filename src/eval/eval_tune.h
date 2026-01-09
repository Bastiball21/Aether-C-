#ifndef EVAL_TUNE_H
#define EVAL_TUNE_H

#include "position.h"
#include <string>

namespace Eval {
    void tune_epd(const std::string& epd_file, const std::string& csv_file);
}

#endif // EVAL_TUNE_H
