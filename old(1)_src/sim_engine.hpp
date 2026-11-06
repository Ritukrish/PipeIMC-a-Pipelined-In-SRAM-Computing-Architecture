
#ifndef SIM_ENGINE_HPP
#define SIM_ENGINE_HPP

#include "sim_op.hpp"
#include "kernel_data.hpp"

#include <vector>
#include <cstdint>
#include <ostream>

enum class SimMode {
    BASELINE,       // Design 1: In-order baseline
    PIPELINED,      // Design 2: Pipelined, in-order issue
    RENAMING,       // Design 3: Pipelined with register renaming
    FINE_GRAINED    // Design 4: Pipelined with renaming and fine-grained issue
};

class SimEngine {
public:

    [[nodiscard]]
    static uint64_t run(
        const std::vector<InSramOp>& trace,
        SimMode mode,
        std::ostream* log_out = nullptr,
        KernelData* data = nullptr   // real values: if given, every operation is actually computed
    );
};

#endif // SIM_ENGINE_HPP
