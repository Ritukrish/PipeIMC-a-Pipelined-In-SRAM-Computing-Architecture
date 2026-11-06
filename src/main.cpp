
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <stdexcept>
#include <filesystem>

#include <sstream>
#include <memory>

#include "trace_generator.hpp"
#include "model_baseline.hpp"
#include "model_pipelined.hpp"
#include "model_renaming.hpp"
#include "model_fine_grained.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {

    // --------------------------------------------------
    // 1. Default configuration
    // --------------------------------------------------

    std::string kernel = "matmul";
    int iterations = 256;
    bool verbose = false;

    // --------------------------------------------------
    // 2. Argument parsing
    // --------------------------------------------------

    try {
        for (int i = 1; i < argc; ++i) {

            std::string arg = argv[i];

            if (arg == "--verbose" || arg == "-v") {
                verbose = true;
            }
            else if (arg == "--kernel") {

                if (i + 1 >= argc) {
                    throw std::invalid_argument(
                        "Missing value after --kernel"
                    );
                }

                kernel = argv[++i];
            }
            else if (arg == "--iterations") {

                if (i + 1 >= argc) {
                    throw std::invalid_argument(
                        "Missing value after --iterations"
                    );
                }

                std::string value = argv[++i];
                size_t pos = 0;

                int parsed = std::stoi(value, &pos);

                if (pos != value.size() || parsed <= 0) {
                    throw std::invalid_argument(
                        "Iterations must be a positive integer"
                    );
                }

                iterations = parsed;
            }
            else if (
                arg == "matmul" ||
                arg == "stencil" ||
                arg == "kmeans"
            ) {
                kernel = arg;
            }
            else {
                // Positional iteration support.
                size_t pos = 0;
                int parsed = std::stoi(arg, &pos);

                if (pos != arg.size() || parsed <= 0) {
                    throw std::invalid_argument(
                        "Unknown argument: " + arg
                    );
                }

                iterations = parsed;
            }
        }

        if (
            kernel != "matmul" &&
            kernel != "stencil" &&
            kernel != "kmeans"
        ) {
            throw std::invalid_argument(
                "Unsupported kernel: " + kernel
            );
        }
    }
    catch (const std::exception& e) {

        std::cerr
            << "Argument Error: "
            << e.what() << "\n\n";

        std::cerr
            << "Usage:\n"
            << "  pipeimc_sim [kernel] [iterations] [--verbose]\n"
            << "  pipeimc_sim --kernel matmul --iterations 256\n\n"
            << "Available kernels: matmul, stencil, kmeans\n";

        return 1;
    }

    // --------------------------------------------------
    // 3. Generate operation trace
    // --------------------------------------------------

    std::vector<InSramOp> trace;

    const std::string tag = kernel;

    // Real input data + memory addresses travel with the trace; the engine computes with them.
    KernelData data;

    if (kernel == "matmul") {
        trace = TraceGenerator::generate_matmul(iterations, &data);
    }
    else if (kernel == "stencil") {
        trace = TraceGenerator::generate_stencil(iterations, &data);
    }
    else if (kernel == "kmeans") {
        trace = TraceGenerator::generate_kmeans(iterations, &data);
    }

    if (trace.empty()) {

        std::cerr
            << "Error: Trace generation produced 0 operations.\n";

        return 1;
    }

    // --------------------------------------------------
    // 4. Create results directory
    // --------------------------------------------------

    const fs::path results_dir = "results";

    try {
        fs::create_directories(results_dir);
    }
    catch (const fs::filesystem_error& e) {

        std::cerr
            << "Error creating results directory: "
            << e.what() << "\n";

        return 1;
    }

    // --------------------------------------------------
    // 5. Create detailed cycle log
    // --------------------------------------------------

    const fs::path log_path =
        results_dir / ("pipeimc_cycle_" + tag + ".log");

    std::ofstream log_file(log_path);

    if (!log_file.is_open()) {

        std::cerr
            << "Error: Could not create log file: "
            << log_path.string() << "\n";

        return 1;
    }

    log_file
        << "============================================================\n"
        << "             PipeIMC Simulation Cycle Trace\n"
        << "============================================================\n"
        << "Kernel      : " << kernel << "\n"
        << "Iterations  : " << iterations << "\n"
        << "Operations  : " << trace.size() << "\n"
        << "============================================================\n\n";

    log_file << "WHAT IS COMPUTED: " << data.name << "\n\n"
             << "INPUT VALUES (signed 32-bit words read from memory by the load phases):\n";
    for (const auto& line : data.input_lines)
        log_file << "  " << line << "\n";
    log_file << "\nEach line below is one clock-cycle event. At [CALC COMPLETE] the real arithmetic is done\n"
             << "(operands => result); [LOAD COMPLETE] shows the value read; [STORE COMPLETE] the value written.\n\n";

    std::ostream* out_stream = &log_file;

    // --------------------------------------------------
    // 6. Display simulation configuration
    // --------------------------------------------------

    if (verbose) {

        std::cout
            << "\nStarting PipeIMC simulation...\n";

        std::cout
            << "Kernel      : " << kernel << "\n";

        std::cout
            << "Iterations  : " << iterations << "\n";

        std::cout
            << "Operations  : " << trace.size() << "\n";

        std::cout
            << "Log file    : " << log_path.string() << "\n\n";
    }

    // --------------------------------------------------
    // 7. Run all four architectural designs
    // --------------------------------------------------

    // Each design starts from the same initial memory/registers; its final memory is checked
    // against the expected values computed by plain C++ in the trace generator.
    struct Check { std::string design; bool pass; size_t ok, total; };
    std::vector<Check> checks;

    auto run_design = [&](const char* design, auto run_fn) -> uint64_t {
        KernelData d = data;                       // private copy of memory and registers
        log_file << "\n################ " << design << " ################\n";
        uint64_t cycles = run_fn(&d);
        log_file << "\n---------------- FINAL RESULT (" << design << ") ----------------\n";
        size_t ok = 0;
        for (const auto& e : d.expected) {
            const int32_t got = d.memory.at(e.addr);
            const bool same = (got == e.value);
            ok += same;
            if (d.expected.size() <= 64 || !same)
                log_file << "  " << e.label << " (mem[" << e.addr << "]): computed by simulator = " << got
                         << "   expected = " << e.value << (same ? "   OK" : "   MISMATCH") << "\n";
        }
        if (d.expected.size() > 64)
            log_file << "  (" << d.expected.size() << " outputs; only mismatches are listed)\n";
        const bool pass = (ok == d.expected.size()) && d.branch_errors == 0;
        log_file << "  Result check: " << (pass ? "PASS" : "FAIL") << " ("
                 << ok << "/" << d.expected.size() << " outputs correct, "
                 << d.branch_errors << " branch disagreements)   total cycles: " << cycles << "\n";
        checks.push_back({design, pass, ok, d.expected.size()});
        return cycles;
    };

    uint64_t c1 = run_design("Design 1: In-Order Baseline",
        [&](KernelData* d) { return ModelBaseline::run(trace, out_stream, d); });
    uint64_t c2 = run_design("Design 2: Pipelined",
        [&](KernelData* d) { return ModelPipelined::run(trace, out_stream, d); });
    uint64_t c3 = run_design("Design 3: Pipelined + Renaming",
        [&](KernelData* d) { return ModelRenaming::run(trace, out_stream, d); });
    uint64_t c4 = run_design("Design 4: Fine-Grained Issue",
        [&](KernelData* d) { return ModelFineGrained::run(trace, out_stream, d); });

    // --------------------------------------------------
    // 8. Close detailed log file
    // --------------------------------------------------

    log_file.close();

    // --------------------------------------------------
    // 9. Calculate speedups
    // --------------------------------------------------

    double s2 =
        (c2 > 0) ? static_cast<double>(c1) / c2 : 0.0;

    double s3 =
        (c3 > 0) ? static_cast<double>(c1) / c3 : 0.0;

    double s4 =
        (c4 > 0) ? static_cast<double>(c1) / c4 : 0.0;

    // --------------------------------------------------
    // 10. Display simulation results (also saved to a summary log)
    // --------------------------------------------------

    std::ostringstream rep;

    rep
        << "\n========================================================\n"
        << "       PipeIMC Simulator Results\n"
        << "========================================================\n";

    rep
        << "Kernel           : " << kernel << "\n";

    rep
        << "Iterations       : " << iterations << "\n";

    rep
        << "Total Operations : " << trace.size() << "\n";

    rep
        << "--------------------------------------------------------\n";

    rep
        << std::left
        << std::setw(36)
        << "Architectural Design"
        << std::setw(16)
        << "Cycles"
        << "Speedup\n";

    rep
        << "--------------------------------------------------------\n";

    rep
        << std::left
        << std::setw(36)
        << "Design 1: In-Order Baseline"
        << std::setw(16)
        << c1
        << "1.00x\n";

    rep
        << std::left
        << std::setw(36)
        << "Design 2: Pipelined"
        << std::setw(16)
        << c2
        << std::fixed << std::setprecision(2)
        << s2 << "x\n";

    rep
        << std::left
        << std::setw(36)
        << "Design 3: Pipelined + Renaming"
        << std::setw(16)
        << c3
        << s3 << "x\n";

    rep
        << std::left
        << std::setw(36)
        << "Design 4: Fine-Grained Issue"
        << std::setw(16)
        << c4
        << s4 << "x\n";

    rep
        << "========================================================\n";

    rep << "Result check (simulated arithmetic vs plain C++ reference):\n";
    for (const auto& c : checks)
        rep << "  " << c.design << ": " << (c.pass ? "PASS" : "FAIL") << " (" << c.ok << "/" << c.total << ")\n";
    rep << "========================================================\n";

    std::cout << rep.str();

    {
        std::ofstream summary_log("results/pipeimc_summary_" + tag + ".log");
        summary_log << rep.str();
    }

    // --------------------------------------------------
    // 11. Display log location
    // --------------------------------------------------

    std::cout
        << "Detailed log saved to: "
        << log_path.string() << "\n";

    for (const auto& c : checks)
        if (!c.pass) return 1;
    return 0;
}
