
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
// Shared helpers for the loop-structured kernels (stencil, kmeans)
// ============================================================
//
// Like matmul, each kernel is a real loop:
//   r20 = 0            loop counter
//   r21 = trip count   loop limit
//   loop body ...
//   r20 = r20 + 1
//   BRANCH while r20 != r21   (real_branch, outcome checked by the simulator)
//
// The loop body handles POINTS_PER_ITER independent elements and reuses the SAME
// architectural registers for each of them (as a compiler would for an unrolled
// loop). That gives the four designs something to differ on: without renaming the
// reused registers create WAR/WAW hazards; with renaming they disappear; and the
// independent elements let memory and calculation phases overlap.

namespace {

constexpr int LOOP_COUNTER_REG = 20;
constexpr int LOOP_LIMIT_REG   = 21;

// Elements handled per loop iteration (the loop branch fences fetch, so this
// is how much independent work each design can overlap).
constexpr uint32_t POINTS_PER_ITER = 4;

// dest = r0 + #value
InSramOp make_const(uint64_t& id, int dest, int value) {
    InSramOp op = make_op(id++, OpType::ADD, 0, 0, dest, false, false, true, false);
    op.has_imm = true;
    op.imm_val = value;
    return op;
}

// counter++ and loop branch (taken while more iterations remain)
void push_loop_tail(std::vector<InSramOp>& trace, uint64_t& id, bool taken) {
    InSramOp inc = make_op(id++, OpType::ADD, LOOP_COUNTER_REG, 0, LOOP_COUNTER_REG,
                           false, false, true, false);
    inc.has_imm = true;
    inc.imm_val = 1;
    trace.push_back(inc);

    InSramOp br = make_op(id++, OpType::BRANCH, LOOP_COUNTER_REG, LOOP_LIMIT_REG, 0,
                          false, false, true, false, true);
    br.real_branch = true;
    br.branch_taken = taken;
    trace.push_back(br);
}

uint32_t loop_trips(uint32_t elements) {
    return (elements + POINTS_PER_ITER - 1) / POINTS_PER_ITER;
}

} // namespace

// ============================================================
// 2. STENCIL
// ============================================================
//
// out[p] = w * ( (N[p] + S[p]) + (E[p] + W[p]) ),  p = 0 .. iterations-1,  w in r9
//
// Per point p (POINTS_PER_ITER points per loop iteration):
//   O1: r5  = N[p] + S[p]          (2 loads, ADD)
//   O2: r6  = E[p] + W[p]          (2 loads, ADD)
//   O3: r7  = r5 + r6
//   O4: r10 = r7 * r9, store out[p] (MUL + store phase)
// then the loop tail (counter++, real branch).
//
// ============================================================

std::vector<InSramOp>
TraceGenerator::generate_stencil(int iterations, KernelData* data) {

    validate_iterations(iterations);

    const uint32_t pts = static_cast<uint32_t>(iterations);
    const uint32_t NB = 0, SB = pts, EB = 2 * pts, WB = 3 * pts, OB = 4 * pts;
    const int32_t weight = 3;
    const uint32_t trips = loop_trips(pts);

    // ---- real input grid ----
    std::vector<int32_t> mem(5 * static_cast<size_t>(pts), 0);
    const int mul[4] = {3, 5, 7, 11}, add[4] = {1, 2, 3, 4};
    for (int d = 0; d < 4; ++d)
        for (uint32_t p = 0; p < pts; ++p)
            mem[d * pts + p] = pat(static_cast<int>(p), mul[d], add[d], 13, 6);

    std::vector<InSramOp> trace;
    trace.reserve(static_cast<size_t>(pts) * 4 + trips * 2 + 2);

    uint64_t id = 1;

    trace.push_back(make_const(id, LOOP_LIMIT_REG, static_cast<int>(trips)));   // r21 = trips
    trace.push_back(make_const(id, LOOP_COUNTER_REG, 0));                      // r20 = 0

    for (uint32_t t = 0; t < trips; ++t) {
        for (uint32_t u = 0; u < POINTS_PER_ITER; ++u) {

            const uint32_t p = t * POINTS_PER_ITER + u;
            if (p >= pts)
                break;

            // O1: North + South
            InSramOp o1 = make_op(id++, OpType::ADD, 1, 2, 5, true, true, true, false);
            o1.load1_addr = NB + p;
            o1.load2_addr = SB + p;
            trace.push_back(o1);

            // O2: East + West
            InSramOp o2 = make_op(id++, OpType::ADD, 3, 4, 6, true, true, true, false);
            o2.load1_addr = EB + p;
            o2.load2_addr = WB + p;
            trace.push_back(o2);

            // O3: combine partial sums
            trace.push_back(make_op(id++, OpType::ADD, 5, 6, 7, false, false, true, false));

            // O4: apply weight and store out[p]
            InSramOp o4 = make_op(id++, OpType::MUL, 7, 9, 10, false, false, true, true);
            o4.store_addr = OB + p;
            trace.push_back(o4);
        }

        push_loop_tail(trace, id, t + 1 != trips);
    }

    if (data) {
        data->name = "stencil: out[p] = w * ((N[p] + S[p]) + (E[p] + W[p])), " +
                     std::to_string(pts) + " points, " + std::to_string(POINTS_PER_ITER) +
                     " points per loop iteration";
        data->memory = mem;
        data->init_reg[9] = weight;     // stencil weight lives in r9
        data->input_lines.clear();
        data->input_lines.push_back("weight (register r9) = " + std::to_string(weight));
        const char* nm[4] = {"North", "South", "East ", "West "};
        for (int d = 0; d < 4; ++d) {
            std::vector<uint32_t> ad;
            for (uint32_t p = 0; p < pts; ++p) ad.push_back(d * pts + p);
            data->input_lines.push_back(list_values(mem, nm[d], ad));
        }
        data->expected.clear();
        for (uint32_t p = 0; p < pts; ++p) {
            const int32_t v = weight * ((mem[NB + p] + mem[SB + p]) + (mem[EB + p] + mem[WB + p]));
            data->expected.push_back({OB + p, v, "out[" + std::to_string(p) + "]"});
        }
    }

    return trace;
}

// ============================================================
// 3. K-MEANS
// ============================================================
//
// distance^2 = sum over f of (point[f] - centroid[f])^2,  f = 0 .. iterations-1
//
// Per feature f (POINTS_PER_ITER features per loop iteration):
//   O1: r3 = point[f] - centroid[f]   (2 loads, SUB)
//   O2: r4 = r3 * r3                  (MUL)
//   O3: r5 = r5 + r4, store distance^2 (ADD + store phase)
// then the loop tail (counter++, real branch).
// r5 (the accumulator) is cleared once before the loop.
//
// ============================================================

std::vector<InSramOp>
TraceGenerator::generate_kmeans(int iterations, KernelData* data) {

    validate_iterations(iterations);

    const uint32_t feats = static_cast<uint32_t>(iterations);
    const uint32_t PB = 0, CB = feats, OUT = 2 * feats;
    const uint32_t trips = loop_trips(feats);

    // ---- real input vectors ----
    std::vector<int32_t> mem(2 * static_cast<size_t>(feats) + 1, 0);
    int32_t acc = 0;
    for (uint32_t f = 0; f < feats; ++f) {
        mem[PB + f] = pat(static_cast<int>(f), 7, 3, 41, 20);
        mem[CB + f] = pat(static_cast<int>(f), 11, 5, 31, 15);
        const int32_t d = mem[PB + f] - mem[CB + f];
        acc += d * d;
    }

    std::vector<InSramOp> trace;
    trace.reserve(static_cast<size_t>(feats) * 3 + trips * 2 + 3);

    uint64_t id = 1;

    trace.push_back(make_const(id, LOOP_LIMIT_REG, static_cast<int>(trips)));   // r21 = trips
    trace.push_back(make_const(id, LOOP_COUNTER_REG, 0));                      // r20 = 0
    trace.push_back(make_const(id, 5, 0));                                     // r5  = 0 (accumulator)

    for (uint32_t t = 0; t < trips; ++t) {
        for (uint32_t u = 0; u < POINTS_PER_ITER; ++u) {

            const uint32_t f = t * POINTS_PER_ITER + u;
            if (f >= feats)
                break;

            // O1: point - centroid
            InSramOp o1 = make_op(id++, OpType::SUB, 1, 2, 3, true, true, true, false);
            o1.load1_addr = PB + f;
            o1.load2_addr = CB + f;
            trace.push_back(o1);

            // O2: square the difference
            trace.push_back(make_op(id++, OpType::MUL, 3, 3, 4, false, false, true, false));

            // O3: accumulate and store the running distance^2
            InSramOp o3 = make_op(id++, OpType::ADD, 4, 5, 5, false, false, true, true);
            o3.store_addr = OUT;
            trace.push_back(o3);
        }

        push_loop_tail(trace, id, t + 1 != trips);
    }

    if (data) {
        data->name = "kmeans: distance^2 = sum_f (point[f] - centroid[f])^2, " +
                     std::to_string(feats) + " features, " + std::to_string(POINTS_PER_ITER) +
                     " features per loop iteration";
        data->memory = mem;
        std::vector<uint32_t> pa, ca;
        for (uint32_t f = 0; f < feats; ++f) { pa.push_back(PB + f); ca.push_back(CB + f); }
        data->input_lines = {list_values(mem, "point[f]   ", pa), list_values(mem, "centroid[f]", ca),
                             "Computation: distance^2 = sum over f of (point[f] - centroid[f])^2"};
        data->expected = {{OUT, acc, "distance^2"}};
    }

    return trace;
}
