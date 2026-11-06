#include "functional.hpp"
#include "trace_generator.hpp"
#include "sim_engine.hpp"

#include <cstdint>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {

enum class FOp { ADD, SUB, MUL, DIV, AND, SLL, SLT };

// dest := IMC(src1, src2, op)
//   m1/m2 : the register holds a memory pointer, the operand is the word it points to (load phase)
//   md    : the register rd holds a memory pointer, the result is stored there (store phase)
//   imm   : src2 is the constant imm_val.   r0 is always 0.
struct Instr {
    bool branch = false;
    FOp op = FOp::ADD;
    int rd = 0, rs1 = 0, rs2 = 0;
    bool m1 = false, m2 = false, md = false, imm = false;
    int32_t imm_val = 0;
    size_t target = 0;               // branch: jump here if rs1 != rs2
};

const char* name(FOp o) {
    switch (o) {
        case FOp::ADD: return "ADD"; case FOp::SUB: return "SUB"; case FOp::MUL: return "MUL";
        case FOp::DIV: return "DIV"; case FOp::AND: return "AND"; case FOp::SLL: return "SLL";
        case FOp::SLT: return "SLT";
    }
    return "?";
}

OpType timing_type(FOp o) {
    switch (o) {
        case FOp::SUB: return OpType::SUB;
        case FOp::MUL: return OpType::MUL;
        case FOp::DIV: return OpType::DIV;
        case FOp::AND: case FOp::SLL: case FOp::SLT: return OpType::LOGIC;
        default: return OpType::ADD;
    }
}

std::string text(const Instr& in) {
    auto r = [](int x) { return "r" + std::to_string(x); };
    if (in.branch) return "BRANCH(" + r(in.rs1) + " != " + r(in.rs2) + ") -> pc" + std::to_string(in.target);
    std::string a = in.m1 ? "[" + r(in.rs1) + "]" : r(in.rs1);
    std::string b = in.imm ? "#" + std::to_string(in.imm_val) : (in.m2 ? "[" + r(in.rs2) + "]" : r(in.rs2));
    std::string d = in.md ? "[" + r(in.rd) + "]" : r(in.rd);
    return d + " := IMC(" + a + ", " + b + ", " + name(in.op) + ")";
}

uint32_t alu(FOp op, uint32_t a, uint32_t b) {
    int32_t sa = static_cast<int32_t>(a), sb = static_cast<int32_t>(b);
    switch (op) {
        case FOp::ADD: return a + b;
        case FOp::SUB: return a - b;
        case FOp::MUL: return a * b;
        case FOp::DIV: return b == 0 ? 0xFFFFFFFFu : static_cast<uint32_t>(sa / sb);
        case FOp::AND: return a & b;
        case FOp::SLL: return a << (b & 31u);
        case FOp::SLT: return sa < sb ? 1u : 0u;
    }
    return 0;
}

struct Builder {
    std::vector<Instr> p;
    void op(FOp o, int rd, int a, int b) { Instr i; i.op = o; i.rd = rd; i.rs1 = a; i.rs2 = b; p.push_back(i); }
    void opi(FOp o, int rd, int a, int32_t v) { Instr i; i.op = o; i.rd = rd; i.rs1 = a; i.imm = true; i.imm_val = v; p.push_back(i); }
    void ld2(FOp o, int rd, int a, int b) { Instr i; i.op = o; i.rd = rd; i.rs1 = a; i.rs2 = b; i.m1 = i.m2 = true; p.push_back(i); }
    void ldi(FOp o, int rd, int a, int32_t v) { Instr i; i.op = o; i.rd = rd; i.rs1 = a; i.m1 = true; i.imm = true; i.imm_val = v; p.push_back(i); }
    void st(int ptr, int a) { Instr i; i.op = FOp::ADD; i.rd = ptr; i.rs1 = a; i.imm = true; i.imm_val = 0; i.md = true; p.push_back(i); }
    void br(int a, int b, size_t t) { Instr i; i.branch = true; i.rs1 = a; i.rs2 = b; i.target = t; p.push_back(i); }
};


// One kernel: the IMC program, its initial memory, how each output element is started, and the host check.
struct Program {
    std::string name;
    std::vector<Instr> code;
    std::vector<uint32_t> mem_init;
    size_t elements = 0;
    std::function<void(uint32_t*, size_t)> setup;                                   // set index registers of element e
    std::function<void(const std::vector<uint32_t>&, size_t&, size_t&)> check;      // checked, wrong
    std::function<std::string(const std::vector<uint32_t>&)> sample;                // first computed outputs
};

int32_t pat(int k, int mul, int add, int mod, int off) { return (k * mul + add) % mod - off; }

Program make_matmul(int n) {
    const uint32_t nn = n * n, A = 0, B = 4 * nn, C = 8 * nn;
    Program P; P.name = "matmul"; P.elements = nn; P.mem_init.assign(3 * nn, 0);
    for (uint32_t x = 0; x < nn; ++x) { P.mem_init[A / 4 + x] = pat(x, 7, 3, 11, 5); P.mem_init[B / 4 + x] = pat(x, 5, 1, 9, 4); }
    const int SUM = 1, W1 = 2, W2 = 3, W3 = 4, K = 5, LIM = 6, PA1 = 7, PB1 = 8, PA2 = 9, PB2 = 10, PC = 11, I4N = 14, J4 = 15, TI = 30, TJ = 31;
    const int32_t row = 4 * n;
    Builder b;
    b.opi(FOp::ADD, LIM, 0, n);
    b.opi(FOp::ADD, K, 0, 0);
    b.opi(FOp::ADD, SUM, 0, 0);
    b.opi(FOp::MUL, I4N, TI, row);
    b.opi(FOp::SLL, J4, TJ, 2);
    b.opi(FOp::ADD, PA1, I4N, A);
    b.opi(FOp::ADD, PB1, J4, B);
    b.opi(FOp::ADD, PA2, J4, A);
    b.opi(FOp::ADD, PB2, I4N, B);
    b.op(FOp::ADD, PC, I4N, J4);
    b.opi(FOp::ADD, PC, PC, C);
    size_t loop = b.p.size();
    b.ld2(FOp::MUL, W1, PA1, PB1);
    b.ld2(FOp::MUL, W2, PA2, PB2);
    b.op(FOp::ADD, W3, W1, W2);
    b.op(FOp::ADD, SUM, SUM, W3);
    b.opi(FOp::ADD, PA1, PA1, 4);
    b.opi(FOp::ADD, PB1, PB1, row);
    b.opi(FOp::ADD, PA2, PA2, row);
    b.opi(FOp::ADD, PB2, PB2, 4);
    b.opi(FOp::ADD, K, K, 1);
    b.br(K, LIM, loop);
    b.st(PC, SUM);
    P.code = b.p;
    P.setup = [=](uint32_t* r, size_t e) { r[TI] = e / n; r[TJ] = e % n; };
    P.check = [=](const std::vector<uint32_t>& mem, size_t& checked, size_t& wrong) {
        for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) {
            uint32_t s = 0;
            for (int k = 0; k < n; ++k) s += mem[A / 4 + i * n + k] * mem[B / 4 + k * n + j] + mem[A / 4 + k * n + j] * mem[B / 4 + i * n + k];
            ++checked; if (mem[C / 4 + i * n + j] != s) ++wrong;
        }
    };
    P.sample = [=](const std::vector<uint32_t>& mem) {
        std::string t = "C[0][0..]:"; for (int j = 0; j < std::min(n, 6); ++j) t += " " + std::to_string(static_cast<int32_t>(mem[C / 4 + j])); return t; };
    return P;
}

Program make_stencil(int n) {
    const int X = n, Y = n, Z = n, SX = X + 2, SY = Y + 2, SZ = Z + 2;
    const uint32_t vol = SX * SY * SZ, IN = 0, OUT = 4 * vol;
    Program P; P.name = "stencil"; P.elements = static_cast<size_t>(X) * Y; P.mem_init.assign(2 * static_cast<size_t>(vol), 0);
    for (int z = 1; z <= Z; ++z) for (int y = 1; y <= Y; ++y) for (int x = 1; x <= X; ++x)
        P.mem_init[IN / 4 + (z * SY + y) * SX + x] = pat(x * 7 + y * 5 + z * 3, 1, 0, 13, 6);
    const int K = 1, LIM = 2, W1 = 3, W2 = 4, W3 = 5, W4 = 6, W5 = 7, W6 = 8, W7 = 9, XM = 10, XP = 11, YM = 12, YP = 13, ZM = 14, ZP = 15,
              PC = 16, PO = 17, T1 = 18, T2 = 19, RY = 30, RX = 31;
    const int32_t rowB = 4 * SX, planeB = 4 * SX * SY;
    Builder b;
    b.opi(FOp::ADD, LIM, 0, Z);
    b.opi(FOp::ADD, K, 0, 0);
    b.opi(FOp::MUL, T1, RY, rowB);
    b.opi(FOp::SLL, T2, RX, 2);
    b.op(FOp::ADD, PC, T1, T2);
    b.opi(FOp::ADD, PC, PC, static_cast<int32_t>(IN) + planeB + rowB + 4);
    b.opi(FOp::ADD, XM, PC, -4);
    b.opi(FOp::ADD, XP, PC, 4);
    b.opi(FOp::ADD, YM, PC, -rowB);
    b.opi(FOp::ADD, YP, PC, rowB);
    b.opi(FOp::ADD, ZM, PC, -planeB);
    b.opi(FOp::ADD, ZP, PC, planeB);
    b.opi(FOp::ADD, PO, PC, static_cast<int32_t>(OUT - IN));
    size_t loop = b.p.size();
    b.ld2(FOp::ADD, W1, XM, XP);
    b.ld2(FOp::ADD, W2, YM, YP);
    b.ld2(FOp::ADD, W3, ZM, ZP);
    b.op(FOp::ADD, W4, W1, W2);
    b.op(FOp::ADD, W5, W4, W3);
    b.opi(FOp::MUL, W6, W5, 2);
    b.ldi(FOp::MUL, W7, PC, 3);
    b.op(FOp::ADD, W6, W6, W7);
    b.st(PO, W6);
    for (int r : {XM, XP, YM, YP, ZM, ZP, PC, PO}) b.opi(FOp::ADD, r, r, planeB);
    b.opi(FOp::ADD, K, K, 1);
    b.br(K, LIM, loop);
    P.code = b.p;
    P.setup = [=](uint32_t* r, size_t e) { r[RY] = e / X; r[RX] = e % X; };
    P.check = [=](const std::vector<uint32_t>& mem, size_t& checked, size_t& wrong) {
        auto at = [&](int z, int y, int x) { return mem[IN / 4 + (z * SY + y) * SX + x]; };
        for (int z = 1; z <= Z; ++z) for (int y = 1; y <= Y; ++y) for (int x = 1; x <= X; ++x) {
            uint32_t ref = 2 * (at(z, y, x - 1) + at(z, y, x + 1) + at(z, y - 1, x) + at(z, y + 1, x) + at(z - 1, y, x) + at(z + 1, y, x)) + 3 * at(z, y, x);
            ++checked; if (mem[OUT / 4 + (z * SY + y) * SX + x] != ref) ++wrong;
        }
    };
    P.sample = [=](const std::vector<uint32_t>& mem) {
        std::string t = "out[z=1][y=1][x=1..]:"; for (int x = 1; x <= std::min(X, 6); ++x) t += " " + std::to_string(static_cast<int32_t>(mem[OUT / 4 + (1 * SY + 1) * SX + x])); return t; };
    return P;
}

Program make_kmeans(int n) {
    const int P_ = n * n, F = 8, Cn = 4;
    const uint32_t PT = 0, CENT = 4u * F * P_, OUT = CENT + 4u * Cn * F;
    Program P; P.name = "kmeans"; P.elements = P_; P.mem_init.assign(static_cast<size_t>(F) * P_ + Cn * F + P_, 0);
    for (int f = 0; f < F; ++f) for (int p = 0; p < P_; ++p) P.mem_init[PT / 4 + f * P_ + p] = pat(p * 7 + f * 11 + p * f, 1, 0, 41, 20);
    for (int c = 0; c < Cn; ++c) for (int f = 0; f < F; ++f) P.mem_init[CENT / 4 + c * F + f] = pat(c * 13 + f * 5, 1, 3, 31, 15);
    const int ACC = 1, D1 = 2, S1 = 3, D2 = 4, S2 = 5, SS = 6, FC = 7, LIMF = 8, CC = 9, LIMC = 10, BEST = 11, IDX = 12, MASK = 13, M2 = 14,
              TMP = 15, PF1 = 16, PF2 = 17, PC1 = 18, PC2 = 19, B1 = 20, B2 = 21, PO = 22, RP = 31;
    Builder b;
    b.opi(FOp::ADD, LIMF, 0, F / 2);
    b.opi(FOp::ADD, LIMC, 0, Cn);
    b.opi(FOp::ADD, BEST, 0, 0x7FFFFFFF);
    b.opi(FOp::ADD, IDX, 0, 0);
    b.opi(FOp::ADD, CC, 0, 0);
    b.opi(FOp::SLL, TMP, RP, 2);
    b.opi(FOp::ADD, B1, TMP, PT);
    b.opi(FOp::ADD, B2, TMP, PT + 4 * P_);
    b.opi(FOp::ADD, PO, TMP, OUT);
    b.opi(FOp::ADD, PC1, 0, CENT);
    b.opi(FOp::ADD, PC2, 0, CENT + 4);
    size_t outer = b.p.size();
    b.opi(FOp::ADD, ACC, 0, 0);
    b.opi(FOp::ADD, FC, 0, 0);
    b.opi(FOp::ADD, PF1, B1, 0);
    b.opi(FOp::ADD, PF2, B2, 0);
    size_t inner = b.p.size();
    b.ld2(FOp::SUB, D1, PF1, PC1);
    b.op(FOp::MUL, S1, D1, D1);
    b.ld2(FOp::SUB, D2, PF2, PC2);
    b.op(FOp::MUL, S2, D2, D2);
    b.op(FOp::ADD, SS, S1, S2);
    b.op(FOp::ADD, ACC, ACC, SS);
    b.opi(FOp::ADD, PF1, PF1, 8 * P_);
    b.opi(FOp::ADD, PF2, PF2, 8 * P_);
    b.opi(FOp::ADD, PC1, PC1, 8);
    b.opi(FOp::ADD, PC2, PC2, 8);
    b.opi(FOp::ADD, FC, FC, 1);
    b.br(FC, LIMF, inner);
    b.op(FOp::SLT, MASK, ACC, BEST);
    b.op(FOp::SUB, M2, 0, MASK);
    b.op(FOp::SUB, TMP, ACC, BEST);
    b.op(FOp::AND, TMP, TMP, M2);
    b.op(FOp::ADD, BEST, BEST, TMP);
    b.op(FOp::SUB, TMP, CC, IDX);
    b.op(FOp::AND, TMP, TMP, M2);
    b.op(FOp::ADD, IDX, IDX, TMP);
    b.opi(FOp::ADD, CC, CC, 1);
    b.br(CC, LIMC, outer);
    b.st(PO, IDX);
    P.code = b.p;
    P.setup = [=](uint32_t* r, size_t e) { r[RP] = static_cast<uint32_t>(e); };
    P.check = [=](const std::vector<uint32_t>& mem, size_t& checked, size_t& wrong) {
        for (int p = 0; p < P_; ++p) {
            int32_t best = INT32_MAX; uint32_t idx = 0;
            for (int c = 0; c < Cn; ++c) {
                uint32_t acc = 0;
                for (int f = 0; f < F; ++f) { uint32_t d = mem[PT / 4 + f * P_ + p] - mem[CENT / 4 + c * F + f]; acc += d * d; }
                if (static_cast<int32_t>(acc) < best) { best = static_cast<int32_t>(acc); idx = c; }
            }
            ++checked; if (mem[OUT / 4 + p] != idx) ++wrong;
        }
    };
    P.sample = [=](const std::vector<uint32_t>& mem) {
        std::string t = "labels[0..]:"; for (int p = 0; p < std::min(P_, 8); ++p) t += " " + std::to_string(mem[OUT / 4 + p]); return t; };
    return P;
}


// Table III: add/and/or/xor 9, sub 17, shifts 82, conditional 13, mul 105-634 (minimum used), div 145-1174.
int calc_cycles(FOp o, int fallback) {
    switch (o) {
        case FOp::SLL: return 82;
        case FOp::SLT: return 13;
        default: return fallback;
    }
}

// The program as seen by the timing engine. Values live here: one value per register index the engine uses.
class Executor : public ExecHook {
public:
    Executor(const Program& P, std::vector<uint32_t>& mem, size_t element) : P_(P), mem_(mem) {
        for (auto& v : val_) v = 0;
        P.setup(val_, element);
        val_[0] = 0;
    }

    bool has_next() const override { return pc_ < P_.code.size(); }

    InSramOp peek() override {
        const Instr& in = P_.code[pc_];
        InSramOp op;
        if (in.branch) {
            op = make_op(next_id_, OpType::BRANCH, in.rs1, in.rs2, 0, false, false, true, false, true);
        } else {
            int s2 = in.imm ? (in.md ? in.rd : 0) : in.rs2;      // a store only reads its pointer register
            op = make_op(next_id_, timing_type(in.op), in.rs1, s2, in.md ? 0 : in.rd, in.m1, in.m2, true, in.md);
            op.has_imm = in.imm; op.imm_val = in.imm_val;
            op.calc_cycles = calc_cycles(in.op, op.calc_cycles);
        }
        return op;
    }

    void fetched() override {
        pc_of_[next_id_] = pc_;
        ++next_id_;
        ++ops_;
        if (P_.code[pc_].branch) { branch_pc_ = pc_; }       // fetch is blocked by the engine until it resolves
        else ++pc_;
    }

    std::string load_done(const InSramOp& op, bool second, int phys_ptr) override {
        Rec& r = rec_[op.op_id];
        uint32_t addr = val_[phys_ptr];
        uint32_t w = word(addr);
        (second ? r.b : r.a) = w;
        return "load mem[" + std::to_string(addr) + "] = " + std::to_string(static_cast<int32_t>(w));
    }

    std::string calc_done(const InSramOp& op, int p1, int p2, int pd) override {
        size_t pc = pc_of_.at(op.op_id);
        const Instr& in = P_.code[pc];
        Rec& r = rec_[op.op_id];
        std::ostringstream os;
        os << "pc" << pc << " " << text(in) << " : ";
        if (in.branch) {
            uint32_t a = val_[p1], b = val_[p2];
            bool taken = a != b;
            pc_ = taken ? in.target : branch_pc_ + 1;
            os << static_cast<int32_t>(a) << " != " << static_cast<int32_t>(b) << " => " << (taken ? "TAKEN" : "not taken");
            return os.str();
        }
        uint32_t a = in.m1 ? r.a : val_[p1];
        uint32_t b = in.imm ? static_cast<uint32_t>(in.imm_val) : (in.m2 ? r.b : val_[p2]);
        uint32_t res = alu(in.op, a, b);
        os << static_cast<int32_t>(a) << ", " << static_cast<int32_t>(b) << " => " << static_cast<int32_t>(res);
        if (in.md) { r.addr = val_[p2]; r.result = res; }   // written to memory when the store phase completes
        else if (pd != 0) val_[pd] = res;
        return os.str();
    }

    std::string store_done(const InSramOp& op) override {
        Rec& r = rec_[op.op_id];
        word(r.addr) = r.result;
        return "store mem[" + std::to_string(r.addr) + "] = " + std::to_string(static_cast<int32_t>(r.result));
    }

    size_t ops() const { return ops_; }

private:
    struct Rec { uint32_t a = 0, b = 0, addr = 0, result = 0; };

    uint32_t& word(uint32_t addr) {
        if (addr % 4 != 0 || addr / 4 >= mem_.size()) throw std::runtime_error("bad memory address " + std::to_string(addr));
        return mem_[addr / 4];
    }

    const Program& P_;
    std::vector<uint32_t>& mem_;
    uint32_t val_[64];                       // register values (index = what the engine uses)
    size_t pc_ = 0, branch_pc_ = 0, ops_ = 0;
    uint64_t next_id_ = 1;
    std::unordered_map<uint64_t, size_t> pc_of_;
    std::unordered_map<uint64_t, Rec> rec_;
};

}  // namespace

static const char* design_name(SimMode m) {
    switch (m) {
        case SimMode::BASELINE: return "Design 1: In-Order Baseline";
        case SimMode::PIPELINED: return "Design 2: Pipelined";
        case SimMode::RENAMING: return "Design 3: Pipelined + Renaming";
        case SimMode::FINE_GRAINED: return "Design 4: Fine-Grained Issue";
    }
    return "?";
}

struct FuncSim::Impl {
    Program prog;
    struct Result { std::string design; bool pass; size_t checked, wrong; uint64_t cycles; bool same_timing; std::string sample; };
    std::vector<Result> results;
};

FuncSim::FuncSim(const std::string& kernel, int n) : impl_(new Impl) {
    if (n < 1 || n > 64) throw std::invalid_argument("--n must be in 1..64");
    if (kernel == "matmul") impl_->prog = make_matmul(n);
    else if (kernel == "stencil") impl_->prog = make_stencil(n);
    else if (kernel == "kmeans") impl_->prog = make_kmeans(n);
    else throw std::invalid_argument("Unsupported kernel: " + kernel);
}
FuncSim::~FuncSim() = default;

uint64_t FuncSim::run(SimMode mode, std::ostream* log) {
    const Program& P = impl_->prog;
    std::vector<uint32_t> mem = P.mem_init;
    uint64_t cycles0 = 0;
    bool same = true;
    if (log) *log << "\n################ " << design_name(mode) << " ################\n";
    for (size_t e = 0; e < P.elements; ++e) {
        Executor ex(P, mem, e);
        uint64_t c = SimEngine::run({}, mode, e == 0 ? log : nullptr, &ex);
        if (e == 0) { cycles0 = c; ops_ = ex.ops(); }
        else if (c != cycles0) same = false;
    }
    Impl::Result r{design_name(mode), false, 0, 0, cycles0, same, P.sample(mem)};
    P.check(mem, r.checked, r.wrong);
    r.pass = r.wrong == 0;
    impl_->results.push_back(r);
    return cycles0;
}

bool FuncSim::all_pass() const {
    for (auto& r : impl_->results) if (!r.pass) return false;
    return !impl_->results.empty();
}

std::string FuncSim::listing() const {
    std::ostringstream os;
    os << "IMC program (" << impl_->prog.code.size() << " static operations):\n";
    for (size_t i = 0; i < impl_->prog.code.size(); ++i) os << "  pc" << i << "  " << text(impl_->prog.code[i]) << "\n";
    return os.str();
}

std::string FuncSim::report() const {
    std::ostringstream os;
    os << "Functional check (real ALU results vs host reference), " << impl_->prog.elements << " output elements per design:\n";
    for (auto& r : impl_->results)
        os << "  " << r.design << ": " << (r.pass ? "PASS" : "FAIL") << " (" << (r.checked - r.wrong) << "/" << r.checked << " correct)"
           << (r.same_timing ? "" : "  [timing differed between elements]") << "\n";
    if (!impl_->results.empty()) os << "  " << impl_->results.front().sample << "\n";
    return os.str();
}
