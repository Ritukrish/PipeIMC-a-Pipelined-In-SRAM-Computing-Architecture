#include "kernels.hpp"

// K-Means / KNN Distance Accumulation Operation Table
std::vector<TraceOp> getKmeansOpTable(int num_iterations, int64_t* golden_out) {
    std::vector<TraceOp> op_table;
    int id = 1;
    int64_t total_dist_sq = 0;

    for (int d = 0; d < num_iterations; ++d) {
        std::string p = "I" + std::to_string(d) + "_";
        int64_t x1  = (d % 9) + 3,  mu1 = (d % 4) + 1;
        int64_t x2  = ((d + 3) % 9) + 2, mu2 = ((d + 1) % 4) + 1;

        int64_t diff1 = x1 - mu1;
        int64_t diff2 = x2 - mu2;
        total_dist_sq += (diff1 * diff1) + (diff2 * diff2);

        op_table.push_back({id++, p + "SUB_FEAT1", OpCode::SUB,    1,  10, 11, true,  true,  false, false, false, x1, mu1, 0});
        op_table.push_back({id++, p + "SQ_FEAT1",  OpCode::MUL,    2,  1,  1,  false, false, false, false, false, 0,  0,   0});
        op_table.push_back({id++, p + "SUB_FEAT2", OpCode::SUB,    3,  12, 13, true,  true,  false, false, false, x2, mu2, 0});
        op_table.push_back({id++, p + "SQ_FEAT2",  OpCode::MUL,    4,  3,  3,  false, false, false, false, false, 0,  0,   0});
        op_table.push_back({id++, p + "ADD_SQ",    OpCode::ADD,    5,  2,  4,  false, false, false, false, false, 0,  0,   0});
        op_table.push_back({id++, p + "ACC_DIST",  OpCode::ADD,    0,  0,  5,  false, false, false, false, false, 0,  0,   0});
        if ((d + 1) % 8 == 0) {
            op_table.push_back({id++, p + "CMP_MIN", OpCode::SUB, 15, 0, 9,  false, false, false, false, false, 0, 0, 0});
            op_table.push_back({id++, p + "UPD_MIN", OpCode::ADD, 9,  0, -1, false, false, false, false, true,  0, 0, 0});
        }
        op_table.push_back({id++, p + "INC_D",     OpCode::ADD,    6,  6,  -1, false, false, false, false, true,  0,  0,   1});
        op_table.push_back({id++, p + "B_LOOP",    OpCode::BRANCH, -1, 6,  8,  false, false, false, true,  false, 0,  0,   0});
    }

    // Store final accumulated distance from w0 to [w14]
    op_table.push_back({id++, "FINAL_STORE_DIST", OpCode::ADD, 14, 0, -1, false, false, true, false, true, 0, 0, 0});

    if (golden_out) *golden_out = total_dist_sq;
    return op_table;
}