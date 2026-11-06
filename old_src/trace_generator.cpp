
#include "trace_generator.hpp"

#include <cstdint>
#include <cstddef>
#include <stdexcept>
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

static void validate_iterations(int iterations) {

    if (iterations <= 0) {
        throw std::invalid_argument(
            "Number of iterations must be greater than zero."
        );
    }
}

// ============================================================
// 1. MATRIX MULTIPLICATION
// ============================================================
//
// Simplified inner-product operation trace.
//
// O1: Load A1 and B1; calculate A1 * B1.
// O2: Load A2 and B2; calculate A2 * B2.
// O3: Add the two multiplication results.
// O4: Accumulate the partial result and store output.
// B : Loop-control branch.
//
// Five operations per iteration.
//
// ============================================================

std::vector<InSramOp>
TraceGenerator::generate_matmul(int iterations) {

    validate_iterations(iterations);

    std::vector<InSramOp> trace;
    trace.reserve(static_cast<size_t>(iterations) * 5);

    uint64_t id = 1;

    for (int i = 0; i < iterations; ++i) {

        // O1: A1 * B1
        trace.push_back(
            make_op(
                id++,
                OpType::MUL,
                1, 2, 3,
                true, true,
                true, false
            )
        );

        // O2: A2 * B2
        trace.push_back(
            make_op(
                id++,
                OpType::MUL,
                4, 5, 6,
                true, true,
                true, false
            )
        );

        // O3: Partial sum
        // Operands are already available in wordlines.
        trace.push_back(
            make_op(
                id++,
                OpType::ADD,
                3, 6, 7,
                false, false,
                true, false
            )
        );

        // O4: Accumulate and store the output.
        trace.push_back(
            make_op(
                id++,
                OpType::ADD,
                7, 8, 8,
                false, false,
                true, true
            )
        );

        // B: Loop-control branch
        trace.push_back(
            make_op(
                id++,
                OpType::BRANCH,
                8, 0, 0,
                false, false,
                true, false,
                true
            )
        );
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
TraceGenerator::generate_stencil(int iterations) {

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
TraceGenerator::generate_kmeans(int iterations) {

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

    return trace;
}
