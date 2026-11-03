
#ifndef TRACE_GENERATOR_HPP
#define TRACE_GENERATOR_HPP

#include "sim_op.hpp"

#include <vector>

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
