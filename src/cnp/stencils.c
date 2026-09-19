// The stencils — docs/dev/plans/CNP-PLAN.md.
//
// THIS FILE IS NOT LINKED INTO RAKUPP. It is compiled at BUILD time to an
// object file, and `tools/cnp-extract` slices that object into one byte array
// per function plus the list of holes in it. The generated table is what ships
// inside the binary; at run time the emitter copies these bytes and fills the
// holes. So the C compiler runs on the developer's machine, once, and never on
// the user's — which is the whole point of `--cnp`.
//
// The rules this file lives by, each of which the extractor or the patcher
// depends on:
//
//   * Every stencil has the SAME signature, because `musttail` requires it.
//     The chain of tail calls is ONE machine frame from entry to exit, so any
//     stencil's `return` returns straight to the trampoline.
//   * A stencil may call only the `rk_cnp_*` helpers in CnpAbi.h, and NONE of
//     them throws. A code buffer has no unwind tables, so a throw crossing one
//     reaches std::terminate. Errors come back as a return value instead and
//     the trampoline rethrows, in a frame that does have tables.
//   * No constants that would land in a literal pool, no static data, no libc.
//     The extractor refuses an object with any section but __text, which is the
//     check that keeps this rule honest rather than aspirational.
//   * `_JIT_*` are undefined symbols on purpose: every reference to one is a
//     relocation, and a relocation is exactly what a hole is.
//
// THE FAST/SLOW SPLIT. Every stencil that can fall back to the interpreter's
// own operator dispatch is written in two pieces. The fast one handles the
// machine-number lanes and is a LEAF — it calls nothing, so the compiler gives
// it no prologue and no epilogue, and the hot path is the compare and the
// store and nothing else. Everything it cannot do it tail-calls to `_JIT_SLOW`,
// a block of ordinary stencils the emitter lays out in a cold region past the
// end of the loop. Written as one function with the call inline, the fast path
// paid six stack-memory operations per op for a frame only the cold half
// needed; that measured difference is why the split exists.

#include "../cnp/CnpAbi.h"

// ---- the holes -------------------------------------------------------------
//
// Reading the ADDRESS of an undefined symbol is how an arbitrary 64-bit value
// gets into the instruction stream. The compiler emits its usual
// address-of-a-global sequence (on arm64 an ADRP/LDR pair through the GOT) and
// one relocation per reference; the patcher rewrites that sequence to
// materialise the number we want. Nothing ever dereferences these.
extern const char _JIT_OP0[], _JIT_OP1[], _JIT_OP2[], _JIT_OP3[];
#define OP0 ((uint64_t)(uintptr_t)_JIT_OP0)
#define OP1 ((uint64_t)(uintptr_t)_JIT_OP1)
#define OP2 ((uint64_t)(uintptr_t)_JIT_OP2)
#define OP3 ((uint64_t)(uintptr_t)_JIT_OP3)

// The three continuations: the next stencil, a branch target, and the cold
// block that handles whatever the fast path refused.
extern int _JIT_CONT  (RkCnpFrame* f, int64_t* r, uint8_t* t);
extern int _JIT_TARGET(RkCnpFrame* f, int64_t* r, uint8_t* t);
extern int _JIT_SLOW  (RkCnpFrame* f, int64_t* r, uint8_t* t);

#define NEXT  __attribute__((musttail)) return _JIT_CONT  (f, r, t)
#define GOTO  __attribute__((musttail)) return _JIT_TARGET(f, r, t)
#define SLOW  __attribute__((musttail)) return _JIT_SLOW  (f, r, t)

#define F RkCnpFrame

// A double lives in the register file as its bit pattern. On every target we
// build for this is one register move, not a memory round trip.
static inline double  rk_d(int64_t x) { union { int64_t i; double d; } u; u.i = x; return u.d; }
static inline int64_t rk_q(double  x) { union { int64_t i; double d; } u; u.d = x; return u.i; }
// `tag <= RK_T_NUM` is "this register holds a machine number". The tag order in
// CnpAbi.h exists for this test.
#define NUMERIC(tag) ((tag) <= RK_T_NUM)
#define ASDOUBLE(k, tag) ((tag) == RK_T_INT ? (double)r[k] : rk_d(r[k]))

// ============================================================================
// FAST STENCILS — leaves. No call, no frame.
// ============================================================================

// ---- constants and moves ---------------------------------------------------

int rk_st_loadi(F* f, int64_t* r, uint8_t* t) {   // r[d] = imm, as an Int
    r[OP0] = (int64_t)OP1; t[OP0] = RK_T_INT; NEXT;
}
int rk_st_loadn(F* f, int64_t* r, uint8_t* t) {   // r[d] = imm, as a Num
    r[OP0] = (int64_t)OP1; t[OP0] = RK_T_NUM; NEXT;
}
int rk_st_loadb(F* f, int64_t* r, uint8_t* t) {   // r[d] = 0/1, as a Bool
    r[OP0] = (int64_t)OP1; t[OP0] = RK_T_BOOL; NEXT;
}
int rk_st_move(F* f, int64_t* r, uint8_t* t) {    // r[d] = r[a]
    uint8_t ta = t[OP1];
    if (ta == RK_T_BOX) SLOW;                     // a Value copy runs a destructor
    r[OP0] = r[OP1]; t[OP0] = ta; NEXT;
}

// ---- arithmetic ------------------------------------------------------------
//
// Int/Int first, then the lane where at least one side is a Num. Anything else
// — a bignum, a Rat, a Str, an object, or an Int/Int that overflowed — goes to
// the cold block, which lands in `applyArith`, the interpreter's own operator
// dispatcher. So an interpreted program and a kernel decide overflow,
// coercion and type errors with exactly the same code.

#define ARITH(name, cop, ovf)                                                  \
int name(F* f, int64_t* r, uint8_t* t) {                                       \
    uint64_t d = OP0, a = OP1, b = OP2;                                        \
    uint8_t ta = t[a], tb = t[b];                                              \
    if (ta == RK_T_INT && tb == RK_T_INT) {                                    \
        int64_t z;                                                             \
        if (ovf(r[a], r[b], &z)) SLOW;                                         \
        r[d] = z; t[d] = RK_T_INT; NEXT;                                       \
    }                                                                          \
    if (NUMERIC(ta) && NUMERIC(tb)) {                                          \
        r[d] = rk_q(ASDOUBLE(a, ta) cop ASDOUBLE(b, tb));                      \
        t[d] = RK_T_NUM; NEXT;                                                 \
    }                                                                          \
    SLOW;                                                                      \
}
ARITH(rk_st_add, +, __builtin_add_overflow)
ARITH(rk_st_sub, -, __builtin_sub_overflow)
ARITH(rk_st_mul, *, __builtin_mul_overflow)
#undef ARITH

// r[d] = r[a] + imm. `$i + 1` and `$i - 1` are most of the arithmetic in a
// loop header, and folding the literal into the instruction stream instead of
// loading it from anywhere is the reason copy-and-patch exists.
int rk_st_addi(F* f, int64_t* r, uint8_t* t) {
    uint64_t d = OP0, a = OP1;
    int64_t k = (int64_t)OP2;
    uint8_t ta = t[a];
    if (ta == RK_T_INT) {
        int64_t z;
        if (__builtin_add_overflow(r[a], k, &z)) SLOW;
        r[d] = z; t[d] = RK_T_INT; NEXT;
    }
    if (ta == RK_T_NUM) { r[d] = rk_q(rk_d(r[a]) + (double)k); t[d] = RK_T_NUM; NEXT; }
    SLOW;
}

// r[d] += imm, in place: `$i++`, `$i--`, `$i += 3`.
int rk_st_incr(F* f, int64_t* r, uint8_t* t) {
    uint64_t d = OP0;
    int64_t k = (int64_t)OP1;
    uint8_t td = t[d];
    if (td == RK_T_INT) {
        int64_t z;
        if (__builtin_add_overflow(r[d], k, &z)) SLOW;
        r[d] = z; NEXT;
    }
    if (td == RK_T_NUM) { r[d] = rk_q(rk_d(r[d]) + (double)k); NEXT; }
    SLOW;
}

int rk_st_neg(F* f, int64_t* r, uint8_t* t) {     // r[d] = -r[a]
    uint64_t d = OP0, a = OP1;
    uint8_t ta = t[a];
    if (ta == RK_T_INT) {
        int64_t z;
        if (__builtin_sub_overflow((int64_t)0, r[a], &z)) SLOW;
        r[d] = z; t[d] = RK_T_INT; NEXT;
    }
    if (ta == RK_T_NUM) { r[d] = rk_q(-rk_d(r[a])); t[d] = RK_T_NUM; NEXT; }
    SLOW;
}

int rk_st_not(F* f, int64_t* r, uint8_t* t) {     // r[d] = !r[a], as a Bool
    uint64_t d = OP0, a = OP1;
    uint8_t ta = t[a];
    if (ta == RK_T_INT || ta == RK_T_BOOL) { r[d] = (r[a] == 0);       t[d] = RK_T_BOOL; NEXT; }
    if (ta == RK_T_NUM)                    { r[d] = (rk_d(r[a]) == 0.0); t[d] = RK_T_BOOL; NEXT; }
    SLOW;
}

// ---- comparison ------------------------------------------------------------
//
// Two shapes of each. The VALUE form leaves a Bool in a register, for `my $b =
// $x < $y`. The BRANCH form never builds one, which is what a loop condition
// wants: `while $i < $n` becomes a single stencil holding the compare and the
// back edge.

#define CMPV(name, cop)                                                        \
int name(F* f, int64_t* r, uint8_t* t) {                                       \
    uint64_t d = OP0, a = OP1, b = OP2;                                        \
    uint8_t ta = t[a], tb = t[b];                                              \
    if (ta == RK_T_INT && tb == RK_T_INT) { r[d] = (r[a] cop r[b]); t[d] = RK_T_BOOL; NEXT; } \
    if (NUMERIC(ta) && NUMERIC(tb)) {                                          \
        r[d] = (ASDOUBLE(a, ta) cop ASDOUBLE(b, tb)); t[d] = RK_T_BOOL; NEXT;  \
    }                                                                          \
    SLOW;                                                                      \
}
CMPV(rk_st_cmplt, < ) CMPV(rk_st_cmple, <=) CMPV(rk_st_cmpgt, > )
CMPV(rk_st_cmpge, >=) CMPV(rk_st_cmpeq, ==) CMPV(rk_st_cmpne, !=)
#undef CMPV

#define CMPB(name, cop)                                                        \
int name(F* f, int64_t* r, uint8_t* t) {                                       \
    uint64_t a = OP0, b = OP1;                                                 \
    uint8_t ta = t[a], tb = t[b];                                              \
    if (ta == RK_T_INT && tb == RK_T_INT) { if (r[a] cop r[b]) GOTO; NEXT; }   \
    if (NUMERIC(ta) && NUMERIC(tb)) { if (ASDOUBLE(a, ta) cop ASDOUBLE(b, tb)) GOTO; NEXT; } \
    SLOW;                                                                      \
}
CMPB(rk_st_jlt, < ) CMPB(rk_st_jle, <=) CMPB(rk_st_jgt, > )
CMPB(rk_st_jge, >=) CMPB(rk_st_jeq, ==) CMPB(rk_st_jne, !=)
#undef CMPB

// The same against a literal Int — `while $i < 5_000_000`, `until $n == 0`.
#define CMPBI(name, cop)                                                       \
int name(F* f, int64_t* r, uint8_t* t) {                                       \
    uint64_t a = OP0;                                                          \
    int64_t k = (int64_t)OP1;                                                  \
    uint8_t ta = t[a];                                                         \
    if (ta == RK_T_INT) { if (r[a] cop k) GOTO; NEXT; }                        \
    if (ta == RK_T_NUM) { if (rk_d(r[a]) cop (double)k) GOTO; NEXT; }          \
    SLOW;                                                                      \
}
CMPBI(rk_st_jlti, < ) CMPBI(rk_st_jlei, <=) CMPBI(rk_st_jgti, > )
CMPBI(rk_st_jgei, >=) CMPBI(rk_st_jeqi, ==) CMPBI(rk_st_jnei, !=)
#undef CMPBI

// And the NEGATED forms of both, which is what `while COND {…}` and `if COND
// {…}` actually want: branch OUT when the condition is false. Written as `!(a <
// b)` rather than as `a >= b` on purpose — those are not the same question when
// an operand is NaN, and inverting the operator to save twelve stencils would
// have quietly changed what a Num loop does at its edges.
#define CMPBN(name, cop)                                                       \
int name(F* f, int64_t* r, uint8_t* t) {                                       \
    uint64_t a = OP0, b = OP1;                                                 \
    uint8_t ta = t[a], tb = t[b];                                              \
    if (ta == RK_T_INT && tb == RK_T_INT) { if (!(r[a] cop r[b])) GOTO; NEXT; } \
    if (NUMERIC(ta) && NUMERIC(tb)) { if (!(ASDOUBLE(a, ta) cop ASDOUBLE(b, tb))) GOTO; NEXT; } \
    SLOW;                                                                      \
}
CMPBN(rk_st_jnlt, < ) CMPBN(rk_st_jnle, <=) CMPBN(rk_st_jngt, > )
CMPBN(rk_st_jnge, >=) CMPBN(rk_st_jneq, ==) CMPBN(rk_st_jnne, !=)
#undef CMPBN

#define CMPBNI(name, cop)                                                      \
int name(F* f, int64_t* r, uint8_t* t) {                                       \
    uint64_t a = OP0;                                                          \
    int64_t k = (int64_t)OP1;                                                  \
    uint8_t ta = t[a];                                                         \
    if (ta == RK_T_INT) { if (!(r[a] cop k)) GOTO; NEXT; }                     \
    if (ta == RK_T_NUM) { if (!(rk_d(r[a]) cop (double)k)) GOTO; NEXT; }       \
    SLOW;                                                                      \
}
CMPBNI(rk_st_jnlti, < ) CMPBNI(rk_st_jnlei, <=) CMPBNI(rk_st_jngti, > )
CMPBNI(rk_st_jngei, >=) CMPBNI(rk_st_jneqi, ==) CMPBNI(rk_st_jnnei, !=)
#undef CMPBNI

// ---- control flow ----------------------------------------------------------

int rk_st_jmp(F* f, int64_t* r, uint8_t* t) { GOTO; }

// Branch on the truth of one register. For the three unboxed tags Raku
// truthiness is "not zero"; anything boxed asks the interpreter, because a user
// class may define its own .Bool.
int rk_st_jt(F* f, int64_t* r, uint8_t* t) {
    uint64_t a = OP0; uint8_t ta = t[a];
    if (ta == RK_T_INT || ta == RK_T_BOOL) { if (r[a]) GOTO; NEXT; }
    if (ta == RK_T_NUM)                    { if (rk_d(r[a]) != 0.0) GOTO; NEXT; }
    SLOW;
}
int rk_st_jf(F* f, int64_t* r, uint8_t* t) {
    uint64_t a = OP0; uint8_t ta = t[a];
    if (ta == RK_T_INT || ta == RK_T_BOOL) { if (!r[a]) GOTO; NEXT; }
    if (ta == RK_T_NUM)                    { if (rk_d(r[a]) == 0.0) GOTO; NEXT; }
    SLOW;
}
// `//` asks whether the left side is DEFINED, not whether it is true — `0 // 9`
// is 0. An unboxed register always holds a defined value, so the fast lane is
// an unconditional branch and only a box has to ask.
int rk_st_jdef(F* f, int64_t* r, uint8_t* t) {
    (void)r;
    if (t[OP0] != RK_T_BOX) GOTO;
    SLOW;
}

// The kernel ran the loop to completion. The trampoline writes the registers
// back into the interpreter's containers.
int rk_st_ret(F* f, int64_t* r, uint8_t* t) { (void)f; (void)r; (void)t; return RK_CNP_OK; }

// ============================================================================
// COLD STENCILS — these call helpers, so they have frames. The emitter lays
// them out past the end of the loop, where the instruction prefetcher never
// goes unless a guard actually failed.
// ============================================================================

// r[d] = r[a] <op> r[b], through applyArith.
int rk_st_binop(F* f, int64_t* r, uint8_t* t) {
    (void)r; (void)t;
    if (rk_cnp_binop(f, OP3, OP0, OP1, OP2)) return RK_CNP_ERR;
    NEXT;
}
// r[d] = <op> r[a]
int rk_st_unop(F* f, int64_t* r, uint8_t* t) {
    (void)r; (void)t;
    if (rk_cnp_unop(f, OP2, OP0, OP1)) return RK_CNP_ERR;
    NEXT;
}
// r[d] = the constant pool's entry k. Always boxed and always a helper: a Str
// or a Rat is a C++ object with a destructor, and none of that belongs in a
// stencil.
int rk_st_loadk(F* f, int64_t* r, uint8_t* t) {
    (void)r; (void)t;
    if (rk_cnp_loadk(f, OP0, OP1)) return RK_CNP_ERR;
    NEXT;
}
// r[d] = r[a], where r[a] is boxed.
int rk_st_movebox(F* f, int64_t* r, uint8_t* t) {
    (void)r; (void)t;
    if (rk_cnp_move(f, OP0, OP1)) return RK_CNP_ERR;
    NEXT;
}
// The boxed lanes of jt / jf / jdef.
int rk_st_jtslow(F* f, int64_t* r, uint8_t* t) {
    (void)r; (void)t;
    int v = rk_cnp_truthy(f, OP0);
    if (v < 0) return RK_CNP_ERR;
    if (v) GOTO;
    NEXT;
}
int rk_st_jfslow(F* f, int64_t* r, uint8_t* t) {
    (void)r; (void)t;
    int v = rk_cnp_truthy(f, OP0);
    if (v < 0) return RK_CNP_ERR;
    if (!v) GOTO;
    NEXT;
}
int rk_st_jdefslow(F* f, int64_t* r, uint8_t* t) {
    (void)r; (void)t;
    int v = rk_cnp_defined(f, OP0);
    if (v < 0) return RK_CNP_ERR;
    if (v) GOTO;
    NEXT;
}
