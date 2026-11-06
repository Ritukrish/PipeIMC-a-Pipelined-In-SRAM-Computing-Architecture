
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
#include "functional.hpp"
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
    bool func = false;   // execute the kernel functionally (real ALU), then time the executed operations
    int func_n = 16;     // problem size of the functional run

    // --------------------------------------------------
    // 2. Argument parsing
    // --------------------------------------------------

    try {
        for (int i = 1; i < argc; ++i) {

            std::string arg = argv[i];

            if (arg == "--verbose" || arg == "-v") {
                verbose = true;
            }
            else if (arg == "--func") {
                func = true;
            }
            else if (arg == "--n") {

                if (i + 1 >= argc) {
                    throw std::invalid_argument(
                        "Missing value after --n"
                    );
                }

                func_n = std::stoi(argv[++i]);
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
            << "  pipeimc_sim --kernel matmul --iterations 256\n"
            << "  pipeimc_sim matmul --func --n 16      (functional execution + timing)\n\n"
            << "Available kernels: matmul, stencil, kmeans\n";

        return 1;
    }

    // --------------------------------------------------
    // 3. Generate operation trace
    // --------------------------------------------------

    std::vector<InSramOp> trace;

    const std::string tag = (func ? "func_" : "") + kernel;

    std::unique_ptr<FuncSim> fsim;

    if (func) {

        // Execution-driven run: the timing engine fetches the IMC program itself and
        // computes the real ALU results when each calculation phase finishes.
        fsim.reset(new FuncSim(kernel, func_n));
        iterations = func_n;
    }
    else if (kernel == "matmul") {
        trace = TraceGenerator::generate_matmul(iterations);
    }
    else if (kernel == "stencil") {
        trace = TraceGenerator::generate_stencil(iterations);
    }
    else if (kernel == "kmeans") {
        trace = TraceGenerator::generate_kmeans(iterations);
    }

    if (!func && trace.empty()) {

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
        << "Kernel      : " << kernel << (func ? " (functional run)" : "") << "\n"
        << (func ? "Size n      : " : "Iterations  : ") << iterations << "\n"
        << "Operations  : " << (func ? std::string("(counted during execution)") : std::to_string(trace.size())) << "\n"
        << "============================================================\n\n";

    if (func) {
        log_file
            << fsim->listing()
            << "\nEach line below is one clock-cycle event. At [CALC COMPLETE] the real ALU operation is computed,\n"
            << "after the Table III calculation cycles have elapsed; loads/stores read/write the real memory.\n\n";
    }

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

    uint64_t c1, c2, c3, c4;

    if (func) {
        c1 = fsim->run(SimMode::BASELINE, out_stream);
        c2 = fsim->run(SimMode::PIPELINED, out_stream);
        c3 = fsim->run(SimMode::RENAMING, out_stream);
        c4 = fsim->run(SimMode::FINE_GRAINED, out_stream);
    }
    else {
        c1 = ModelBaseline::run(trace, out_stream);
        c2 = ModelPipelined::run(trace, out_stream);
        c3 = ModelRenaming::run(trace, out_stream);
        c4 = ModelFineGrained::run(trace, out_stream);
    }

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
        << (func ? "Size n            : " : "Iterations       : ") << iterations << "\n";

    rep
        << "Total Operations : " << (func ? fsim->ops_per_element() : trace.size()) << "\n";

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

    if (func) {
        rep << fsim->report()
            << "========================================================\n";
    }

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

    return (func && !fsim->all_pass()) ? 1 : 0;
}
