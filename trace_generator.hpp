#ifndef TRACE_GENERATOR_HPP
#define TRACE_GENERATOR_HPP

#include "types.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

class TraceGenerator {
public:
    static std::string opToString(OpCode op) {
        switch (op) {
            case OpCode::ADD:    return "ADD";
            case OpCode::SUB:    return "SUB";
            case OpCode::MUL:    return "MUL";
            case OpCode::DIV:    return "DIV";
            case OpCode::BRANCH: return "BRANCH";
        }
        return "ADD";
    }

    static OpCode stringToOp(const std::string& s) {
        if (s == "SUB")    return OpCode::SUB;
        if (s == "MUL")    return OpCode::MUL;
        if (s == "DIV")    return OpCode::DIV;
        if (s == "BRANCH") return OpCode::BRANCH;
        return OpCode::ADD;
    }

    // Write any trace vector to a human-readable CSV trace file
    static void saveTraceToFile(const std::string& filename, const std::vector<TraceOp>& trace) {
        std::ofstream out(filename);
        out << "# ID, Label, OpCode, rd, rs1, rs2, rs1_is_mem, rs2_is_mem, rd_is_mem, is_branch\n";
        for (const auto& op : trace) {
            out << op.id << ", " << op.label << ", " << opToString(op.op) << ", "
                << op.rd << ", " << op.rs1 << ", " << op.rs2 << ", "
                << op.rs1_is_mem << ", " << op.rs2_is_mem << ", "
                << op.rd_is_mem << ", " << op.is_branch << "\n";
        }
    }

    // Load a .trace file from disk into the simulator
    static std::vector<TraceOp> loadTraceFromFile(const std::string& filename) {
        std::vector<TraceOp> trace;
        std::ifstream in(filename);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string item;
            TraceOp op;
            std::getline(ss, item, ','); op.id = std::stoi(item);
            std::getline(ss, item, ',');
            item.erase(0, item.find_first_not_of(" \t"));
            op.label = item;
            std::getline(ss, item, ',');
            item.erase(0, item.find_first_not_of(" \t"));
            op.op = stringToOp(item);
            std::getline(ss, item, ','); op.rd = std::stoi(item);
            std::getline(ss, item, ','); op.rs1 = std::stoi(item);
            std::getline(ss, item, ','); op.rs2 = std::stoi(item);
            std::getline(ss, item, ','); op.rs1_is_mem = (std::stoi(item) != 0);
            std::getline(ss, item, ','); op.rs2_is_mem = (std::stoi(item) != 0);
            std::getline(ss, item, ','); op.rd_is_mem  = (std::stoi(item) != 0);
            std::getline(ss, item, ','); op.is_branch  = (std::stoi(item) != 0);
            trace.push_back(op);
        }
        return trace;
    }

    // 1. Matrix Multiply (matmul) Trace Generator — Exact translation of Fig. 2(a) & 2(b)
    static std::vector<TraceOp> buildMatmulTrace(int num_iterations = 32) {
        std::vector<TraceOp> trace;
        int id = 1;
        for (int k = 0; k < num_iterations; ++k) {
            std::string p = "I" + std::to_string(k) + "_";
            // O1: w1 := IMC([ptrA1], [ptrB1], MUL)
            trace.push_back({id++, p + "O1_MUL_A1_B1", OpCode::MUL, 1, 10, 11, true,  true,  false, false});
            // O2: w2 := IMC([ptrA2], [ptrB2], MUL)
            trace.push_back({id++, p + "O2_MUL_A2_B2", OpCode::MUL, 2, 12, 13, true,  true,  false, false});
            // O3: w3 := IMC(w1, w2, ADD)
            trace.push_back({id++, p + "O3_ADD_W1_W2", OpCode::ADD, 3, 1,  2,  false, false, false, false});
            // O4: w0 := IMC(w0, w3, ADD)
            trace.push_back({id++, p + "O4_ACC_SUM",   OpCode::ADD, 0, 0,  3,  false, false, false, false});
            // O5: w4 := IMC(w4, w5, ADD)  (k++)
            trace.push_back({id++, p + "O5_INC_K",     OpCode::ADD, 4, 4,  5,  false, false, false, false});
            // B:  BRANCH(w4 != w6, Loop)
            trace.push_back({id++, p + "B_LOOP",       OpCode::BRANCH, -1, 4, 6, false, false, false, true});
        }
        // Final Store: C[i*n+j] = sum -> [w14] := IMC(w0, 0, ADD)
        trace.push_back({id++, "FINAL_STORE_C", OpCode::ADD, 14, 0, 0, false, false, true, false});
        return trace;
    }

    // 2. 3D Stencil (stencil3d) Trace Generator
    static std::vector<TraceOp> buildStencilTrace(int num_iterations = 32) {
        std::vector<TraceOp> trace;
        int id = 1;
        for (int i = 0; i < num_iterations; ++i) {
            std::string p = "I" + std::to_string(i) + "_";
            trace.push_back({id++, p + "LOAD_ADD_X", OpCode::ADD, 1, 10, 11, true,  true,  false, false});
            trace.push_back({id++, p + "LOAD_ADD_Y", OpCode::ADD, 2, 12, 13, true,  true,  false, false});
            trace.push_back({id++, p + "LOAD_ADD_Z", OpCode::ADD, 3, 14, 15, true,  true,  false, false});
            trace.push_back({id++, p + "SUM_XY",     OpCode::ADD, 4, 1,  2,  false, false, false, false});
            trace.push_back({id++, p + "SUM_XYZ",    OpCode::ADD, 5, 4,  3,  false, false, false, false});
            trace.push_back({id++, p + "MUL_C1",     OpCode::MUL, 6, 5,  20, false, false, false, false});
            trace.push_back({id++, p + "MUL_CENTER", OpCode::MUL, 7, 16, 21, true,  false, false, false});
            trace.push_back({id++, p + "STORE_OUT",  OpCode::ADD, 17, 6, 7,  false, false, true,  false});
            trace.push_back({id++, p + "INC_IDX",    OpCode::ADD, 8, 8,  9,  false, false, false, false});
            trace.push_back({id++, p + "B_LOOP",     OpCode::BRANCH, -1, 8, 22, false, false, false, true});
        }
        return trace;
    }

    // 3. K-Means Clustering (kmeans) Trace Generator
    static std::vector<TraceOp> buildKmeansTrace(int num_iterations = 32) {
        std::vector<TraceOp> trace;
        int id = 1;
        for (int d = 0; d < num_iterations; ++d) {
            std::string p = "I" + std::to_string(d) + "_";
            trace.push_back({id++, p + "SUB_FEAT1", OpCode::SUB, 1, 10, 11, true,  true,  false, false});
            trace.push_back({id++, p + "SQ_FEAT1",  OpCode::MUL, 2, 1,  1,  false, false, false, false});
            trace.push_back({id++, p + "SUB_FEAT2", OpCode::SUB, 3, 12, 13, true,  true,  false, false});
            trace.push_back({id++, p + "SQ_FEAT2",  OpCode::MUL, 4, 3,  3,  false, false, false, false});
            trace.push_back({id++, p + "ADD_SQ",    OpCode::ADD, 5, 2,  4,  false, false, false, false});
            trace.push_back({id++, p + "ACC_DIST",  OpCode::ADD, 0, 0,  5,  false, false, false, false});
            trace.push_back({id++, p + "INC_D",     OpCode::ADD, 6, 6,  7,  false, false, false, false});
            trace.push_back({id++, p + "B_LOOP",    OpCode::BRANCH, -1, 6, 8, false, false, false, true});
        }
        return trace;
    }
};

#endif // TRACE_GENERATOR_HPP
