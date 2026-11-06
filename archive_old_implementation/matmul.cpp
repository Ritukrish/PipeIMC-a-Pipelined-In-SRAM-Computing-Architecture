#include "kernels.hpp"

// Matrix Multiplication (matmul) Operation Table — Fig. 2(a) & 2(b)
std::vector<TraceOp> getMatmulOpTable(int num_iterations, int64_t* golden_out) {
    std::vector<TraceOp> op_table;
    int id = 1;
    int64_t golden_sum = 0;

    for (int k = 0; k < num_iterations; ++k) {
        std::string p = "I" + std::to_string(k) + "_";
        // Deterministic matrix elements for A and B in iteration k
        int64_t a1 = (k % 7) + 1;
        int64_t b1 = ((k * 3) % 5) + 1;
        int64_t a2 = ((k + 2) % 7) + 1;
        int64_t b2 = ((k * 2 + 1) % 5) + 1;

        golden_sum += (a1 * b1) + (a2 * b2);

        // O1: w1 := IMC([ptrA1], [ptrB1], MUL)
        op_table.push_back({id++, p + "O1_MUL_A1_B1", OpCode::MUL,    1,  10, 11, true,  true,  false, false, false, a1, b1, 0});
        // O2: w2 := IMC([ptrA2], [ptrB2], MUL)
        op_table.push_back({id++, p + "O2_MUL_A2_B2", OpCode::MUL,    2,  12, 13, true,  true,  false, false, false, a2, b2, 0});
        // O3: w3 := IMC(w1, w2, ADD)
        op_table.push_back({id++, p + "O3_ADD_W1_W2", OpCode::ADD,    3,  1,  2,  false, false, false, false, false, 0,  0,  0});
        // O4: w0 := IMC(w0, w3, ADD)  (Accumulate into sum register w0)
        op_table.push_back({id++, p + "O4_ACC_SUM",   OpCode::ADD,    0,  0,  3,  false, false, false, false, false, 0,  0,  0});
        // O5: w4 := IMC(w4, 1, ADD)   (Increment loop counter k)
        op_table.push_back({id++, p + "O5_INC_K",     OpCode::ADD,    4,  4,  -1, false, false, false, false, true,  0,  0,  1});
        // B:  BRANCH(w4 != w6, Loop)
        op_table.push_back({id++, p + "B_LOOP",       OpCode::BRANCH, -1, 4,  6,  false, false, false, true,  false, 0,  0,  0});
    }

    // Final Store: C[i * n + j] = sum -> [w14] := IMC(w0, 0, ADD)
    op_table.push_back({id++, "FINAL_STORE_C", OpCode::ADD, 14, 0, -1, false, false, true, false, true, 0, 0, 0});

    if (golden_out) *golden_out = golden_sum;
    return op_table;
}