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
//    only `$` variables holding a plain Int, Num or Str, its own `my`
//    locals and loop variables, through the integer operators above, `**`,
//    `min`, `max`, Num `+ - * / **` and comparisons, the six string
//    comparisons, `~` and `=`, `+=`, `-=`, `*=`, `/=` (Nums), `~=`, `++`,
//    `--`, the methods `.chars .abs .sign .Str .Num .Int .ord .uc .lc
//    .substr`, with `if`/`unless`, nested `for` over integer ranges,
//    `while`/`until`, `last` and `next`, and `given`/`when` over a plain
//    topic. The kernel works on
//    COPIES of those variables (the frame) and writes them back only when the
//    whole loop is done, so until then nothing it did is visible to anything.
//    It may also use plain `@` and `%` containers of the program IN PLACE:
//    `@a[i]` and `%h{k}` read, stored, `+= -= *= ~=` and `++`/`--`,
//    `@a.push`, `.elems`, each element checked as it is read. Those writes
//    are visible at once, so each is logged first, and a bail takes them all
//    back before anything else happens (KConts).
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
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <numeric>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>   // _AddressOfReturnAddress
#endif

// Every Num operation rounds on its own, as the generic path's do: nothing
// here may fuse a multiply and an add.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace rakupp {

namespace {

// Exact numbers (an Int or a Rat in one slot pair) compute in 128 bits.
constexpr bool kHasExact = RAKUPP_HAS_INT128 != 0;
// The word in front of a loop frame: the denominator of the exact number a
// node has just returned. (A register in KRun cost fib 5%, and a slot of the
// frame that moved every other slot by one cost mainwhen 4%; neither uses it.)
constexpr int kDenSlot = -1;

// A Num travels through the int64 world of the kernels as its bits.
inline int64_t dbits(double d) { int64_t r; std::memcpy(&r, &d, sizeof r); return r; }
inline double bitsd(int64_t r) { double d; std::memcpy(&d, &r, sizeof d); return d; }

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
    // Nums: a double travels as its bits in the int64 a node returns and a
    // slot holds (Param, Lit, Set, Cond carry it unchanged)
    NAdd, NSub, NMul, NDiv, NPow, NNeg,
    NLt, NLe, NGt, NGe, NEq, NNe,
    NAddTo, NSubTo, NMulTo, NDivTo,
    NPreInc, NPreDec, NPostInc, NPostDec,
    NOfInt,   // an Int as a Num, as the generic path converts one
    NTruth,   // a Num as a condition: true when not zero
    SNum,     // a Num's text
    // more integer operators and pure methods
    Pow, Min, Max, Abs, Sign,
    Ord, Uc, Lc, Substr,
    // exact numbers (KT::Exact): an Int or a Rat. A node returns the
    // numerator and leaves the denominator in the loop frame's kDenSlot, 0
    // for an Int; a variable is two integer slots, the numerator and then
    // the denominator.
    XVar, XLit, XOfInt,
    XAdd, XSub, XMul, XDiv, XNeg,
    XLt, XLe, XGt, XGe, XEq, XNe,
    XSet, XAddTo, XSubTo, XMulTo, XDivTo,
    XPreInc, XPreDec, XPostInc, XPostDec,
    XTruth, SExact, XNumer, XDenom,
    // containers of the program, read and written IN PLACE (an `@a` or `%h`
    // in the loop's scope): `lit` is the container's index in KRun::conts,
    // `a` the subscript, `nargs` the kFlag bits below. Every write is logged
    // first, so a bail can put the container back (KRun::undo).
    CGet, SCGet,          // @a[i] / %h{k} as an Int / a Str (`slot`: a string temporary)
    CElems,               // @a.elems / %h.elems
    CSet, SCSet,          // … = b
    CAddTo, CSubTo, CMulTo, CSApp,
    CPreInc, CPreDec, CPostInc, CPostDec,
    CPush, SCPush,        // @a.push(a)
};
// KNode::nargs on a container node
constexpr uint8_t kCHash = 1;     // %h, not @a
constexpr uint8_t kCStrKey = 2;   // the key is a Str node (an Int key is its decimal text)

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
    union { IKernel* callee = nullptr; int64_t den; };   // Call / XLit: the denominator
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
enum class KT { Int, Bool, Str, Void, Num, Exact };

// A loop kernel's names. Every `$` variable the loop reads or writes gets a
// slot in the frame: an integer slot (a Num's too, as its bits) or a string
// slot, by the type its value
// has when the kernel is compiled — which is the type it must still have at
// every later entry.
struct LSym { std::string name; KT t; int slot; bool ro; };
struct LOuter { std::string name; KT t; int slot; bool written; };
// A container of the program the loop reads or writes in place: `@a` or `%h`,
// and the kind of element it holds (Int or Str, from its contents or from the
// first value the loop stores; Void while neither has said).
struct LCont { std::string name; bool hash; KT elem; bool written; };
struct LoopCx {
    Env* env = nullptr;                      // where the loop's free names resolve (this entry)
    const Callable* sub = nullptr;           // set for a sub body compiled as statements: no free names
    std::vector<std::vector<LSym>> scopes;   // the kernel's own: loop variables and `my`s
    std::vector<LOuter> outer;               // variables of the program, copied in and out
    std::vector<LCont> conts;                // containers of the program, used in place
    int nint = 0, nstr = 0;                  // frame sizes, temporaries included
    int loops = 0;                           // loops enclosing the statement being compiled
    int givens = 0;                          // `given`s enclosing it, inside the innermost loop
    bool inModifier = false;                 // a statement-modifier body: no `my` (it would leak out)
    bool usesInc = false;                    // any `++` / `--`
    bool usesMethods = false;                // a built-in method (`.chars`), which `augment` could replace
    // Int variables that are assigned a Rat somewhere in the loop: compiled as
    // exact slots from the start, which takes a second compile once the first
    // has found them (`again`)
    std::vector<std::string> promote;
    bool again = false;
    bool promoted(const std::string& n) const {
        for (auto& p : promote) if (p == n) return true;
        return false;
    }
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
    // containers (task 1)
    int cont(const std::string& name, bool hash, std::string& why);
    KNode* contRef(IKernel& k, const Callable* c, Index* ix, KOp op, KT& elem);
    // An Int or a Str as a string (an Int's text is what `~` and interpolation
    // make of it); null for anything else.
    KNode* asStr(IKernel& k, KNode* n, KT t) {
        if (!n) return nullptr;
        if (t == KT::Str) return n;
        if ((t != KT::Int && t != KT::Num && t != KT::Exact) || !L || L->sub) return nullptr;
        KNode* s = k.node(t == KT::Num ? KOp::SNum : t == KT::Exact ? KOp::SExact : KOp::SInt);
        s->a = n; s->slot = L->nstr++;
        return s;
    }
    // An Int or an exact number as an exact number; null for anything else.
    KNode* asExact(IKernel& k, KNode* n, KT t) {
        if (!n) return nullptr;
        if (t == KT::Exact) return n;
        if (t != KT::Int) return nullptr;
        if (n->op == KOp::Lit) { KNode* l = k.node(KOp::XLit); l->lit = n->lit; l->slot = 0; return l; }
        KNode* c = k.node(KOp::XOfInt); c->a = n;
        return c;
    }
    // A slot pair for an exact variable
    int exactSlot() { const int s = L->nint; L->nint += 2; return s; }
    // An Int or a Num as a Num: an Int literal is converted here, as the
    // generic path would convert it at run time; null for anything else.
    KNode* asNum(IKernel& k, KNode* n, KT t) {
        if (!n) return nullptr;
        if (t == KT::Num) return n;
        if (t != KT::Int) return nullptr;
        if (n->op == KOp::Lit) { KNode* l = k.node(KOp::Lit); l->lit = dbits((double)n->lit); return l; }
        KNode* c = k.node(KOp::NOfInt); c->a = n;
        return c;
    }
    // An expression read as a condition: a Num is true when it is not zero
    // (its bits are not: -0e0).
    KNode* cexpr(IKernel& k, const Callable* c, Expr* e, KT& t) {
        KNode* n = expr(k, c, e, true, t);
        if (n && (t == KT::Num || t == KT::Exact)) {
            KNode* b = k.node(t == KT::Num ? KOp::NTruth : KOp::XTruth); b->a = n; n = b; t = KT::Bool;
        }
        return n;
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
inline bool plainNumValue(const Value& v) {
    return v.t == VT::Num && !v.x_ && v.pk_ == PK::None && !v.natBits && !v.natSigned && !v.natFloat &&
           !v.b && !v.isList && !v.objKeyed && !v.namedArg && v.enumName.empty() && v.enumType.empty() &&
           v.hashKind.empty() && v.s.empty();
}
// A Rat whose numerator and denominator both fit an int64 (not a FatRat, not
// a zero denominator, no tag, nothing in its block but the two parts)
inline bool plainRatValue(const Value& v) {
    if (v.t != VT::Rat || !v.x_ || v.pk_ != PK::None || v.natBits || v.natSigned || v.natFloat || v.b ||
        v.isList || v.objKeyed || v.namedArg || !v.enumName.empty() || !v.enumType.empty() ||
        !v.hashKind.empty() || !v.s.empty())
        return false;
    const ValueExt& x = *v.x_;
    if (x.fatRat || x.big || x.pairKey || x.cont || x.holdsCells || x.rNum || !x.ratN || !x.ratD) return false;
    if (!x.ratN->fitsLL() || !x.ratD->fitsLL()) return false;
    return x.ratD->toLL() > 0;
}
inline bool plainStrValue(const Value& v) {
    return v.t == VT::Str && !v.x_ && v.pk_ == PK::None && !v.natBits && !v.natSigned && !v.natFloat &&
           !v.b && !v.isList && !v.objKeyed && !v.namedArg && v.enumName.empty() && v.enumType.empty() &&
           v.hashKind.empty();
}

// An `@a` / `%h` a loop kernel may read and write in place: one whose element
// store is a plain store and nothing more. Not a List or a Map (immutable),
// not typed, shaped, native or lazy, no `is default`, no bound elements, no
// object keys, no Set/Bag/Mix or other tagged kind.
inline bool plainContainer(const Value& v, bool hash) {
    if (v.isList || v.itemized || v.objKeyed || v.namedArg || !v.enumName.empty() || !v.enumType.empty() ||
        !v.hashKind.empty() || !v.s.empty() || v.natBits || v.natSigned || v.natFloat)
        return false;
    if (hash ? (v.t != VT::Hash || v.pk_ != PK::Hash || !v.hash() || v.hash()->hasObjKeys())
             : (v.t != VT::Array || v.pk_ != PK::List || !v.arr()))
        return false;
    if (v.x_) {
        const ValueExt& x = *v.x_;
        if (x.holdsCells || x.big || x.ratN || x.ratD || x.pairKey) return false;
        if (x.cont) {
            const ValueContExt& c = *x.cont;
            if (c.elemDefault || c.ext || c.shape || !c.ofType.empty() || c.seqTok) return false;
        }
    }
    return true;
}
// …and an element a container store may simply replace: a plain Int or Str,
// or the Any of a hole
inline bool plainAnyValue(const Value& v) {
    return v.t == VT::Any && !v.x_ && v.pk_ == PK::None && !v.natBits && !v.b && !v.isList &&
           v.hashKind.empty() && v.s.empty();
}
bool plainContName(const std::string& n, char sigil) {
    if (n.size() < 2 || n[0] != sigil) return false;
    const char c = n[1];
    if (!(ascii::isalpha((unsigned char)c) || c == '_')) return false;
    return n.find("::") == std::string::npos;
}

// Can evaluating `e` change anything? (An assignment or a ++/-- anywhere in it.)
bool sideEffect(const Expr* e) {
    if (!e) return false;
    switch (e->kind) {
        case NK::Assign: return true;
        case NK::Unary: {
            auto* u = static_cast<const Unary*>(e);
            return u->op == "++" || u->op == "--" || sideEffect(u->operand.get());
        }
        case NK::Binary: {
            auto* b = static_cast<const Binary*>(e);
            return sideEffect(b->lhs.get()) || sideEffect(b->rhs.get());
        }
        case NK::Ternary: {
            auto* t = static_cast<const Ternary*>(e);
            return sideEffect(t->cond.get()) || sideEffect(t->then.get()) || sideEffect(t->els.get());
        }
        case NK::Index: return sideEffect(static_cast<const Index*>(e)->index.get());
        case NK::MethodCall: {
            auto* m = static_cast<const MethodCall*>(e);
            if (m->method == "push") return true;
            for (auto& a : m->args) if (sideEffect(a.get())) return true;
            return sideEffect(m->inv.get());
        }
        case NK::InterpStr:
            for (auto& p : static_cast<const InterpStr*>(e)->parts) if (sideEffect(p.get())) return true;
            return false;
        case NK::Call:
            for (auto& a : static_cast<const Call*>(e)->args) if (sideEffect(a.get())) return true;
            return false;
        default: return false;
    }
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

// The program's `@a` / `%h` as an index into L->conts, found from the loop's
// scope: a plain container (plainContainer) whose first element says what
// the loop will find in it, a plain Int or a plain Str. -1 when it cannot be
// used, with `why` saying so.
int Compiler::cont(const std::string& name, bool hash, std::string& why) {
    for (size_t i = 0; i < L->conts.size(); i++)
        if (L->conts[i].name == name) return (int)i;
    if (!plainContName(name, hash ? '%' : '@')) { why = "container " + name; return -1; }
    Value* raw = L->env->findRaw(name);
    if (!raw) { why = "container " + name + " is not in scope"; return -1; }
    const Value& cv = *raw->deref();
    if (!plainContainer(cv, hash)) { why = "container " + name + " of a kind a plain store would not honour"; return -1; }
    const Value* first = nullptr;
    if (hash) { for (auto& kv : *cv.hash()) { first = &kv.second; break; } }
    else if (!cv.arr()->empty()) first = &(*cv.arr())[0];
    KT elem = KT::Void;
    if (first) {
        if (plainIntValue(*first)) elem = KT::Int;
        else if (plainStrValue(*first)) elem = KT::Str;
        else { why = "container " + name + " holds neither a plain Int nor a plain Str"; return -1; }
    }
    L->conts.push_back({name, hash, elem, false});
    return (int)L->conts.size() - 1;
}

// `@a[EXPR]` / `%h{EXPR}` as a container node of kind `op`: the subscript an
// Int (or, for a hash, a Str), with no side effect of its own, so that the
// order the generic path evaluates a store's two sides in cannot matter.
KNode* Compiler::contRef(IKernel& k, const Callable* c, Index* ix, KOp op, KT& elem) {
    if (!L || L->sub) return refuse("a container in a sub");
    if (ix->multiDim || ix->zen || ix->semicolonSub || !ix->adverb.empty() || !ix->index || !ix->base ||
        ix->base->kind != NK::VarExpr)
        return refuse("a subscript of this form");
    auto* bv = static_cast<VarExpr*>(ix->base.get());
    const bool hash = ix->isHash;
    if (bv->declare || bv->name.empty() || bv->name[0] != (hash ? '%' : '@'))
        return refuse("a subscript of something not an @ or % variable");
    std::string why;
    const int ci = cont(bv->name, hash, why);
    if (ci < 0) return refuse(why);
    if (sideEffect(ix->index.get())) return refuse("a subscript with a side effect");
    KT kt;
    KNode* key = expr(k, c, ix->index.get(), false, kt);
    if (!key) return nullptr;
    uint8_t fl = hash ? kCHash : 0;
    if (kt == KT::Str && hash) fl |= kCStrKey;
    else if (kt != KT::Int) return refuse("a subscript neither an Int nor a hash's Str");
    KNode* n = k.node(op); n->lit = ci; n->a = key; n->nargs = fl;
    elem = L->conts[ci].elem;
    return n;
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
        case NK::NumLit: {
            // `0.5e0`; `0.5` is a Rat
            auto* l = static_cast<NumLit*>(e);
            if (!L || L->sub) return refuse("a Num in a sub");
            if (l->imaginary) return refuse("a Complex literal");
            if (l->isRat) {
                // `0.5`: reduced here as the generic path reduces it
                if (!kHasExact) return refuse("a Rat without 128-bit integers");
                if (!l->bigNum.empty() || !l->bigDen.empty() || l->ratDen <= 0) return refuse("a Rat literal of this size");
                long long g = std::gcd(l->ratNum < 0 ? -l->ratNum : l->ratNum, l->ratDen);
                if (g <= 0) g = 1;
                KNode* n = k.node(KOp::XLit); n->lit = l->ratNum / g; n->slot = (int32_t)0;
                n->den = l->ratDen / g;
                t = KT::Exact;
                return n;
            }
            KNode* n = k.node(KOp::Lit); n->lit = dbits(l->v); t = KT::Num;
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
                    if (plainIntValue(cv) && L->promoted(v->name)) { vt = KT::Exact; slot = exactSlot(); }
                    else if (plainIntValue(cv)) { vt = KT::Int; slot = L->nint++; }
                    else if (plainRatValue(cv) && kHasExact) { vt = KT::Exact; slot = exactSlot(); }
                    else if (plainNumValue(cv)) { vt = KT::Num; slot = L->nint++; }
                    else if (plainStrValue(cv)) { vt = KT::Str; slot = L->nstr++; }
                    else return refuse("variable " + v->name + " holds neither a plain Int, Rat, Num nor Str");
                    L->outer.push_back({v->name, vt, slot, false});
                    o = &L->outer.back();
                }
                st = o->t; slot = o->slot;
            }
            KNode* n = k.node(st == KT::Str ? KOp::SVar : st == KT::Exact ? KOp::XVar : KOp::Param);
            n->slot = slot; t = st;
            return n;
        }
        case NK::Index: {
            // an element read: the element is checked on every read, and
            // anything but the kind the loop was compiled for bails
            if (!L) return nullptr;
            KT et;
            KNode* n = contRef(k, c, static_cast<Index*>(e), KOp::CGet, et);
            if (!n) return nullptr;
            if (et == KT::Void)
                return refuse("container " + L->conts[n->lit].name + " holds neither a plain Int nor a plain Str yet");
            if (et == KT::Str) { n->op = KOp::SCGet; n->slot = L->nstr++; }
            t = et;
            return n;
        }
        case NK::Unary: {
            auto* u = static_cast<Unary*>(e);
            if (!u->operand) return nullptr;
            KT ot;
            if ((u->op == "++" || u->op == "--") && L && u->operand->kind == NK::Index) {
                // an element stepped in place; a hole or a missing key counts
                // from 0, as the generic path's does
                KT et;
                KNode* n = contRef(k, c, static_cast<Index*>(u->operand.get()), KOp::CPreInc, et);
                if (!n) return nullptr;
                LCont& ct = L->conts[n->lit];
                if (et == KT::Str) return refuse("++/-- on a Str element");
                ct.elem = KT::Int; ct.written = true;
                n->op = u->postfix ? (u->op == "++" ? KOp::CPostInc : KOp::CPostDec)
                                   : (u->op == "++" ? KOp::CPreInc : KOp::CPreDec);
                if (!u->postfix) prefix = true;
                L->usesInc = true;
                t = KT::Int;
                return n;
            }
            if (u->op == "++" || u->op == "--") {
                if (!L) return nullptr;
                if (u->operand->kind != NK::VarExpr) return refuse("++/-- on something not a variable");
                KNode* tn = expr(k, c, u->operand.get(), false, ot);
                if (!tn) return nullptr;
                if (ot != KT::Int && ot != KT::Num && ot != KT::Exact) return refuse("++/-- on a Str");
                const std::string& nm = static_cast<VarExpr*>(u->operand.get())->name;
                if (const LSym* s = lookup(nm)) { if (s->ro) return refuse("++/-- on a loop variable"); }
                else for (auto& o : L->outer) if (o.name == nm) o.written = true;
                if (!u->postfix) prefix = true;
                L->usesInc = true;
                const bool num = ot == KT::Num, ex = ot == KT::Exact;
                KNode* n = k.node(u->postfix ? (u->op == "++" ? (ex ? KOp::XPostInc : num ? KOp::NPostInc : KOp::PostInc)
                                                              : (ex ? KOp::XPostDec : num ? KOp::NPostDec : KOp::PostDec))
                                             : (u->op == "++" ? (ex ? KOp::XPreInc : num ? KOp::NPreInc : KOp::PreInc)
                                                              : (ex ? KOp::XPreDec : num ? KOp::NPreDec : KOp::PreDec)));
                n->slot = tn->slot; t = ot;
                return n;
            }
            if (u->postfix) return nullptr;
            if (u->op == "-") {
                KNode* a = expr(k, c, u->operand.get(), false, ot);
                if (!a || (ot != KT::Int && ot != KT::Num && ot != KT::Exact)) return nullptr;
                prefix = true;
                KNode* n = k.node(ot == KT::Num ? KOp::NNeg : ot == KT::Exact ? KOp::XNeg : KOp::Neg); n->a = a; t = ot;
                return n;
            }
            if ((u->op == "!" || u->op == "not") && cond) {
                KNode* a = cexpr(k, c, u->operand.get(), ot);
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
            else if (op == "/" && L && !L->sub) kop = KOp::NDiv;   // a Num's (two Ints make a Rat)
            else if (op == "**") kop = KOp::Pow;
            else if (op == "min") kop = KOp::Min;
            else if (op == "max") kop = KOp::Max;
            else if (op == "div") kop = KOp::Div;
            // `mod` is `%` over Ints (floored, the divisor's sign) — Rakudo-checked
            // with negatives and past int64; a zero divisor bails either way, and
            // the generic path then throws for `mod` where `%` fails softly.
            // It was missing, so time-parts' loop never became a kernel.
            else if (op == "%" || op == "mod") kop = KOp::Mod;
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
            KNode* l = logical ? cexpr(k, c, b->lhs.get(), lt) : expr(k, c, b->lhs.get(), false, lt);
            KNode* r = l ? (logical ? cexpr(k, c, b->rhs.get(), rt) : expr(k, c, b->rhs.get(), false, rt)) : nullptr;
            if (!r) return nullptr;
            bool num = false;
            if (strop) {
                // two Strs; an Int or Num operand takes its text, as the generic path's does
                l = asStr(k, l, lt); r = asStr(k, r, rt);
                if (!l || !r) return refuse("string operator " + op + " on a non-Str");
            }
            // a condition's && / || take an Int (true when it is not zero) or a Bool
            else if (logical) {
                if (lt == KT::Str || rt == KT::Str || lt == KT::Void || rt == KT::Void)
                    return refuse("a Str as a condition");
            }
            // arithmetic and comparison take Ints, or Nums: an Int meeting a
            // Num is converted, as the generic path converts it
            else if (lt == KT::Int && rt == KT::Int && kop != KOp::NDiv) {}
            // exact: an Int or a Rat on either side (two Ints under `/` make a
            // Rat), computed exactly; a Num meeting a Rat is left alone
            else if ((lt == KT::Exact || lt == KT::Int) && (rt == KT::Exact || rt == KT::Int) && L && !L->sub &&
                     kHasExact) {
                switch (kop) {
                    case KOp::Add: kop = KOp::XAdd; break;
                    case KOp::Sub: kop = KOp::XSub; break;
                    case KOp::Mul: kop = KOp::XMul; break;
                    case KOp::NDiv: kop = KOp::XDiv; break;
                    case KOp::Lt: kop = KOp::XLt; break;
                    case KOp::Le: kop = KOp::XLe; break;
                    case KOp::Gt: kop = KOp::XGt; break;
                    case KOp::Ge: kop = KOp::XGe; break;
                    case KOp::Eq: kop = KOp::XEq; break;
                    case KOp::Ne: kop = KOp::XNe; break;
                    default: return refuse("operator " + op + " on a Rat");
                }
                l = asExact(k, l, lt); r = asExact(k, r, rt);
                noteOp(op);
                KNode* n = k.node(kop); n->a = l; n->b = r;
                t = compare ? KT::Bool : KT::Exact;
                return n;
            }
            else if ((lt == KT::Num || lt == KT::Int) && (rt == KT::Num || rt == KT::Int) && L && !L->sub) {
                switch (kop) {
                    case KOp::Add: kop = KOp::NAdd; break;
                    case KOp::Sub: kop = KOp::NSub; break;
                    case KOp::Mul: kop = KOp::NMul; break;
                    case KOp::NDiv: break;
                    case KOp::Pow: kop = KOp::NPow; break;
                    case KOp::Lt: kop = KOp::NLt; break;
                    case KOp::Le: kop = KOp::NLe; break;
                    case KOp::Gt: kop = KOp::NGt; break;
                    case KOp::Ge: kop = KOp::NGe; break;
                    case KOp::Eq: kop = KOp::NEq; break;
                    case KOp::Ne: kop = KOp::NNe; break;
                    default: return refuse("operator " + op + " on a Num");
                }
                l = asNum(k, l, lt); r = asNum(k, r, rt);
                num = true;
            }
            else return refuse("operator " + op + " on a non-Int");
            noteOp(op);
            KNode* n = k.node(kop); n->a = l; n->b = r;
            if (kop == KOp::SCat) { n->slot = L->nstr++; t = KT::Str; }   // the result's temporary
            else t = (compare || logical) ? KT::Bool : num ? KT::Num : KT::Int;
            return n;
        }
        case NK::Ternary: {
            auto* tn = static_cast<Ternary*>(e);
            KT ct, at, bt;
            KNode* cn = cexpr(k, c, tn->cond.get(), ct);
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
            if (!a->userOp && !a->containerSigil && a->target && a->target->kind == NK::Index) {
                // an element store, in place (logged first: see contSlot)
                KT et, vt;
                KNode* n = contRef(k, c, static_cast<Index*>(a->target.get()), KOp::CSet, et);
                if (!n) return nullptr;
                const int ci = (int)n->lit;
                KNode* v = expr(k, c, a->value.get(), false, vt);
                if (!v) return nullptr;
                LCont& ct = L->conts[ci];
                KOp op;
                if (a->op == "=") {
                    if (vt != KT::Int && vt != KT::Str) return refuse("an element store of this type");
                    if (ct.elem != KT::Void && ct.elem != vt) return refuse("a store that would change a container's element type");
                    ct.elem = vt;
                    op = vt == KT::Str ? KOp::SCSet : KOp::CSet;
                }
                else if ((a->op == "+=" || a->op == "-=" || a->op == "*=") && vt == KT::Int && ct.elem != KT::Str) {
                    ct.elem = KT::Int;
                    op = a->op[0] == '+' ? KOp::CAddTo : a->op[0] == '-' ? KOp::CSubTo : KOp::CMulTo;
                    noteOp(a->op.substr(0, 1));
                }
                else if (a->op == "~=" && (vt == KT::Str || vt == KT::Int) && ct.elem != KT::Int) {
                    ct.elem = KT::Str;
                    v = asStr(k, v, vt);
                    op = KOp::CSApp;
                    noteOp("~");
                }
                else return refuse("element assignment " + a->op + " on these types");
                ct.written = true;
                n->op = op; n->b = v;
                t = ct.elem == KT::Str ? KT::Void : KT::Int;
                return n;
            }
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
                if (vt != KT::Int && vt != KT::Str && vt != KT::Num && vt != KT::Exact) return refuse("a declaration of a Bool");
                for (auto& s : L->scopes.back()) if (s.name == tv->name) return refuse("a redeclaration");
                if (vt == KT::Int && L->promoted(tv->name)) { v = asExact(k, v, vt); vt = KT::Exact; }
                const int slot = vt == KT::Str ? L->nstr++ : vt == KT::Exact ? exactSlot() : L->nint++;
                L->scopes.back().push_back({tv->name, vt, slot, false});
                KNode* n = k.node(vt == KT::Str ? KOp::SSet : vt == KT::Exact ? KOp::XSet : KOp::Set);
                n->slot = slot; n->a = v;
                t = vt == KT::Str ? KT::Void : vt;
                return n;
            }
            KT tt;
            KNode* tn = expr(k, c, tv, false, tt);
            if (!tn) return nullptr;
            KNode* v = expr(k, c, a->value.get(), false, vt);
            if (!v) return nullptr;
            KOp kop;
            // an Int variable that is given a Rat (`$t += 0.5`) is an exact
            // slot: compiled again with it promoted
            if (tt == KT::Int && vt == KT::Exact &&
                (a->op == "=" || a->op == "+=" || a->op == "-=" || a->op == "*=" || a->op == "/=")) {
                if (const LSym* s = lookup(tv->name); s && s->ro) return refuse("an assignment to a loop variable");
                if (!L->promoted(tv->name)) L->promote.push_back(tv->name);
                L->again = true;
                return refuse("an Int variable given a Rat (compiled again)");
            }
            if (tt == KT::Int && vt == KT::Int && a->op == "/=") {
                if (const LSym* s = lookup(tv->name); s && s->ro) return refuse("an assignment to a loop variable");
                if (!L->promoted(tv->name)) L->promote.push_back(tv->name);
                L->again = true;
                return refuse("an Int variable divided (compiled again)");
            }
            if (a->op == "=") {
                if (vt != tt && !(tt == KT::Exact && vt == KT::Int)) return refuse("an assignment that would change a variable's type");
                if (tt == KT::Exact) v = asExact(k, v, vt);
                kop = tt == KT::Str ? KOp::SSet : tt == KT::Exact ? KOp::XSet : KOp::Set;
            }
            else if (a->op == "~=" && tt == KT::Str && (vt == KT::Str || vt == KT::Int)) {
                v = asStr(k, v, vt);
                kop = KOp::SApp; noteOp("~");
            }
            else if ((a->op == "+=" || a->op == "-=" || a->op == "*=") && tt == KT::Int && vt == KT::Int) {
                kop = a->op[0] == '+' ? KOp::AddTo : a->op[0] == '-' ? KOp::SubTo : KOp::MulTo;
                noteOp(a->op.substr(0, 1));
            }
            // a Num variable takes a Num or an Int (converted); an Int
            // variable never takes a Num (it would change type)
            else if ((a->op == "+=" || a->op == "-=" || a->op == "*=" || a->op == "/=") && tt == KT::Exact &&
                     (vt == KT::Exact || vt == KT::Int)) {
                kop = a->op[0] == '+' ? KOp::XAddTo : a->op[0] == '-' ? KOp::XSubTo
                    : a->op[0] == '*' ? KOp::XMulTo : KOp::XDivTo;
                v = asExact(k, v, vt);
                noteOp(a->op.substr(0, 1));
            }
            else if ((a->op == "+=" || a->op == "-=" || a->op == "*=" || a->op == "/=") && tt == KT::Num &&
                     (vt == KT::Num || vt == KT::Int)) {
                kop = a->op[0] == '+' ? KOp::NAddTo : a->op[0] == '-' ? KOp::NSubTo
                    : a->op[0] == '*' ? KOp::NMulTo : KOp::NDivTo;
                v = asNum(k, v, vt);
                noteOp(a->op.substr(0, 1));
            }
            else return refuse("assignment operator " + a->op + " on these types");
            if (const LSym* s = lookup(tv->name)) { if (s->ro) return refuse("an assignment to a loop variable"); }
            else for (auto& o : L->outer) if (o.name == tv->name) o.written = true;
            KNode* n = k.node(kop); n->slot = tn->slot; n->a = v;
            t = tt == KT::Str ? KT::Void : tt;
            return n;
        }
        case NK::MethodCall: {
            // A few pure methods of an Int, a Num or a Str, each answered as
            // the generic path's own code answers it, and bailing where that
            // would fail or where its answer needs more than ASCII. Entered
            // only while no `augment` has touched a built-in type, which is
            // how a program could give Str its own.
            auto* m = static_cast<MethodCall*>(e);
            if (!L || L->sub || m->methodExpr || m->maybe || m->allMode || m->bang || m->mutate || m->hyper ||
                m->meta || !m->methodQual.empty() || !m->inv)
                return refuse("a method call");
            const std::string& mn = m->method;
            const size_t na = m->args.size();
            if (m->inv->kind == NK::VarExpr) {
                // `@a.elems`, `%h.elems`, `@a.push(EXPR)` on a container in place
                auto* iv = static_cast<VarExpr*>(m->inv.get());
                if (!iv->declare && !iv->name.empty() && (iv->name[0] == '@' || iv->name[0] == '%')) {
                    const bool hash = iv->name[0] == '%';
                    std::string why;
                    const int ci = cont(iv->name, hash, why);
                    if (ci < 0) return refuse(why);
                    L->usesMethods = true;
                    if (mn == "elems" && !na) {
                        KNode* n = k.node(KOp::CElems); n->lit = ci; n->nargs = hash ? kCHash : 0;
                        t = KT::Int;
                        return n;
                    }
                    if (mn == "push" && !hash && na == 1 && m->args[0] && m->args[0]->kind != NK::Pair &&
                        !(m->args[0]->kind == NK::Unary && static_cast<Unary*>(m->args[0].get())->op == "|")) {
                        KT vt;
                        KNode* v = expr(k, c, m->args[0].get(), false, vt);
                        if (!v) return nullptr;
                        LCont& ct = L->conts[ci];
                        if (vt != KT::Int && vt != KT::Str) return refuse(".push of this type");
                        if (ct.elem != KT::Void && ct.elem != vt) return refuse("a push that would change a container's element type");
                        ct.elem = vt; ct.written = true;
                        KNode* n = k.node(vt == KT::Str ? KOp::SCPush : KOp::CPush); n->lit = ci; n->a = v;
                        t = KT::Void;
                        return n;
                    }
                    return refuse("method ." + mn + " of a container");
                }
            }
            KT it;
            KNode* inv = expr(k, c, m->inv.get(), false, it);
            if (!inv) return nullptr;
            KNode* n = nullptr;
            if (mn == "chars" && !na) {
                // the grapheme count (an Int's digits are ASCII)
                if (it == KT::Num || it == KT::Exact || !(inv = asStr(k, inv, it)))
                    return refuse(".chars of something neither an Int nor a Str");
                n = k.node(KOp::Chars); n->a = inv; t = KT::Int;
            }
            else if ((mn == "abs" || mn == "sign") && !na && it == KT::Int) {
                n = k.node(mn == "abs" ? KOp::Abs : KOp::Sign); n->a = inv; t = KT::Int;
            }
            else if (mn == "Str" && !na && (it == KT::Int || it == KT::Num || it == KT::Str || it == KT::Exact)) {
                n = asStr(k, inv, it); t = KT::Str;
            }
            else if (mn == "Num" && !na && (it == KT::Int || it == KT::Num)) {
                n = asNum(k, inv, it); t = KT::Num;
            }
            else if (mn == "Int" && !na && it == KT::Int) { n = inv; t = KT::Int; }
            else if ((mn == "numerator" || mn == "denominator") && !na && (it == KT::Exact || it == KT::Int)) {
                n = k.node(mn == "numerator" ? KOp::XNumer : KOp::XDenom); n->a = asExact(k, inv, it); t = KT::Int;
            }
            else if (mn == "ord" && !na && it == KT::Str) { n = k.node(KOp::Ord); n->a = inv; t = KT::Int; }
            else if ((mn == "uc" || mn == "lc") && !na && it == KT::Str) {
                n = k.node(mn == "uc" ? KOp::Uc : KOp::Lc); n->a = inv; n->slot = L->nstr++; t = KT::Str;
            }
            else if (mn == "substr" && (na == 1 || na == 2) && it == KT::Str) {
                // `.substr(FROM)` / `.substr(FROM, CHARS)` with Int arguments
                KNode* ab[2] = {nullptr, nullptr};
                for (size_t i = 0; i < na; i++) {
                    Expr* ae = m->args[i].get();
                    if (!ae || ae->kind == NK::Pair) return refuse(".substr with a named argument");
                    KT at;
                    if (!(ab[i] = expr(k, c, ae, false, at))) return nullptr;
                    if (at != KT::Int) return refuse(".substr with a non-Int argument");
                }
                n = k.node(KOp::Substr); n->a = inv; n->b = ab[0]; n->c = ab[1]; n->slot = L->nstr++;
                t = KT::Str;
            }
            else return refuse("method ." + mn + " of this kind");
            L->usesMethods = true;
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
                KNode* cn = cexpr(k, &c, br.first.get(), ct);
                if (!cn || ct == KT::Str || ct == KT::Void) return nullptr;
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
            KNode* cn = cexpr(k, nullptr, w->cond.get(), ct);
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
        KNode* cn = cexpr(k, nullptr, br.first.get(), ct);
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
// How to take back one write to a container of the program, logged before
// the write: a bail replays the log backwards, which leaves every container
// exactly as it was, and the loop then runs the ordinary way from the start.
struct CUndo {
    enum Kind : uint8_t { Elem, Size, HSet, HNew } kind;
    uint16_t ci;          // the container (KConts::conts)
    void* c;              // Elem, Size: the ValueList; HSet: the element (a hash entry never moves
                          // while the hash lives, ValueHash's contract); HNew: the ValueHash
    size_t idx;           // Elem: the element; Size: the length before
    std::string key;      // HSet, HNew
    Value old;            // Elem, HSet: what the element held
};

struct KRun {
    char* stackFloor;     // below this address the generic path would refuse
    int depthLeft;        // …or past this many more frames
    bool bail = false;
    bool ret = false;
    bool next = false;
    bool last = false;
    bool succeed = false;         // a `when` matched: leave its `given`
    std::string* sfr = nullptr;   // a loop kernel's string slots
    struct KConts* cc = nullptr;  // the containers a loop kernel uses in place (one pointer: the
                                  // layout of this struct moves the scalar loops' timing)
};
// The containers a loop kernel uses in place, and how to take its writes to
// them back. The log is bounded by the containers, not by the iterations: once
// it outgrows the containers it covers, each of them is saved as it was before
// the loop (a copy with its log taken back on the copy) and logs no more.
struct KConts {
    Value* const* conts = nullptr;
    size_t n = 0;
    std::vector<CUndo> undo;
    size_t check = 1024;          // the log length at which compacting is next considered
    std::vector<uint8_t> logged;  // per container: has entries in the log
    std::vector<std::unique_ptr<ValueList>> savedList;   // per container: its state before the loop
    std::vector<std::unique_ptr<ValueHash>> savedHash;
    std::string key;              // a hash key being built
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
    else if constexpr (OP == KOp::Pow) {
        // a negative exponent makes a Rat, and a result past int64 a BigInt
        if (pow_ovf(x, y, &z)) R.bail = true;
        return z;
    }
    else if constexpr (OP == KOp::Min) return x < y ? x : y;
    else if constexpr (OP == KOp::Max) return x > y ? x : y;
    else if constexpr (OP == KOp::Lt) return x < y;
    else if constexpr (OP == KOp::Le) return x <= y;
    else if constexpr (OP == KOp::Gt) return x > y;
    else if constexpr (OP == KOp::Ge) return x >= y;
    else if constexpr (OP == KOp::Eq) return x == y;
    else return x != y;
}

// The call path of a recursive integer kernel (fib) starts on cache lines of
// its own, so that code added elsewhere in this file does not move its timing
// (it moved fib by up to 5% either way). Not binFn: aligning every one of its
// instances cost the loop kernels 8% (mainwhen).
#if defined(__GNUC__) || defined(__clang__)
#define KHOT __attribute__((aligned(64)))
#else
#define KHOT
#endif

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
KHOT int64_t condFn(const KNode* n, int64_t* fr, KRun& R) {
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
KHOT int64_t callFn(const KNode* n, int64_t* fr, KRun& R) {
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
    const KOp ao = n->a->op;
    if (ao == KOp::SCat || ao == KOp::SInt || ao == KOp::SNum || ao == KOp::Uc || ao == KOp::Lc || ao == KOp::Substr)
        std::swap(dst, R.sfr[n->a->slot]);
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

// Nums. Each operation is the one numFastArith (InterpreterCore.cpp) and
// applyArith's double arm perform; where those decline or fail — a zero
// divisor, a power that underflows — the kernel bails.
template <KOp OP>
[[gnu::always_inline]] inline int64_t napply(double x, double y, KRun& R) {
    if constexpr (OP == KOp::NAdd) return dbits(x + y);
    else if constexpr (OP == KOp::NSub) return dbits(x - y);
    else if constexpr (OP == KOp::NMul) return dbits(x * y);
    else if constexpr (OP == KOp::NDiv) {
        if (y == 0.0) { R.bail = true; return 0; }
        return dbits(x / y);
    }
    else if constexpr (OP == KOp::NPow) {
        const double r = std::pow(x, y);
        if (r == 0 && x != 0 && !std::isnan(x) && std::isfinite(y)) { R.bail = true; return 0; }
        return dbits(r);
    }
    else if constexpr (OP == KOp::NLt) return x < y;
    else if constexpr (OP == KOp::NLe) return x <= y;
    else if constexpr (OP == KOp::NGt) return x > y;
    else if constexpr (OP == KOp::NGe) return x >= y;
    else if constexpr (OP == KOp::NEq) return x == y;
    else return x != y;
}
template <KOp OP, int L, int Rk>
int64_t nbinFn(const KNode* n, int64_t* fr, KRun& R) {
    const double x = bitsd(leaf<L>(n->a, fr, R));
    return napply<OP>(x, bitsd(leaf<Rk>(n->b, fr, R)), R);
}
template <KOp OP>
KFn nbinPick(int l, int r) {
    static const KFn T[3][3] = {
        {nbinFn<OP, kP, kP>, nbinFn<OP, kP, kL>, nbinFn<OP, kP, kX>},
        {nbinFn<OP, kL, kP>, nbinFn<OP, kL, kL>, nbinFn<OP, kL, kX>},
        {nbinFn<OP, kX, kP>, nbinFn<OP, kX, kL>, nbinFn<OP, kX, kX>}};
    return T[l][r];
}
template <KOp OP, int K>
int64_t nstoreFn(const KNode* n, int64_t* fr, KRun& R) {
    const double v = bitsd(leaf<K>(n->a, fr, R));
    constexpr KOp B = OP == KOp::NAddTo ? KOp::NAdd : OP == KOp::NSubTo ? KOp::NSub
                    : OP == KOp::NMulTo ? KOp::NMul : KOp::NDiv;
    const int64_t r = napply<B>(bitsd(fr[n->slot]), v, R);
    if (R.bail) return 0;
    return fr[n->slot] = r;
}
template <KOp OP>
KFn nstorePick(int k) {
    static const KFn T[3] = {nstoreFn<OP, kP>, nstoreFn<OP, kL>, nstoreFn<OP, kX>};
    return T[k];
}
template <int D, bool POST>
int64_t nstepFn(const KNode* n, int64_t* fr, KRun&) {
    const int64_t old = fr[n->slot];
    const int64_t z = dbits(bitsd(old) + (D > 0 ? 1.0 : -1.0));
    fr[n->slot] = z;
    return POST ? old : z;
}
int64_t nnegFn(const KNode* n, int64_t* fr, KRun& R) { return dbits(-bitsd(krun(n->a, fr, R))); }
int64_t nofintFn(const KNode* n, int64_t* fr, KRun& R) { return dbits((double)krun(n->a, fr, R)); }
int64_t ntruthFn(const KNode* n, int64_t* fr, KRun& R) { return bitsd(krun(n->a, fr, R)) != 0.0; }
const std::string& snumFn(const KNode* n, int64_t* fr, KRun& R) {
    std::string& out = R.sfr[n->slot];
    out = Value::number(bitsd(krun(n->a, fr, R))).toStr();
    return out;
}

// Pure methods.
int64_t absFn(const KNode* n, int64_t* fr, KRun& R) {
    const int64_t x = krun(n->a, fr, R);
    if (x == LLONG_MIN) { R.bail = true; return 0; }
    return x < 0 ? -x : x;
}
int64_t signFn(const KNode* n, int64_t* fr, KRun& R) {
    const int64_t x = krun(n->a, fr, R);
    return (x > 0) - (x < 0);
}
// `.ord` of an ASCII first character; an empty string's is Nil
int64_t ordFn(const KNode* n, int64_t* fr, KRun& R) {
    const std::string& s = srun(n->a, fr, R);
    if (s.empty() || (unsigned char)s[0] >= 0x80) { R.bail = true; return 0; }
    return (unsigned char)s[0];
}
template <bool UP>
const std::string& caseFn(const KNode* n, int64_t* fr, KRun& R) {
    std::string& out = R.sfr[n->slot];
    const std::string& s = srun(n->a, fr, R);
    if (!allAscii(s)) { R.bail = true; return out; }
    if (&s != &out) out = s;
    for (char& ch : out) ch = UP ? (char)ascii::toupper((unsigned char)ch) : (char)ascii::tolower((unsigned char)ch);
    return out;
}
// `.substr(FROM)` / `.substr(FROM, CHARS)` of an ASCII string; a start
// outside it or a negative length is a Failure, so it bails
const std::string& substrFn(const KNode* n, int64_t* fr, KRun& R) {
    std::string& out = R.sfr[n->slot];
    const std::string& s = srun(n->a, fr, R);
    const int64_t from = krun(n->b, fr, R);
    const int64_t len = n->c ? krun(n->c, fr, R) : INT64_MAX;
    if (R.bail || !allAscii(s) || from < 0 || from > (int64_t)s.size() || len < 0) { R.bail = true; return out; }
    const size_t cnt = (size_t)std::min<int64_t>(len, (int64_t)s.size() - from);
    if (&s == &out) out = s.substr((size_t)from, cnt);
    else out.assign(s, (size_t)from, cnt);
    return out;
}

// Exact numbers: what applyArith's exact tower answers for an Int and a Rat
// whose parts fit an int64 — a Rat reduced, with a positive denominator, and
// still a Rat when that denominator is 1. Products are taken in 128 bits; a
// result whose parts leave int64 bails, as does a zero divisor, and the
// generic path decides (a BigInt Rat, or the Num a large denominator spills to).
#if RAKUPP_HAS_INT128
[[gnu::noinline]] int64_t xratio(__int128 n, __int128 d, int64_t* fr, KRun& R) {
    if (d == 0) { R.bail = true; return 0; }
    if (d < 0) { n = -n; d = -d; }
    unsigned __int128 a = (unsigned __int128)(n < 0 ? -n : n), b = (unsigned __int128)d;
    auto ctz = [](unsigned __int128 x) -> int {
        const unsigned long long lo = (unsigned long long)x;
        return lo ? __builtin_ctzll(lo) : 64 + __builtin_ctzll((unsigned long long)(x >> 64));
    };
    if (a) {
        const int sh = ctz(a | b);
        a >>= ctz(a);
        while (b) {
            b >>= ctz(b);
            if (a > b) { const unsigned __int128 t = a; a = b; b = t; }
            b -= a;
        }
        const unsigned __int128 g = a << sh;
        if (g > 1) { n /= (__int128)g; d /= (__int128)g; }
    }
    else d = 1;   // 0/x is 0/1
    if (n < (__int128)INT64_MIN || n > (__int128)INT64_MAX || d > (__int128)INT64_MAX) { R.bail = true; return 0; }
    fr[kDenSlot] = (int64_t)d;
    return (int64_t)n;
}
// the two operands of an exact operator, each with its denominator (1 for an Int)
struct XPair { int64_t x, xd, y, yd; bool ints; };
[[gnu::always_inline]] inline XPair xoperands(const KNode* n, int64_t* fr, KRun& R) {
    XPair p;
    p.x = krun(n->a, fr, R); p.xd = fr[kDenSlot];
    p.y = krun(n->b, fr, R); p.yd = fr[kDenSlot];
    p.ints = !p.xd && !p.yd;
    if (!p.xd) p.xd = 1;
    if (!p.yd) p.yd = 1;
    return p;
}
template <KOp OP>
[[gnu::always_inline]] inline int64_t xapply(int64_t x, int64_t xd, int64_t y, int64_t yd, bool ints, int64_t* fr, KRun& R) {
    if constexpr (OP == KOp::XAdd || OP == KOp::XSub || OP == KOp::XMul) {
        if (ints) {
            long long z = 0;
            if (OP == KOp::XAdd ? add_ovf(x, y, &z) : OP == KOp::XSub ? sub_ovf(x, y, &z) : mul_ovf(x, y, &z))
                R.bail = true;
            fr[kDenSlot] = 0;
            return z;
        }
        if constexpr (OP == KOp::XMul) return xratio((__int128)x * y, (__int128)xd * yd, fr, R);
        else {
            const __int128 a = (__int128)x * yd, b = (__int128)y * xd;
            return xratio(OP == KOp::XAdd ? a + b : a - b, (__int128)xd * yd, fr, R);
        }
    }
    else if constexpr (OP == KOp::XDiv) {
        if (y == 0) { R.bail = true; return 0; }
        return xratio((__int128)x * yd, (__int128)xd * y, fr, R);
    }
    else {
        const __int128 a = (__int128)x * yd, b = (__int128)y * xd;
        if constexpr (OP == KOp::XLt) return a < b;
        else if constexpr (OP == KOp::XLe) return a <= b;
        else if constexpr (OP == KOp::XGt) return a > b;
        else if constexpr (OP == KOp::XGe) return a >= b;
        else if constexpr (OP == KOp::XEq) return a == b;
        else return a != b;
    }
}
template <KOp OP>
int64_t xbinFn(const KNode* n, int64_t* fr, KRun& R) {
    const XPair p = xoperands(n, fr, R);
    return xapply<OP>(p.x, p.xd, p.y, p.yd, p.ints, fr, R);
}
template <KOp OP>
int64_t xstoreFn(const KNode* n, int64_t* fr, KRun& R) {
    const int64_t y = krun(n->a, fr, R);
    const int64_t yd = fr[kDenSlot];
    const int64_t xd = fr[n->slot + 1];
    constexpr KOp B = OP == KOp::XAddTo ? KOp::XAdd : OP == KOp::XSubTo ? KOp::XSub
                    : OP == KOp::XMulTo ? KOp::XMul : KOp::XDiv;
    const int64_t r = xapply<B>(fr[n->slot], xd ? xd : 1, y, yd ? yd : 1, !xd && !yd, fr, R);
    if (R.bail) return 0;
    fr[n->slot] = r;
    fr[n->slot + 1] = fr[kDenSlot];
    return r;
}
// `++` / `--`: an Int steps by 1, a Rat by its denominator (and stays reduced)
template <int D, bool POST>
int64_t xstepFn(const KNode* n, int64_t* fr, KRun& R) {
    const int64_t old = fr[n->slot], d = fr[n->slot + 1];
    long long z;
    if (D > 0 ? add_ovf(old, d ? d : 1, &z) : sub_ovf(old, d ? d : 1, &z)) { R.bail = true; return 0; }
    fr[n->slot] = z;
    fr[kDenSlot] = d;
    return POST ? old : z;
}
int64_t xvarFn(const KNode* n, int64_t* fr, KRun& R) { fr[kDenSlot] = fr[n->slot + 1]; return fr[n->slot]; }
int64_t xlitFn(const KNode* n, int64_t* fr, KRun&) { fr[kDenSlot] = n->den; return n->lit; }
int64_t xofintFn(const KNode* n, int64_t* fr, KRun& R) { const int64_t v = krun(n->a, fr, R); fr[kDenSlot] = 0; return v; }
int64_t xnegFn(const KNode* n, int64_t* fr, KRun& R) {
    const int64_t v = krun(n->a, fr, R);
    if (v == INT64_MIN) { R.bail = true; return 0; }
    return -v;   // the operand's denominator stays in fr[kDenSlot]
}
int64_t xsetFn(const KNode* n, int64_t* fr, KRun& R) {
    const int64_t v = krun(n->a, fr, R);
    fr[n->slot] = v;
    fr[n->slot + 1] = fr[kDenSlot];
    return v;
}
int64_t xtruthFn(const KNode* n, int64_t* fr, KRun& R) { return krun(n->a, fr, R) != 0; }
int64_t xnumerFn(const KNode* n, int64_t* fr, KRun& R) { return krun(n->a, fr, R); }
int64_t xdenomFn(const KNode* n, int64_t* fr, KRun& R) { krun(n->a, fr, R); return fr[kDenSlot] ? fr[kDenSlot] : 1; }
Value ratValue(int64_t n, int64_t d) {
    Value v;
    v.t = VT::Rat;
    v.ratNM() = std::make_shared<BigInt>((long long)n);
    v.ratDM() = std::make_shared<BigInt>((long long)d);
    return v;
}
const std::string& sexactFn(const KNode* n, int64_t* fr, KRun& R) {
    std::string& out = R.sfr[n->slot];
    const int64_t v = krun(n->a, fr, R);
    out = fr[kDenSlot] ? ratValue(v, fr[kDenSlot]).toStr() : std::to_string(v);
    return out;
}
#endif

void compactUndo(KConts& cc);
// Log a write to container `ci` — unless it has been saved whole already
inline void logWrite(KConts& cc, int ci, CUndo&& u) {
    if (cc.savedList[ci] || cc.savedHash[ci]) return;
    u.ci = (uint16_t)ci;
    cc.logged[ci] = 1;
    cc.undo.push_back(std::move(u));
    if (cc.undo.size() >= cc.check) compactUndo(cc);
}
// The log has grown: when it is longer than the containers it covers hold,
// save each of them as it was before the loop and stop logging them.
void compactUndo(KConts& cc) {
    size_t total = 0;
    for (size_t i = 0; i < cc.n; i++)
        if (cc.logged[i]) {
            const Value& c = *cc.conts[i];
            total += c.t == VT::Hash ? c.hash()->size() : c.arr()->size();
        }
    if (cc.undo.size() < total) { cc.check = cc.undo.size() * 2; return; }
    for (size_t i = 0; i < cc.n; i++)
        if (cc.logged[i]) {
            const Value& c = *cc.conts[i];
            if (c.t == VT::Hash) cc.savedHash[i].reset(new ValueHash(*c.hash()));
            else cc.savedList[i].reset(new ValueList(*c.arr()));
        }
    for (auto it = cc.undo.rbegin(); it != cc.undo.rend(); ++it) {
        if (auto* l = cc.savedList[it->ci].get()) {
            if (it->kind == CUndo::Elem) (*l)[it->idx] = std::move(it->old);
            else l->resize(it->idx);
        }
        else if (auto* h = cc.savedHash[it->ci].get()) {
            if (it->kind == CUndo::HSet) { auto f = h->find(it->key); if (f != h->end()) f->second = std::move(it->old); }
            else h->erase(it->key);
        }
    }
    cc.undo.clear();
    cc.check = 1024;
}
// A bail's undo: the log replayed backwards, then every saved container put
// back IN PLACE (an element or an entry keeps its identity: a Proxy or a live
// Pair that holds one still reaches it)
void undoContainers(KConts& cc) {
    for (auto it = cc.undo.rbegin(); it != cc.undo.rend(); ++it) {
        switch (it->kind) {
            case CUndo::Elem: (*static_cast<ValueList*>(it->c))[it->idx] = std::move(it->old); break;
            case CUndo::Size: static_cast<ValueList*>(it->c)->resize(it->idx); break;
            case CUndo::HSet: *static_cast<Value*>(it->c) = std::move(it->old); break;
            case CUndo::HNew: static_cast<ValueHash*>(it->c)->erase(it->key); break;
        }
    }
    cc.undo.clear();
    for (size_t i = 0; i < cc.n; i++) {
        if (auto* l = cc.savedList[i].get()) {
            ValueList& live = *cc.conts[i]->arr();
            live.resize(l->size());
            for (size_t j = 0; j < l->size(); j++) live[j] = std::move((*l)[j]);
        }
        else if (auto* h = cc.savedHash[i].get()) {
            ValueHash& live = *cc.conts[i]->hash();
            std::vector<std::string> added;   // (a loop never deletes a key: it can only add)
            for (auto& kv : live) if (h->find(kv.first) == h->end()) added.push_back(kv.first);
            for (auto& k : added) live.erase(k);
            for (auto& kv : *h) { auto f = live.find(kv.first); if (f != live.end()) f->second = kv.second; }
        }
    }
}

// Containers in place. contSlot finds the element a node names: null when it
// is absent (an index past the end, a missing key) and `write` is false, or
// on a bail. A write is logged before anything changes: a key or a stretch of
// array the store creates, then what the element held.
Value* contSlot(const KNode* n, int64_t* fr, KRun& R, bool write) {
    const Value& c = *R.cc->conts[n->lit];
    if (n->nargs & kCHash) {
        ValueHash& h = *c.hash();
        std::string& key = R.cc->key;
        if (n->nargs & kCStrKey) key = srun(n->a, fr, R);
        else {
            // an Int key is its decimal text, as hashSubKey makes it
            char buf[24];
            char* e = buf + sizeof buf;
            char* p = e;
            const int64_t v = krun(n->a, fr, R);
            uint64_t u = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
            do { *--p = char('0' + u % 10); u /= 10; } while (u);
            if (v < 0) *--p = '-';
            key.assign(p, (size_t)(e - p));
        }
        if (R.bail) return nullptr;
        auto it = h.find(key);
        if (it == h.end()) {
            if (!write) return nullptr;
            logWrite(*R.cc, (int)n->lit, {CUndo::HNew, 0, &h, 0, key, Value()});
            Value& slot = h[key];
            slot = Value::any();
            return &slot;
        }
        if (write) logWrite(*R.cc, (int)n->lit, {CUndo::HSet, 0, &it->second, 0, key, it->second});
        return &it->second;
    }
    ValueList& a = *c.arr();
    const int64_t i = krun(n->a, fr, R);
    if (R.bail) return nullptr;
    // a negative index is an error, and a store far past the end is the
    // generic path's to make
    if (i < 0 || (write && (uint64_t)i > a.size() + (size_t(1) << 20))) { R.bail = true; return nullptr; }
    if ((uint64_t)i >= a.size()) {
        if (!write) return nullptr;
        const size_t was = a.size();
        logWrite(*R.cc, (int)n->lit, {CUndo::Size, 0, &a, was, {}, Value()});
        a.resize((size_t)i + 1);
        for (size_t j = was; j <= (size_t)i; j++) a[j] = Value::any();
    }
    if (write) logWrite(*R.cc, (int)n->lit, {CUndo::Elem, 0, &a, (size_t)i, {}, a[(size_t)i]});
    return &a[(size_t)i];
}
// …and the element a store may replace: a plain Int, a plain Str or a hole.
// Anything else (a bound container, a typed value) bails — logged already,
// so the undo puts it back unchanged.
inline Value* contStore(const KNode* n, int64_t* fr, KRun& R) {
    Value* el = contSlot(n, fr, R, true);
    if (el && !plainIntValue(*el) && !plainStrValue(*el) && !plainAnyValue(*el)) { R.bail = true; return nullptr; }
    return el;
}
int64_t cgetFn(const KNode* n, int64_t* fr, KRun& R) {
    const Value* el = contSlot(n, fr, R, false);
    if (!el || !plainIntValue(*el)) { R.bail = true; return 0; }
    return el->i;
}
const std::string& scgetFn(const KNode* n, int64_t* fr, KRun& R) {
    std::string& out = R.sfr[n->slot];
    const Value* el = contSlot(n, fr, R, false);
    if (!el || !plainStrValue(*el)) { R.bail = true; out.clear(); return out; }
    out = el->s.str();
    return out;
}
int64_t celemsFn(const KNode* n, int64_t*, KRun& R) {
    const Value& c = *R.cc->conts[n->lit];
    return (n->nargs & kCHash) ? (int64_t)c.hash()->size() : (int64_t)c.arr()->size();
}
int64_t csetFn(const KNode* n, int64_t* fr, KRun& R) {
    const int64_t v = krun(n->b, fr, R);
    if (R.bail) return 0;
    Value* el = contStore(n, fr, R);
    if (!el) return 0;
    *el = Value::integer(v);
    return v;
}
int64_t scsetFn(const KNode* n, int64_t* fr, KRun& R) {
    const std::string& v = srun(n->b, fr, R);
    if (R.bail) return 0;
    std::string text = v;   // (contSlot builds a hash key over a buffer of its own)
    Value* el = contStore(n, fr, R);
    if (!el) return 0;
    *el = Value::str(std::move(text));
    return 0;
}
// `op=` and ++/-- on an element: a hole or a missing key starts from the
// operator's identity (0, or 1 for `*=`), as the generic path's do
template <KOp OP>
int64_t cmodFn(const KNode* n, int64_t* fr, KRun& R) {
    int64_t v = 0;
    if constexpr (OP == KOp::CAddTo || OP == KOp::CSubTo || OP == KOp::CMulTo) {
        v = krun(n->b, fr, R);
        if (R.bail) return 0;
    }
    Value* el = contStore(n, fr, R);
    if (!el) return 0;
    // (a postfix one answers that identity too: `my $x; $x++` is 0)
    const bool hole = el->t == VT::Any;
    const int64_t old = hole ? (OP == KOp::CMulTo ? 1 : 0) : el->i;
    long long z = 0;
    bool ovf;
    if constexpr (OP == KOp::CAddTo) ovf = add_ovf(old, v, &z);
    else if constexpr (OP == KOp::CSubTo) ovf = sub_ovf(old, v, &z);
    else if constexpr (OP == KOp::CMulTo) ovf = mul_ovf(old, v, &z);
    else if constexpr (OP == KOp::CPreInc || OP == KOp::CPostInc) ovf = add_ovf(old, 1, &z);
    else ovf = sub_ovf(old, 1, &z);
    if (ovf) { R.bail = true; return 0; }
    *el = Value::integer(z);
    return (OP == KOp::CPostInc || OP == KOp::CPostDec) ? old : z;
}
int64_t csappFn(const KNode* n, int64_t* fr, KRun& R) {
    std::string v = srun(n->b, fr, R);
    if (R.bail) return 0;
    Value* el = contStore(n, fr, R);
    if (!el) return 0;
    if (el->t == VT::Any) { *el = Value::str(std::move(v)); return 0; }
    if (el->t != VT::Str) { R.bail = true; return 0; }
    std::string cur = el->s.str();
    if (allAscii(v)) cur += v;
    else cur = nfcNormalize(cur + v);
    *el = Value::str(std::move(cur));
    return 0;
}
int64_t cpushFn(const KNode* n, int64_t* fr, KRun& R) {
    const int64_t v = krun(n->a, fr, R);
    if (R.bail) return 0;
    ValueList& a = *R.cc->conts[n->lit]->arr();
    logWrite(*R.cc, (int)n->lit, {CUndo::Size, 0, &a, a.size(), {}, Value()});
    a.push_back(Value::integer(v));
    return 0;
}
int64_t scpushFn(const KNode* n, int64_t* fr, KRun& R) {
    std::string v = srun(n->a, fr, R);
    if (R.bail) return 0;
    ValueList& a = *R.cc->conts[n->lit]->arr();
    logWrite(*R.cc, (int)n->lit, {CUndo::Size, 0, &a, a.size(), {}, Value()});
    a.push_back(Value::str(std::move(v)));
    return 0;
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
        case KOp::NAdd: n->fn = nbinPick<KOp::NAdd>(l, r); break;
        case KOp::NSub: n->fn = nbinPick<KOp::NSub>(l, r); break;
        case KOp::NMul: n->fn = nbinPick<KOp::NMul>(l, r); break;
        case KOp::NDiv: n->fn = nbinPick<KOp::NDiv>(l, r); break;
        case KOp::NPow: n->fn = nbinFn<KOp::NPow, kX, kX>; break;
        case KOp::NLt: n->fn = nbinPick<KOp::NLt>(l, r); break;
        case KOp::NLe: n->fn = nbinPick<KOp::NLe>(l, r); break;
        case KOp::NGt: n->fn = nbinPick<KOp::NGt>(l, r); break;
        case KOp::NGe: n->fn = nbinPick<KOp::NGe>(l, r); break;
        case KOp::NEq: n->fn = nbinPick<KOp::NEq>(l, r); break;
        case KOp::NNe: n->fn = nbinPick<KOp::NNe>(l, r); break;
        case KOp::NNeg: n->fn = nnegFn; break;
        case KOp::NAddTo: n->fn = nstorePick<KOp::NAddTo>(l); break;
        case KOp::NSubTo: n->fn = nstorePick<KOp::NSubTo>(l); break;
        case KOp::NMulTo: n->fn = nstorePick<KOp::NMulTo>(l); break;
        case KOp::NDivTo: n->fn = nstorePick<KOp::NDivTo>(l); break;
        case KOp::NPreInc: n->fn = nstepFn<1, false>; break;
        case KOp::NPreDec: n->fn = nstepFn<-1, false>; break;
        case KOp::NPostInc: n->fn = nstepFn<1, true>; break;
        case KOp::NPostDec: n->fn = nstepFn<-1, true>; break;
        case KOp::NOfInt: n->fn = nofintFn; break;
        case KOp::NTruth: n->fn = ntruthFn; break;
        case KOp::SNum: n->sfn = snumFn; break;
        case KOp::Pow: n->fn = binFn<KOp::Pow, kX, kX>; break;
        case KOp::Min: n->fn = binPick<KOp::Min>(l, r); break;
        case KOp::Max: n->fn = binPick<KOp::Max>(l, r); break;
        case KOp::Abs: n->fn = absFn; break;
        case KOp::Sign: n->fn = signFn; break;
        case KOp::Ord: n->fn = ordFn; break;
        case KOp::Uc: n->sfn = caseFn<true>; break;
        case KOp::Lc: n->sfn = caseFn<false>; break;
        case KOp::Substr: n->sfn = substrFn; break;
        case KOp::CGet: n->fn = cgetFn; break;
        case KOp::SCGet: n->sfn = scgetFn; break;
        case KOp::CElems: n->fn = celemsFn; break;
        case KOp::CSet: n->fn = csetFn; break;
        case KOp::SCSet: n->fn = scsetFn; break;
        case KOp::CAddTo: n->fn = cmodFn<KOp::CAddTo>; break;
        case KOp::CSubTo: n->fn = cmodFn<KOp::CSubTo>; break;
        case KOp::CMulTo: n->fn = cmodFn<KOp::CMulTo>; break;
        case KOp::CPreInc: n->fn = cmodFn<KOp::CPreInc>; break;
        case KOp::CPreDec: n->fn = cmodFn<KOp::CPreDec>; break;
        case KOp::CPostInc: n->fn = cmodFn<KOp::CPostInc>; break;
        case KOp::CPostDec: n->fn = cmodFn<KOp::CPostDec>; break;
        case KOp::CSApp: n->fn = csappFn; break;
        case KOp::CPush: n->fn = cpushFn; break;
        case KOp::SCPush: n->fn = scpushFn; break;
#if RAKUPP_HAS_INT128
        case KOp::XVar: n->fn = xvarFn; break;
        case KOp::XLit: n->fn = xlitFn; break;
        case KOp::XOfInt: n->fn = xofintFn; break;
        case KOp::XAdd: n->fn = xbinFn<KOp::XAdd>; break;
        case KOp::XSub: n->fn = xbinFn<KOp::XSub>; break;
        case KOp::XMul: n->fn = xbinFn<KOp::XMul>; break;
        case KOp::XDiv: n->fn = xbinFn<KOp::XDiv>; break;
        case KOp::XNeg: n->fn = xnegFn; break;
        case KOp::XLt: n->fn = xbinFn<KOp::XLt>; break;
        case KOp::XLe: n->fn = xbinFn<KOp::XLe>; break;
        case KOp::XGt: n->fn = xbinFn<KOp::XGt>; break;
        case KOp::XGe: n->fn = xbinFn<KOp::XGe>; break;
        case KOp::XEq: n->fn = xbinFn<KOp::XEq>; break;
        case KOp::XNe: n->fn = xbinFn<KOp::XNe>; break;
        case KOp::XSet: n->fn = xsetFn; break;
        case KOp::XAddTo: n->fn = xstoreFn<KOp::XAddTo>; break;
        case KOp::XSubTo: n->fn = xstoreFn<KOp::XSubTo>; break;
        case KOp::XMulTo: n->fn = xstoreFn<KOp::XMulTo>; break;
        case KOp::XDivTo: n->fn = xstoreFn<KOp::XDivTo>; break;
        case KOp::XPreInc: n->fn = xstepFn<1, false>; break;
        case KOp::XPreDec: n->fn = xstepFn<-1, false>; break;
        case KOp::XPostInc: n->fn = xstepFn<1, true>; break;
        case KOp::XPostDec: n->fn = xstepFn<-1, true>; break;
        case KOp::XTruth: n->fn = xtruthFn; break;
        case KOp::SExact: n->sfn = sexactFn; break;
        case KOp::XNumer: n->fn = xnumerFn; break;
        case KOp::XDenom: n->fn = xdenomFn; break;
#else
        default: break;   // never built: no exact numbers without 128-bit integers
#endif
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
    std::vector<LCont> conts;  // the program's containers it uses in place
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
            LKernel* lk2 = nullptr;
            LoopCx cx;
            Compiler cc;
            KNode* body = nullptr;
            KNode* root = nullptr;
            // A second compile (or more) when the first finds an Int variable
            // that is given a Rat: it then holds an exact slot from the start.
            std::vector<std::string> promote;
            for (int attempt = 0;; attempt++) {
                lk2 = new LKernel;
                cx = LoopCx{};
                cx.promote = promote;
                cx.env = env;
                cx.nint = 3;   // a `for`'s bounds and loop variable
                cx.loops = 1;
                cc = Compiler{};
                cc.global = global_.get();
                cc.L = &cx;
                body = root = nullptr;
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
                        KNode* cn = cc.cexpr(lk2->k, nullptr, ws->cond.get(), ct);
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
                            cn = cc.cexpr(lk2->k, nullptr, ls->cond.get(), ct);
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
                if (body || !cx.again || attempt >= 4) break;
                promote = cx.promote;
                finishSession(cc, false, nullptr);
                delete lk2;
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
            lk2->conts = std::move(cx.conts);
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
        if (o.t == KT::Int ? !plainIntValue(*cell) : o.t == KT::Num ? !plainNumValue(*cell)
            : o.t == KT::Exact ? !(plainIntValue(*cell) || plainRatValue(*cell)) : !plainStrValue(*cell))
            return notEntered("a value of another type in", o.name);
        if (o.written && !writableCell(*cell, owner, o.name))
            return notEntered("a container a plain store would not honour:", o.name);
        for (size_t j = 0; j < i; j++)
            if (cells[j] == cell) return notEntered("two names for one container:", o.name);
        cells[i] = cell;
    }
    // …and its containers: still plain ones (two names for one container are
    // fine here — every write goes to the container itself)
    const size_t nc = L->conts.size();
    Value* stackConts[8];
    std::unique_ptr<Value*[]> heapConts;
    Value** conts = nc <= 8 ? stackConts : (heapConts.reset(new Value*[nc]), heapConts.get());
    for (size_t i = 0; i < nc; i++) {
        Env* owner = nullptr;
        Value* cell = outerCell(env, L->conts[i].name, &owner);
        if (!cell) return notEntered("no container", L->conts[i].name);
        if (!plainContainer(*cell, L->conts[i].hash))
            return notEntered("a container of another kind:", L->conts[i].name);
        conts[i] = cell;
    }
    int64_t stackInts[33];   // kDenSlot first
    std::unique_ptr<int64_t[]> heapInts;
    int64_t* fr = 1 + (L->nint <= 32 ? stackInts : (heapInts.reset(new int64_t[L->nint + 1]), heapInts.get()));
    std::unique_ptr<std::string[]> strs(L->nstr ? new std::string[L->nstr] : nullptr);
    fr[kLoSlot] = lo;
    fr[kHiSlot] = hi;
    for (size_t i = 0; i < no; i++) {
        const LOuter& o = L->outer[i];
        if (o.t == KT::Int) fr[o.slot] = cells[i]->i;
        else if (o.t == KT::Num) fr[o.slot] = dbits(cells[i]->n);
        else if (o.t == KT::Exact) {
            if (cells[i]->t == VT::Int) { fr[o.slot] = cells[i]->i; fr[o.slot + 1] = 0; }
            else { fr[o.slot] = cells[i]->ratN()->toLL(); fr[o.slot + 1] = cells[i]->ratD()->toLL(); }
        }
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
    KConts kc;
    kc.conts = conts;
    kc.n = nc;
    if (nc) { kc.logged.assign(nc, 0); kc.savedList.resize(nc); kc.savedHash.resize(nc); }
    R.cc = &kc;
    krun(L->k.body, fr, R);
    if (R.bail) {
        undoContainers(kc);
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
        else if (o.t == KT::Num) cells[i]->n = bitsd(fr[o.slot]);
#if RAKUPP_HAS_INT128
        else if (o.t == KT::Exact) {
            // an Int stays in place; anything else is a new value in the plain
            // container (an Int that became a Rat, or the reverse)
            if (!fr[o.slot + 1] && cells[i]->t == VT::Int) cells[i]->i = fr[o.slot];
            else *cells[i] = fr[o.slot + 1] ? ratValue(fr[o.slot], fr[o.slot + 1]) : Value::integer(fr[o.slot]);
        }
#endif
        else cells[i]->s = std::move(strs[o.slot]);
    }
    return true;
}

}  // namespace rakupp
