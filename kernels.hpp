#ifndef KERNELS_HPP
#define KERNELS_HPP

#include "types.hpp"
#include <vector>

std::vector<TraceOp> getMatmulOpTable(int num_iterations = 256, int64_t* golden_out = nullptr);
std::vector<TraceOp> getStencilOpTable(int num_iterations = 256, int64_t* golden_out = nullptr);
std::vector<TraceOp> getKmeansOpTable(int num_iterations = 256, int64_t* golden_out = nullptr);

#endif // KERNELS_HPP