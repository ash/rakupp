// `--cnp`, the copy-and-patch backend — docs/dev/plans/CNP-PLAN.md.
//
// Three things live here:
//
//   1. THE HELPERS the stencils call. Every one of them is a catch-all
//      boundary: it does its work through the interpreter's own runtime, and
//      if that throws it puts the exception in the frame and returns a status.
//      Nothing may throw back into the code buffer, which has no unwind tables.
//   2. THE TRAMPOLINE. It unboxes the interpreter's containers into the
//      register file, calls the kernel, boxes them back, and rethrows anything
//      the frame is carrying — in a C++ frame, where a throw is ordinary again.
//   3. THE LOWERING: the whitelisted loop subtree into a linear register IR.
//
// The register file is the reason this is fast, and it is sound for exactly one
// reason, which is the same rule `--jit` rests on: the eligibility whitelist
// admits NO CALLS. With no call in the subtree, nothing running inside a kernel
// can observe a loop variable except through the kernel's own registers, so
// holding them unboxed from entry to exit is unobservable. Every widening of
// the whitelist has to answer that again.
#include "Cnp.h"
#include "Ast.h"
#include "Interpreter.h"
#include "Value.h"
#include "cnp/CnpAbi.h"
#include "cnp/CnpEmit.h"

#include <cstring>
#include <exception>
#include <map>
#include <set>
#include <vector>

namespace rakupp {
namespace cnp {

namespace {

inline double  bits2d(int64_t x) { double d; std::memcpy(&d, &x, 8); return d; }
inline int64_t d2bits(double d)  { int64_t x; std::memcpy(&x, &d, 8); return x; }

// The frame the trampoline owns and the stencils see. `abi` is the part the
// machine code knows about; everything else is C++ the helpers reach through it.
struct Frame {
    RkCnpFrame           abi{};
    std::vector<int64_t> r;
    std::vector<uint8_t> t;
    std::vector<Value>   boxes;
    std::exception_ptr   err;

    explicit Frame(size_t n) : r(n, 0), t(n, RK_T_INT), boxes(n) {
        abi.r = r.data();
        abi.t = t.data();
        abi.boxes = boxes.data();
        abi.err = &err;
    }
};

// Everything a helper needs is reachable through the ABI struct's own fields,
// so nothing here has to cast the frame back to the C++ type that contains it —
// which would mean an offsetof on a type holding vectors.
Interpreter& interpOf(RkCnpFrame* f) { return *static_cast<Interpreter*>(f->interp); }
void stash(RkCnpFrame* f) { *static_cast<std::exception_ptr*>(f->err) = std::current_exception(); }

// ---- register <-> Value ----------------------------------------------------

Value regValue(RkCnpFrame* f, uint64_t k) {
    switch (f->t[k]) {
        case RK_T_INT:  return Value::integer(f->r[k]);
        case RK_T_NUM:  return Value::number(bits2d(f->r[k]));
        case RK_T_BOOL: return Value::boolean(f->r[k] != 0);
        default:        return static_cast<Value*>(f->boxes)[k];
    }
}

// The unboxing rules, and why each exclusion is there:
//   * a bignum Int cannot be an int64 at all;
//   * `enumName` is the value's identity — `Less` prints as "Less", and a
//     register that kept only its .i would print as -1 the moment it was copied
//     into another container;
//   * everything else is a Str, a Rat, an object … and stays a Value.
// `natBits`/`natFloat` are deliberately NOT excluded: they describe what
// happens when something is STORED into that container, and the harness already
// refuses a native container the kernel would write to. Reading one is fine.
void setReg(RkCnpFrame* f, uint64_t k, const Value& v) {
    Value* boxes = static_cast<Value*>(f->boxes);
    uint8_t was = f->t[k];
    if (v.enumName.empty() && v.t == VT::Int && !v.big()) { f->r[k] = v.i; f->t[k] = RK_T_INT; }
    else if (v.enumName.empty() && v.t == VT::Num)        { f->r[k] = d2bits(v.n); f->t[k] = RK_T_NUM; }
    else if (v.enumName.empty() && v.t == VT::Bool)       { f->r[k] = v.b ? 1 : 0; f->t[k] = RK_T_BOOL; }
    else { boxes[k] = v; f->t[k] = RK_T_BOX; return; }
    // Release whatever the register used to hold, so that a loop cannot pin a
    // string or an object it stopped using thousands of iterations ago.
    if (was == RK_T_BOX) boxes[k] = Value();
}

// ---- the operator table ----------------------------------------------------
//
// One table, read from both ends: the lowering turns an operator into an index,
// a helper turns the index back into the string `applyArith` dispatches on. A
// single table is what makes it impossible for the two to disagree.
const char* const kOpNames[] = {
    "+", "-", "*", "/", "%", "**",
    "div", "mod", "%%", "gcd", "lcm",
    "<", "<=", ">", ">=", "==", "!=", "<=>",
    "eq", "ne", "lt", "gt", "le", "ge",
    "leg", "cmp", "~", "x",
    "+&", "+|", "+^", "+<", "+>",
    "min", "max", "^^",
    "\0neg", "\0plus", "\0not", "\0so", "\0bnot", "\0str",
};
static_assert(sizeof(kOpNames) / sizeof(*kOpNames) == RK_OP__COUNT,
              "kOpNames and enum RkCnpOp must stay in step");

int opIndex(const std::string& op) {
    for (int i = 0; i < RK_OP_NEG; i++) if (op == kOpNames[i]) return i;
    // `and`/`or` are short-circuit and never reach here; `xor` is the word form
    // of `^^`, which applyArith does take.
    if (op == "xor") return RK_OP_XOR;
    return -1;
}

}  // namespace

// ---- the helpers -----------------------------------------------------------
//
// THE RULE: none of these throws. Each is the boundary between a code buffer
// with no unwind tables and a runtime whose errors are C++ exceptions.
extern "C" {

int rk_cnp_binop(RkCnpFrame* f, uint64_t op, uint64_t d, uint64_t a, uint64_t b) {
    try {
        // `$s ~= …`, the compound-assign shape the lowering emits as
        // `binop(dst, dst, src)`. The general path below copies the accumulator
        // OUT of its box (regValue returns by value), builds a whole new string
        // beside it, and copies that back in — three O(n) passes per append, so
        // a loop that appends n times moves O(n²) bytes. Measured: 400,000
        // appends took 34.4 s against the interpreter's 0.06 s, and the answer
        // was right the whole time, which is why nothing caught it.
        //
        // rtCatAssign appends into the box instead. It is the same definition
        // the interpreter and `--exe` already use for `~=` — the comment on it
        // in Interpreter.h says "one definition for the interpreter and both
        // compiling backends", and this is the caller that was missing.
        if (d == a && op == (uint64_t)RK_OP_CONCAT && f->t[a] == RK_T_BOX) {
            Value* boxes = static_cast<Value*>(f->boxes);
            const Value vb = regValue(f, b);
            rtCatAssign(boxes[d], vb);
            return 0;
        }
        Value va = regValue(f, a), vb = regValue(f, b);
        setReg(f, d, applyArith(std::string(kOpNames[op]), va, vb));
        return 0;
    } catch (...) { stash(f); return 1; }
}

int rk_cnp_unop(RkCnpFrame* f, uint64_t op, uint64_t d, uint64_t a) {
    try {
        Value v = regValue(f, a);
        // Verbatim from what Codegen emits for the same operators, so that a
        // kernel and a compiled program cannot disagree about them.
        switch (op) {
            case RK_OP_NEG:  setReg(f, d, applyArith(std::string("-"), Value::integer(0), v)); break;
            case RK_OP_PLUS: setReg(f, d, applyArith(std::string("+"), Value::integer(0), v)); break;
            case RK_OP_NOT:  setReg(f, d, Value::boolean(!interpOf(f).boolify(v))); break;
            case RK_OP_SO:   setReg(f, d, Value::boolean(interpOf(f).boolify(v))); break;
            case RK_OP_STR:  setReg(f, d, Value::str(v.toStr())); break;
            case RK_OP_BNOT: setReg(f, d, Value::integer(~v.toInt())); break;
            default: return 1;
        }
        return 0;
    } catch (...) { stash(f); return 1; }
}

int rk_cnp_cmp(RkCnpFrame* f, uint64_t op, uint64_t a, uint64_t b) {
    try {
        Value va = regValue(f, a), vb = regValue(f, b);
        return interpOf(f).boolify(applyArith(std::string(kOpNames[op]), va, vb)) ? 1 : 0;
    } catch (...) { stash(f); return -1; }
}

int rk_cnp_truthy(RkCnpFrame* f, uint64_t a) {
    try { return interpOf(f).boolify(regValue(f, a)) ? 1 : 0; }
    catch (...) { stash(f); return -1; }
}

int rk_cnp_defined(RkCnpFrame* f, uint64_t a) {
    try { return rtIsDefined(regValue(f, a)) ? 1 : 0; }
    catch (...) { stash(f); return -1; }
}

int rk_cnp_loadk(RkCnpFrame* f, uint64_t d, uint64_t k) {
    try { setReg(f, d, (*static_cast<const std::vector<Value>*>(f->consts))[k]); return 0; }
    catch (...) { stash(f); return 1; }
}

int rk_cnp_move(RkCnpFrame* f, uint64_t d, uint64_t a) {
    try { setReg(f, d, static_cast<Value*>(f->boxes)[a]); return 0; }
    catch (...) { stash(f); return 1; }
}

}  // extern "C"

// ---- the kernel ------------------------------------------------------------

struct Kernel {
    std::unique_ptr<Code> code;
    std::vector<Value>    consts;
    size_t                nregs = 0;
    size_t                nops  = 0;
};

namespace {

// ---- lowering --------------------------------------------------------------

// The stencils, by the name they have in stencils.c. Resolved once; a name that
// is not there turns `--cnp` off with a message rather than emitting a wrong
// index.
struct StencilIds {
    int loadi, loadn, loadb, loadk, move, movebox;
    int add, sub, mul, addi, incr, neg, notOp, binop, unop;
    int cmp[6];          // lt le gt ge eq ne — the value form
    int jcmp[6], jcmpi[6], jncmp[6], jncmpi[6];
    int jmp, jt, jf, jdef, ret, jtslow, jfslow, jdefslow;
    bool ok = false;
};

const StencilIds& ids() {
    static StencilIds s = [] {
        StencilIds v{};
        auto g = [&](const char* n) { int i = stencilIndex(n); if (i < 0) v.ok = false; return i; };
        v.ok = true;
        v.loadi = g("rk_st_loadi"); v.loadn = g("rk_st_loadn"); v.loadb = g("rk_st_loadb");
        v.loadk = g("rk_st_loadk"); v.move  = g("rk_st_move");  v.movebox = g("rk_st_movebox");
        v.add = g("rk_st_add"); v.sub = g("rk_st_sub"); v.mul = g("rk_st_mul");
        v.addi = g("rk_st_addi"); v.incr = g("rk_st_incr");
        v.neg = g("rk_st_neg"); v.notOp = g("rk_st_not");
        v.binop = g("rk_st_binop"); v.unop = g("rk_st_unop");
        static const char* cv[6] = {"rk_st_cmplt","rk_st_cmple","rk_st_cmpgt","rk_st_cmpge","rk_st_cmpeq","rk_st_cmpne"};
        static const char* jb[6] = {"rk_st_jlt","rk_st_jle","rk_st_jgt","rk_st_jge","rk_st_jeq","rk_st_jne"};
        static const char* ji[6] = {"rk_st_jlti","rk_st_jlei","rk_st_jgti","rk_st_jgei","rk_st_jeqi","rk_st_jnei"};
        static const char* nb[6] = {"rk_st_jnlt","rk_st_jnle","rk_st_jngt","rk_st_jnge","rk_st_jneq","rk_st_jnne"};
        static const char* ni[6] = {"rk_st_jnlti","rk_st_jnlei","rk_st_jngti","rk_st_jngei","rk_st_jneqi","rk_st_jnnei"};
        for (int i = 0; i < 6; i++) {
            v.cmp[i] = g(cv[i]); v.jcmp[i] = g(jb[i]); v.jcmpi[i] = g(ji[i]);
            v.jncmp[i] = g(nb[i]); v.jncmpi[i] = g(ni[i]);
        }
        v.jmp = g("rk_st_jmp"); v.jt = g("rk_st_jt"); v.jf = g("rk_st_jf"); v.jdef = g("rk_st_jdef");
        v.ret = g("rk_st_ret");
        v.jtslow = g("rk_st_jtslow"); v.jfslow = g("rk_st_jfslow"); v.jdefslow = g("rk_st_jdefslow");
        return v;
    }();
    return s;
}

// Which of the six comparisons an operator is, or -1.
int cmpSlot(const std::string& op) {
    if (op == "<")  return 0;
    if (op == "<=") return 1;
    if (op == ">")  return 2;
    if (op == ">=") return 3;
    if (op == "==") return 4;
    if (op == "!=") return 5;
    return -1;
}
int cmpOpIndex(int slot) {
    static const int m[6] = { RK_OP_LT, RK_OP_LE, RK_OP_GT, RK_OP_GE, RK_OP_EQ, RK_OP_NE };
    return m[slot];
}

// A cold block, deferred until every hot op exists. Emitting it inline would
// have meant knowing the index of the op that comes AFTER the one bailing out,
// which is not known while that one is being emitted.
struct ColdReq {
    enum Kind { Binop, BinopImm, CmpBranch, CmpBranchImm, Unop, Movebox, Truthy, Defined } kind;
    int      hot = -1;            // the op whose guard bails here
    uint64_t a = 0, b = 0, c = 0; // meaning depends on kind
    int      op = 0;              // an RkCnpOp
    int      scratch = 0, tmp = 0;
    bool     negate = false;      // the branch forms: bail when the test is FALSE
};

struct Lower {
    std::string err;
    std::vector<Op>      ops;
    std::vector<ColdReq> colds;
    std::vector<Value>   consts;
    std::vector<std::map<std::string, int>> scopes;
    int nNamed = 0;          // registers reserved for slots and `my` variables
    int namedTop = 0;
    int tempTop = 0, maxTemp = 0;
    // `last` and `next` sites waiting for their loop to finish being lowered.
    struct LoopCtx { std::vector<int> breaks, continues; };
    std::vector<LoopCtx> loops;

    void fail(const std::string& m) { if (err.empty()) err = m; }
    bool bad() const { return !err.empty(); }

    int emit(int stencil, uint64_t o0 = 0, uint64_t o1 = 0, uint64_t o2 = 0, uint64_t o3 = 0) {
        Op o;
        o.stencil = (uint16_t)stencil;
        o.operand[0] = o0; o.operand[1] = o1; o.operand[2] = o2; o.operand[3] = o3;
        ops.push_back(o);
        return (int)ops.size() - 1;
    }

    // Temps are handed out above the named registers and given back only at
    // statement boundaries, which is the whole of their lifetime: no expression
    // value outlives the statement that produced it.
    int temp() { int r = nNamed + tempTop++; if (tempTop > maxTemp) maxTemp = tempTop; return r; }
    struct TempMark {
        Lower& L; int save;
        explicit TempMark(Lower& l) : L(l), save(l.tempTop) {}
        ~TempMark() { L.tempTop = save; }
    };

    int declare(const std::string& n) {
        int r = namedTop++;
        if (namedTop > nNamed) { fail("more variables than the register pre-count found"); return 0; }
        scopes.back()[n] = r;
        return r;
    }
    int lookup(const std::string& n) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(n);
            if (f != it->end()) return f->second;
        }
        fail("variable " + n + " has no register");
        return 0;
    }

    int constant(const Value& v) {
        consts.push_back(v);
        return (int)consts.size() - 1;
    }

    // ---- expressions -------------------------------------------------------

    // The register holding `e`'s value. May be a variable's own register, so a
    // caller that means to WRITE must copy first.
    int expr(Expr* e);
    // `e` into a specific register.
    void exprInto(Expr* e, int dst);
    // Branch to a site (returned, for patching) when `cond` is true / false.
    int branch(Expr* cond, bool whenTrue);
    void stmt(Stmt* s);
    void block(Block* b, bool ownScope);
    void headerExpr(Expr* e);
    void loopStmt(Stmt* s, bool outermost);

    void patch(int site, int target) { ops[site].target = target; }
    int  here() const { return (int)ops.size(); }

    // Resolve fallthroughs, then build every cold block. Hot ops were all
    // emitted first, so appending the cold ones leaves the vector already
    // partitioned and no renumbering is needed.
    void finish();
};

// An integer literal small enough to ride in the instruction stream.
bool intLit(Expr* e, long long& out) {
    if (!e || e->kind != NK::IntLit) return false;
    auto* l = static_cast<IntLit*>(e);
    if (!l->big.empty()) return false;
    out = l->v;
    return true;
}

// The Value a literal node evaluates to. Copied from the interpreter's own arms
// rather than re-derived, because the interesting cases are not obvious: a
// decimal literal like `3.14` is a RAT in Raku and not a Num, `1i` is a Complex,
// and an integer literal too big for an int64 carries its digits as a string.
Value literalValue(Expr* e) {
    switch (e->kind) {
        case NK::IntLit: {
            auto* il = static_cast<IntLit*>(e);
            return il->big.empty() ? Value::integer(il->v) : Value::bigint(BigInt::fromString(il->big));
        }
        case NK::NumLit: {
            auto* nl = static_cast<NumLit*>(e);
            if (nl->imaginary) return Value::complex(0, nl->v);
            if (nl->isRat)
                return nl->bigNum.empty()
                    ? Value::ratZ(BigInt(nl->ratNum), BigInt(nl->ratDen))
                    : Value::ratZ(BigInt::fromString(nl->bigNum), BigInt::fromString(nl->bigDen));
            return Value::number(nl->v);
        }
        case NK::StrLit:  return Value::str(static_cast<StrLit*>(e)->v);
        case NK::BoolLit: return Value::boolean(static_cast<BoolLit*>(e)->v);
        default:          return Value();
    }
}

void Lower::exprInto(Expr* e, int dst) {
    int r = expr(e);
    if (bad() || r == dst) return;
    int m = emit(ids().move, (uint64_t)dst, (uint64_t)r);
    colds.push_back({ColdReq::Movebox, m, (uint64_t)dst, (uint64_t)r, 0, 0, 0, 0, false});
}

int Lower::expr(Expr* e) {
    if (bad()) return 0;
    if (!e) { fail("an empty expression"); return 0; }
    switch (e->kind) {
        case NK::IntLit: {
            auto* l = static_cast<IntLit*>(e);
            int d = temp();
            if (l->big.empty()) emit(ids().loadi, (uint64_t)d, (uint64_t)l->v);
            else                emit(ids().loadk, (uint64_t)d, (uint64_t)constant(literalValue(e)));
            return d;
        }
        case NK::NumLit: {
            auto* nl = static_cast<NumLit*>(e);
            int d = temp();
            // Only a plain Num rides unboxed; a Rat or a Complex is a constant.
            if (!nl->isRat && !nl->imaginary) emit(ids().loadn, (uint64_t)d, (uint64_t)d2bits(nl->v));
            else emit(ids().loadk, (uint64_t)d, (uint64_t)constant(literalValue(e)));
            return d;
        }
        case NK::BoolLit: {
            int d = temp();
            emit(ids().loadb, (uint64_t)d, static_cast<BoolLit*>(e)->v ? 1u : 0u);
            return d;
        }
        case NK::StrLit: {
            int d = temp();
            emit(ids().loadk, (uint64_t)d, (uint64_t)constant(Value::str(static_cast<StrLit*>(e)->v)));
            return d;
        }
        case NK::InterpStr: {
            // The whitelist only admits one with nothing to interpolate.
            std::string s;
            for (auto& p : static_cast<InterpStr*>(e)->parts) {
                if (!p || p->kind != NK::StrLit) { fail("an interpolated string"); return 0; }
                s += static_cast<StrLit*>(p.get())->v;
            }
            int d = temp();
            emit(ids().loadk, (uint64_t)d, (uint64_t)constant(Value::str(s)));
            return d;
        }
        case NK::VarExpr: {
            auto* v = static_cast<VarExpr*>(e);
            if (v->declare) {
                int r = declare(v->name);
                // `my $x;` with no initialiser is an undefined Any, and it is
                // reset on every pass through the declaration.
                emit(ids().loadk, (uint64_t)r, (uint64_t)constant(Value()));
                return r;
            }
            return lookup(v->name);
        }
        case NK::Assign: {
            auto* a = static_cast<Assign*>(e);
            if (a->target->kind != NK::VarExpr) { fail("assignment to a non-variable"); return 0; }
            auto* tv = static_cast<VarExpr*>(a->target.get());
            if (a->op == "=") {
                // The VALUE first, so that `my $x = $x` reads the OUTER `$x`
                // before the declaration shadows it — Raku's own order, and the
                // one the eligibility walk assumed.
                int src = expr(a->value.get());
                if (bad()) return 0;
                int dst = tv->declare ? declare(tv->name) : lookup(tv->name);
                if (bad()) return 0;
                if (src != dst) {
                    int m = emit(ids().move, (uint64_t)dst, (uint64_t)src);
                    colds.push_back({ColdReq::Movebox, m, (uint64_t)dst, (uint64_t)src, 0, 0, 0, 0, false});
                }
                return dst;
            }
            // Compound assignment. `$x op= V` is `$x = $x op V`, and the
            // destination is the variable's own register.
            if (tv->declare) { fail("a compound assignment to a declaration"); return 0; }
            int dst = lookup(tv->name);
            if (bad()) return 0;
            std::string op = a->op.substr(0, a->op.size() - 1);
            long long k = 0;
            if ((op == "+" || op == "-") && intLit(a->value.get(), k)) {
                long long delta = op == "+" ? k : -k;
                int scratch = temp();
                int h = emit(ids().incr, (uint64_t)dst, (uint64_t)delta, (uint64_t)scratch);
                colds.push_back({ColdReq::BinopImm, h, (uint64_t)dst, (uint64_t)dst, (uint64_t)delta,
                                 RK_OP_ADD, scratch, 0, false});
                return dst;
            }
            int src = expr(a->value.get());
            if (bad()) return 0;
            int oi = opIndex(op);
            if (oi < 0) { fail("compound operator '" + a->op + "'"); return 0; }
            int fast = op == "+" ? ids().add : op == "-" ? ids().sub : op == "*" ? ids().mul : -1;
            if (fast >= 0) {
                int h = emit(fast, (uint64_t)dst, (uint64_t)dst, (uint64_t)src);
                colds.push_back({ColdReq::Binop, h, (uint64_t)dst, (uint64_t)dst, (uint64_t)src, oi, 0, 0, false});
            } else {
                emit(ids().binop, (uint64_t)dst, (uint64_t)dst, (uint64_t)src, (uint64_t)oi);
            }
            return dst;
        }
        case NK::Binary: {
            auto* b = static_cast<Binary*>(e);
            const std::string& op = b->op;
            // The three short-circuit operators are control flow, not calls
            // into the operator table — `$a && $b` never evaluates `$b` when
            // `$a` is false, and it yields an OPERAND, not a Bool.
            if (op == "&&" || op == "and" || op == "||" || op == "or" || op == "//") {
                int d = temp();
                exprInto(b->lhs.get(), d);
                if (bad()) return 0;
                int skip;
                if (op == "//") skip = emit(ids().jdef, (uint64_t)d);
                else if (op == "&&" || op == "and") skip = emit(ids().jf, (uint64_t)d);
                else skip = emit(ids().jt, (uint64_t)d);
                colds.push_back({op == "//" ? ColdReq::Defined : ColdReq::Truthy,
                                 skip, (uint64_t)d, 0, 0, 0, 0, 0, false});
                exprInto(b->rhs.get(), d);
                patch(skip, here());
                return d;
            }
            int oi = opIndex(op);
            if (oi < 0) { fail("operator '" + op + "'"); return 0; }
            int cs = cmpSlot(op);
            long long k = 0;
            if ((op == "+" || op == "-") && intLit(b->rhs.get(), k)) {
                int a = expr(b->lhs.get());
                if (bad()) return 0;
                int d = temp(), scratch = temp();
                long long delta = op == "+" ? k : -k;
                int h = emit(ids().addi, (uint64_t)d, (uint64_t)a, (uint64_t)delta, (uint64_t)scratch);
                colds.push_back({ColdReq::BinopImm, h, (uint64_t)d, (uint64_t)a, (uint64_t)delta,
                                 RK_OP_ADD, scratch, 0, false});
                return d;
            }
            int a = expr(b->lhs.get());
            if (bad()) return 0;
            int r = expr(b->rhs.get());
            if (bad()) return 0;
            int d = temp();
            int fast = op == "+" ? ids().add : op == "-" ? ids().sub : op == "*" ? ids().mul
                     : cs >= 0 ? ids().cmp[cs] : -1;
            if (fast >= 0) {
                int h = emit(fast, (uint64_t)d, (uint64_t)a, (uint64_t)r);
                colds.push_back({ColdReq::Binop, h, (uint64_t)d, (uint64_t)a, (uint64_t)r, oi, 0, 0, false});
            } else {
                emit(ids().binop, (uint64_t)d, (uint64_t)a, (uint64_t)r, (uint64_t)oi);
            }
            return d;
        }
        case NK::Unary: {
            auto* u = static_cast<Unary*>(e);
            const std::string& op = u->op;
            if (op == "++" || op == "--") {
                if (u->operand->kind != NK::VarExpr) { fail("++/-- on a non-variable"); return 0; }
                int r = lookup(static_cast<VarExpr*>(u->operand.get())->name);
                if (bad()) return 0;
                long long delta = op == "++" ? 1 : -1;
                int old = -1;
                if (u->postfix) {            // the value is what it held BEFORE
                    old = temp();
                    int m = emit(ids().move, (uint64_t)old, (uint64_t)r);
                    colds.push_back({ColdReq::Movebox, m, (uint64_t)old, (uint64_t)r, 0, 0, 0, 0, false});
                }
                int scratch = temp();
                int h = emit(ids().incr, (uint64_t)r, (uint64_t)delta, (uint64_t)scratch);
                colds.push_back({ColdReq::BinopImm, h, (uint64_t)r, (uint64_t)r, (uint64_t)delta,
                                 RK_OP_ADD, scratch, 0, false});
                return u->postfix ? old : r;
            }
            int a = expr(u->operand.get());
            if (bad()) return 0;
            int d = temp();
            if (op == "-") {
                int h = emit(ids().neg, (uint64_t)d, (uint64_t)a);
                colds.push_back({ColdReq::Unop, h, (uint64_t)d, (uint64_t)a, 0, RK_OP_NEG, 0, 0, false});
                return d;
            }
            if (op == "!" || op == "not") {
                int h = emit(ids().notOp, (uint64_t)d, (uint64_t)a);
                colds.push_back({ColdReq::Unop, h, (uint64_t)d, (uint64_t)a, 0, RK_OP_NOT, 0, 0, false});
                return d;
            }
            int oi = op == "+" ? RK_OP_PLUS : op == "?" ? RK_OP_SO
                   : op == "~" ? RK_OP_STR  : op == "+^" ? RK_OP_BNOT : -1;
            if (oi < 0) { fail("prefix operator '" + op + "'"); return 0; }
            emit(ids().unop, (uint64_t)d, (uint64_t)a, (uint64_t)oi);
            return d;
        }
        case NK::Ternary: {
            auto* t = static_cast<Ternary*>(e);
            int d = temp();
            int toElse = branch(t->cond.get(), false);
            if (bad()) return 0;
            exprInto(t->then.get(), d);
            int done = emit(ids().jmp);
            patch(toElse, here());
            exprInto(t->els.get(), d);
            patch(done, here());
            return d;
        }
        case NK::ListExpr:
            fail("a list in value position");
            return 0;
        default:
            fail("an expression the lowering has no stencil for");
            return 0;
    }
}

// A conditional branch. Returns the op whose target still has to be patched.
//
// A comparison is FUSED into the branch — `while $i < $n` is one stencil with
// the test and the back edge in it, and no Bool is ever built. The negated
// stencils exist so that "jump when the condition is false" needs no operator
// inversion: `!(a < b)` and `a >= b` are different questions once an operand is
// NaN, and getting that wrong would have been invisible until it mattered.
int Lower::branch(Expr* cond, bool whenTrue) {
    if (bad()) return 0;
    if (cond && cond->kind == NK::Binary) {
        auto* b = static_cast<Binary*>(cond);
        int cs = cmpSlot(b->op);
        if (cs >= 0) {
            long long k = 0;
            if (intLit(b->rhs.get(), k)) {
                int a = expr(b->lhs.get());
                if (bad()) return 0;
                int scratch = temp(), tmp = temp();
                int st = whenTrue ? ids().jcmpi[cs] : ids().jncmpi[cs];
                int h = emit(st, (uint64_t)a, (uint64_t)k, (uint64_t)scratch);
                colds.push_back({ColdReq::CmpBranchImm, h, (uint64_t)a, (uint64_t)k, 0,
                                 cmpOpIndex(cs), scratch, tmp, !whenTrue});
                return h;
            }
            int a = expr(b->lhs.get());
            if (bad()) return 0;
            int r = expr(b->rhs.get());
            if (bad()) return 0;
            int tmp = temp();
            int st = whenTrue ? ids().jcmp[cs] : ids().jncmp[cs];
            int h = emit(st, (uint64_t)a, (uint64_t)r);
            colds.push_back({ColdReq::CmpBranch, h, (uint64_t)a, (uint64_t)r, 0,
                             cmpOpIndex(cs), 0, tmp, !whenTrue});
            return h;
        }
    }
    int c = expr(cond);
    if (bad()) return 0;
    int h = emit(whenTrue ? ids().jt : ids().jf, (uint64_t)c);
    colds.push_back({ColdReq::Truthy, h, (uint64_t)c, 0, 0, 0, 0, 0, !whenTrue});
    return h;
}

void Lower::headerExpr(Expr* e) {
    if (bad() || !e) return;
    if (e->kind == NK::ListExpr) {
        for (auto& it : static_cast<ListExpr*>(e)->items) { TempMark m(*this); expr(it.get()); if (bad()) return; }
        return;
    }
    TempMark m(*this);
    expr(e);
}

void Lower::block(Block* b, bool ownScope) {
    if (bad() || !b) return;
    if (ownScope) scopes.push_back({});
    for (auto& s : b->stmts) { stmt(s.get()); if (bad()) break; }
    if (ownScope) scopes.pop_back();
}

void Lower::loopStmt(Stmt* s, bool outermost) {
    loops.push_back({});
    if (s->kind == NK::WhileStmt) {
        auto* w = static_cast<WhileStmt*>(s);
        int top = here();
        // `until` is `while not`: the same test, taken the other way.
        int out = branch(w->cond.get(), w->isUntil);
        if (bad()) { loops.pop_back(); return; }
        block(w->body.get(), true);
        int back = emit(ids().jmp);
        patch(back, top);
        patch(out, here());
        for (int c : loops.back().continues) patch(c, top);
        for (int c : loops.back().breaks)    patch(c, here());
    } else {
        auto* l = static_cast<LoopStmt*>(s);
        scopes.push_back({});
        // The OUTERMOST loop's init is NOT emitted at all. The interpreter ran
        // it before the kernel was entered, and every name it declared is
        // already a slot the kernel binds — so there is nothing left to walk,
        // and walking it anyway re-ran `my $i = 0` on entry and restarted the
        // loop from the top. The gate caught that as one extra iteration, which
        // is what re-running a zeroed counter looks like from the outside.
        if (!outermost) headerExpr(l->init.get());
        int top = here();
        int out = -1;
        if (l->cond) { out = branch(l->cond.get(), false); if (bad()) { scopes.pop_back(); loops.pop_back(); return; } }
        block(l->body.get(), true);
        int step = here();
        headerExpr(l->incr.get());
        int back = emit(ids().jmp);
        patch(back, top);
        if (out >= 0) patch(out, here());
        // `next` in a three-clause loop has to reach the STEP, which is what a
        // C++ `continue` in a `for` does and what rewriting it as a `while`
        // would silently break.
        for (int c : loops.back().continues) patch(c, step);
        for (int c : loops.back().breaks)    patch(c, here());
        scopes.pop_back();
    }
    loops.pop_back();
}

void Lower::stmt(Stmt* s) {
    if (bad() || !s) return;
    switch (s->kind) {
        case NK::EmptyStmt: return;
        case NK::ExprStmt: { TempMark m(*this); expr(static_cast<ExprStmt*>(s)->e.get()); return; }
        case NK::LastStmt:
            if (loops.empty()) { fail("a `last` outside the kernel's loops"); return; }
            loops.back().breaks.push_back(emit(ids().jmp));
            return;
        case NK::NextStmt:
            if (loops.empty()) { fail("a `next` outside the kernel's loops"); return; }
            loops.back().continues.push_back(emit(ids().jmp));
            return;
        case NK::IfStmt: {
            auto* f = static_cast<IfStmt*>(s);
            std::vector<int> ends;
            for (size_t i = 0; i < f->branches.size(); i++) {
                int nextArm;
                { TempMark m(*this); nextArm = branch(f->branches[i].first.get(), false); }
                if (bad()) return;
                // A postfix `STMT if COND` has no block of its own, so a `my` in
                // it declares in the ENCLOSING scope.
                block(f->branches[i].second.get(), !f->modifier);
                if (bad()) return;
                bool more = (i + 1 < f->branches.size()) || f->elseBlock;
                if (more) ends.push_back(emit(ids().jmp));
                patch(nextArm, here());
            }
            if (f->elseBlock) block(f->elseBlock.get(), !f->modifier);
            for (int e : ends) patch(e, here());
            return;
        }
        case NK::Block:     block(static_cast<Block*>(s), true); return;
        case NK::WhileStmt:
        case NK::LoopStmt:  loopStmt(s, false); return;
        default: fail("a statement the lowering has no stencil for"); return;
    }
}

void Lower::finish() {
    if (bad()) return;
    // Fallthroughs first, so a cold block can read where its hot op was going.
    for (size_t i = 0; i < ops.size(); i++)
        if (ops[i].cont < 0) ops[i].cont = (int)i + 1;

    for (const ColdReq& c : colds) {
        int cont = ops[c.hot].cont, target = ops[c.hot].target;
        int start = here();
        switch (c.kind) {
            case ColdReq::Binop:
                emit(ids().binop, c.a, c.b, c.c, (uint64_t)c.op);
                ops.back().cont = cont;
                break;
            case ColdReq::BinopImm:
                emit(ids().loadi, (uint64_t)c.scratch, c.c);
                ops.back().cont = (int)ops.size();
                emit(ids().binop, c.a, c.b, (uint64_t)c.scratch, (uint64_t)c.op);
                ops.back().cont = cont;
                break;
            case ColdReq::Unop:
                emit(ids().unop, c.a, c.b, (uint64_t)c.op);
                ops.back().cont = cont;
                break;
            case ColdReq::Movebox:
                emit(ids().movebox, c.a, c.b);
                ops.back().cont = cont;
                break;
            case ColdReq::Truthy: {
                int h = emit(c.negate ? ids().jfslow : ids().jtslow, c.a);
                ops[h].cont = cont; ops[h].target = target;
                break;
            }
            case ColdReq::Defined: {
                int h = emit(ids().jdefslow, c.a);
                ops[h].cont = cont; ops[h].target = target;
                break;
            }
            case ColdReq::CmpBranch:
            case ColdReq::CmpBranchImm: {
                uint64_t rhs = c.b;
                if (c.kind == ColdReq::CmpBranchImm) {
                    emit(ids().loadi, (uint64_t)c.scratch, c.b);
                    ops.back().cont = (int)ops.size();
                    rhs = (uint64_t)c.scratch;
                }
                emit(ids().binop, (uint64_t)c.tmp, c.a, rhs, (uint64_t)c.op);
                ops.back().cont = (int)ops.size();
                // The comparison's Bool decides the same way the fast lane did.
                int h = emit(c.negate ? ids().jf : ids().jt, (uint64_t)c.tmp);
                ops[h].cont = cont; ops[h].target = target;
                int sl = emit(c.negate ? ids().jfslow : ids().jtslow, (uint64_t)c.tmp);
                ops[sl].cont = cont; ops[sl].target = target;
                ops[h].slow = sl;
                break;
            }
        }
        ops[c.hot].slow = start;
    }
    // The cold ops just appended default to falling through to whatever follows
    // them, which is another cold block. Every one of them was given an explicit
    // cont above, except the jt/jf pairs, which have both.
    for (size_t i = 0; i < ops.size(); i++)
        if (ops[i].cont < 0) ops[i].cont = (int)i + 1;
}

// How many registers the named things need. A pre-pass rather than an
// allocation as we go, because a `my` inside an expression would otherwise be
// handed a register a live temporary was already using.
int countDecls(Expr* e);
int countDecls(Stmt* s);

int countDecls(Expr* e) {
    if (!e) return 0;
    switch (e->kind) {
        case NK::VarExpr: return static_cast<VarExpr*>(e)->declare ? 1 : 0;
        case NK::Assign: { auto* a = static_cast<Assign*>(e); return countDecls(a->target.get()) + countDecls(a->value.get()); }
        case NK::Binary: { auto* b = static_cast<Binary*>(e); return countDecls(b->lhs.get()) + countDecls(b->rhs.get()); }
        case NK::Unary:  return countDecls(static_cast<Unary*>(e)->operand.get());
        case NK::Ternary: { auto* t = static_cast<Ternary*>(e);
                            return countDecls(t->cond.get()) + countDecls(t->then.get()) + countDecls(t->els.get()); }
        case NK::ListExpr: { int n = 0; for (auto& i : static_cast<ListExpr*>(e)->items) n += countDecls(i.get()); return n; }
        default: return 0;
    }
}

int countDecls(Stmt* s) {
    if (!s) return 0;
    switch (s->kind) {
        case NK::ExprStmt: return countDecls(static_cast<ExprStmt*>(s)->e.get());
        case NK::Block: { int n = 0; for (auto& x : static_cast<Block*>(s)->stmts) n += countDecls(x.get()); return n; }
        case NK::IfStmt: {
            auto* f = static_cast<IfStmt*>(s);
            int n = 0;
            for (auto& br : f->branches) { n += countDecls(br.first.get()); n += countDecls(br.second.get()); }
            if (f->elseBlock) n += countDecls(f->elseBlock.get());
            return n;
        }
        case NK::WhileStmt: { auto* w = static_cast<WhileStmt*>(s);
                              return countDecls(w->cond.get()) + countDecls(w->body.get()); }
        case NK::LoopStmt: { auto* l = static_cast<LoopStmt*>(s);
                             return countDecls(l->init.get()) + countDecls(l->cond.get()) +
                                    countDecls(l->incr.get()) + countDecls(l->body.get()); }
        default: return 0;
    }
}

// A kernel bigger than this is not what `--cnp` is for: the copy is linear in
// the op count, and a loop with thousands of nodes is not where the time goes.
constexpr size_t kMaxOps = 4096;
constexpr size_t kMaxRegs = 1024;

}  // namespace

bool available() { return stencilsAvailable() && ids().ok; }

const char* unavailableReason() {
    if (!ids().ok && stencilsAvailable())
        return "the stencil table does not match this build's lowering";
    return stencilsUnavailableReason();
}

Kernel* compile(Stmt* loop, const std::vector<std::string>& slots, std::string& why) {
    if (!available()) { why = unavailableReason(); return nullptr; }
    Lower L;
    L.nNamed = (int)slots.size() + countDecls(loop) + 4;
    if (L.nNamed > (int)kMaxRegs) { why = "too many variables"; return nullptr; }
    L.scopes.push_back({});
    for (size_t i = 0; i < slots.size(); i++) L.scopes.back()[slots[i]] = (int)i;
    L.namedTop = (int)slots.size();

    L.loopStmt(loop, true);
    if (!L.bad()) L.emit(ids().ret);
    L.finish();
    if (L.bad()) { why = L.err; return nullptr; }
    if (L.ops.size() > kMaxOps) { why = "the loop lowers to more ops than a kernel holds"; return nullptr; }
    size_t nregs = (size_t)L.nNamed + (size_t)L.maxTemp;
    if (nregs > kMaxRegs) { why = "the loop needs more registers than a kernel holds"; return nullptr; }

    std::string err;
    auto code = assemble(L.ops, err);
    if (!code) { why = "the assembler refused: " + err; return nullptr; }

    Kernel* k = new Kernel();
    k->code = std::move(code);
    k->consts = std::move(L.consts);
    k->nregs = nregs;
    k->nops = L.ops.size();
    return k;
}

bool run(Kernel* k, Interpreter& I, Value** slots, const std::vector<bool>& written,
         std::string& why) {
    size_t n = written.size();
    // Two names that resolve to ONE container would be two registers here and
    // one cell in the interpreter, and the write-back would drop whichever went
    // last. The harness's own guards do not see this case, so it is checked on
    // the spot; `n` is a handful, so the pairwise scan is cheaper than a set.
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < i; j++)
            if (slots[i] == slots[j]) {
                why = "two of this loop's variables share one container";
                return false;
            }

    Frame f(k->nregs);
    f.abi.interp = &I;
    f.abi.consts = (void*)&k->consts;

    for (size_t i = 0; i < n; i++) setReg(&f.abi, i, *slots[i]);

    int rc = reinterpret_cast<RkCnpFn>(k->code->entry())(&f.abi, f.r.data(), f.t.data());

    // The write-back happens on BOTH exits. A loop that dies half way has to
    // leave the same values behind as an interpreted one would, because a CATCH
    // outside is about to read them.
    for (size_t i = 0; i < n; i++) if (written[i]) *slots[i] = regValue(&f.abi, i);

    if (rc != RK_CNP_OK) {
        // Every RK_CNP_ERR follows a helper that stashed something, so the
        // second arm is unreachable by construction. It is here so that a
        // future stencil which returns an error WITHOUT one does not silently
        // look like a successful loop.
        if (f.err) std::rethrow_exception(f.err);
        why = "the kernel reported an error it did not record";
        return false;
    }
    return true;
}

size_t codeBytes(const Kernel* k) { return k && k->code ? k->code->bytes() : 0; }
size_t opCount(const Kernel* k)   { return k ? k->nops : 0; }
size_t regCount(const Kernel* k)  { return k ? k->nregs : 0; }

}  // namespace cnp
}  // namespace rakupp
