// Kernels: code whose values are all machine integers or plain strings runs on
// int64s and std::strings instead of Values.
//
// Two shapes get one:
//
//  * A SUB KERNEL. A plain sub whose body uses nothing but its own `$`
//    parameters, integer literals, `+ - * div % %%`, the six comparisons,
//    `&& || ! not` in a condition, `?? !!`, `if`/`elsif`/`else`/`unless`,
//    `return`, and calls to subs that qualify themselves (fib, ackermann, tak,
//    gcd). Such a body has no side effects: it reads its arguments and computes.
//
//  * A LOOP KERNEL. A `for` over an integer Range (or a `while`, or a C-style
//    `loop` once its init has run) whose body reads and writes
//    only `$` variables holding a plain Int or a plain Str, its own `my`
//    locals and loop variables, through the integer operators above, the six
//    string comparisons, `~` and `=`, `+=`, `-=`, `*=`, `~=`, `++`, `--`, with
//    `if`/`unless`, nested `for` over integer ranges, `while`/`until`, `last`
//    and `next`, and `given`/`when` over a plain topic. The kernel works on
//    COPIES of those variables (the frame) and writes them back only when the
//    whole loop is done, so until then nothing it did is visible to anything.
//
// That is what makes the whole scheme safe. A kernel that meets anything it
// cannot answer exactly — an argument that is not a plain Int, a result that
// would need a BigInt, a zero divisor, the generic path's recursion limit —
// BAILS: it abandons the attempt and the call or the loop runs the ordinary way
// from the start, so the program sees exactly what it would have seen without
// kernels, error messages included.
//
// The checks that can change between runs are made once per ENTRY into a
// kernel, not per recursive call or per iteration: no routine in the kernel's
// call graph is wrapped, no operator it uses is shadowed by a user `infix:<…>` /
// `prefix:<…>`, every argument is a plain Int, every variable a loop kernel
// copies still holds the type it was compiled for and every one it writes is a
// plain, writable container, and no other thread is running. Inside a run
// nothing can change them, because the body cannot run user code.
//
// RAKUPP_NO_KERNELS=1 turns the whole thing off (A/B and gates);
// RAKUPP_KERNEL_TRACE=1 names each sub and loop as its kernel is decided.
#include "InterpreterParts.h"
#include "IntOps.h"
#include "BuiltinsShared.h"   // graphemeCount
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
    // loop kernels: statements that write the frame
    Set,      // slot = a
    AddTo, SubTo, MulTo,   // slot OP= a
    PreInc, PreDec, PostInc, PostDec,
    For,      // for a .. b { c } with the loop variable in slot
    While,    // while a { c }
    Last, Next,
    Given,    // given a { c }: the topic a in slot (SGiven: a string topic)
    SGiven,
    When,     // when a { c }: a is the smartmatch already, c the body; then leave the given
    CLoop,    // loop (; a; b) { c }: a C-style loop whose init has already run
    SInt,     // an Int's text (into a string temporary)
    SCond,    // a ?? b !! c over strings
    Chars,    // a.chars
    // strings: SVar, SLit and SCat yield a string (sfn), the rest an integer
    SVar, SLit, SCat, SSet, SApp,
    SEq, SNe, SLt, SLe, SGt, SGe,
};

struct IKernel;
struct KNode;
struct KRun;
using KFn = int64_t (*)(const KNode*, int64_t*, KRun&);
using SFn = const std::string& (*)(const KNode*, int64_t*, KRun&);

struct KNode {
    union { KFn fn = nullptr; SFn sfn; };   // set by link once the kernel is built
    KOp op;
    uint8_t nargs = 0;
    int32_t slot = 0;           // Param/Set/…/For: an integer slot; SVar/SSet/SApp/SCat: a string slot
    union { int64_t lit = 0; const std::string* str; };   // Lit / SLit
    KNode* a = nullptr;
    KNode* b = nullptr;
    KNode* c = nullptr;
    IKernel* callee = nullptr;  // Call
    KNode** kids = nullptr;     // Call: nargs arguments; Seq: nargs statements
};

constexpr int kMaxParams = 6;

constexpr int kMaxFrame = 32;   // a sub kernel's parameters and `my`s

struct IKernel {
    Callable* owner = nullptr;
    int nparams = 0;
    int nframe = 0;                        // parameters + locals: the frame a call allocates
    KNode* body = nullptr;
    std::vector<std::unique_ptr<KNode>> nodes;
    std::vector<std::unique_ptr<KNode*[]>> lists;
    std::vector<std::unique_ptr<std::string>> strs;   // SLit texts
    std::vector<IKernel*> callees;         // direct, may include this
    std::vector<Value> keepAlive;          // the callees' Code values (not this one)
    std::vector<Callable*> calleeOwners;   // the routines `callees` run (alive: keepAlive, or this one's caller)
    // How each callee was found: `&name` in `scope`, and what it found. A
    // routine bound to a variable (`my &g = &a`) can be re-bound, and so can a
    // sub under `:=`, so every entry looks again (callsBound).
    Env* scope = nullptr;
    std::vector<std::pair<std::string, Callable*>> calls;
    // Filled once the compile session that built this kernel succeeds:
    std::vector<IKernel*> graph;           // every kernel reachable, this included
    std::vector<std::string> binOps;       // every infix spelling the graph uses
    bool usesPrefix = false;               // any prefix `-` / `!` / `not`

    KNode* node(KOp op) { nodes.emplace_back(new KNode{}); nodes.back()->op = op; return nodes.back().get(); }
    KNode** list(size_t n) { lists.emplace_back(new KNode*[n]); return lists.back().get(); }
    const std::string* text(std::string s) { strs.emplace_back(new std::string(std::move(s))); return strs.back().get(); }
};

// --- compiling ---------------------------------------------------------------

// Void: an expression with no value a kernel can use (a string assignment)
enum class KT { Int, Bool, Str, Void };

// A loop kernel's names. Every `$` variable the loop reads or writes gets a
// slot in the frame: an integer slot or a string slot, by the type its value
// has when the kernel is compiled — which is the type it must still have at
// every later entry.
struct LSym { std::string name; KT t; int slot; bool ro; };
struct LOuter { std::string name; KT t; int slot; bool written; };
struct LoopCx {
    Env* env = nullptr;                      // where the loop's free names resolve (this entry)
    const Callable* sub = nullptr;           // set for a sub body compiled as statements: no free names
    std::vector<std::vector<LSym>> scopes;   // the kernel's own: loop variables and `my`s
    std::vector<LOuter> outer;               // variables of the program, copied in and out
    int nint = 0, nstr = 0;                  // frame sizes, temporaries included
    int loops = 0;                           // loops enclosing the statement being compiled
    int givens = 0;                          // `given`s enclosing it, inside the innermost loop
    bool inModifier = false;                 // a statement-modifier body: no `my` (it would leak out)
    bool usesInc = false;                    // any `++` / `--`
    bool usesMethods = false;                // a built-in method (`.chars`), which `augment` could replace
    std::string why;                         // the first thing refused, for the trace
};

struct Compiler {
    Env* global;
    LoopCx* L = nullptr;             // set when compiling a loop kernel
    std::vector<IKernel*> session;   // kernels created by this compile, in order
    std::vector<std::string> ops;
    bool prefix = false;
    bool resolveFailed = false;      // a call named a routine that could not be linked (not the syntax)

    bool compileSub(Callable& c, IKernel*& out);
    KNode* expr(IKernel& k, const Callable* c, Expr* e, bool cond, KT& t);
    KNode* stmts(IKernel& k, const Callable& c, const std::vector<StmtPtr>& ss, bool tail);
    KNode* stmt(IKernel& k, const Callable& c, Stmt* s, bool tail);
    // loop kernels
    KNode* lstmts(IKernel& k, const std::vector<StmtPtr>& ss);
    KNode* lstmt(IKernel& k, Stmt* s);
    KNode* lbody(IKernel& k, Block* b, bool ownScope);
    KNode* lrange(IKernel& k, Expr* e, KNode*& hi);
    KNode* ltail(IKernel& k, const std::vector<StmtPtr>& ss);
    KNode* lif(IKernel& k, IfStmt* is, bool tail);
    const LSym* lookup(const std::string& name);
    // An Int or a Str as a string (an Int's text is what `~` and interpolation
    // make of it); null for anything else.
    KNode* asStr(IKernel& k, KNode* n, KT t) {
        if (!n) return nullptr;
        if (t == KT::Str) return n;
        if (t != KT::Int || !L || L->sub) return nullptr;
        KNode* s = k.node(KOp::SInt); s->a = n; s->slot = L->nstr++;
        return s;
    }
    KNode* refuse(const std::string& why) {
        if (L && L->why.empty()) L->why = why;
        return nullptr;
    }
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

// `$name`: no twigil, not the topic, not a package name. (The topic is a loop
// kernel's own variable when it is the loop's, and found as a local first.)
bool plainScalarName(const std::string& n) {
    if (n.size() < 2 || n[0] != '$') return false;
    const char c = n[1];
    if (!(ascii::isalpha((unsigned char)c) || c == '_')) return false;
    if (n == "$_") return false;
    return n.find("::") == std::string::npos;
}

// A value a kernel slot may hold as its number or its text alone. Read-only
// bindings are fine for a read; a slot the kernel writes is checked again as a
// container at entry (bindOuter).
inline bool plainIntValue(const Value& v) {
    return v.t == VT::Int && !v.x_ && v.pk_ == PK::None && !v.natBits && !v.natSigned && !v.natFloat &&
           !v.b && !v.isList && !v.objKeyed && !v.namedArg && v.enumName.empty() && v.enumType.empty() &&
           v.hashKind.empty() && v.s.empty();
}
inline bool plainStrValue(const Value& v) {
    return v.t == VT::Str && !v.x_ && v.pk_ == PK::None && !v.natBits && !v.natSigned && !v.natFloat &&
           !v.b && !v.isList && !v.objKeyed && !v.namedArg && v.enumName.empty() && v.enumType.empty() &&
           v.hashKind.empty();
}

// Does `e` read the variable `name`? (Over the expression kinds a kernel
// compiles, which are the only ones this is asked about.)
bool mentions(const Expr* e, const std::string& name) {
    if (!e) return false;
    switch (e->kind) {
        case NK::VarExpr: return static_cast<const VarExpr*>(e)->name == name;
        case NK::Binary: {
            auto* b = static_cast<const Binary*>(e);
            return mentions(b->lhs.get(), name) || mentions(b->rhs.get(), name);
        }
        case NK::Unary: return mentions(static_cast<const Unary*>(e)->operand.get(), name);
        case NK::Ternary: {
            auto* t = static_cast<const Ternary*>(e);
            return mentions(t->cond.get(), name) || mentions(t->then.get(), name) || mentions(t->els.get(), name);
        }
        case NK::Assign: {
            auto* a = static_cast<const Assign*>(e);
            return mentions(a->target.get(), name) || mentions(a->value.get(), name);
        }
        case NK::Call:
            for (auto& a : static_cast<const Call*>(e)->args) if (mentions(a.get(), name)) return true;
            return false;
        default: return false;
    }
}

const LSym* Compiler::lookup(const std::string& name) {
    for (auto it = L->scopes.rbegin(); it != L->scopes.rend(); ++it)
        for (auto& s : *it)
            if (s.name == name) return &s;
    return nullptr;
}

KNode* Compiler::expr(IKernel& k, const Callable* c, Expr* e, bool cond, KT& t) {
    if (!e) return nullptr;
    switch (e->kind) {
        case NK::IntLit: {
            auto* l = static_cast<IntLit*>(e);
            if (!l->big.empty()) return refuse("an integer literal beyond 64 bits");
            KNode* n = k.node(KOp::Lit); n->lit = l->v; t = KT::Int;
            return n;
        }
        case NK::BoolLit: {
            // a truth value only: a Bool is not an Int a kernel may store
            if (!cond) return refuse("a Bool outside a condition");
            KNode* n = k.node(KOp::Lit); n->lit = static_cast<BoolLit*>(e)->v ? 1 : 0; t = KT::Bool;
            return n;
        }
        case NK::StrLit: {
            if (!L || L->sub) return refuse("a string in a sub");
            KNode* n = k.node(KOp::SLit); n->str = k.text(static_cast<StrLit*>(e)->v); t = KT::Str;
            return n;
        }
        case NK::InterpStr: {
            // "…" with nothing interpolated: its parts are literal text, each
            // already NFC, and pure ASCII joins without renormalizing
            if (!L || L->sub) return refuse("a string in a sub");
            std::string s;
            auto* is = static_cast<InterpStr*>(e);
            bool literal = true;
            for (auto& p : is->parts) literal = literal && p && p->kind == NK::StrLit;
            if (!literal) {
                // "…$x…": the parts' texts joined, renormalized as `~` does
                // (normalizing a join in steps is normalizing it once)
                KNode* acc = nullptr;
                for (auto& p : is->parts) {
                    if (!p) return refuse("an interpolated string");
                    KT pt;
                    KNode* pn = asStr(k, expr(k, c, p.get(), false, pt), pt);
                    if (!pn) return refuse("an interpolated part that is neither an Int nor a Str");
                    if (!acc) { acc = pn; continue; }
                    KNode* cat = k.node(KOp::SCat); cat->a = acc; cat->b = pn; cat->slot = L->nstr++;
                    acc = cat;
                }
                if (!acc) return refuse("an empty interpolation");
                t = KT::Str;
                return acc;
            }
            for (auto& p : is->parts) s += static_cast<StrLit*>(p.get())->v;
            if (is->parts.size() > 1)
                for (unsigned char ch : s) if (ch >= 0x80) return refuse("a non-ASCII string in parts");
            KNode* n = k.node(KOp::SLit); n->str = k.text(std::move(s)); t = KT::Str;
            return n;
        }
        case NK::VarExpr: {
            auto* v = static_cast<VarExpr*>(e);
            if (!plainVarRead(v)) return refuse("variable " + v->name + " (a declaration or a special form)");
            if (!L) {
                const int i = paramIndex(*c, v->name);
                if (i < 0) return nullptr;
                KNode* n = k.node(KOp::Param); n->slot = i; t = KT::Int;
                return n;
            }
            const LSym* s = lookup(v->name);
            KT st;
            int slot;
            if (s) { st = s->t; slot = s->slot; }
            else {
                if (L->sub) return refuse("variable " + v->name + " from outside the sub");
                // a variable of the program: copied into the frame at entry
                const LOuter* o = nullptr;
                for (auto& x : L->outer) if (x.name == v->name) o = &x;
                if (!o) {
                    if (!plainScalarName(v->name)) return refuse("variable " + v->name);
                    Value* raw = L->env->findRaw(v->name);
                    if (!raw) return refuse("variable " + v->name + " is not in scope");
                    const Value& cv = *raw->deref();
                    KT vt;
                    if (plainIntValue(cv)) { vt = KT::Int; slot = L->nint++; }
                    else if (plainStrValue(cv)) { vt = KT::Str; slot = L->nstr++; }
                    else return refuse("variable " + v->name + " holds neither a plain Int nor a plain Str");
                    L->outer.push_back({v->name, vt, slot, false});
                    o = &L->outer.back();
                }
                st = o->t; slot = o->slot;
            }
            KNode* n = k.node(st == KT::Str ? KOp::SVar : KOp::Param);
            n->slot = slot; t = st;
            return n;
        }
        case NK::Unary: {
            auto* u = static_cast<Unary*>(e);
            if (!u->operand) return nullptr;
            KT ot;
            if (u->op == "++" || u->op == "--") {
                if (!L) return nullptr;
                if (u->operand->kind != NK::VarExpr) return refuse("++/-- on something not a variable");
                KNode* tn = expr(k, c, u->operand.get(), false, ot);
                if (!tn) return nullptr;
                if (ot != KT::Int) return refuse("++/-- on a Str");
                const std::string& nm = static_cast<VarExpr*>(u->operand.get())->name;
                if (const LSym* s = lookup(nm)) { if (s->ro) return refuse("++/-- on a loop variable"); }
                else for (auto& o : L->outer) if (o.name == nm) o.written = true;
                if (!u->postfix) prefix = true;
                L->usesInc = true;
                KNode* n = k.node(u->postfix ? (u->op == "++" ? KOp::PostInc : KOp::PostDec)
                                             : (u->op == "++" ? KOp::PreInc : KOp::PreDec));
                n->slot = tn->slot; t = KT::Int;
                return n;
            }
            if (u->postfix) return nullptr;
            if (u->op == "-") {
                KNode* a = expr(k, c, u->operand.get(), false, ot);
                if (!a || ot != KT::Int) return nullptr;
                prefix = true;
                KNode* n = k.node(KOp::Neg); n->a = a; t = KT::Int;
                return n;
            }
            if ((u->op == "!" || u->op == "not") && cond) {
                KNode* a = expr(k, c, u->operand.get(), true, ot);
                if (!a || ot == KT::Str || ot == KT::Void) return nullptr;
                prefix = true;
                KNode* n = k.node(KOp::Not); n->a = a; t = KT::Bool;
                return n;
            }
            return refuse("prefix " + u->op);
        }
        case NK::Binary: {
            auto* b = static_cast<Binary*>(e);
            const std::string& op = b->op;
            KOp kop;
            bool logical = false, compare = false, strop = false;
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
            else if (L && op == "~") { kop = KOp::SCat; strop = true; }
            else if (L && op == "eq") { kop = KOp::SEq; compare = strop = true; }
            else if (L && op == "ne") { kop = KOp::SNe; compare = strop = true; }
            else if (L && op == "lt") { kop = KOp::SLt; compare = strop = true; }
            else if (L && op == "le") { kop = KOp::SLe; compare = strop = true; }
            else if (L && op == "gt") { kop = KOp::SGt; compare = strop = true; }
            else if (L && op == "ge") { kop = KOp::SGe; compare = strop = true; }
            else return refuse("operator " + op);
            KT lt, rt;
            KNode* l = expr(k, c, b->lhs.get(), logical, lt);
            KNode* r = l ? expr(k, c, b->rhs.get(), logical, rt) : nullptr;
            if (!r) return nullptr;
            if (strop) {
                // two Strs; an Int operand takes its text, as the generic path's does
                l = asStr(k, l, lt); r = asStr(k, r, rt);
                if (!l || !r) return refuse("string operator " + op + " on a non-Str");
            }
            // arithmetic and comparison take Ints; a condition's && / || take
            // either (an Int is true when it is not zero)
            else if (!logical && (lt != KT::Int || rt != KT::Int)) return refuse("operator " + op + " on a non-Int");
            else if (logical && (lt == KT::Str || rt == KT::Str || lt == KT::Void || rt == KT::Void))
                return refuse("a Str as a condition");
            noteOp(op);
            KNode* n = k.node(kop); n->a = l; n->b = r;
            if (kop == KOp::SCat) { n->slot = L->nstr++; t = KT::Str; }   // the result's temporary
            else t = (compare || logical) ? KT::Bool : KT::Int;
            return n;
        }
        case NK::Ternary: {
            auto* tn = static_cast<Ternary*>(e);
            KT ct, at, bt;
            KNode* cn = expr(k, c, tn->cond.get(), true, ct);
            if (cn && (ct == KT::Str || ct == KT::Void)) return refuse("a Str as a condition");
            KNode* an = cn ? expr(k, c, tn->then.get(), cond, at) : nullptr;
            KNode* bn = an ? expr(k, c, tn->els.get(), cond, bt) : nullptr;
            if (!bn || at != bt || at == KT::Void) return refuse("?? !! of these types");
            KNode* n = k.node(at == KT::Str ? KOp::SCond : KOp::Cond); n->a = cn; n->b = an; n->c = bn; t = at;
            return n;
        }
        case NK::Assign: {
            if (!L) return nullptr;
            auto* a = static_cast<Assign*>(e);
            if (a->userOp || a->containerSigil || !a->target || a->target->kind != NK::VarExpr)
                return refuse("an assignment of this form");
            auto* tv = static_cast<VarExpr*>(a->target.get());
            KT vt;
            if (tv->declare) {
                // `my $x = EXPR`: a slot of the kernel's own, of EXPR's type.
                // Anything beyond a bare `my $x` changes what an assignment
                // means (a type check, a coercion, a container trait, `our` /
                // `state` storage).
                if (a->op != "=" || L->inModifier || tv->declScope != "my" || !tv->declType.empty() ||
                    !tv->declCoerce.empty() || tv->declDefault || tv->declDynamic || tv->declExport ||
                    !tv->containerIs.empty() || tv->declShape || tv->declTypeExpr || tv->declSmiley ||
                    tv->declWhereExpr || tv->declHasWhere || !tv->declStubType.empty() || tv->pkgSymbol ||
                    tv->viaPseudoPkg || !plainScalarName(tv->name) || mentions(a->value.get(), tv->name))
                    return refuse("a declaration with a type, a trait or its own name");
                KNode* v = expr(k, c, a->value.get(), false, vt);
                if (!v) return nullptr;
                if (vt != KT::Int && vt != KT::Str) return refuse("a declaration of a Bool");
                for (auto& s : L->scopes.back()) if (s.name == tv->name) return refuse("a redeclaration");
                const int slot = vt == KT::Str ? L->nstr++ : L->nint++;
                L->scopes.back().push_back({tv->name, vt, slot, false});
                KNode* n = k.node(vt == KT::Str ? KOp::SSet : KOp::Set);
                n->slot = slot; n->a = v;
                t = vt == KT::Str ? KT::Void : KT::Int;
                return n;
            }
            KT tt;
            KNode* tn = expr(k, c, tv, false, tt);
            if (!tn) return nullptr;
            KNode* v = expr(k, c, a->value.get(), false, vt);
            if (!v) return nullptr;
            KOp kop;
            if (a->op == "=") {
                if (vt != tt) return refuse("an assignment that would change a variable's type");
                kop = tt == KT::Str ? KOp::SSet : KOp::Set;
            }
            else if (a->op == "~=" && tt == KT::Str && (vt == KT::Str || vt == KT::Int)) {
                v = asStr(k, v, vt);
                kop = KOp::SApp; noteOp("~");
            }
            else if ((a->op == "+=" || a->op == "-=" || a->op == "*=") && tt == KT::Int && vt == KT::Int) {
                kop = a->op[0] == '+' ? KOp::AddTo : a->op[0] == '-' ? KOp::SubTo : KOp::MulTo;
                noteOp(a->op.substr(0, 1));
            }
            else return refuse("assignment operator " + a->op + " on these types");
            if (const LSym* s = lookup(tv->name)) { if (s->ro) return refuse("an assignment to a loop variable"); }
            else for (auto& o : L->outer) if (o.name == tv->name) o.written = true;
            KNode* n = k.node(kop); n->slot = tn->slot; n->a = v;
            t = tt == KT::Str ? KT::Void : KT::Int;
            return n;
        }
        case NK::MethodCall: {
            // `.chars` of an Int or a Str: the grapheme count (an Int's digits
            // are ASCII). Entered only while no `augment` has touched a
            // built-in type, which is how a program could give Str its own.
            auto* m = static_cast<MethodCall*>(e);
            if (!L || L->sub || m->method != "chars" || !m->args.empty() || m->methodExpr || m->maybe || m->allMode ||
                m->bang || m->mutate || m->hyper || m->meta || !m->methodQual.empty() || !m->inv)
                return refuse("a method call");
            KT it;
            KNode* inv = asStr(k, expr(k, c, m->inv.get(), false, it), it);
            if (!inv) return refuse(".chars of something neither an Int nor a Str");
            L->usesMethods = true;
            KNode* n = k.node(KOp::Chars); n->a = inv; t = KT::Int;
            return n;
        }
        case NK::Call: {
            auto* call = static_cast<Call*>(e);
            if (call->callee || call->name.empty() || callSpecialName(call) || call->dotAmp)
                return refuse("a call of this form");
            if (call->args.size() > (size_t)kMaxParams) return refuse("a call with many arguments");
            // resolved where the generic path resolves it: a routine's own
            // scope outward, or a loop's from where it runs
            const Callable* home = c ? c : L ? L->sub : nullptr;
            Env* scope = home ? (home->closure ? home->closure.get() : global) : L ? L->env : nullptr;
            if (!scope) return refuse("a call with nowhere to resolve it");
            const std::string key = "&" + call->name;
            Value* f = scope->find(key);
            if (!f || f->t != VT::Code || !f->code()) { resolveFailed = true; return refuse("a call to " + call->name); }
            Callable& callee = *f->code();
            if ((int)call->args.size() != (callee.params ? (int)callee.params->size() : -1))
                return refuse("a call with the wrong number of arguments");
            IKernel* ck = nullptr;
            {
                LoopCx* saved = L;   // the callee is compiled on its own
                L = nullptr;
                const bool ok = compileSub(callee, ck);
                L = saved;
                if (!ok) { resolveFailed = true; return refuse("a call to " + call->name + ", which is no kernel"); }
            }
            k.scope = scope;
            bool known = false;
            for (auto& kc : k.calls) known = known || kc.first == key;
            if (!known) k.calls.push_back({key, &callee});
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
            return refuse("an expression of this kind");
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
            KNode* n = expr(k, &c, static_cast<ExprStmt*>(s)->e.get(), false, t);
            return n && t == KT::Int ? n : nullptr;
        }
        case NK::ReturnStmt: {
            auto* r = static_cast<ReturnStmt*>(s);
            if (r->isRw || !r->value) return nullptr;
            KNode* v = expr(k, &c, r->value.get(), false, t);
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
                KNode* cn = expr(k, &c, br.first.get(), true, ct);
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

// --- loop kernels: statements ------------------------------------------------

// A block's statements, in a scope of their own when `ownScope` (a statement
// modifier's body declares into the enclosing one, and is refused a `my`).
KNode* Compiler::lbody(IKernel& k, Block* b, bool ownScope) {
    if (!b) return refuse("a loop with no body");
    if (!b->phaser.empty() || b->isCatch || !b->label.empty()) return refuse("a phaser, CATCH or label");
    if (ownScope) L->scopes.emplace_back();
    KNode* n = lstmts(k, b->stmts);
    if (ownScope) L->scopes.pop_back();
    return n;
}

KNode* Compiler::lstmts(IKernel& k, const std::vector<StmtPtr>& ss) {
    if (ss.empty()) { KNode* n = k.node(KOp::Lit); n->lit = 0; return n; }
    if (ss.size() > 255) return refuse("a body of more than 255 statements");
    KNode** kids = k.list(ss.size());
    for (size_t i = 0; i < ss.size(); i++) {
        kids[i] = lstmt(k, ss[i].get());
        if (!kids[i]) return nullptr;
    }
    if (ss.size() == 1) return kids[0];
    KNode* n = k.node(KOp::Seq); n->kids = kids; n->nargs = (uint8_t)ss.size();
    return n;
}

// An integer Range a nested `for` walks: `A .. B` (either end open) or `^N`.
// (An endpoint that is a Whatever, a Str or a Date makes some other kind of
// list; the kernel's endpoints are Ints by their types.)
// Its ends are integers whatever they hold, so the loop is the counted walk
// the generic path makes over such a Range.
KNode* Compiler::lrange(IKernel& k, Expr* e, KNode*& hi) {
    if (!e) return nullptr;
    KT lt, ht;
    if (e->kind == NK::Unary) {
        auto* u = static_cast<Unary*>(e);
        if (u->op != "^" || u->postfix) return refuse("a nested `for` over this list");
        KNode* n = expr(k, nullptr, u->operand.get(), false, ht);
        if (!n || ht != KT::Int) return refuse("a nested `for` over a non-Int range");
        KNode* one = k.node(KOp::Lit); one->lit = 1;
        hi = k.node(KOp::Sub); hi->a = n; hi->b = one;
        KNode* lo = k.node(KOp::Lit); lo->lit = 0;
        return lo;
    }
    if (e->kind != NK::Range) return refuse("a nested `for` over this list");
    auto* r = static_cast<RangeExpr*>(e);
    const bool exLo = r->exFrom, exHi = r->exTo;
    KNode* lo = expr(k, nullptr, r->from.get(), false, lt);
    hi = lo ? expr(k, nullptr, r->to.get(), false, ht) : nullptr;
    if (!hi || lt != KT::Int || ht != KT::Int) return refuse("a nested `for` over a non-Int range");
    if (exLo) { KNode* one = k.node(KOp::Lit); one->lit = 1; KNode* n = k.node(KOp::Add); n->a = lo; n->b = one; lo = n; }
    if (exHi) { KNode* one = k.node(KOp::Lit); one->lit = 1; KNode* n = k.node(KOp::Sub); n->a = hi; n->b = one; hi = n; }
    return lo;
}

KNode* Compiler::lstmt(IKernel& k, Stmt* s) {
    if (!s) return refuse("an empty statement");
    if (!s->label.empty()) return refuse("a label");
    KT t;
    switch (s->kind) {
        case NK::EmptyStmt: { KNode* n = k.node(KOp::Lit); n->lit = 0; return n; }
        case NK::ExprStmt: {
            KNode* n = expr(k, nullptr, static_cast<ExprStmt*>(s)->e.get(), false, t);
            if (n && t == KT::Str) return refuse("a string in sink context");
            return n;
        }
        case NK::Block: return lbody(k, static_cast<Block*>(s), true);
        case NK::IfStmt: return lif(k, static_cast<IfStmt*>(s), false);
        case NK::ForStmt: {
            auto* fs = static_cast<ForStmt*>(s);
            if (fs->asExpr || fs->destructure || fs->rwVars || !fs->params.empty() || fs->emptyPointy ||
                fs->hyper || fs->vars.size() > 1)
                return refuse("a nested `for` of this shape");
            for (auto tr : fs->varTraits) if (tr) return refuse("a nested `for` with an `is rw` / `is raw` variable");
            KNode* hi = nullptr;
            KNode* lo = lrange(k, fs->list.get(), hi);
            if (!lo) return nullptr;
            const std::string var = fs->vars.empty() ? "$_" : fs->vars[0];
            const int slot = L->nint++;
            L->scopes.push_back({{var, KT::Int, slot, true}});
            L->loops++;
            const bool sv = L->inModifier;
            const int sg = L->givens;
            L->inModifier = sv || fs->modifier;   // `STMT for …` declares into the enclosing scope
            L->givens = 0;                        // (a `when` in here would end the loop's pass)
            KNode* body = lbody(k, fs->body.get(), false);
            L->inModifier = sv;
            L->givens = sg;
            L->loops--;
            L->scopes.pop_back();
            if (!body) return nullptr;
            KNode* n = k.node(KOp::For); n->slot = slot; n->a = lo; n->b = hi; n->c = body;
            return n;
        }
        case NK::GivenStmt: {
            // `given X { … }` with a plain topic: `$_` is X, read-only (a
            // value, not a container), for the block
            auto* g = static_cast<GivenStmt*>(s);
            if (!g->var.empty() || !g->params.empty() || !g->elseParams.empty() || g->modifier ||
                g->defGuard != 0 || g->hasElse || g->elseBody)
                return refuse("a `given` of this shape");
            KT tt;
            KNode* tn = expr(k, nullptr, g->topic.get(), false, tt);
            if (!tn) return nullptr;
            if (tt != KT::Int && tt != KT::Str) return refuse("a `given` of a Bool");
            const int slot = tt == KT::Str ? L->nstr++ : L->nint++;
            L->scopes.push_back({{"$_", tt, slot, true}});
            L->givens++;
            KNode* body = lbody(k, g->body.get(), false);
            L->givens--;
            L->scopes.pop_back();
            if (!body) return nullptr;
            KNode* n = k.node(tt == KT::Str ? KOp::SGiven : KOp::Given);
            n->slot = slot; n->a = tn; n->c = body;
            return n;
        }
        case NK::WhenStmt: {
            // `$_ ~~ X` over a plain topic: an Int matches by `==`, a Str by
            // `eq`, and a Bool (`when $_ > 5`) is the answer itself
            auto* w = static_cast<WhenStmt*>(s);
            if (L->givens == 0) return refuse("a `when` outside a `given`");
            KNode* cn;
            if (w->isDefault || !w->cond) { cn = k.node(KOp::Lit); cn->lit = 1; }
            else {
                const LSym* topic = lookup("$_");
                KT ct;
                KNode* x = expr(k, nullptr, w->cond.get(), true, ct);
                if (!x) return nullptr;
                if (ct == KT::Bool) cn = x;
                else if (topic && ct == topic->t && (ct == KT::Int || ct == KT::Str)) {
                    KNode* tv = k.node(ct == KT::Str ? KOp::SVar : KOp::Param);
                    tv->slot = topic->slot;
                    cn = k.node(ct == KT::Str ? KOp::SEq : KOp::Eq);
                    cn->a = tv; cn->b = x;
                }
                else return refuse("a `when` that would smartmatch across types");
            }
            KNode* body = lbody(k, w->body.get(), true);
            if (!body) return nullptr;
            KNode* n = k.node(KOp::When); n->a = cn; n->c = body;
            return n;
        }
        case NK::WhileStmt: {
            auto* w = static_cast<WhileStmt*>(s);
            if (w->modifier || w->asExpr || !w->var.empty() || !w->params.empty())
                return refuse("a `while` of this shape");
            KT ct;
            KNode* cn = expr(k, nullptr, w->cond.get(), true, ct);
            if (!cn) return nullptr;
            if (ct == KT::Str || ct == KT::Void) return refuse("a Str as a condition");
            if (w->isUntil) { KNode* nn = k.node(KOp::Not); nn->a = cn; cn = nn; }
            L->loops++;
            const int sg = L->givens;
            L->givens = 0;
            KNode* body = lbody(k, w->body.get(), true);
            L->givens = sg;
            L->loops--;
            if (!body) return nullptr;
            KNode* n = k.node(KOp::While); n->a = cn; n->c = body;
            return n;
        }
        case NK::ReturnStmt: {
            auto* r = static_cast<ReturnStmt*>(s);
            if (!L->sub) return refuse("a `return`");
            if (r->isRw || !r->value) return refuse("a `return` of this form");
            KNode* v = expr(k, nullptr, r->value.get(), false, t);
            if (!v || t != KT::Int) return refuse("a `return` of a non-Int");
            KNode* n = k.node(KOp::Ret); n->a = v;
            return n;
        }
        case NK::LastStmt:
            if (!static_cast<LastStmt*>(s)->target.empty()) return refuse("a labelled `last`");
            return k.node(KOp::Last);
        case NK::NextStmt:
            if (!static_cast<NextStmt*>(s)->target.empty()) return refuse("a labelled `next`");
            return k.node(KOp::Next);
        default:
            return refuse("a statement of this kind");
    }
}

// An `if` / `elsif` / `else` / `unless`; in `tail` position (a sub's last
// statement) every branch must end in a value, so there must be an `else`.
KNode* Compiler::lif(IKernel& k, IfStmt* is, bool tail) {
    if (!is->thenVar.empty() || !is->elseVar.empty() || !is->elseParams.empty() || is->branches.empty())
        return refuse("an `if` with a binder");
    for (auto& bv : is->branchVars) if (!bv.empty()) return refuse("an `elsif` with a binder");
    for (auto& bp : is->branchParams) if (!bp.empty()) return refuse("an `if` with a binder");
    if (is->isUnless && is->branches.size() > 1) return refuse("`unless` with `elsif`");
    if (tail && (!is->elseBlock || is->modifier)) return refuse("an `if` with no `else` as a sub's value");
    auto branch = [&](Block* b) -> KNode* {
        if (!tail) return lbody(k, b, !is->modifier);
        if (!b || !b->phaser.empty() || b->isCatch || !b->label.empty()) return refuse("a phaser, CATCH or label");
        L->scopes.emplace_back();
        KNode* n = ltail(k, b->stmts);
        L->scopes.pop_back();
        return n;
    };
    const bool sv = L->inModifier;
    L->inModifier = sv || is->modifier;
    KNode* elseN = nullptr;
    if (is->elseBlock && !(elseN = branch(is->elseBlock.get()))) return nullptr;
    for (size_t bi = is->branches.size(); bi-- > 0;) {
        auto& br = is->branches[bi];
        KT ct;
        KNode* cn = expr(k, nullptr, br.first.get(), true, ct);
        if (!cn) return nullptr;
        if (ct == KT::Str || ct == KT::Void) return refuse("a Str as a condition");
        if (is->isUnless && bi == 0) { KNode* nn = k.node(KOp::Not); nn->a = cn; cn = nn; }
        KNode* body = branch(br.second.get());
        if (!body) return nullptr;
        KNode* n = k.node(KOp::If); n->a = cn; n->b = body; n->c = elseN;
        elseN = n;
    }
    L->inModifier = sv;
    return elseN;
}

// A sub body compiled as statements: every statement but the last as a
// loop body's are, and the last one gives the routine's value — an Int
// expression, a `return`, or an `if` whose every branch ends in one.
KNode* Compiler::ltail(IKernel& k, const std::vector<StmtPtr>& ss) {
    if (ss.empty()) return refuse("an empty body");
    if (ss.size() > 255) return refuse("a body of more than 255 statements");
    KNode** kids = k.list(ss.size());
    for (size_t i = 0; i + 1 < ss.size(); i++)
        if (!(kids[i] = lstmt(k, ss[i].get()))) return nullptr;
    Stmt* last = ss.back().get();
    KNode* ln = nullptr;
    if (!last || !last->label.empty()) return refuse("a label");
    if (last->kind == NK::ExprStmt) {
        KT t;
        ln = expr(k, nullptr, static_cast<ExprStmt*>(last)->e.get(), false, t);
        if (ln && t != KT::Int) return refuse("a sub whose value is not an Int");
    }
    else if (last->kind == NK::ReturnStmt) ln = lstmt(k, last);
    else if (last->kind == NK::IfStmt) ln = lif(k, static_cast<IfStmt*>(last), true);
    else return refuse("a sub whose last statement gives no Int");
    if (!ln) return nullptr;
    kids[ss.size() - 1] = ln;
    if (ss.size() == 1) return ln;
    KNode* n = k.node(KOp::Seq); n->kids = kids; n->nargs = (uint8_t)ss.size();
    return n;
}

// What a routine must be to have a kernel, from facts fixed when it is
// created: a sub (not a block, method, multi, builtin, native or regex) whose
// signature is plain required `$x` positionals — untyped or `Int`, `is copy`
// or not, no default, other trait or where — with no `-->` but `Int`, no
// placeholders and no `@_`/`%_`. The body's half of the test is the compile
// itself, which accepts no CATCH, phaser or declaration it cannot model, so
// nothing a first call works out is needed here.
bool kernelShape(const Callable& c) {
    if (c.isBlock || c.isMethod || c.builtin || c.isNative || c.isMultiDispatcher ||
        c.isMultiCandidate || c.isProto || c.isWhateverCode || c.isRegexRoutine || c.deprecated ||
        c.testAssertion || !c.hadSig || !c.params || !c.body || c.body->empty() ||
        !c.placeholders.empty() || c.usesArgs || c.implicitArgs || c.hasPrimed ||
        c.retLiteral || c.retRw || c.params->size() > (size_t)kMaxParams)
        return false;
    // `--> Int` / `--> Int:D`: every value a kernel returns is a defined Int
    if (!c.retType.empty() &&
        (retTypeCoerces(c.retType) || retTypeName(c.retType) != "Int" || retTypeSmiley(c.retType) == 'U'))
        return false;
    // …and an `Int` / `Int:D` parameter is one a plain Int binds to as it is
    // (any other argument keeps the call path, and its type check)
    for (auto& p : *c.params)
        if (p.sigil != '$' || p.named || p.slurpy || p.optional || p.invocant ||
            p.isRw || p.isRaw || p.defaultVal || p.subSig || p.litVal || p.whereExpr ||
            p.hadWhere || p.defConstraint == 2 || p.coerce || p.typeCapture || p.codeSig ||
            !p.captureName.empty() || !(p.type.empty() || p.type == "Int") || p.name.size() < 2 ||
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
    k->nparams = k->nframe = (int)c.params->size();
    slot.k.store(k, std::memory_order_release);
    session.push_back(k);
    k->body = stmts(*k, c, *c.body, true);
    if (!k->body && !resolveFailed) {
        // not one expression: the body as statements, on a frame of the
        // parameters and the sub's own `my`s (an `is copy` parameter is
        // writable, the others read-only)
        k->callees.clear(); k->keepAlive.clear(); k->calleeOwners.clear(); k->calls.clear();
        LoopCx cx;
        cx.sub = &c;
        cx.nint = k->nparams;
        cx.scopes.emplace_back();
        for (size_t i = 0; i < c.params->size(); i++)
            cx.scopes.back().push_back({(*c.params)[i].name, KT::Int, (int)i, !(*c.params)[i].isCopy});
        LoopCx* saved = L;
        L = &cx;
        k->body = ltail(*k, *c.body);
        L = saved;
        k->nframe = cx.nint;
        if (cx.nint > kMaxFrame) k->body = nullptr;
    }
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
// big switch, and an operator whose operands are a slot or a literal reads
// them in place instead of calling through to them.

// What ends the statements in flight: a bail, a `return`, and in a loop
// kernel `next` / `last` and a `when` that matched. Bools rather than one word of bits: the call
// path SETS them with plain stores, and a read-modify-write of a flag word
// there cost fib 12% (measured; the instructions were otherwise identical).
struct KRun {
    char* stackFloor;     // below this address the generic path would refuse
    int depthLeft;        // …or past this many more frames
    bool bail = false;
    bool ret = false;
    bool next = false;
    bool last = false;
    bool succeed = false;         // a `when` matched: leave its `given`
    std::string* sfr = nullptr;   // a loop kernel's string slots
};
[[gnu::always_inline]] inline bool stopped(const KRun& R) { return R.bail | R.ret | R.next | R.last | R.succeed; }

inline int64_t krun(const KNode* n, int64_t* fr, KRun& R) { return n->fn(n, fr, R); }
inline const std::string& srun(const KNode* n, int64_t* fr, KRun& R) { return n->sfn(n, fr, R); }

// operand kinds a fused operator reads in place
enum { kP = 0, kL = 1, kX = 2 };
template <int K>
[[gnu::always_inline]] inline int64_t leaf(const KNode* x, int64_t* fr, KRun& R) {
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
int64_t binFn(const KNode* n, int64_t* fr, KRun& R) {
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

int64_t paramFn(const KNode* n, int64_t* fr, KRun&) { return fr[n->slot]; }
int64_t litFn(const KNode* n, int64_t*, KRun&) { return n->lit; }
int64_t negFn(const KNode* n, int64_t* fr, KRun& R) {
    const int64_t x = krun(n->a, fr, R);
    if (x == LLONG_MIN) { R.bail = true; return 0; }
    return -x;
}
int64_t notFn(const KNode* n, int64_t* fr, KRun& R) { return krun(n->a, fr, R) == 0; }
int64_t andFn(const KNode* n, int64_t* fr, KRun& R) { return krun(n->a, fr, R) != 0 && krun(n->b, fr, R) != 0; }
int64_t orFn(const KNode* n, int64_t* fr, KRun& R) { return krun(n->a, fr, R) != 0 || krun(n->b, fr, R) != 0; }
int64_t condFn(const KNode* n, int64_t* fr, KRun& R) {
    return krun(n->a, fr, R) != 0 ? krun(n->b, fr, R) : krun(n->c, fr, R);
}
int64_t ifFn(const KNode* n, int64_t* fr, KRun& R) {
    if (krun(n->a, fr, R) != 0) return krun(n->b, fr, R);
    return n->c ? krun(n->c, fr, R) : 0;
}
int64_t retFn(const KNode* n, int64_t* fr, KRun& R) {
    const int64_t v = krun(n->a, fr, R);
    R.ret = true;
    return v;
}
int64_t seqFn(const KNode* n, int64_t* fr, KRun& R) {
    int64_t v = 0;
    for (int i = 0; i < n->nargs; i++) {
        v = krun(n->kids[i], fr, R);
        if (stopped(R)) break;
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
int64_t callFn(const KNode* n, int64_t* fr, KRun& R) {
    int64_t a[N > 0 ? N : kMaxParams];   // (N = 0: four to six arguments)
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

// A call into a kernel with `my`s of its own: the frame holds them after the
// arguments.
int64_t callFnL(const KNode* n, int64_t* fr, KRun& R) {
    int64_t a[kMaxFrame];
    for (int i = 0; i < n->nargs; i++) a[i] = krun(n->kids[i], fr, R);
    if (R.bail || --R.depthLeft < 0 || stackHere() <= R.stackFloor) {
        R.bail = true;
        return 0;
    }
    const int64_t v = krun(n->callee->body, a, R);
    R.ret = false;
    ++R.depthLeft;
    return v;
}

// --- loop kernels: running ---------------------------------------------------

// Integer stores. A value that would leave int64 bails before the slot is
// written, though nothing would see the slot after a bail anyway.
template <KOp OP, int K>
int64_t storeFn(const KNode* n, int64_t* fr, KRun& R) {
    const int64_t v = leaf<K>(n->a, fr, R);
    if constexpr (OP == KOp::Set) return fr[n->slot] = v;
    else {
        const int64_t r = apply<OP == KOp::AddTo ? KOp::Add : OP == KOp::SubTo ? KOp::Sub : KOp::Mul>(fr[n->slot], v, R);
        return fr[n->slot] = r;
    }
}
template <KOp OP>
KFn storePick(int k) {
    static const KFn T[3] = {storeFn<OP, kP>, storeFn<OP, kL>, storeFn<OP, kX>};
    return T[k];
}

template <int D, bool POST>
int64_t stepFn(const KNode* n, int64_t* fr, KRun& R) {
    const int64_t old = fr[n->slot];
    long long z;
    if (D > 0 ? add_ovf(old, 1, &z) : sub_ovf(old, 1, &z)) { R.bail = true; return 0; }
    fr[n->slot] = z;
    return POST ? old : z;
}

// What a loop does with the statements' stop flags at the end of a pass:
// `next` is spent, `last` ends the loop (and is spent), a bail ends everything.
// True: leave the loop.
[[gnu::always_inline]] inline bool loopStop(KRun& R) {
    if (R.bail || R.ret || R.succeed) return true;
    if (R.last) { R.last = false; return true; }
    R.next = false;
    return false;
}

int64_t forFn(const KNode* n, int64_t* fr, KRun& R) {
    const int64_t lo = krun(n->a, fr, R);
    const int64_t hi = R.bail ? 0 : krun(n->b, fr, R);
    if (R.bail) return 0;
    const KNode* body = n->c;
    const KFn bfn = body->fn;
    for (int64_t i = lo; i <= hi; i++) {
        fr[n->slot] = i;
        const int64_t v = bfn(body, fr, R);
        if (stopped(R) && loopStop(R)) return R.ret ? v : 0;   // a `return` carries its value out
        if (i == INT64_MAX) break;
    }
    return 0;
}

int64_t whileFn(const KNode* n, int64_t* fr, KRun& R) {
    for (;;) {
        const int64_t c = krun(n->a, fr, R);
        if (R.bail || !c) break;
        const int64_t v = krun(n->c, fr, R);
        if (stopped(R) && loopStop(R)) return R.ret ? v : 0;
    }
    return 0;
}
int64_t givenFn(const KNode* n, int64_t* fr, KRun& R) {
    fr[n->slot] = krun(n->a, fr, R);
    if (R.bail) return 0;
    const int64_t v = krun(n->c, fr, R);
    R.succeed = false;
    return R.ret ? v : 0;
}
int64_t sgivenFn(const KNode* n, int64_t* fr, KRun& R) {
    const std::string& t = srun(n->a, fr, R);
    if (&t != &R.sfr[n->slot]) R.sfr[n->slot] = t;
    const int64_t v = krun(n->c, fr, R);
    R.succeed = false;
    return R.ret ? v : 0;
}
int64_t whenFn(const KNode* n, int64_t* fr, KRun& R) {
    if (!krun(n->a, fr, R) || R.bail) return 0;
    const int64_t v = krun(n->c, fr, R);
    if (!stopped(R)) R.succeed = true;
    return R.ret ? v : 0;
}
// a C-style loop: `next` still runs the step, as the generic loop's does
int64_t cloopFn(const KNode* n, int64_t* fr, KRun& R) {
    for (;;) {
        if (n->a) {
            const int64_t c = krun(n->a, fr, R);
            if (R.bail || !c) break;
        }
        const int64_t v = krun(n->c, fr, R);
        if (stopped(R)) {
            if (R.bail || R.ret || R.succeed) return R.ret ? v : 0;
            if (R.last) { R.last = false; break; }
            R.next = false;
        }
        if (n->b) {
            krun(n->b, fr, R);
            if (R.bail) break;
        }
    }
    return 0;
}
int64_t lastFn(const KNode*, int64_t*, KRun& R) { R.last = true; return 0; }
int64_t nextFn(const KNode*, int64_t*, KRun& R) { R.next = true; return 0; }

// Strings. A plain Str is NFC already, and the generic path's `~` is
// nfcNormalize(l ~ r): text that is all ASCII joins to itself, so only the
// rest is renormalized. `~=` appends an ASCII right side in place, as the
// generic path does.
inline bool allAscii(const std::string& s) {
    for (unsigned char c : s) if (c >= 0x80) return false;
    return true;
}
const std::string& svarFn(const KNode* n, int64_t*, KRun& R) { return R.sfr[n->slot]; }
const std::string& slitFn(const KNode* n, int64_t*, KRun&) { return *n->str; }
const std::string& scatFn(const KNode* n, int64_t* fr, KRun& R) {
    std::string& out = R.sfr[n->slot];
    const std::string& l = srun(n->a, fr, R);
    if (&l != &out) out = l;   // (a temporary may be its own left operand only through a chain)
    const std::string& r = srun(n->b, fr, R);
    if (allAscii(out) && allAscii(r)) out += r;
    else out = nfcNormalize(out + r);
    return out;
}
const std::string& sintFn(const KNode* n, int64_t* fr, KRun& R) {
    std::string& out = R.sfr[n->slot];
    out = std::to_string(krun(n->a, fr, R));
    return out;
}
const std::string& scondFn(const KNode* n, int64_t* fr, KRun& R) {
    return krun(n->a, fr, R) != 0 ? srun(n->b, fr, R) : srun(n->c, fr, R);
}
int64_t charsFn(const KNode* n, int64_t* fr, KRun& R) {
    const std::string& s = srun(n->a, fr, R);
    return allAscii(s) ? (int64_t)s.size() : (int64_t)graphemeCount(s);
}
int64_t ssetFn(const KNode* n, int64_t* fr, KRun& R) {
    const std::string& v = srun(n->a, fr, R);
    std::string& dst = R.sfr[n->slot];
    // a concatenation's temporary is dead once stored: take its buffer
    if (n->a->op == KOp::SCat || n->a->op == KOp::SInt) std::swap(dst, R.sfr[n->a->slot]);
    else if (&v != &dst) dst = v;
    return 0;
}
int64_t sappFn(const KNode* n, int64_t* fr, KRun& R) {
    const std::string& v = srun(n->a, fr, R);
    std::string& dst = R.sfr[n->slot];
    if (allAscii(v)) dst += v;
    else dst = nfcNormalize(dst + v);
    return 0;
}

// The six string comparisons: a byte compare, which for UTF-8 is code-point
// order — what applyArith answers for two plain Strs.
template <KOp OP>
int64_t scmpFn(const KNode* n, int64_t* fr, KRun& R) {
    const std::string& x = srun(n->a, fr, R);
    const std::string& y = srun(n->b, fr, R);
    if constexpr (OP == KOp::SEq) return x.size() == y.size() && x.compare(y) == 0;
    else if constexpr (OP == KOp::SNe) return !(x.size() == y.size() && x.compare(y) == 0);
    else {
        const int c = x.compare(y);
        if constexpr (OP == KOp::SLt) return c < 0;
        else if constexpr (OP == KOp::SLe) return c <= 0;
        else if constexpr (OP == KOp::SGt) return c > 0;
        else return c >= 0;
    }
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
            n->fn = n->callee->nframe > n->nargs ? callFnL
                  : n->nargs == 1 ? callFn<1> : n->nargs == 2 ? callFn<2> : n->nargs == 3 ? callFn<3>
                                                                                           : callFn<0>;
            break;
        case KOp::Set: n->fn = storePick<KOp::Set>(l); break;
        case KOp::AddTo: n->fn = storePick<KOp::AddTo>(l); break;
        case KOp::SubTo: n->fn = storePick<KOp::SubTo>(l); break;
        case KOp::MulTo: n->fn = storePick<KOp::MulTo>(l); break;
        case KOp::PreInc: n->fn = stepFn<1, false>; break;
        case KOp::PreDec: n->fn = stepFn<-1, false>; break;
        case KOp::PostInc: n->fn = stepFn<1, true>; break;
        case KOp::PostDec: n->fn = stepFn<-1, true>; break;
        case KOp::For: n->fn = forFn; break;
        case KOp::While: n->fn = whileFn; break;
        case KOp::Last: n->fn = lastFn; break;
        case KOp::Next: n->fn = nextFn; break;
        case KOp::Given: n->fn = givenFn; break;
        case KOp::SGiven: n->fn = sgivenFn; break;
        case KOp::When: n->fn = whenFn; break;
        case KOp::CLoop: n->fn = cloopFn; break;
        case KOp::SInt: n->sfn = sintFn; break;
        case KOp::SCond: n->sfn = scondFn; break;
        case KOp::Chars: n->fn = charsFn; break;
        case KOp::SVar: n->sfn = svarFn; break;
        case KOp::SLit: n->sfn = slitFn; break;
        case KOp::SCat: n->sfn = scatFn; break;
        case KOp::SSet: n->fn = ssetFn; break;
        case KOp::SApp: n->fn = sappFn; break;
        case KOp::SEq: n->fn = scmpFn<KOp::SEq>; break;
        case KOp::SNe: n->fn = scmpFn<KOp::SNe>; break;
        case KOp::SLt: n->fn = scmpFn<KOp::SLt>; break;
        case KOp::SLe: n->fn = scmpFn<KOp::SLe>; break;
        case KOp::SGt: n->fn = scmpFn<KOp::SGt>; break;
        case KOp::SGe: n->fn = scmpFn<KOp::SGe>; break;
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

// A compiled loop kernel, owned by its ForStmt (loopKernel) and never freed:
// like the AST it hangs from, it lives as long as the program.
struct LKernel {
    IKernel k;                 // the nodes; k.body is the loop; k.graph the sub kernels it calls
    std::vector<LOuter> outer; // the program's variables it copies, in slot order of discovery
    std::vector<std::pair<std::string, Callable*>> calls;   // `&name` → the routine, found from the loop's scope
    int nint = 0, nstr = 0;
    std::vector<std::string> binOps;
    bool usesPrefix = false;
    bool usesMethods = false;
    std::atomic<unsigned char> bails{0};
};

// The kernel slot of a loop statement: a `for` over an integer Range, a
// `while` / `until`, or a C-style `loop`.
PublishedOnce<void*>* loopSlot(Stmt* s, DecidedOnce<unsigned char>*& tries) {
    switch (s->kind) {
        case NK::ForStmt: { auto* f = static_cast<ForStmt*>(s); tries = &f->loopKernelTries; return &f->loopKernel; }
        case NK::WhileStmt: { auto* w = static_cast<WhileStmt*>(s); tries = &w->loopKernelTries; return &w->loopKernel; }
        case NK::LoopStmt: { auto* l = static_cast<LoopStmt*>(s); tries = &l->loopKernelTries; return &l->loopKernel; }
        default: return nullptr;
    }
}

// The frame slots 0 and 1 hold a top-level loop's bounds and 2 its variable.
constexpr int kLoSlot = 0, kHiSlot = 1, kVarSlot = 2;

// The container a free name of the loop denotes HERE, found as Env::find finds
// it, with the frame that owns it.
Value* outerCell(Env* env, const std::string& name, Env** owner) {
    Value* raw = env->findRaw(name, owner);
    return raw ? raw->deref() : nullptr;
}

// A container the kernel will STORE to must be one a plain store is exactly
// right for: every trait below makes the generic store do something more (a
// type or `where` check, a coercion, a default, a native width, an `is rw`
// write-through, a readonly refusal), and the write-back does none of it.
bool writableCell(const Value& v, Env* owner, const std::string& name) {
    if (v.readonly || v.immutableBind || v.itemized || v.pairValRO) return false;
    if (owner && owner->ex) {
        const EnvExtras& x = *owner->ex;
        if (x.varDefault.count(name) || x.varCoerce.count(name) || x.varDynamic.count(name) ||
            x.varSmiley.count(name) || x.varWhere.count(name) || x.varConstant.count(name) ||
            x.varValueBound.count(name) || x.rwLinks.count(name) || x.rwDirect.count(name) ||
            x.rwRoots.count(name) || x.rwSynced.count(name) || x.rwDead.count(name) ||
            x.rwCelled.count(name))
            return false;
    }
    return true;
}

// All or nothing: the kernels one compile built link to each other, so they
// are published together or not at all. `asker` is the routine whose call
// started it (settled for good when it fails; the others may still qualify on
// their own), null for a loop.
void finishSession(Compiler& cc, bool ok, const Callable* asker) {
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
            k->owner->intKernel.state.store(k->owner == asker ? 0 : -1, std::memory_order_release);
            delete k;
        }
    }
}

// Are the routines a kernel graph calls still the ones it was compiled
// against, and none of them wrapped? (Checked at each entry: a `my &g` can be
// re-assigned and a sub re-bound, which a compiled call would not see.)
bool callsBound(const std::vector<IKernel*>& graph) {
    for (auto* g : graph) {
        for (auto* o : g->calleeOwners)
            if (!o->wrappers.empty()) return false;
        for (auto& kc : g->calls) {
            const Value* f = g->scope ? g->scope->find(kc.first) : nullptr;
            if (!f || f->t != VT::Code || f->code() != kc.second) return false;
        }
    }
    return true;
}

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
            finishSession(cc, ok, &c);
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
    int64_t a[kMaxFrame];
    for (size_t i = 0; i < args.size(); i++) {
        if (!plainIntArg(args[i])) return false;
        a[i] = args[i].i;
    }
    if (!c.wrappers.empty() || !callsBound(k->graph)) return false;
    for (auto& op : k->binOps)
        if (opShadowed(op)) return false;
    if (k->usesPrefix && g_userPrefixShadow.load(std::memory_order_relaxed)) return false;
    // the generic path's own limits (DepthGuard): bail where it would refuse,
    // and it will refuse with its own error. (Recorded here when no guarded
    // frame has run yet: a sub called from the mainline declined until one
    // had, so only a RECURSIVE sub ever got its kernel at the top level.)
    ensureStackBounds(stackHere());
    if (!t_stack.top) return false;
    size_t reserve = size_t(2) << 20;
    if (t_stack.limit < reserve * 4) reserve = t_stack.limit / 4;
    KRun R;
    R.stackFloor = t_stack.top - (t_stack.limit - reserve) + 4096;
    R.depthLeft = 100000 - callDepth - 1;
    if (R.depthLeft <= 0) return false;
    const int64_t v = krun(k->body, a, R);
    if (R.bail) {
        if (g_kernelTrace)
            std::fprintf(stderr, "kernel: %s bailed\n", c.name.empty() ? "<anon>" : c.name.c_str());
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

// A loop statement run as a loop kernel: a `for` over an integer Range with
// `lo .. hi` already worked out (`var` its loop variable, `$_` when it names
// none), or a `while` / `until`, or a C-style `loop` whose init has already
// run. False: nothing ran and nothing changed — run the loop the ordinary way.
bool Interpreter::tryLoopKernel(Stmt* loop, const std::string& var, long long lo, long long hi) {
    const bool isFor = loop->kind == NK::ForStmt;
    if (g_noKernels || g_traceStmts || jit::on() || (isFor && lo > hi)) return false;
    // the kernel holds the loop's variables in its frame until the loop is
    // done, which only this thread may do
    if (liveWorkers_.load(std::memory_order_acquire) > 0 || cuedLoads_.load(std::memory_order_acquire) > 0)
        return false;
    DecidedOnce<unsigned char>* tries = nullptr;
    PublishedOnce<void*>* slot = loopSlot(loop, tries);
    if (!slot) return false;
    void* h = slot->get();
    if (h == kNeverKernel) return false;
    Env* env = tctx_.cur.get();
    if (!h) {
        // Decided on the first entry, from the syntax and from the types the
        // loop's variables hold now. Body checks that do not depend on the
        // values come first, so a loop that can never qualify says so for good.
        std::lock_guard<std::mutex> lk(g_compileMu);
        if (!(h = slot->get())) {
            auto* lk2 = new LKernel;
            LoopCx cx;
            cx.env = env;
            cx.nint = 3;   // a `for`'s bounds and loop variable
            cx.loops = 1;
            Compiler cc;
            cc.global = global_.get();
            cc.L = &cx;
            KNode* body = nullptr;
            KNode* root = nullptr;
            if (isFor) {
                auto* fs = static_cast<ForStmt*>(loop);
                cx.scopes.push_back({{var, KT::Int, kVarSlot, true}});
                cx.inModifier = fs->modifier;
                if (fs->asExpr || fs->destructure || fs->rwVars || !fs->params.empty() || fs->emptyPointy ||
                    fs->hyper || fs->vars.size() > 1 || !fs->label.empty())
                    cx.why = "a `for` of this shape";
                else {
                    bool traits = false;
                    for (auto tr : fs->varTraits) traits = traits || tr;
                    if (traits) cx.why = "an `is rw` / `is raw` loop variable";
                    else body = cc.lbody(lk2->k, fs->body.get(), !fs->modifier);
                }
                if (body) {
                    KNode* loLeaf = lk2->k.node(KOp::Param); loLeaf->slot = kLoSlot;
                    KNode* hiLeaf = lk2->k.node(KOp::Param); hiLeaf->slot = kHiSlot;
                    root = lk2->k.node(KOp::For);
                    root->slot = kVarSlot; root->a = loLeaf; root->b = hiLeaf; root->c = body;
                }
            }
            else if (loop->kind == NK::WhileStmt) {
                // the condition declares nothing the kernel could keep (a `my`
                // there belongs to the enclosing block, after the loop too)
                auto* ws = static_cast<WhileStmt*>(loop);
                cx.scopes.emplace_back();
                if (ws->modifier || ws->asExpr || !ws->var.empty() || !ws->params.empty() || !ws->label.empty())
                    cx.why = "a `while` of this shape";
                else {
                    KT ct;
                    cx.inModifier = true;
                    KNode* cn = cc.expr(lk2->k, nullptr, ws->cond.get(), true, ct);
                    cx.inModifier = false;
                    if (cn && (ct == KT::Str || ct == KT::Void)) { cn = nullptr; cx.why = "a Str as a condition"; }
                    if (cn && ws->isUntil) { KNode* nn = lk2->k.node(KOp::Not); nn->a = cn; cn = nn; }
                    if (cn) body = cc.lbody(lk2->k, ws->body.get(), true);
                    if (body) { root = lk2->k.node(KOp::While); root->a = cn; root->c = body; }
                }
            }
            else {
                // `loop (INIT; COND; STEP)`: the interpreter has run INIT, so
                // what it declared is a variable of the program by now
                auto* ls = static_cast<LoopStmt*>(loop);
                cx.scopes.emplace_back();
                if (ls->asExpr || !ls->label.empty()) cx.why = "a `loop` of this shape";
                else {
                    KT ct = KT::Int, st = KT::Int;
                    KNode* cn = nullptr;
                    KNode* sn = nullptr;
                    bool ok = true;
                    cx.inModifier = true;
                    if (ls->cond) {
                        cn = cc.expr(lk2->k, nullptr, ls->cond.get(), true, ct);
                        ok = cn && ct != KT::Str && ct != KT::Void;
                    }
                    if (ok && ls->incr) {
                        sn = cc.expr(lk2->k, nullptr, ls->incr.get(), false, st);
                        ok = sn && st != KT::Str;
                    }
                    cx.inModifier = false;
                    if (ok) body = cc.lbody(lk2->k, ls->body.get(), true);
                    else if (cx.why.empty()) cx.why = "a `loop` header of this kind";
                    if (body) { root = lk2->k.node(KOp::CLoop); root->a = cn; root->b = sn; root->c = body; }
                }
            }
            // the sub kernels the body calls were compiled with it
            finishSession(cc, body != nullptr, nullptr);
            // a body nothing can be compiled for is settled for good; one that
            // met a value of another type (or a name not yet bound) may be
            // tried again on a later entry — but not on every one
            if (!root) {
                const bool valueDependent = cx.why.find("holds neither") != std::string::npos ||
                                            cx.why.find("not in scope") != std::string::npos;
                if (g_kernelTrace)
                    std::fprintf(stderr, "kernel: loop at line %d declined: %s\n", loop->line,
                                 cx.why.empty() ? "?" : cx.why.c_str());
                delete lk2;
                if (valueDependent) {
                    const unsigned char t = *tries;
                    *tries = (unsigned char)(t + 1);
                    if (t + 1 < 4) return false;
                }
                slot->publish(kNeverKernel);
                return false;
            }
            link(root);
            lk2->k.body = root;
            // the loop's own calls resolve from the scope it runs in, which is
            // a different frame at every entry: checked there (below), not
            // through k.scope
            lk2->calls = std::move(lk2->k.calls);
            lk2->k.calls.clear();
            lk2->k.scope = nullptr;
            collectGraph(&lk2->k, lk2->k.graph);
            lk2->outer = std::move(cx.outer);
            lk2->nint = cx.nint;
            lk2->nstr = cx.nstr;
            lk2->binOps = cc.ops;
            lk2->usesPrefix = cc.prefix;
            lk2->usesMethods = cx.usesMethods;
            slot->publish(lk2);
            h = lk2;
            if (g_kernelTrace)
                std::fprintf(stderr, "kernel: loop at line %d compiled (%zu variable(s))\n", loop->line,
                             lk2->outer.size());
        }
        if (h == kNeverKernel) return false;
    }
    auto* L = static_cast<LKernel*>(h);
    auto notEntered = [&](const char* why, const std::string& name = std::string()) {
        if (g_kernelTrace)
            std::fprintf(stderr, "kernel: loop at line %d not entered: %s%s%s\n", loop->line, why,
                         name.empty() ? "" : " ", name.c_str());
        return false;
    };
    for (auto& op : L->binOps)
        if (opShadowed(op)) return notEntered("the program shadows operator", op);
    for (auto& kc : L->calls) {
        const Value* f = env->find(kc.first);
        if (!f || f->t != VT::Code || f->code() != kc.second)
            return notEntered("a call that finds another routine:", kc.first);
    }
    if (!callsBound(L->k.graph)) return notEntered("a sub it calls is wrapped or re-bound");
    if (L->usesPrefix && g_userPrefixShadow.load(std::memory_order_relaxed))
        return notEntered("the program declares a prefix operator");
    if (L->usesMethods && !builtinExt_.empty()) return notEntered("a built-in type is augmented");
    // The program's variables, found again for THIS entry: each must still
    // hold the type its slots were compiled for, each one written must be a
    // plain container, and no two names may share a container (the kernel
    // would keep two copies of it).
    const size_t no = L->outer.size();
    Value* stackCells[16];
    std::unique_ptr<Value*[]> heapCells;
    Value** cells = no <= 16 ? stackCells : (heapCells.reset(new Value*[no]), heapCells.get());
    for (size_t i = 0; i < no; i++) {
        const LOuter& o = L->outer[i];
        Env* owner = nullptr;
        Value* cell = outerCell(env, o.name, &owner);
        if (!cell) return notEntered("no variable", o.name);
        if (o.t == KT::Int ? !plainIntValue(*cell) : !plainStrValue(*cell))
            return notEntered("a value of another type in", o.name);
        if (o.written && !writableCell(*cell, owner, o.name))
            return notEntered("a container a plain store would not honour:", o.name);
        for (size_t j = 0; j < i; j++)
            if (cells[j] == cell) return notEntered("two names for one container:", o.name);
        cells[i] = cell;
    }
    int64_t stackInts[32];
    std::unique_ptr<int64_t[]> heapInts;
    int64_t* fr = L->nint <= 32 ? stackInts : (heapInts.reset(new int64_t[L->nint]), heapInts.get());
    std::unique_ptr<std::string[]> strs(L->nstr ? new std::string[L->nstr] : nullptr);
    fr[kLoSlot] = lo;
    fr[kHiSlot] = hi;
    for (size_t i = 0; i < no; i++) {
        const LOuter& o = L->outer[i];
        if (o.t == KT::Int) fr[o.slot] = cells[i]->i;
        else strs[o.slot] = cells[i]->s.str();
    }
    // the generic path's own limits for the calls it makes (see tryIntKernel)
    KRun R;
    R.stackFloor = nullptr;
    R.depthLeft = 0;
    if (!L->calls.empty()) {
        ensureStackBounds(stackHere());
        if (!t_stack.top) return false;
        size_t reserve = size_t(2) << 20;
        if (t_stack.limit < reserve * 4) reserve = t_stack.limit / 4;
        R.stackFloor = t_stack.top - (t_stack.limit - reserve) + 4096;
        R.depthLeft = 100000 - tctx_.callDepth - 1;
        if (R.depthLeft <= 0) return false;
    }
    R.sfr = strs.get();
    krun(L->k.body, fr, R);
    if (R.bail) {
        // the loop runs again the ordinary way, from the start; one that keeps
        // bailing (its numbers outgrow int64) stops trying
        const unsigned char b = L->bails.load(std::memory_order_relaxed);
        L->bails.store((unsigned char)(b + 1), std::memory_order_relaxed);
        if (b + 1 >= 2) *slot = kNeverKernel;
        if (g_kernelTrace) std::fprintf(stderr, "kernel: loop at line %d bailed\n", loop->line);
        return false;
    }
    for (size_t i = 0; i < no; i++) {
        const LOuter& o = L->outer[i];
        if (!o.written) continue;
        if (o.t == KT::Int) cells[i]->i = fr[o.slot];
        else cells[i]->s = std::move(strs[o.slot]);
    }
    return true;
}

}  // namespace rakupp
