#ifndef MODEL_RENAMING_HPP
#define MODEL_RENAMING_HPP

#include "sim_op.hpp"
#include "kernel_data.hpp"

#include <cstdint>
#include <ostream>
#include <vector>

class ModelRenaming {
public:
    static uint64_t run(
        const std::vector<InSramOp>& trace,
        std::ostream* log_out = nullptr,
        KernelData* data = nullptr
    );
};

#endif