
#ifndef SIM_OP_HPP
#define SIM_OP_HPP

#include <cstdint>

enum class OpType {
    ADD,
    SUB,
    MUL,
    DIV,
    LOGIC,
    BRANCH,
    BARRIER
};

struct InSramOp {

    // Unique operation identifier
    uint64_t op_id = 0;

    // Execution information
    int warp_id = 0;
    int subarray_id = 0;
    OpType type = OpType::ADD;

    // Architectural registers (r0-r31)
    // r0 is the immutable zero register.
    int src1 = 0;
    int src2 = 0;
    int dest = 0;

    // Immediate operand
    bool has_imm = false;
    int imm_val = 0;

    // Operation phases
    // Each enabled load is an independent memory phase.
    bool has_load1 = false;
    bool has_load2 = false;
    bool has_calc = true;
    bool has_store = false;

    // Branch, barrier or other control-flow operation
    bool affects_control_flow = false;

    // Latency of ONE memory phase:
    // Address generation + Memory visiting + Writeback
    int addr_cycles = 2;
    int visit_cycles = 16;
    int writeback_cycles = 2;

    // Calculation phase latency
    // Based on PipeIMC Table III.
    int calc_cycles = 9;

    // Latency of one memory phase, not the sum of all
    // load and store phases of this operation.
    constexpr int mem_cycles() const noexcept {
        return addr_cycles + visit_cycles + writeback_cycles;
    }

    // Number of independent memory phases.
    constexpr int memory_phase_count() const noexcept {
        return static_cast<int>(has_load1) +
               static_cast<int>(has_load2) +
               static_cast<int>(has_store);
    }

    constexpr bool has_memory() const noexcept {
        return memory_phase_count() > 0;
    }

    constexpr bool valid_latencies() const noexcept {
        return addr_cycles >= 0 &&
               visit_cycles >= 0 &&
               writeback_cycles >= 0 &&
               calc_cycles >= 0;
    }

    constexpr bool valid_registers() const noexcept {
        return src1 >= 0 && src1 < 32 &&
               src2 >= 0 && src2 < 32 &&
               dest >= 0 && dest < 32;
    }
};

#endif // SIM_OP_HPP
