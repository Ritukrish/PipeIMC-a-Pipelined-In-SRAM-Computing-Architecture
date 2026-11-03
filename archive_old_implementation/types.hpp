
#ifndef TYPES_HPP
#define TYPES_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <algorithm>

// ============================================================
// ARCHITECTURAL DESIGN MODES
// ============================================================

enum class DesignMode {

    IN_ORDER_BASELINE,        // Design 1: Single port, in-order

    PIPELINED,                // Design 2: Dual port, scoreboard

    PIPELINED_RENAMING_FIG3C, // Additional Fig. 3(c) variant

    PIPELINED_RENAMING,       // Design 3: Dual port + renaming

    FINE_GRAINED_ISSUE,       // Design 4: Fine-grained issue

    PIPE_1,                   // Additional Table V experiment

    PIPE_1R                   // Additional Table V experiment
};

// ============================================================
// SIMULATION CONFIGURATION
// ============================================================

struct SimConfig {

    DesignMode mode = DesignMode::FINE_GRAINED_ISSUE;

    int mul_latency = 105;
    int div_latency = 145;

    int mem_visit_latency = 16;

    size_t max_mshrs = 32;

    bool scale_hybrid4 = false;
};

// ============================================================
// COMPUTATION LATENCY
// ============================================================

enum class OpCode {
    ADD,
    SUB,
    SLL,
    MUL,
    DIV,
    BRANCH
};

inline int getCalcLatency(
    OpCode op,
    const SimConfig& cfg,
    bool is_renamed
) {

    int base = 9;

    switch (op) {

        case OpCode::ADD:
            base = 9;
            break;

        case OpCode::SUB:
            base = 17;
            break;

        case OpCode::BRANCH:
            base = 13;
            break;

        case OpCode::SLL:
            base = 82;
            break;

        case OpCode::MUL:
            base = cfg.mul_latency;
            break;

        case OpCode::DIV:
            base = cfg.div_latency;
            break;
    }

    // Optional hybrid-4 scaling.
    // Retained from the original configuration.
    if (cfg.scale_hybrid4 && !is_renamed) {
        return std::max(1, (base + 1) / 2);
    }

    return std::max(1, base);
}

// ============================================================
// MEMORY PHASE CONSTANTS
// ============================================================

constexpr int T_ADDR_STEP = 2;
constexpr int T_WB_STEP = 2;

// ============================================================
// OPERATION TRACE
// ============================================================

struct TraceOp {

    int id = 0;

    std::string label;

    OpCode op = OpCode::ADD;

    // Architectural registers:
    // 0..31 = valid register indices
    // -1 = unused register / immediate operand

    int rd = -1;
    int rs1 = -1;
    int rs2 = -1;

    // Memory access flags

    bool rs1_is_mem = false;
    bool rs2_is_mem = false;
    bool rd_is_mem = false;

    // Control flow

    bool is_branch = false;

    // Immediate operand

    bool has_imm = false;

    // Functional payloads for mathematical verification

    int64_t mem_val1 = 0;
    int64_t mem_val2 = 0;
    int64_t imm_val = 0;
};

// ============================================================
// EXECUTION PHASE TYPES
// ============================================================

enum class PhaseType {

    NONE,

    LOAD_PHASE_1,
    LOAD_PHASE_2,

    CALC_PHASE,

    STORE_PHASE
};

enum class PhaseState {

    SKIPPED,

    PENDING,

    ON_FLY,

    COMPLETED
};

enum class MemSubStep {

    NONE,

    NEED_ADDR,
    IN_ADDR,

    IN_MEM_VISIT,

    NEED_WB,
    IN_WB,

    DONE
};

// ============================================================
// OPERATION TABLE ENTRY
// ============================================================

struct OpTableEntry {

    TraceOp trace_op;

    // Physical registers

    int phys_rd = -1;
    int old_phys_rd = -1;

    int phys_rs1 = -1;
    int phys_rs2 = -1;

    int phys_load1_dest = -1;
    int phys_load2_dest = -1;

    std::vector<int> scratch_regs;

    // Execution phase states

    PhaseState load1 = PhaseState::SKIPPED;
    PhaseState load2 = PhaseState::SKIPPED;

    PhaseState calc = PhaseState::SKIPPED;

    PhaseState store = PhaseState::SKIPPED;

    // Memory subphase states

    MemSubStep load1_sub = MemSubStep::NONE;
    MemSubStep load2_sub = MemSubStep::NONE;
    MemSubStep store_sub = MemSubStep::NONE;

    bool committed = false;

    // --------------------------------------------------------
    // Check whether any phase is currently executing.
    // --------------------------------------------------------

    bool hasOnFlyPhase() const {

        return
            load1 == PhaseState::ON_FLY ||
            load2 == PhaseState::ON_FLY ||
            calc == PhaseState::ON_FLY ||
            store == PhaseState::ON_FLY;
    }

    // --------------------------------------------------------
    // Return the next pending phase.
    //
    // This represents a preferred phase order within one
    // operation, not a restriction on global OoO scheduling.
    // --------------------------------------------------------

    PhaseType nextPendingPhase() const {

        if (load1 == PhaseState::PENDING)
            return PhaseType::LOAD_PHASE_1;

        if (load2 == PhaseState::PENDING)
            return PhaseType::LOAD_PHASE_2;

        if (calc == PhaseState::PENDING)
            return PhaseType::CALC_PHASE;

        if (store == PhaseState::PENDING)
            return PhaseType::STORE_PHASE;

        return PhaseType::NONE;
    }

    // --------------------------------------------------------
    // Check whether all required phases are completed.
    //
    // SKIPPED means that the operation does not require
    // that particular phase.
    // --------------------------------------------------------

    bool allPhasesDone() const {

        const bool load1_done =
            load1 == PhaseState::SKIPPED ||
            load1 == PhaseState::COMPLETED;

        const bool load2_done =
            load2 == PhaseState::SKIPPED ||
            load2 == PhaseState::COMPLETED;

        const bool calc_done =
            calc == PhaseState::SKIPPED ||
            calc == PhaseState::COMPLETED;

        const bool store_done =
            store == PhaseState::SKIPPED ||
            store == PhaseState::COMPLETED;

        return
            load1_done &&
            load2_done &&
            calc_done &&
            store_done;
    }
};

#endif // TYPES_HPP
