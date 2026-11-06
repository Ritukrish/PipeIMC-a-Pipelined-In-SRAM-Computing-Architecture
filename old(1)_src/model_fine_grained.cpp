#include "model_fine_grained.hpp"
#include "sim_engine.hpp"

uint64_t ModelFineGrained::run(
    const std::vector<InSramOp>& trace,
    std::ostream* log_out,
    KernelData* data
) {
    return SimEngine::run(trace, SimMode::FINE_GRAINED, log_out, data);
}