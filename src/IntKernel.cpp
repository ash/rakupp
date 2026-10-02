// Integer kernels: a plain sub whose body is closed integer arithmetic runs on
// machine int64s instead of Values.
//
// A sub qualifies when its body uses nothing but its own `$` parameters,
// integer literals, `+ - * div % %%`, the six comparisons, `&& || ! not` in a
// condition, `?? !!`, `if`/`elsif`/`else`/`unless`, `return`, and calls to
// subs that qualify themselves (fib, ackermann, tak, gcd). Such a body has no
// side effects: it reads its arguments and computes. That is what makes the
// whole scheme safe. A kernel that meets anything it cannot answer exactly —
// an argument that is not a plain Int, a result that would need a BigInt, a
// zero divisor, the generic path's recursion limit — BAILS: it abandons the
// attempt and the call runs the ordinary way from the start, so the program
// sees exactly what it would have seen without kernels, error messages
// included. Nothing the kernel did is observable.
//
// The checks that can change between calls are made once per ENTRY into a
// kernel, not per recursive call: no routine in the kernel's call graph is
// wrapped, no operator it uses is shadowed by a user `infix:<…>` /
// `prefix:<…>`, and every argument is a plain Int. Inside a run nothing can
// change them, because the body cannot run user code.
//
// RAKUPP_NO_KERNELS=1 turns the whole thing off (A/B and gates);
// RAKUPP_KERNEL_TRACE=1 names each routine as its kernel is decided.
#include "InterpreterParts.h"
#include "IntOps.h"
#include <climits>
#include <cstdlib>
#include <mutex>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>   // _AddressOfReturnAddress
#endif

namespace rakupp {

namespace {

enum class KOp : uint8_t {
    Param, Lit,
    Add, Sub, Mul, Div, Mod, DivBy, Neg,
    Lt, Le, Gt, Ge, Eq, Ne,
    And, Or, Not,
    Cond,     // a ? b : c (also an if/else in value position)
    Call,
    Seq,      // statements; the value is the last one's
    If,       // a statement: a ? b : c, c may be null
    Ret,      // return a
};

struct IKernel;
struct KNode;
struct KRun;
using KFn = int64_t (*)(const KNode*, const int64_t*, KRun&);

struct KNode {
    KFn fn = nullptr;           // set by link once the kernel is built
    KOp op;
    uint8_t nargs = 0;
    int32_t slot = 0;           // Param: the argument index
    int64_t lit = 0;            // Lit
    KNode* a = nullptr;
    KNode* b = nullptr;
    KNode* c = nullptr;
    IKernel* callee = nullptr;  // Call
    KNode** kids = nullptr;     // Call: nargs arguments; Seq: nargs statements
};

constexpr int kMaxParams = 6;

struct IKernel {
    Callable* owner = nullptr;
    int nparams = 0;
    KNode* body = nullptr;
    std::vector<std::unique_ptr<KNode>> nodes;
    std::vector<std::unique_ptr<KNode*[]>> lists;
    std::vector<IKernel*> callees;         // direct, may include this
    std::vector<Value> keepAlive;          // the callees' Code values (not this one)
    std::vector<Callable*> calleeOwners;   // the routines `callees` run (alive: keepAlive, or this one's caller)
    // Filled once the compile session that built this kernel succeeds:
    std::vector<IKernel*> graph;           // every kernel reachable, this included
    std::vector<std::string> binOps;       // every infix spelling the graph uses
    bool usesPrefix = false;               // any prefix `-` / `!` / `not`

    KNode* node(KOp op) { nodes.emplace_back(new KNode{}); nodes.back()->op = op; return nodes.back().get(); }
    KNode** list(size_t n) { lists.emplace_back(new KNode*[n]); return lists.back().get(); }
};

// --- compiling ---------------------------------------------------------------

enum class KT { Int, Bool };

struct Compiler {
    Env* global;
    std::vector<IKernel*> session;   // kernels created by this compile, in order
    std::vector<std::string> ops;
    bool prefix = false;
    bool resolveFailed = false;      // a call named a routine that could not be linked (not the syntax)

    bool compileSub(Callable& c, IKernel*& out);
    KNode* expr(IKernel& k, const Callable& c, Expr* e, bool cond, KT& t);
    KNode* stmts(IKernel& k, const Callable& c, const std::vector<StmtPtr>& ss, bool tail);
    KNode* stmt(IKernel& k, const Callable& c, Stmt* s, bool tail);
    void noteOp(const std::string& op) {
        for (auto& o : ops) if (o == op) return;
        ops.push_back(op);
    }
};

int paramIndex(const Callable& c, const std::string& name) {
    const auto& ps = *c.params;
    for (size_t i = 0; i < ps.size(); i++)
        if (ps[i].name == name) return (int)i;
    return -1;
}

bool plainVarRead(const VarExpr* v) {
    return !v->declare && !v->viaPseudoPkg && !v->pkgSymbol && !v->processScoped &&
           !v->nativeIntRead && !v->nativeNumRead && !v->nativeStrRead && !v->synthTopic;
}

KNode* Compiler::expr(IKernel& k, const Callable& c, Expr* e, bool cond, KT& t) {
    if (!e) return nullptr;
    switch (e->kind) {
        case NK::IntLit: {
            auto* l = static_cast<IntLit*>(e);
            if (!l->big.empty()) return nullptr;
            KNode* n = k.node(KOp::Lit); n->lit = l->v; t = KT::Int;
            return n;
        }
        case NK::VarExpr: {
            auto* v = static_cast<VarExpr*>(e);
            if (!plainVarRead(v)) return nullptr;
            const int i = paramIndex(c, v->name);
            if (i < 0) return nullptr;
            KNode* n = k.node(KOp::Param); n->slot = i; t = KT::Int;
            return n;
        }
        case NK::Unary: {
            auto* u = static_cast<Unary*>(e);
            if (u->postfix || !u->operand) return nullptr;
            KT ot;
            if (u->op == "-") {
                KNode* a = expr(k, c, u->operand.get(), false, ot);
                if (!a || ot != KT::Int) return nullptr;
                prefix = true;
                KNode* n = k.node(KOp::Neg); n->a = a; t = KT::Int;
                return n;
            }
            if ((u->op == "!" || u->op == "not") && cond) {
                KNode* a = expr(k, c, u->operand.get(), true, ot);
                if (!a) return nullptr;
                prefix = true;
                KNode* n = k.node(KOp::Not); n->a = a; t = KT::Bool;
                return n;
            }
            return nullptr;
        }
        case NK::Binary: {
            auto* b = static_cast<Binary*>(e);
            const std::string& op = b->op;
            KOp kop;
            bool logical = false, compare = false;
            if (op == "+") kop = KOp::Add;
            else if (op == "-") kop = KOp::Sub;
            else if (op == "*") kop = KOp::Mul;
            else if (op == "div") kop = KOp::Div;
            else if (op == "%") kop = KOp::Mod;
            else if (op == "%%") { kop = KOp::DivBy; compare = true; }
            else if (op == "<") { kop = KOp::Lt; compare = true; }
            else if (op == "<=") { kop = KOp::Le; compare = true; }
            else if (op == ">") { kop = KOp::Gt; compare = true; }
            else if (op == ">=") { kop = KOp::Ge; compare = true; }
            else if (op == "==") { kop = KOp::Eq; compare = true; }
            else if (op == "!=") { kop = KOp::Ne; compare = true; }
            else if ((op == "&&" || op == "and") && cond) { kop = KOp::And; logical = true; }
            else if ((op == "||" || op == "or") && cond) { kop = KOp::Or; logical = true; }
            else return nullptr;
            KT lt, rt;
            KNode* l = expr(k, c, b->lhs.get(), logical, lt);
            KNode* r = l ? expr(k, c, b->rhs.get(), logical, rt) : nullptr;
            if (!r) return nullptr;
            // arithmetic and comparison take Ints; a condition's && / || take
            // either (an Int is true when it is not zero)
            if (!logical && (lt != KT::Int || rt != KT::Int)) return nullptr;
            noteOp(op);
            KNode* n = k.node(kop); n->a = l; n->b = r;
            t = (compare || logical) ? KT::Bool : KT::Int;
            return n;
        }
        case NK::Ternary: {
            auto* tn = static_cast<Ternary*>(e);
            KT ct, at, bt;
            KNode* cn = expr(k, c, tn->cond.get(), true, ct);
            KNode* an = cn ? expr(k, c, tn->then.get(), cond, at) : nullptr;
            KNode* bn = an ? expr(k, c, tn->els.get(), cond, bt) : nullptr;
            if (!bn || at != bt) return nullptr;
            KNode* n = k.node(KOp::Cond); n->a = cn; n->b = an; n->c = bn; t = at;
            return n;
        }
        case NK::Call: {
            auto* call = static_cast<Call*>(e);
            if (call->callee || call->name.empty() || callSpecialName(call) || call->dotAmp) return nullptr;
            if (call->args.size() > (size_t)kMaxParams) return nullptr;
            // resolved where the generic path resolves it: the routine's own
            // scope outward. A lexical `sub` binding cannot be re-bound.
            Env* scope = c.closure ? c.closure.get() : global;
            if (!scope) return nullptr;
            Value* f = scope->find("&" + call->name);
            if (!f || f->t != VT::Code || !f->code()) { resolveFailed = true; return nullptr; }
            Callable& callee = *f->code();
            if ((int)call->args.size() != (callee.params ? (int)callee.params->size() : -1)) return nullptr;
            IKernel* ck = nullptr;
            if (!compileSub(callee, ck)) { resolveFailed = true; return nullptr; }
            KNode** kids = k.list(call->args.size() ? call->args.size() : 1);
            for (size_t i = 0; i < call->args.size(); i++) {
                Expr* ae = call->args[i].get();
                if (!ae || ae->kind == NK::Pair) return nullptr;
                KT at;
                kids[i] = expr(k, c, ae, false, at);
                if (!kids[i] || at != KT::Int) return nullptr;
            }
            KNode* n = k.node(KOp::Call);
            n->callee = ck; n->kids = kids; n->nargs = (uint8_t)call->args.size();
            bool seen = false;
            for (auto* x : k.callees) if (x == ck) seen = true;
            if (!seen) {
                k.callees.push_back(ck);
                k.calleeOwners.push_back(&callee);
                if (ck != &k) k.keepAlive.push_back(*f);
            }
            t = KT::Int;
            return n;
        }
        default:
            return nullptr;
    }
}

// A statement list. `tail`: its value is the routine's (the last statement
// produces it); otherwise every value is discarded.
KNode* Compiler::stmts(IKernel& k, const Callable& c, const std::vector<StmtPtr>& ss, bool tail) {
    if (ss.empty()) return nullptr;
    KNode** kids = k.list(ss.size());
    for (size_t i = 0; i < ss.size(); i++) {
        kids[i] = stmt(k, c, ss[i].get(), tail && i + 1 == ss.size());
        if (!kids[i]) return nullptr;
    }
    if (ss.size() == 1) return kids[0];
    KNode* n = k.node(KOp::Seq); n->kids = kids; n->nargs = (uint8_t)std::min<size_t>(ss.size(), 255);
    return ss.size() > 255 ? nullptr : n;
}

KNode* Compiler::stmt(IKernel& k, const Callable& c, Stmt* s, bool tail) {
    if (!s || !s->label.empty()) return nullptr;
    KT t;
    switch (s->kind) {
        case NK::ExprStmt: {
            // a non-tail expression statement is pure, so it would compute a
            // value nobody reads; leave such a body to the generic path
            if (!tail) return nullptr;
            KNode* n = expr(k, c, static_cast<ExprStmt*>(s)->e.get(), false, t);
            return n && t == KT::Int ? n : nullptr;
        }
        case NK::ReturnStmt: {
            auto* r = static_cast<ReturnStmt*>(s);
            if (r->isRw || !r->value) return nullptr;
            KNode* v = expr(k, c, r->value.get(), false, t);
            if (!v || t != KT::Int) return nullptr;
            KNode* n = k.node(KOp::Ret); n->a = v;
            return n;
        }
        case NK::IfStmt: {
            auto* is = static_cast<IfStmt*>(s);
            if (!is->thenVar.empty() || !is->elseVar.empty() || !is->elseParams.empty() ||
                is->branches.empty())
                return nullptr;
            for (auto& bv : is->branchVars) if (!bv.empty()) return nullptr;
            for (auto& bp : is->branchParams) if (!bp.empty()) return nullptr;
            // in value position every path must produce a value
            if (tail && !is->elseBlock) return nullptr;
            KNode* elseN = nullptr;
            if (is->elseBlock) {
                if (!is->elseBlock->label.empty()) return nullptr;
                elseN = stmts(k, c, is->elseBlock->stmts, tail);
                if (!elseN) return nullptr;
            }
            // build the elsif chain from the back
            for (size_t bi = is->branches.size(); bi-- > 0;) {
                auto& br = is->branches[bi];
                if (!br.second || !br.second->label.empty()) return nullptr;
                KT ct;
                KNode* cn = expr(k, c, br.first.get(), true, ct);
                if (!cn) return nullptr;
                if (is->isUnless && bi == 0) { KNode* nn = k.node(KOp::Not); nn->a = cn; cn = nn; }
                KNode* body = stmts(k, c, br.second->stmts, tail);
                if (!body) return nullptr;
                KNode* n = k.node(KOp::If); n->a = cn; n->b = body; n->c = elseN;
                elseN = n;
            }
            if (is->isUnless && is->branches.size() > 1) return nullptr;
            return elseN;
        }
        default:
            return nullptr;
    }
}

// What a routine must be to have a kernel, from facts fixed when it is
// created: a sub (not a block, method, multi, builtin, native or regex) whose
// signature is plain required `$x` positionals — no type, default, trait or
// where — with no `-->`/`is rw`, no placeholders and no `@_`/`%_`. The body's
// half of the test is the compile itself, which accepts no CATCH, phaser or
// declaration, so nothing a first call works out is needed here.
bool kernelShape(const Callable& c) {
    if (c.isBlock || c.isMethod || c.builtin || c.isNative || c.isMultiDispatcher ||
        c.isMultiCandidate || c.isProto || c.isWhateverCode || c.isRegexRoutine || c.deprecated ||
        c.testAssertion || !c.hadSig || !c.params || !c.body || c.body->empty() ||
        !c.placeholders.empty() || c.usesArgs || c.implicitArgs || c.hasPrimed ||
        !c.retType.empty() || c.retLiteral || c.retRw || c.params->size() > (size_t)kMaxParams)
        return false;
    for (auto& p : *c.params)
        if (p.sigil != '$' || p.named || p.slurpy || p.optional || p.invocant || p.isCopy ||
            p.isRw || p.isRaw || p.defaultVal || p.subSig || p.litVal || p.whereExpr ||
            p.hadWhere || p.defConstraint || p.coerce || p.typeCapture || p.codeSig ||
            !p.captureName.empty() || !p.type.empty() || p.name.size() < 2 ||
            (p.name.size() > 2 && (p.name[1] == '!' || p.name[1] == '.')))
            return false;
    return true;
}

// The kernel slot's states: -1 undecided, 0 none, 1 compiled, 2 being compiled.
bool Compiler::compileSub(Callable& c, IKernel*& out) {
    auto& slot = c.intKernel;
    const signed char st = slot.state.load(std::memory_order_acquire);
    if (st == 1 || st == 2) {
        out = static_cast<IKernel*>(slot.k.load(std::memory_order_acquire));
        // a kernel being built by THIS session (recursion) is fine; one being
        // built by another thread is not ours to link to
        for (auto* s : session) if (s == out) return true;
        return st == 1 && out;
    }
    if (st == 0) return false;
    if (!kernelShape(c)) return false;
    signed char expect = -1;
    if (!slot.state.compare_exchange_strong(expect, 2, std::memory_order_acq_rel)) return false;
    auto* k = new IKernel;
    k->owner = &c;
    k->nparams = (int)c.params->size();
    slot.k.store(k, std::memory_order_release);
    session.push_back(k);
    k->body = stmts(*k, c, *c.body, true);
    out = k;
    return k->body != nullptr;
}

void collectGraph(IKernel* k, std::vector<IKernel*>& g) {
    for (auto* x : g) if (x == k) return;
    g.push_back(k);
    for (auto* x : k->callees) collectGraph(x, g);
}

// --- running -----------------------------------------------------------------
//
// Each node runs through its own function, chosen once the kernel is built
// (link): a node is a direct call to a small function rather than a case of one
// big switch, and an operator whose operands are a parameter or a literal reads
// them in place instead of calling through to them.

struct KRun {
    char* stackFloor;     // below this address the generic path would refuse
    int depthLeft;        // …or past this many more frames
    bool bail = false;
    bool ret = false;
};

inline int64_t krun(const KNode* n, const int64_t* fr, KRun& R) { return n->fn(n, fr, R); }

// operand kinds a fused operator reads in place
enum { kP = 0, kL = 1, kX = 2 };
template <int K>
[[gnu::always_inline]] inline int64_t leaf(const KNode* x, const int64_t* fr, KRun& R) {
    if constexpr (K == kP) return fr[x->slot];
    else if constexpr (K == kL) return x->lit;
    else return x->fn(x, fr, R);
}
inline int kindOf(const KNode* x) { return x->op == KOp::Param ? kP : x->op == KOp::Lit ? kL : kX; }

template <KOp OP>
[[gnu::always_inline]] inline int64_t apply(int64_t x, int64_t y, KRun& R) {
    long long z = 0;
    if constexpr (OP == KOp::Add) { if (add_ovf(x, y, &z)) R.bail = true; return z; }
    else if constexpr (OP == KOp::Sub) { if (sub_ovf(x, y, &z)) R.bail = true; return z; }
    else if constexpr (OP == KOp::Mul) { if (mul_ovf(x, y, &z)) R.bail = true; return z; }
    else if constexpr (OP == KOp::Div) {
        if (y == 0 || (y == -1 && x == LLONG_MIN)) { R.bail = true; return 0; }
        int64_t q = x / y;
        if ((x % y != 0) && ((x < 0) != (y < 0))) q--;   // floor
        return q;
    }
    else if constexpr (OP == KOp::Mod) {
        if (y == 0) { R.bail = true; return 0; }
        if (y == -1) return 0;
        int64_t m = x % y;
        if (m && ((m < 0) != (y < 0))) m += y;           // the divisor's sign
        return m;
    }
    else if constexpr (OP == KOp::DivBy) {
        if (y == 0) { R.bail = true; return 0; }
        return y == -1 || x % y == 0;
    }
    else if constexpr (OP == KOp::Lt) return x < y;
    else if constexpr (OP == KOp::Le) return x <= y;
    else if constexpr (OP == KOp::Gt) return x > y;
    else if constexpr (OP == KOp::Ge) return x >= y;
    else if constexpr (OP == KOp::Eq) return x == y;
    else return x != y;
}

template <KOp OP, int L, int Rk>
int64_t binFn(const KNode* n, const int64_t* fr, KRun& R) {
    const int64_t x = leaf<L>(n->a, fr, R);
    return apply<OP>(x, leaf<Rk>(n->b, fr, R), R);
}

template <KOp OP>
KFn binPick(int l, int r) {
    static const KFn T[3][3] = {
        {binFn<OP, kP, kP>, binFn<OP, kP, kL>, binFn<OP, kP, kX>},
        {binFn<OP, kL, kP>, binFn<OP, kL, kL>, binFn<OP, kL, kX>},
        {binFn<OP, kX, kP>, binFn<OP, kX, kL>, binFn<OP, kX, kX>}};
    return T[l][r];
}

int64_t paramFn(const KNode* n, const int64_t* fr, KRun&) { return fr[n->slot]; }
int64_t litFn(const KNode* n, const int64_t*, KRun&) { return n->lit; }
int64_t negFn(const KNode* n, const int64_t* fr, KRun& R) {
    const int64_t x = krun(n->a, fr, R);
    if (x == LLONG_MIN) { R.bail = true; return 0; }
    return -x;
}
int64_t notFn(const KNode* n, const int64_t* fr, KRun& R) { return krun(n->a, fr, R) == 0; }
int64_t andFn(const KNode* n, const int64_t* fr, KRun& R) { return krun(n->a, fr, R) != 0 && krun(n->b, fr, R) != 0; }
int64_t orFn(const KNode* n, const int64_t* fr, KRun& R) { return krun(n->a, fr, R) != 0 || krun(n->b, fr, R) != 0; }
int64_t condFn(const KNode* n, const int64_t* fr, KRun& R) {
    return krun(n->a, fr, R) != 0 ? krun(n->b, fr, R) : krun(n->c, fr, R);
}
int64_t ifFn(const KNode* n, const int64_t* fr, KRun& R) {
    if (krun(n->a, fr, R) != 0) return krun(n->b, fr, R);
    return n->c ? krun(n->c, fr, R) : 0;
}
int64_t retFn(const KNode* n, const int64_t* fr, KRun& R) {
    const int64_t v = krun(n->a, fr, R);
    R.ret = true;
    return v;
}
int64_t seqFn(const KNode* n, const int64_t* fr, KRun& R) {
    int64_t v = 0;
    for (int i = 0; i < n->nargs; i++) {
        v = krun(n->kids[i], fr, R);
        if (R.ret || R.bail) break;
    }
    return v;
}

// Where this frame sits on the stack (the frame address: a local's address
// means nothing under AddressSanitizer's fake frames — see DepthGuard).
[[gnu::always_inline]] inline char* stackHere() {
#if defined(_MSC_VER) && !defined(__clang__)
    return static_cast<char*>(_AddressOfReturnAddress());
#else
    return static_cast<char*>(__builtin_frame_address(0));
#endif
}

// A call: the arguments, then the limits the generic path would enforce, then
// the callee's body on a frame of machine integers. After a bail nothing more
// recurses, so a doomed attempt ends quickly.
template <int N>
int64_t callFn(const KNode* n, const int64_t* fr, KRun& R) {
    int64_t a[N > 0 ? N : 1];
    const int cnt = N > 0 ? N : n->nargs;
    for (int i = 0; i < cnt; i++) a[i] = krun(n->kids[i], fr, R);
    if (R.bail || --R.depthLeft < 0 || stackHere() <= R.stackFloor) {
        R.bail = true;
        return 0;
    }
    const int64_t v = krun(n->callee->body, a, R);
    R.ret = false;
    ++R.depthLeft;
    return v;
}

void link(KNode* n) {
    if (!n) return;
    link(n->a); link(n->b); link(n->c);
    if (n->kids) for (int i = 0; i < n->nargs; i++) link(n->kids[i]);
    const int l = n->a ? kindOf(n->a) : kX, r = n->b ? kindOf(n->b) : kX;
    switch (n->op) {
        case KOp::Param: n->fn = paramFn; break;
        case KOp::Lit: n->fn = litFn; break;
        case KOp::Add: n->fn = binPick<KOp::Add>(l, r); break;
        case KOp::Sub: n->fn = binPick<KOp::Sub>(l, r); break;
        case KOp::Mul: n->fn = binPick<KOp::Mul>(l, r); break;
        case KOp::Div: n->fn = binFn<KOp::Div, kX, kX>; break;
        case KOp::Mod: n->fn = binFn<KOp::Mod, kX, kX>; break;
        case KOp::DivBy: n->fn = binFn<KOp::DivBy, kX, kX>; break;
        case KOp::Lt: n->fn = binPick<KOp::Lt>(l, r); break;
        case KOp::Le: n->fn = binPick<KOp::Le>(l, r); break;
        case KOp::Gt: n->fn = binPick<KOp::Gt>(l, r); break;
        case KOp::Ge: n->fn = binPick<KOp::Ge>(l, r); break;
        case KOp::Eq: n->fn = binPick<KOp::Eq>(l, r); break;
        case KOp::Ne: n->fn = binPick<KOp::Ne>(l, r); break;
        case KOp::Neg: n->fn = negFn; break;
        case KOp::Not: n->fn = notFn; break;
        case KOp::And: n->fn = andFn; break;
        case KOp::Or: n->fn = orFn; break;
        case KOp::Cond: n->fn = condFn; break;
        case KOp::If: n->fn = ifFn; break;
        case KOp::Ret: n->fn = retFn; break;
        case KOp::Seq: n->fn = seqFn; break;
        case KOp::Call:
            n->fn = n->nargs == 1 ? callFn<1> : n->nargs == 2 ? callFn<2> : n->nargs == 3 ? callFn<3>
                                                                                           : callFn<0>;
            break;
    }
}

// The binary-operator shadow test the typed fast paths use (binaryShadowMaybe
// in InterpreterCore.cpp).
bool opShadowed(const std::string& op) {
    return lexShadowPossible(op) ||
           (g_lexShadowMask.load(std::memory_order_relaxed) && op.size() <= 2 &&
            (op == "*" || op == "/" || op == "-" || op == ">=" || op == "<=" || op == "!="));
}

// An argument the kernel may take as its number alone.
inline bool plainIntArg(const Value& v) {
    return v.t == VT::Int && !v.x_ && v.pk_ == PK::None && !v.natBits && !v.natSigned && !v.natFloat &&
           !v.b && !v.isList && !v.objKeyed && !v.namedArg && v.enumName.empty() && v.enumType.empty() &&
           v.hashKind.empty() && v.s.empty();
}

const bool g_noKernels = [] {
    const char* e = std::getenv("RAKUPP_NO_KERNELS");
    return e && *e && *e != '0';
}();
// RAKUPP_KERNEL_TRACE=1: name each routine as its kernel is decided (stderr)
const bool g_kernelTrace = [] {
    const char* e = std::getenv("RAKUPP_KERNEL_TRACE");
    return e && *e && *e != '0';
}();

std::mutex g_compileMu;

// Stmt::kernelHome's "this body can never be a kernel"
void* const kNeverKernel = reinterpret_cast<void*>(uintptr_t(1));

}  // namespace

void intKernelFree(void* k) { delete static_cast<IKernel*>(k); }

bool Interpreter::tryIntKernel(Callable& c, ValueList& args, int callDepth, Value& out) {
    if (g_noKernels || g_traceStmts) return false;
    signed char st = c.intKernel.state.load(std::memory_order_acquire);
    if (st == 0 || st == 2) return false;
    if (st < 0) {
        // most routines fail the static test: settle those without the lock
        if (!kernelShape(c)) { c.intKernel.state.store(0, std::memory_order_relaxed); return false; }
        // a closure made from a body that already has a kernel (or never
        // will) takes that verdict: closures are rebuilt from the AST on
        // every evaluation, and compiling each one cost more than it ran
        auto& home = (*c.body)[0]->kernelHome;
        if (void* h = home.get()) {
            if (h == kNeverKernel) { c.intKernel.state.store(0, std::memory_order_relaxed); return false; }
            signed char expect = -1;
            if (!c.intKernel.state.compare_exchange_strong(expect, 2, std::memory_order_acq_rel)) return false;
            c.intKernel.borrowed.store(true, std::memory_order_relaxed);
            c.intKernel.k.store(h, std::memory_order_relaxed);
            c.intKernel.state.store(1, std::memory_order_release);
            if (g_kernelTrace)
                std::fprintf(stderr, "kernel: %s shared\n", c.name.empty() ? "<anon>" : c.name.c_str());
        }
        std::unique_lock<std::mutex> lk(g_compileMu, std::defer_lock);
        st = c.intKernel.state.load(std::memory_order_acquire);
        if (st < 0) {
            lk.lock();
            st = c.intKernel.state.load(std::memory_order_acquire);
        }
        if (st < 0) {
            Compiler cc;
            cc.global = global_.get();
            IKernel* root = nullptr;
            const bool ok = cc.compileSub(c, root) && root == static_cast<IKernel*>(c.intKernel.k.load());
            // all or nothing: a kernel links to the others built with it
            for (auto* k : cc.session) {
                if (ok) {
                    collectGraph(k, k->graph);
                    link(k->body);
                    k->binOps = cc.ops;
                    k->usesPrefix = cc.prefix;
                    k->owner->intKernel.state.store(1, std::memory_order_release);
                }
                else {
                    k->owner->intKernel.k.store(nullptr, std::memory_order_release);
                    // the routine that asked is settled; the others may still
                    // qualify on their own
                    k->owner->intKernel.state.store(k->owner == &c ? 0 : -1, std::memory_order_release);
                    delete k;
                }
            }
            if (!ok && c.intKernel.state.load() != 0) c.intKernel.state.store(0, std::memory_order_release);
            // what depends on the body alone is shared through its AST: a
            // kernel that calls nothing, or a verdict the syntax gave
            auto& home = (*c.body)[0]->kernelHome;
            if (ok && root->callees.empty()) {
                if (home.publish(root) == root) c.intKernel.borrowed.store(true, std::memory_order_relaxed);
            }
            else if (!ok && !cc.resolveFailed)
                home.publish(kNeverKernel);
            if (g_kernelTrace)
                std::fprintf(stderr, "kernel: %s %s\n", c.name.empty() ? "<anon>" : c.name.c_str(),
                             ok ? "compiled" : "declined");
            st = c.intKernel.state.load(std::memory_order_acquire);
        }
        if (st != 1) return false;
    }
    auto* k = static_cast<IKernel*>(c.intKernel.k.load(std::memory_order_acquire));
    if (!k || args.size() != (size_t)k->nparams) return false;
    int64_t a[kMaxParams];
    for (size_t i = 0; i < args.size(); i++) {
        if (!plainIntArg(args[i])) return false;
        a[i] = args[i].i;
    }
    if (!c.wrappers.empty()) return false;
    for (auto* g : k->graph)
        for (auto* o : g->calleeOwners)
            if (!o->wrappers.empty()) return false;
    for (auto& op : k->binOps)
        if (opShadowed(op)) return false;
    if (k->usesPrefix && g_userPrefixShadow.load(std::memory_order_relaxed)) return false;
    // the generic path's own limits (DepthGuard): bail where it would refuse,
    // and it will refuse with its own error
    if (!t_stack.top) return false;
    size_t reserve = size_t(2) << 20;
    if (t_stack.limit < reserve * 4) reserve = t_stack.limit / 4;
    KRun R;
    R.stackFloor = t_stack.top - (t_stack.limit - reserve) + 4096;
    R.depthLeft = 100000 - callDepth - 1;
    if (R.depthLeft <= 0) return false;
    const int64_t v = krun(k->body, a, R);
    if (R.bail) {
        // a kernel that keeps bailing (its numbers outgrow int64) stops trying
        const unsigned char b = c.intKernel.bails.load(std::memory_order_relaxed);
        if (b >= 8) c.intKernel.state.store(0, std::memory_order_release);
        else c.intKernel.bails.store((unsigned char)(b + 1), std::memory_order_relaxed);
        return false;
    }
    if (c.intKernel.bails.load(std::memory_order_relaxed)) c.intKernel.bails.store(0, std::memory_order_relaxed);
    out = Value::integer(v);
    return true;
}

}  // namespace rakupp
