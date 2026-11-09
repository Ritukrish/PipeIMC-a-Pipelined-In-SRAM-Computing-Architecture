
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
// 1. MATRIX MULTIPLICATION (bounded output tile, independent reduction length)
// ============================================================
//
// C[i][j] = sum over k of ( A[i][k]*B[k][j] + A[k][j]*B[i][k] )   (paper Fig. 2 pattern)
//
// The trace contains the in-SRAM operations of EVERY output element C[i][j], one after the
// other. Each operation is an InSramOp built with make_op(), carrying the real numbers it
// works on (load values, immediates) and the memory words it touches.
//
// For each output element in the tile:
//   Z1: r8 = 0           accumulator reset         (ADD r0, #0)
//   then for k = 0 .. reduction_iterations-1 (statically unrolled in the trace):
//   O1: r3 = A[i][k] * B[k][j]      (2 loads, MUL)
//   O2: r6 = A[k][j] * B[i][k]      (2 loads, MUL)
//   O3: r7 = r3 + r6
//   O4: r8 = r8 + r7, store C[i][j]  (ADD + store phase)
//
// ============================================================

std::vector<InSramOp>
TraceGenerator::generate_matmul(
    int output_dimension,
    KernelData* data,
    int source_dimension,
    int reduction_iterations
) {

    validate_iterations(output_dimension);

    if (output_dimension > 16) {
        throw std::invalid_argument(
            "matmul output tile is limited to 16 x 16."
        );
    }
    if (reduction_iterations < 0) {
        throw std::invalid_argument(
            "matmul reduction iterations cannot be negative."
        );
    }
    if (source_dimension < 0) {
        throw std::invalid_argument(
            "matmul source dimension cannot be negative."
        );
    }

    const uint32_t n = static_cast<uint32_t>(output_dimension);
    const uint32_t k_count = static_cast<uint32_t>(
        reduction_iterations == 0 ? output_dimension : reduction_iterations
    );
    const uint32_t source_n = static_cast<uint32_t>(
        source_dimension == 0
            ? std::max<uint32_t>(n, k_count)
            : source_dimension
    );
    if (source_n < n || source_n < k_count) {
        throw std::invalid_argument(
            "matmul source dimension must cover the output tile and reduction."
        );
    }

    const uint32_t nn = n * n;
    const uint32_t matrix_words = source_n * source_n;
    const uint32_t A = 0, B = matrix_words, C = 2 * matrix_words;

    std::vector<int32_t> mem(
        2 * static_cast<size_t>(matrix_words) + nn, 0
    );
    const auto a_value = [&](uint32_t row, uint32_t col) {
        return pat(static_cast<int>(row * source_n + col), 7, 3, 11, 5);
    };
    const auto b_value = [&](uint32_t row, uint32_t col) {
        return pat(static_cast<int>(row * source_n + col), 5, 1, 9, 4);
    };

    std::vector<InSramOp> trace;
    trace.reserve(static_cast<size_t>(nn) * (k_count * 4 + 1));

    uint64_t id = 1;
    auto set_const = [&](int dest, int value) {
        InSramOp op = make_op(id++, OpType::ADD, 0, 0, dest,
                              false, false, true, false);
        op.has_imm = true;
        op.imm_val = value;
        trace.push_back(op);
    };

    if (data) {
        data->expected.clear();
    }

    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {

            set_const(8, 0);                   // Z1: accumulator = 0

            int32_t sum = 0;

            for (uint32_t k = 0; k < k_count; ++k) {

                const uint32_t a1 = A + i * source_n + k;
                const uint32_t b1 = B + k * source_n + j;
                const uint32_t a2 = A + k * source_n + j;
                const uint32_t b2 = B + i * source_n + k;
                mem[a1] = a_value(i, k);
                mem[b1] = b_value(k, j);
                mem[a2] = a_value(k, j);
                mem[b2] = b_value(i, k);

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

                sum += a_value(i, k) * b_value(k, j) +
                       a_value(k, j) * b_value(i, k);
            }

            if (data)
                data->expected.push_back({C + i * n + j, sum,
                    "C[" + std::to_string(i) + "][" + std::to_string(j) + "]"});
        }
    }

    if (data) {
        data->name = "matmul: " + std::to_string(n) + " x " +
                     std::to_string(n) + " output tile, " +
                     std::to_string(k_count) +
                     " reduction iterations, C[i][j] = sum_k ( A[i][k]*B[k][j] + A[k][j]*B[i][k] )";
        data->memory = mem;
        data->input_lines.clear();
        for (uint32_t r = 0; r < n; ++r) {
            std::string la = "A row " + std::to_string(r) + ":", lb = "B row " + std::to_string(r) + ":";
            for (uint32_t c = 0; c < std::min<uint32_t>(k_count, 16); ++c) {
                la += " " + std::to_string(a_value(r, c));
                lb += " " + std::to_string(b_value(r, c));
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

std::vector<InSramOp>
TraceGenerator::generate_stencil3d_sample(
    int sample_points,
    int volume_dimension,
    KernelData* data
) {
    validate_iterations(sample_points);
    if (volume_dimension < 3) {
        throw std::invalid_argument("3D stencil volume dimension must be at least 3.");
    }

    const uint32_t dim = static_cast<uint32_t>(volume_dimension);
    const uint32_t sample_count = static_cast<uint32_t>(sample_points);
    const uint32_t interior = dim - 2;
    const uint64_t interior_points =
        static_cast<uint64_t>(interior) * interior * interior;
    if (sample_count > interior_points) {
        throw std::invalid_argument(
            "3D stencil sample cannot exceed the number of interior volume points."
        );
    }
    if (sample_count % POINTS_PER_ITER != 0) {
        throw std::invalid_argument(
            "3D stencil sample point count must be divisible by four loop elements."
        );
    }

    const size_t sample_size = sample_count;
    const uint32_t output_base = 6 * sample_count;
    std::vector<int32_t> mem(7 * sample_size, 0);
    std::vector<InSramOp> trace;
    trace.reserve(sample_size * 7);
    uint64_t id = 1;
    constexpr int32_t weight = 3;

    if (data)
        data->expected.clear();

    for (uint32_t p = 0; p < sample_count; ++p) {
        const uint32_t x = 1 + p % interior;
        const uint32_t y = 1 + (p / interior) % interior;
        const uint32_t z = 1 + (p / (interior * interior)) % interior;
        const uint32_t center = (z * dim + y) * dim + x;
        const uint32_t neighbors[6] = {
            center - dim * dim, center + dim * dim,
            center - dim, center + dim,
            center - 1, center + 1
        };
        int32_t values[6] = {};
        int32_t expected = 0;

        for (uint32_t d = 0; d < 6; ++d) {
            values[d] = pat(
                static_cast<int>(neighbors[d] + d * dim * dim * dim),
                3 + static_cast<int>(d),
                1 + static_cast<int>(d),
                17,
                8
            );
            mem[d * sample_size + p] = values[d];
        }

        for (uint32_t pair = 0; pair < 3; ++pair) {
            const uint32_t first = pair * 2;
            const int dest = 5 + static_cast<int>(pair);
            InSramOp op = make_op(id++, OpType::ADD, 1, 2, dest,
                                  true, true, true, false);
            op.load1_addr = first * sample_count + p;
            op.load2_addr = (first + 1) * sample_count + p;
            op.load1_val = values[first];
            op.load2_val = values[first + 1];
            op.has_load_vals = true;
            trace.push_back(op);
            expected += values[first] + values[first + 1];
        }

        trace.push_back(make_op(id++, OpType::ADD, 5, 6, 8,
                                false, false, true, false));
        trace.push_back(make_op(id++, OpType::ADD, 8, 7, 9,
                                false, false, true, false));

        InSramOp scale = make_op(id++, OpType::MUL, 9, 11, 10,
                                 false, false, true, true);
        scale.store_addr = output_base + p;
        trace.push_back(scale);
        expected *= weight;
        if (data)
            data->expected.push_back(
                {output_base + p, expected, "out[" + std::to_string(p) + "]"}
            );
    }

    if (data) {
        data->name = "sampled 3D six-neighbor stencil";
        data->memory = std::move(mem);
        data->init_reg[11] = weight;
        data->reference_workload_shape =
            std::to_string(volume_dimension) + " x " +
            std::to_string(volume_dimension) + " x " +
            std::to_string(volume_dimension) + " volume";
        data->simulated_sample_shape =
            std::to_string(sample_points) +
            " interior points (" +
            std::to_string(sample_points / POINTS_PER_ITER) +
            " batches of four)";
        data->sample_note =
            "Bounded spatial sample; points are grouped in four per loop trip; not a full-volume run.";
        data->input_lines = {
            "Sampled 3D points: " + std::to_string(sample_points),
            "Stencil neighbors: +/-X, +/-Y, +/-Z",
            "Weight: " + std::to_string(weight)
        };
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

std::vector<InSramOp>
TraceGenerator::generate_kmeans_sample(
    int sample_points,
    int features,
    KernelData* data
) {
    validate_iterations(sample_points);
    validate_iterations(features);
    if (features % POINTS_PER_ITER != 0) {
        throw std::invalid_argument(
            "kmeans sample features must be divisible by four loop elements."
        );
    }

    const size_t points = static_cast<size_t>(sample_points);
    const size_t feature_count = static_cast<size_t>(features);
    const size_t total_features = points * feature_count;
    const uint32_t centroid_base = static_cast<uint32_t>(total_features);
    const uint32_t output_base = static_cast<uint32_t>(2 * total_features);
    std::vector<int32_t> mem(2 * total_features + points, 0);
    std::vector<InSramOp> trace;
    trace.reserve(points * (1 + feature_count * 3));
    uint64_t id = 1;

    if (data)
        data->expected.clear();

    for (size_t point = 0; point < points; ++point) {
        InSramOp reset = make_op(id++, OpType::ADD, 0, 0, 5,
                                 false, false, true, false);
        reset.has_imm = true;
        reset.imm_val = 0;
        trace.push_back(reset);

        int32_t distance = 0;
        for (size_t feature = 0; feature < feature_count; ++feature) {
            const size_t index = point * feature_count + feature;
            mem[index] = pat(static_cast<int>(index), 7, 3, 41, 20);
            mem[centroid_base + index] =
                pat(static_cast<int>(index), 11, 5, 31, 15);
            const int32_t difference =
                mem[index] - mem[centroid_base + index];
            distance += difference * difference;

            InSramOp subtract = make_op(id++, OpType::SUB, 1, 2, 3,
                                        true, true, true, false);
            subtract.load1_addr = static_cast<uint32_t>(index);
            subtract.load2_addr =
                centroid_base + static_cast<uint32_t>(index);
            trace.push_back(subtract);
            trace.push_back(make_op(id++, OpType::MUL, 3, 3, 4,
                                    false, false, true, false));

            InSramOp accumulate = make_op(id++, OpType::ADD, 4, 5, 5,
                                          false, false, true, true);
            accumulate.store_addr =
                output_base + static_cast<uint32_t>(point);
            trace.push_back(accumulate);
        }

        if (data)
            data->expected.push_back(
                {output_base + static_cast<uint32_t>(point), distance,
                 "distance^2[" + std::to_string(point) + "]"}
            );
    }

    if (data) {
        data->name = "sampled k-means squared distance";
        data->memory = std::move(mem);
        data->reference_workload_shape = "8192 points x 16 features";
        data->simulated_sample_shape =
            std::to_string(sample_points) + " points x " +
            std::to_string(features) + " features (" +
            std::to_string(features / POINTS_PER_ITER) +
            " batches of four per point)";
        data->sample_note =
            "First points/features from deterministic input; each point has 256 batches of four features.";
        data->input_lines = {
            "Sampled points: " + std::to_string(sample_points),
            "Features per point: " + std::to_string(features),
            "Only the first point's first 16 input values are displayed."
        };
        std::vector<uint32_t> point_addresses;
        std::vector<uint32_t> centroid_addresses;
        const size_t shown = std::min<size_t>(feature_count, 16);
        for (size_t feature = 0; feature < shown; ++feature) {
            point_addresses.push_back(static_cast<uint32_t>(feature));
            centroid_addresses.push_back(
                centroid_base + static_cast<uint32_t>(feature)
            );
        }
        data->input_lines.push_back(
            list_values(data->memory, "point[0]", point_addresses)
        );
        data->input_lines.push_back(
            list_values(data->memory, "centroid", centroid_addresses)
        );
    }
    return trace;
}

std::vector<InSramOp>
TraceGenerator::generate_matvec_sample(
    int sample_dimension,
    int matrix_dimension,
    KernelData* data
) {
    validate_iterations(sample_dimension);
    if (sample_dimension > 256 || matrix_dimension < sample_dimension) {
        throw std::invalid_argument(
            "matvec requires a sample dimension <= 256 and no larger than the paper matrix dimension."
        );
    }

    const uint32_t sample = static_cast<uint32_t>(sample_dimension);
    const uint32_t source = static_cast<uint32_t>(matrix_dimension);
    const size_t matrix_words = static_cast<size_t>(sample) * sample;
    const uint32_t vector_base = static_cast<uint32_t>(matrix_words);
    const uint32_t output_base =
        vector_base + static_cast<uint32_t>(sample);
    std::vector<int32_t> mem(matrix_words + sample + sample, 0);
    std::vector<InSramOp> trace;
    trace.reserve(static_cast<size_t>(sample) * (1 + sample * 2));
    uint64_t id = 1;

    if (data)
        data->expected.clear();

    for (uint32_t row = 0; row < sample; ++row) {
        InSramOp reset = make_op(id++, OpType::ADD, 0, 0, 5,
                                 false, false, true, false);
        reset.has_imm = true;
        reset.imm_val = 0;
        trace.push_back(reset);

        int32_t expected = 0;
        for (uint32_t col = 0; col < sample; ++col) {
            const uint32_t matrix_index = row * sample + col;
            const int source_index = static_cast<int>(row * source + col);
            const int32_t matrix_value = pat(source_index, 7, 3, 17, 8);
            const int32_t vector_value = pat(static_cast<int>(col), 5, 1, 13, 6);
            mem[matrix_index] = matrix_value;
            mem[vector_base + col] = vector_value;
            expected += matrix_value * vector_value;

            InSramOp multiply = make_op(id++, OpType::MUL, 1, 2, 3,
                                        true, true, true, false);
            multiply.load1_addr = matrix_index;
            multiply.load2_addr = vector_base + col;
            multiply.load1_val = matrix_value;
            multiply.load2_val = vector_value;
            multiply.has_load_vals = true;
            trace.push_back(multiply);

            InSramOp accumulate = make_op(id++, OpType::ADD, 3, 5, 5,
                                          false, false, true, true);
            accumulate.store_addr = output_base + row;
            trace.push_back(accumulate);
        }

        if (data)
            data->expected.push_back(
                {output_base + row, expected, "y[" + std::to_string(row) + "]"}
            );
    }

    if (data) {
        data->name = "sampled matrix-vector multiplication";
        data->memory = std::move(mem);
        data->reference_workload_shape =
            std::to_string(matrix_dimension) + " x " +
            std::to_string(matrix_dimension) + " matrix";
        data->simulated_sample_shape =
            std::to_string(sample_dimension) + " x " +
            std::to_string(sample_dimension) + " matrix tile";
        data->sample_note =
            "Top-left matrix tile with source row stride preserved; not a full matrix run.";
        data->input_lines = {
            "Sampled matrix tile: " + std::to_string(sample_dimension) +
                " x " + std::to_string(sample_dimension),
            "Input vector length: " + std::to_string(sample_dimension)
        };
        std::vector<uint32_t> matrix_addresses;
        std::vector<uint32_t> vector_addresses;
        const uint32_t shown = std::min<uint32_t>(sample, 16);
        for (uint32_t index = 0; index < shown; ++index) {
            matrix_addresses.push_back(index);
            vector_addresses.push_back(vector_base + index);
        }
        data->input_lines.push_back(
            list_values(data->memory, "matrix row 0", matrix_addresses)
        );
        data->input_lines.push_back(
            list_values(data->memory, "vector", vector_addresses)
        );
    }
    return trace;
}

std::vector<InSramOp>
TraceGenerator::generate_backprop_sample(int sample_elements, KernelData* data) {
    validate_iterations(sample_elements);
    if (sample_elements > 256) {
        throw std::invalid_argument(
            "backprop sample is limited to 256 elements."
        );
    }

    const uint32_t count = static_cast<uint32_t>(sample_elements);
    const uint32_t input_base = 0;
    const uint32_t target_base = count;
    const uint32_t weight_base = 2 * count;
    const uint32_t output_base = 3 * count;
    std::vector<int32_t> mem(4 * static_cast<size_t>(count), 0);
    std::vector<InSramOp> trace;
    trace.reserve(static_cast<size_t>(count) * 5);
    uint64_t id = 1;
    constexpr int32_t learning_rate = 1;

    if (data)
        data->expected.clear();

    for (uint32_t index = 0; index < count; ++index) {
        const int32_t input = pat(static_cast<int>(index), 7, 3, 19, 9);
        const int32_t target = pat(static_cast<int>(index), 5, 2, 11, 5);
        const int32_t weight = pat(static_cast<int>(index), 3, 1, 13, 6);
        mem[input_base + index] = input;
        mem[target_base + index] = target;
        mem[weight_base + index] = weight;

        const int32_t prediction = input * weight;
        const int32_t error = target - prediction;
        const int32_t gradient = error * input;
        const int32_t updated_weight = weight + learning_rate * gradient;

        InSramOp forward = make_op(id++, OpType::MUL, 1, 2, 3,
                                   true, true, true, false);
        forward.load1_addr = input_base + index;
        forward.load2_addr = weight_base + index;
        forward.load1_val = input;
        forward.load2_val = weight;
        forward.has_load_vals = true;
        trace.push_back(forward);

        InSramOp error_op = make_op(id++, OpType::SUB, 4, 3, 4,
                                    true, false, true, false);
        error_op.load1_addr = target_base + index;
        error_op.load1_val = target;
        error_op.has_load_vals = true;
        trace.push_back(error_op);

        InSramOp gradient_op = make_op(id++, OpType::MUL, 1, 4, 5,
                                       true, false, true, false);
        gradient_op.load1_addr = input_base + index;
        gradient_op.load1_val = input;
        gradient_op.has_load_vals = true;
        trace.push_back(gradient_op);

        InSramOp scale = make_op(id++, OpType::MUL, 5, 0, 6,
                                 false, false, true, false);
        scale.has_imm = true;
        scale.imm_val = learning_rate;
        trace.push_back(scale);

        InSramOp update = make_op(id++, OpType::ADD, 7, 6, 7,
                                  true, false, true, true);
        update.load1_addr = weight_base + index;
        update.load1_val = weight;
        update.has_load_vals = true;
        update.store_addr = output_base + index;
        trace.push_back(update);

        if (data)
            data->expected.push_back(
                {output_base + index, updated_weight,
                 "updated_weight[" + std::to_string(index) + "]"}
            );
    }

    if (data) {
        data->name = "sampled one-layer backpropagation weight update";
        data->memory = std::move(mem);
        data->reference_workload_shape = "16384 backprop elements";
        data->simulated_sample_shape =
            std::to_string(sample_elements) + " independent elements";
        data->sample_note =
            "Simplified linear-neuron gradient update; does not reproduce the full Rodinia network.";
        std::vector<uint32_t> input_addresses;
        std::vector<uint32_t> target_addresses;
        for (uint32_t index = 0; index < std::min<uint32_t>(count, 16); ++index) {
            input_addresses.push_back(input_base + index);
            target_addresses.push_back(target_base + index);
        }
        data->input_lines = {
            "Learning rate: " + std::to_string(learning_rate),
            list_values(data->memory, "input", input_addresses),
            list_values(data->memory, "target", target_addresses)
        };
    }
    return trace;
}
