#ifndef RENAME_UNIT_HPP
#define RENAME_UNIT_HPP

#include "types.hpp"
#include <queue>
#include <stdexcept>

class RenameUnit {
public:
    static constexpr int NUM_ARCH = 32;
    static constexpr int NUM_PHYS = 64;

    int rat[NUM_ARCH];
    bool arch_flags[NUM_PHYS];
    int usage_counter[NUM_PHYS];
    bool phys_ready[NUM_PHYS];
    std::queue<int> free_list;

    RenameUnit() { reset(); }

    void reset() {
        while (!free_list.empty()) free_list.pop();
        for (int i = 0; i < NUM_ARCH; ++i) {
            rat[i] = i;
            arch_flags[i] = true;
            usage_counter[i] = 0;
            phys_ready[i] = true;
        }
        for (int i = NUM_ARCH; i < NUM_PHYS; ++i) {
            arch_flags[i] = false;
            usage_counter[i] = 0;
            phys_ready[i] = true;
            free_list.push(i);
        }
    }

    int allocPhysReg(bool is_arch) {
        if (free_list.empty()) {
            throw std::runtime_error("RenameUnit: Free list exhausted!");
        }
        int p = free_list.front();
        free_list.pop();
        arch_flags[p] = is_arch;
        phys_ready[p] = false;
        usage_counter[p]++; // FIX: Increment usage for the destination itself!
        return p;
    }

    void checkAndReturnToFreeList(int p) {
        if (p >= 0 && !arch_flags[p] && usage_counter[p] == 0) {
            phys_ready[p] = true;
            free_list.push(p);
        }
    }

    bool canRename(const TraceOp& op) const {
        int needed = 0;
        if (op.rs1_is_mem) needed++;
        if (op.rs2_is_mem) needed++;
        if (op.rd >= 0) needed++;
        if (op.op == OpCode::MUL || op.op == OpCode::DIV || op.has_imm) needed++;
        return static_cast<int>(free_list.size()) >= needed;
    }

    void renameOp(OpTableEntry& entry) {
        const auto& op = entry.trace_op;

        entry.phys_rs1 = (op.rs1 >= 0) ? rat[op.rs1] : -1;
        if (entry.phys_rs1 >= 0) usage_counter[entry.phys_rs1]++;

        entry.phys_rs2 = (op.rs2 >= 0) ? rat[op.rs2] : -1;
        if (entry.phys_rs2 >= 0) usage_counter[entry.phys_rs2]++;

        // If storing to memory, fetch the pointer register so we can check dependencies
        if (op.rd_is_mem && op.rd >= 0) {
            int ptr_reg = rat[op.rd];
            entry.scratch_regs.push_back(ptr_reg);
            usage_counter[ptr_reg]++;
        }

        if (op.rs1_is_mem) entry.phys_load1_dest = allocPhysReg(false);
        if (op.rs2_is_mem) entry.phys_load2_dest = allocPhysReg(false);

        if (op.rd >= 0) {
            if (op.rd_is_mem) {
                entry.phys_rd = allocPhysReg(false);
            } else {
                entry.old_phys_rd = rat[op.rd];
                arch_flags[entry.old_phys_rd] = false;
                checkAndReturnToFreeList(entry.old_phys_rd);

                entry.phys_rd = allocPhysReg(true);
                rat[op.rd] = entry.phys_rd;
            }
        }

        if (op.op == OpCode::MUL || op.op == OpCode::DIV || op.has_imm) {
            entry.scratch_regs.push_back(allocPhysReg(false));
        }
    }

    void commitOp(const OpTableEntry& entry) {
        if (entry.phys_rs1 >= 0) {
            usage_counter[entry.phys_rs1]--;
            checkAndReturnToFreeList(entry.phys_rs1);
        }
        if (entry.phys_rs2 >= 0) {
            usage_counter[entry.phys_rs2]--;
            checkAndReturnToFreeList(entry.phys_rs2);
        }
        if (entry.phys_load1_dest >= 0) {
            usage_counter[entry.phys_load1_dest]--;
            checkAndReturnToFreeList(entry.phys_load1_dest);
        }
        if (entry.phys_load2_dest >= 0) {
            usage_counter[entry.phys_load2_dest]--;
            checkAndReturnToFreeList(entry.phys_load2_dest);
        }
        for (int s : entry.scratch_regs) {
            usage_counter[s]--;
            checkAndReturnToFreeList(s);
        }
        if (entry.phys_rd >= 0) {
            phys_ready[entry.phys_rd] = true; // FIX: Broadcasts data ready, breaking the deadlock!
            usage_counter[entry.phys_rd]--;
            checkAndReturnToFreeList(entry.phys_rd);
        }
    }
};

#endif // RENAME_UNIT_HPP