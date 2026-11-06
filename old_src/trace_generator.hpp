
#ifndef TRACE_GENERATOR_HPP
#define TRACE_GENERATOR_HPP

#include "sim_op.hpp"

#include <cstdint>
#include <vector>

// Builds one operation with the PipeIMC latencies (also used by the functional execution)
InSramOp make_op(uint64_t id, OpType type, int src1, int src2, int dest,
                 bool load1, bool load2, bool calc, bool store, bool branch = false);

class TraceGenerator {
public:

    // Generate matrix multiplication operations
    static std::vector<InSramOp> generate_matmul(int iterations);

    // Generate stencil operations
    static std::vector<InSramOp> generate_stencil(int iterations);

    // Generate k-means operations
    static std::vector<InSramOp> generate_kmeans(int iterations);
};

#endif // TRACE_GENERATOR_HPP
