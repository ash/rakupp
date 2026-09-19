#pragma once
// The copy-and-patch ABI — the one file the stencils (C, compiled at BUILD
// time) and the emitter (C++, compiled into rakupp) both read.
// docs/dev/plans/CNP-PLAN.md.
//
// A stencil is a pre-compiled snippet of machine code with holes in it. The
// emitter copies it into a code buffer and fills the holes with this run's
// values — a register number, an immediate, the address of the next stencil.
// Nothing is compiled while the program runs and nothing is written to disk,
// which is the whole point: `--cnp` needs no C++ compiler on the box.
//
// Everything here is plain C so that stencils.c can include it. The C++ side
// includes it too, so the two cannot drift.

#include <stdint.h>

// ---- registers -------------------------------------------------------------
//
// A kernel runs on a flat register file, NOT on Value objects. `r[k]` holds
// either an int64, the bits of a double, or 0/1 for a Bool; `t[k]` says which.
// Anything else — a bignum, a Str, a Rat, an object — lives in `boxes[k]`, a
// real Value, and `t[k]` is RK_T_BOX.
//
// The tag values are ordered so that `t <= RK_T_NUM` is the "this is a machine
// number" test the arithmetic stencils open with.
#define RK_T_INT   0u   // r[k] is a long long
#define RK_T_NUM   1u   // r[k] is the bit pattern of a double
#define RK_T_BOOL  2u   // r[k] is 0 or 1, and means Bool
#define RK_T_BOX   3u   // the value is boxes[k]

// ---- what a stencil returns ------------------------------------------------
//
// Every stencil tail-calls the next one, so the whole kernel is a single
// machine frame and ANY stencil's `return` returns to the trampoline. That is
// also how errors travel: a helper that would have thrown instead stashes the
// exception in the frame and returns non-zero, the stencil returns RK_CNP_ERR,
// and the trampoline rethrows in a frame that HAS unwind tables. No C++
// exception ever crosses the code buffer, which is what lets the buffer be raw
// bytes with no unwind info of its own.
#define RK_CNP_OK   0
#define RK_CNP_ERR  1

// ---- the frame -------------------------------------------------------------
//
// `r` and `t` are passed to every stencil as arguments as well as living here,
// so the hot paths reach them without loading through the frame. The rest is
// touched only by the helpers, which are already the slow path.
typedef struct RkCnpFrame {
    int64_t*  r;        // register file
    uint8_t*  t;        // one tag per register
    void*     boxes;    // Value[] — the boxed form of any RK_T_BOX register
    void*     consts;   // const Value[] — the kernel's constant pool
    void*     interp;   // Interpreter*
    void*     err;      // std::exception_ptr* — where a helper stashes a throw
} RkCnpFrame;

// The signature every stencil has, and therefore the signature of the whole
// kernel. `musttail` between two functions requires identical signatures; that
// is the constraint the register-file design is built around.
typedef int (*RkCnpFn)(RkCnpFrame* f, int64_t* r, uint8_t* t);

// ---- the helpers a stencil may call ----------------------------------------
//
// THE RULE OF THIS INTERFACE: none of these may throw. Each is a C++ function
// whose body is wrapped in a catch-all that stashes the exception in
// `f->err`. A helper that threw would unwind into a code buffer with no unwind
// tables and reach std::terminate. The set is small because the eligibility
// whitelist is small — that is why the constraint is affordable here.
//
// Convention: an `int` that is 0 on success and 1 when the frame carries an
// error. The predicates return 0/1 and -1 for an error, so that no stencil
// needs to take the address of a local (which would complicate the tail call).
#ifdef __cplusplus
extern "C" {
#endif

int rk_cnp_binop  (RkCnpFrame* f, uint64_t op, uint64_t d, uint64_t a, uint64_t b);
int rk_cnp_unop   (RkCnpFrame* f, uint64_t op, uint64_t d, uint64_t a);
int rk_cnp_loadk  (RkCnpFrame* f, uint64_t d, uint64_t k);
int rk_cnp_move   (RkCnpFrame* f, uint64_t d, uint64_t a);  // the RK_T_BOX case only
int rk_cnp_cmp    (RkCnpFrame* f, uint64_t op, uint64_t a, uint64_t b);  // 0/1, -1 on error
int rk_cnp_truthy (RkCnpFrame* f, uint64_t a);                          // 0/1, -1 on error
int rk_cnp_defined(RkCnpFrame* f, uint64_t a);                          // 0/1, -1 on error

#ifdef __cplusplus
}
#endif

// ---- the operator table ----------------------------------------------------
//
// The index a generic stencil is patched with. The emitter and the helper both
// index the same table in Cnp.cpp, so an operator cannot mean two things.
enum RkCnpOp {
    RK_OP_ADD = 0, RK_OP_SUB, RK_OP_MUL, RK_OP_DIV, RK_OP_MOD, RK_OP_POW,
    RK_OP_IDIV, RK_OP_MODOP, RK_OP_DIVIS, RK_OP_GCD, RK_OP_LCM,
    RK_OP_LT, RK_OP_LE, RK_OP_GT, RK_OP_GE, RK_OP_EQ, RK_OP_NE, RK_OP_CMP3,
    RK_OP_SEQ, RK_OP_SNE, RK_OP_SLT, RK_OP_SGT, RK_OP_SLE, RK_OP_SGE,
    RK_OP_LEG, RK_OP_CMP, RK_OP_CONCAT, RK_OP_REPEAT,
    RK_OP_BAND, RK_OP_BOR, RK_OP_BXOR, RK_OP_SHL, RK_OP_SHR,
    RK_OP_MIN, RK_OP_MAX, RK_OP_XOR,
    RK_OP_NEG, RK_OP_PLUS, RK_OP_NOT, RK_OP_SO, RK_OP_BNOT, RK_OP_STR,
    RK_OP__COUNT
};
