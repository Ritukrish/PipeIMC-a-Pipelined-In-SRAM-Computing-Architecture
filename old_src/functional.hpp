#ifndef FUNCTIONAL_HPP
#define FUNCTIONAL_HPP

#include "sim_engine.hpp"

#include <memory>
#include <ostream>
#include <string>
#include <vector>

// Execution-driven functional + timing simulation (one notion of time).
//
// The kernel is a program of in-SRAM (IMC) operations. The timing engine fetches an operation, spends the
// Table III / memory-phase cycles on it, and when those cycles have elapsed the real ALU operation is computed
// (real data, real memory, real branch decision). The result is checked against a plain C++ reference.
class FuncSim {
public:
    FuncSim(const std::string& kernel, int n);   // kernel: matmul | stencil | kmeans
    ~FuncSim();

    // Run all output elements through the engine with this design. Element 0 is the timed/logged run
    // (all elements have the same timing). Returns the cycles of element 0.
    uint64_t run(SimMode mode, std::ostream* log);

    size_t ops_per_element() const { return ops_; }
    bool all_pass() const;
    std::string listing() const;                 // the IMC program
    std::string report() const;                  // functional check of every design that was run

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    size_t ops_ = 0;
};

#endif
