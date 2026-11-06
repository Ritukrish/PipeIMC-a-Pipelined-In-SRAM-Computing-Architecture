
## Dispatch Phase 

If the SimMode is baseline, loops break after 1st instruction (for instruction at i==0 in op_table), and doesn't even go to check 'try_issue_load' 'try_store_load' 'try_calc' for next operations. This ensure sequential inorder execution where no later instruction can issue until the oldest one finishes.
The control goes back to the main simulation loop
For other modes : it can test for next instructions.

## start_memory_phase
`start_memory_phase` is a shared utility function used by both **Loads** and **Stores** to launch a memory transaction on the processor's memory port (`Port 0`).

Instead of treating memory access as an instant event, the simulator breaks down every memory operation into a realistic three-stage pipeline: **Address Generation** $\rightarrow$ **Memory Visit (Access)** $\rightarrow$ **Writeback**.

---

### Step-by-Step Breakdown of the Code

#### 1. Latency Calculation

```cpp
const int addr = std::max(1, entry.op.addr_cycles);
const int visit = std::max(1, entry.op.visit_cycles);
const int writeback = std::max(1, entry.op.writeback_cycles);
const int total_latency = addr + visit + writeback;

```

It reads the configuration parameters for the instruction (`entry.op`) to find out how many clock cycles each sub-phase takes. `std::max(1, ...)` ensures that every phase takes at least 1 cycle. It then sums them up to get the `total_latency`.

#### 2. Setting In-Flight Flags

```cpp
if (is_load)
    entry.load_on_fly = true;
else
    entry.store_on_fly = true;

```

This acts as a safety lock on the individual instruction (`TableEntry`). By setting `load_on_fly` or `store_on_fly` to `true`, it tells the dispatcher: *"This instruction's memory phase is already running; do not try to issue another load/store for it next cycle."*

#### 3. Configuring the Global `memory_task` Struct

```cpp
memory_task.active = true;
memory_task.op_id = entry.op.op_id;
memory_task.type = type;
memory_task.fine_grained = (mode == SimMode::FINE_GRAINED);
memory_task.visit_end_cycle = cycle + addr + visit;
memory_task.completion_cycle = cycle + total_latency;
memory_task.writeback_reserved = false;

```

The engine maintains a global `memory_task` tracker representing what Port 0 is currently doing. This block populates it with:

* The active instruction ID and whether it's a Load or Store.
* Future milestone targets, such as when the visit phase ends (`visit_end_cycle`) and when the entire memory operation finishes (`completion_cycle`).

#### 4. Port Reservation & Simulation Modes (The Core Logic)

How long Port 0 remains locked (`port0_busy_until`) depends entirely on the simulation mode:

```cpp
if (mode != SimMode::FINE_GRAINED) {
    port0_busy_until = memory_task.completion_cycle;
}
else {
    port0_busy_until = cycle + addr;
}

```

* **Coarse-Grained Modes (Baseline, Pipelined, Renaming):** The memory port is locked for the **entire duration** of the transaction (`completion_cycle`). No other memory operations can use Port 0 until this one completely finishes.
* **Fine-Grained Mode:** Port 0 is locked **only during address generation** (`cycle + addr`). Once the address is generated, Port 0 is released early, allowing calculations to overlap while the memory unit handles the longer visit/access phase in the background.

#### 5. Execution Logging

```cpp
if (log_out) {
    *log_out << "Cycle " << cycle << ...
}

```

If a log stream is provided, it prints a clean debug record showing the exact cycle, whether it's a load or store start, and the cycle breakdown for address generation, visit, and writeback.