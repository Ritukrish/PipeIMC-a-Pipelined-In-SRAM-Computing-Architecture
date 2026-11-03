#ifndef SIMULATOR_HPP
#define SIMULATOR_HPP

#include "types.hpp"
#include "rename_unit.hpp"
#include <deque>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include "logger.hpp"

struct ActiveJob {
    int op_id = -1;
    std::string label = "";
    PhaseType phase = PhaseType::NONE;
    int remaining_cycles = 0;
    bool is_writeback_step = false;
    bool frozen = false;
};

struct SimStats {
    uint64_t total_cycles = 0;
    uint64_t compute_port_cycles = 0;
    uint64_t mem_port_step_cycles = 0;
    uint64_t branch_stall_cycles = 0;
    uint64_t scoreboard_or_rename_stalls = 0;
    uint64_t mshr_full_stalls = 0;
    double utilization_pct = 0.0;
    int64_t computed_output = 0;
};

class PipeIMCSimulator {
public:
    SimConfig cfg;
    RenameUnit renamer;
    std::deque<OpTableEntry> op_table;
    static constexpr size_t OP_TABLE_CAPACITY = 16;

    int64_t sram_wordlines[64];

    ActiveJob port0_mem_job;
    ActiveJob port0_calc_job;
    ActiveJob port1_calc_job;

    struct MemVisitReq {
        int op_id;
        std::string label;
        PhaseType phase;
        int remaining_cycles;
    };
    std::vector<MemVisitReq> mem_visit_queue;

    SimStats stats;

    PipeIMCSimulator(const SimConfig& c) : cfg(c) {
        std::memset(sram_wordlines, 0, sizeof(sram_wordlines));
    }

    bool isInOrder() const { return cfg.mode == DesignMode::IN_ORDER_BASELINE; }

    bool numPortsIsOne() const {
        return cfg.mode == DesignMode::IN_ORDER_BASELINE ||
               cfg.mode == DesignMode::PIPE_1 ||
               cfg.mode == DesignMode::PIPE_1R;
    }

    bool useRenaming() const {
        return cfg.mode == DesignMode::PIPELINED_RENAMING_FIG3C ||
               cfg.mode == DesignMode::PIPELINED_RENAMING ||
               cfg.mode == DesignMode::FINE_GRAINED_ISSUE ||
               cfg.mode == DesignMode::PIPE_1R;
    }

    bool port0CanComputeWhenIdle() const {
        return cfg.mode == DesignMode::IN_ORDER_BASELINE ||
               cfg.mode == DesignMode::PIPELINED_RENAMING ||
               cfg.mode == DesignMode::FINE_GRAINED_ISSUE ||
               cfg.mode == DesignMode::PIPE_1 ||
               cfg.mode == DesignMode::PIPE_1R;
    }

    bool useFineGrained() const {
        return cfg.mode == DesignMode::FINE_GRAINED_ISSUE ||
               cfg.mode == DesignMode::PIPE_1 ||
               cfg.mode == DesignMode::PIPE_1R;
    }

    int coarseMemLatency() const {
        return T_ADDR_STEP + cfg.mem_visit_latency + T_WB_STEP;
    }

    static const char* phaseName(PhaseType phase) {
        switch (phase) {
            case PhaseType::LOAD_PHASE_1: return "load 1";
            case PhaseType::LOAD_PHASE_2: return "load 2";
            case PhaseType::CALC_PHASE: return "compute";
            case PhaseType::STORE_PHASE: return "store";
            case PhaseType::NONE: return "idle";
        }
        return "unknown";
    }

    static std::string describeJob(int op_id, const std::string& label,
                                   PhaseType phase, int remaining_cycles) {
        return "#" + std::to_string(op_id) + " " + label + " [" + phaseName(phase) +
               ", " + std::to_string(remaining_cycles) + " cycles left]";
    }

    void logCycleActivity(uint64_t cycle) const {
        std::string port0;
        if (port0_mem_job.phase != PhaseType::NONE) {
            port0 = "Memory: " + describeJob(port0_mem_job.op_id, port0_mem_job.label,
                                             port0_mem_job.phase, port0_mem_job.remaining_cycles);
        }
        if (port0_calc_job.phase != PhaseType::NONE) {
            if (!port0.empty()) port0 += "; ";
            port0 += port0_calc_job.frozen ? "Compute (frozen): " : "Compute: ";
            port0 += describeJob(port0_calc_job.op_id, port0_calc_job.label,
                                 port0_calc_job.phase, port0_calc_job.remaining_cycles);
        }
        if (port0.empty()) port0 = "IDLE";

        std::string port1 = port1_calc_job.phase == PhaseType::NONE
            ? "IDLE"
            : "Compute: " + describeJob(port1_calc_job.op_id, port1_calc_job.label,
                                        port1_calc_job.phase, port1_calc_job.remaining_cycles);

        std::string memory_visits;
        for (const auto& visit : mem_visit_queue) {
            if (!memory_visits.empty()) memory_visits += "; ";
            memory_visits += describeJob(visit.op_id, visit.label, visit.phase,
                                         visit.remaining_cycles);
        }
        if (memory_visits.empty()) memory_visits = "none";

        ExecutionLogger::get_instance().log_cycle_activity(cycle, port0, port1, memory_visits);
    }

    void executeFunctionalCalc(const OpTableEntry& O) {
        if (O.trace_op.is_branch) return;

        int64_t v1 = 0;
        int64_t v2 = 0;

        if (useRenaming()) {
            int s1 = (O.phys_load1_dest >= 0) ? O.phys_load1_dest : O.phys_rs1;
            int s2 = (O.phys_load2_dest >= 0) ? O.phys_load2_dest : O.phys_rs2;
            v1 = (s1 >= 0) ? sram_wordlines[s1] : 0;
            v2 = O.trace_op.has_imm ? O.trace_op.imm_val : ((s2 >= 0) ? sram_wordlines[s2] : 0);
        } else {
            v1 = O.trace_op.rs1_is_mem ? O.trace_op.mem_val1
                                       : ((O.trace_op.rs1 >= 0) ? sram_wordlines[O.trace_op.rs1] : 0);
            v2 = O.trace_op.has_imm    ? O.trace_op.imm_val
                                       : (O.trace_op.rs2_is_mem ? O.trace_op.mem_val2
                                                                : ((O.trace_op.rs2 >= 0) ? sram_wordlines[O.trace_op.rs2] : 0));
        }

        int64_t res = 0;
        switch (O.trace_op.op) {
            case OpCode::ADD: res = v1 + v2; break;
            case OpCode::SUB: res = v1 - v2; break;
            case OpCode::SLL: res = v1 << v2; break;
            case OpCode::MUL: res = v1 * v2; break;
            case OpCode::DIV: res = (v2 != 0) ? (v1 / v2) : 0; break;
            default: break;
        }

        if (useRenaming()) {
            if (O.phys_rd >= 0) sram_wordlines[O.phys_rd] = res;
        } else {
            if (O.trace_op.rd >= 0 && !O.trace_op.rd_is_mem) {
                sram_wordlines[O.trace_op.rd] = res;
            }
        }

        if (O.trace_op.rd_is_mem) {
            stats.computed_output = res;
        }
    }

    bool scoreboardCanEnterTable(const TraceOp& incoming) const {
        int my_dest = incoming.rd_is_mem ? -1 : incoming.rd;
        if (my_dest < 0) return true;

        for (const auto& older : op_table) {
            if (older.committed) continue;
            int older_dest = older.trace_op.rd_is_mem ? -1 : older.trace_op.rd;
            if (older_dest >= 0 && my_dest == older_dest) return false;
            if (older.calc != PhaseState::COMPLETED) {
                if (my_dest == older.trace_op.rs1 || my_dest == older.trace_op.rs2) return false;
            }
        }
        return true;
    }

    bool hasDataDependency(size_t idx, PhaseType P) const {
        const auto& O = op_table[idx];

        if (useRenaming()) {
            if (P == PhaseType::LOAD_PHASE_1) return (O.phys_rs1 >= 0 && !renamer.phys_ready[O.phys_rs1]);
            if (P == PhaseType::LOAD_PHASE_2) return (O.phys_rs2 >= 0 && !renamer.phys_ready[O.phys_rs2]);
            if (P == PhaseType::CALC_PHASE) {
                int s1 = (O.phys_load1_dest >= 0) ? O.phys_load1_dest : O.phys_rs1;
                int s2 = (O.phys_load2_dest >= 0) ? O.phys_load2_dest : O.phys_rs2;
                return !((s1 < 0 || renamer.phys_ready[s1]) && (s2 < 0 || renamer.phys_ready[s2]));
            }
            if (P == PhaseType::STORE_PHASE) {
                bool ptr_ok = O.scratch_regs.empty() || renamer.phys_ready[O.scratch_regs[0]];
                return !ptr_ok;
            }
            return false;
        } else {
            for (size_t j = 0; j < idx; ++j) {
                const auto& older = op_table[j];
                if (older.committed) continue;
                int older_dest = older.trace_op.rd_is_mem ? -1 : older.trace_op.rd;
                if (older_dest < 0) continue;

                if (P == PhaseType::LOAD_PHASE_1) {
                    if (O.trace_op.rs1 == older_dest) return true;
                } else if (P == PhaseType::LOAD_PHASE_2) {
                    if (O.trace_op.rs2 == older_dest) return true;
                } else if (P == PhaseType::CALC_PHASE) {
                    if (!O.trace_op.rs1_is_mem && O.trace_op.rs1 == older_dest) return true;
                    if (!O.trace_op.rs2_is_mem && O.trace_op.rs2 == older_dest) return true;
                } else if (P == PhaseType::STORE_PHASE) {
                    if (O.trace_op.rd == older_dest) return true;
                }
            }
            return false;
        }
    }

    void runDispatcher(uint64_t cycle) {
        if (isInOrder()) {
            if (op_table.empty() || port0_mem_job.phase != PhaseType::NONE || port0_calc_job.phase != PhaseType::NONE) return;
            auto& O = op_table[0];
            if (O.hasOnFlyPhase()) return;
            PhaseType P = O.nextPendingPhase();
            if (P == PhaseType::NONE) return;

            if (P == PhaseType::CALC_PHASE) {
                O.calc = PhaseState::ON_FLY;
                int lat = getCalcLatency(O.trace_op.op, cfg, false);
                port0_calc_job = {O.trace_op.id, O.trace_op.label, P, lat, false, false};
                LOG_CYCLE(cycle, "Dispatch", -1, O.trace_op.id, "Dispatched In-Order Calc Phase to Port 0");
            } else {
                if (P == PhaseType::LOAD_PHASE_1) O.load1 = PhaseState::ON_FLY;
                if (P == PhaseType::LOAD_PHASE_2) O.load2 = PhaseState::ON_FLY;
                if (P == PhaseType::STORE_PHASE)  O.store = PhaseState::ON_FLY;
                port0_mem_job = {O.trace_op.id, O.trace_op.label, P, coarseMemLatency(), false, false};
                LOG_CYCLE(cycle, "Dispatch", -1, O.trace_op.id, "Dispatched In-Order Memory Phase");
            }
            return;
        }

        if (useFineGrained() && port0_mem_job.phase == PhaseType::NONE) {
            for (auto& O : op_table) {
                if (O.load1_sub == MemSubStep::NEED_WB) {
                    O.load1_sub = MemSubStep::IN_WB;
                    port0_mem_job = {O.trace_op.id, O.trace_op.label, PhaseType::LOAD_PHASE_1, T_WB_STEP, true, false};
                    if (port0_calc_job.phase != PhaseType::NONE) port0_calc_job.frozen = true;
                    LOG_CYCLE(cycle, "Dispatch", -1, O.trace_op.id, "Dispatched Writeback Step for Load 1");
                    return;
                }
                if (O.load2_sub == MemSubStep::NEED_WB) {
                    O.load2_sub = MemSubStep::IN_WB;
                    port0_mem_job = {O.trace_op.id, O.trace_op.label, PhaseType::LOAD_PHASE_2, T_WB_STEP, true, false};
                    if (port0_calc_job.phase != PhaseType::NONE) port0_calc_job.frozen = true;
                    LOG_CYCLE(cycle, "Dispatch", -1, O.trace_op.id, "Dispatched Writeback Step for Load 2");
                    return;
                }
                if (O.store_sub == MemSubStep::NEED_WB) {
                    O.store_sub = MemSubStep::IN_WB;
                    port0_mem_job = {O.trace_op.id, O.trace_op.label, PhaseType::STORE_PHASE, T_WB_STEP, true, false};
                    if (port0_calc_job.phase != PhaseType::NONE) port0_calc_job.frozen = true;
                    LOG_CYCLE(cycle, "Dispatch", -1, O.trace_op.id, "Dispatched Writeback Step for Store");
                    return;
                }
            }
        }

        bool mshr_available = !useFineGrained() || (mem_visit_queue.size() < cfg.max_mshrs);
        if (!mshr_available && port0_mem_job.phase == PhaseType::NONE) {
            stats.mshr_full_stalls++;
        }

        bool p0_can_mem = (port0_mem_job.phase == PhaseType::NONE) && mshr_available &&
                          (useFineGrained() || port0_calc_job.phase == PhaseType::NONE);
        bool p0_can_calc = port0CanComputeWhenIdle() &&
                           (port0_mem_job.phase == PhaseType::NONE) &&
                           (port0_calc_job.phase == PhaseType::NONE);
        bool p1_can_calc = !numPortsIsOne() && (port1_calc_job.phase == PhaseType::NONE);

        if (!p0_can_mem && !p0_can_calc && !p1_can_calc) return;

        int chosen_idx = -1;
        PhaseType schedulePhase = PhaseType::NONE;

        for (size_t i = 0; i < op_table.size(); ++i) {
            auto& O = op_table[i];
            bool specialPhase = false;

            if (O.hasOnFlyPhase()) continue;
            PhaseType P = O.nextPendingPhase();
            if (P == PhaseType::NONE) continue;

            if (P != PhaseType::CALC_PHASE) {
                if (!p0_can_mem) continue;
                specialPhase = true;
            } else {
                if (!p0_can_calc && !p1_can_calc) continue;
            }

            if (O.trace_op.is_branch) {
                if (cfg.mode == DesignMode::PIPELINED && i != 0) break;
                specialPhase = true;
            }

            if (!hasDataDependency(i, P)) {
                if (specialPhase) {
                    chosen_idx = static_cast<int>(i);
                    schedulePhase = P;
                    break;
                }
                if (schedulePhase == PhaseType::NONE) {
                    chosen_idx = static_cast<int>(i);
                    schedulePhase = P;
                }
            }
        }

        if (chosen_idx != -1) {
            auto& O = op_table[chosen_idx];
            if (schedulePhase != PhaseType::CALC_PHASE) {
                if (schedulePhase == PhaseType::LOAD_PHASE_1) { O.load1 = PhaseState::ON_FLY; O.load1_sub = MemSubStep::IN_ADDR; }
                if (schedulePhase == PhaseType::LOAD_PHASE_2) { O.load2 = PhaseState::ON_FLY; O.load2_sub = MemSubStep::IN_ADDR; }
                if (schedulePhase == PhaseType::STORE_PHASE)  { O.store = PhaseState::ON_FLY; O.store_sub = MemSubStep::IN_ADDR; }

                int dur = useFineGrained() ? T_ADDR_STEP : coarseMemLatency();
                port0_mem_job = {O.trace_op.id, O.trace_op.label, schedulePhase, dur, false, false};
                if (useFineGrained() && port0_calc_job.phase != PhaseType::NONE) {
                    port0_calc_job.frozen = true;
                }
                LOG_CYCLE(cycle, "Dispatch", -1, O.trace_op.id, "Dispatched Memory Phase to Port 0");
            } else {
                O.calc = PhaseState::ON_FLY;
                int lat = getCalcLatency(O.trace_op.op, cfg, useRenaming());
                if (p0_can_calc) {
                    port0_calc_job = {O.trace_op.id, O.trace_op.label, schedulePhase, lat, false, false};
                    LOG_CYCLE(cycle, "Dispatch", -1, O.trace_op.id, "Dispatched Calc Phase to Port 0");
                } else {
                    port1_calc_job = {O.trace_op.id, O.trace_op.label, schedulePhase, lat, false, false};
                    LOG_CYCLE(cycle, "Dispatch", -1, O.trace_op.id, "Dispatched Calc Phase to Port 1");
                }
            }
        }
    }

    void finishMemoryPhase(int op_id, PhaseType P, uint64_t cycle) {
        for (auto& O : op_table) {
            if (O.trace_op.id == op_id) {
                if (P == PhaseType::LOAD_PHASE_1) {
                    O.load1 = PhaseState::COMPLETED; O.load1_sub = MemSubStep::DONE;
                    if (useRenaming() && O.phys_load1_dest >= 0) {
                        sram_wordlines[O.phys_load1_dest] = O.trace_op.mem_val1;
                        renamer.phys_ready[O.phys_load1_dest] = true;
                    }
                } else if (P == PhaseType::LOAD_PHASE_2) {
                    O.load2 = PhaseState::COMPLETED; O.load2_sub = MemSubStep::DONE;
                    if (useRenaming() && O.phys_load2_dest >= 0) {
                        sram_wordlines[O.phys_load2_dest] = O.trace_op.mem_val2;
                        renamer.phys_ready[O.phys_load2_dest] = true;
                    }
                } else if (P == PhaseType::STORE_PHASE) {
                    O.store = PhaseState::COMPLETED; O.store_sub = MemSubStep::DONE;
                    if (useRenaming() && O.phys_rd >= 0) {
                        stats.computed_output = sram_wordlines[O.phys_rd];
                    }
                }
                LOG_CYCLE(cycle, "Memory", -1, op_id, "Memory Phase Completed");
                break;
            }
        }
    }

    uint64_t runTrace(const std::vector<TraceOp>& trace) {
        renamer.reset();
        op_table.clear();
        mem_visit_queue.clear();
        port0_mem_job = {};
        port0_calc_job = {};
        port1_calc_job = {};
        std::memset(sram_wordlines, 0, sizeof(sram_wordlines));
        stats = {};

        size_t fetch_idx = 0;
        bool branch_stall = false;
        uint64_t watchdog_last_commit = 0;

        while (fetch_idx < trace.size() || !op_table.empty()) {
            if (stats.total_cycles - watchdog_last_commit > 100000) {
                throw std::runtime_error("Watchdog triggered: deadlock in pipeline!");
            }

            logCycleActivity(stats.total_cycles);

            // 1. Commit Stage
            for (auto& O : op_table) {
                if (!O.committed && O.allPhasesDone()) {
                    O.committed = true;
                    if (useRenaming()) renamer.commitOp(O, stats.total_cycles);
                    LOG_CYCLE(stats.total_cycles, "Commit", -1, O.trace_op.id, "Operation Committed and Evicted");
                    if (O.trace_op.is_branch) branch_stall = false;
                    watchdog_last_commit = stats.total_cycles;
                    break;
                }
            }
            while (!op_table.empty() && op_table.front().committed) {
                op_table.pop_front();
            }

            // 2. Advance Off-Array Memory Visiting Queue
            if (useFineGrained()) {
                for (auto it = mem_visit_queue.begin(); it != mem_visit_queue.end(); ) {
                    it->remaining_cycles--;
                    if (it->remaining_cycles <= 0) {
                        for (auto& O : op_table) {
                            if (O.trace_op.id == it->op_id) {
                                if (it->phase == PhaseType::LOAD_PHASE_1) O.load1_sub = MemSubStep::NEED_WB;
                                if (it->phase == PhaseType::LOAD_PHASE_2) O.load2_sub = MemSubStep::NEED_WB;
                                if (it->phase == PhaseType::STORE_PHASE)  O.store_sub = MemSubStep::NEED_WB;
                            }
                        }
                        it = mem_visit_queue.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            // 3. Advance Port 0
            if (port0_mem_job.phase != PhaseType::NONE) {
                port0_mem_job.remaining_cycles--;
                stats.mem_port_step_cycles++;
                if (port0_mem_job.remaining_cycles <= 0) {
                    if (useFineGrained() && !port0_mem_job.is_writeback_step) {
                        mem_visit_queue.push_back({port0_mem_job.op_id, port0_mem_job.label, port0_mem_job.phase, cfg.mem_visit_latency});
                        port0_mem_job = {};
                        if (port0_calc_job.phase != PhaseType::NONE) port0_calc_job.frozen = false;
                    } else {
                        finishMemoryPhase(port0_mem_job.op_id, port0_mem_job.phase, stats.total_cycles);
                        port0_mem_job = {};
                        if (port0_calc_job.phase != PhaseType::NONE) port0_calc_job.frozen = false;
                    }
                }
            } else if (port0_calc_job.phase != PhaseType::NONE && !port0_calc_job.frozen) {
                port0_calc_job.remaining_cycles--;
                stats.compute_port_cycles++;
                if (port0_calc_job.remaining_cycles <= 0) {
                    for (auto& O : op_table) {
                        if (O.trace_op.id == port0_calc_job.op_id) {
                            O.calc = PhaseState::COMPLETED;
                            executeFunctionalCalc(O);
                        }
                    }
                    LOG_CYCLE(stats.total_cycles, "Calc", -1, port0_calc_job.op_id, "Calc Phase Completed on Port 0");
                    port0_calc_job = {};
                }
            }

            // 4. Advance Port 1
            if (port1_calc_job.phase != PhaseType::NONE) {
                port1_calc_job.remaining_cycles--;
                stats.compute_port_cycles++;
                if (port1_calc_job.remaining_cycles <= 0) {
                    for (auto& O : op_table) {
                        if (O.trace_op.id == port1_calc_job.op_id) {
                            O.calc = PhaseState::COMPLETED;
                            executeFunctionalCalc(O);
                        }
                    }
                    LOG_CYCLE(stats.total_cycles, "Calc", -1, port1_calc_job.op_id, "Calc Phase Completed on Port 1");
                    port1_calc_job = {};
                }
            }

            // 5. Frontend Fetch & Rename / Scoreboard Check
            if (branch_stall) stats.branch_stall_cycles++;

            bool can_fetch = (fetch_idx < trace.size()) && !branch_stall && (op_table.size() < OP_TABLE_CAPACITY);
            if (isInOrder() && !op_table.empty()) can_fetch = false;

            if (can_fetch) {
                const auto& next_op = trace[fetch_idx];
                bool allowed = useRenaming() ? renamer.canRename(next_op)
                                             : (isInOrder() || scoreboardCanEnterTable(next_op));
                if (allowed) {
                    OpTableEntry entry;
                    entry.trace_op = next_op;
                    entry.load1 = next_op.rs1_is_mem ? PhaseState::PENDING : PhaseState::SKIPPED;
                    entry.load2 = next_op.rs2_is_mem ? PhaseState::PENDING : PhaseState::SKIPPED;
                    entry.calc  = PhaseState::PENDING;
                    entry.store = next_op.rd_is_mem  ? PhaseState::PENDING : PhaseState::SKIPPED;

                    if (useRenaming()) renamer.renameOp(entry, stats.total_cycles);
                    LOG_CYCLE(stats.total_cycles, "Fetch", -1, next_op.id, "Fetched into Operation Table");
                    
                    op_table.push_back(entry);
                    fetch_idx++;
                    if (next_op.is_branch) branch_stall = true;
                } else {
                    stats.scoreboard_or_rename_stalls++;
                }
            }

            // 6. Run Dispatcher
            runDispatcher(stats.total_cycles);
            stats.total_cycles++;
        }

        if (cfg.scale_hybrid4 && useRenaming()) {
            stats.total_cycles *= 2;
            stats.compute_port_cycles *= 2;
        }

        int num_ports = numPortsIsOne() ? 1 : 2;
        stats.utilization_pct = (100.0 * stats.compute_port_cycles) / (stats.total_cycles * num_ports);
        return stats.total_cycles;
    }
};

#endif // SIMULATOR_HPP