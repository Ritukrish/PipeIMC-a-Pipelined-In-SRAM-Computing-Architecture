
#include "trace_generator.hpp"

#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <vector>

// ============================================================
// OPERATION CREATION
// ============================================================

InSramOp make_op(
    uint64_t id,
    OpType type,
    int src1,
    int src2,
    int dest,
    bool load1,
    bool load2,
    bool calc,
    bool store,
    bool branch
) {
    InSramOp op{};

    op.op_id = id;

    op.warp_id = 0;
    op.subarray_id = 0;

    op.type = type;

    op.src1 = src1;
    op.src2 = src2;
    op.dest = dest;

    op.has_imm = false;
    op.imm_val = 0;

    // Independent operation phases
    op.has_load1 = load1;
    op.has_load2 = load2;
    op.has_calc = calc;
    op.has_store = store;

    op.affects_control_flow = branch;

    // PipeIMC memory-phase step latencies
    op.addr_cycles = 2;
    op.visit_cycles = 16;
    op.writeback_cycles = 2;

    // PipeIMC Table III calculation latencies
    switch (type) {

        case OpType::ADD:
        case OpType::LOGIC:
            op.calc_cycles = 9;
            break;

        case OpType::SUB:
            op.calc_cycles = 17;
            break;

        case OpType::MUL:
            // Minimum multiplication latency.
            // Actual latency depends on operand width.
            op.calc_cycles = 105;
            break;

        case OpType::DIV:
            // Minimum division latency.
            // Actual latency depends on operand width.
            op.calc_cycles = 145;
            break;

        case OpType::BRANCH:
            // Simplified branch-resolution latency.
            // This is a simulator assumption, not a
            // calculation latency specified in Table III.
            op.calc_cycles = 13;
            break;

        case OpType::BARRIER:
            // Simplified synchronization latency.
            op.calc_cycles = 1;
            break;
    }

    return op;
}

// ============================================================
// INPUT VALIDATION
// ============================================================

// deterministic small test values
static int32_t pat(int k, int mul, int add, int mod, int off) {
    return (k * mul + add) % mod - off;
}

static std::string list_values(const std::vector<int32_t>& m, const char* name,
                               const std::vector<uint32_t>& addr) {
    std::string s = std::string(name) + " =";
    size_t shown = std::min<size_t>(addr.size(), 16);
    for (size_t i = 0; i < shown; ++i) s += " " + std::to_string(m[addr[i]]);
    if (shown < addr.size()) s += " ...";
    return s;
}

static void validate_iterations(int iterations) {

    if (iterations <= 0) {
        throw std::invalid_argument(
            "Number of iterations must be greater than zero."
        );
    }
}

// ============================================================
// 1. MATRIX MULTIPLICATION  (full n x n product, n = "iterations")
// ============================================================
//
// C[i][j] = sum over k of ( A[i][k]*B[k][j] + A[k][j]*B[i][k] )   (paper Fig. 2 pattern)
//
// The trace contains the in-SRAM operations of EVERY output element C[i][j], one after the
// other. Each operation is an InSramOp built with make_op(), carrying the real numbers it
// works on (load values, immediates) and the memory words it touches.
//
// Per output element:
//   Z1: r8 = 0           accumulator reset         (ADD r0, #0)
//   Z2: r9 = 0           loop counter reset        (ADD r0, #0)
//   then for k = 0 .. n-1:
//   O1: r3 = A[i][k] * B[k][j]      (2 loads, MUL)
//   O2: r6 = A[k][j] * B[i][k]      (2 loads, MUL)
//   O3: r7 = r3 + r6
//   O4: r8 = r8 + r7, store C[i][j]  (ADD + store phase)
//   O5: r9 = r9 + 1
//   B : branch while r9 != r10 (r10 = n, set once at the start)
//
// ============================================================

std::vector<InSramOp>
TraceGenerator::generate_matmul(int iterations, KernelData* data) {

    validate_iterations(iterations);

    if (iterations > 16) {
        throw std::invalid_argument(
            "matmul builds the full n x n product (n^3 loop iterations); "
            "use n <= 16."
        );
    }

    const uint32_t n = static_cast<uint32_t>(iterations);
    const uint32_t nn = n * n;
    const uint32_t A = 0, B = nn, C = 2 * nn;

    // ---- real input matrices ----
    std::vector<int32_t> mem(3 * static_cast<size_t>(nn), 0);
    for (uint32_t x = 0; x < nn; ++x) {
        mem[A + x] = pat(static_cast<int>(x), 7, 3, 11, 5);
        mem[B + x] = pat(static_cast<int>(x), 5, 1, 9, 4);
    }

    std::vector<InSramOp> trace;
    trace.reserve(static_cast<size_t>(nn) * (n * 6 + 2) + 1);

    uint64_t id = 1;

    // makes a constant-assignment operation:  dest = r0 + #value
    auto set_const = [&](int dest, int value) {
        InSramOp op = make_op(id++, OpType::ADD, 0, 0, dest,
                              false, false, true, false);
        op.has_imm = true;
        op.imm_val = value;
        trace.push_back(op);
    };

    set_const(10, static_cast<int>(n));        // r10 = n (loop limit)

    if (data) {
        data->expected.clear();
    }

    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {

            set_const(8, 0);                   // Z1: accumulator = 0
            set_const(9, 0);                   // Z2: counter = 0

            int32_t sum = 0;

            for (uint32_t k = 0; k < n; ++k) {

                const uint32_t a1 = A + i * n + k, b1 = B + k * n + j;
                const uint32_t a2 = A + k * n + j, b2 = B + i * n + k;

                // O1: A[i][k] * B[k][j]
                InSramOp o1 = make_op(id++, OpType::MUL, 1, 2, 3,
                                      true, true, true, false);
                o1.load1_addr = a1;  o1.load1_val = mem[a1];
                o1.load2_addr = b1;  o1.load2_val = mem[b1];
                o1.has_load_vals = true;
                trace.push_back(o1);

                // O2: A[k][j] * B[i][k]
                InSramOp o2 = make_op(id++, OpType::MUL, 4, 5, 6,
                                      true, true, true, false);
                o2.load1_addr = a2;  o2.load1_val = mem[a2];
                o2.load2_addr = b2;  o2.load2_val = mem[b2];
                o2.has_load_vals = true;
                trace.push_back(o2);

                // O3: partial sum (operands already in the array)
                trace.push_back(make_op(id++, OpType::ADD, 3, 6, 7,
                                        false, false, true, false));

                // O4: accumulate and store C[i][j]
                InSramOp o4 = make_op(id++, OpType::ADD, 7, 8, 8,
                                      false, false, true, true);
                o4.store_addr = C + i * n + j;
                trace.push_back(o4);

                // O5: counter = counter + 1
                InSramOp o5 = make_op(id++, OpType::ADD, 9, 0, 9,
                                      false, false, true, false);
                o5.has_imm = true;
                o5.imm_val = 1;
                trace.push_back(o5);

                // B: loop branch, taken while counter != n
                InSramOp br = make_op(id++, OpType::BRANCH, 9, 10, 0,
                                      false, false, true, false, true);
                br.real_branch = true;
                br.branch_taken = (k + 1 != n);
                trace.push_back(br);

                sum += mem[a1] * mem[b1] + mem[a2] * mem[b2];
            }

            if (data)
                data->expected.push_back({C + i * n + j, sum,
                    "C[" + std::to_string(i) + "][" + std::to_string(j) + "]"});
        }
    }

    if (data) {
        data->name = "matmul: full " + std::to_string(n) + " x " + std::to_string(n) +
                     " product, C[i][j] = sum_k ( A[i][k]*B[k][j] + A[k][j]*B[i][k] )";
        data->memory = mem;
        data->input_lines.clear();
        for (uint32_t r = 0; r < n; ++r) {
            std::string la = "A row " + std::to_string(r) + ":", lb = "B row " + std::to_string(r) + ":";
            for (uint32_t c = 0; c < n; ++c) {
                la += " " + std::to_string(mem[A + r * n + c]);
                lb += " " + std::to_string(mem[B + r * n + c]);
            }
            data->input_lines.push_back(la);
            data->input_lines.push_back(lb);
        }
    }

    return trace;
}

// ============================================================
// 2. STENCIL
// ============================================================
//
// Simplified 2D stencil operation trace.
//
// O1: Load North and South; add.
// O2: Load East and West; add.
// O3: Combine the two partial sums.
// O4: Multiply by the stencil weight and store output.
// B : Loop-control branch.
//
// Five operations per iteration.
//
// ============================================================

std::vector<InSramOp>
TraceGenerator::generate_stencil(int iterations, KernelData* data) {

    validate_iterations(iterations);

    std::vector<InSramOp> trace;
    trace.reserve(static_cast<size_t>(iterations) * 5);

    uint64_t id = 1;

    for (int i = 0; i < iterations; ++i) {

        // O1: North + South
        trace.push_back(
            make_op(
                id++,
                OpType::ADD,
                1, 2, 5,
                true, true,
                true, false
            )
        );

        // O2: East + West
        trace.push_back(
            make_op(
                id++,
                OpType::ADD,
                3, 4, 6,
                true, true,
                true, false
            )
        );

        // O3: Combine partial sums
        trace.push_back(
            make_op(
                id++,
                OpType::ADD,
                5, 6, 7,
                false, false,
                true, false
            )
        );

        // O4: Apply weight and store output
        trace.push_back(
            make_op(
                id++,
                OpType::MUL,
                7, 9, 10,
                false, false,
                true, true
            )
        );

        // B: Loop-control branch
        trace.push_back(
            make_op(
                id++,
                OpType::BRANCH,
                10, 0, 0,
                false, false,
                true, false,
                true
            )
        );
    }

    // ---- real data and memory addresses: one output point per iteration ----
    {
        const uint32_t it = static_cast<uint32_t>(iterations);
        for (uint32_t i = 0; i < it; ++i) {
            const size_t base = static_cast<size_t>(i) * 5;
            trace[base + 0].load1_addr = i;              // North[i]
            trace[base + 0].load2_addr = it + i;         // South[i]
            trace[base + 1].load1_addr = 2 * it + i;     // East[i]
            trace[base + 1].load2_addr = 3 * it + i;     // West[i]
            trace[base + 3].store_addr = 4 * it + i;     // out[i]
        }
        if (data) {
            data->name = "stencil (out[i] = weight * (N[i] + S[i] + E[i] + W[i]), one point per iteration)";
            data->memory.assign(5 * static_cast<size_t>(it), 0);
            const int mul[4] = {3, 5, 7, 11}, add[4] = {1, 2, 3, 4};
            const char* nm[4] = {"North", "South", "East ", "West "};
            data->init_reg[9] = 3;      // stencil weight lives in r9
            for (int d = 0; d < 4; ++d) {
                std::vector<uint32_t> ad;
                for (uint32_t i = 0; i < it; ++i) {
                    data->memory[d * it + i] = pat(static_cast<int>(i), mul[d], add[d], 13, 6);
                    ad.push_back(d * it + i);
                }
                data->input_lines.push_back(list_values(data->memory, nm[d], ad));
            }
            data->input_lines.insert(data->input_lines.begin(), "weight (register r9) = 3");
            for (uint32_t i = 0; i < it; ++i) {
                int32_t v = 3 * ((data->memory[i] + data->memory[it + i]) + (data->memory[2 * it + i] + data->memory[3 * it + i]));
                data->expected.push_back({4 * it + i, v, "out[" + std::to_string(i) + "]"});
            }
        }
    }

    return trace;
}

// ============================================================
// 3. K-MEANS
// ============================================================
//
// Simplified distance-accumulation operation trace.
//
// O1: Load point and centroid; subtract.
// O2: Square the difference.
// O3: Accumulate squared distance and store output.
// B : Loop-control branch.
//
// Four operations per iteration.
//
// ============================================================

std::vector<InSramOp>
TraceGenerator::generate_kmeans(int iterations, KernelData* data) {

    validate_iterations(iterations);

    std::vector<InSramOp> trace;
    trace.reserve(static_cast<size_t>(iterations) * 4);

    uint64_t id = 1;

    for (int i = 0; i < iterations; ++i) {

        // O1: Point - centroid
        trace.push_back(
            make_op(
                id++,
                OpType::SUB,
                1, 2, 3,
                true, true,
                true, false
            )
        );

        // O2: Square the difference
        trace.push_back(
            make_op(
                id++,
                OpType::MUL,
                3, 3, 4,
                false, false,
                true, false
            )
        );

        // O3: Accumulate squared distance and store
        trace.push_back(
            make_op(
                id++,
                OpType::ADD,
                4, 5, 5,
                false, false,
                true, true
            )
        );

        // B: Loop-control branch
        trace.push_back(
            make_op(
                id++,
                OpType::BRANCH,
                5, 0, 0,
                false, false,
                true, false,
                true
            )
        );
    }

    // ---- real data and memory addresses: squared distance accumulated over features ----
    {
        const uint32_t it = static_cast<uint32_t>(iterations);
        for (uint32_t f = 0; f < it; ++f) {
            const size_t base = static_cast<size_t>(f) * 4;
            trace[base + 0].load1_addr = f;              // point[f]
            trace[base + 0].load2_addr = it + f;         // centroid[f]
            trace[base + 2].store_addr = 2 * it;         // running squared distance
        }
        if (data) {
            data->name = "kmeans (squared distance between one point and one centroid, sum over features)";
            data->memory.assign(2 * static_cast<size_t>(it) + 1, 0);
            std::vector<uint32_t> pa, ca;
            int32_t acc = 0;
            for (uint32_t f = 0; f < it; ++f) {
                data->memory[f] = pat(static_cast<int>(f), 7, 3, 41, 20);
                data->memory[it + f] = pat(static_cast<int>(f), 11, 5, 31, 15);
                pa.push_back(f); ca.push_back(it + f);
                int32_t d = data->memory[f] - data->memory[it + f];
                acc += d * d;
            }
            data->input_lines = {list_values(data->memory, "point[f]   ", pa), list_values(data->memory, "centroid[f]", ca),
                                 "Computation: distance^2 = sum over f of (point[f] - centroid[f])^2"};
            data->expected = {{2 * it, acc, "distance^2"}};
        }
    }

    return trace;
}
