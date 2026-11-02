#ifndef TYPES_HPP
#define TYPES_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

enum class DesignMode {
    IN_ORDER_BASELINE,       // Design 1: Fig. 3(a) / SIMT-EVE (1 Port, In-Order)
    PIPELINED,               // Design 2: Fig. 3(b) / Pipe-2 Scoreboard (2 Ports, Scoreboard, Coarse Mem)
    PIPELINED_RENAMING_FIG3C,// Design 3a: Fig. 3(c) cartoon (2 Ports, Renaming, Port 0 Mem-only)
    PIPELINED_RENAMING,      // Design 3b: True Design 3 (2 Ports, Renaming, Port 0 computes when idle, Coarse Mem)
    FINE_GRAINED_ISSUE,      // Design 4: Fig. 3(d) / Pipe-2r (Design 3 + 3-step Fine-Grained Mem & SA Latch Freeze)
    PIPE_1,                  // Table V: 1 Port, Scoreboard, Fine-Grained, hybrid-4
    PIPE_1R                  // Table V: 1 Port, Renaming, Fine-Grained, hybrid-8
};

enum class OpCode { ADD, SUB, SLL, MUL, DIV, BRANCH };

struct SimConfig {
    DesignMode mode = DesignMode::FINE_GRAINED_ISSUE;
    int mul_latency = 105;       // Table III range: 105 - 634
    int div_latency = 145;       // Table III range: 145 - 1174
    int mem_visit_latency = 16;  // Off-array Memory Visiting Step ('M') cycles
    size_t max_mshrs = 32;        // Max concurrent in-flight memory visits per unit
    bool scale_hybrid4 = false;  // Toggle Table V hybrid-4 (64 threads) vs hybrid-8 (32 threads) scaling
};

inline int getCalcLatency(OpCode op, const SimConfig& cfg, bool is_renamed) {
    int base = 9;
    switch (op) {
        case OpCode::ADD:    base = 9; break;
        case OpCode::SUB:    base = 17; break;
        case OpCode::BRANCH: base = 13; break;
        case OpCode::SLL:    base = 82; break;
        case OpCode::MUL:    base = cfg.mul_latency; break;
        case OpCode::DIV:    base = cfg.div_latency; break;
    }
    if (cfg.scale_hybrid4 && !is_renamed) {
        return std::max(1, (base + 1) / 2);
    }
    return base;
}

constexpr int T_ADDR_STEP = 2; // Blue box: Addressing step on Port 0
constexpr int T_WB_STEP   = 2; // Gray box: Writeback step on Port 0

struct TraceOp {
    int id;
    std::string label;
    OpCode op;
    int rd;             // Architectural dest reg (0..31), or -1 if none
    int rs1;            // Architectural src1 reg (0..31), or -1 if immediate
    int rs2;            // Architectural src2 reg (0..31), or -1 if immediate
    bool rs1_is_mem;    // True if [rs1] is a memory pointer -> LOAD_PHASE_1
    bool rs2_is_mem;    // True if [rs2] is a memory pointer -> LOAD_PHASE_2
    bool rd_is_mem;     // True if [rd]  is a memory pointer -> STORE_PHASE
    bool is_branch;     // True if BRANCH -> stalls frontend fetch until committed
    bool has_imm = false;

    // Functional payloads for in-SRAM mathematical verification
    int64_t mem_val1 = 0; // Value loaded from memory during LOAD_PHASE_1
    int64_t mem_val2 = 0; // Value loaded from memory during LOAD_PHASE_2
    int64_t imm_val  = 0; // Immediate value if has_imm == true
};

enum class PhaseType  { NONE, LOAD_PHASE_1, LOAD_PHASE_2, CALC_PHASE, STORE_PHASE };
enum class PhaseState { SKIPPED, PENDING, ON_FLY, COMPLETED };
enum class MemSubStep { NONE, NEED_ADDR, IN_ADDR, IN_MEM_VISIT, NEED_WB, IN_WB, DONE };

struct OpTableEntry {
    TraceOp trace_op;

    int phys_rd = -1;
    int old_phys_rd = -1;
    int phys_rs1 = -1;
    int phys_rs2 = -1;
    int phys_load1_dest = -1;
    int phys_load2_dest = -1;
    std::vector<int> scratch_regs;

    PhaseState load1 = PhaseState::SKIPPED;
    PhaseState load2 = PhaseState::SKIPPED;
    PhaseState calc  = PhaseState::PENDING;
    PhaseState store = PhaseState::SKIPPED;

    MemSubStep load1_sub = MemSubStep::NONE;
    MemSubStep load2_sub = MemSubStep::NONE;
    MemSubStep store_sub = MemSubStep::NONE;

    bool committed = false;

    bool hasOnFlyPhase() const {
        return load1 == PhaseState::ON_FLY || load2 == PhaseState::ON_FLY ||
               calc  == PhaseState::ON_FLY || store == PhaseState::ON_FLY;
    }

    PhaseType nextPendingPhase() const {
        if (load1 == PhaseState::PENDING) return PhaseType::LOAD_PHASE_1;
        if (load2 == PhaseState::PENDING) return PhaseType::LOAD_PHASE_2;
        if (calc  == PhaseState::PENDING) return PhaseType::CALC_PHASE;
        if (store == PhaseState::PENDING) return PhaseType::STORE_PHASE;
        return PhaseType::NONE;
    }

    bool allPhasesDone() const {
        return (load1 == PhaseState::SKIPPED || load1 == PhaseState::COMPLETED) &&
               (load2 == PhaseState::SKIPPED || load2 == PhaseState::COMPLETED) &&
               (calc  == PhaseState::COMPLETED) &&
               (store == PhaseState::SKIPPED || store == PhaseState::COMPLETED);
    }
};

#endif // TYPES_HPP