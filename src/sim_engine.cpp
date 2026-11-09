// ============================================================
// PipeIMC timing + execution engine
// ============================================================
//
// One engine runs all four designs; SimMode only changes the rules
// (how many ops may issue, which ports exist, renaming, fine-grained).
//
// Life of one operation (InSramOp built by make_op in the trace, carrying
// its REAL operand values: load1_val / load2_val / imm_val):
//
//   1. FETCH + RENAME   op enters the operation table (16 entries).
//                       Renaming modes map its registers to physical ones.
//   2. DISPATCH         each cycle every op tries its next phase:
//        a. LOAD phase(s)  on port 0:  address generation (2) ->
//                          memory visit (16) -> writeback into the array (2)
//        b. CALC phase     on the calc port, Table III latency
//                          (ADD 9, SUB 17, MUL 105, DIV 145, ...).
//                          When it completes, the ALU computes the real value.
//        c. STORE phase    on port 0: address generation -> memory visit
//                          (the value is written to memory) -> writeback
//   3. COMPLETE         finished calc / memory phases are marked done.
//   4. COMMIT           in program order, an op retires only when all its
//                       phases are done; renaming frees old registers here,
//                       and a retired branch lets fetch continue.
//
// ============================================================

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

    // real values carried by this operation
    int32_t ld1 = 0;      // value loaded by memory phase 1
    int32_t ld2 = 0;      // value loaded by memory phase 2
    int32_t result = 0;   // calculation result (written to memory by the store phase)
};

enum class EventType {
    LOAD_COMPLETE,
    CALC_COMPLETE,
    STORE_COMPLETE
};

struct PendingEvent { //tracs when calculation phase finishes  
    uint64_t completion_cycle = 0;
    uint64_t op_id = 0;
    EventType type = EventType::LOAD_COMPLETE;
};

// One memory phase at a time, using the shared memory port.
struct MemoryTask {
    bool active = false;

    uint64_t op_id = 0;
    EventType type = EventType::LOAD_COMPLETE;

    uint64_t addr_end_cycle = 0;      // address generation finished
    uint64_t visit_end_cycle = 0;     // memory visiting finished
    uint64_t completion_cycle = 0;    // writeback finished

    bool addr_logged = false;
    bool visit_logged = false;

    bool fine_grained = false;
    bool writeback_reserved = false;
};

const char* op_name(OpType t) {
    switch (t) {
        case OpType::ADD:     return "ADD";
        case OpType::SUB:     return "SUB";
        case OpType::MUL:     return "MUL";
        case OpType::DIV:     return "DIV";
        case OpType::LOGIC:   return "AND";
        case OpType::BRANCH:  return "BRANCH";
        case OpType::BARRIER: return "BARRIER";
    }
    return "?";
}

// Human-readable instruction, including the real values it carries.
std::string describe_op(const InSramOp& op) {
    std::string s = op_name(op.type);

    if (op.type == OpType::BRANCH) {
        s += " if r" + std::to_string(op.src1) + " != r" + std::to_string(op.src2);
        return s;
    }

    s += " r" + std::to_string(op.dest) + " = r" + std::to_string(op.src1) + ", ";
    s += op.has_imm ? "#" + std::to_string(op.imm_val) : "r" + std::to_string(op.src2);

    if (op.has_load1) {
        s += " | load1 mem[" + std::to_string(op.load1_addr) + "]";
        if (op.has_load_vals) s += "=" + std::to_string(op.load1_val);
    }
    if (op.has_load2) {
        s += " | load2 mem[" + std::to_string(op.load2_addr) + "]";
        if (op.has_load_vals) s += "=" + std::to_string(op.load2_val);
    }
    if (op.has_store)
        s += " | store mem[" + std::to_string(op.store_addr) + "]";

    s += " | calc " + std::to_string(op.calc_cycles) + " cycles";
    return s;
}

// The in-SRAM ALU: the arithmetic of one calculation phase.
int32_t alu(OpType type, int32_t a, int32_t b, const char*& sym) {
    const uint32_t ua = static_cast<uint32_t>(a);
    const uint32_t ub = static_cast<uint32_t>(b);

    switch (type) {
        case OpType::ADD:   sym = "+"; return static_cast<int32_t>(ua + ub);
        case OpType::SUB:   sym = "-"; return static_cast<int32_t>(ua - ub);
        case OpType::MUL:   sym = "*"; return static_cast<int32_t>(ua * ub);
        case OpType::DIV:   sym = "/"; return (b == 0) ? -1 : a / b;
        case OpType::LOGIC: sym = "&"; return static_cast<int32_t>(ua & ub);
        default:            sym = "?"; return 0;
    }
}

bool reads_src1(const InSramOp& op) {
    return op.type == OpType::BARRIER || !op.has_load1;
}

bool reads_src2(const InSramOp& op) {
    if (op.type == OpType::BRANCH)
        return true;
    if (op.type == OpType::BARRIER)
        return false;
    return !op.has_imm && !op.has_load2;
}

bool writes_dest(const InSramOp& op) {
    return op.has_calc &&
           op.type != OpType::BRANCH &&
           op.type != OpType::BARRIER &&
           op.dest != 0;
}

// Check whether an operation can safely access architectural registers
// when explicit physical-register renaming is disabled.
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

        const bool older_writes = writes_dest(older);
        const bool current_writes = writes_dest(current);
        const bool raw =
            older_writes &&
            ((reads_src1(current) && older.dest == current.src1) ||
             (reads_src2(current) && older.dest == current.src2));
        if (raw) {
            return true;
        }

        // WAR: older operation must read before current overwrites.
        const bool war =
            current_writes &&
            ((reads_src1(older) && older.src1 == current.dest) ||
             (reads_src2(older) && older.src2 == current.dest));
        if (war) {
            return true;
        }

        // WAW: older write must finish before current write.
        if (older_writes && current_writes &&
            older.dest == current.dest) {
            return true;
        }
    }

    return false;
}


// ============================================================
// Engine: simulation state + one member function per stage
// ============================================================

class Engine {
public:
    Engine(
        const std::vector<InSramOp>& trace,
        SimMode mode,
        std::ostream* log_out,
        KernelData* data
    );

    uint64_t run();

private:
    // ---- configuration ----
    const std::vector<InSramOp>& trace;
    const SimMode mode;
    std::ostream* const log_out;
    KernelData* const data;          // real values (nullptr = timing only)
    int32_t val[PHYS_REG_COUNT] = {};   // register VALUES, indexed by the register number the engine uses
    const bool uses_renaming;

    // ---- simulation state ----
    uint64_t cycle = 0;
    size_t fetch_ptr = 0;

    std::deque<TableEntry> op_table;
    std::deque<int> free_list; // The free_list holds available physical registers

    // Register Alias Table: maps architectural registers to physical ones.
    int rat[ARCH_REG_COUNT];

    // Physical register state.
    bool phys_ready[PHYS_REG_COUNT] = {};
    int usage_counter[PHYS_REG_COUNT] = {}; // tracks how many operations are reading a specific physical register so it isn't accidentally overwritten
    bool pending_free[PHYS_REG_COUNT] = {};
    bool already_free[PHYS_REG_COUNT] = {};

    // Port 0: memory port / fine-grained shared port.
    // Port 1: calculation port.
    uint64_t port0_busy_until = 0;
    uint64_t port1_busy_until = 0;

    MemoryTask memory_task;
    std::vector<PendingEvent> pending_events;

    bool warp_branch_stall = false;

    // ---- setup / loop control ----
    void validate_trace() const;
    void init_registers();
    bool has_work() const;

    // ---- register helpers ----
    void release_physical_register(int reg);
    void release_source_usage(int reg);

    // ---- the five per-cycle stages (executed in this order) ----
    void fetch_and_rename();           // STAGE 1
    void dispatch();                   // STAGE 2
    void complete_calculations();      // STAGE 3
    void complete_memory();            // STAGE 4
    void commit();                     // STAGE 5

    // ---- helpers of the stages ----
    void complete_memory_phase();
    std::string execute_alu(TableEntry& entry);
    void rename_operands(TableEntry& entry, const InSramOp& next_op, bool& can_fetch);

    // ---- helpers of dispatch (one per phase) ----
    bool try_issue_load(size_t i);
    bool try_issue_calc(size_t i);
    bool try_issue_store(size_t i);
    void start_memory_phase(TableEntry& entry, EventType type);
    bool pick_calc_port(uint64_t completion_cycle, int& selected_port) const;
    bool memory_phase_waiting() const;
};

Engine::Engine(
    const std::vector<InSramOp>& trace_,
    SimMode mode_,
    std::ostream* log_out_,
    KernelData* data_
)
    : trace(trace_),
      mode(mode_),
      log_out(log_out_),
      data(data_),
      uses_renaming(
          mode_ == SimMode::RENAMING ||
          mode_ == SimMode::FINE_GRAINED)
{
    validate_trace();
    init_registers();
}

// ------------------------------------------------------------
// Validate input trace
// ------------------------------------------------------------

void Engine::validate_trace() const {
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
}

// ------------------------------------------------------------
// Initial RAT, ready bits and free list
// ------------------------------------------------------------

void Engine::init_registers() {
    if (data)
        for (int r = 0; r < ARCH_REG_COUNT; ++r)
            val[r] = data->init_reg[r];

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
}

// ------------------------------------------------------------
// Main simulation loop
// ------------------------------------------------------------

bool Engine::has_work() const {
    return
        fetch_ptr < trace.size() ||
        !op_table.empty() ||
        !pending_events.empty() ||
        memory_task.active ||
        port0_busy_until > cycle ||
        port1_busy_until > cycle;
}

uint64_t Engine::run() {
    while (has_work()) {
        cycle++;

        fetch_and_rename();        // STAGE 1
        dispatch();                // STAGE 2
        complete_calculations();   // STAGE 3
        complete_memory();         // STAGE 4
        commit();                  // STAGE 5
    }

    // ------------------------------------------------------------
    // Final statistics
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

// ------------------------------------------------------------
// Register helpers
// ------------------------------------------------------------

// Release a physical register safely
void Engine::release_physical_register(int reg) {
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
}

// Decrement physical source usage
void Engine::release_source_usage(int reg) {
    if (reg <= 0 || reg >= PHYS_REG_COUNT)
        return;

    if (usage_counter[reg] > 0)
        usage_counter[reg]--;

    if (usage_counter[reg] == 0 && pending_free[reg])
        release_physical_register(reg);
}

// ============================================================
// STAGE 1: FETCH AND DECODE
// ============================================================

void Engine::fetch_and_rename() {
    if (
        !warp_branch_stall &&
        fetch_ptr < trace.size() &&
        op_table.size() < OP_TABLE_CAPACITY
    ) {
        const InSramOp next_op =
            trace[fetch_ptr];

        TableEntry entry{};
        entry.op = next_op;

        entry.loads_needed =
            (next_op.has_load1 ? 1 : 0) +
            (next_op.has_load2 ? 1 : 0);

        entry.loads_completed = 0;

        entry.calc_done = !next_op.has_calc;
        entry.store_done = !next_op.has_store;

        bool can_fetch = true;

        rename_operands(entry, next_op, can_fetch);

        if (can_fetch) {
            op_table.push_back(entry);

            if (next_op.affects_control_flow)
                warp_branch_stall = true;
                // If it's a branch/, the next instruction fetch will be stalled until the branch is resolved.
            fetch_ptr++;


            if (log_out) {
                *log_out
                    << "Cycle " << cycle
                    << " | [FETCH] Op "
                    << next_op.op_id
                    << " | " << describe_op(next_op);
                if (uses_renaming)
                    *log_out
                        << " | renamed: p" << entry.phys_dest
                        << " = f(p" << entry.phys_src1
                        << ", p" << entry.phys_src2 << ")";
                *log_out << "\n";
            }
        }
    }
}

// Register renaming (or plain architectural names without renaming)
void Engine::rename_operands(
    TableEntry& entry,
    const InSramOp& next_op,
    bool& can_fetch
) {
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
            if (reads_src1(next_op) && entry.phys_src1 > 0)
                usage_counter[entry.phys_src1]++;

            if (reads_src2(next_op) && entry.phys_src2 > 0)
                usage_counter[entry.phys_src2]++;
        }
    }
    else { // for baseline when use_renaming is false, the physical registers are the same as the architectural ones.
        entry.phys_src1 = next_op.src1;
        entry.phys_src2 = next_op.src2;
        entry.phys_dest = next_op.dest;
        entry.old_phys_dest = next_op.dest;
    }
}

// ============================================================
// STAGE 2: DISPATCH
// ============================================================

void Engine::dispatch() {
    for (size_t i = 0; i < op_table.size(); ++i) {

        // Baseline: only the oldest operation may execute.
        if (mode == SimMode::BASELINE && i > 0)
            break;

        // A. MEMORY LOAD PHASE
        if (try_issue_load(i))
            continue;

        // B. CALCULATION PHASE
        if (try_issue_calc(i))
            continue;

        // C. MEMORY STORE PHASE
        try_issue_store(i);
    }
}

// ----------------------------------------------------
// A. MEMORY LOAD PHASE
// ----------------------------------------------------
// It checks if this operation still needs to complete a memory load (loads_completed < loads_needed)
// AND that a load is not already actively running for this operation (!entry.load_on_fly)
// Returns true if the load was started.
bool Engine::try_issue_load(size_t i) {
    auto& entry = op_table[i];

    if (
        entry.loads_completed < entry.loads_needed &&
        !entry.load_on_fly
    ) {
        if (
        // if the memory unit is currently free (!memory_task.active) AND if Port 0 is no longer blocked by a previous task
            !memory_task.active &&
            cycle >= port0_busy_until
        ) {
            start_memory_phase(entry, EventType::LOAD_COMPLETE);
            return true;
        }
    }

    return false;
}

// ----------------------------------------------------
// C. MEMORY STORE PHASE
// ----------------------------------------------------
// Returns true if the store was started.
bool Engine::try_issue_store(size_t i) {
    auto& entry = op_table[i];

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
            start_memory_phase(entry, EventType::STORE_COMPLETE);
            return true;
        }
    }

    return false;
}

// Shared by load and store: one memory phase
// (address -> visit -> writeback) on the memory port.
void Engine::start_memory_phase(TableEntry& entry, EventType type) {
    const bool is_load = (type == EventType::LOAD_COMPLETE); // check if the memory phase is for store ?

    const int addr = std::max(1, entry.op.addr_cycles);
    const int visit = std::max(1, entry.op.visit_cycles);
    const int writeback = std::max(1, entry.op.writeback_cycles);

    const int total_latency = addr + visit + writeback;

    if (is_load)
        entry.load_on_fly = true;
    else
        entry.store_on_fly = true;

    // Configuring the Global memory_task Struct
    memory_task.active = true;
    memory_task.op_id = entry.op.op_id;
    memory_task.type = type;

    memory_task.fine_grained = (mode == SimMode::FINE_GRAINED);
    memory_task.addr_end_cycle = cycle + addr;
    memory_task.visit_end_cycle = cycle + addr + visit;
    memory_task.addr_logged = false;
    memory_task.visit_logged = false;
    memory_task.completion_cycle = cycle + total_latency;
    memory_task.writeback_reserved = false;

    // for baseline, pipelined and renaming architectures, the memory phase occupies the shared memory
    // port for the complete memory phase.
    if (mode != SimMode::FINE_GRAINED) {
        port0_busy_until =
            memory_task.completion_cycle;
    }
    else { // for FINE GRAIN ARCHITECTURE
        // Fine-grained mode initially occupies
        // Port 0 only for address generation.
        port0_busy_until = cycle + addr;
    }

    if (log_out) {
        *log_out
            << "Cycle " << cycle
            << (is_load ? " | [LOAD START] Op "
                        : " | [STORE START] Op ")
            << entry.op.op_id
            << " | Address: " << addr
            << " | Visit: " << visit
            << " | Writeback: " << writeback
            << "\n";
    }
}

// ----------------------------------------------------
// B. CALCULATION PHASE
// ----------------------------------------------------
// Returns true when this entry is finished for this cycle:
// either the calculation was started, or it is waiting on
// its source registers (in which case the store phase is
// not attempted either).
bool Engine::try_issue_calc(size_t i) {
    auto& entry = op_table[i];

    if (
        entry.op.has_calc && // needs calculaton
        entry.loads_completed >= entry.loads_needed && // all loads are completed
        !entry.calc_done && // calculation is not yet done
        !entry.calc_on_fly // calculation is not already running
    ) {
        bool sources_ready = true;

        if (uses_renaming) {
            sources_ready =
                (!reads_src1(entry.op) ||
                 phys_ready[entry.phys_src1]) &&
                (!reads_src2(entry.op) ||
                 phys_ready[entry.phys_src2]);
        }
        else {
            sources_ready =
                !has_register_hazard(op_table, i);
        }

        if (!sources_ready)
            return true;

        const uint64_t calc_latency =
            static_cast<uint64_t>(
                std::max(1, entry.op.calc_cycles)
            );

        const uint64_t completion_cycle =
            cycle + calc_latency;

        int selected_port = -1;

        const bool scheduled =
            pick_calc_port(completion_cycle, selected_port);

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

            return true;
        }
    }

    return false;
}

// Choose the port for a calculation according to the mode.
// Returns true (and sets selected_port) if one is available now.
bool Engine::pick_calc_port(
    uint64_t completion_cycle,
    int& selected_port
) const {
    bool scheduled = false;

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
            // With Port 0 idle, a calculation may borrow it only if no
            // memory phase is waiting for it; otherwise a long calculation
            // (e.g. a 105-cycle MUL) would block the next load/store and
            // make fine-grained issue slower than plain pipelining.
            else if (memory_phase_waiting()) {
                fits = false;
            }

            if (fits) {
                selected_port = 0;
                scheduled = true;
            }
        }
    }

    return scheduled;
}

// True if some operation in the table could start a memory phase
// (a load, or a store whose calculation is finished) right now.
bool Engine::memory_phase_waiting() const {
    for (const auto& e : op_table) {
        const bool loads_left = e.loads_completed < e.loads_needed;

        if (loads_left && !e.load_on_fly)
            return true;

        if (e.op.has_store && !loads_left &&
            (!e.op.has_calc || e.calc_done) &&
            !e.store_done && !e.store_on_fly)
            return true;
    }
    return false;
}

// ============================================================
// STAGE 3: COMPLETE CALCULATION EVENTS
// ============================================================

void Engine::complete_calculations() {
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

                // The REAL computation happens now, after the calculation cycles have elapsed.
                std::string detail;
                if (data)
                    detail = execute_alu(entry);

                if (log_out) {
                    *log_out
                        << "Cycle " << cycle
                        << " | [CALC COMPLETE] Op "
                        << entry.op.op_id
                        << (detail.empty() ? "" : " | " + detail)
                        << "\n";
                }
            }

            break;
        }

        it = pending_events.erase(it);
    }
}

// ------------------------------------------------------------
// The actual arithmetic of one operation (called when its calculation
// phase completes):  dest = src1 OP src2
//   * an operand that has a memory phase uses the value that phase loaded
//   * otherwise it is read from the register (value array, indexed by the
//     register number the engine uses: architectural, or physical with renaming)
//   * a store operation keeps its result until its store phase writes memory
// ------------------------------------------------------------
std::string Engine::execute_alu(TableEntry& entry) {
    const InSramOp& op = entry.op;

    if (op.type == OpType::BRANCH && op.real_branch) {
        // Real loop decision from the register values (e.g. k != n)
        const int32_t a = val[entry.phys_src1];
        const int32_t b = val[entry.phys_src2];
        const bool taken = (a != b);
        std::string msg = "branch: " + std::to_string(a) + " != " + std::to_string(b) +
                          (taken ? " => TAKEN" : " => not taken");
        if (taken != op.branch_taken) {
            ++data->branch_errors;
            msg += "   ** DISAGREES WITH TRACE **";
        }
        return msg;
    }

    if (op.type == OpType::BRANCH || op.type == OpType::BARRIER) {
        const int32_t v = val[entry.phys_src1];
        return "loop-control (no data): r" + std::to_string(op.src1) + " = " + std::to_string(v);
    }

    const int32_t a = op.has_load1 ? entry.ld1 : val[entry.phys_src1];
    const int32_t b = op.has_imm   ? op.imm_val
                    : op.has_load2 ? entry.ld2
                                   : val[entry.phys_src2];

    const char* sym = "?";
    const int32_t r = alu(op.type, a, b, sym);

    entry.result = r;
    if (entry.phys_dest > 0)
        val[entry.phys_dest] = r;

    return std::to_string(a) + " " + sym + " " + std::to_string(b) + " = " + std::to_string(r);
}

// ============================================================
// STAGE 4: MEMORY PHASE COMPLETION
// ============================================================

void Engine::complete_memory() {
    if (!memory_task.active)
        return;

    const char* kind =
        (memory_task.type == EventType::LOAD_COMPLETE) ? "LOAD" : "STORE";

    if (!memory_task.addr_logged && cycle >= memory_task.addr_end_cycle) {
        memory_task.addr_logged = true;
        if (log_out)
            *log_out << "Cycle " << cycle << " | [" << kind
                     << " ADDR-GEN DONE] Op " << memory_task.op_id << "\n";
    }

    if (!memory_task.visit_logged && cycle >= memory_task.visit_end_cycle) {
        memory_task.visit_logged = true;
        if (log_out)
            *log_out << "Cycle " << cycle << " | [" << kind
                     << " MEM-VISIT DONE] Op " << memory_task.op_id << "\n";
    }

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

// Process a completed memory phase
void Engine::complete_memory_phase() {
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

            case EventType::LOAD_COMPLETE: {
                completed_entry->load_on_fly = false;
                completed_entry->loads_completed++;

                std::string detail;
                if (data) {
                    // first finished load is load 1 if the op has one, otherwise load 2
                    const bool second =
                        !(completed_entry->op.has_load1 &&
                          completed_entry->loads_completed == 1);
                    const uint32_t addr = second ? completed_entry->op.load2_addr
                                                 : completed_entry->op.load1_addr;
                    const InSramOp& lop = completed_entry->op;
                    // the number comes with the operation itself (trace), or from memory
                    const int32_t value = lop.has_load_vals ? (second ? lop.load2_val : lop.load1_val)
                                                            : data->memory.at(addr);
                    (second ? completed_entry->ld2 : completed_entry->ld1) = value;
                    detail = "read mem[" + std::to_string(addr) + "] = " + std::to_string(value);
                }

                if (log_out) {
                    *log_out
                        << "Cycle " << cycle
                        << " | [LOAD COMPLETE] Op "
                        << completed_entry->op.op_id
                        << " | Loads completed: "
                        << completed_entry->loads_completed
                        << "/"
                        << completed_entry->loads_needed
                        << (detail.empty() ? "" : " | " + detail)
                        << "\n";
                }
                break;
            }

            case EventType::STORE_COMPLETE: {
                completed_entry->store_on_fly = false;
                completed_entry->store_done = true;

                std::string detail;
                if (data) {
                    const uint32_t addr = completed_entry->op.store_addr;
                    data->memory.at(addr) = completed_entry->result;
                    detail = "write mem[" + std::to_string(addr) + "] = " +
                             std::to_string(completed_entry->result);
                }

                if (log_out) {
                    *log_out
                        << "Cycle " << cycle
                        << " | [STORE COMPLETE] Op "
                        << completed_entry->op.op_id
                        << (detail.empty() ? "" : " | " + detail)
                        << "\n";
                }
                break;
            }

            default:
                break;
        }
    }

    memory_task.active = false;
}

// ============================================================
// STAGE 5: IN-ORDER COMMIT
// ============================================================

void Engine::commit() {
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

        if (log_out) {
            *log_out
                << "Cycle " << cycle
                << " | [COMMIT] Op "
                << head.op.op_id
                << " retired.\n";
        }

        if (uses_renaming) {
            // Release source references first.
            if (reads_src1(head.op))
                release_source_usage(head.phys_src1);
            if (reads_src2(head.op))
                release_source_usage(head.phys_src2);

            // The previous architectural mapping is no
            // longer needed by this committed instruction.
            release_physical_register(head.old_phys_dest);
        }

        if (head.op.affects_control_flow)
            warp_branch_stall = false;

        op_table.pop_front();
    }
}

} // namespace

// ============================================================
// Public entry point
// ============================================================

uint64_t SimEngine::run(
    const std::vector<InSramOp>& trace,
    SimMode mode,
    std::ostream* log_out,
    KernelData* data
) {
    if (trace.empty())
        return 0;

    Engine engine(trace, mode, log_out, data);
    return engine.run();
}
