
#include "model_baseline.hpp"
#include "sim_engine.hpp"

#include <cstdint>
#include <vector>
#include <ostream>

uint64_t ModelBaseline::run(
    const std::vector<InSramOp>& trace,
    std::ostream* log_out
) {
    return SimEngine::run(
        trace,
        SimMode::BASELINE,
        log_out
    );
}
