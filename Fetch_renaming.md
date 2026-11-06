This code implements the **Fetch and Rename** stage (Stage 1) of a cycle-accurate hardware simulation engine. It handles fetching instructions from an execution trace, initializing their tracking metadata (`TableEntry`), and performing dynamic register renaming using a Register Alias Table (RAT) and a physical register free list.

---

### `Engine::fetch_and_rename()`

This function runs once per clock cycle to pull the next instruction into the instruction window (`op_table`), provided three front-end gates are met:

1. **`!warp_branch_stall`**: No unresolved control-flow instruction (branch) is currently in flight.
2. **`fetch_ptr < trace.size()`**: The instruction trace has not reached its end.
3. **`op_table.size() < OP_TABLE_CAPACITY`**: The active instruction table (e.g., capacity of 16 entries) has structural room.

#### Execution Flow

* **Bookkeeping Initialization:** A `TableEntry` is created from the trace instruction (`trace[fetch_ptr]`). It calculates load dependencies (`loads_needed`) and initializes completion flags (`calc_done`, `store_done`). Operations lacking a calculation or store phase begin with those phases pre-marked as completed.
* **Operand Renaming:** It invokes `rename_operands()`. If register renaming fails (due to an exhausted free list), `can_fetch` is set to `false`, stalling the fetch stage so it retries the same instruction on the next cycle.
* **Queue Insertion:** If `can_fetch` remains `true`, the entry is appended to `op_table`. If the instruction affects control flow, `warp_branch_stall` is triggered, freezing further instruction fetches until the branch resolves at commit. Finally, `fetch_ptr` is advanced.

---

### `Engine::rename_operands()`

This function manages how architectural registers (e.g., `r0`–`r31`) map to physical hardware registers, supporting both renamed (out-of-order style) and non-renamed execution models.

#### 1. When Renaming is Enabled (`uses_renaming = true`)

* **Source Mapping:** Architectural source registers (`src1`, `src2`) are translated to physical registers via the Register Alias Table (`rat[src1]`, `rat[src2]`), ensuring the instruction reads the most up-to-date producer values.
* **Destination Remapping & Allocation:**
* The previous physical register assigned to the destination is saved as `old_phys_dest` so it can be reclaimed later when the instruction commits.
* If the destination is architectural register `0` (`dest == 0`, such as the immutable zero register `x0` in RISC-V), it maps directly to physical register `0` with no allocation.
* For any other destination, a free physical register is popped from the `free_list`. The `rat` is updated to point to this new physical register, and its readiness flag (`phys_ready`) is set to `false` until the calculation finishes.


* **Structural Hazard Handling:** If the `free_list` is empty, allocation fails: `can_fetch` becomes `false`, leaving the RAT and resource counters untouched.
* **Reference Counting:** `usage_counter` is incremented for each source physical register to track active in-flight readers, preventing registers from being freed prematurely.

#### 2. When Renaming is Disabled (`uses_renaming = false`)

* Physical register identifiers simply mirror architectural ones (`phys_src1 = next_op.src1`), bypassing dynamic allocation and deferring hazard detection to later pipeline stages.

### EXAMPLE
The exact line where `next_op` and `entry` split apart and connect is right here in `fetch_and_rename()`:

```cpp
TableEntry entry{};
entry.op = next_op; // <-- Copying the static blueprint into the dynamic tracker

rename_operands(entry, next_op, can_fetch); // <-- Where renaming happens

```

`next_op` is just the raw text read from the file. `entry` is the active tracker that takes that `next_op` and adds runtime intelligence to it—most notably, **register renaming**.

---

### Step-by-Step Example: A Data Dependency

Imagine your trace has two back-to-back instructions:

1. **Op 1:** `r1 = r2 + r3` (Writes a new value into architectural register `r1`)
2. **Op 2:** `r4 = r1 + r5` (Reads from architectural register `r1`)

Here is how the code distinguishes between them and uses **register renaming** to handle the dependency.

#### Phase 1: Processing Op 1 (`r1 = r2 + r3`)

1. **The Fetch:**
`next_op` loads the blueprint: `dest = 1`.
2. **The Tracker Creation:**
`entry` is created. `entry.op = next_op;` copies the blueprint. At this point, `entry.phys_dest` is still blank.
3. **The Renaming (`rename_operands`):**
The code looks at `next_op.dest` (which is `1`).
* It checks the Register Alias Table: `rat[1]` points to whatever physical register `r1` currently uses (say, initial `phys 1`).
* It stores `entry.old_phys_dest = 1`.
* Since `dest` is not `0`, it grabs a fresh physical register from the free list—let's say **`phys 40`**.
* It updates the RAT so future instructions know `r1` is now at `40`: `rat[1] = 40`.
* It saves this inside `entry`: **`entry.phys_dest = 40`**.



#### Phase 2: Processing Op 2 (`r4 = r1 + r5`)

Later, the loop reaches Op 2.

1. **The Fetch:**
`next_op` loads the blueprint for Op 2. Its `src1 = 1` (because it wants to read architectural register `r1`).
2. **The Tracker Creation & Renaming:**
A new `entry` is created for Op 2. `rename_operands` runs on it:
* It looks at `next_op.src1` (which is `1`).
* It queries the RAT: **`entry.phys_src1 = rat[1];`**
* Because Op 1 just updated `rat[1]` to **`40`**, `entry.phys_src1` becomes **`40`**.



---

### Why this proves they are different:

Look at what `next_op` says versus what `entry` contains for Op 2:

* **`next_op` (Static Blueprint):** Still says `"Read from architectural register `src1 = 1`"`. If you printed `next_op` 100 times, it would always say `1`.
* **`entry` (Dynamic Tracker):** Says `"Read from physical register `phys_src1 = 40`"`.

Without `entry` holding its own dynamic state (`phys_src1` and `phys_dest`), the processor would only see architectural names (`r1`) and wouldn't know which specific version of `r1` to read from or write to when instructions overlap.