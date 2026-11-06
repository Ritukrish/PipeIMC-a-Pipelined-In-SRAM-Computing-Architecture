
#ifndef TRACE_GENERATOR_HPP
#define TRACE_GENERATOR_HPP

#include "sim_op.hpp"
#include "kernel_data.hpp"

#include <cstdint>
#include <vector>

// Builds one operation with the PipeIMC latencies (also used by the functional execution)
InSramOp make_op(uint64_t id, OpType type, int src1, int src2, int dest,
                 bool load1, bool load2, bool calc, bool store, bool branch = false);

class TraceGenerator {
public:

    // Generate matrix multiplication operations
    static std::vector<InSramOp> generate_matmul(int iterations, KernelData* data = nullptr);

    // Generate stencil operations
    static std::vector<InSramOp> generate_stencil(int iterations, KernelData* data = nullptr);

    // Generate k-means operations
    static std::vector<InSramOp> generate_kmeans(int iterations, KernelData* data = nullptr);
};

#endif // TRACE_GENERATOR_HPP
