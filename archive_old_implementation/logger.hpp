#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>

class ExecutionLogger {
private:
    static constexpr int BENCHMARK_WIDTH = 22;
    static constexpr int MODE_WIDTH = 46;
    static constexpr int CYCLE_WIDTH = 10;
    static constexpr int STAGE_WIDTH = 14;
    static constexpr int WARP_WIDTH = 8;
    static constexpr int OP_WIDTH = 9;
    static constexpr int LOG_WIDTH =
        BENCHMARK_WIDTH + MODE_WIDTH + CYCLE_WIDTH + STAGE_WIDTH +
        WARP_WIDTH + OP_WIDTH + 15;

    std::ofstream log_file;
    std::mutex log_mutex;
    std::string benchmark = "Uncategorized";
    std::string mode = "Unspecified";

    ExecutionLogger() = default;

    void write_separator(char character = '-') {
        log_file << std::string(LOG_WIDTH, character) << '\n';
    }

public:
    ExecutionLogger(const ExecutionLogger&) = delete;
    ExecutionLogger& operator=(const ExecutionLogger&) = delete;

    static ExecutionLogger& get_instance() {
        static ExecutionLogger instance;
        return instance;
    }

    void init(const std::string& filename = "execution_trace.log") {
        std::lock_guard<std::mutex> lock(log_mutex);
        log_file.open(filename, std::ios::out | std::ios::trunc);
        if (log_file.is_open()) {
            log_file << "PipeIMC SIMULATION EVENT LOG\n";
            log_file << "Each run is grouped by benchmark and optimization mode.\n";
            write_separator('=');
        } else {
            std::cerr << "Error: Unable to open log file " << filename << std::endl;
        }
    }

    void begin_run(const std::string& benchmark_name, const std::string& mode_name,
                   size_t operation_count) {
        std::lock_guard<std::mutex> lock(log_mutex);
        benchmark = benchmark_name;
        mode = mode_name;
        if (log_file.is_open()) {
            write_separator('=');
            log_file << "BENCHMARK : " << benchmark << '\n'
                     << "MODE      : " << mode << '\n'
                     << "OPERATIONS: " << operation_count << '\n';
            write_separator();
            log_file << std::left
                     << std::setw(BENCHMARK_WIDTH) << "Benchmark"
                     << " | "
                     << std::setw(MODE_WIDTH) << "Optimization Mode"
                     << " | "
                     << std::setw(CYCLE_WIDTH) << "Cycle"
                     << " | "
                     << std::setw(STAGE_WIDTH) << "Stage"
                     << " | "
                     << std::setw(WARP_WIDTH) << "Warp"
                     << " | "
                     << std::setw(OP_WIDTH) << "Op ID"
                     << " | Event\n";
            write_separator();
        }
    }

    void log_event(uint64_t cycle, const std::string& stage, int warp_id, int instr_id, const std::string& description) {
        std::lock_guard<std::mutex> lock(log_mutex);
        if (log_file.is_open()) {
            log_file << std::left
                     << std::setw(BENCHMARK_WIDTH) << benchmark
                     << " | "
                     << std::setw(MODE_WIDTH) << mode
                     << " | "
                     << std::setw(CYCLE_WIDTH) << cycle
                     << " | "
                     << std::setw(STAGE_WIDTH) << stage
                     << " | "
                     << std::setw(WARP_WIDTH) << warp_id
                     << " | "
                     << std::setw(OP_WIDTH) << instr_id
                     << " | " << description << "\n";
        }
    }

    void log_cycle_activity(uint64_t cycle, const std::string& port0,
                            const std::string& port1, const std::string& memory_visits) {
        std::lock_guard<std::mutex> lock(log_mutex);
        if (log_file.is_open()) {
            log_file << std::left
                     << std::setw(BENCHMARK_WIDTH) << benchmark
                     << " | "
                     << std::setw(MODE_WIDTH) << mode
                     << " | "
                     << std::setw(CYCLE_WIDTH) << cycle
                     << " | " << "Cycle State"
                     << " | " << std::setw(WARP_WIDTH) << "-"
                     << " | " << std::setw(OP_WIDTH) << "-"
                     << " | Port 0: " << port0
                     << " || Port 1: " << port1
                     << " || Off-array visits: " << memory_visits << '\n';
        }
    }

    void end_run(uint64_t total_cycles, int64_t output, int64_t expected_output) {
        std::lock_guard<std::mutex> lock(log_mutex);
        if (log_file.is_open()) {
            write_separator();
            log_file << "RUN SUMMARY: " << ((output == expected_output) ? "PASS" : "FAIL")
                     << " | Benchmark: " << benchmark
                     << " | Mode: " << mode
                     << " | Cycles: " << total_cycles
                     << " | Output: " << output
                     << " | Expected: " << expected_output << '\n';
            write_separator('=');
            log_file << '\n';
        }
    }

    void close() {
        std::lock_guard<std::mutex> lock(log_mutex);
        if (log_file.is_open()) {
            write_separator('=');
            log_file << "SIMULATION LOG COMPLETE\n";
            log_file.close();
        }
    }
};

#define LOG_CYCLE(cycle, stage, warp, instr, desc) \
    ::ExecutionLogger::get_instance().log_event(cycle, stage, warp, instr, desc)

#endif // LOGGER_HPP