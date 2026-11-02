#include "types.hpp"
#include "kernels.hpp"
#include "simulator.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cassert>

void runVerificationTests() {
    std::cout << "Running Hardware Verification Tests...\n";
    {
        RenameUnit ru;
        OpTableEntry e1, e2;
        e1.trace_op = {1, "Producer1_Dead", OpCode::MUL, 1, 2, 3, false, false, false, false, false};
        e2.trace_op = {2, "Producer2_Live", OpCode::ADD, 1, 4, 5, false, false, false, false, false};
        ru.renameOp(e1);
        int p1 = e1.phys_rd;
        assert(ru.usage_counter[p1] == 1);
        ru.renameOp(e2);
        assert(ru.usage_counter[p1] == 1);
        ru.commitOp(e1);
        assert(ru.usage_counter[p1] == 0);
        std::cout << "  [PASS] Test 1: Dead-Write Rename Unit Reference Counting Verified.\n";
    }
    {
        std::vector<TraceOp> fig5_trace = {
            {1, "Op1_MUL", OpCode::MUL, 1, 3, 4, false, false, false, false, false},
            {2, "Op2_WAR", OpCode::ADD, 3, 5, 6, false, false, false, false, false}
        };
        SimConfig cfg_no_ren{DesignMode::PIPELINED};
        SimConfig cfg_ren{DesignMode::PIPELINED_RENAMING};
        PipeIMCSimulator sim_no_ren(cfg_no_ren), sim_ren(cfg_ren);
        uint64_t cyc_no_ren = sim_no_ren.runTrace(fig5_trace);
        uint64_t cyc_ren    = sim_ren.runTrace(fig5_trace);
        assert(cyc_ren < cyc_no_ren);
        std::cout << "  [PASS] Test 2: Fig. 5 WAR Conflict Stalls in Scoreboard (" << cyc_no_ren
                  << " cyc) vs Overlaps with Renaming (" << cyc_ren << " cyc).\n";
    }
    {
        std::vector<TraceOp> fig6_trace = {
            {1, "Op1_DIV", OpCode::DIV, 2, 4, 5, false, false, false, false, false},
            {2, "Op2_MUL", OpCode::MUL, 3, 1, 2, true,  false, false, false, false}
        };
        SimConfig cfg_in{DesignMode::IN_ORDER_BASELINE};
        SimConfig cfg_fg{DesignMode::FINE_GRAINED_ISSUE};
        PipeIMCSimulator sim_in(cfg_in), sim_fg(cfg_fg);
        uint64_t c_in = sim_in.runTrace(fig6_trace);
        uint64_t c_fg = sim_fg.runTrace(fig6_trace);
        assert(c_fg < c_in);
        std::cout << "  [PASS] Test 3: Fig. 6 Load Phase Overlaps with Preceding DIV (" << c_in << " -> " << c_fg << " cyc).\n\n";
    }
}

void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "  --kernel <matmul|stencil|kmeans|knn|all>\n"
              << "  --mode   <baseline|pipelined|renaming_fig3c|renaming|fine_grained|all>\n"
              << "  --iters  <N>          (Default: 256 iterations)\n"
              << "  --mul-lat <cycles>    (Table III MUL latency: 105..634, default 105)\n"
              << "  --mshrs  <N>          (Max in-flight memory visits, default 8)\n"
              << "  --scale-layout        (Enable hybrid-4 vs hybrid-8 thread/latency scaling)\n"
              << "  --test                (Run verification unit tests)\n";
}

int main(int argc, char* argv[]) {
    std::string kernel_arg = "all";
    std::string mode_arg   = "all";
    int num_iters          = 256;
    SimConfig base_cfg;
    bool run_tests         = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--kernel" || arg == "-k") && i + 1 < argc) {
            kernel_arg = argv[++i];
        } else if ((arg == "--mode" || arg == "-m") && i + 1 < argc) {
            mode_arg = argv[++i];
        } else if ((arg == "--iters" || arg == "-n") && i + 1 < argc) {
            num_iters = std::stoi(argv[++i]);
        } else if (arg == "--mul-lat" && i + 1 < argc) {
            base_cfg.mul_latency = std::stoi(argv[++i]);
        } else if (arg == "--mshrs" && i + 1 < argc) {
            base_cfg.max_mshrs = std::stoul(argv[++i]);
        } else if (arg == "--scale-layout") {
            base_cfg.scale_hybrid4 = true;
        } else if (arg == "--test") {
            run_tests = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (run_tests) {
        runVerificationTests();
    }

    struct KernelSpec {
        std::string name;
        std::vector<TraceOp> trace;
        int64_t golden_val;
    };

    std::vector<KernelSpec> selected_kernels;
    if (kernel_arg == "matmul" || kernel_arg == "all") {
        int64_t g = 0;
        auto t = getMatmulOpTable(num_iters, &g);
        selected_kernels.push_back({"matmul", t, g});
    }
    if (kernel_arg == "stencil" || kernel_arg == "stencil3d" || kernel_arg == "all") {
        int64_t g = 0;
        auto t = getStencilOpTable(num_iters, &g);
        selected_kernels.push_back({"stencil3d", t, g});
    }
    if (kernel_arg == "kmeans" || kernel_arg == "knn" || kernel_arg == "all") {
        int64_t g = 0;
        auto t = getKmeansOpTable(num_iters, &g);
        std::string kname = (kernel_arg == "knn") ? "knn" : "kmeans";
        selected_kernels.push_back({kname, t, g});
    }

    struct ModeSpec {
        std::string key;
        DesignMode mode;
        std::string title;
    };

    std::vector<ModeSpec> all_modes = {
        {"baseline",       DesignMode::IN_ORDER_BASELINE,       "1. In-Order Baseline (Fig. 3a)"},
        {"pipelined",      DesignMode::PIPELINED,               "2. Pipelined Scoreboard (Fig. 3b)"},
        {"renaming_fig3c", DesignMode::PIPELINED_RENAMING_FIG3C,"3a. Renaming [Port0 Mem-Only] (Fig. 3c)"},
        {"renaming",       DesignMode::PIPELINED_RENAMING,      "3b. Renaming [Port0 Idle-Compute]"},
        {"fine_grained",   DesignMode::FINE_GRAINED_ISSUE,      "4. Fine-Grained Issue + Freeze (Fig. 3d)"}
    };

    std::cout << "================================================================================================================================\n";
    std::cout << " PipeIMC Simulator (Iters=" << num_iters << ", MUL=" << base_cfg.mul_latency
              << " cyc, MSHRs=" << base_cfg.max_mshrs
              << ", LayoutScale=" << (base_cfg.scale_hybrid4 ? "ON" : "OFF") << ")\n";
    std::cout << "================================================================================================================================\n";
    std::cout << std::left << std::setw(10) << "Kernel"
              << "| " << std::setw(41) << "Optimization Mode"
              << "| " << std::setw(10) << "Cycles"
              << "| " << std::setw(8)  << "Speedup"
              << "| " << std::setw(8)  << "Util"
              << "| " << "Output Verification (SRAM vs Golden)\n";
    std::cout << "--------------------------------------------------------------------------------------------------------------------------------\n";

    for (const auto& k : selected_kernels) {
        SimConfig c0 = base_cfg;
        c0.mode = DesignMode::IN_ORDER_BASELINE;
        PipeIMCSimulator base_sim(c0);
        uint64_t baseline_cycles = base_sim.runTrace(k.trace);

        for (const auto& m : all_modes) {
            if (mode_arg == "all" || mode_arg == m.key) {
                SimConfig cm = base_cfg;
                cm.mode = m.mode;
                PipeIMCSimulator sim(cm);
                uint64_t cycles = sim.runTrace(k.trace);
                double speedup  = static_cast<double>(baseline_cycles) / static_cast<double>(cycles);

                bool pass = (sim.stats.computed_output == k.golden_val);
                std::string check_str = (pass ? "PASS [SRAM=" : "FAIL [SRAM=") +
                                        std::to_string(sim.stats.computed_output) +
                                        " == Golden=" + std::to_string(k.golden_val) + "]";

                std::cout << std::left << std::setw(10) << k.name
                          << "| " << std::setw(41) << m.title
                          << "| " << std::setw(10) << cycles
                          << "| " << std::fixed << std::setprecision(2) << speedup << "x "
                          << "| " << std::fixed << std::setprecision(1) << std::setw(5) << sim.stats.utilization_pct << "% "
                          << "| " << check_str << "\n";
            }
        }
        std::cout << "--------------------------------------------------------------------------------------------------------------------------------\n";
    }

    return 0;
}