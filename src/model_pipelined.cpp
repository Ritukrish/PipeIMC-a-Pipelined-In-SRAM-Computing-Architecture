#include "model_pipelined.hpp"
#include "sim_engine.hpp"

uint64_t ModelPipelined::run(
    const std::vector<InSramOp>& trace,
    std::ostream* log_out,
    KernelData* data
) {
    return SimEngine::run(trace, SimMode::PIPELINED, log_out, data);
}