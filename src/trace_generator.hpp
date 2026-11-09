
#ifndef TRACE_GENERATOR_HPP
#define TRACE_GENERATOR_HPP

#include "sim_op.hpp"
#include "kernel_data.hpp"

#include <cstdint>
#include <vector>

// Builds one operation with the PipeIMC latencies
InSramOp make_op(uint64_t id, OpType type, int src1, int src2, int dest,
                 bool load1, bool load2, bool calc, bool store, bool branch = false);

class TraceGenerator {
public:

    // Generate matrix multiplication operations
    static std::vector<InSramOp> generate_matmul(
        int output_dimension,
        KernelData* data = nullptr,
        int source_dimension = 0,
        int reduction_iterations = 0);

    // Generate stencil operations
    static std::vector<InSramOp> generate_stencil(int iterations, KernelData* data = nullptr);

    // Generate a bounded sample of a 3D stencil workload.
    static std::vector<InSramOp> generate_stencil3d_sample(
        int sample_points, int volume_dimension, KernelData* data = nullptr);

    // Generate k-means operations
    static std::vector<InSramOp> generate_kmeans(int iterations, KernelData* data = nullptr);

    // Generate a bounded sample containing several points and all their features.
    static std::vector<InSramOp> generate_kmeans_sample(
        int sample_points, int features, KernelData* data = nullptr);

    // Generate bounded samples of the paper's matvec and backprop workloads.
    static std::vector<InSramOp> generate_matvec_sample(
        int sample_dimension, int matrix_dimension, KernelData* data = nullptr);
    static std::vector<InSramOp> generate_backprop_sample(
        int sample_elements, KernelData* data = nullptr);
};

#endif // TRACE_GENERATOR_HPP
