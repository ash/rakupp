// Dispatch probe — docs/dev/findings/DISPATCH-PROBES.md. A LADDER: the same
// three kernels as tools/dispatch-probe/kernels.raku (k1-k3), run on real
// `Value`s through the engine's own arithmetic, by five tree-walkers that each
// add ONE idea to the one before:
//
//   L1 switch        one switch over node kinds, results by value, every binary
//                    op through applyArith(std::string) — the engine's own
//                    general entry. The leanest walker that still has today's
//                    protocol; the gap between it and the engine is everything
//                    eval() pays beyond that (ladders, ceremony, exec).
//   L2 quickened     L1, with each binary node's kind rewritten once to a
//                    per-operator kind whose case calls rtAdd/rtLtB/… → item 2
//   L3 closures      L2, but each node carries its handler's address: no
//                    switch, one small frame per node kind          → item 1
//   L4 borrowed      L3, and results go INTO a caller's slot; a variable operand
//                    is read by reference, never copied            → item 3
//   L5 fused         L4 plus superinstructions: `$v OP $w`, `$v OP lit`, a
//                    comparison that yields a C++ bool, an int stored in place
//                                                                   → item 4
//   C++              the same loop on int64 locals — the --cnp ceiling
//
// Item 5 (the per-node checks inside eval) cannot be modelled outside the
// engine; it was measured there, by temporary variant builds (see the findings).
//
// Build and run (one core, a few seconds):
//   c++ -std=c++20 -O2 -DNDEBUG -w -Isrc -Iinclude tools/dispatch-probe.cpp \
//       build-arm64/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props}.a -o /tmp/dp && /tmp/dp
#include "Interpreter.h"
#include "Value.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
using namespace rakupp;

// ---- the tree ---------------------------------------------------------------
enum class K { Lit, Var, Bin, Idx, Asg, While, Seq };
enum class Op { Add, Sub, Mul, Mod, Lt };

struct N;
using Fn  = Value (*)(N*);            // L3: result by value
using Fo  = void  (*)(N*, Value&);    // L4/L5: result into the caller's slot
using Fb  = bool  (*)(N*);            // L5: a condition as a C++ bool

struct N {
    K k;
    std::string op;          // L1 dispatches on the spelling, as evalBinary does
    Op opc = Op::Add;        // L2+ : resolved once
    Value lit;
    Value* slot = nullptr;   // a variable's pad slot (the resolved padPtr)
    N* a = nullptr;          // lhs / cond / array
    N* b = nullptr;          // rhs / value / index
    std::vector<N*> kids;    // Seq, While body
    Fn fn = nullptr; Fo fo = nullptr; Fb fb = nullptr;
    int q = 0;               // L2: the quickened kind (an enum Q, declared below)
};

static std::vector<N*> g_nodes;
static N* mk(K k) { N* n = new N; n->k = k; g_nodes.push_back(n); return n; }
static Op opOf(const std::string& s) {
    return s == "+" ? Op::Add : s == "-" ? Op::Sub : s == "*" ? Op::Mul : s == "%" ? Op::Mod : Op::Lt;
}
static N* lit(long long v) { N* n = mk(K::Lit); n->lit = Value::integer(v); return n; }
static N* var(Value& s)    { N* n = mk(K::Var); n->slot = &s; return n; }
static N* bin(const char* op, N* a, N* b) { N* n = mk(K::Bin); n->op = op; n->opc = opOf(op); n->a = a; n->b = b; return n; }
static N* idx(N* arr, N* i) { N* n = mk(K::Idx); n->a = arr; n->b = i; return n; }
static N* asg(Value& s, N* v) { N* n = mk(K::Asg); n->slot = &s; n->b = v; return n; }
static N* whl(N* c, std::vector<N*> body) { N* n = mk(K::While); n->a = c; n->kids = std::move(body); return n; }

static inline Value quick(Op o, const Value& l, const Value& r) {
    switch (o) {
        case Op::Add: return rtAdd(l, r);
        case Op::Sub: return rtSub(l, r);
        case Op::Mul: return rtMul(l, r);
        case Op::Mod: return rtMod(l, r);
        case Op::Lt:  return Value::boolean(rtLtB(l, r));
    }
    return Value();
}
// a variable read hands out a COPY with the container flags cleared, as the
// engine's VarExpr case does
static inline Value readVar(const Value* p) { Value out = *p; out.readonly = out.immutableBind = false; return out; }

// ---- L1: switch, by value, applyArith(std::string) ---------------------------
static Value ev1(N* n) {
    switch (n->k) {
        case K::Lit: return n->lit;
        case K::Var: return readVar(n->slot);
        case K::Bin: { Value l = ev1(n->a); Value r = ev1(n->b); return applyArith(n->op, l, r); }
        case K::Idx: { Value i = ev1(n->b); return (*n->a->slot->arr())[(size_t)i.i]; }
        case K::Asg: *n->slot = ev1(n->b); return Value();
        case K::While: while (ev1(n->a).truthy()) for (N* s : n->kids) ev1(s); return Value();
        case K::Seq: for (N* s : n->kids) ev1(s); return Value();
    }
    return Value();
}
// ---- L2: switch, by value, quickened ------------------------------------------
// The node's kind is rewritten ONCE to a per-operator kind (BinAdd, BinLt, …),
// so the switch lands directly on the operator's own code — the same
// specialisation L3's handlers get, which keeps L2 → L3 about the switch alone.
enum class Q { Lit, Var, Add, Sub, Mul, Mod, Lt, Idx, Asg, While };
static Q qOf(const N* n) {
    switch (n->k) {
        case K::Lit: return Q::Lit; case K::Var: return Q::Var; case K::Idx: return Q::Idx;
        case K::Asg: return Q::Asg; case K::While: return Q::While; case K::Seq: return Q::While;
        case K::Bin: switch (n->opc) { case Op::Add: return Q::Add; case Op::Sub: return Q::Sub;
                                       case Op::Mul: return Q::Mul; case Op::Mod: return Q::Mod;
                                       case Op::Lt: return Q::Lt; }
    }
    return Q::Lit;
}
static Value ev2(N* n) {
    switch ((Q)n->q) {
        case Q::Lit: return n->lit;
        case Q::Var: return readVar(n->slot);
        case Q::Add: { Value l = ev2(n->a); Value r = ev2(n->b); return rtAdd(l, r); }
        case Q::Sub: { Value l = ev2(n->a); Value r = ev2(n->b); return rtSub(l, r); }
        case Q::Mul: { Value l = ev2(n->a); Value r = ev2(n->b); return rtMul(l, r); }
        case Q::Mod: { Value l = ev2(n->a); Value r = ev2(n->b); return rtMod(l, r); }
        case Q::Lt:  { Value l = ev2(n->a); Value r = ev2(n->b); return Value::boolean(rtLtB(l, r)); }
        case Q::Idx: { Value i = ev2(n->b); return (*n->a->slot->arr())[(size_t)i.i]; }
        case Q::Asg: *n->slot = ev2(n->b); return Value();
        case Q::While: while (ev2(n->a).truthy()) for (N* s : n->kids) ev2(s); return Value();
    }
    return Value();
}
// ---- L3: closures, by value --------------------------------------------------
static Value f3Lit(N* n) { return n->lit; }
static Value f3Var(N* n) { return readVar(n->slot); }
template <Op O> static Value f3Bin(N* n) { Value l = n->a->fn(n->a); Value r = n->b->fn(n->b); return quick(O, l, r); }
static Value f3Idx(N* n) { Value i = n->b->fn(n->b); return (*n->a->slot->arr())[(size_t)i.i]; }
static Value f3Asg(N* n) { *n->slot = n->b->fn(n->b); return Value(); }
static Value f3While(N* n) { while (n->a->fn(n->a).truthy()) for (N* s : n->kids) s->fn(s); return Value(); }
static Fn binFn3(Op o) {
    switch (o) { case Op::Add: return f3Bin<Op::Add>; case Op::Sub: return f3Bin<Op::Sub>;
                 case Op::Mul: return f3Bin<Op::Mul>; case Op::Mod: return f3Bin<Op::Mod>;
                 case Op::Lt: return f3Bin<Op::Lt>; }
    return nullptr;
}
// ---- L4: closures, into the caller's slot, variables borrowed -----------------
// An operand that is a variable or a literal is handed out by reference; only a
// computed operand is evaluated, into a temporary the caller owns.
static inline const Value& arg4(N* n, Value& tmp) {
    if (n->k == K::Var) return *n->slot;
    if (n->k == K::Lit) return n->lit;
    n->fo(n, tmp); return tmp;
}
static void f4Lit(N* n, Value& o) { o = n->lit; }
static void f4Var(N* n, Value& o) { o = readVar(n->slot); }
template <Op O> static void f4Bin(N* n, Value& o) {
    Value t1, t2; const Value& l = arg4(n->a, t1); const Value& r = arg4(n->b, t2); o = quick(O, l, r);
}
static void f4Idx(N* n, Value& o) { Value t; const Value& i = arg4(n->b, t); o = (*n->a->slot->arr())[(size_t)i.i]; }
static void f4Asg(N* n, Value&) { n->b->fo(n->b, *n->slot); }   // evaluated straight into the slot
static void f4While(N* n, Value&) {
    Value c, sink;
    for (;;) { n->a->fo(n->a, c); if (!c.truthy()) break; for (N* s : n->kids) s->fo(s, sink); }
}
static Fo binFo4(Op o) {
    switch (o) { case Op::Add: return f4Bin<Op::Add>; case Op::Sub: return f4Bin<Op::Sub>;
                 case Op::Mul: return f4Bin<Op::Mul>; case Op::Mod: return f4Bin<Op::Mod>;
                 case Op::Lt: return f4Bin<Op::Lt>; }
    return nullptr;
}
// ---- L5: L4 + superinstructions ---------------------------------------------
// Two plain operands (variable or literal) are read in place and, when both are
// small Ints, computed on int64 with the overflow check; the result is stored
// into an Int slot IN PLACE. Anything else takes the L4 handler.
static inline const Value& leaf(N* n) { return n->k == K::Var ? *n->slot : n->lit; }
static inline bool intOp(Op o, long long a, long long b, long long& z) {
    switch (o) {
        case Op::Add: return !rakupp::add_ovf(a, b, &z);
        case Op::Sub: return !rakupp::sub_ovf(a, b, &z);
        case Op::Mul: return !rakupp::mul_ovf(a, b, &z);
        case Op::Mod: if (b == 0) return false; z = a % b; if (z != 0 && ((z < 0) != (b < 0))) z += b; return true;
        case Op::Lt:  return false;
    }
    return false;
}
template <Op O> static void f5LeafBin(N* n, Value& o) {
    const Value& l = leaf(n->a); const Value& r = leaf(n->b); long long z;
    if (rtIntBox(l) && rtIntBox(r) && intOp(O, l.i, r.i, z)) {
        if (rtIntSlot(o)) o.i = z; else o = Value::integer(z);
        return;
    }
    o = quick(O, l, r);
}
// one computed operand, one leaf: evaluate the computed side into a scratch the
// node keeps, then the int lane
template <Op O> static void f5MixBin(N* n, Value& o) {
    Value t1, t2; const Value& l = arg4(n->a, t1); const Value& r = arg4(n->b, t2); long long z;
    if (rtIntBox(l) && rtIntBox(r) && intOp(O, l.i, r.i, z)) {
        if (rtIntSlot(o)) o.i = z; else o = Value::integer(z);
        return;
    }
    o = quick(O, l, r);
}
static bool b5LeafLt(N* n) { const Value& l = leaf(n->a); const Value& r = leaf(n->b); return rtLtB(l, r); }
static void f5While(N* n, Value&) {
    Value sink;
    while (n->a->fb(n->a)) for (N* s : n->kids) s->fo(s, sink);
}
static Fo leafFo5(Op o) {
    switch (o) { case Op::Add: return f5LeafBin<Op::Add>; case Op::Sub: return f5LeafBin<Op::Sub>;
                 case Op::Mul: return f5LeafBin<Op::Mul>; case Op::Mod: return f5LeafBin<Op::Mod>;
                 case Op::Lt: return f4Bin<Op::Lt>; }
    return nullptr;
}
static Fo mixFo5(Op o) {
    switch (o) { case Op::Add: return f5MixBin<Op::Add>; case Op::Sub: return f5MixBin<Op::Sub>;
                 case Op::Mul: return f5MixBin<Op::Mul>; case Op::Mod: return f5MixBin<Op::Mod>;
                 case Op::Lt: return f4Bin<Op::Lt>; }
    return nullptr;
}

// ---- "compiling": pick each node's handler, once -----------------------------
static void compile(N* n, int level) {
    n->q = (int)qOf(n);
    if (n->a) compile(n->a, level);
    if (n->b) compile(n->b, level);
    for (N* s : n->kids) compile(s, level);
    bool leaves = n->k == K::Bin && (n->a->k == K::Var || n->a->k == K::Lit) &&
                                    (n->b->k == K::Var || n->b->k == K::Lit);
    switch (n->k) {
        case K::Lit: n->fn = f3Lit; n->fo = f4Lit; break;
        case K::Var: n->fn = f3Var; n->fo = f4Var; break;
        case K::Bin: n->fn = binFn3(n->opc);
                     n->fo = level >= 5 ? (leaves ? leafFo5(n->opc) : mixFo5(n->opc)) : binFo4(n->opc);
                     if (level >= 5 && leaves && n->opc == Op::Lt) n->fb = b5LeafLt;
                     break;
        case K::Idx: n->fn = f3Idx; n->fo = f4Idx; break;
        case K::Asg: n->fn = f3Asg; n->fo = f4Asg; break;
        case K::While: n->fn = f3While; n->fo = level >= 5 && n->a->fb ? f5While : f4While; break;
        case K::Seq: break;
    }
}

// ---- the kernels --------------------------------------------------------------
struct Kernel {
    const char* name;
    Value v[4];        // the pad: $i $s/$x $j, and @a
    N* root = nullptr;
    long long expect = 0;
    void reset();
};
static const long long NN = 2000000;

static void build(Kernel& k) {
    Value& i = k.v[0]; Value& s = k.v[1]; Value& j = k.v[2]; Value& a = k.v[3];
    const std::string nm = k.name;
    if (nm == "k1")
        k.root = whl(bin("<", var(i), lit(NN)),
                     {asg(s, bin("+", var(s), var(i))), asg(i, bin("+", var(i), lit(1)))});
    else if (nm == "k2")
        k.root = whl(bin("<", var(i), lit(NN)),
                     {asg(s, bin("%", bin("+", bin("*", var(s), lit(31)), var(i)), lit(1000003))),
                      asg(i, bin("+", var(i), lit(1)))});
    else
        k.root = whl(bin("<", var(i), lit(NN)),
                     {asg(j, bin("%", var(i), lit(1000))),
                      asg(s, bin("+", var(s), idx(var(a), var(j)))),
                      asg(i, bin("+", var(i), lit(1)))});
}
void Kernel::reset() {
    const std::string nm = name;
    v[0] = Value::integer(0); v[1] = Value::integer(nm == "k2" ? 7 : 0); v[2] = Value::integer(0);
    ValueList xs; for (long long q = 0; q < 1000; q++) xs.push_back(Value::integer(q));
    v[3] = Value::array(std::move(xs));
}
// The ceiling. The empty asm makes each value opaque to the optimiser, so the
// loop runs once per iteration instead of being folded to a closed form or
// vectorised — it prices the work, not the compiler's algebra.
#define OPAQUE(x) asm volatile("" : "+r"(x))
static long long native(const std::string& nm) {
    if (nm == "k1") { long long s = 0; for (long long i = 0; i < NN; i++) { s += i; OPAQUE(s); } return s; }
    if (nm == "k2") { long long x = 7; for (long long i = 0; i < NN; i++) { x = (x * 31 + i) % 1000003; OPAQUE(x); } return x; }
    static long long arr[1000]; for (int q = 0; q < 1000; q++) arr[q] = q;
    long long s = 0; for (long long i = 0; i < NN; i++) { s += arr[i % 1000]; OPAQUE(s); } return s;
}
using Clock = std::chrono::steady_clock;

// ---- the price of one tctx_ access --------------------------------------------
// The engine's per-thread context is `static thread_local ExecContext tctx_`.
// ExecContext has a non-trivial constructor and destructor, so every access
// goes through a TLS wrapper (__tls_init, then _tlv_get_addr on Darwin) — the
// profile of k1 put ~19% of the loop there. Three ways to reach the same field,
// each from a function the optimiser may not inline (as the engine's callees):
static thread_local ExecContext t_ctx;                 // as the engine declares it
static thread_local ExecContext* t_ptr = nullptr;      // trivial: no wrapper, still a TLV
__attribute__((noinline)) static long f_wrapped()               { return t_ctx.loopCtl; }
__attribute__((noinline)) static long f_trivial()               { return t_ptr->loopCtl; }
__attribute__((noinline)) static long f_passed(ExecContext& c)  { return c.loopCtl; }
static void tlsProbe(int reps) {
    const long M = 50000000;
    t_ptr = &t_ctx;
    double best[3] = {1e18, 1e18, 1e18};
    for (int r = 0; r < reps; r++) {
        long s = 0;
        auto t0 = Clock::now(); for (long i = 0; i < M; i++) { s += f_wrapped(); OPAQUE(s); }
        auto t1 = Clock::now(); for (long i = 0; i < M; i++) { s += f_trivial(); OPAQUE(s); }
        auto t2 = Clock::now(); ExecContext& c = t_ctx;
                                for (long i = 0; i < M; i++) { s += f_passed(c); OPAQUE(s); }
        auto t3 = Clock::now();
        double d[3] = {std::chrono::duration<double, std::nano>(t1 - t0).count() / M,
                       std::chrono::duration<double, std::nano>(t2 - t1).count() / M,
                       std::chrono::duration<double, std::nano>(t3 - t2).count() / M};
        for (int q = 0; q < 3; q++) if (d[q] < best[q]) best[q] = d[q];
    }
    std::printf("\ntctx_ access, one call each, best of %d (ns/access):\n", reps);
    std::printf("  thread_local ExecContext (as today)   %6.2f\n", best[0]);
    std::printf("  thread_local ExecContext* (trivial)   %6.2f\n", best[1]);
    std::printf("  ExecContext& passed in                %6.2f\n", best[2]);
}

static double run(Kernel& k, int level) {
    k.reset();
    auto t0 = Clock::now();
    switch (level) {
        case 1: ev1(k.root); break;
        case 2: ev2(k.root); break;
        case 3: k.root->fn(k.root); break;
        default: { Value sink; k.root->fo(k.root, sink); break; }
    }
    double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    if (k.v[1].i != k.expect) std::printf("  WRONG %s L%d: %lld != %lld\n", k.name, level, k.v[1].i, k.expect);
    return ms;
}

int main(int argc, char** argv) {
    int reps = argc > 1 ? std::atoi(argv[1]) : 5;
    const char* names[] = {"k1", "k2", "k3"};
    std::printf("dispatch ladder: %lld iterations, best of %d, interleaved\n\n", NN, reps);
    std::printf("  kernel   L1 switch  L2 quick  L3 closure  L4 borrow  L5 fused     C++   (ns/iteration)\n");
    for (const char* nm : names) {
        // each level gets its own tree, compiled for that level
        Kernel ks[6];
        for (int l = 1; l <= 5; l++) { ks[l].name = nm; build(ks[l]); compile(ks[l].root, l); ks[l].expect = native(nm); }
        double best[7]; for (double& b : best) b = 1e18;
        for (int r = 0; r < reps; r++) {
            for (int l = 1; l <= 5; l++) { double m = run(ks[l], l); if (m < best[l]) best[l] = m; }
            auto t0 = Clock::now(); volatile long long z = native(nm); (void)z;
            double m = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            if (m < best[6]) best[6] = m;
        }
        auto ns = [&](int l) { return best[l] * 1e6 / NN; };
        std::printf("  %-6s  %9.1f %9.1f %11.1f %10.1f %9.1f %7.2f\n", nm, ns(1), ns(2), ns(3), ns(4), ns(5), ns(6));
    }
    tlsProbe(reps);
    return 0;
}
