#ifndef KERNEL_DATA_HPP
#define KERNEL_DATA_HPP

#include <cstdint>
#include <string>
#include <vector>

// Real data for one kernel trace: the values the operations compute on.
// The trace says WHICH memory word each load/store touches (InSramOp::load1_addr ...);
// this struct holds the memory contents, the starting register values and the expected answer.
struct KernelData {
    std::string name;
    std::string reference_workload_shape;
    std::string simulated_sample_shape;
    std::string sample_note;

    std::vector<int32_t> memory;        // word-addressed memory
    int32_t init_reg[32] = {};          // starting value of r0..r31

    size_t branch_errors = 0;           // real branch decisions that disagreed with the trace

    std::vector<std::string> input_lines;   // human-readable input values (printed in the log)

    // Expected final memory words, computed by plain C++ (not by the simulator)
    struct Expect {
        uint32_t addr;
        int32_t value;
        std::string label;
    };
    std::vector<Expect> expected;
};

#endif
