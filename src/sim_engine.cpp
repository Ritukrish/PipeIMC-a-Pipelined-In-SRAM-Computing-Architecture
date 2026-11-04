
#include "sim_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

constexpr size_t OP_TABLE_CAPACITY = 16;
constexpr int ARCH_REG_COUNT = 32;
constexpr int PHYS_REG_COUNT = 64;
constexpr int FIRST_FREE_PHYS_REG = 32;

struct TableEntry {
    InSramOp op;

    int phys_src1 = 0;
    int phys_src2 = 0;
    int phys_dest = 0;
    int old_phys_dest = 0;

    int loads_needed = 0;
    int loads_completed = 0;

    bool load_on_fly = false;

    bool calc_done = false;
    bool calc_on_fly = false;

    bool store_done = false;
    bool store_on_fly = false;
};

enum class EventType {
    LOAD_COMPLETE,
    CALC_COMPLETE,
    STORE_COMPLETE
};

struct PendingEvent {
    uint64_t completion_cycle = 0;
    uint64_t op_id = 0;
    EventType type = EventType::LOAD_COMPLETE;
};

// One memory phase at a time, using the shared memory port.
struct MemoryTask {
    bool active = false;

    uint64_t op_id = 0;
    EventType type = EventType::LOAD_COMPLETE;

    uint64_t visit_end_cycle = 0;
    uint64_t completion_cycle = 0;

    bool fine_grained = false;
    bool writeback_reserved = false;
};

// Check whether an operation can safely read/write architectural
// registers when explicit physical-register renaming is disabled.
bool has_register_hazard(
    const std::deque<TableEntry>& table,
    size_t current_index
) {
    const auto& current = table[current_index].op;

    for (size_t i = 0; i < current_index; ++i) {
        const auto& older = table[i].op;

        if (!older.has_calc)
            continue;

        if (table[i].calc_done)
            continue;

        // RAW: older operation must produce a source first.
        if (older.dest == current.src1 ||
            older.dest == current.src2) {
            return true;
        }

        // WAR: older operation must read before current overwrites.
        if (older.src1 == current.dest ||
            older.src2 == current.dest) {
            return true;
        }

        // WAW: older write must finish before current write.
        if (older.dest == current.dest) {
            return true;
        }
    }

    return false;
}

} // namespace


uint64_t SimEngine::run(
    const std::vector<InSramOp>& trace,
    SimMode mode,
    std::ostream* log_out
) {
    if (trace.empty())
        return 0;

    // ------------------------------------------------------------
    // 1. Validate input trace
    // ------------------------------------------------------------

    for (const auto& op : trace) {
        if (!op.valid_latencies()) {
            throw std::runtime_error(
                "Invalid latency in operation " +
                std::to_string(op.op_id)
            );
        }

        if (!op.valid_registers()) {
            throw std::runtime_error(
                "Invalid register index in operation " +
                std::to_string(op.op_id)
            );
        }
    }

    // ------------------------------------------------------------
    // 2. Simulation state
    // ------------------------------------------------------------

    uint64_t cycle = 0;
    size_t fetch_ptr = 0;

    std::deque<TableEntry> op_table;
    std::deque<int> free_list;

    // Architectural-to-physical register mapping.
    int rat[ARCH_REG_COUNT];

    // Physical register state.
    bool phys_ready[PHYS_REG_COUNT] = {};
    int usage_counter[PHYS_REG_COUNT] = {};
    bool pending_free[PHYS_REG_COUNT] = {};
    bool already_free[PHYS_REG_COUNT] = {};

    for (int r = 0; r < ARCH_REG_COUNT; ++r) {
        rat[r] = r;
        phys_ready[r] = true;
    }

    for (int r = FIRST_FREE_PHYS_REG;
         r < PHYS_REG_COUNT;
         ++r) {
        free_list.push_back(r);
        already_free[r] = true;
        phys_ready[r] = true;
    }

    // Port 0: memory port / fine-grained shared port.
    // Port 1: calculation port.
    uint64_t port0_busy_until = 0;
    uint64_t port1_busy_until = 0;

    MemoryTask memory_task;
    std::vector<PendingEvent> pending_events;

    bool warp_branch_stall = false;

    const bool uses_renaming =
        (mode == SimMode::RENAMING ||
         mode == SimMode::FINE_GRAINED);

    // ------------------------------------------------------------
    // 3. Helper: release a physical register safely
    // ------------------------------------------------------------

    auto release_physical_register = [&](int reg) {
        if (reg < FIRST_FREE_PHYS_REG ||
            reg >= PHYS_REG_COUNT) {
            return;
        }

        if (usage_counter[reg] != 0) {
            pending_free[reg] = true;
            return;
        }

        if (!already_free[reg]) {
            free_list.push_back(reg);
            already_free[reg] = true;
            pending_free[reg] = false;
        }
    };

    // ------------------------------------------------------------
    // 4. Helper: decrement physical source usage
    // ------------------------------------------------------------

    auto release_source_usage = [&](int reg) {
        if (reg <= 0 || reg >= PHYS_REG_COUNT)
            return;

        if (usage_counter[reg] > 0)
            usage_counter[reg]--;

        if (usage_counter[reg] == 0 && pending_free[reg])
            release_physical_register(reg);
    };

    // ------------------------------------------------------------
    // 5. Helper: process a completed memory phase
    // ------------------------------------------------------------

    auto complete_memory_phase = [&]() {
        if (!memory_task.active)
            return;

        TableEntry* completed_entry = nullptr;

        for (auto& entry : op_table) {
            if (entry.op.op_id == memory_task.op_id) {
                completed_entry = &entry;
                break;
            }
        }

        if (completed_entry != nullptr) {
            switch (memory_task.type) {

                case EventType::LOAD_COMPLETE:
                    completed_entry->load_on_fly = false;
                    completed_entry->loads_completed++;

                    if (log_out) {
                        *log_out
                            << "Cycle " << cycle
                            << " | [LOAD COMPLETE] Op "
                            << completed_entry->op.op_id
                            << " | Loads completed: "
                            << completed_entry->loads_completed
                            << "/"
                            << completed_entry->loads_needed
                            << "\n";
                    }
                    break;

                case EventType::STORE_COMPLETE:
                    completed_entry->store_on_fly = false;
                    completed_entry->store_done = true;

                    if (log_out) {
                        *log_out
                            << "Cycle " << cycle
                            << " | [STORE COMPLETE] Op "
                            << completed_entry->op.op_id
                            << "\n";
                    }
                    break;

                default:
                    break;
            }
        }

        memory_task.active = false;
    };

    // ------------------------------------------------------------
    // 6. Main simulation loop
    // ------------------------------------------------------------

    while (
        fetch_ptr < trace.size() ||
        !op_table.empty() ||
        !pending_events.empty() ||
        memory_task.active ||
        port0_busy_until > cycle ||
        port1_busy_until > cycle
    ) {
        cycle++;

        // ========================================================
        // STAGE 1: COMPLETE CALCULATION EVENTS
        // ========================================================

        for (auto it = pending_events.begin();
             it != pending_events.end();) {

            if (cycle < it->completion_cycle) {
                ++it;
                continue;
            }

            for (auto& entry : op_table) {
                if (entry.op.op_id != it->op_id)
                    continue;

                if (it->type == EventType::CALC_COMPLETE) {
                    entry.calc_on_fly = false;
                    entry.calc_done = true;

                    if (uses_renaming &&
                        entry.phys_dest > 0) {
                        phys_ready[entry.phys_dest] = true;
                    }

                    if (log_out) {
                        *log_out
                            << "Cycle " << cycle
                            << " | [CALC COMPLETE] Op "
                            << entry.op.op_id
                            << "\n";
                    }
                }

                break;
            }

            it = pending_events.erase(it);
        }

        // ========================================================
        // STAGE 2: MEMORY PHASE COMPLETION
        // ========================================================

        if (memory_task.active) {

            // In fine-grained mode, reserve Port 0 for the
            // writeback interval after memory visiting.
            if (
                memory_task.fine_grained &&
                !memory_task.writeback_reserved &&
                cycle >= memory_task.visit_end_cycle
            ) {
                port0_busy_until =
                    memory_task.completion_cycle;

                memory_task.writeback_reserved = true;

                if (log_out) {
                    *log_out
                        << "Cycle " << cycle
                        << " | [MEMORY WRITEBACK START] Op "
                        << memory_task.op_id
                        << "\n";
                }
            }

            if (cycle >= memory_task.completion_cycle) {
                complete_memory_phase();
            }
        }

        // ========================================================
        // STAGE 3: IN-ORDER COMMIT
        // ========================================================

        while (!op_table.empty()) {
            auto& head = op_table.front();

            const bool loads_finished =
                (head.loads_completed >= head.loads_needed);

            const bool calc_finished =
                (!head.op.has_calc || head.calc_done);

            const bool store_finished =
                (!head.op.has_store || head.store_done);

            if (!(loads_finished &&
                  calc_finished &&
                  store_finished)) {
                break;
            }

            if (log_out && head.op.op_id <= 10) {
                *log_out
                    << "Cycle " << cycle
                    << " | [COMMIT] Op "
                    << head.op.op_id
                    << " retired.\n";
            }

            if (uses_renaming) {
                // Release source references first.
                release_source_usage(head.phys_src1);
                release_source_usage(head.phys_src2);

                // The previous architectural mapping is no
                // longer needed by this committed instruction.
                release_physical_register(head.old_phys_dest);
            }

            if (head.op.affects_control_flow)
                warp_branch_stall = false;

            op_table.pop_front();
        }

        // ========================================================
        // STAGE 4: FETCH AND DECODE
        // ========================================================

        if (
            !warp_branch_stall &&
            fetch_ptr < trace.size() &&
            op_table.size() < OP_TABLE_CAPACITY
        ) {
            const auto& next_op = trace[fetch_ptr];

            TableEntry entry{};
            entry.op = next_op;

            entry.loads_needed =
                (next_op.has_load1 ? 1 : 0) +
                (next_op.has_load2 ? 1 : 0);

            entry.loads_completed = 0;

            entry.calc_done = !next_op.has_calc;
            entry.store_done = !next_op.has_store;

            bool can_fetch = true;

            if (uses_renaming) {
                const int src1 = next_op.src1;
                const int src2 = next_op.src2;
                const int dest = next_op.dest;

                entry.phys_src1 = rat[src1];
                entry.phys_src2 = rat[src2];

                entry.old_phys_dest = rat[dest];

                if (dest == 0) {
                    // x0 is immutable.
                    entry.phys_dest = 0;
                }
                else if (!free_list.empty()) {
                    const int new_phys = free_list.front();
                    free_list.pop_front();

                    already_free[new_phys] = false;

                    entry.phys_dest = new_phys;
                    rat[dest] = new_phys;

                    phys_ready[new_phys] = false;
                    pending_free[new_phys] = false;
                }
                else {
                    // Do not modify the RAT or source usage
                    // counters if allocation fails.
                    can_fetch = false;
                }

                if (can_fetch) {
                    if (entry.phys_src1 > 0)
                        usage_counter[entry.phys_src1]++;

                    if (entry.phys_src2 > 0)
                        usage_counter[entry.phys_src2]++;
                }
            }
            else {
                entry.phys_src1 = next_op.src1;
                entry.phys_src2 = next_op.src2;
                entry.phys_dest = next_op.dest;
                entry.old_phys_dest = next_op.dest;
            }

            if (can_fetch) {
                op_table.push_back(entry);

                if (next_op.affects_control_flow)
                    warp_branch_stall = true;

                fetch_ptr++;

                if (log_out) {
                    *log_out
                        << "Cycle " << cycle
                        << " | [FETCH] Op "
                        << next_op.op_id
                        << "\n";
                }
            }
        }

        // ========================================================
        // STAGE 5: DISPATCH
        // ========================================================

        for (size_t i = 0; i < op_table.size(); ++i) {
            auto& entry = op_table[i];

            // Baseline: only the oldest operation may execute.
            if (mode == SimMode::BASELINE && i > 0)
                break;

            // ----------------------------------------------------
            // A. MEMORY LOAD PHASE
            // ----------------------------------------------------

            if (
                entry.loads_completed < entry.loads_needed &&
                !entry.load_on_fly
            ) {
                if (
                    !memory_task.active &&
                    cycle >= port0_busy_until
                ) {
                    const int addr =
                        std::max(1, entry.op.addr_cycles);

                    const int visit =
                        std::max(1, entry.op.visit_cycles);

                    const int writeback =
                        std::max(1, entry.op.writeback_cycles);

                    const int total_latency =
                        addr + visit + writeback;

                    entry.load_on_fly = true;

                    memory_task.active = true;
                    memory_task.op_id = entry.op.op_id;
                    memory_task.type = EventType::LOAD_COMPLETE;

                    memory_task.fine_grained =
                        (mode == SimMode::FINE_GRAINED);

                    memory_task.visit_end_cycle =
                        cycle + addr + visit;

                    memory_task.completion_cycle =
                        cycle + total_latency;

                    memory_task.writeback_reserved = false;

                    // Coarse-grained modes occupy the memory
                    // port for the complete memory phase.
                    if (mode != SimMode::FINE_GRAINED) {
                        port0_busy_until =
                            memory_task.completion_cycle;
                    }
                    else {
                        // Fine-grained mode initially occupies
                        // Port 0 only for address generation.
                        port0_busy_until = cycle + addr;
                    }

                    if (log_out) {
                        *log_out
                            << "Cycle " << cycle
                            << " | [LOAD START] Op "
                            << entry.op.op_id
                            << " | Address: " << addr
                            << " | Visit: " << visit
                            << " | Writeback: " << writeback
                            << "\n";
                    }

                    continue;
                }
            }

            // ----------------------------------------------------
            // B. CALCULATION PHASE
            // ----------------------------------------------------

            if (
                entry.op.has_calc &&
                entry.loads_completed >= entry.loads_needed &&
                !entry.calc_done &&
                !entry.calc_on_fly
            ) {
                bool sources_ready = true;

                if (uses_renaming) {
                    sources_ready =
                        phys_ready[entry.phys_src1] &&
                        phys_ready[entry.phys_src2];
                }
                else {
                    sources_ready =
                        !has_register_hazard(op_table, i);
                }

                if (!sources_ready)
                    continue;

                const uint64_t calc_latency =
                    static_cast<uint64_t>(
                        std::max(1, entry.op.calc_cycles)
                    );

                const uint64_t completion_cycle =
                    cycle + calc_latency;

                bool scheduled = false;
                int selected_port = -1;

                // -----------------------------------------------
                // Baseline: one shared port.
                // -----------------------------------------------

                if (mode == SimMode::BASELINE) {
                    if (
                        !memory_task.active &&
                        cycle >= port0_busy_until
                    ) {
                        selected_port = 0;
                        scheduled = true;
                    }
                }

                // -----------------------------------------------
                // Pipelined / Renaming:
                // Port 0 = memory
                // Port 1 = calculation
                // -----------------------------------------------

                else if (
                    mode == SimMode::PIPELINED ||
                    mode == SimMode::RENAMING
                ) {
                    if (cycle >= port1_busy_until) {
                        selected_port = 1;
                        scheduled = true;
                    }
                }

                // -----------------------------------------------
                // Fine-grained issue:
                // Prefer the calculation port.
                // Otherwise use Port 0 if it is available.
                // -----------------------------------------------

                else if (mode == SimMode::FINE_GRAINED) {
                    if (cycle >= port1_busy_until) {
                        selected_port = 1;
                        scheduled = true;
                    }
                    else if (cycle >= port0_busy_until) {
                        bool fits = true;

                        // During memory visiting, a calculation
                        // using Port 0 must finish before the
                        // reserved writeback interval.
                        if (
                            memory_task.active &&
                            memory_task.fine_grained
                        ) {
                            fits =
                                completion_cycle <=
                                memory_task.visit_end_cycle;
                        }

                        if (fits) {
                            selected_port = 0;
                            scheduled = true;
                        }
                    }
                }

                if (scheduled) {
                    entry.calc_on_fly = true;

                    if (selected_port == 0)
                        port0_busy_until = completion_cycle;
                    else
                        port1_busy_until = completion_cycle;

                    PendingEvent event;
                    event.completion_cycle = completion_cycle;
                    event.op_id = entry.op.op_id;
                    event.type = EventType::CALC_COMPLETE;

                    pending_events.push_back(event);

                    if (log_out) {
                        *log_out
                            << "Cycle " << cycle
                            << " | [CALC START] Op "
                            << entry.op.op_id
                            << " | Port " << selected_port
                            << " | Latency: "
                            << entry.op.calc_cycles
                            << "\n";
                    }

                    continue;
                }
            }

            // ----------------------------------------------------
            // C. MEMORY STORE PHASE
            // ----------------------------------------------------

            if (
                entry.op.has_store &&
                entry.loads_completed >= entry.loads_needed &&
                (!entry.op.has_calc || entry.calc_done) &&
                !entry.store_done &&
                !entry.store_on_fly
            ) {
                if (
                    !memory_task.active &&
                    cycle >= port0_busy_until
                ) {
                    const int addr =
                        std::max(1, entry.op.addr_cycles);

                    const int visit =
                        std::max(1, entry.op.visit_cycles);

                    const int writeback =
                        std::max(1, entry.op.writeback_cycles);

                    const int total_latency =
                        addr + visit + writeback;

                    entry.store_on_fly = true;

                    memory_task.active = true;
                    memory_task.op_id = entry.op.op_id;
                    memory_task.type = EventType::STORE_COMPLETE;

                    memory_task.fine_grained =
                        (mode == SimMode::FINE_GRAINED);

                    memory_task.visit_end_cycle =
                        cycle + addr + visit;

                    memory_task.completion_cycle =
                        cycle + total_latency;

                    memory_task.writeback_reserved = false;

                    if (mode != SimMode::FINE_GRAINED) {
                        port0_busy_until =
                            memory_task.completion_cycle;
                    }
                    else {
                        port0_busy_until = cycle + addr;
                    }

                    if (log_out) {
                        *log_out
                            << "Cycle " << cycle
                            << " | [STORE START] Op "
                            << entry.op.op_id
                            << " | Address: " << addr
                            << " | Visit: " << visit
                            << " | Writeback: " << writeback
                            << "\n";
                    }

                    continue;
                }
            }
        }
    }

    // ------------------------------------------------------------
    // 7. Final statistics
    // ------------------------------------------------------------

    if (log_out) {
        *log_out
            << "\n====================================\n"
            << "SIMULATION COMPLETED\n"
            << "Total Cycles: " << cycle
            << "\n====================================\n";
    }

    return cycle;
}
