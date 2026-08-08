// ir-boundary.cpp — price the two per-instruction costs an interpreter IR
// would introduce, against the REAL runtime (`Value`, `Env`, `applyArith`).
// Phase I0 of docs/dev/plans/IR-PLAN.md; the numbers behind
// docs/dev/experiments/IR-BOUNDARY.md. Build & run (arm64 lib, NOT the default
// x86_64 `build/` this machine's Rosetta cmake produces):
//
//   clang++ -std=c++17 -O2 -DNDEBUG -Isrc tools/ir-boundary.cpp \
//           build-arm64/librakupp_rt.a -o /tmp/ir-boundary && /tmp/ir-boundary
//
// The question I0 must answer before any opcode is written: an IR that is only
// PARTLY lowered runs un-lowered subtrees through today's tree-walker via an
// `EVAL_NODE <const Expr*>` instruction. That crossing is not free — it stores
// the result into a REGISTER (a move-assign that must destroy the slot's
// previous contents) instead of into a fresh local. If the crossing costs more
// than lowering saves, a half-lowered program is SLOWER than tree-walking it and
// the plan's phase order is wrong.
//
// Two prices, measured with the same Value the interpreter uses:
//
//   the TAX     — B vs A: eval-into-a-register vs eval-into-a-local
//   the SAVING  — G vs F: what lowering one `$a + $b` node actually removes
//
// and from those, the break-even lowered fraction.
//
// FAITHFULNESS — the traps this file had to avoid, each of which flattered the
// IR by inflating the tree-walk side:
//   * `find("$a")` with a literal constructs a std::string per call. The real
//     interpreter passes `VarExpr::name`, which already exists. Names here are
//     hoisted into const std::string, built once.
//   * `applyArith("+", …)` with a literal does the same. The real call passes
//     `Binary::op`. Hoisted the same way.
//   * The tree-walk fast path (Interpreter.cpp:15613) ends in `applyArith(op,…)`
//     — the STRING-keyed dispatcher — not in `rtAdd`. G models that; G2 splits
//     out how much of the saving is the op-string dispatch alone.
//   * G omits eval()'s own `switch (e->kind)` and the simpleOp/fastShape
//     branches that precede the fast path. That biases AGAINST the IR, which is
//     the safe direction to be wrong in.
//
// Methodology matches BENCHMARKS.md: N iterations per shape, 7 reps, first
// discarded, minimum reported. Every shape feeds a `sink` that is printed, so
// nothing measured here can be optimised away.
#include "Interpreter.h"
#include "Value.h"
#include <chrono>
#include <cstdio>
#include <memory>
#include <new>
#include <string>

using namespace rakupp;
using clk = std::chrono::steady_clock;

// A stand-in for `Interpreter::eval(Expr*)`: noinline (the real one is a
// 2,000-line recursive switch no compiler inlines) and it CONSTRUCTS its Value
// inside the callee, exactly as eval() does.
__attribute__((noinline)) static Value evalStubInt(long long k) {
    return Value::integer(k);
}
__attribute__((noinline)) static Value evalStubStr(long long k) {
    // A Str result: the returned Value carries a std::string that a register
    // store then has to destroy on the next write.
    return Value::str(std::string("row-") + std::to_string(k));
}

// A minimal IR instruction, sized like a real one would be: opcode + three
// register operands + the AST pointer the escape hatch needs + a line number
// (IR-PLAN.md: "every opcode carries one; this is not optional").
enum class Op : unsigned char { EvalNode, AddVV, Nop };
struct Ins {
    Op op;
    unsigned int dst, a, b;
    const void* node;
    int line;
};

template <typename F>
static double bench(const char* label, long long n, F&& body) {
    double best = 1e18;
    for (int rep = 0; rep < 7; rep++) {
        auto t0 = clk::now();
        body(n);
        double ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
        if (rep > 0 && ms < best) best = ms;  // discard first rep as warm-up
    }
    printf("  %-52s %8.2f ms  %7.2f ns/op\n", label, best, best * 1e6 / n);
    return best;
}

int main() {
    const long long N = 20'000'000;
    const long long NS = 5'000'000;
    long long sink = 0;

    // Registers: what a frame is in IR-PLAN.md — one vector<Value>, sized once.
    std::vector<Value> regs(8);
    std::vector<Ins> code = {
        {Op::EvalNode, 0, 0, 0, nullptr, 1},
        {Op::AddVV, 2, 0, 1, nullptr, 2},
        {Op::Nop, 0, 0, 0, nullptr, 3},
    };

    printf("sizeof(Value) = %zu   sizeof(Ins) = %zu\n\n", sizeof(Value), sizeof(Ins));

    // ---- part 1: the crossing tax ----------------------------------------
    puts("part 1 — the EVAL_NODE crossing (un-lowered subtree)");

    // A. TODAY. eval() returns into a fresh local, consumed, destroyed.
    double a_int = bench("A  local:   Value v = eval(node)            [Int]", N, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            Value v = evalStubInt(k);
            sink += v.i;
        }
    });

    // B. THE IR. dispatch, then move-assign into a register, then read it.
    double b_int = bench("B  xing:    dispatch + regs[d] = eval(node) [Int]", N, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            const Ins* ip = &code[0];
            switch (ip->op) {
                case Op::EvalNode: regs[ip->dst] = evalStubInt(k); break;
                case Op::AddVV: regs[ip->dst] = rtAdd(regs[ip->a], regs[ip->b]); break;
                case Op::Nop: break;
            }
            sink += regs[0].i;
        }
    });

    // C. the store alone (no dispatch) — splits B's cost in two.
    double c_int = bench("C  store:   regs[d] = eval(node)  (no dispatch)", N, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            regs[0] = evalStubInt(k);
            sink += regs[0].i;
        }
    });

    // D. dispatch alone — the other half.
    double d_disp = bench("D  disp:    switch over the opcode array only", N, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            const Ins* ip = &code[2];
            switch (ip->op) {
                case Op::EvalNode: regs[ip->dst] = evalStubInt(k); break;
                case Op::AddVV: regs[ip->dst] = rtAdd(regs[ip->a], regs[ip->b]); break;
                case Op::Nop: sink += ip->line; break;
            }
        }
    });

    // E. THE TRAP, kept as a measurement: materialise a temporary, then move it
    // into the slot. This is the shape a VM falls into if `EVAL_NODE` returns a
    // Value that the opcode handler then stores — it pays a move AND two
    // destroys. Shown so the difference from H is not mistaken for noise.
    double e_place = bench("E  trap:    temporary + destroy + move-construct", N, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            Value tmp = evalStubInt(k);
            Value* slot = &regs[0];
            slot->~Value();
            new (slot) Value(std::move(tmp));
            sink += regs[0].i;
        }
    });

    // H. THE PROPOSED SHAPE. The slot is known DEAD (a stack discipline, or a
    // register written once per basic block), so: destroy the dead occupant —
    // which the tree-walk's dying local also pays — then construct the result
    // DIRECTLY in the slot. `new (slot) Value(eval(node))` is a prvalue
    // initialisation, so C++17 guaranteed copy elision means eval() builds its
    // return value in the slot itself: no move, no temporary.
    alignas(Value) static unsigned char slotBuf[sizeof(Value)];
    new (slotBuf) Value();  // the stack slot starts live, as a real one would
    double h_place = bench("H  proposed: dispatch + destroy-dead + construct-in-place", N,
                           [&](long long n) {
        for (long long k = 0; k < n; k++) {
            const Ins* ip = &code[0];
            switch (ip->op) {
                case Op::EvalNode: {
                    Value* slot = reinterpret_cast<Value*>(slotBuf);
                    slot->~Value();
                    new (slot) Value(evalStubInt(k));
                    break;
                }
                default: break;
            }
            sink += reinterpret_cast<Value*>(slotBuf)->i;
        }
    });

    // A2/A3 — WHY is a slot store 3x a local? Two candidate explanations, and
    // they lead to different designs, so they get separated here:
    //   "assign into a LIVE slot is worse than construct into a FRESH one"  vs
    //   "the optimiser reasons about a stack alloca and cannot about a frame".
    // A2 keeps the memory a stack local but makes the store an assignment;
    // A3 keeps the construct-into-dead-storage shape but on a stack buffer.
    Value liveLocal;
    double a2_assign = bench("A2 local:   v = eval(node)   (live stack local)", N, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            liveLocal = evalStubInt(k);
            sink += liveLocal.i;
        }
    });
    double a3_stack = bench("A3 stack:   destroy + construct (stack buffer)", N, [&](long long n) {
        alignas(Value) unsigned char buf[sizeof(Value)];
        new (buf) Value();
        for (long long k = 0; k < n; k++) {
            Value* slot = reinterpret_cast<Value*>(buf);
            slot->~Value();
            new (slot) Value(evalStubInt(k));
            sink += slot->i;
        }
        reinterpret_cast<Value*>(buf)->~Value();
    });

    // The same A/B pair for a Str result — the register must destroy a heap
    // string on the next write, which a dying local also does.
    double a_str = bench("A' local:   Value v = eval(node)            [Str]", NS, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            Value v = evalStubStr(k);
            sink += (long long)v.s.size();
        }
    });
    double b_str = bench("B' xing:    dispatch + regs[d] = eval(node) [Str]", NS, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            const Ins* ip = &code[0];
            switch (ip->op) {
                case Op::EvalNode: regs[ip->dst] = evalStubStr(k); break;
                default: break;
            }
            sink += (long long)regs[0].s.size();
        }
    });

    // ---- part 2: what lowering ONE arith node saves -----------------------
    // The tree-walk's fast path for `$a + $b` (NODE-SPECIALIZATION.md shape 3,
    // Interpreter.cpp:15613): look BOTH names up through the Env chain, check
    // each is a plain scalar, then applyArith(op, …). A real Env chain three
    // deep — loop body -> sub frame -> file scope — with the variables in the
    // middle frame, which is where a sub's `my` lives.
    puts("\npart 2 — lowering one `$a + $b` node (tax vs saving)");

    auto fileScope = std::make_shared<Env>();
    fileScope->vars["$global"] = Value::integer(1);
    auto subFrame = std::make_shared<Env>();
    subFrame->parent = fileScope;
    subFrame->routineFrame = true;
    subFrame->vars["$a"] = Value::integer(3);
    subFrame->vars["$b"] = Value::integer(4);
    auto loopBody = std::make_shared<Env>();
    loopBody->parent = subFrame;
    loopBody->vars["$_"] = Value::integer(0);

    // Built ONCE, like the AST's VarExpr::name and Binary::op. Constructing
    // these inside the loop is the trap noted in the header.
    const std::string nameA = "$a", nameB = "$b", opPlus = "+";

    regs[0] = Value::integer(3);
    regs[1] = Value::integer(4);

    // the fast path's plain-scalar guard, verbatim from Interpreter.cpp:15621
    auto scal = [](Value* p) -> const Value* {
        return (p && p->hashKind.empty() &&
                (p->t == VT::Int || p->t == VT::Num || p->t == VT::Str || p->t == VT::Bool))
                   ? p
                   : nullptr;
    };

    // G. TREE-WALK today: 2 name lookups + guards + applyArith -> local.
    double g_walk = bench("G  walk:    2x find + guard + applyArith -> local", N, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            const Value* l = scal(loopBody->find(nameA));
            const Value* r = scal(loopBody->find(nameB));
            if (l && r) {
                Value v = applyArith(opPlus, *l, *r);
                sink += v.i;
            }
        }
    });

    // G2. the same, but with rtAdd instead of applyArith — isolates how much of
    // the saving is the op-STRING dispatch rather than the name lookups.
    double g2_walk = bench("G2 walk':   2x find + guard + rtAdd     -> local", N, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            const Value* l = scal(loopBody->find(nameA));
            const Value* r = scal(loopBody->find(nameB));
            if (l && r) {
                Value v = rtAdd(*l, *r);
                sink += v.i;
            }
        }
    });

    // F. LOWERED, naive store: dispatch + rtAdd on two registers, move-assigned
    // into a live register.
    double f_low = bench("F  lowered: dispatch + rtAdd -> regs[d] (assign)", N, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            const Ins* ip = &code[1];
            switch (ip->op) {
                case Op::AddVV: regs[ip->dst] = rtAdd(regs[ip->a], regs[ip->b]); break;
                default: break;
            }
            sink += regs[2].i;
        }
    });

    // F2. LOWERED, the H store: construct the result in a dead slot.
    double f2_low = bench("F2 lowered: dispatch + rtAdd -> dead slot (place)", N, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            const Ins* ip = &code[1];
            switch (ip->op) {
                case Op::AddVV: {
                    Value* slot = reinterpret_cast<Value*>(slotBuf);
                    slot->~Value();
                    new (slot) Value(rtAdd(regs[ip->a], regs[ip->b]));
                    break;
                }
                default: break;
            }
            sink += reinterpret_cast<Value*>(slotBuf)->i;
        }
    });

    // ---- part 3: where a CALL's time goes ---------------------------------
    // The node counter says fib(29) visits 9.98M nodes in ~750 ms — about 75 ns
    // per node visit — while `--exe` runs the same program in 160 ms. So the
    // interpreter's cost per call is ~451 ns against compiled code's ~96 ns,
    // and NONE of that ~355 ns gap is opcode dispatch (D, above: 0.27 ns).
    // These are the two per-call allocations an IR frame would replace, priced
    // so the campaign can be aimed at the money instead of at the tree walk.
    puts("\npart 3 — the per-call costs an IR frame would replace");
    const long long NC = 5'000'000;
    const std::string pname = "$n";

    double p_env = bench("P1 Env:     make_shared<Env> + parent + 1 var", NC, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            auto e = std::make_shared<Env>();
            e->parent = subFrame;
            e->routineFrame = true;
            e->vars[pname] = Value::integer(k);
            sink += e->vars.size();
        }
    });

    double p_args = bench("P2 args:    ValueList with one argument", NC, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            ValueList args;
            args.push_back(Value::integer(k));
            sink += (long long)args.size();
        }
    });

    double p_both = bench("P3 both:    ValueList + Env + bind (a whole call)", NC, [&](long long n) {
        for (long long k = 0; k < n; k++) {
            ValueList args;
            args.push_back(Value::integer(k));
            auto e = std::make_shared<Env>();
            e->parent = subFrame;
            e->routineFrame = true;
            e->vars[pname] = std::move(args[0]);
            sink += e->vars.size();
        }
    });

    // ---- the verdict ------------------------------------------------------
    double per = 1e6 / N, perS = 1e6 / NS;
    double tax_assign = (b_int - a_int) * per;   // naive register VM
    double tax_place = (h_place - a_int) * per;  // proposed dead-slot VM
    double tax_str = (b_str - a_str) * perS;
    double save_assign = (g_walk - f_low) * per;
    double save_place = (g_walk - f2_low) * per;

    printf("\n---- I0 verdict -------------------------------------------------\n");
    puts("the crossing tax, per un-lowered node:");
    printf("  naive   register move-assign  (B-A) : %+7.2f ns   [Str: %+.2f]\n", tax_assign, tax_str);
    printf("  proposed dead-slot placement  (H-A) : %+7.2f ns\n", tax_place);
    printf("  dispatch alone                 (D)  : %7.2f ns\n", d_disp * per);
    printf("  the trap: temporary then move (E-A) : %+7.2f ns\n", (e_place - a_int) * per);
    puts("  why (A2/A3 separate the two candidate causes):");
    printf("    assign into a LIVE stack local(A2) : %+7.2f ns\n", (a2_assign - a_int) * per);
    printf("    construct into a stack buffer (A3) : %+7.2f ns\n", (a3_stack - a_int) * per);
    puts("the saving, per lowered `$a + $b` node:");
    printf("  vs naive store                (G-F) : %+7.2f ns\n", save_assign);
    printf("  vs dead-slot store           (G-F2) : %+7.2f ns\n", save_place);
    printf("    of which op-string dispatch(G-G2) : %+7.2f ns\n", (g_walk - g2_walk) * per);
    auto breakeven = [](const char* what, double tax, double save) {
        if (save <= 0) { printf("  %-28s : NONE — lowering saves nothing\n", what); return; }
        if (tax <= 0)  { printf("  %-28s :   0.0%% — the crossing is free\n", what); return; }
        printf("  %-28s : %6.1f%% of executed nodes\n", what, 100.0 * tax / (tax + save));
    };
    puts("break-even lowered fraction:");
    breakeven("naive register VM", tax_assign, save_assign);
    breakeven("dead-slot VM", tax_place, save_place);
    double perC = 1e6 / NC;
    puts("per-call costs, against ~451 ns/call interpreted and ~96 ns compiled:");
    printf("  Env (make_shared + 1 var)     (P1) : %7.2f ns\n", p_env * perC);
    printf("  ValueList of one argument     (P2) : %7.2f ns\n", p_args * perC);
    printf("  both, as a call does them     (P3) : %7.2f ns  = %.0f%% of the\n",
           p_both * perC, 100.0 * (p_both * perC) / 355.0);
    puts("                                          ~355 ns/call interp-vs-compiled gap");
    printf("sink = %lld\n", sink);
    return 0;
}
