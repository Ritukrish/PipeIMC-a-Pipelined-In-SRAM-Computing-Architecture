
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <stdexcept>
#include <filesystem>
#include <chrono>
#include <limits>

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
    int iterations = 0;
    int reduction_iterations = 256;
    bool iterations_specified = false;
    bool verbose = false;
    bool paper_sample = false;
    bool summary_only = false;

    // --------------------------------------------------
    // 2. Argument parsing
    // --------------------------------------------------

    try {
        for (int i = 1; i < argc; ++i) {

            std::string arg = argv[i];

            if (arg == "--verbose" || arg == "-v") {
                verbose = true;
            }
            else if (arg == "--paper-sample") {
                paper_sample = true;
            }
            else if (arg == "--summary-only") {
                summary_only = true;
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
                iterations_specified = true;
            }
            else if (arg == "--reduction-iterations") {
                if (i + 1 >= argc) {
                    throw std::invalid_argument(
                        "Missing value after --reduction-iterations"
                    );
                }

                std::string value = argv[++i];
                size_t pos = 0;
                const int parsed = std::stoi(value, &pos);
                if (pos != value.size() || parsed <= 0) {
                    throw std::invalid_argument(
                        "Reduction iterations must be a positive integer"
                    );
                }
                reduction_iterations = parsed;
            }
            else if (
                arg == "matmul" ||
                arg == "stencil" ||
                arg == "kmeans" ||
                arg == "matvec" ||
                arg == "backprop"
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
                iterations_specified = true;
            }
        }

        if (
            kernel != "matmul" &&
            kernel != "stencil" &&
            kernel != "kmeans" &&
            kernel != "matvec" &&
            kernel != "backprop"
        ) {
            throw std::invalid_argument(
                "Unsupported kernel: " + kernel
            );
        }

        if (!iterations_specified) {
            if (kernel == "matmul")
                iterations = 8;
            else
                iterations = 256;
        }
    }
    catch (const std::exception& e) {

        std::cerr
            << "Argument Error: "
            << e.what() << "\n\n";

        std::cerr
            << "Usage:\n"
            << "  pipeimc_sim [kernel] [iterations] [--verbose]\n"
            << "  pipeimc_sim matmul [output-tile-N] --reduction-iterations 256\n"
            << "  pipeimc_sim [kernel] --paper-sample\n\n"
            << "Available kernels: matmul, stencil, kmeans, matvec, backprop\n"
            << "Add --summary-only to suppress per-cycle event logs.\n";

        return 1;
    }

    // --------------------------------------------------
    // 3. Generate operation trace
    // --------------------------------------------------

    std::vector<InSramOp> trace;

    const std::string tag = kernel;
    int matmul_output_dimension = kernel == "matmul" ? iterations : 0;

    // Real input data + memory addresses travel with the trace; the engine computes with them.
    KernelData data;

    if (paper_sample) {
        if (kernel == "matmul") {
            iterations = 16;
            matmul_output_dimension = iterations;
            trace = TraceGenerator::generate_matmul(
                matmul_output_dimension, &data, 512, reduction_iterations
            );
            data.reference_workload_shape = "512 x 512 matrices";
            data.simulated_sample_shape =
                "first 16 x 16 output tile, 256 reduction iterations";
            data.sample_note =
                "Top-left tile uses the 512-element source row stride; this is not a full matrix run.";
            iterations = reduction_iterations;
        }
        else if (kernel == "stencil") {
            iterations = 256;
            trace = TraceGenerator::generate_stencil3d_sample(
                iterations * 4, 128, &data
            );
        }
        else if (kernel == "kmeans") {
            iterations = 256;
            trace = TraceGenerator::generate_kmeans_sample(
                16, iterations * 4, &data
            );
        }
        else if (kernel == "matvec") {
            iterations = 256;
            trace = TraceGenerator::generate_matvec_sample(
                iterations, 4096, &data
            );
        }
        else if (kernel == "backprop") {
            iterations = 256;
            trace = TraceGenerator::generate_backprop_sample(
                iterations, &data
            );
        }
    }
    else if (kernel == "matmul") {
        trace = TraceGenerator::generate_matmul(
            matmul_output_dimension, &data, 512, reduction_iterations
        );
        iterations = reduction_iterations;
    }
    else if (kernel == "stencil") {
        if (iterations > std::numeric_limits<int>::max() / 4) {
            std::cerr << "Error: stencil iterations are too large.\n";
            return 1;
        }
        trace = TraceGenerator::generate_stencil(iterations * 4, &data);
    }
    else if (kernel == "kmeans") {
        if (iterations > std::numeric_limits<int>::max() / 4) {
            std::cerr << "Error: kmeans iterations are too large.\n";
            return 1;
        }
        trace = TraceGenerator::generate_kmeans(iterations * 4, &data);
    }
    else if (kernel == "matvec") {
        trace = TraceGenerator::generate_matvec_sample(
            iterations, 4096, &data
        );
    }
    else if (kernel == "backprop") {
        trace = TraceGenerator::generate_backprop_sample(iterations, &data);
    }

    if (trace.empty()) {

        std::cerr
            << "Error: Trace generation produced 0 operations.\n";

        return 1;
    }

    // --------------------------------------------------
    // 4. Create results directory
    // --------------------------------------------------

    const fs::path results_dir =
        paper_sample ? fs::path("results/paper_sample") : fs::path("results");

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
        << (kernel == "matmul"
                ? "Output tile: " + std::to_string(matmul_output_dimension) +
                      " x " + std::to_string(matmul_output_dimension) + "\n"
                      "Reduction  : " + std::to_string(reduction_iterations) + "\n"
                : "")
        << (summary_only ? "Cycle log mode : summary-only\n" : "")
        << (paper_sample ? "Profile     : bounded paper-workload sample\n" : "")
        << (paper_sample
                ? "Paper size  : " + data.reference_workload_shape + "\n"
                : "")
        << (paper_sample
                ? "Simulated   : " + data.simulated_sample_shape + "\n"
                : "")
        << (paper_sample ? "Sample note : " + data.sample_note + "\n" : "")
        << "============================================================\n\n";

    log_file << "WHAT IS COMPUTED: " << data.name << "\n\n"
             << "INPUT VALUES (signed 32-bit words read from memory by the load phases):\n";
    for (const auto& line : data.input_lines)
        log_file << "  " << line << "\n";
    log_file << "\nEach line below is one clock-cycle event, in this order for every operation:\n"
             << "  [FETCH]            op enters the operation table (with its real values; renamed registers if enabled)\n"
             << "  [LOAD ...]         memory phase: ADDR-GEN (2) -> MEM-VISIT (16) -> writeback (2) = LOAD COMPLETE\n"
             << "  [CALC START/COMPLETE]  calculation phase with Table III latency; the ALU result is shown at COMPLETE\n"
             << "  [STORE ...]        memory phase that writes the result: ADDR-GEN -> MEM-VISIT -> writeback\n"
             << "  [COMMIT]           op retires in program order once all its phases are done\n\n";

    std::ostream* out_stream = summary_only ? nullptr : &log_file;

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

        if (paper_sample) {
            std::cout
                << "Paper size  : " << data.reference_workload_shape << "\n"
                << "Simulated   : " << data.simulated_sample_shape << "\n"
                << "Sample note : " << data.sample_note << "\n";
        }
        if (kernel == "matmul")
            std::cout << "Output tile : " << matmul_output_dimension << " x "
                      << matmul_output_dimension << "\n"
                      << "Reduction   : " << reduction_iterations << "\n";

        std::cout
            << "Log file    : " << log_path.string() << "\n\n";
    }

    // --------------------------------------------------
    // 7. Run all four architectural designs
    // --------------------------------------------------

    // Each design starts from the same initial memory/registers; its final memory is checked
    // against the expected values computed by plain C++ in the trace generator.
    struct Check {
        std::string design;
        bool pass;
        size_t ok;
        size_t total;
        double wall_time_ms;
    };
    std::vector<Check> checks;

    auto run_design = [&](const char* design, auto run_fn) -> uint64_t {
        KernelData d = data;                       // private copy of memory and registers
        log_file << "\n################ " << design << " ################\n";
        const auto start = std::chrono::steady_clock::now();
        uint64_t cycles = run_fn(&d);
        const double wall_time_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start
            ).count();
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
        checks.push_back({design, pass, ok, d.expected.size(), wall_time_ms});
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

    if (paper_sample) {
        rep << "Paper workload size : " << data.reference_workload_shape << "\n"
            << "Simulated sample    : " << data.simulated_sample_shape << "\n"
            << "Sample note         : " << data.sample_note << "\n";
    }
    if (kernel == "matmul")
        rep << "Output tile          : " << matmul_output_dimension << " x "
            << matmul_output_dimension << "\n"
            << "Reduction iterations : " << reduction_iterations << "\n";
    if (summary_only)
        rep << "Cycle log mode       : summary-only\n";

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
        << std::setw(12)
        << "Speedup"
        << "Host time (ms)\n";

    rep
        << "--------------------------------------------------------\n";

    rep
        << std::left
        << std::setw(36)
        << "Design 1: In-Order Baseline"
        << std::setw(16)
        << c1
        << std::setw(12)
        << "1.00x"
        << std::fixed << std::setprecision(3)
        << checks[0].wall_time_ms << "\n";

    rep
        << std::left
        << std::setw(36)
        << "Design 2: Pipelined"
        << std::setw(16)
        << c2
        << std::fixed << std::setprecision(2)
        << s2 << "x"
        << std::setw(8)
        << " "
        << std::fixed << std::setprecision(3)
        << checks[1].wall_time_ms << "\n";

    rep
        << std::left
        << std::setw(36)
        << "Design 3: Pipelined + Renaming"
        << std::setw(16)
        << c3
        << std::fixed << std::setprecision(2)
        << s3 << "x"
        << std::setw(8)
        << " "
        << std::fixed << std::setprecision(3)
        << checks[2].wall_time_ms << "\n";

    rep
        << std::left
        << std::setw(36)
        << "Design 4: Fine-Grained Issue"
        << std::setw(16)
        << c4
        << std::fixed << std::setprecision(2)
        << s4 << "x"
        << std::setw(8)
        << " "
        << std::fixed << std::setprecision(3)
        << checks[3].wall_time_ms << "\n";

    rep
        << "========================================================\n";

    rep << "Result check (simulated arithmetic vs plain C++ reference):\n";
    for (const auto& c : checks)
        rep << "  " << c.design << ": " << (c.pass ? "PASS" : "FAIL") << " (" << c.ok << "/" << c.total << ")\n";
    rep << "========================================================\n";

    std::cout << rep.str();

    {
        std::ofstream summary_log(
            results_dir / ("pipeimc_summary_" + tag + ".log")
        );
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
