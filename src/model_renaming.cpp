#include "model_renaming.hpp"
#include "sim_engine.hpp"

uint64_t ModelRenaming::run(
    const std::vector<InSramOp>& trace,
    std::ostream* log_out,
    KernelData* data
) {
    return SimEngine::run(trace, SimMode::RENAMING, log_out, data);
}