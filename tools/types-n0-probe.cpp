// TYPES-PLAN.md phase N0: what native STORAGE is worth to a tree-walker.
//
// The question the plan leaves open: under `--types` a variable's type is a
// promise, so the interpreter could keep an Int as a bare int64 and a Num as a
// bare double instead of a Value, and evaluate arithmetic on them directly.
// How much would that buy, and how much of it could be had WITHOUT the
// promise, by typed evaluation that checks each leaf's tag (NATIVE-MATH-PLAN
// phase 4)? Nothing here touches the engine: it is a small tree-walker over a
// hand-built tree of the Mandelbrot kernel (tools/bench/types/mandel-*.raku),
// evaluated five ways, all of which must print the kernel's checksum.
//
//   B  Value slots, Value intermediates through applyArith: the interpreter's
//      arithmetic, without the rest of the interpreter
//   C  Value slots, typed evaluation, a tag check at every leaf: the guarded
//      form, available with no `--types` at all
//   D  native slots (int64 / double), typed evaluation, no checks: what the
//      `--types` contract permits
//   E  native slots read back through Values: storage changed, evaluation
//      not — the combination the plan warns is slower than today
//   F  the loop written by hand in C++: the floor
//
// Build and run (Release static libs, as the other probes):
//   c++ -std=c++17 -O2 -w -Isrc -Iinclude tools/types-n0-probe.cpp \
//       build/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props,stubs}.a -o /tmp/n0 && /tmp/n0
#include "Interpreter.h"
#include "Value.h"
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>
using namespace rakupp;

// ---- the tree --------------------------------------------------------------

enum class K { IntC, NumC, Var, Add, Sub, Mul, Div, Lt, Gt, Assign, While, BreakIf, Block };
struct PNode {
    K k;
    bool num = false;                 // the node's static type: Num (true) or Int
    long long i = 0; double d = 0;    // constants
    int slot = -1;                    // Var / Assign
    PNode *a = nullptr, *b = nullptr;  // operands; While: cond, body; BreakIf: cond
    std::vector<PNode*> kids;          // Block
};
static std::vector<std::unique_ptr<PNode>> pool;
static PNode* mk(K k) { pool.emplace_back(new PNode{k}); return pool.back().get(); }
static PNode* ic(long long v) { PNode* n = mk(K::IntC); n->i = v; return n; }
static PNode* nc(double v) { PNode* n = mk(K::NumC); n->d = v; n->num = true; return n; }
static bool slotNum[16];
static PNode* var(int s) { PNode* n = mk(K::Var); n->slot = s; n->num = slotNum[s]; return n; }
static PNode* bin(K k, PNode* a, PNode* b) {
    PNode* n = mk(k); n->a = a; n->b = b;
    n->num = k == K::Div || a->num || b->num;
    if (k == K::Lt || k == K::Gt) n->num = false;
    return n;
}
static PNode* asg(int s, PNode* e) { PNode* n = mk(K::Assign); n->slot = s; n->a = e; n->num = slotNum[s]; return n; }
static PNode* whl(PNode* c, PNode* body) { PNode* n = mk(K::While); n->a = c; n->b = body; return n; }
static PNode* brk(PNode* c) { PNode* n = mk(K::BreakIf); n->a = c; return n; }
static PNode* blk(std::vector<PNode*> v) { PNode* n = mk(K::Block); n->kids = std::move(v); return n; }

enum { W, H, SUM, Y, CI, X, CR, ZR, ZI, KK, T, NSLOTS };
static const int Wv = 300, Hv = 260;

static PNode* program() {
    slotNum[CI] = slotNum[CR] = slotNum[ZR] = slotNum[ZI] = slotNum[T] = true;
    PNode* inner = whl(bin(K::Lt, var(KK), ic(112)), blk({
        asg(T,  bin(K::Add, bin(K::Sub, bin(K::Mul, var(ZR), var(ZR)), bin(K::Mul, var(ZI), var(ZI))), var(CR))),
        asg(ZI, bin(K::Add, bin(K::Mul, bin(K::Mul, nc(2), var(ZR)), var(ZI)), var(CI))),
        asg(ZR, var(T)),
        brk(bin(K::Gt, bin(K::Add, bin(K::Mul, var(ZR), var(ZR)), bin(K::Mul, var(ZI), var(ZI))), nc(10))),
        asg(KK, bin(K::Add, var(KK), ic(1))),
    }));
    PNode* xloop = whl(bin(K::Lt, var(X), var(W)), blk({
        asg(CR, bin(K::Sub, bin(K::Div, bin(K::Mul, var(X), nc(4)), var(W)), nc(2))),
        asg(ZR, nc(0)), asg(ZI, nc(0)), asg(KK, ic(0)),
        inner,
        asg(SUM, bin(K::Add, var(SUM), var(KK))),
        asg(X, bin(K::Add, var(X), ic(1))),
    }));
    return blk({
        asg(W, ic(Wv)), asg(H, ic(Hv)), asg(SUM, ic(0)), asg(Y, ic(0)),
        whl(bin(K::Lt, var(Y), var(H)), blk({
            asg(CI, bin(K::Sub, bin(K::Div, bin(K::Mul, var(Y), nc(4)), var(H)), nc(2))),
            asg(X, ic(0)),
            xloop,
            asg(Y, bin(K::Add, var(Y), ic(1))),
        })),
    });
}

struct Break {};   // `last`, as a C++ exception would be too slow and too unlike
                   // the engine; the walkers return a flag instead

// ---- B: Values everywhere ----------------------------------------------------

struct WalkB {
    Value s[NSLOTS];
    Value ev(PNode* n) {
        switch (n->k) {
            case K::IntC: return Value::integer(n->i);
            case K::NumC: return Value::number(n->d);
            case K::Var:  return s[n->slot];
            case K::Add:  return applyArith("+", ev(n->a), ev(n->b));
            case K::Sub:  return applyArith("-", ev(n->a), ev(n->b));
            case K::Mul:  return applyArith("*", ev(n->a), ev(n->b));
            case K::Div:  return applyArith("/", ev(n->a), ev(n->b));
            case K::Lt:   return applyArith("<", ev(n->a), ev(n->b));
            case K::Gt:   return applyArith(">", ev(n->a), ev(n->b));
            default: return Value();
        }
    }
    bool run(PNode* n) {   // false = a `last` left the enclosing loop
        switch (n->k) {
            case K::Assign: s[n->slot] = ev(n->a); return true;
            case K::Block:  for (PNode* c : n->kids) if (!run(c)) return false; return true;
            case K::While:  while (ev(n->a).truthy()) if (!run(n->b)) break; return true;
            case K::BreakIf: return !ev(n->a).truthy();
            default: ev(n); return true;
        }
    }
    long long go(PNode* p) { run(p); return s[SUM].i; }
};

// ---- C: Value slots, typed evaluation with a check at every leaf -------------

struct WalkC {
    Value s[NSLOTS];
    bool bad = false;   // a leaf held the wrong type: the real thing would fall back
    double num(PNode* n) {
        switch (n->k) {
            case K::NumC: return n->d;
            case K::IntC: return (double)n->i;
            case K::Var: {
                const Value& v = s[n->slot];
                if (v.t == VT::Num) return v.n;
                if (v.t == VT::Int && !v.big()) return (double)v.i;
                bad = true; return 0;
            }
            case K::Add: return num(n->a) + num(n->b);
            case K::Sub: return num(n->a) - num(n->b);
            case K::Mul: return num(n->a) * num(n->b);
            case K::Div: return num(n->a) / num(n->b);
            default: bad = true; return 0;
        }
    }
    long long in(PNode* n) {
        switch (n->k) {
            case K::IntC: return n->i;
            case K::Var: {
                const Value& v = s[n->slot];
                if (v.t == VT::Int && !v.big()) return v.i;
                bad = true; return 0;
            }
            case K::Add: { long long z; if (__builtin_add_overflow(in(n->a), in(n->b), &z)) bad = true; return z; }
            case K::Sub: { long long z; if (__builtin_sub_overflow(in(n->a), in(n->b), &z)) bad = true; return z; }
            case K::Mul: { long long z; if (__builtin_mul_overflow(in(n->a), in(n->b), &z)) bad = true; return z; }
            default: bad = true; return 0;
        }
    }
    bool cond(PNode* n) {
        bool f = n->a->num || n->b->num;
        if (n->k == K::Lt) return f ? num(n->a) < num(n->b) : in(n->a) < in(n->b);
        return f ? num(n->a) > num(n->b) : in(n->a) > in(n->b);
    }
    bool run(PNode* n) {
        switch (n->k) {
            case K::Assign: {
                Value& v = s[n->slot];
                // the root builds (or reuses) the one Value: in place when the tag already matches
                if (n->num) { double d = num(n->a); if (v.t == VT::Num) v.n = d; else v = Value::number(d); }
                else { long long x = in(n->a); if (v.t == VT::Int && !v.big()) v.i = x; else v = Value::integer(x); }
                return true;
            }
            case K::Block:   for (PNode* c : n->kids) if (!run(c)) return false; return true;
            case K::While:   while (cond(n->a)) if (!run(n->b)) break; return true;
            case K::BreakIf: return !cond(n->a);
            default: return true;
        }
    }
    long long go(PNode* p) { run(p); if (bad) std::fprintf(stderr, "C: a guard failed\n"); return s[SUM].i; }
};

// ---- D: native slots, typed evaluation, no checks ---------------------------

struct WalkD {
    long long si[NSLOTS] = {}; double sd[NSLOTS] = {};
    bool ovf = false;
    double num(PNode* n) {
        switch (n->k) {
            case K::NumC: return n->d;
            case K::IntC: return (double)n->i;
            case K::Var:  return slotNum[n->slot] ? sd[n->slot] : (double)si[n->slot];
            case K::Add: return num(n->a) + num(n->b);
            case K::Sub: return num(n->a) - num(n->b);
            case K::Mul: return num(n->a) * num(n->b);
            case K::Div: return num(n->a) / num(n->b);
            default: return 0;
        }
    }
    long long in(PNode* n) {
        switch (n->k) {
            case K::IntC: return n->i;
            case K::Var:  return si[n->slot];
            // an Int still grows past 64 bits: the overflow check stays (the slot
            // would switch to a big Int), only the type checks go
            case K::Add: { long long z; if (__builtin_add_overflow(in(n->a), in(n->b), &z)) ovf = true; return z; }
            case K::Sub: { long long z; if (__builtin_sub_overflow(in(n->a), in(n->b), &z)) ovf = true; return z; }
            case K::Mul: { long long z; if (__builtin_mul_overflow(in(n->a), in(n->b), &z)) ovf = true; return z; }
            default: return 0;
        }
    }
    bool cond(PNode* n) {
        bool f = n->a->num || n->b->num;
        if (n->k == K::Lt) return f ? num(n->a) < num(n->b) : in(n->a) < in(n->b);
        return f ? num(n->a) > num(n->b) : in(n->a) > in(n->b);
    }
    bool run(PNode* n) {
        switch (n->k) {
            case K::Assign:  if (n->num) sd[n->slot] = num(n->a); else si[n->slot] = in(n->a); return true;
            case K::Block:   for (PNode* c : n->kids) if (!run(c)) return false; return true;
            case K::While:   while (cond(n->a)) if (!run(n->b)) break; return true;
            case K::BreakIf: return !cond(n->a);
            default: return true;
        }
    }
    long long go(PNode* p) { run(p); return si[SUM]; }
};

// ---- E: native slots, evaluated through Values ------------------------------

struct WalkE {
    long long si[NSLOTS] = {}; double sd[NSLOTS] = {};
    Value ev(PNode* n) {
        switch (n->k) {
            case K::IntC: return Value::integer(n->i);
            case K::NumC: return Value::number(n->d);
            case K::Var:  return slotNum[n->slot] ? Value::number(sd[n->slot]) : Value::integer(si[n->slot]);
            case K::Add:  return applyArith("+", ev(n->a), ev(n->b));
            case K::Sub:  return applyArith("-", ev(n->a), ev(n->b));
            case K::Mul:  return applyArith("*", ev(n->a), ev(n->b));
            case K::Div:  return applyArith("/", ev(n->a), ev(n->b));
            case K::Lt:   return applyArith("<", ev(n->a), ev(n->b));
            case K::Gt:   return applyArith(">", ev(n->a), ev(n->b));
            default: return Value();
        }
    }
    bool run(PNode* n) {
        switch (n->k) {
            case K::Assign: {
                Value v = ev(n->a);
                if (slotNum[n->slot]) sd[n->slot] = v.toNum(); else si[n->slot] = v.toInt();
                return true;
            }
            case K::Block:   for (PNode* c : n->kids) if (!run(c)) return false; return true;
            case K::While:   while (ev(n->a).truthy()) if (!run(n->b)) break; return true;
            case K::BreakIf: return !ev(n->a).truthy();
            default: ev(n); return true;
        }
    }
    long long go(PNode* p) { run(p); return si[SUM]; }
};

// ---- F: by hand ---------------------------------------------------------------

// The sizes come through volatiles, or the compiler folds the whole loop into
// its answer at compile time and this row reads 0.0 ms.
static volatile int gW = Wv, gH = Hv;
static long long byHand() {
    const int Wr = gW, Hr = gH;
    long long sum = 0;
    for (int y = 0; y < Hr; y++) { double ci = y * 4.0 / Hr - 2.0;
      for (int x = 0; x < Wr; x++) { double cr = x * 4.0 / Wr - 2.0, zr = 0, zi = 0; long long k = 0;
        while (k < 112) { double t = zr*zr - zi*zi + cr; zi = 2.0*zr*zi + ci; zr = t;
                          if (zr*zr + zi*zi > 10.0) break; k++; }
        sum += k; } }
    return sum;
}

template <class F> double best(F f, long long& out, int reps) {
    double b = 1e18;
    for (int r = 0; r < reps; r++) {
        auto t0 = std::chrono::steady_clock::now();
        out = f();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms < b) b = ms;
    }
    return b;
}

int main() {
    PNode* p = program();
    const int reps = 3;
    long long rb, rc, rd, re, rf;
    double tb = best([&] { WalkB w; return w.go(p); }, rb, reps);
    double tc = best([&] { WalkC w; return w.go(p); }, rc, reps);
    double td = best([&] { WalkD w; return w.go(p); }, rd, reps);
    double te = best([&] { WalkE w; return w.go(p); }, re, reps);
    double tf = best(byHand, rf, reps);
    const long long want = 1046820;
    auto ok = [&](long long r) { return r == want ? "" : "  CHECKSUM DIFFERS"; };
    std::printf("Mandelbrot %d x %d, best of %d, checksum %lld\n", Wv, Hv, reps, want);
    std::printf("  B  Value slots, applyArith         %8.1f ms   1.00x%s\n", tb, ok(rb));
    std::printf("  C  Value slots, typed + leaf check %8.1f ms  %5.1fx%s\n", tc, tb / tc, ok(rc));
    std::printf("  D  native slots, typed, no check   %8.1f ms  %5.1fx%s\n", td, tb / td, ok(rd));
    std::printf("  E  native slots through Values     %8.1f ms  %5.1fx%s\n", te, tb / te, ok(re));
    std::printf("  F  by hand                         %8.1f ms  %5.1fx%s\n", tf, tb / tf, ok(rf));
    std::printf("  D over C: %.2fx   (what the --types contract adds over guarded typed evaluation)\n", tc / td);
}
