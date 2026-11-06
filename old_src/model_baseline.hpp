#ifndef MODEL_BASELINE_HPP
#define MODEL_BASELINE_HPP

#include "sim_op.hpp"

#include <cstdint>
#include <ostream>
#include <vector>

class ModelBaseline {
public:
    static uint64_t run(
        const std::vector<InSramOp>& trace,
        std::ostream* log_out = nullptr
    );
};

#endif