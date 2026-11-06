#ifndef EXEC_HOOK_HPP
#define EXEC_HOOK_HPP

#include "sim_op.hpp"

#include <cstdint>
#include <string>

// Execution-driven simulation: the timing engine asks the program for the next operation,
// and tells it when each phase FINISHES (after the Table III / memory cycles have passed).
// The real ALU computation, the real memory access and the real branch decision all happen
// at exactly that cycle, so there is a single notion of time for functional + in-SRAM timing.
//
// phys_* are the register indices the engine uses (architectural index without renaming,
// physical index with renaming); the executor keeps one value per index.
class ExecHook {
public:
    virtual ~ExecHook() = default;

    virtual bool has_next() const = 0;            // is there another operation to fetch?
    virtual InSramOp peek() = 0;                  // next operation in program order
    virtual void fetched() = 0;                   // the engine took it (a branch blocks further fetch)

    // Each returns a short text for the cycle log (what was computed / read / written).
    virtual std::string load_done(const InSramOp& op, bool second, int phys_ptr) = 0;
    virtual std::string calc_done(const InSramOp& op, int phys_src1, int phys_src2, int phys_dest) = 0;
    virtual std::string store_done(const InSramOp& op) = 0;
};

#endif
