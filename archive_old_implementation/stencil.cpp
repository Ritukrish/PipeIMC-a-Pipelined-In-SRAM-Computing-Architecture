#include "kernels.hpp"

// 3D Stencil (stencil3d) 7-Point Grid Operation Table
std::vector<TraceOp> getStencilOpTable(int num_iterations, int64_t* golden_out) {
    std::vector<TraceOp> op_table;
    int id = 1;
    int64_t last_grid_output = 0;

    // Stencil weights: c1 = 2 (stored in reg 20 via init), c0 = 4 (stored in reg 21 via init)
    op_table.push_back({id++, "INIT_WEIGHT_C1", OpCode::ADD, 20, 20, -1, false, false, false, false, true, 0, 0, 2});
    op_table.push_back({id++, "INIT_WEIGHT_C0", OpCode::ADD, 21, 21, -1, false, false, false, false, true, 0, 0, 4});

    for (int i = 0; i < num_iterations; ++i) {
        std::string p = "I" + std::to_string(i) + "_";
        int64_t xp = (i % 5) + 1, xm = ((i + 1) % 5) + 1;
        int64_t yp = ((i + 2) % 5) + 1, ym = ((i + 3) % 5) + 1;
        int64_t zp = ((i + 4) % 5) + 1, zm = (i % 3) + 1;
        int64_t center = (i % 7) + 2;

        last_grid_output = ((xp + xm) + (yp + ym) + (zp + zm)) * 2 + (center * 4);

        op_table.push_back({id++, p + "LOAD_ADD_X", OpCode::ADD,    1,  10, 11, true,  true,  false, false, false, xp, xm, 0});
        op_table.push_back({id++, p + "LOAD_ADD_Y", OpCode::ADD,    2,  12, 13, true,  true,  false, false, false, yp, ym, 0});
        op_table.push_back({id++, p + "LOAD_ADD_Z", OpCode::ADD,    3,  14, 15, true,  true,  false, false, false, zp, zm, 0});
        op_table.push_back({id++, p + "SUM_XY",     OpCode::ADD,    4,  1,  2,  false, false, false, false, false, 0,  0,  0});
        op_table.push_back({id++, p + "SUM_XYZ",    OpCode::ADD,    5,  4,  3,  false, false, false, false, false, 0,  0,  0});
        op_table.push_back({id++, p + "MUL_C1",     OpCode::MUL,    6,  5,  20, false, false, false, false, false, 0,  0,  0});
        op_table.push_back({id++, p + "MUL_CENTER", OpCode::MUL,    7,  16, 21, true,  false, false, false, false, center, 0, 0});
        op_table.push_back({id++, p + "STORE_OUT",  OpCode::ADD,    17, 6,  7,  false, false, true,  false, false, 0,  0,  0});
        op_table.push_back({id++, p + "INC_IDX",    OpCode::ADD,    8,  8,  -1, false, false, false, false, true,  0,  0,  1});
        op_table.push_back({id++, p + "B_LOOP",     OpCode::BRANCH, -1, 8,  22, false, false, false, true,  false, 0,  0,  0});
    }

    if (golden_out) *golden_out = last_grid_output;
    return op_table;
}