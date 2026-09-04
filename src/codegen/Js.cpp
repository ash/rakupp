// The JavaScript backend. See Js.h and docs/dev/plans/TRANSPILE-PLAN.md.
//
// Shape of the output: one function body handed to R.main — the program's
// subs as hoisted function declarations, its `my` variables as `let`s, its
// classes as R.defClass calls — so a reader sees their program. Every value
// operation is a call into the runtime (R.add, R.mc, ...); control flow is
// JavaScript's own wherever it is lexically local (labelled break/continue,
// try/finally), and a thrown control object only where it crosses a closure.
//
// Refusals: anything outside the core throws CodegenError with the line, and
// main.cpp reports it in the `--cpp` shape. The runtime's builtin and method
// tables are what the emitter knows (kBuiltins below); t/js/run.raku checks
// the two agree.
#include "Js.h"
#include "../AsciiCtype.h"
#include "../Lexer.h"
#include "../Parser.h"
#include "../Regex.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <functional>
#include <map>
#include <sstream>
#include <vector>

namespace rakupp {
std::vector<std::string> computePlaceholders(const std::vector<StmtPtr>& body); // Interpreter.cpp

namespace {
using std::string;

// ---------------------------------------------------------------- helpers ----
[[noreturn]] void refuse(const string& what, int line) {
    throw CodegenError{what + (line > 0 ? " (line " + std::to_string(line) + ")" : "")};
}
string jsStr(const string& s) {
    string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '"': o += "\\\""; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case 0: o += "\\0"; break;
            default:
                if (c < 0x20 || c == 0x7f) { char b[8]; std::snprintf(b, sizeof b, "\\x%02x", c); o += b; }
                else o += (char)c;
        }
    }
    // U+2028/2029 are line terminators inside a JS string literal (pre-ES2019 hosts): escape their UTF-8
    string r; for (size_t i = 0; i < o.size(); i++) {
        if ((unsigned char)o[i] == 0xe2 && i + 2 < o.size() && (unsigned char)o[i+1] == 0x80 && ((unsigned char)o[i+2] == 0xa8 || (unsigned char)o[i+2] == 0xa9)) {
            r += (unsigned char)o[i+2] == 0xa8 ? "\\u2028" : "\\u2029"; i += 2;
        } else r += o[i];
    }
    return r + "\"";
}
// The C++ backend's injective byte encoding (Codegen.cpp): alnum passes, every
// other byte becomes _HH, so `$a-b` and `$a_b` cannot collide.
string mangleBody(const string& s) {
    string o;
    for (unsigned char c : s) {
        if (ascii::isalnum(c)) o += (char)c;
        else { char b[4]; std::snprintf(b, sizeof b, "_%02x", c); o += b; }
    }
    return o;
}
string mangleVar(const string& name) {
    if (name == "$/") return "v__slash";
    if (name == "$!") return "v__bang";
    if (name.size() <= 1 || name.find('\x01') != string::npos) return "_anon";   // `my $;` — declared with `var` (redeclarable), never referenced
    char sigil = (!name.empty() && (name[0] == '$' || name[0] == '@' || name[0] == '%' || name[0] == '&')) ? name[0] : 0;
    string body = sigil ? name.substr(1) : name;
    const char* pre = sigil == '@' ? "a_" : sigil == '%' ? "h_" : sigil == '&' ? "c_" : "v_";
    return pre + mangleBody(body);
}
string mangleSub(const string& name) { return "u_" + mangleBody(name); }
string mangleType(const string& name) { return "c_" + mangleBody(name); }
string nkName(NK k) {
    switch (k) {
        case NK::RegexLit: return "a regex literal"; case NK::SubstLit: return "a substitution";
        case NK::SymbolicRef: return "a symbolic reference ::(…)"; case NK::NqpOp: return "an nqp:: op";
        case NK::NamedRegexDecl: return "a named regex declaration"; case NK::SubsetDecl: return "a subset declaration";
        case NK::ClassDecl: return "a class declaration"; case NK::EnumDecl: return "an enum declaration";
        case NK::UseStmt: return "a use statement"; case NK::AllomorphLit: return "an allomorph literal";
        default: return "node kind " + std::to_string((int)k);
    }
}
bool isIdent(const string& k) {
    if (k.empty() || !(ascii::isalpha((unsigned char)k[0]) || k[0] == '_' || (unsigned char)k[0] >= 0x80)) return false;
    for (unsigned char c : k) if (!(ascii::isalnum(c) || c == '-' || c == '_' || c == '\'' || c >= 0x80)) return false;
    return true;
}
bool jsIdent(const string& k) {
    if (k.empty() || !(ascii::isalpha((unsigned char)k[0]) || k[0] == '_')) return false;
    for (unsigned char c : k) if (!(ascii::isalnum(c) || c == '_')) return false;
    return true;
}
// R.name or R["na-me"]
string rt(const string& name) { return jsIdent(name) ? "R." + name : "R[" + jsStr(name) + "]"; }

// Builtins the runtime exports by Raku name (src/js-rt/50-builtins.js). A call
// to a name outside this table is a refusal, so the histogram names the gap.
const std::set<string> kBuiltins = {
    "item", "decont", "bindArray", "iterTopic", "parametric",
    "opFn", "react", "supplyBlock", "whenever", "emit", "done", "sleepP", "allo", "val", "module", "exportFn", "exportType", "exportMain", "boolOf", "isMatch", "isRegex", "smartmatchWith", "attrSet", "isFailure", "substMutate", "withDefault", "__radix", "__radix-list",
    "say", "print", "put", "note", "printf", "dd", "exit", "sqrt", "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh", "exp", "cbrt",
    "log", "log2", "log10", "atan2", "floor", "ceiling", "truncate", "round", "sign", "is-prime", "expmod", "polymod", "rand", "srand", "min", "max", "sum",
    "elems", "end", "join", "reverse", "sort", "map", "grep", "first", "unique", "keys", "values", "kv", "pairs", "push", "append", "pop", "shift", "unshift",
    "prepend", "splice", "zip", "roundrobin", "head", "tail", "defined", "item", "flat", "pick", "roll", "categorize", "classify", "reduce", "produce",
    "any", "all", "none", "one", "set", "bag", "mix", "chrs", "ords", "ucfirst", "slurp", "spurt", "lines", "get", "prompt", "sleep", "now", "time", "open",
    "close", "mkdir", "rmdir", "unlink", "dir", "chdir", "shell", "run", "chars", "ord", "chr", "uc", "lc", "tc", "tclc", "flip", "trim", "chomp", "chop",
    "substr", "index", "rindex", "split", "words", "comb", "sprintf", "abs", "gcd", "lcm", "not", "so", "gist", "raku", "numify", "trim-leading", "trim-trailing",
    "samecase", "indent", "fc", "wordcase", "minmax", "warn", "die", "fail", "take", "squish", "rotor", "batch", "combinations", "permutations", "hash", "list",
    "slip", "elem", "cross", "antipairs", "invert", "succ", "pred", "lazy", "eager", "cache", "capture", "infix", "exists", "unival",
};
// Names the runtime answers as type objects (R.T.<name>).
const std::set<string> kCoreTypes = {
    "Supply", "Supplier", "Supplier::Preserving", "Channel", "Tap", "Vow",
    "Mu", "Any", "Cool", "Numeric", "Real", "Int", "Num", "Rational", "Rat", "FatRat", "Str", "Bool", "Nil", "Positional", "Iterable", "List", "Array", "Seq", "Slip",
    "Range", "Associative", "Map", "Hash", "Pair", "Callable", "Code", "Block", "Routine", "Sub", "Method", "Whatever", "WhateverCode", "Exception", "X::AdHoc",
    "Failure", "Junction", "Order", "Setty", "Set", "SetHash", "Baggy", "Bag", "BagHash", "Mixy", "Mix", "MixHash", "Complex", "IO", "IO::Path", "IO::Handle",
    "Stringy", "Version", "Date", "DateTime", "Instant", "Match", "Regex", "Capture", "Signature", "IterationEnd", "Int:D", "Str:D", "Promise", "PromiseStatus",
};
const std::set<string> kLaterTypes = { "Lock", "Thread", "Proc", "Proc::Async", "IO::Socket::INET", "IO::Socket::Async", "Semaphore", "Lock::Async", "Scheduler", "ThreadPoolScheduler", "atomicint" };
const std::map<string, string> kBinOps = {
    {"+", "add"}, {"-", "sub"}, {"*", "mul"}, {"/", "div"}, {"%", "mod"}, {"**", "pow"}, {"div", "idiv"}, {"mod", "imod"}, {"gcd", "gcd"}, {"lcm", "lcm"},
    {"~", "concat"}, {"x", "xrepeat"}, {"==", "numeq"}, {"!=", "numne"}, {"<", "lt"}, {"<=", "le"}, {">", "gt"}, {">=", "ge"},
    {"eq", "seq"}, {"ne", "sne"}, {"lt", "slt"}, {"le", "sle"}, {"gt", "sgt"}, {"ge", "sge"}, {"<=>", "spaceship"}, {"cmp", "cmp"}, {"leg", "leg"},
    {"===", "identical"}, {"eqv", "eqv"}, {"=:=", "identical"}, {"+&", "bitand"}, {"+|", "bitor"}, {"+^", "bitxor"}, {"+<", "shl"}, {"+>", "shr"},
    {"~~", "smartmatch"}, {"=>", "pair"}, {"(elem)", "elem"}, {"∈", "elem"},
};
// Binary ops the runtime's OPS table has, reached as R.OPS["op"](a, b)
const std::set<string> kTableOps = {
    "min", "max", "(cont)", "∋", "(|)", "∪", "(&)", "∩", "(-)", "∖", "(^)", "⊖", "(+)", "⊎", "(<=)", "⊆", "(<)", "⊂", "(>=)", "⊇", "(>)", "⊃", "(==)", "≡",
    "?&", "?|", "?^", "xor", "!==", "!eq", "Z", "X", "but", "does", "o", "∘", "≤", "≥", "≠",
    "before", "after", "minmax", "!===", "unicmp", "coll", "~&", "~|", "~^", "!eqv", "!=:=", "!(elem)", "∉", "!(cont)", "∌",
};
const std::map<string, string> kUnicodeOps = { {"≤", "<="}, {"≥", ">="}, {"≠", "!="} };

// ------------------------------------------------------------ AST walking ----
// One child visitor for the whole polymorphic AST, so the analyses below stay short.
void forEachChild(Node* n, const std::function<void(Node*)>& f) {
    if (!n) return;
    auto E = [&](const ExprPtr& p) { if (p) f(p.get()); };
    auto S = [&](const StmtPtr& p) { if (p) f(p.get()); };
    auto B = [&](const std::unique_ptr<Block>& p) { if (p) f(p.get()); };
    auto PS = [&](const std::vector<Param>& ps) { for (auto& p : ps) { E(p.defaultVal); E(p.whereExpr); E(p.litVal); if (p.subSig) for (auto& q : *p.subSig) { E(q.defaultVal); E(q.whereExpr); } } };
    switch (n->kind) {
        case NK::InterpStr: for (auto& p : static_cast<InterpStr*>(n)->parts) E(p); break;
        case NK::VarExpr: { auto* v = static_cast<VarExpr*>(n); E(v->declDefault); E(v->declShape); E(v->declTypeExpr); break; }
        case NK::ListExpr: for (auto& p : static_cast<ListExpr*>(n)->items) E(p); break;
        case NK::SymbolicRef: { auto* s = static_cast<SymbolicRef*>(n); E(s->nameExpr); for (auto& p : s->segs) E(p); break; }
        case NK::ArrayLit: for (auto& p : static_cast<ArrayLit*>(n)->items) E(p); break;
        case NK::HashLit: for (auto& p : static_cast<HashLit*>(n)->items) E(p); break;
        case NK::Assign: { auto* a = static_cast<Assign*>(n); E(a->target); E(a->value); break; }
        case NK::Binary: { auto* b = static_cast<Binary*>(n); E(b->lhs); E(b->rhs); break; }
        case NK::Unary: E(static_cast<Unary*>(n)->operand); break;
        case NK::Call: { auto* c = static_cast<Call*>(n); E(c->callee); for (auto& p : c->args) E(p); break; }
        case NK::MethodCall: { auto* m = static_cast<MethodCall*>(n); E(m->inv); E(m->methodExpr); for (auto& p : m->args) E(p); break; }
        case NK::Index: { auto* i = static_cast<Index*>(n); E(i->base); E(i->index); break; }
        case NK::Ternary: { auto* t = static_cast<Ternary*>(n); E(t->cond); E(t->then); E(t->els); break; }
        case NK::NqpOp: for (auto& p : static_cast<NqpOp*>(n)->args) E(p); break;
        case NK::Range: { auto* r = static_cast<RangeExpr*>(n); E(r->from); E(r->to); break; }
        case NK::Pair: { auto* p = static_cast<PairExpr*>(n); E(p->keyExpr); E(p->value); break; }
        case NK::BlockExpr: { auto* b = static_cast<BlockExpr*>(n); PS(b->params); for (auto& s : b->body) S(s); break; }
        case NK::ChainExpr: for (auto& p : static_cast<ChainExpr*>(n)->operands) E(p); break;
        case NK::AllomorphLit: E(static_cast<AllomorphLit*>(n)->num); break;
        case NK::ExprStmt: E(static_cast<ExprStmt*>(n)->e); break;
        case NK::VarDecl: E(static_cast<VarDecl*>(n)->init); break;
        case NK::SubDecl: { auto* d = static_cast<SubDecl*>(n); E(d->nameExpr); PS(d->params); for (auto& a : d->altParams) PS(a); for (auto& s : d->body) S(s); for (auto& t : d->traits) E(t.arg); for (auto& a : d->immediateArgs) E(a); E(d->retLiteral); break; }
        case NK::IfStmt: { auto* i = static_cast<IfStmt*>(n); for (auto& br : i->branches) { E(br.first); B(br.second); } for (auto& ps : i->branchParams) PS(ps); PS(i->elseParams); B(i->elseBlock); break; }
        case NK::WhileStmt: { auto* w = static_cast<WhileStmt*>(n); E(w->cond); PS(w->params); B(w->body); break; }
        case NK::ForStmt: { auto* fo = static_cast<ForStmt*>(n); E(fo->list); PS(fo->params); B(fo->body); break; }
        case NK::LoopStmt: { auto* l = static_cast<LoopStmt*>(n); E(l->init); E(l->cond); E(l->incr); B(l->body); break; }
        case NK::Block: for (auto& s : static_cast<Block*>(n)->stmts) S(s); break;
        case NK::ReturnStmt: E(static_cast<ReturnStmt*>(n)->value); break;
        case NK::UseStmt: { auto* u = static_cast<UseStmt*>(n); E(u->ifCond); E(u->argExpr); break; }
        case NK::GivenStmt: { auto* g = static_cast<GivenStmt*>(n); E(g->topic); PS(g->params); PS(g->elseParams); B(g->body); B(g->elseBody); break; }
        case NK::WhenStmt: { auto* w = static_cast<WhenStmt*>(n); E(w->cond); B(w->body); break; }
        case NK::RepeatStmt: { auto* r = static_cast<RepeatStmt*>(n); E(r->cond); B(r->body); break; }
        case NK::ClassDecl: { auto* c = static_cast<ClassDecl*>(n); E(c->nameExpr); for (auto& a : c->attrs) { E(a.def); E(a.whereExpr); for (auto& t : a.userTraits) E(t.second); } for (auto& m : c->methods) f(m.get()); PS(c->roleParams); for (auto& ra : c->roleArgs) for (auto& e : ra.second) E(e); for (auto& s : c->body) S(s); E(c->verExpr); E(c->authExpr); E(c->apiExpr); break; }
        case NK::EnumDecl: E(static_cast<EnumDecl*>(n)->values); break;
        case NK::SubsetDecl: E(static_cast<SubsetDecl*>(n)->where); break;
        default: break;
    }
}
bool isClosureNode(Node* n) { return n->kind == NK::BlockExpr || n->kind == NK::SubDecl || n->kind == NK::ClassDecl; }
// Does the subtree contain a node satisfying pred? `intoClosures` = descend into blocks/subs.
bool containsRec(Node* n, const std::function<bool(Node*)>& pred, bool intoClosures, bool root) {
    if (!n) return false;
    if (pred(n)) return true;
    if (!root && !intoClosures && isClosureNode(n)) return false;
    bool found = false;
    forEachChild(n, [&](Node* c) { if (!found && containsRec(c, pred, intoClosures, false)) found = true; });
    return found;
}
bool contains(Node* n, const std::function<bool(Node*)>& pred, bool intoClosures) { return containsRec(n, pred, intoClosures, true); }
bool stmtsContain(const std::vector<StmtPtr>& ss, const std::function<bool(Node*)>& pred, bool intoClosures) {
    for (auto& s : ss) if (contains(s.get(), pred, intoClosures)) return true;
    return false;
}
bool isCallNamed(Node* n, const char* name) { return n->kind == NK::Call && static_cast<Call*>(n)->name == name; }
bool isTakeNode(Node* n) { return isCallNamed(n, "take") || isCallNamed(n, "take-rw"); }
bool isVarNamed(Node* n, const string& name) { return n->kind == NK::VarExpr && static_cast<VarExpr*>(n)->name == name; }
bool isLoopNode(Node* n) { return n->kind == NK::ForStmt || n->kind == NK::WhileStmt || n->kind == NK::LoopStmt || n->kind == NK::RepeatStmt; }

// `my` declarations buried inside an expression (`if my $m = …`, `(my $x = 5)`),
// which JavaScript cannot declare mid-expression: they are hoisted as `let`s.
void collectExprDecls(Node* n, std::vector<VarExpr*>& out) {
    if (!n) return;
    if (n->kind == NK::VarExpr) { auto* v = static_cast<VarExpr*>(n); if (v->declare) out.push_back(v); }
    forEachChild(n, [&](Node* c) { if (!isClosureNode(c)) collectExprDecls(c, out); });
}

struct SubInfo {
    std::vector<SubDecl*> cands;         // one for a plain sub, several for a multi
    bool isMulti = false;
    std::vector<int> rwIdx;              // positional indices declared `is rw` (plain subs)
    bool simple = false;                 // fixed-arity positional `$` params only
};

// A signature's shape, decided once per routine.
// A `$` parameter itemizes what it binds (`sub f($x) { $x.raku }; f((1,2))` is
// `$(1, 2)`): the runtime's item view. Skipped for types no list can satisfy.
static const std::set<std::string> kScalarOnly = { "Int", "UInt", "Str", "Num", "Rat", "Bool", "Numeric", "Real", "Complex", "Date", "DateTime", "Instant", "Duration", "Code", "Callable", "Block", "Sub", "Regex", "IntStr", "NumStr", "RatStr", "Version", "Pair", "Match", "Blob", "Buf", "Junction" };
static bool itemParam(const Param& p) {
    if (p.sigil != '$' || p.isRaw || p.isRw || p.name.empty()) return false;
    std::string t = p.type; size_t c = t.find(':'); if (c != std::string::npos && t.compare(c, 2, "::") != 0) t = t.substr(0, c);
    return !kScalarOnly.count(t);
}
bool simpleSig(const std::vector<Param>& ps) {
    for (auto& p : ps)
        if (p.named || p.slurpy || p.optional || p.defaultVal || p.invocant || p.subSig || p.isRw || p.litVal || p.sigil != '$' || p.name.empty() || p.whereExpr || p.typeCapture)
            return false;
    return true;
}

struct FnCtx {                           // one per emitted JS function (routine, closure, IIFE)
    std::set<std::string> params;        // the routine's parameter names: bound, not containers, so `for $p` iterates
    std::vector<string> temps;
    int tempN = 0;
    bool isRoutine = false;              // owns `return`
    bool needsRetCatch = false;          // a nested closure threw a RetCtl at us
    bool usesBang = false;               // $! / try / CATCH in this function
    bool usesSlash = false;              // $/ read or set in this function
    bool slashParam = false;             // …but bound as a parameter (an action method's `$/`)
    std::set<string> boxed;              // JS names read through `.v` (is rw params)
    std::map<string, string> stateAlias; // `state $x` → its module-level slot
    std::vector<string> declared;        // hoisted lets to prepend
};
struct LoopCtx {
    string rakuLabel, jsLabel;
    int fnDepth;
    bool needsCatch = false;
    string nextPhaser;                   // NEXT block text, replayed at every `next`
};
struct BlockCtx {                        // a block that `when` may leave
    string jsLabel;
    int fnDepth;
    bool isLoopBody;                     // `when` in a loop body → continue
    string loopLabel;
};

struct JsGen {
    std::ostringstream out;
    const JsOptions& opt;
    Program& prog;
    std::map<string, SubInfo> subs;      // every sub declaration in the unit, by name
    std::set<string> classNames;         // user classes/roles/grammars
    std::map<string, string> enumKeys;   // enum member → JS expression
    std::set<string> subsetNames;
    std::set<string> codeVars;           // `my &f` / `&f` params: calls by that name go through the variable
    // Lexical scopes: which sub names and sigilless variables are visible where.
    // `sub min` inside a block must not turn the builtin into u_min outside it.
    struct Scope { std::set<string> subs, sigilless; };
    std::vector<Scope> scopes;
    bool subVisible(const string& n) { for (int i = (int)scopes.size() - 1; i >= 0; i--) if (scopes[i].subs.count(n)) return true; return false; }
    bool sigillessVisible(const string& n) { for (int i = (int)scopes.size() - 1; i >= 0; i--) if (scopes[i].sigilless.count(n)) return true; return false; }
    void declareSigilless(const string& n) { if (!scopes.empty()) scopes.back().sigilless.insert(n); }
    void declareSigillessParams(const std::vector<Param>& ps) { for (auto& p : ps) if (!p.name.empty() && p.name[0] != '$' && p.name[0] != '@' && p.name[0] != '%' && p.name[0] != '&') declareSigilless(p.name); }
    std::vector<string> prelude;         // literal consts, state slots (top of the program function)
    int litN = 0, labelN = 0, stateN = 0, fnCounter = 0;
    std::vector<FnCtx> fns;
    std::vector<LoopCtx> loops;
    std::vector<BlockCtx> blocks;
    std::vector<string> topics;          // JS name of `$_` per topic scope
    std::set<string> itemTopics;         // …those bound to an itemized value (`given $x`)
    std::map<string, string> topicSlots; // topic JS name → the array slot it aliases (`for @a { $_ *= 2 }` writes back)
    std::vector<string> givens;          // JS labels of enclosing `given` blocks
    string selfName;                     // "self" inside a method, else empty
    string curClass;                     // class being emitted
    std::vector<std::vector<string>> stateHoists;
    bool hasMain = false;
    std::vector<string> endBlocks;
    // Async colouring (TRANSPILE-PLAN P4): a routine that awaits — `await`, `.result`,
    // or a call to a coloured sub / a coloured method name — is `async`, and every
    // call to it is awaited. Decided once in prepass, by fixpoint.
    std::set<string> asyncSubs, asyncMethods;
    bool usesChannel = false;            // the program names Channel: `.list` may have to wait for a close
    bool isAwaitMethod(const string& m) { return m == "receive" || m == "wait" || (usesChannel && m == "list"); }
    bool hasAsyncBlockArg(const std::vector<ExprPtr>& as) { for (auto& a : as) if (a->kind == NK::BlockExpr && !static_cast<BlockExpr*>(a.get())->isSub && stmtsAwait(static_cast<BlockExpr*>(a.get())->body)) return true; return false; }
    // Does this function body await? Nested closures are their own functions — a `start`
    // block or a map callback that awaits does not make its enclosing routine async — except
    // the blocks run in place as IIFEs: do, try, gather.
    bool awaitsIn(Node* n) {
        return contains(n, [&](Node* x) {
            if (x->kind == NK::Call) { auto* c = static_cast<Call*>(x); return c->name == "await" || c->name == "react" || c->name == "sleep" || (!c->name.empty() && asyncSubs.count(c->name)); }
            if (x->kind == NK::MethodCall) { auto* m = static_cast<MethodCall*>(x); return m->method == "result" || isAwaitMethod(m->method) || asyncMethods.count(m->method) || hasAsyncBlockArg(m->args); }
            if (x->kind == NK::Unary) { auto* u = static_cast<Unary*>(x); if ((u->op == "do" || u->op == "try" || u->op == "gather") && u->operand && u->operand->kind == NK::BlockExpr) return stmtsAwait(static_cast<BlockExpr*>(u->operand.get())->body); }
            return false;
        }, false);
    }
    bool stmtsAwait(const std::vector<StmtPtr>& ss) { for (auto& s : ss) if (awaitsIn(s.get())) return true; return false; }
    string sinkVar;                      // value-collecting loop: tail values are pushed here, not returned
    bool jsInterop = false;              // `use JS` seen: the JS term and JS::Object are live
    string ret(const string& v) { return sinkVar.empty() ? "return " + v + ";" : sinkVar + ".push(" + v + ");"; }

    JsGen(Program& p, const JsOptions& o) : opt(o), prog(p) {}

    // -- output plumbing --
    // every JS line carries the Raku line of the statement it came from as a trailing
    // marker; transpileToJs turns the markers into the source map and strips them
    int pendingSrcLine = 0;
    void line(int ind, const string& s) { out << string(ind * 4, ' ') << s; if (pendingSrcLine > 0 && !s.empty()) { out << " //@L" << pendingSrcLine; pendingSrcLine = 0; } out << "\n"; }
    string capture(const std::function<void()>& f) {
        std::ostringstream saved; saved.swap(out);
        f();
        string s = out.str(); out.swap(saved);
        return s;
    }
    FnCtx& fn() { return fns.back(); }
    string tmp() { string t = "_t" + std::to_string(++fn().tempN); fn().temps.push_back(t); return t; }
    string label(const char* pre) { return pre + std::to_string(++labelN); }
    int fnDepth() const { return (int)fns.size(); }
    string topic() { return topics.empty() ? "R.Any" : topics.back(); }
    string newTopic() { return "v___" + std::to_string(topics.size() + 1) + "_" + std::to_string(++labelN); }
    string litConst(const string& init) { string n = "_lit" + std::to_string(++litN); prelude.push_back("const " + n + " = " + init + ";"); return n; }

    // Emit a JS function body: run `body` in a fresh FnCtx, then prepend its temps/lets
    // and wrap it in the return-catch when a nested closure needed one.
    string fnBody(bool isRoutine, const std::function<void()>& body, int ind) {
        fns.emplace_back(); fn().isRoutine = isRoutine;
        string savedSink = sinkVar; sinkVar.clear();
        string inner = capture(body);
        sinkVar = savedSink;
        FnCtx ctx = fns.back(); fns.pop_back();
        string pre;
        if (!ctx.temps.empty()) { pre += string(ind * 4, ' ') + "let "; for (size_t i = 0; i < ctx.temps.size(); i++) pre += (i ? ", " : "") + ctx.temps[i]; pre += ";\n"; }
        if (ctx.usesBang) pre += string(ind * 4, ' ') + "let v__bang = R.Nil;\n";
        if (ctx.usesSlash && !ctx.slashParam) pre += string(ind * 4, ' ') + "let v__slash = R.Nil;\n";
        for (auto& d : ctx.declared) pre += string(ind * 4, ' ') + d + "\n";
        if (ctx.needsRetCatch) {
            string s = pre + string(ind * 4, ' ') + "const _rt = {};\n" + string(ind * 4, ' ') + "try {\n" + inner +
                       string(ind * 4, ' ') + "} catch (_e) { if (_e instanceof R.RetCtl && _e.token === _rt) return _e.v; throw _e; }\n";
            return s;
        }
        return pre + inner;
    }

    // ------------------------------------------------------------ variables --
    // JS name of a variable reference. `$_` and friends resolve to the current topic scope.
    string varRef(VarExpr* v) {
        const string& n = v->name;
        if (n == "$_") return topic();
        if (n == "$!") { fn().usesBang = true; return "v__bang"; }
        if (n == "$\xC2\xA2") { if (!fn().slashParam) return "R.Nil"; fn().usesSlash = true; return "v__slash"; }   // $¢: the cursor inside regex code, gone outside
        if (n == "$/") { fn().usesSlash = true; return "v__slash"; }
        if (n.size() > 1 && n[0] == '$' && ascii::isdigit((unsigned char)n[1])) { fn().usesSlash = true; return "R.matchAt(v__slash, " + n.substr(1) + ")"; }
        if (n.size() > 2 && (n[1] == '!' || n[1] == '.')) {
            if (selfName.empty()) refuse("attribute access outside a method", v->line);
            if (n[1] == '!') return selfName + ".a_" + mangleBody(v->attrBare);
            return "R.mc(" + selfName + ", " + jsStr(v->attrBare) + ")";
        }
        if (n.size() > 2 && n[1] == '*') {
            static const std::set<string> core = { "@*ARGS", "%*ENV", "$*PROGRAM-NAME", "$*PROGRAM", "$*CWD", "$*IN", "$*OUT", "$*ERR", "$*EXECUTABLE", "$*EXECUTABLE-NAME", "$*PID", "$*TMPDIR", "$*HOME", "$*USER", "$*RAKU", "$*PERL", "$*VM", "$*KERNEL", "$*DISTRO", "$*USAGE", "$*INIT-INSTANT", "$*COLLATION", "$*SCHEDULER", "$*THREAD", "$*DEFAULT-READ-ELEMS", "$*REPO", "$*DISTRIBUTION" };
            if (n == "$*EXECUTABLE" || n == "$*EXECUTABLE-NAME") refuse("a program that runs the rakupp executable (" + n + ")", v->line);
            if (core.count(n)) return "R.dynVar(" + jsStr(n) + ")";
            return "R.dynGet(" + jsStr(n) + ")";
        }
        if (n.size() > 2 && n[1] == '?') {
            if (n == "$?FILE") return jsStr(opt.srcPath);
            if (n == "$?LINE") return std::to_string(v->line);
            if (n == "$?PACKAGE" || n == "$?CLASS") return curClass.empty() ? "R.T.Any" : mangleType(curClass);
            refuse("compile-time variable " + n, v->line);
        }
        if (n.size() > 2 && n[1] == '^') return mangleVar("$" + n.substr(2));   // placeholder $^a → v_a
        if (n.size() > 2 && n[1] == ':') return mangleVar("$" + n.substr(2));   // :$named placeholder
        if (n[0] == '&') {
            string bare = n.substr(1);
            if (subVisible(bare)) return mangleSub(bare);
            if (kBuiltins.count(bare)) return rt(bare);
            if (bare.rfind("infix:<", 0) == 0) return "R.opFn(" + jsStr(bare.substr(7, bare.size() - 8)) + ")";
            if (bare.size() > 2 && bare.front() == '[' && bare.back() == ']') return "R.opFn(" + jsStr(bare.substr(1, bare.size() - 2)) + ")";
            return mangleVar(n);
        }
        string js = mangleVar(n);
        for (int i = (int)fns.size() - 1; i >= 0; i--) { auto sa = fns[i].stateAlias.find(js); if (sa != fns[i].stateAlias.end()) return sa->second; }
        if (!fns.empty() && fn().boxed.count(js)) return js + ".v";
        return js;
    }
    bool isPlainVar(Expr* e) { return e->kind == NK::VarExpr && !static_cast<VarExpr*>(e)->declare; }

    // --------------------------------------------------------- expressions --
    // value position: a `*`-bearing expression curries into a WhateverCode — but not
    // while one is already being built (its operands belong to the outer curry)
    string exArg(Expr* e) {
        if (!hasWhatever(e) || !wcArity.empty()) return ex(e);
        return exCurry(e);
    }
    // an argument / list item: always its own closure scope (`.grep(* %% 3)`)
    string exCurry(Expr* e) {
        if (e->kind == NK::RegexLit) return regexObject(static_cast<RegexLit*>(e));   // an argument is the Regex, not a match
        if (e->kind == NK::Whatever) return "R.Whatever";   // f(*): the Whatever itself, not a curry
        if (!hasWhatever(e)) return ex(e);
        wcArity.push_back(0);
        string body = ex(e);
        int n = wcArity.back(); wcArity.pop_back();
        string params;
        for (int i = 1; i <= std::max(1, n); i++) params += (i > 1 ? ", " : "") + string("_w") + std::to_string(i);
        string f = "((" + params + ") => " + body + ")";
        if (n > 1) return "R.wc(" + f + ", " + std::to_string(n) + ")";
        return "R.wc(" + f + ", 1)";
    }
    std::vector<int> wcArity;
    // `$^a` / `$:x` → the plain variable name the body refers to
    static string plainName(const string& n) { return (n.size() > 2 && (n[1] == '^' || n[1] == ':')) ? n.substr(0, 1) + n.substr(2) : n; }
    static bool hasWhatever(Expr* e) {
        if (!e) return false;
        switch (e->kind) {
            case NK::Whatever: return !static_cast<WhateverExpr*>(e)->hyper;
            case NK::Binary: { auto* b = static_cast<Binary*>(e);
                if (b->op == "..." || b->op == "...^" || b->op == "^..." || b->op == "^...^" || b->op == "xx") return false;
                return hasWhatever(b->lhs.get()) || hasWhatever(b->rhs.get()); }
            case NK::Unary: return hasWhatever(static_cast<Unary*>(e)->operand.get());
            case NK::Ternary: { auto* t = static_cast<Ternary*>(e); return hasWhatever(t->cond.get()) || hasWhatever(t->then.get()) || hasWhatever(t->els.get()); }
            case NK::MethodCall: return hasWhatever(static_cast<MethodCall*>(e)->inv.get());
            case NK::Call: return hasWhatever(static_cast<Call*>(e)->callee.get());
            case NK::Index: return hasWhatever(static_cast<Index*>(e)->base.get());
            case NK::Range: return false;
            default: return false;
        }
    }
    // a `k => v` / `:k(v)` argument with an identifier key is a named argument
    static bool isNamedArg(Expr* a) { return syntacticNamedPair(a); }
    static bool isSlip(Expr* e) { return e->kind == NK::Unary && static_cast<Unary*>(e)->op == "|" && !static_cast<Unary*>(e)->postfix; }

    // Arguments of a call: positionals, then one trailing R.named([...]) when there are named ones.
    string args(const std::vector<ExprPtr>& as) {
        string pos, named; bool hashSpread = false; string hashSpreadExpr;
        for (auto& a : as) {
            Expr* e = a.get();
            if (isNamedArg(e)) {
                auto* p = static_cast<PairExpr*>(e);
                string v = p->value ? exCurry(p->value.get()) : "true";
                if (!named.empty()) named += ", ";
                named += "[" + jsStr(p->key) + ", " + v + "]";
                continue;
            }
            if (isSlip(e)) {
                Expr* op = static_cast<Unary*>(e)->operand.get();
                if (op->kind == NK::VarExpr && static_cast<VarExpr*>(op)->name[0] == '%') { hashSpread = true; hashSpreadExpr = ex(op); continue; }
                if (!pos.empty()) pos += ", ";
                pos += "...R.spreadArgs(" + exCurry(op) + ")";
                continue;
            }
            if (e->kind == NK::ListExpr && static_cast<ListExpr*>(e)->semicolon) {   // f(1; 2): each segment a List
                for (auto& seg : static_cast<ListExpr*>(e)->items) {
                    if (!pos.empty()) pos += ", ";
                    pos += seg->kind == NK::ListExpr ? "R.mkList([" + listItems(static_cast<ListExpr*>(seg.get())->items, true) + "])" : "R.mkList([" + exCurry(seg.get()) + "])";
                }
                continue;
            }
            if (!pos.empty()) pos += ", ";
            pos += isItemized(e) ? "R.item(" + exCurry(e) + ")" : exCurry(e);   // a scalar variable passes as its item: a slurpy keeps it whole
        }
        string tail;
        if (hashSpread) tail = "R.namedFromHash(" + hashSpreadExpr + (named.empty() ? "" : ", [" + named + "]") + ")";
        else if (!named.empty()) tail = "R.named([" + named + "])";
        if (tail.empty()) return pos;
        return pos.empty() ? tail : pos + ", " + tail;
    }
    string listItems(const std::vector<ExprPtr>& items, bool asList = false) {   // items of a list literal, slips spliced by the runtime; in a List an itemized element stays an item
        string s;
        for (size_t i = 0; i < items.size(); i++) {
            if (i) s += ", ";
            Expr* e = items[i].get();
            if (isSlip(e)) s += "R.slip(" + exCurry(static_cast<Unary*>(e)->operand.get()) + ")";
            else if (asList && isItemized(e)) s += "R.item(" + exCurry(e) + ")";
            else s += exCurry(e);
        }
        return s;
    }
    // an expression whose value is itemized (a scalar container's content): in list
    // assignment and `for` it contributes ONE element, however iterable it is
    bool isItemized(Expr* e) {
        if (e->kind == NK::VarExpr) { auto* v = static_cast<VarExpr*>(e); if (v->name == "$_") return !topics.empty() && itemTopics.count(topics.back()); return v->name[0] == '$' && !fn().params.count(v->name); }
        if (e->kind == NK::MethodCall) { auto* m = static_cast<MethodCall*>(e); return !m->hyper && !m->methodExpr && (m->method == "made" || m->method == "ast"); }   // .made lives in a scalar container
        if (e->kind == NK::Index) {
            auto* ix = static_cast<Index*>(e);
            if (ix->base && ix->base->kind == NK::VarExpr && static_cast<VarExpr*>(ix->base.get())->name == "$/") return false;   // $<x>: a Match's captures are bound, not containers — `for $<pair>` iterates
            return ix->adverb.empty() && ix->index && !isSliceIndex(ix->index.get());
        }
        if (e->kind == NK::Unary) return static_cast<Unary*>(e)->op == "ctx$";
        return false;
    }
    // @a[^3] / %h<a b> / @a[1..*] / @a[*] / @a[@idx]: a slice, i.e. a list, not one item
    static bool isSliceIndex(Expr* i) {
        switch (i->kind) {
            case NK::ListExpr: case NK::Range: case NK::Whatever: return true;
            case NK::ArrayLit: return static_cast<ArrayLit*>(i)->isList && static_cast<ArrayLit*>(i)->items.size() != 1;
            case NK::Unary: return static_cast<Unary*>(i)->op == "^";
            case NK::VarExpr: return static_cast<VarExpr*>(i)->name[0] == '@';
            default: return false;
        }
    }
    string listSource(Expr* e) {         // the RHS of a list assignment / a `for` list
        if (isItemized(e)) return "[" + exArg(e) + "]";
        return exArg(e);
    }

    string binop(const string& op, const string& L, const string& R_, int lineNo) {
        auto it = kBinOps.find(op);
        if (it != kBinOps.end()) return "R." + it->second + "(" + L + ", " + R_ + ")";
        auto u = kUnicodeOps.find(op);
        if (u != kUnicodeOps.end()) return binop(u->second, L, R_, lineNo);
        if (kTableOps.count(op)) return "R.OPS[" + jsStr(op) + "](" + L + ", " + R_ + ")";
        if (op.size() >= 3 && op.front() == '[' && op.back() == ']') return "R.opFn(" + jsStr(op.substr(1, op.size() - 2)) + ")(" + L + ", " + R_ + ")";   // a [R//] b: the bracketed (reversed, negated) infix
        refuse("infix operator '" + op + "'", lineNo);
    }

    string ex(Expr* e) {
        switch (e->kind) {
            case NK::IntLit: {
                auto* n = static_cast<IntLit*>(e);
                if (!n->big.empty()) return litConst("R.normBig(" + n->big + "n)");
                if (n->v > 9007199254740991LL || n->v < -9007199254740991LL) return litConst(std::to_string(n->v) + "n");
                return n->v < 0 ? "(" + std::to_string(n->v) + ")" : std::to_string(n->v);
            }
            case NK::NumLit: {
                auto* n = static_cast<NumLit*>(e);
                if (n->imaginary) return litConst("new R.RComplex(0, " + numLit(n->v) + ")");
                if (n->isRat) {
                    string num = n->bigNum.empty() ? std::to_string(n->ratNum) : n->bigNum;
                    string den = n->bigDen.empty() ? std::to_string(n->ratDen) : n->bigDen;
                    return litConst("R.mkRat(" + num + "n, " + den + "n)");
                }
                if (std::isinf(n->v)) return n->v < 0 ? "(-Infinity)" : "Infinity";
                if (std::isnan(n->v)) return "NaN";
                if (n->v == (long long)n->v || n->v == 0) return litConst("new R.RNum(" + numLit(n->v) + ")");
                return "(" + numLit(n->v) + ")";
            }
            case NK::StrLit: return jsStr(static_cast<StrLit*>(e)->v);
            case NK::BoolLit: return static_cast<BoolLit*>(e)->v ? "true" : "false";
            case NK::AllomorphLit: { auto* a = static_cast<AllomorphLit*>(e); return "R.allo(" + exArg(a->num.get()) + ", " + jsStr(a->str) + ")"; }   // <42>: the number that is also its spelling
            case NK::InterpStr: {
                auto* s = static_cast<InterpStr*>(e);
                if (s->parts.empty()) return "\"\"";
                string r;
                for (size_t i = 0; i < s->parts.size(); i++) {
                    if (i) r += " + ";
                    Expr* p = s->parts[i].get();
                    if (p->kind == NK::StrLit) r += jsStr(static_cast<StrLit*>(p)->v);
                    else r += "R.str(" + exArg(p) + ")";
                }
                if (s->parts.size() == 1 && s->parts[0]->kind != NK::StrLit) return r;
                if (s->parts[0]->kind != NK::StrLit) r = "\"\" + " + r;
                return "(" + r + ")";
            }
            case NK::VarExpr: {
                auto* v = static_cast<VarExpr*>(e);
                if (v->declare) {   // hoisted by the enclosing statement: a plain reference now
                    if (v->declScope == "state") { for (int i = (int)fns.size() - 1; i >= 0; i--) { auto sa = fns[i].stateAlias.find(mangleVar(v->name)); if (sa != fns[i].stateAlias.end()) return sa->second; } }
                    if (v->name[0] == '@') return mangleVar(v->name) + " = R.mkArray([])";
                    if (v->name[0] == '%') return mangleVar(v->name) + " = R.mkHash()";
                    return mangleVar(v->name);
                }
                return varRef(v);
            }
            case NK::SelfTerm: if (selfName.empty()) refuse("`self` outside a method", e->line); return selfName;
            case NK::Whatever: {
                auto* w = static_cast<WhateverExpr*>(e);
                if (w->hyper) refuse("a HyperWhatever (**)", e->line);
                if (wcArity.empty()) return "R.Whatever";
                return "_w" + std::to_string(++wcArity.back());
            }
            case NK::BlockExpr: return blockClosure(static_cast<BlockExpr*>(e));
            case NK::Range: {
                auto* r = static_cast<RangeExpr*>(e);
                string from = r->from ? rangeEnd(r->from.get()) : "(-Infinity)";
                string to = r->to ? rangeEnd(r->to.get()) : "Infinity";
                return "R.range(" + from + ", " + to + (r->exFrom || r->exTo ? string(", ") + (r->exFrom ? "true" : "false") + ", " + (r->exTo ? "true" : "false") : "") + ")";
            }
            case NK::RegexLit: return regexObject(static_cast<RegexLit*>(e));   // a Regex object; boolOperand() matches it against $_ in boolean context
            case NK::SubstLit: return substExpr(static_cast<SubstLit*>(e), nullptr);
            case NK::Index: return indexExpr(static_cast<Index*>(e));
            case NK::NameTerm: return nameTerm(static_cast<NameTerm*>(e));
            case NK::Ternary: { auto* t = static_cast<Ternary*>(e); return "(R.truthy(" + boolOperand(t->cond.get()) + ") ? " + exArg(t->then.get()) + " : " + exArg(t->els.get()) + ")"; }
            case NK::NqpOp: refuse("an nqp:: op", e->line);
            case NK::Unary: return unary(static_cast<Unary*>(e));
            case NK::Binary: return binary(static_cast<Binary*>(e));
            case NK::Call: return call(static_cast<Call*>(e));
            case NK::MethodCall: return methodCall(static_cast<MethodCall*>(e));
            case NK::Assign: return assign(static_cast<Assign*>(e));
            case NK::ChainExpr: {
                auto* c = static_cast<ChainExpr*>(e);
                // (_t1 = a, _t2 = b, R.truthy(a<b) && (_t3 = c, R.truthy(b<c)) && …): each
                // operand evaluated once, later ones only while the chain still holds
                std::vector<string> ts;
                for (size_t i = 0; i < c->operands.size(); i++) ts.push_back(tmp());
                string r = "(" + ts[0] + " = " + exArg(c->operands[0].get()) + ", " + ts[1] + " = " + exArg(c->operands[1].get()) +
                           ", R.truthy(" + binop(c->ops[0], ts[0], ts[1], c->line) + ")";
                for (size_t i = 1; i < c->ops.size(); i++)
                    r += " && (" + ts[i + 1] + " = " + exArg(c->operands[i + 1].get()) + ", R.truthy(" + binop(c->ops[i], ts[i], ts[i + 1], c->line) + "))";
                return r + ")";
            }
            case NK::Pair: {
                auto* p = static_cast<PairExpr*>(e);
                string k = p->keyExpr ? exArg(p->keyExpr.get()) : jsStr(p->key);
                string v = p->value ? exArg(p->value.get()) : "true";
                return "R.pair(" + k + ", " + v + ")";
            }
            case NK::ListExpr: {
                auto* l = static_cast<ListExpr*>(e);
                if (l->semicolon) refuse("a semicolon list", e->line);
                return "R.list(" + listItems(l->items, true) + ")";
            }
            case NK::HashLit: return "R.hashLit([" + listItems(static_cast<HashLit*>(e)->items) + "])";
            case NK::ArrayLit: {
                auto* a = static_cast<ArrayLit*>(e);
                if (a->isList) return "R.mkList([" + listItems(a->items, true) + "])";
                bool single = !a->fromCommaList && a->items.size() == 1 && !isSlip(a->items[0].get()) && !isItemized(a->items[0].get());
                return "R.arrayLit([" + listItems(a->items) + "], " + (single ? "true" : "false") + ")";
            }
            case NK::SymbolicRef: refuse("a symbolic reference ::(…)", e->line);
            default: refuse(nkName(e->kind), e->line);
        }
    }
    static string numLit(double v) { char b[40]; std::snprintf(b, sizeof b, "%.17g", v); string s = b; if (s.find_first_of(".eEn") == string::npos) s += ".0"; if (s == "-0.0") return "-0"; return s; }
    string rangeEnd(Expr* e) {
        if (e->kind == NK::Whatever) return "R.Whatever";
        if (e->kind == NK::NameTerm) { auto& n = static_cast<NameTerm*>(e)->name; if (n == "Inf" || n == "\xe2\x88\x9e") return "Infinity"; }
        return exArg(e);
    }

    string nameTerm(NameTerm* n) {
        const string& name = n->name;
        if (sigillessVisible(name)) return mangleVar(name);   // a `\x` variable shadows any term
        if (jsInterop && name == "JS") return "R.JS";
        if (jsInterop && name == "JS::Object") return "R.JsObjectT";
        if (name == "True") return "true";
        if (name == "False") return "false";
        if (name == "Nil") return "R.Nil";
        if (name == "Inf" || name == "\xe2\x88\x9e") return "Infinity";
        if (name == "NaN") return "NaN";
        if (name == "pi" || name == "\xcf\x80") return "Math.PI";
        if (name == "tau" || name == "\xcf\x84") return "(2 * Math.PI)";
        if (name == "e") return "Math.E";
        if (name == "i") return litConst("new R.RComplex(0, 1)");
        if (name == "rand") return "R.rand()";
        if (name == "time") return "R.time()";
        if (name == "now") return "R.now()";
        if (name == "Empty") return "R.mkSlip([])";
        if (name == "Less") return "R.Less"; if (name == "Same") return "R.Same"; if (name == "More") return "R.More";
        if (name == "Order::Less") return "R.Less"; if (name == "Order::Same") return "R.Same"; if (name == "Order::More") return "R.More";
        if (name == "Bool::True") return "true"; if (name == "Bool::False") return "false";
        if (name == "IterationEnd") return "R.T.IterationEnd";
        if (name == "self") { if (selfName.empty()) refuse("`self` outside a method", n->line); return selfName; }
        auto ek = enumKeys.find(name);
        if (ek != enumKeys.end()) return ek->second;
        if (classNames.count(name)) return mangleType(name);
        if (subsetNames.count(name)) return mangleType(name);
        if (kLaterTypes.count(name)) refuse("concurrency/process types (" + name + ") — P4 of the plan", n->line);
        if (n->ofType.size()) { if (kCoreTypes.count(name)) return "R.parametric(R.T." + name + ", " + typeObj(n->ofType, n->line) + ")"; refuse("a parameterized type " + name, n->line); }   // Array[Int]: one type object per parameter
        if (subVisible(name)) return mangleSub(name) + "()";
        if (kBuiltins.count(name)) return rt(name) + "()";
        if (kCoreTypes.count(name)) return jsIdent(name) ? "R.T." + name : "R.T[" + jsStr(name) + "]";
        if (name.rfind("X::", 0) == 0) return "R.mkExType(" + jsStr(name) + ")";
        size_t dc = name.find("::");
        if (dc != string::npos) {   // Enum::Member
            string ty = name.substr(0, dc), key = name.substr(dc + 2);
            auto e2 = enumKeys.find(key);
            if (e2 != enumKeys.end()) return e2->second;
            if (classNames.count(ty)) return "R.mc(" + mangleType(ty) + ", " + jsStr(key) + ")";
        }
        refuse("the name '" + name + "'", n->line);
    }

    // %h{k} / @a[i] and their adverbs
    string indexExpr(Index* ix) {
        if (ix->multiDim || ix->semicolonSub) refuse("a multi-dimensional subscript", ix->line);
        string base = ex(ix->base.get());
        string key = ix->index ? exArg(ix->index.get()) : "R.Whatever";
        if (ix->index && ix->index->kind == NK::ListExpr && static_cast<ListExpr*>(ix->index.get())->items.empty()) key = "R.Whatever";
        const string& adv = ix->adverb;
        string g = ix->isHash ? "R.hget" : "R.aget";
        if (adv.empty()) return g + "(" + base + ", " + key + ")";
        bool neg = adv[0] == '!';
        string a = neg ? adv.substr(1) : adv;
        if (a == "exists") { string r = (ix->isHash ? "R.hexists(" : "R.aexists(") + base + ", " + key + ")"; return neg ? "!" + r : r; }
        if (a == "delete") return (ix->isHash ? "R.hdelete(" : "R.adelete(") + base + ", " + key + ")";
        string exists = string(ix->isHash ? "R.hexists(" : "R.aexists(") + base + ", " + key + ")";
        if (a == "k") return "(" + exists + " ? " + key + " : R.mkSlip([]))";
        if (a == "v") return "(" + exists + " ? " + g + "(" + base + ", " + key + ") : R.mkSlip([]))";
        if (a == "kv") return "R.kvAdverb(" + base + ", " + key + ", " + (ix->isHash ? "true" : "false") + ")";
        if (a == "p") return "R.pAdverb(" + base + ", " + key + ", " + (ix->isHash ? "true" : "false") + ")";
        refuse("subscript adverb :" + adv, ix->line);
    }

    // -------------------------------------------------------------- lvalues --
    // A target as (getter, setter-prefix) so compound assignment can read and write it.
    struct LV { string get; std::function<string(const string&)> set; string prep; };
    // an expression over an lvalue: the temp assignments first, then the body
    static string lvExpr(const LV& lv, const string& body) { return "(" + (lv.prep.empty() ? "" : lv.prep + ", ") + body + ")"; }
    LV lvalue(Expr* t) {
        if (t->kind == NK::VarExpr) {
            auto* v = static_cast<VarExpr*>(t);
            const string& n = v->name;
            if (n.size() > 2 && n[1] == '.') { if (selfName.empty()) refuse("attribute assignment outside a method", t->line); string k = selfName + ".a_" + mangleBody(v->attrBare); string nm = jsStr(v->attrBare), sn = selfName; return { k, [k, nm, sn](const string& x) { return "R.attrSet(" + sn + ", " + nm + ", " + x + ")"; } }; }
            if (n.size() > 2 && n[1] == '*' && !v->declare) {
                static const std::set<string> core = { "$*OUT", "$*ERR", "$*IN", "@*ARGS", "%*ENV" };
                if (!core.count(n)) { string k = jsStr(n); return { "R.dynGet(" + k + ")", [k](const string& x) { return "R.dynSet(" + k + ", " + x + ")"; } }; }
            }
            string r = varRef(v);
            if (n == "$_") { auto sl = topicSlots.find(r); if (sl != topicSlots.end()) { string slot = sl->second; return { r, [r, slot](const string& x) { return "(" + r + " = " + x + ", " + slot + " = R.decont(" + r + "))"; } }; } }   // the topic aliases an array slot
            return { r, [r](const string& x) { return r + " = " + x; } };
        }
        if (t->kind == NK::Index) {
            auto* ix = static_cast<Index*>(t);
            if (!ix->adverb.empty()) refuse("an adverbed subscript as an assignment target", t->line);
            if (ix->multiDim) refuse("a multi-dimensional subscript as an assignment target", t->line);
            string b = tmp(), k = tmp();
            // the base autovivifies (`%h{$a}{$b} += 1`), as the plain assignment path does
            string base = vivBase(ix->base.get(), ix->isHash ? '%' : '@'), key = ix->index ? exArg(ix->index.get()) : "R.Whatever";
            string get = (ix->isHash ? "R.hget(" : "R.aget(") + b + ", " + k + ")";
            string setf = ix->isHash ? "R.hset" : "R.aset";
            return { get, [b, k, setf](const string& x) { return setf + "(" + b + ", " + k + ", " + x + ")"; }, b + " = " + base + ", " + k + " = " + key };
        }
        if (t->kind == NK::ListExpr) refuse("a list as a compound-assignment target", t->line);
        if (t->kind == NK::Unary && static_cast<Unary*>(t)->op == "decont") return lvalue(static_cast<Unary*>(t)->operand.get());
        if (t->kind == NK::MethodCall) {
            auto* m = static_cast<MethodCall*>(t);
            if (!m->args.empty() || m->hyper) refuse("assignment to this method call", t->line);
            string inv = ex(m->inv.get()), name = jsStr(m->method);
            return { "R.mc(" + inv + ", " + name + ")", [inv, name](const string& x) { return "R.mcSet(" + inv + ", " + name + ", " + x + ")"; } };
        }
        refuse("assignment to this target", t->line);
    }
    // the base autovivified: `%h{$k}.push(1)` / `%h{$a}{$b} = 1`
    string vivBase(Expr* b, char sigil) {
        if (b->kind == NK::Index) {
            auto* ix = static_cast<Index*>(b);
            if (ix->adverb.empty() && !ix->multiDim && ix->index) {
                string inner = vivBase(ix->base.get(), ix->isHash ? '%' : '@');
                return (ix->isHash ? "R.hviv(" : "R.aviv(") + inner + ", " + exArg(ix->index.get()) + ", \"" + sigil + "\")";
            }
        }
        return ex(b);
    }
    string assign(Assign* a) {
        Expr* t = a->target.get();
        const string& op = a->op;
        if (op == "=" || op == ":=" || op == "::=") {
            // a declaring target inside an expression was hoisted; treat as plain assignment
            if (t->kind == NK::VarExpr && static_cast<VarExpr*>(t)->declare) return declAssign(static_cast<VarExpr*>(t), a->value.get(), true);
            if (t->kind == NK::VarExpr) {
                auto* v = static_cast<VarExpr*>(t);
                char sig = v->name[0];
                if (sig == '@' && v->name.size() > 1 && v->name[1] != '!' && v->name[1] != '.' && op == "=") return "R.assignArray(" + varRef(v) + ", " + listSource(a->value.get()) + ")";
                if (sig == '%' && v->name.size() > 1 && v->name[1] != '!' && v->name[1] != '.' && op == "=") return "R.assignHash(" + varRef(v) + ", " + listSource(a->value.get()) + ")";
                if ((sig == '@' || sig == '%') && v->name.size() > 2 && v->name[1] == '!') { string k = selfName + ".a_" + mangleBody(v->attrBare); return (sig == '@' ? "R.assignArray(" : "R.assignHash(") + k + ", " + listSource(a->value.get()) + ")"; }
            }
            if (t->kind == NK::ListExpr) return listAssign(static_cast<ListExpr*>(t), a->value.get());
            if (t->kind == NK::Index) {
                auto* ix = static_cast<Index*>(t);
                if (op != "=") refuse("binding (:=) to a subscript", a->line);
                if (ix->adverb.empty() && !ix->multiDim && ix->index) {
                    string base = vivBase(ix->base.get(), ix->isHash ? '%' : '@');
                    return (ix->isHash ? "R.hset(" : "R.aset(") + base + ", " + exArg(ix->index.get()) + ", " + exArg(a->value.get()) + ")";
                }
            }
            LV lv = lvalue(t);
            return lvExpr(lv, lv.set(exArg(a->value.get())));
        }
        // compound: op=
        string bop = op.substr(0, op.size() - 1);
        LV lv = lvalue(t);
        if (bop == "//") { string x = tmp(); return lvExpr(lv, "R.defined(" + x + " = " + lv.get + ") ? " + x + " : " + lv.set(exArg(a->value.get()))); }
        if (bop == "||") { string x = tmp(); return lvExpr(lv, "R.truthy(" + x + " = " + lv.get + ") ? " + x + " : " + lv.set(exArg(a->value.get()))); }
        if (bop == "&&") { string x = tmp(); return lvExpr(lv, "R.truthy(" + x + " = " + lv.get + ") ? " + lv.set(exArg(a->value.get())) + " : " + x); }
        if (bop == ",") return lvExpr(lv, lv.set("R.listAppendAssign(" + lv.get + ", " + exArg(a->value.get()) + ")"));
        if (bop == "xx") return lvExpr(lv, lv.set("R.listRepeat(" + lv.get + ", " + exArg(a->value.get()) + ")"));
        if (bop == "Z" || bop == "X") return lvExpr(lv, lv.set("R.OPS[" + jsStr(bop) + "](" + lv.get + ", " + exArg(a->value.get()) + ")"));
        if (bop == "~~") refuse("~~= assignment", a->line);
        return lvExpr(lv, lv.set(binop(bop, lv.get, exArg(a->value.get()), a->line)));
    }
    // ($a, $b) = ... / my ($a, @rest) = ...
    string listAssign(ListExpr* l, Expr* value) {
        string arr = tmp();
        string r = "(" + arr + " = R.arr(" + listSource(value) + ")";
        size_t i = 0;
        for (auto& it : l->items) {
            Expr* t = it.get();
            if (t->kind == NK::VarExpr && static_cast<VarExpr*>(t)->name[0] == '@') {
                r += ", " + lvalue(t).set("R.mkArray(" + arr + ".slice(" + std::to_string(i) + "))") ; i = 1000000; continue;
            }
            if (t->kind == NK::VarExpr && static_cast<VarExpr*>(t)->name[0] == '%') {
                r += ", " + lvalue(t).set("R.newHash(R.mkList(" + arr + ".slice(" + std::to_string(i) + ")))"); i = 1000000; continue;
            }
            if (t->kind == NK::Whatever || (t->kind == NK::VarExpr && (static_cast<VarExpr*>(t)->name == "$" || static_cast<VarExpr*>(t)->name == "*"))) { i++; continue; }
            string val = arr + "[" + std::to_string(i) + "] ?? R.Any";
            if (t->kind == NK::VarExpr && static_cast<VarExpr*>(t)->declare) r += ", " + declAssign(static_cast<VarExpr*>(t), nullptr, true, val);
            else r += ", " + lvalue(t).set(val);
            i++;
        }
        return r + ", R.mkList(" + arr + "))";
    }
    // `my $x = v` when the declaration was hoisted (let emitted earlier): plain assignment with container semantics
    string declAssign(VarExpr* v, Expr* value, bool hoisted, const string& preVal = "") {
        string name = mangleVar(v->name);
        char sig = v->name[0];
        string val = preVal.empty() ? (value ? listSource(value) : "") : preVal;
        if (sig == '@') return name + " = R.newArray(" + val + ")";
        if (sig == '%') return name + " = R.newHash(" + val + ")";
        if (val.empty()) val = declDefault(v);
        else if (value) val = exArg(value);
        return name + " = " + val;
    }
    string declDefault(VarExpr* v) {
        char sig = v->name[0];
        if (v->declDefault && (sig == '@' || sig == '%')) return "R.withDefault(" + string(sig == '@' ? "R.mkArray([])" : "R.mkHash()") + ", " + exArg(v->declDefault.get()) + ")";
        if (v->declDefault) return exArg(v->declDefault.get());
        if ((sig == '@' || sig == '%') && !v->declType.empty()) return "R.withOf(" + string(sig == '@' ? "R.mkArray([])" : "R.mkHash()") + ", " + typeObj(v->declType, v->line) + ")";
        if (sig == '@') return "R.mkArray([])";
        if (sig == '%') return "R.mkHash()";
        if (!v->declType.empty()) return typeObj(v->declType, v->line);
        return "R.Any";
    }
    string typeObj(const string& name, int lineNo) {
        if (classNames.count(name) || subsetNames.count(name)) return mangleType(name);
        string base = name; size_t c = base.find(':'); if (c != string::npos && base.compare(c, 2, "::") != 0) base = base.substr(0, c);
        if (kCoreTypes.count(base)) return jsIdent(base) ? "R.T." + base : "R.T[" + jsStr(base) + "]";
        if (base.rfind("X::", 0) == 0) return "R.mkExType(" + jsStr(base) + ")";
        return "R.Any";
    }

    // ---------------------------------------------------------------- unary --
    string unary(Unary* u) {
        const string& op = u->op;
        Expr* x = u->operand.get();
        if (op.size() >= 3 && op.front() == '[' && op.back() == ']') {
            string inner = op.substr(1, op.size() - 2);
            bool tri = !inner.empty() && inner[0] == '\\';
            if (tri) inner = inner.substr(1);
            return "R.reduceOp(" + jsStr(inner) + ", " + exArg(x) + (tri ? ", true" : "") + ")";
        }
        if (op == "do") return doBlock(x, false);
        if (op == "try") return doBlock(x, true);
        if (op == "gather") return gatherExpr(x, u->line);
        if (op == "next" || op == "last" || op == "redo") {
            if (x) refuse("labelled/valued loop control in expression position", u->line);
            return "(" + loopCtl(op, "", u->line, true) + ")";
        }
        if (op == "quietly") { if (x->kind == NK::BlockExpr) return doBlock(x, false); return exArg(x); }
        if (op == "capture") { if (x->kind == NK::ListExpr) return "R.capture(" + args(static_cast<ListExpr*>(x)->items) + ")"; std::vector<ExprPtr> one; return "R.capture(" + (isNamedArg(x) ? "R.named([[" + jsStr(static_cast<PairExpr*>(x)->key) + ", " + (static_cast<PairExpr*>(x)->value ? exArg(static_cast<PairExpr*>(x)->value.get()) : "true") + "]])" : exArg(x)) + ")"; }
        if (op == "siglit") refuse("a signature literal", u->line);
        if (op == "lazy") return "R.lazyOf(" + (x->kind == NK::BlockExpr ? doBlock(x, false) : exArg(x)) + ")";
        if (op == "eager") return "R.eagerOf(" + exArg(x) + ")";
        if (op == "sink") return "(" + exArg(x) + ", R.Nil)";
        if (op == "hyper" || op == "race") return exArg(x);
        if (op == "not") return "R.not(" + boolOperand(x) + ")";
        if (op == "so") return "R.so(" + boolOperand(x) + ")";
        if (op == "start" || op == "await") refuse("concurrency (" + op + ")", u->line);
        if (op == "temp" || op == "let") refuse("a temp/let variable", u->line);
        if (op == "postfix ...") refuse("a stub (...)", u->line);
        if (u->postfix) {
            if (op == "++" || op == "--") {
                LV lv = lvalue(x); string t = tmp();
                return lvExpr(lv, t + " = " + lv.get + ", " + lv.set(string(op == "++" ? "R.inc(" : "R.dec(") + t + ")") + ", " + t);
            }
            if (op == "i") return "R.mul(" + ex(x) + ", " + litConst("new R.RComplex(0, 1)") + ")";
            if (op == "!") return "R.factorial(" + ex(x) + ")";
            refuse("postfix " + op, u->line);
        }
        if (op == "++" || op == "--") { LV lv = lvalue(x); return lvExpr(lv, lv.set(string(op == "++" ? "R.inc(" : "R.dec(") + lv.get + ")")); }
        if (op == "!") return "!R.truthy(" + boolOperand(x) + ")";
        if (op == "?") return "R.truthy(" + boolOperand(x) + ")";
        string v = exArg(x);
        if (op == "-") return "R.neg(" + v + ")";
        if (op == "+") return "R.numify(" + v + ")";
        if (op == "~") return "R.str(" + v + ")";
        if (op == "+^") return "R.bitneg(" + v + ")";
        if (op == "?^") return "!R.truthy(" + v + ")";
        if (op == "^") return "R.upto(" + v + ")";
        if (op == "ctx%") return "R.newHash(" + v + ")";
        if (op == "ctx@") return "R.mkList(R.itemsOf(R.decont(" + v + ")).slice())";
        if (op == "ctx$") return v;
        if (op == "decont") return v;
        if (op == "|") return "R.slip(" + v + ")";
        if (op == "abs") return "R.abs(" + v + ")";
        if (op == "-.") return "R.neg(" + v + ")";
        refuse("prefix operator '" + op + "'", u->line);
    }
    // do { … } / try { … } as a value: an IIFE whose last statement is returned
    string doBlock(Expr* x, bool isTry) {
        string body;
        if (x->kind == NK::BlockExpr) {
            auto* be = static_cast<BlockExpr*>(x);
            body = fnBody(false, [&]() { emitStmts(be->body, 2, true); }, 2);
        } else {
            body = fnBody(false, [&]() { line(2, "return " + exArg(x) + ";"); }, 2);
        }
        bool isAsync = x->kind == NK::BlockExpr ? stmtsAwait(static_cast<BlockExpr*>(x)->body) : awaitsIn(x);
        string pre = isAsync ? "(await (async () => {" : "(() => {";
        string post = isAsync ? "})())" : "})()";
        if (!isTry) return pre + "\n" + body + "    " + post;
        fn().usesBang = true;
        string inner = (isAsync ? "await (async () => {\n" : "(() => {\n") + body + "    })()";
        return pre + " v__bang = R.Nil; try { const _r = " + inner + "; if (R.isFailure(_r)) { v__bang = _r.err; return R.Nil; } return _r; } catch (_e) { if (R.isControl(_e)) throw _e; v__bang = R.exc(_e); return R.Nil; } " + post;
    }
    string gatherExpr(Expr* x, int lineNo) {
        if (x->kind != NK::BlockExpr) refuse("gather without a block", lineNo);
        auto* be = static_cast<BlockExpr*>(x);
        // every `take` lexically inside (no closure between) → a generator
        bool insideClosure = stmtsContain(be->body, [](Node* n) { return isClosureNode(n) && contains(n, isTakeNode, true); }, false);
        // a take inside a nested closure reached through a loop is still "inside"; the walker above already treats loops as transparent
        if (!insideClosure) {
            int saved = genFnDepth; genFnDepth = fnDepth() + 1;
            string body = fnBody(false, [&]() { emitStmts(be->body, 2, false); }, 2);
            genFnDepth = saved;
            return "R.gather(function* () {\n" + body + "    })";
        }
        string body = fnBody(false, [&]() { emitStmts(be->body, 2, false); }, 2);
        return "R.gatherEager(() => {\n" + body + "    })";
    }
    int genFnDepth = -1;                 // fnDepth of the generator a `take` may yield from

    // --------------------------------------------------------------- binary --
    string binary(Binary* b) {
        const string& op = b->op;
        if (op == "=:=") {   // container identity: a scalar is only ever =:= itself
            Expr* l = b->lhs.get(); Expr* r = b->rhs.get();
            bool lv = l->kind == NK::VarExpr, rv = r->kind == NK::VarExpr;
            if (lv && rv && static_cast<VarExpr*>(l)->name == static_cast<VarExpr*>(r)->name) return "true";
            auto scalar = [](Expr* e) { return e->kind == NK::VarExpr && static_cast<VarExpr*>(e)->name[0] == '$' && static_cast<VarExpr*>(e)->name != "$_"; };
            if (scalar(l) || scalar(r)) return "false";
        }
        if ((op == "~~" || op == "!~~") && b->rhs->kind == NK::RegexLit) {
            fn().usesSlash = true;
            string m = slashSet("R.rxMatch(" + exArg(b->lhs.get()) + ", " + regexObject(static_cast<RegexLit*>(b->rhs.get())) + ")");
            return op == "~~" ? m : "!R.truthy(" + m + ")";
        }
        if (op == "~~" && b->rhs->kind == NK::SubstLit) return substExpr(static_cast<SubstLit*>(b->rhs.get()), b->lhs.get());
        if ((op == "~~" || op == "!~~") && b->rhs->kind != NK::NameTerm && b->rhs->kind != NK::StrLit && b->rhs->kind != NK::IntLit && b->rhs->kind != NK::NumLit) {
            // a Regex on the right (a variable, &name, a grammar) sets $/ like a literal does
            fn().usesSlash = true;
            if (contains(b->rhs.get(), [](Node* n) { return isVarNamed(n, "$_"); }, false)) {   // the right side reads $_: it is the left side there
                string nt = newTopic(); topics.push_back(nt);
                string f = "(" + nt + ") => " + exArg(b->rhs.get());
                topics.pop_back();
                string m = "R.smartmatchWith(" + exArg(b->lhs.get()) + ", " + f + ")";
                return op == "~~" ? m : "!R.truthy(" + m + ")";
            }
            string l = tmp(), r = tmp(), u = tmp();
            string m = "(" + l + " = " + exArg(b->lhs.get()) + ", " + r + " = " + exArg(b->rhs.get()) + ", " + u + " = R.smartmatch(" + l + ", " + r + "), " + (fn().slashParam ? u : "R.isRegex(" + r + ") ? (v__slash = " + u + ") : " + u) + ")";
            return op == "~~" ? m : "!R.truthy(" + m + ")";
        }
        if (op == "xx") {
            string n = b->rhs->kind == NK::Whatever ? "R.Whatever" : exArg(b->rhs.get());
            Expr* l = b->lhs.get();
            bool thunk = !(l->kind == NK::IntLit || l->kind == NK::NumLit || l->kind == NK::StrLit || l->kind == NK::BoolLit || l->kind == NK::VarExpr);
            if (thunk) return "R.xxThunk(() => " + exArg(l) + ", " + n + ")";
            return "R.listRepeat(" + exArg(l) + ", " + n + ")";
        }
        if (op == "..." || op == "...^" || op == "^..." || op == "^...^") {
            if (op[0] == '^') refuse("a ^... sequence", b->line);
            // seeds: a comma list on the left is the seed list
            string seeds;
            Expr* l = b->lhs.get();
            if (l->kind == NK::ListExpr) seeds = "[" + listItems(static_cast<ListExpr*>(l)->items) + "]";
            else if (l->kind == NK::Binary && (static_cast<Binary*>(l)->op == "..." || static_cast<Binary*>(l)->op == "...^")) refuse("a chained sequence", b->line);
            else seeds = "[" + exArg(l) + "]";
            Expr* r = b->rhs.get();
            string endv = r->kind == NK::Whatever ? "R.Whatever" : exArg(r);
            if (r->kind == NK::ListExpr) refuse("a sequence with a list on the right", b->line);
            return "R.seqOp(" + seeds + ", " + endv + ", " + (op == "...^" ? "true" : "false") + ")";
        }
        if (op == "R//") { string t = tmp(); return "(R.defined(" + t + " = " + exArg(b->rhs.get()) + ") ? " + t + " : " + exArg(b->lhs.get()) + ")"; }   // the reversed defined-or, still lazy
        if (op == "R||" || op == "Ror") { string t = tmp(); return "(R.truthy(" + t + " = " + exArg(b->rhs.get()) + ") ? " + t + " : " + exArg(b->lhs.get()) + ")"; }
        if (op == "R&&" || op == "Rand") { string t = tmp(); return "(R.truthy(" + t + " = " + exArg(b->rhs.get()) + ") ? " + exArg(b->lhs.get()) + " : " + t + ")"; }
        if (op.size() > 1 && op[0] == 'R' && !ascii::isalnum((unsigned char)op[1])) return binop(op.substr(1), exArg(b->rhs.get()), exArg(b->lhs.get()), b->line);
        if (op.size() > 1 && op[0] == 'Z' && op != "Z") return "R.zipOp(" + jsStr(op.substr(1)) + ", " + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op.size() > 1 && op[0] == 'X' && op != "X" && op != "Xor" && !ascii::isalpha((unsigned char)op[1])) return "R.crossOp(" + jsStr(op.substr(1)) + ", " + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op.size() > 4 && (op.rfind(">>", 0) == 0 || op.rfind("<<", 0) == 0 || op.rfind("\xc2\xbb", 0) == 0 || op.rfind("\xc2\xab", 0) == 0)) {
            // hyper: >>op<<, <<op>>, >>op>>, <<op<<
            string t = op; bool dl = false, dr = false;
            if (t.rfind(">>", 0) == 0) { t = t.substr(2); dl = false; } else if (t.rfind("<<", 0) == 0) { t = t.substr(2); dl = true; }
            else if (t.rfind("\xc2\xbb", 0) == 0) { t = t.substr(2); dl = false; } else if (t.rfind("\xc2\xab", 0) == 0) { t = t.substr(2); dl = true; }
            if (t.size() >= 2 && t.compare(t.size() - 2, 2, ">>") == 0) { dr = true; t = t.substr(0, t.size() - 2); }
            else if (t.size() >= 2 && t.compare(t.size() - 2, 2, "<<") == 0) { dr = false; t = t.substr(0, t.size() - 2); }
            else if (t.size() >= 2 && t.compare(t.size() - 2, 2, "\xc2\xbb") == 0) { dr = true; t = t.substr(0, t.size() - 2); }
            else if (t.size() >= 2 && t.compare(t.size() - 2, 2, "\xc2\xab") == 0) { dr = false; t = t.substr(0, t.size() - 2); }
            if (t.size() > 1 && t.back() == '=' && t != "==" && t != "<=" && t != ">=" && t != "!=" && t != "eq" && t != "ne" && t != "le" && t != "ge") {
                LV lv = lvalue(b->lhs.get());
                return lvExpr(lv, lv.set("R.hyperOp(" + jsStr(t.substr(0, t.size() - 1)) + ", " + lv.get + ", " + exArg(b->rhs.get()) + ", " + (dl ? "true" : "false") + ", " + (dr ? "true" : "false") + ")"));
            }
            return "R.hyperOp(" + jsStr(t) + ", " + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ", " + (dl ? "true" : "false") + ", " + (dr ? "true" : "false") + ")";
        }
        if (op == "&&" || op == "and") { string t = tmp(); return "(R.truthy(" + t + " = " + boolOperand(b->lhs.get()) + ") ? " + exArg(b->rhs.get()) + " : " + t + ")"; }
        if (op == "||" || op == "or") { string t = tmp(); return "(R.truthy(" + t + " = " + boolOperand(b->lhs.get()) + ") ? " + t + " : " + exArg(b->rhs.get()) + ")"; }
        if (op == "//") { string t = tmp(); return "(R.defined(" + t + " = " + exArg(b->lhs.get()) + ") ? " + t + " : " + exArg(b->rhs.get()) + ")"; }
        if (op == "andthen") { string t = tmp(); return "(R.defined(" + t + " = " + exArg(b->lhs.get()) + ") ? " + withTopic(t, b->rhs.get()) + " : " + t + ")"; }
        if (op == "orelse") { string t = tmp(); return "(R.defined(" + t + " = " + exArg(b->lhs.get()) + ") ? " + t + " : " + withTopic(t, b->rhs.get()) + ")"; }
        if (op == "notandthen") { string t = tmp(); return "(R.defined(" + t + " = " + exArg(b->lhs.get()) + ") ? " + t + " : " + withTopic(t, b->rhs.get()) + ")"; }
        if (op == "!~~") return "!R.smartmatch(" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op == "~~") {
            Expr* r = b->rhs.get();
            if (r->kind == NK::BlockExpr) return "R.truthy(" + blockClosure(static_cast<BlockExpr*>(r)) + "(" + exArg(b->lhs.get()) + "))";
            return "R.smartmatch(" + exArg(b->lhs.get()) + ", " + exArg(r) + ")";
        }
        if (op == ",") return "R.list(" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op == "|") return "R.junction(\"any\", [" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + "])";
        if (op == "&") return "R.junction(\"all\", [" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + "])";
        if (op == "^") return "R.junction(\"one\", [" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + "])";
        if (op == ":=" || op == "::=") refuse("binding (:=) in expression position", b->line);
        if (op == "=>") { Expr* l = b->lhs.get(); string k = l->kind == NK::NameTerm ? jsStr(static_cast<NameTerm*>(l)->name) : exArg(l); return "R.pair(" + k + ", " + exArg(b->rhs.get()) + ")"; }
        if (op == "but" || op == "does") refuse("the " + op + " operator", b->line);
        if (op == "o" || op == "\xe2\x88\x98") return "R.OPS.o(" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op == "..") return "R.range(" + rangeEnd(b->lhs.get()) + ", " + rangeEnd(b->rhs.get()) + ")";
        if (op == "..^") return "R.range(" + rangeEnd(b->lhs.get()) + ", " + rangeEnd(b->rhs.get()) + ", false, true)";
        if (op == "^..") return "R.range(" + rangeEnd(b->lhs.get()) + ", " + rangeEnd(b->rhs.get()) + ", true, false)";
        if (op == "^..^") return "R.range(" + rangeEnd(b->lhs.get()) + ", " + rangeEnd(b->rhs.get()) + ", true, true)";
        if (op == "min" || op == "max") return "R.OPS[" + jsStr(op) + "](" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op == "!==" ) return "!R.numeq(" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op == "!eq") return "!R.seq(" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op == "!=:=") return "!R.identical(" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op == "!eqv") return "!R.eqv(" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op == "%%") return "R.numeq(R.mod(" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + "), 0)";
        if (op == "!%%") return "!R.numeq(R.mod(" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + "), 0)";
        if (op == "=~=") return "R.approxEq(" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op == "=>") return "R.pair(" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op == "(elem)" || op == "\xe2\x88\x88") return "R.elem(" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op == "!(elem)" || op == "\xe2\x88\x89") return "!R.elem(" + exArg(b->lhs.get()) + ", " + exArg(b->rhs.get()) + ")";
        if (op == "\xe2\x88\x8b" || op == "(cont)") return "R.elem(" + exArg(b->rhs.get()) + ", " + exArg(b->lhs.get()) + ")";
        if (op == "!(cont)" || op == "\xe2\x88\x8c") return "!R.elem(" + exArg(b->rhs.get()) + ", " + exArg(b->lhs.get()) + ")";
        return binop(op, exArg(b->lhs.get()), exArg(b->rhs.get()), b->line);
    }
    // `andthen`'s right side sees the left value as $_
    string withTopic(const string& t, Expr* rhs) {
        topics.push_back(t);
        string r = rhs->kind == NK::BlockExpr ? blockClosure(static_cast<BlockExpr*>(rhs)) + "(" + t + ")" : exArg(rhs);
        topics.pop_back();
        return r;
    }

    // --------------------------------------------------------------- regexes --
    // The pattern is parsed at transpile time with the engine's own parser; the
    // tree goes into the output as a JS literal the runtime's matcher interprets.
    // Embedded code, `$var` atoms and `<name(args)>` become closures over the
    // enclosing JS scope, with the cursor match as `$/`.
    bool substSlash = false;   // the next implicit-topic block is a .subst replacement: its $/ is the match
    // `@array` bare in a pattern is an alternation of its elements: spell it as the <@array> assertion the tree carries
    static string liftArrayAtoms(const string& p) {
        string o; char q = 0; int cls = 0, code = 0, angle = 0;
        for (size_t i = 0; i < p.size(); i++) {
            char c = p[i];
            if (q) { o += c; if (c == q) q = 0; else if (c == '\\' && i + 1 < p.size()) o += p[++i]; continue; }
            if (c == '\\' && i + 1 < p.size()) { o += c; o += p[++i]; continue; }
            if (c == '\'' || c == '"') { q = c; o += c; continue; }
            if (c == '<' && i + 1 < p.size() && (p[i + 1] == '[' || (p[i + 1] == '-' && i + 2 < p.size() && p[i + 2] == '['))) cls++;
            else if (c == '<' && !cls) angle++;
            if (c == ']' && i + 1 < p.size() && p[i + 1] == '>' && cls) cls--;
            else if (c == '>' && !cls && angle) angle--;
            if (c == '{') code++; else if (c == '}' && code) code--;
            if (c == '@' && !cls && !code && !angle && i + 1 < p.size() && (ascii::isalpha((unsigned char)p[i + 1]) || p[i + 1] == '_') && (i == 0 || p[i - 1] != '<')) {
                size_t j = i + 1; while (j < p.size() && (ascii::isalnum((unsigned char)p[j]) || p[j] == '_' || (p[j] == '-' && j + 1 < p.size() && ascii::isalpha((unsigned char)p[j + 1])))) j++;
                o += "<@" + p.substr(i, j - i) + ">"; i = j - 1; continue;   // <@@name>: the tree marks it a literal alternation
            }
            o += c;
        }
        return o;
    }
    string compileRegex(const string& pattern, const string& flags, int lineNo) {
        string pat = liftArrayAtoms(pattern);
        string adv;   // match-level adverbs the pattern carries as leading `:name ` tokens
        auto strip = [&](const string& tok, const string& key) { size_t p = pat.find(tok); if (p != string::npos) { pat.erase(p, tok.size()); adv += (adv.empty() ? "" : ", ") + key; } };
        strip(":g ", "g: 1"); strip(":global ", "g: 1"); strip(":ex ", "ex: 1"); strip(":exhaustive ", "ex: 1"); strip(":ov ", "ov: 1"); strip(":overlap ", "ov: 1");
        auto takeArg = [&](const string& tok) -> string {   // `:nth(EXPR) ` → EXPR, removed from the pattern
            size_t p = pat.find(tok); if (p == string::npos) return "";
            size_t i = p + tok.size(); int depth = 1;
            while (i < pat.size() && depth) { if (pat[i] == '(') depth++; else if (pat[i] == ')') depth--; i++; }
            string text = pat.substr(p + tok.size(), i - 1 - (p + tok.size()));
            if (i < pat.size() && pat[i] == ' ') i++;
            pat.erase(p, i - p);
            return text.empty() ? "1" : text;
        };
        { string t = takeArg(":nth("); if (!t.empty()) adv += (adv.empty() ? "" : ", ") + string("nth: ") + embedRegexCode("range", t, lineNo); }
        { string t = takeArg(":x("); if (!t.empty()) adv += (adv.empty() ? "" : ", ") + string("x: ") + embedRegexCode("range", t, lineNo); }
        for (size_t p = pat.find(':'); p != string::npos; p = pat.find(':', p + 1)) {   // `:3rd ` and friends
            size_t i = p + 1; while (i < pat.size() && ascii::isdigit((unsigned char)pat[i])) i++;
            if (i == p + 1 || i + 2 >= pat.size() + 0 || i + 2 > pat.size()) continue;
            string suf = pat.substr(i, 2);
            if ((suf == "st" || suf == "nd" || suf == "rd" || suf == "th") && (i + 2 == pat.size() || pat[i + 2] == ' ')) {
                adv += (adv.empty() ? "" : ", ") + string("nth: () => ") + pat.substr(p + 1, i - p - 1);
                pat.erase(p, i + 2 - p + (i + 2 < pat.size() ? 1 : 0)); break;
            }
        }
        if (pat.find('\x01') != string::npos) refuse("a regex with an interpolated sub-pattern", lineNo);
        Regex re(pat, flags);
        if (!re.ok()) refuse("a regex the engine could not parse: /" + pat + "/", lineNo);
        if (!re.obsolete().empty()) refuse("an obsolete regex construct " + re.obsolete(), lineNo);
        bool pure = true;
        string tree = re.toJsTree([&](const string& kind, const string& text) -> string {
            pure = false;
            return embedRegexCode(kind, text, lineNo);
        });
        if (tree.find("{k:\"CondRef\"") != string::npos) refuse("a Perl 5 conditional group (?(N)…)", lineNo);
        if (pat.find(":P5 ") != string::npos || pat.find(":Perl5 ") != string::npos) {   // the interpreter splices a variable's text into a P5 pattern; the tree cannot
            for (size_t p = pat.find('$'); p != string::npos; p = pat.find('$', p + 1)) if (p + 1 < pat.size() && (ascii::isalpha((unsigned char)pat[p + 1]) || pat[p + 1] == '_') && (p == 0 || pat[p - 1] != '\\')) refuse("a variable interpolated into a Perl 5 regex", lineNo);
        }
        string obj = "R.rx(" + tree + ", " + (adv.empty() ? "null" : "{ " + adv + " }") + ", " + jsStr(pattern) + ")";   // the source text is the gist
        return pure ? litConst(obj) : obj;
    }
    string regexObject(RegexLit* r) {
        string flags = r->declKind == "token" ? "r" : r->declKind == "rule" ? "sr" : "";
        return compileRegex(r->pattern, flags, r->line);
    }
    // Raku text inside a regex, as a closure. kind: "run" (a `{…}` block), "assert"
    // (`<?{…}>`), "var" (`$x`), "args" (`<name(args)>`), "range" (`** {…}`).
    string embedRegexCode(const string& kind, const string& text, int lineNo) {
        if (kind == "var") {
            if (text.size() > 2 && (text[1] == '<' || ascii::isdigit((unsigned char)text[1]))) return "null";   // a backreference: the matcher resolves it
            if (text.size() < 2) refuse("a placeholder variable inside a regex", lineNo);
            VarExpr v(text);
            return "() => " + varRef(&v);
        }
        Program prog;
        try {
            Lexer lx(kind == "args" ? "(" + text + ",)" : text);
            Parser ps(lx.tokenize());
            prog = ps.parseProgram();
        } catch (const ParseError& e) {
            refuse("regex code the parser refused: " + text, lineNo);
        }
        if (kind == "run") {
            {   // `:my $x` declared in one block is read by the pattern's other closures: hoist it to the enclosing routine
                std::vector<VarExpr*> ds;
                for (auto& st : prog.stmts) { collectExprDecls(st.get(), ds); if (st->kind == NK::VarDecl) for (auto& nm : static_cast<VarDecl*>(st.get())->names) { fn().declared.push_back("let " + mangleVar(nm) + " = R.Any;"); predeclared.insert(mangleVar(nm)); } }
                for (auto* v : ds) if (v->declScope != "state" && v->declScope != "constant" && mangleVar(v->name) != "_anon") { fn().declared.push_back("let " + mangleVar(v->name) + emptyFor(v->name) + ";"); predeclared.insert(mangleVar(v->name)); }
            }
            string body = fnBody(false, [&]() { fn().slashParam = true; fn().usesSlash = true; emitStmts(prog.stmts, 2, false); }, 2);
            return "(v__slash) => {\n" + body + "    }";
        }
        if (prog.stmts.size() != 1 || prog.stmts[0]->kind != NK::ExprStmt) refuse("regex code that is not one expression: " + text, lineNo);
        Expr* e = static_cast<ExprStmt*>(prog.stmts[0].get())->e.get();
        string body = fnBody(false, [&]() {
            fn().slashParam = true; fn().usesSlash = true;
            if (kind == "assert") line(2, "return R.truthy(" + exArg(e) + ");");
            else if (kind == "args") line(2, "return R.arr(" + exArg(e) + ");");
            else line(2, "return " + exArg(e) + ";");
        }, 2);
        return "(v__slash) => {\n" + body + "    }";
    }
    // s/pat/repl/ on `target` (null = $_): the replacement is an interpolating
    // string evaluated with the match as $/; the result is the Match (or their
    // list under :g), and the target is assigned the new string.
    string substExpr(SubstLit* s, Expr* target) {
        string rx = compileRegex(s->pattern, "", s->line);
        Program prog;
        try {
            // a delimiter the replacement does not contain (`qq{{…}}` would read as the doubled-delimiter form)
            // `$0.tc()` / `$<x>.uc()` / `$/.Str()` in a replacement call the method (the interpreter's
            // replacement rule; a plain string would not): spell them as `{…}` interpolations
            string repl;
            for (size_t i = 0; i < s->repl.size(); i++) {
                if (s->repl[i] == '$' && i + 1 < s->repl.size() && (ascii::isdigit((unsigned char)s->repl[i + 1]) || s->repl[i + 1] == '<' || s->repl[i + 1] == '/')) {
                    size_t j = i + 1;
                    if (s->repl[j] == '<') { j = s->repl.find('>', j); if (j == string::npos) { repl += s->repl[i]; continue; } j++; }
                    else if (s->repl[j] == '/') j++;
                    else while (j < s->repl.size() && ascii::isdigit((unsigned char)s->repl[j])) j++;
                    size_t k = j; bool calls = false;
                    while (k + 1 < s->repl.size() && s->repl[k] == '.' && (ascii::isalpha((unsigned char)s->repl[k + 1]) || s->repl[k + 1] == '_')) {
                        size_t e = k + 1; while (e < s->repl.size() && (ascii::isalnum((unsigned char)s->repl[e]) || s->repl[e] == '_' || s->repl[e] == '-')) e++;
                        if (e < s->repl.size() && s->repl[e] == '(') { int d = 0; do { if (s->repl[e] == '(') d++; else if (s->repl[e] == ')') d--; e++; } while (e < s->repl.size() && d); k = e; calls = true; }
                        else break;
                    }
                    if (calls) { repl += "{" + s->repl.substr(i, k - i) + "}"; i = k - 1; continue; }
                }
                repl += s->repl[i];
            }
            string open = "{", close = "}";   // braces, unless the text starts with one (`qq{{…}}` is the doubled-delimiter form)
            if (!repl.empty() && (repl[0] == '{' || repl.back() == '}')) { for (const char* c : { "/", "|", "!", "~", "^", "," }) if (repl.find(c) == string::npos) { open = close = c; break; } }
            Lexer lx("qq" + open + repl + close);
            Parser ps(lx.tokenize());
            prog = ps.parseProgram();
        } catch (const ParseError& e) {
            refuse("a substitution replacement the parser refused: " + s->repl, s->line);
        }
        if (prog.stmts.size() != 1 || prog.stmts[0]->kind != NK::ExprStmt) refuse("a substitution replacement of this form", s->line);
        Expr* e = static_cast<ExprStmt*>(prog.stmts[0].get())->e.get();
        string replFn = "(v__slash) => {\n" + fnBody(false, [&]() { fn().slashParam = true; fn().usesSlash = true; line(2, "return R.str(" + exArg(e) + ");"); }, 2) + "    }";
        fn().usesSlash = true;
        string t = tmp();
        if (s->nonMut) return "(" + t + " = R.rxSubst(" + (target ? exArg(target) : topic()) + ", " + rx + ", " + replFn + "), " + slashAssign(t + ".m") + ", " + t + ".s)";
        LV lv = target ? lvalue(target) : lvalue(topicLv());
        return lvExpr(lv, t + " = R.rxSubst(" + lv.get + ", " + rx + ", " + replFn + "), " + lv.set(t + ".s") + ", " + slashAssign(t + ".m"));
    }
    // `$_` as an assignment target
    VarExpr topicVar{"$_"};
    Expr* topicLv() { return &topicVar; }

    // ----------------------------------------------------------------- calls --
    string call(Call* c) {
        if (c->callee) {
            string callee = ex(c->callee.get());
            return "R.callCode(" + callee + (c->args.empty() ? "" : ", " + args(c->args)) + ")";
        }
        const string& name = c->name;
        if (name == "await") { if (c->args.size() != 1) refuse("await with " + std::to_string(c->args.size()) + " arguments", c->line); return "(await R.awaitP(" + exArg(c->args[0].get()) + "))"; }
        if (name == "sleep") return "(await R.sleepP(" + (c->args.empty() ? "undefined" : exArg(c->args[0].get())) + "))";   // the event loop runs meanwhile
        if ((name == "react" || name == "supply") && c->args.size() == 1 && c->args[0]->kind == NK::BlockExpr) {
            string body = blockClosure(static_cast<BlockExpr*>(c->args[0].get()));
            return name == "react" ? "(await R.awaitP(R.react(" + body + ")))" : "R.supplyBlock(" + body + ")";
        }
        if (name == "whenever" && c->args.size() == 2 && c->args[1]->kind == NK::BlockExpr) {
            auto* be = static_cast<BlockExpr*>(c->args[1].get());
            string phasers;
            for (auto& st : be->body) {   // LAST / QUIT phasers are the tap's done and quit
                if (st->kind != NK::Block) continue;
                auto* b = static_cast<Block*>(st.get());
                if (b->phaser == "LAST" || b->phaser == "QUIT") {
                    string body = fnBody(false, [&]() { if (b->phaser == "QUIT") { string nt = newTopic(); topics.push_back(nt); line(2, "let " + nt + " = _e;"); } emitStmts(b->stmts, 2, false); if (b->phaser == "QUIT") topics.pop_back(); }, 2);
                    phasers += (phasers.empty() ? "" : ", ") + string(b->phaser == "LAST" ? "last: () => {\n" : "quit: (_e) => {\n") + body + "    }";
                }
            }
            return "R.whenever(" + exArg(c->args[0].get()) + ", " + blockClosure(be) + (phasers.empty() ? "" : ", { " + phasers + " }") + ")";
        }
        if (name == "emit" && c->args.size() == 1) return "R.emit(" + exArg(c->args[0].get()) + ")";
        if (name == "done" && c->args.empty()) return "R.done()";
        if (name == "start") {
            if (c->args.size() != 1) refuse("start with " + std::to_string(c->args.size()) + " arguments", c->line);
            Expr* a = c->args[0].get();
            if (a->kind == NK::BlockExpr) return "R.start(" + blockClosure(static_cast<BlockExpr*>(a)) + ")";
            return "R.start(() => " + exArg(a) + ")";
        }
        if (name == "make" && c->args.size() == 1) { fn().usesSlash = true; return "R.make(v__slash, " + exArg(c->args[0].get()) + ")"; }
        if (name == "take" || name == "take-rw") {
            if (c->args.size() != 1) refuse("take with " + std::to_string(c->args.size()) + " arguments", c->line);
            if (genFnDepth == fnDepth()) return "(yield " + exArg(c->args[0].get()) + ")";
            return "R.take(" + exArg(c->args[0].get()) + ")";
        }
        if (name == "return" ) refuse("return as a call", c->line);
        if (name == "callsame" || name == "nextsame" || name == "callwith" || name == "nextwith" || name == "samewith") refuse(name, c->line);
        if (name == "EVAL" && jsInterop && c->args.size() == 2 && c->args[0]->kind == NK::StrLit && isNamedArg(c->args[1].get())) {
            auto* p = static_cast<PairExpr*>(c->args[1].get());
            if (p->key == "lang" && p->value && p->value->kind == NK::StrLit && static_cast<StrLit*>(p->value.get())->v == "JavaScript")
                return "R.jsEval((() => { return (" + static_cast<StrLit*>(c->args[0].get())->v + "); })())";   // the literal, verbatim
            if (p->key == "lang" && p->value && p->value->kind == NK::ArrayLit) {
                auto* al = static_cast<ArrayLit*>(p->value.get());
                if (al->items.size() == 1 && al->items[0]->kind == NK::StrLit && static_cast<StrLit*>(al->items[0].get())->v == "JavaScript")
                    return "R.jsEval((() => { return (" + static_cast<StrLit*>(c->args[0].get())->v + "); })())";
            }
        }
        if (name == "EVAL" || name == "EVALFILE" || name == "require") refuse("EVAL", c->line);
        if (name == "proceed" || name == "succeed") refuse(name, c->line);
        if (name == "sprintf" && !c->args.empty()) return "R.sprintf(" + args(c->args) + ")";
        auto si = subs.find(name);
        if (si != subs.end() && subVisible(name)) {
            SubInfo& info = si->second;
            string aw = asyncSubs.count(name) ? "await " : "";
            if (!info.isMulti && !info.rwIdx.empty()) return "(" + aw + rwCall(name, info, c) + ")";
            return "(" + aw + mangleSub(name) + "(" + args(c->args) + "))";
        }
        if (codeVars.count(name)) return "R.callCode(" + mangleVar("&" + name) + (c->args.empty() ? "" : ", " + args(c->args)) + ")";
        if (classNames.count(name) || subsetNames.count(name) || kCoreTypes.count(name)) {   // Int(…) coercion call
            if (c->args.size() == 1) return "R.coerce(" + typeObj(name, c->line) + ", " + exArg(c->args[0].get()) + ")";
            refuse("a coercion call " + name + "(…)", c->line);
        }
        if (kBuiltins.count(name)) {
            if (name == "flat") {   // `flat $x`: the container's item is one element, as under the slurpy's single-argument rule
                bool any = false; for (auto& a : c->args) if (isItemized(a.get())) any = true;
                if (any) { string s; for (auto& a : c->args) { if (!s.empty()) s += ", "; s += isItemized(a.get()) ? "R.item(" + exArg(a.get()) + ")" : exArg(a.get()); } return "R.flat(" + s + ")"; }
            }
            // first-argument blocks: `map { … }, @a`
            return rt(name) + "(" + args(c->args) + ")";
        }
        if (name == "ok" || name == "is" || name == "plan" || name == "done-testing" || name == "nok" || name == "is-deeply" || name == "isnt" || name == "like" || name == "dies-ok" || name == "lives-ok" || name == "cmp-ok" || name == "subtest" || name == "pass" || name == "flunk" || name == "skip" || name == "todo" || name == "diag" || name == "isa-ok" || name == "throws-like" || name == "is-approx" || name == "can-ok" || name == "does-ok" || name == "use-ok" || name == "eval-dies-ok" || name == "eval-lives-ok" || name == "bail-out" || name == "skip-rest" || name == "unlike" || name == "fails-like")
            refuse("the Test module", c->line);
        auto ek = enumKeys.find(name);
        if (ek != enumKeys.end() && c->args.size() == 1) return "R.enumFromValue(" + ek->second + ".ty, " + exArg(c->args[0].get()) + ")";
        refuse("a call to '" + name + "'", c->line);
    }
    // a call to a sub with `is rw` parameters: those arguments travel as boxes
    string rwCall(const string& name, SubInfo& info, Call* c) {
        string s;
        for (size_t i = 0; i < c->args.size(); i++) {
            if (i) s += ", ";
            Expr* a = c->args[i].get();
            bool rw = std::find(info.rwIdx.begin(), info.rwIdx.end(), (int)i) != info.rwIdx.end();
            if (isNamedArg(a) || isSlip(a)) refuse("named or slipped arguments to a sub with is rw parameters", c->line);
            if (!rw) { s += exArg(a); continue; }
            if (a->kind == NK::VarExpr || a->kind == NK::Index) {
                LV lv = lvalue(a);
                s += (lv.prep.empty() ? "" : "(" + lv.prep + ", ") + "{ get v() { return " + lv.get + "; }, set v(_x) { " + lv.set("_x") + "; } }" + (lv.prep.empty() ? "" : ")");
            } else s += "new R.RScalar(" + exArg(a) + ")";
        }
        return mangleSub(name) + "(" + s + ")";
    }
    string methodCall(MethodCall* m) {
        if (!m->methodQual.empty()) refuse("a qualified method call (.Class::method)", m->line);
        if (m->methodExpr) return "R.mcDyn(" + ex(m->inv.get()) + ", " + exArg(m->methodExpr.get()) + (m->args.empty() ? "" : ", " + args(m->args)) + ")";
        string name = m->method;
        if (name == "raku" && !m->mutate && !m->hyper && m->args.empty()) {   // an itemized value renders as $(…)
            Expr* iv = m->inv.get();
            if (iv ? isItemized(iv) : (!topics.empty() && itemTopics.count(topics.back()))) return "R.mc(R.item(" + (iv ? exArg(iv) : topic()) + "), \"raku\")";
        }
        if (m->mutate) {
            LV lv = lvalue(m->inv.get());
            string t = tmp();
            string callee = "R.mc(" + t + ", " + jsStr(name) + (m->args.empty() ? "" : ", " + args(m->args)) + ")";
            if (m->inv->kind == NK::VarExpr && static_cast<VarExpr*>(m->inv.get())->declare) {
                auto* v = static_cast<VarExpr*>(m->inv.get());
                string ty = v->name[0] == '@' ? "R.mkArray([])" : v->name[0] == '%' ? "R.mkHash()" : v->declType.empty() ? "R.Any" : typeObj(v->declType, m->line);
                return "(" + mangleVar(v->name) + " = R.mc(" + ty + ", " + jsStr(name) + (m->args.empty() ? "" : ", " + args(m->args)) + "))";
            }
            return lvExpr(lv, t + " = " + lv.get + ", " + lv.set(callee));
        }
        if (name == "subst-mutate" && !m->args.empty()) {
            LV lv = lvalue(m->inv.get());
            string t = tmp();
            fn().usesSlash = true; substSlash = true;
            string a = args(m->args);
            substSlash = false;
            return lvExpr(lv, "(" + t + " = R.substMutate(" + lv.get + ", " + a + "), " + lv.set(t + ".s") + ", " + slashAssign(t + ".m") + ")");
        }
        if (m->hyper) return "R.hyperMethod(" + ex(m->inv.get()) + ", " + jsStr(name) + (m->args.empty() ? "" : ", " + args(m->args)) + ")";
        if (m->meta) return "R.meta(" + ex(m->inv.get()) + ", " + jsStr(name) + (m->args.empty() ? "" : ", " + args(m->args)) + ")";
        if (m->bang && curClass.empty()) return "R.die(" + jsStr("Private method call to '" + name + "' outside the defining class") + ")";   // refused before any lookup
        if (m->bang) name = "!" + name;
        string fnName = m->maybe ? "R.mcMaybe" : "R.mc";
        // autovivification through a subscript: %h{$k}.push(…)
        if ((name == "push" || name == "append" || name == "unshift" || name == "prepend") && m->inv->kind == NK::Index && static_cast<Index*>(m->inv.get())->adverb.empty()) {
            string base = vivBase(m->inv.get(), '@');
            return fnName + "(" + base + ", " + jsStr(name) + (m->args.empty() ? "" : ", " + args(m->args)) + ")";
        }
        if ((name == "push" || name == "append" || name == "unshift" || name == "prepend") && m->inv->kind == NK::VarExpr && static_cast<VarExpr*>(m->inv.get())->name[0] == '$' && !static_cast<VarExpr*>(m->inv.get())->declare) {
            LV lv = lvalue(m->inv.get());
            return fnName + "(" + lv.set("R.vivArray(" + lv.get + ")") + ", " + jsStr(name) + (m->args.empty() ? "" : ", " + args(m->args)) + ")";
        }
        if (name == "new" && m->inv->kind == NK::NameTerm && !m->maybe) {
            string ty = nameTerm(static_cast<NameTerm*>(m->inv.get()));
            return "R.construct(" + ty + (m->args.empty() ? "" : ", " + args(m->args)) + ")";
        }
        string inv;
        if (m->inv->kind == NK::Whatever) { if (wcArity.empty()) refuse("a method call on a bare *", m->line); inv = "_w" + std::to_string(++wcArity.back()); }
        else inv = ex(m->inv.get());
        if (name == "result" && m->args.empty()) return "(await R.awaitP(" + inv + "))";
        bool substBlock = (name == "subst" || name == "subst-mutate") && std::any_of(m->args.begin(), m->args.end(), [](const std::unique_ptr<Expr>& a) { return a->kind == NK::BlockExpr; });
        if (substBlock) { fn().usesSlash = true; substSlash = true; }
        string call = fnName + "(" + inv + ", " + jsStr(name) + (m->args.empty() ? "" : ", " + args(m->args)) + ")";
        substSlash = false;
        if (name == "parse" || name == "subparse" || name == "parsefile" || name == "match") { fn().usesSlash = true; call = slashSet(call); }
        if (isAwaitMethod(name)) return "(await R.awaitP(" + call + "))";   // .receive / .wait / a Channel's .list
        if (hasAsyncBlockArg(m->args)) return "(await R.awaitP(" + call + "))";   // .map({ $ch.receive }): the callback's promises, collected
        if (asyncMethods.count(name)) return "(await " + call + ")";
        return call;
    }

    // ------------------------------------------------------------- closures --
    // A block as a JS arrow function. Implicit `$_` blocks default their topic
    // to the enclosing one, which has a different JS name, so no self-reference.
    // A bare /…/ as the operand of if/unless/while/?/!/so/&&/|| matches $_ (and sets $/); elsewhere it is a Regex object
    // A match sets $/ — except in a routine that declares `$/` as a parameter (an
    // action method, a regex code block): there the parameter stays what it was, as
    // the interpreter keeps it.
    string slashSet(const string& e) { return fn().slashParam ? "(" + e + ")" : "(v__slash = " + e + ")"; }
    string slashAssign(const string& e) { return fn().slashParam ? e : "v__slash = " + e; }
    string boolOperand(Expr* e) {
        if (e->kind == NK::VarExpr) { const string& n = static_cast<VarExpr*>(e)->name; if (n.size() > 1 && n[0] == '$' && n[1] != '!' && n[1] != '.' && n[1] != '*' && n != "$_" && n != "$/" && n != "$!") return "R.boolOf(" + exArg(e) + ", " + topic() + ")"; }
        if (e->kind == NK::RegexLit) {
            auto* r = static_cast<RegexLit*>(e);
            if (!r->isRx && r->declKind.empty()) { fn().usesSlash = true; return slashSet("R.rxMatch(" + topic() + ", " + regexObject(r) + ")"); }
        }
        return exArg(e);
    }
    string blockClosure(BlockExpr* be) {
        const std::vector<Param>& params = be->params;
        std::vector<string> placeholders;
        if (params.empty() && !be->isPointy && !be->isSub) placeholders = computePlaceholders(be->body);
        bool implicitTopic = params.empty() && placeholders.empty() && !be->isPointy && !be->isSub;
        bool isRoutine = be->isSub;
        if (be->isMethodTerm) refuse("an anonymous method", be->line);
        string outerTopic = topic();
        string newT;
        string paramList;
        std::vector<string> declNames;
        string bindPre;
        bool slashFromTopic = false;
        if (implicitTopic) {
            newT = newTopic();
            paramList = newT + " = " + outerTopic;
            if (substSlash) { substSlash = false; slashFromTopic = true; paramList += ", _sl = v__slash"; }
        } else if (!placeholders.empty()) {
            for (size_t i = 0; i < placeholders.size(); i++) paramList += (i ? ", " : "") + mangleVar(plainName(placeholders[i]));
        }
        int savedGen = genFnDepth; genFnDepth = -1;
        bool usesArgs = isRoutine && params.empty() && stmtsContain(be->body, [](Node* n) { return isVarNamed(n, "@_"); }, false);
        string body = fnBody(isRoutine, [&]() {
            scopes.emplace_back(); declareSigillessParams(params);
            for (auto& p : params) fn().params.insert(p.name);
            struct ScopePop { std::vector<Scope>& v; ~ScopePop() { v.pop_back(); } } scopePop{ scopes };
            if (slashFromTopic) { fn().slashParam = true; fn().usesSlash = true; line(2, "let v__slash = R.isMatch(" + newT + ") ? " + newT + " : _sl;"); }
            bool bindsTopic = false;
            for (auto& p : params) if (p.name == "$/") fn().slashParam = true;
            if (usesArgs) { paramList = "..._args"; line(2, "let " + mangleVar("@_") + " = R.mkArray(R.splitArgs(_args)[0]);"); }
            if (!params.empty()) {
                // a pointy block's / anonymous sub's params: the same binder subs use
                if (simpleSig(params)) {
                    for (size_t i = 0; i < params.size(); i++) paramList += (i ? ", " : "") + mangleVar(params[i].name);
                    for (auto& p : params) if (p.sigil == '$' && (p.type.empty() ? isRoutine : (p.type == "Any" || p.type == "Any:D"))) line(2, "if (" + mangleVar(p.name) + " === R.Mu) R.notAny(" + jsStr(p.name) + ");");
                    for (auto& p : params) if (itemParam(p)) line(2, mangleVar(p.name) + " = R.item(" + mangleVar(p.name) + ");");
                }
                else { paramList = "..._args"; bindParams(params, 2, "", false); }
                for (auto& p : params) if (p.name == "$_") bindsTopic = true;
            }
            if (implicitTopic) topics.push_back(newT);
            else if (bindsTopic) topics.push_back(mangleVar("$_"));
            else if (isRoutine) { string t = newTopic(); fn().declared.push_back("let " + t + " = R.Any;"); topics.push_back(t); }
            else topics.push_back(outerTopic);
            emitStmts(be->body, 2, true);
            topics.pop_back();
        }, 2);
        genFnDepth = savedGen;
        bool isAsync = stmtsAwait(be->body);
        string f = string(isAsync ? "async " : "") + (isRoutine ? "function (" : "(") + paramList + (isRoutine ? ") {\n" : ") => {\n") + body + "    }";
        int arity = implicitTopic ? 1 : (int)(placeholders.size() ? placeholders.size() : params.size());
        if (implicitTopic || (!params.empty() && !simpleSig(params))) {
            int count = 0; for (auto& p : params) if (!p.named && !p.slurpy) count++;
            if (implicitTopic) count = 1;
            return "R.blk(" + f + ", " + std::to_string(arity) + ", " + std::to_string(count) + ")";
        }
        return f;
    }

    // ---------------------------------------------------------- statements --
    // `next`/`last`/`redo`: JS control when the loop is in this function, else a thrown signal
    string loopCtl(const string& op, const string& target, int lineNo, bool asExpr) {
        LoopCtx* lp = nullptr;
        for (int i = (int)loops.size() - 1; i >= 0; i--) {
            if (target.empty() || loops[i].rakuLabel == target) { lp = &loops[i]; break; }
        }
        if (!lp) {
            if (!target.empty()) refuse("loop control with an unknown label " + target, lineNo);
            if (asExpr) return "R.throwCtl(" + jsStr(op) + ", null)";
            return "throw new R." + string(op == "next" ? "NextCtl" : op == "last" ? "LastCtl" : "RedoCtl") + "(null);";
        }
        bool local = lp->fnDepth == fnDepth();
        string lbl = target.empty() ? "null" : jsStr(target);
        if (!local || asExpr) {
            lp->needsCatch = true;
            if (asExpr) return "R.throwCtl(" + jsStr(op) + ", " + lbl + ")";
            return "throw new R." + string(op == "next" ? "NextCtl" : op == "last" ? "LastCtl" : "RedoCtl") + "(" + lbl + ");";
        }
        if (op == "next") return (lp->nextPhaser.empty() ? "" : lp->nextPhaser) + "continue " + lp->jsLabel + ";";
        if (op == "last") return "break " + lp->jsLabel + ";";
        return "continue " + lp->jsLabel + "_redo;";
    }
    // The statements of a block. tail = the last statement's value is returned.
    void emitStmts(const std::vector<StmtPtr>& ss, int ind, bool tail) {
        // phasers and CATCH first
        std::vector<Stmt*> pre, main, post, classes;
        Block* catchBlock = nullptr;
        bool hasWhen = false;
        scopes.emplace_back();
        struct ScopePop { std::vector<Scope>& v; ~ScopePop() { v.pop_back(); } } scopePop{ scopes };
        for (auto& sp : ss) {
            Stmt* s = sp.get();
            if (s->kind == NK::SubDecl) { auto* d = static_cast<SubDecl*>(s); if (!d->name.empty() && !d->isMethod && !d->isSubmethod) scopes.back().subs.insert(d->name); }
            if (s->kind == NK::Block) {
                auto* b = static_cast<Block*>(s);
                if (b->isCatch) { if (catchBlock) refuse("two CATCH blocks in one scope", s->line); catchBlock = b; continue; }
                if (b->phaser == "LEAVE" || b->phaser == "KEEP" || b->phaser == "UNDO") { post.push_back(s); continue; }
                if (b->phaser == "ENTER" || b->phaser == "BEGIN" || b->phaser == "CHECK") { pre.push_back(s); continue; }
                if (b->phaser == "INIT") { pre.push_back(s); continue; }
                if (b->phaser == "END") { endBlocks.push_back(capture([&]() { line(0, "R.atEnd(() => {"); emitStmts(b->stmts, 1, false); line(0, "});"); })); continue; }
                if (b->phaser == "FIRST" || b->phaser == "NEXT" || b->phaser == "LAST") continue;   // handled by the loop
                if (b->phaser == "CONTROL") refuse("a CONTROL phaser", s->line);
                if (b->phaser == "QUIT" || b->phaser == "CLOSE" || b->phaser == "PRE" || b->phaser == "POST" || b->phaser == "COMPOSE" || b->phaser == "DOC") refuse("a " + b->phaser + " phaser", s->line);
            }
            if (s->kind == NK::WhenStmt) hasWhen = true;
            if (s->kind == NK::ClassDecl) { classes.push_back(s); continue; }   // compile-time in Raku: a sub above may already call it
            main.push_back(s);
        }
        for (auto* s : pre) emitStmts(static_cast<Block*>(s)->stmts, ind, false);
        int bodyInd = ind;
        string blkLabel;
        bool wrapTry = catchBlock || !post.empty();
        if (wrapTry) {
            // a `let` inside the try is invisible to the catch/finally: declare the
            // block's variables first, and let their declarations become assignments
            std::vector<string> names;
            for (auto* s : main) {
                std::vector<VarExpr*> ds; collectExprDecls(s, ds);
                for (auto* v : ds) if (v->declScope != "state" && v->declScope != "constant" && mangleVar(v->name) != "_anon") names.push_back(mangleVar(v->name));
                if (s->kind == NK::VarDecl) for (auto& nm : static_cast<VarDecl*>(s)->names) if (mangleVar(nm) != "_anon") names.push_back(mangleVar(nm));
            }
            if (!names.empty()) { string l = "let "; for (size_t i = 0; i < names.size(); i++) l += (i ? ", " : "") + names[i]; line(ind, l + ";"); for (auto& nm : names) predeclared.insert(nm); }
            line(ind, "try {"); bodyInd = ind + 1;
        }
        if (hasWhen) { blkLabel = label("_blk"); line(bodyInd, blkLabel + ": {"); blocks.push_back({ blkLabel, fnDepth(), false, "" }); bodyInd++; }
        for (auto* s : classes) stmt(s, bodyInd, false);
        bool tailIsClass = tail && !ss.empty() && ss.back()->kind == NK::ClassDecl && !static_cast<ClassDecl*>(ss.back().get())->isPackage;
        for (size_t i = 0; i < main.size(); i++) {
            bool last = i + 1 == main.size();
            // every `when` body is a possible last statement of the block
            stmt(main[i], bodyInd, tail && (last || main[i]->kind == NK::WhenStmt));
        }
        // A body with no value is Nil — and in a value-collecting loop that Nil is
        // still one element per iteration, so it goes through ret() rather than
        // being dropped: `do for 1..2 { }` is (Nil, Nil), not ().
        if (tailIsClass && !lastClassJs.empty()) line(bodyInd, ret(lastClassJs));
        else if (tail && (main.empty() || !yieldsValue(main.back()))) line(bodyInd, ret("R.Nil"));
        if (hasWhen) { bodyInd--; line(bodyInd, "}"); blocks.pop_back(); }
        if (wrapTry) {
            if (catchBlock) {
                fn().usesBang = true;
                line(ind, "} catch (_e) {");
                line(ind + 1, "if (R.isControl(_e)) throw _e;");
                line(ind + 1, "v__bang = R.exc(_e);");
                string t = newTopic();
                line(ind + 1, "const " + t + " = v__bang;");
                topics.push_back(t);
                string cl = label("_catch");
                line(ind + 1, cl + ": {");
                blocks.push_back({ cl, fnDepth(), false, "" });
                bool handled = false;
                for (auto& s2 : catchBlock->stmts) {
                    if (s2->kind == NK::WhenStmt) handled = true;
                    stmt(s2.get(), ind + 2, false);
                }
                if (!handled) { /* plain statements: the exception is still rethrown */ }
                line(ind + 2, "throw _e;");
                line(ind + 1, "}");
                blocks.pop_back();
                topics.pop_back();
            }
            if (!post.empty()) {
                line(ind, "} finally {");
                for (auto it = post.rbegin(); it != post.rend(); ++it) {
                    auto* b = static_cast<Block*>(*it);
                    if (b->phaser == "KEEP" || b->phaser == "UNDO") refuse("a " + b->phaser + " phaser", b->line);
                    emitStmts(b->stmts, ind + 1, false);
                }
            }
            line(ind, "}");
        }
    }
    static bool yieldsValue(Stmt* s) {
        if (s->kind == NK::ForStmt) return static_cast<ForStmt*>(s)->asExpr;
        if (s->kind == NK::WhileStmt) return static_cast<WhileStmt*>(s)->asExpr;
        if (s->kind == NK::LoopStmt) return static_cast<LoopStmt*>(s)->asExpr;
        return s->kind == NK::ExprStmt || s->kind == NK::IfStmt || s->kind == NK::GivenStmt || s->kind == NK::ReturnStmt || s->kind == NK::VarDecl;
    }
    void hoistExprDecls(Node* n, int ind) {
        std::vector<VarExpr*> ds;
        collectExprDecls(n, ds);
        for (auto* v : ds) {
            if (v->declScope == "state") { stateSlot(v, ind); continue; }
            string kw = declKw(mangleVar(v->name), "let "); if (!kw.empty()) line(ind, kw + mangleVar(v->name) + ";");
        }
    }
    std::set<string> predeclared;        // declared before a try wrapper: emit assignments, not lets
    string declKw(const string& js, const char* kw) { if (js == "_anon") return "var "; auto it = predeclared.find(js); if (it == predeclared.end()) return kw; predeclared.erase(it); return ""; }
    // `state $n = 0`: a module-level slot plus a once flag
    string stateSlot(VarExpr* v, int ind) {
        string slot = "_state" + std::to_string(++stateN);
        prelude.push_back("let " + slot + ", " + slot + "_init = false;");
        line(ind, "if (!" + slot + "_init) { " + slot + "_init = true; " + slot + " = " + declDefault(v) + "; }");
        fn().stateAlias[mangleVar(v->name)] = slot;
        return slot;
    }

    // ---- the statement switch ----
    void stmt(Stmt* s, int ind, bool tail) {
        if (s->line > 0) pendingSrcLine = s->line;
        if (!s->label.empty() && !isLoopNode(s)) refuse("a label on a non-loop statement", s->line);
        switch (s->kind) {
            case NK::EmptyStmt: return;
            case NK::UseStmt: { auto* u = static_cast<UseStmt*>(s); useStmt(u); return; }
            case NK::SubDecl: subDecl(static_cast<SubDecl*>(s), ind); return;
            case NK::ClassDecl: classDecl(static_cast<ClassDecl*>(s), ind); if (tail && !lastClassJs.empty()) line(ind, ret(lastClassJs)); return;
            case NK::EnumDecl: enumDecl(static_cast<EnumDecl*>(s), ind); return;
            case NK::SubsetDecl: subsetDecl(static_cast<SubsetDecl*>(s), ind); return;
            case NK::NamedRegexDecl: {
                auto* d = static_cast<NamedRegexDecl*>(s);
                line(ind, "var " + mangleVar("&" + d->name) + " = R.namedRegex(" + jsStr(d->name) + ", " + compileRegex(d->pattern, d->kind == "token" ? "r" : d->kind == "rule" ? "sr" : "", d->line) + ");");
                return;
            }
            case NK::ExprStmt: exprStmt(static_cast<ExprStmt*>(s), ind, tail); return;
            case NK::VarDecl: varDecl(static_cast<VarDecl*>(s), ind, tail); return;
            case NK::ReturnStmt: {
                auto* r = static_cast<ReturnStmt*>(s);
                if (r->isRw) refuse("return-rw", s->line);
                hoistExprDecls(r->value.get(), ind);
                string v = r->value ? exArg(r->value.get()) : "R.Nil";
                if (r->value && r->value->kind == NK::ListExpr && static_cast<ListExpr*>(r->value.get())->items.size() > 1) v = "R.mkList(R.spliceSlips([" + listItems(static_cast<ListExpr*>(r->value.get())->items) + "]))";
                if (fn().isRoutine) line(ind, "return " + v + ";");
                else {
                    // find the owning routine: any enclosing routine frame; throw to it
                    bool found = false;
                    for (int i = (int)fns.size() - 1; i >= 0; i--) if (fns[i].isRoutine) { fns[i].needsRetCatch = true; found = true; break; }
                    if (!found) line(ind, "return " + v + ";");   // mainline: `return` ends the program body
                    else line(ind, "throw new R.RetCtl(_rt, " + v + ");");
                }
                return;
            }
            case NK::LastStmt: line(ind, loopCtl("last", static_cast<LastStmt*>(s)->target, s->line, false)); return;
            case NK::NextStmt: line(ind, loopCtl("next", static_cast<NextStmt*>(s)->target, s->line, false)); return;
            case NK::RedoStmt: line(ind, loopCtl("redo", static_cast<RedoStmt*>(s)->target, s->line, false)); return;
            case NK::Block: {
                auto* b = static_cast<Block*>(s);
                if (b->isCatch || !b->phaser.empty()) { if (b->phaser == "BEGIN" || b->phaser == "INIT" || b->phaser == "ENTER" || b->phaser == "CHECK") { emitStmts(b->stmts, ind, false); return; } refuse("a " + b->phaser + " phaser here", s->line); }
                if (b->stmtForm) { emitStmts(b->stmts, ind, false); return; }
                line(ind, "{");
                string t = newTopic();
                line(ind + 1, "let " + t + " = " + topic() + ";");
                topics.push_back(t);
                emitStmts(b->stmts, ind + 1, tail);
                topics.pop_back();
                line(ind, "}");
                return;
            }
            case NK::IfStmt: ifStmt(static_cast<IfStmt*>(s), ind, tail); return;
            case NK::WhileStmt: whileStmt(static_cast<WhileStmt*>(s), ind, tail); return;
            case NK::RepeatStmt: repeatStmt(static_cast<RepeatStmt*>(s), ind); return;
            case NK::LoopStmt: loopStmt(static_cast<LoopStmt*>(s), ind, tail); return;
            case NK::ForStmt: forStmt(static_cast<ForStmt*>(s), ind, tail); return;
            case NK::GivenStmt: givenStmt(static_cast<GivenStmt*>(s), ind, tail); return;
            case NK::WhenStmt: whenStmt(static_cast<WhenStmt*>(s), ind, tail); return;
            default: refuse(nkName(s->kind), s->line);
        }
    }
    void useStmt(UseStmt* u) {
        const string& m = u->module;
        if (u->isNo) { if (m == "strict" || m == "worries" || m == "precompilation" || m == "trace" || m == "fatal") return; refuse("no " + m, u->line); }
        if (m == "lib" || m == "strict" || m == "worries" || m == "fatal" || m == "trace" || m == "isms" || m == "MONKEY-SEE-NO-EVAL" || m == "MONKEY-TYPING" || m == "MONKEY" || m == "MONKEY-GUTS" || m == "soft" || m == "nqp" || m == "experimental" || m == "newline" || m == "variables" || m == "invocant" || m == "parameters" || m == "attributes" || m == "dynamic-scope" || m == "precompilation") {
            if (m == "nqp") refuse("use nqp", u->line);
            return;
        }
        if (m.rfind("v6", 0) == 0 || m == "v6" || m.rfind("v6.", 0) == 0) return;
        if (m == "Test") refuse("the Test module", u->line);
        if (m == "JS") { jsInterop = true; return; }
        refuse("use " + m, u->line);
    }
    void exprStmt(ExprStmt* es, int ind, bool tail) {
        Expr* e = es->e.get();
        if (e->kind == NK::RegexLit && !static_cast<RegexLit*>(e)->isRx && static_cast<RegexLit*>(e)->declKind.empty()) { string v = boolOperand(e); if (tail) line(ind, ret(v)); else line(ind, v + ";"); return; }   // a bare /…/ statement matches $_
        // declarations
        if (e->kind == NK::VarExpr && static_cast<VarExpr*>(e)->declare) {
            auto* v = static_cast<VarExpr*>(e);
            if (v->declScope == "state") { string slot = stateSlot(v, ind); if (tail) line(ind, ret(slot)); return; }
            if (v->name.size() > 2 && v->name[1] == '*') { line(ind, "R.dynSet(" + jsStr(v->name) + ", " + declDefault(v) + ");"); return; }
            if (v->name[0] != '$' && v->name[0] != '@' && v->name[0] != '%' && v->name[0] != '&') declareSigilless(v->name);
            if (!v->declTypeExpr && !v->declShape) {
                line(ind, declKw(mangleVar(v->name), v->declScope == "constant" ? "const " : "let ") + mangleVar(v->name) + " = " + declDefault(v) + ";");
                if (tail) line(ind, ret(mangleVar(v->name)));
                return;
            }
            refuse("a shaped or parameterized declaration", v->line);
        }
        if (e->kind == NK::Assign) {
            auto* a = static_cast<Assign*>(e);
            Expr* t = a->target.get();
            if (t->kind == NK::VarExpr && static_cast<VarExpr*>(t)->declare && (a->op == "=" || a->op == ":=")) {
                auto* v = static_cast<VarExpr*>(t);
                if (v->declShape) refuse("a shaped array declaration", v->line);
                hoistExprDecls(a->value.get(), ind);
                if (v->declScope == "state") {
                    string slot = "_state" + std::to_string(++stateN);
                    prelude.push_back("let " + slot + ", " + slot + "_init = false;");
                    string init = v->name[0] == '@' ? "R.newArray(" + listSource(a->value.get()) + ")" : v->name[0] == '%' ? "R.newHash(" + listSource(a->value.get()) + ")" : exArg(a->value.get());
                    line(ind, "if (!" + slot + "_init) { " + slot + "_init = true; " + slot + " = " + init + "; }");
                    fn().stateAlias[mangleVar(v->name)] = slot;
                    if (tail) line(ind, ret(slot));
                    return;
                }
                if (v->name.size() > 2 && v->name[1] == '*') { line(ind, "R.dynSet(" + jsStr(v->name) + ", " + (v->name[0] == '@' ? "R.newArray(" + listSource(a->value.get()) + ")" : v->name[0] == '%' ? "R.newHash(" + listSource(a->value.get()) + ")" : exArg(a->value.get())) + ");"); return; }
                if (v->name[0] != '$' && v->name[0] != '@' && v->name[0] != '%' && v->name[0] != '&') declareSigilless(v->name);
                string init;
                char sig = a->containerSigil ? a->containerSigil : v->name[0];   // `=@=` / `=%=`: the operator names the container
                if ((sig == '@' || sig == '%') && a->op == ":=") init = exArg(a->value.get());   // binding: the object itself
                else if (sig == '@') init = "R.newArray(" + listSource(a->value.get()) + ")";
                else if (sig == '%') init = "R.newHash(" + listSource(a->value.get()) + ")";
                else if (!v->declCoerce.empty()) init = "R.coerce(" + typeObj(v->declCoerce, v->line) + ", " + exArg(a->value.get()) + ")";
                else init = exArg(a->value.get());
                if ((sig == '@' || sig == '%') && !v->declType.empty()) init = "R.withOf(" + init + ", " + typeObj(v->declType, v->line) + ")";
                if ((sig == '@' || sig == '%') && v->declDefault) init = "R.withDefault(" + init + ", " + exArg(v->declDefault.get()) + ")";   // `is default(v)`: what a missing element answers
                line(ind, declKw(mangleVar(v->name), v->declScope == "constant" ? "const " : "let ") + mangleVar(v->name) + " = " + init + ";");
                if (tail) line(ind, ret(mangleVar(v->name)));
                return;
            }
            if (t->kind == NK::ListExpr) {
                // my ($a, $b) = … : declare, then list-assign
                auto* l = static_cast<ListExpr*>(t);
                bool anyDecl = false;
                for (auto& it : l->items) if (it->kind == NK::VarExpr && static_cast<VarExpr*>(it.get())->declare) { anyDecl = true; string js = mangleVar(static_cast<VarExpr*>(it.get())->name); string kw = declKw(js, "let "); if (!kw.empty()) line(ind, kw + js + ";"); }
                (void)anyDecl;
                hoistExprDecls(a->value.get(), ind);
                if (tail) line(ind, ret(listAssign(l, a->value.get()))); else line(ind, listAssign(l, a->value.get()) + ";");
                return;
            }
        }
        // `my Foo $x .= new`
        if (e->kind == NK::MethodCall && static_cast<MethodCall*>(e)->mutate && static_cast<MethodCall*>(e)->inv->kind == NK::VarExpr && static_cast<VarExpr*>(static_cast<MethodCall*>(e)->inv.get())->declare) {
            auto* v = static_cast<VarExpr*>(static_cast<MethodCall*>(e)->inv.get());
            if (!declKw(mangleVar(v->name), "let ").empty()) line(ind, "let " + mangleVar(v->name) + ";");
            if (tail) line(ind, ret(methodCall(static_cast<MethodCall*>(e)))); else line(ind, methodCall(static_cast<MethodCall*>(e)) + ";");
            return;
        }
        // a statement-level `do for` / loop with values
        hoistExprDecls(e, ind);
        // statement-level loop control in expression form: `last if …` parses as IfStmt; `$x or next` as Binary
        string v = exArg(e);
        if (tail) line(ind, ret(v));
        else if (e->kind == NK::Call || e->kind == NK::MethodCall || e->kind == NK::Binary) line(ind, "R.sink(" + v + ");");   // a Failure in sink context throws
        else line(ind, v + ";");
    }
    void varDecl(VarDecl* d, int ind, bool tail) {
        if (d->scope == "has") refuse("has outside a class", d->line);
        for (auto& nm : d->names) if (!nm.empty() && nm[0] != '$' && nm[0] != '@' && nm[0] != '%' && nm[0] != '&') declareSigilless(nm);
        if (d->names.size() == 1) {
            string n = d->names[0];
            string init = d->init ? (n[0] == '@' ? "R.newArray(" + listSource(d->init.get()) + ")" : n[0] == '%' ? "R.newHash(" + listSource(d->init.get()) + ")" : exArg(d->init.get()))
                                  : (n[0] == '@' ? "R.mkArray([])" : n[0] == '%' ? "R.mkHash()" : "R.Any");
            line(ind, declKw(mangleVar(n), "let ") + mangleVar(n) + " = " + init + ";");
            if (tail) line(ind, ret(mangleVar(n)));
            return;
        }
        string arr = tmp();
        for (auto& n : d->names) { string kw = declKw(mangleVar(n), "let "); if (!kw.empty()) line(ind, kw + mangleVar(n) + ";"); }
        line(ind, arr + " = R.arr(" + (d->init ? listSource(d->init.get()) : "[]") + ");");
        for (size_t i = 0; i < d->names.size(); i++) {
            const string& n = d->names[i];
            if (n[0] == '@') { line(ind, mangleVar(n) + " = R.mkArray(" + arr + ".slice(" + std::to_string(i) + "));"); break; }
            if (n[0] == '%') { line(ind, mangleVar(n) + " = R.newHash(R.mkList(" + arr + ".slice(" + std::to_string(i) + ")));"); break; }
            line(ind, mangleVar(n) + " = " + arr + "[" + std::to_string(i) + "] ?? R.Any;");
        }
        if (tail) line(ind, ret("R.mkList(" + arr + ")"));
    }
    static string emptyFor(const string& rakuName) { return rakuName[0] == '@' ? " = R.mkArray([])" : rakuName[0] == '%' ? " = R.mkHash()" : " = R.Any"; }
    void hoistModifierDecls(Block* b, int ind) {
        if (!b) return;
        std::vector<VarExpr*> ds;
        for (auto& st : b->stmts) { collectExprDecls(st.get(), ds); if (st->kind == NK::VarDecl) for (auto& nm : static_cast<VarDecl*>(st.get())->names) { line(ind, "let " + mangleVar(nm) + emptyFor(nm) + ";"); predeclared.insert(mangleVar(nm)); } }
        for (auto* v : ds) if (v->declScope != "state" && v->declScope != "constant" && mangleVar(v->name) != "_anon") { line(ind, "let " + mangleVar(v->name) + emptyFor(v->name) + ";"); predeclared.insert(mangleVar(v->name)); }
    }
    void ifStmt(IfStmt* s, int ind, bool tail) {
        if (s->modifier) for (auto& br : s->branches) hoistModifierDecls(br.second.get(), ind);
        for (auto& br : s->branches) hoistExprDecls(br.first.get(), ind);
        for (size_t i = 0; i < s->branchParams.size(); i++) if (!s->branchParams[i].empty()) refuse("if EXPR -> (…) destructuring binder", s->line);
        if (!s->elseParams.empty()) refuse("else -> (…) destructuring binder", s->line);
        for (size_t i = 0; i < s->branches.size(); i++) {
            string cond = boolOperand(s->branches[i].first.get());
            string var = i == 0 ? s->thenVar : (i < s->branchVars.size() ? s->branchVars[i] : "");
            bool neg = i == 0 && s->isUnless;
            string t;
            if (!var.empty()) { t = tmp(); cond = "(" + t + " = " + cond + ")"; }
            string test = neg ? "!R.truthy(" + cond + ")" : "R.truthy(" + cond + ")";
            line(ind, (i ? "} else if (" : "if (") + test + ") {");
            if (!var.empty()) {
                if (var == "$_") { string nt = newTopic(); line(ind + 1, "let " + nt + " = " + t + ";"); topics.push_back(nt); }
                else line(ind + 1, "const " + mangleVar(var) + " = " + t + ";");
            }
            emitStmts(s->branches[i].second->stmts, ind + 1, tail);
            if (var == "$_") topics.pop_back();
        }
        if (s->elseBlock) {
            line(ind, "} else {");
            if (!s->elseVar.empty()) refuse("else -> $x binder", s->line);
            emitStmts(s->elseBlock->stmts, ind + 1, tail);
        } else if (tail && sinkVar.empty()) { line(ind, "} else {"); line(ind + 1, "return R.mkSlip([]);"); }
        line(ind, "}");
    }
    // ---- loops ----
    struct LoopPhasers { std::vector<Stmt*> first, next, last; };
    LoopPhasers loopPhasers(Block* body) {
        LoopPhasers p;
        for (auto& s : body->stmts) if (s->kind == NK::Block) { auto* b = static_cast<Block*>(s.get()); if (b->phaser == "FIRST") p.first.push_back(b); else if (b->phaser == "NEXT") p.next.push_back(b); else if (b->phaser == "LAST") p.last.push_back(b); }
        return p;
    }
    // Emit a loop body inside the loop: handles redo, thrown control, FIRST/NEXT/LAST.
    // `head` is the loop header up to the opening brace; `pre` runs before the loop.
    void loopBody(Stmt* loop, Block* body, int ind, const string& head, const std::function<void()>& bindVars, bool tailBody = false) {
        LoopPhasers ph = loopPhasers(body);
        bool hasRedo = contains(body, [](Node* n) { return n->kind == NK::RedoStmt || (n->kind == NK::Unary && static_cast<Unary*>(n)->op == "redo"); }, true);
        string lbl = label("L");
        loops.push_back({ loop->label, lbl, fnDepth() });
        if (!ph.next.empty()) loops.back().nextPhaser = capture([&]() { for (auto* b : ph.next) emitStmts(static_cast<Block*>(b)->stmts, 0, false); });
        string firstFlag, enteredFlag;
        if (!ph.first.empty()) { firstFlag = tmp(); line(ind, firstFlag + " = true;"); }
        if (!ph.last.empty()) { enteredFlag = tmp(); line(ind, enteredFlag + " = false;"); }
        line(ind, lbl + ": " + head + " {");
        int bi = ind + 1;
        bindVars();
        if (!enteredFlag.empty()) line(bi, enteredFlag + " = true;");
        if (!firstFlag.empty()) { line(bi, "if (" + firstFlag + ") {"); line(bi + 1, firstFlag + " = false;"); for (auto* b : ph.first) emitStmts(static_cast<Block*>(b)->stmts, bi + 1, false); line(bi, "}"); }
        if (hasRedo) { line(bi, lbl + "_redo: for (;;) {"); bi++; }
        string bodyText = capture([&]() { emitStmts(body->stmts, bi + 1, tailBody); });
        // emitting the body may have set needsCatch (a nested closure signals this loop)
        if (loops.back().needsCatch) {
            line(bi, "try {");
            out << bodyText;
            line(bi, "} catch (_e) {");
            string lt = loop->label.empty() ? "null" : jsStr(loop->label);
            line(bi + 1, "if (_e instanceof R.NextCtl && (_e.label === null || _e.label === " + lt + ")) { " + loops.back().nextPhaser + (hasRedo ? "break " + lbl + "_redo;" : "continue " + lbl + ";") + " }");
            line(bi + 1, "if (_e instanceof R.LastCtl && (_e.label === null || _e.label === " + lt + ")) break " + lbl + ";");
            if (hasRedo) line(bi + 1, "if (_e instanceof R.RedoCtl && (_e.label === null || _e.label === " + lt + ")) continue " + lbl + "_redo;");
            line(bi + 1, "throw _e;");
            line(bi, "}");
        } else { line(bi, "{"); out << bodyText; line(bi, "}"); }
        if (!ph.next.empty()) for (auto* b : ph.next) emitStmts(static_cast<Block*>(b)->stmts, bi, false);
        if (hasRedo) { line(bi, "break;"); bi--; line(bi, "}"); }
        line(ind, "}");
        loops.pop_back();
        if (!ph.last.empty()) { line(ind, "if (" + enteredFlag + ") {"); for (auto* b : ph.last) emitStmts(static_cast<Block*>(b)->stmts, ind + 1, false); line(ind, "}"); }
    }
    // a loop in value position (`do for`, a routine ending in a loop): its body's
    // values are collected into a Seq
    template <class F> void collecting(bool asExpr, bool tail, int ind, F emit) {
        if (!(asExpr && tail)) { emit(false); return; }
        string sv = tmp();
        line(ind, sv + " = [];");
        string saved = sinkVar; sinkVar = sv;
        emit(true);
        sinkVar = saved;
        line(ind, ret("R.mkList(" + sv + ")"));
    }
    void whileStmt(WhileStmt* w, int ind, bool tail) {
        collecting(w->asExpr, tail, ind, [&](bool tb) { whileStmtBody(w, ind, tb); });
    }
    void whileStmtBody(WhileStmt* w, int ind, bool tb) {
        if (w->modifier) hoistModifierDecls(w->body.get(), ind);
        if (!w->params.empty()) refuse("while EXPR -> (…) destructuring binder", w->line);
        hoistExprDecls(w->cond.get(), ind);
        if (w->body == nullptr) { refuse("a while modifier without a block", w->line); }
        if (!w->var.empty()) {
            string t = tmp();
            loopBody(w, w->body.get(), ind, "for (;;)", [&]() {
                line(ind + 1, "if (" + string(w->isUntil ? "" : "!") + "R.truthy(" + t + " = " + boolOperand(w->cond.get()) + ")) break;");
                line(ind + 1, "const " + mangleVar(w->var) + " = " + t + ";");
            }, tb);
            return;
        }
        string cond = boolOperand(w->cond.get());
        loopBody(w, w->body.get(), ind, "while (" + string(w->isUntil ? "!" : "") + "R.truthy(" + cond + "))", []() {}, tb);
    }
    void repeatStmt(RepeatStmt* r, int ind) {
        string flag = tmp();
        line(ind, flag + " = true;");
        string cond = boolOperand(r->cond.get());
        loopBody(r, r->body.get(), ind, "while (" + flag + " || " + string(r->isUntil ? "!" : "") + "R.truthy(" + cond + "))", [&]() { line(ind + 1, flag + " = false;"); });
    }
    void loopStmt(LoopStmt* l, int ind, bool tail) {
        collecting(l->asExpr, tail, ind, [&](bool tb) { loopStmtBody(l, ind, tb); });
    }
    void loopStmtBody(LoopStmt* l, int ind, bool tb) {
        line(ind, "{");
        if (l->init) hoistExprDecls(l->init.get(), ind + 1);
        if (l->cond) hoistExprDecls(l->cond.get(), ind + 1);
        string init = l->init ? exArg(l->init.get()) : "";
        string cond = l->cond ? "R.truthy(" + boolOperand(l->cond.get()) + ")" : "";
        string incr = l->incr ? exArg(l->incr.get()) : "";
        loopBody(l, l->body.get(), ind + 1, "for (" + init + "; " + cond + "; " + incr + ")", []() {}, tb);
        line(ind, "}");
    }
    void forStmt(ForStmt* f, int ind, bool tail) {
        collecting(f->asExpr, tail, ind, [&](bool tb) { forStmtBody(f, ind, tb); });
    }
    void forStmtBody(ForStmt* f, int ind, bool tb) {
        if (f->modifier) hoistModifierDecls(f->body.get(), ind);
        if (f->rwVars) refuse("a read-write (<->) loop parameter", f->line);
        hoistExprDecls(f->list.get(), ind);
        Expr* le = f->list.get();
        string src = listSource(le);
        if (!f->params.empty()) {
            // `for … -> ($a, $b)` / `-> $k, ($x, $y)`: each row bound like a call's arguments
            int k = 0; for (auto& p : f->params) { if (p.slurpy || p.named) refuse("a slurpy or named loop parameter", f->line); k++; }
            string nt; for (auto& p : f->params) if (p.name == "$_") nt = mangleVar("$_");
            if (!nt.empty()) topics.push_back(nt);
            loopBody(f, f->body.get(), ind, "for (const _args of R.iterN(" + src + ", " + std::to_string(k) + "))", [&]() { bindParams(f->params, ind + 1, "", false); }, tb);
            if (!nt.empty()) topics.pop_back();
            return;
        }
        if (f->destructure) {
            // `-> ($a, $b)` over a list of lists, spelled with plain vars
            loopBody(f, f->body.get(), ind, "for (const _row of R.iter(" + src + "))", [&]() {
                string arr = tmp();
                line(ind + 1, arr + " = R.arr(_row);");
                for (size_t i = 0; i < f->vars.size(); i++) {
                    const string& v = f->vars[i];
                    if (v[0] == '@') { line(ind + 1, "const " + mangleVar(v) + " = R.mkArray(" + arr + ".slice(" + std::to_string(i) + "));"); break; }
                    line(ind + 1, "let " + mangleVar(v) + " = " + arr + "[" + std::to_string(i) + "] ?? R.Any;");
                }
            }, tb);
            return;
        }
        // the loop variables live OUTSIDE the loop: a LAST phaser reads the last one
        string vars, decl;
        string nt;
        if (f->vars.empty() && f->params.empty() && le->kind == NK::VarExpr && !static_cast<VarExpr*>(le)->declare) {
            const string& vn = static_cast<VarExpr*>(le)->name;
            auto writesTopic = [&]() { return stmtsContain(f->body->stmts, [](Node* n) {
                if (n->kind == NK::SubstLit) return true;
                if (n->kind == NK::Assign) { auto* a = static_cast<Assign*>(n); return a->target && a->target->kind == NK::VarExpr && static_cast<VarExpr*>(a->target.get())->name == "$_"; }
                if (n->kind == NK::MethodCall) { auto* m = static_cast<MethodCall*>(n); return m->mutate && m->inv && m->inv->kind == NK::VarExpr && static_cast<VarExpr*>(m->inv.get())->name == "$_"; }
                return false; }, false); };
            if (vn.size() > 1 && vn[0] == '$' && vn[1] != '!' && vn[1] != '.' && vn[1] != '*' && vn != "$_" && vn != "$/" && writesTopic()) {
                line(ind, "{"); ind++;
                topics.push_back(varRef(static_cast<VarExpr*>(le)));
                loopBody(f, f->body.get(), ind, "for (const _once of [0])", []() {}, tb);
                topics.pop_back();
                ind--; line(ind, "}");
                return;
            }
            if (vn[0] == '@' && vn.size() > 1 && vn[1] != '*' && writesTopic()) {
                // `for @a { $_ *= 2 }` / `for @a { s/o/0/ }`: the topic IS the slot — every write goes back into the array
                string nt2 = newTopic(), arrN = label("_slots"), iN = label("_i");
                line(ind, "{"); ind++;
                line(ind, "const " + arrN + " = R.arr(" + varRef(static_cast<VarExpr*>(le)) + ");");
                line(ind, "let " + nt2 + ";");
                topics.push_back(nt2); topicSlots[nt2] = arrN + "[" + iN + "]";
                loopBody(f, f->body.get(), ind, "for (let " + iN + " = 0; " + iN + " < " + arrN + ".length; " + iN + "++)", [&]() { line(ind + 1, nt2 + " = R.item(" + arrN + "[" + iN + "]);"); }, tb);
                topicSlots.erase(nt2); topics.pop_back();
                ind--; line(ind, "}");
                return;
            }
        }
        if (f->vars.empty()) { nt = newTopic(); vars = nt; decl = nt; }
        else if (f->vars.size() == 1) { if (f->vars[0] == "$_") { nt = newTopic(); vars = nt; } else vars = mangleVar(f->vars[0]); decl = vars; }
        else { vars = "["; for (size_t i = 0; i < f->vars.size(); i++) { string vn = f->vars[i] == "$_" ? (nt = newTopic()) : mangleVar(f->vars[i]); vars += (i ? ", " : "") + vn; decl += (i ? ", " : "") + vn; } vars += "]"; }
        line(ind, "{");
        ind++;
        for (auto& v : f->vars) if (v[0] != '$' && v[0] != '@' && v[0] != '%' && v[0] != '&') declareSigilless(v);
        line(ind, "let " + decl + ";");
        string iterExpr = f->vars.size() > 1 ? "R.iterN(" + src + ", " + std::to_string(f->vars.size()) + ")" : (f->vars.empty() || f->vars[0] == "$_") ? "R.iterTopic(" + src + ")" : "R.iter(" + src + ")";   // the bare topic: an Array's slots arrive as items
        // a range: the runtime's counted iterator (no generator on the hot path)
        if (f->vars.size() <= 1 && le->kind == NK::Range) {
            auto* r = static_cast<RangeExpr*>(le);
            if (r->from && r->to && !hasWhatever(r->from.get()) && !hasWhatever(r->to.get()))
                iterExpr = "R.rangeIter(" + rangeEnd(r->from.get()) + ", " + rangeEnd(r->to.get()) + ", " + (r->exFrom ? "true" : "false") + ", " + (r->exTo ? "true" : "false") + ")";
        }
        if (!nt.empty()) topics.push_back(nt);
        loopBody(f, f->body.get(), ind, "for (" + vars + " of " + iterExpr + ")", [&]() { for (auto& v : f->vars) if (v[0] == '$' && v != "$_") line(ind + 1, mangleVar(v) + " = R.item(" + mangleVar(v) + ");"); }, tb);   // a named $ loop variable itemizes
        if (!nt.empty()) topics.pop_back();
        ind--;
        line(ind, "}");
    }
    void givenStmt(GivenStmt* g, int ind, bool tail) {
        if (!g->params.empty() || !g->elseParams.empty()) refuse("given/with EXPR -> (…) destructuring binder", g->line);
        if (g->modifier) hoistModifierDecls(g->body.get(), ind);
        hoistExprDecls(g->topic.get(), ind);
        string t = tmp();
        string topicE = exArg(g->topic.get());
        if (g->defGuard) {
            string test = string(g->defGuard == 1 ? "" : "!") + "R.defined(" + t + " = " + topicE + ")";
            line(ind, "if (" + test + ") {");
            string nt = g->var.empty() || g->var == "$_" ? newTopic() : mangleVar(g->var);
            line(ind + 1, "let " + nt + " = " + t + ";");
            if (g->var.empty() || g->var == "$_") topics.push_back(nt);
            emitStmts(g->body->stmts, ind + 1, tail);
            if (g->var.empty() || g->var == "$_") topics.pop_back();
            if (g->hasElse && g->elseBody) {
                line(ind, "} else {");
                if (!g->elseVar.empty()) line(ind + 1, "const " + mangleVar(g->elseVar) + " = " + t + ";");
                emitStmts(g->elseBody->stmts, ind + 1, tail);
            } else if (tail && sinkVar.empty()) { line(ind, "} else {"); line(ind + 1, "return R.mkSlip([]);"); }
            line(ind, "}");
            return;
        }
        string lbl = label("_given");
        line(ind, lbl + ": {");
        string nt;
        bool itemTopic = g->topic && isItemized(g->topic.get());   // `given $x`: the topic is the container's item
        if (g->topic && g->topic->kind == NK::VarExpr && !static_cast<VarExpr*>(g->topic.get())->declare && static_cast<VarExpr*>(g->topic.get())->name.size() > 1 && static_cast<VarExpr*>(g->topic.get())->name[0] == '$' && static_cast<VarExpr*>(g->topic.get())->name[1] != '!' && static_cast<VarExpr*>(g->topic.get())->name[1] != '.' && static_cast<VarExpr*>(g->topic.get())->name != "$_")
            nt = varRef(static_cast<VarExpr*>(g->topic.get()));   // the variable itself: assignments to $_ reach it
        else { nt = newTopic(); line(ind + 1, "let " + nt + " = " + (itemTopic ? "R.item(" + topicE + ")" : topicE) + ";"); }
        if (!g->var.empty() && g->var != "$_") line(ind + 1, "const " + mangleVar(g->var) + " = " + nt + ";");
        topics.push_back(nt);
        if (itemTopic) itemTopics.insert(nt);
        blocks.push_back({ lbl, fnDepth(), false, "" });
        emitStmts(g->body->stmts, ind + 1, tail);
        blocks.pop_back();
        if (itemTopic) itemTopics.erase(nt);
        topics.pop_back();
        line(ind, "}");
    }
    void whenStmt(WhenStmt* w, int ind, bool tail) {
        if (blocks.empty()) refuse("when outside a block", w->line);
        if (blocks.back().fnDepth != fnDepth()) refuse("when inside a closure within its block", w->line);
        string lbl = blocks.back().jsLabel;   // copied: the body's own blocks may grow the vector and move it
        string test = w->isDefault ? "" : smartmatchTest(w->cond.get());
        if (w->isDefault) line(ind, "{");
        else line(ind, "if (" + test + ") {");
        emitStmts(w->body->stmts, ind + 1, tail);
        line(ind + 1, "break " + lbl + ";");
        line(ind, "}");
    }
    string smartmatchTest(Expr* cond) {
        if (cond->kind == NK::BlockExpr) return "R.truthy(" + blockClosure(static_cast<BlockExpr*>(cond)) + "(" + topic() + "))";
        if (cond->kind == NK::RegexLit) { fn().usesSlash = true; return "R.truthy(" + slashSet("R.rxMatch(" + topic() + ", " + regexObject(static_cast<RegexLit*>(cond)) + ")") + ")"; }
        return "R.smartmatch(" + topic() + ", " + exArg(cond) + ")";
    }

    // ------------------------------------------------------------- routines --
    struct MainCand { string fn; std::vector<Param>* params; };
    std::vector<MainCand> mainCands;
    std::set<string> emittedMulti;

    // Bind a general signature from `_args` (positionals + a trailing R.named).
    void bindParams(const std::vector<Param>& ps, int ind, const string& who, bool isMethod) {
        declareSigillessParams(ps);
        for (auto& p : ps) if (p.name == "$/") fn().slashParam = true;
        line(ind, "const [_pos, _named] = R.splitArgs(_args);");
        int req = 0, opt = 0; bool slurpy = false;
        for (auto& p : ps) { if (p.named || p.invocant) continue; if (p.slurpy || p.sigil == '|' || p.sigil == '\\') slurpy = true; else if (p.optional || p.defaultVal) opt++; else req++; }
        if (req) line(ind, "if (_pos.length < " + std::to_string(req) + ") R.tooFew(" + jsStr(who) + ", " + std::to_string(req) + ", _pos.length);");
        bool laxTail = false; for (auto it = ps.rbegin(); it != ps.rend(); ++it) { if (it->named || it->invocant) continue; laxTail = (it->sigil == '@' || it->sigil == '%') && !it->slurpy; break; }   // extras after a trailing @/% parameter are ignored, as the interpreter does
        if (!slurpy && !laxTail) line(ind, "if (_pos.length > " + std::to_string(req + opt) + ") R.tooMany(" + jsStr(who) + ", " + std::to_string(req + opt) + ", _pos.length);");
        int pi = 0;
        std::vector<string> usedNamed;
        for (auto& p : ps) {
            if (p.invocant) { if (!p.name.empty()) line(ind, "const " + mangleVar(p.name) + " = self;"); continue; }
            if (p.typeCapture) refuse("a type capture (::T)", 0);
            string name = p.name.empty() ? "" : mangleVar(p.name);
            char sig = p.sigil;
            if (sig == '|' || sig == '\\') {   // |c: the rest of the arguments as a Capture, named ones included
                if (!name.empty()) line(ind, "let " + name + " = new R.RCapture(_pos.slice(" + std::to_string(pi) + "), _named);");
                if (p.subSig) { string tmpn = name.empty() ? "_cap" + std::to_string(pi) : name; if (name.empty()) line(ind, "let " + tmpn + " = new R.RCapture(_pos.slice(" + std::to_string(pi) + "), _named);"); bindSubSig(*p.subSig, "R.mkList(" + tmpn + ".pos)", ind); }   // |c ($first, *@rest)
                continue;
            }
            if (p.named) {
                std::vector<string> keys;
                if (!p.namedKey.empty()) keys.push_back(p.namedKey); else if (!p.name.empty()) keys.push_back(p.name.substr(1));
                if (p.aliasBoth && !p.name.empty()) keys.push_back(p.name.substr(1));
                for (auto& k : p.aliasKeys) keys.push_back(k);
                for (auto& k : keys) usedNamed.push_back(k);
                if (p.slurpy) { line(ind, "let " + name + " = R.namedHash(_named, new Set([" + joinStrs(usedNamed) + "]));"); continue; }
                string dflt = p.defaultVal ? exArg(p.defaultVal.get()) : sig == '@' ? "R.mkArray([])" : sig == '%' ? "R.mkHash()" : (p.required ? "undefined" : (p.type.empty() ? "R.Any" : typeObj(p.type, 0)));
                if (name.empty()) continue;
                line(ind, "let " + name + " = R.namedArg(_named, [" + joinStrs(keys) + "], " + dflt + ");");
                if (p.required) line(ind, "if (" + name + " === undefined) R.die(\"Required named parameter '" + keys[0] + "' not passed\");");
                if (sig == '$' && p.type.empty()) line(ind, "if (" + name + " === R.Mu) R.notAny(" + jsStr(p.name) + ");");
                if (sig == '@') line(ind, name + " = R.newArray(" + name + ");");
                if (itemParam(p)) line(ind, name + " = R.item(" + name + ");");
                if (p.subSig) bindSubSig(*p.subSig, name, ind);
                continue;
            }
            if (p.slurpy) {
                string rest = "_pos.slice(" + std::to_string(pi) + ")";
                if (name.empty()) continue;
                if (sig == '%') line(ind, "let " + name + " = R.newHash(R.mkList(" + rest + "));");
                else if (p.slurpyKind == 'n') line(ind, "let " + name + " = R.mkArray(" + rest + ");");
                else if (p.slurpyKind == '1') line(ind, "let " + name + " = R.mkArray(_pos.length - " + std::to_string(pi) + " === 1 ? R.itemsOf(_pos[" + std::to_string(pi) + "]).slice() : " + rest + ");");
                else line(ind, "let " + name + " = R.mkArray(R.slurpyFlat(" + rest + "));");
                continue;
            }
            string val = "_pos[" + std::to_string(pi) + "]";
            if (p.defaultVal) val = "(_pos.length > " + std::to_string(pi) + " ? " + val + " : " + exArg(p.defaultVal.get()) + ")";
            else if (p.optional) val = "(_pos.length > " + std::to_string(pi) + " ? " + val + " : " + (sig == '@' ? "R.mkArray([])" : sig == '%' ? "R.mkHash()" : (p.type.empty() ? "R.Any" : typeObj(p.type, 0))) + ")";
            pi++;
            if (p.litVal) { line(ind, "if (!R.smartmatch(" + val + ", " + exArg(p.litVal.get()) + ")) R.die(\"Constraint type check failed in binding\");"); if (name.empty()) continue; }
            if (name.empty() && !p.subSig) continue;
            if (p.isRw) { line(ind, "let " + name + " = R.rwBox(" + val + ");"); fn().boxed.insert(name); continue; }
            if (p.subSig) {
                string sv = name.empty() ? tmp() : name;
                line(ind, (name.empty() ? "" : "let ") + sv + " = " + val + ";");
                bindSubSig(*p.subSig, sv, ind);
                continue;
            }
            if (sig == '@' && p.isCopy) line(ind, "let " + name + " = R.newArray(" + val + ");");
            else if (sig == '%' && p.isCopy) line(ind, "let " + name + " = R.newHash(" + val + ");");
            else line(ind, "let " + name + " = " + (sig == '@' ? "R.bindArray(" + val + ")" : val) + ";");   // a bound @ keeps a List a List
            if (sig == '$' && p.type.empty()) line(ind, "if (" + name + " === R.Mu) R.notAny(" + jsStr(p.name) + ");");
            if (itemParam(p)) line(ind, name + " = R.item(" + name + ");");
            typeGuard(p, name, ind);
        }
    }
    static string joinStrs(const std::vector<string>& v) { string s; for (size_t i = 0; i < v.size(); i++) s += (i ? ", " : "") + jsStr(v[i]); return s; }
    // `-> ($a, $b)` / `sub f([$h, *@t])`: unpack a positional into its sub-signature
    void bindSubSig(const std::vector<Param>& sub, const string& src, int ind) {
        string arr = tmp();
        line(ind, arr + " = R.arr(" + src + ");");
        int i = 0;
        for (auto& q : sub) {
            if (q.named || q.invocant) refuse("a named parameter in a sub-signature", 0);
            string n = q.name.empty() ? "" : mangleVar(q.name);
            if (q.slurpy) { if (!n.empty()) line(ind, "let " + n + " = R.mkArray(" + arr + ".slice(" + std::to_string(i) + "));"); break; }
            if (!n.empty()) {
                string v = arr + "[" + std::to_string(i) + "]";
                if (q.defaultVal) v = "(" + arr + ".length > " + std::to_string(i) + " ? " + v + " : " + exArg(q.defaultVal.get()) + ")";
                else v += " ?? R.Any";
                if (q.subSig) { line(ind, "let " + n + " = " + v + ";"); bindSubSig(*q.subSig, n, ind); }
                else line(ind, "let " + n + " = " + (q.sigil == '@' ? (q.isCopy ? "R.newArray(" : "R.bindArray(") + v + ")" : q.sigil == '%' ? "R.newHash(" + v + ")" : v) + ";");
                if (!q.subSig && itemParam(q)) line(ind, n + " = R.item(" + n + ");");
            }
            i++;
        }
    }
    void typeGuard(const Param& p, const string& name, int ind) {
        if (p.sigil != '$' || p.coerce) { if (p.coerce && p.sigil == '$') line(ind, name + " = R.coerce(" + typeObj(p.type, 0) + ", " + name + ");"); return; }
        if (p.type == "Any" || p.type == "Any:D") line(ind, "if (" + name + " === R.Mu) R.notAny(" + jsStr(p.name) + ");");
        else if (!p.type.empty() && p.type != "Mu" && p.type != "Cool" && (kCoreTypes.count(p.type) || classNames.count(p.type) || subsetNames.count(p.type)))
            line(ind, "R.typeCheck(" + name + ", " + typeObj(p.type, 0) + ", " + jsStr(p.name) + ");");
        if (p.defConstraint == 1) line(ind, "if (!R.defined(" + name + ")) R.die(\"Parameter '" + p.name + "' requires an instance of type " + (p.type.empty() ? "Any" : p.type) + ", but a type object was passed\");");
        if (p.whereExpr) line(ind, "if (!R.truthy(R.matcherOf(" + exArg(p.whereExpr.get()) + ")(" + name + "))) R.die(\"Constraint type check failed in binding to parameter '" + p.name + "'\");");
    }
    // the function text of one routine (sub, method or multi candidate)
    string routineFn(const string& jsName, SubDecl* d, bool isMethod, const string& who) {
        if (d->isNative) refuse("a NativeCall sub", d->line);
        if (d->nameExpr) refuse("a routine with a computed name", d->line);
        if (d->immediateCall) refuse("an immediately called sub declaration", d->line);
        if (!d->altParams.empty()) refuse("alternative signatures", d->line);
        for (auto& t : d->traits) refuse("the trait 'is " + t.name + "'", d->line);
        bool usesArgs = !d->hadSig && d->params.empty() && stmtsContain(d->body, [](Node* n) { return isVarNamed(n, "@_"); }, false);
        std::vector<string> placeholders;
        if (!d->hadSig && d->params.empty() && !usesArgs) placeholders = computePlaceholders(d->body);
        bool simple = simpleSig(d->params) && !hasWhereOrType(d->params) && !usesArgs && placeholders.empty();
        string params;
        if (isMethod) params = "self";
        if (simple) { for (auto& p : d->params) { if (!params.empty()) params += ", "; params += mangleVar(p.name); } }
        else params += (params.empty() ? "" : ", ") + string("..._args");
        string savedSelf = selfName; if (isMethod) selfName = "self";
        bool isAsync = stmtsAwait(d->body);
        string body = fnBody(true, [&]() {
            scopes.emplace_back(); declareSigillessParams(d->params);
            for (auto& p : d->params) fn().params.insert(p.name);
            for (auto& p : d->params) if (p.name == "$/") fn().slashParam = true;
            struct ScopePop { std::vector<Scope>& v; ~ScopePop() { v.pop_back(); } } scopePop{ scopes };
            if (usesArgs) line(2, "let " + mangleVar("@_") + " = R.mkArray(R.splitArgs(_args)[0]);");
            else if (!placeholders.empty()) {
                line(2, "const [_pos, _named] = R.splitArgs(_args);");
                int pi = 0;
                for (auto& ph : placeholders) {
                    if (ph.size() > 2 && ph[1] == ':') line(2, "let " + mangleVar(plainName(ph)) + " = R.namedArg(_named, [" + jsStr(ph.substr(2)) + "], R.Any);");
                    else { line(2, "let " + mangleVar(plainName(ph)) + " = _pos.length > " + std::to_string(pi) + " ? _pos[" + std::to_string(pi) + "] : R.Any;"); pi++; }
                }
            }
            else if (!simple) bindParams(d->params, 2, who, isMethod);
            else if (!d->params.empty()) {
                line(2, "if (arguments.length " + string(!d->params.empty() && (d->params.back().sigil == '@' || d->params.back().sigil == '%') ? "<" : "!==") + " " + std::to_string(d->params.size() + (isMethod ? 1 : 0)) + ") R.arityError(" + jsStr(who) + ", " + std::to_string(d->params.size()) + ", arguments.length" + (isMethod ? " - 1" : "") + ");");
                for (auto& p : d->params) line(2, "if (" + mangleVar(p.name) + " === R.Mu) R.notAny(" + jsStr(p.name) + ");");
                for (auto& p : d->params) if (itemParam(p)) line(2, mangleVar(p.name) + " = R.item(" + mangleVar(p.name) + ");");
            }
            bool bindsTopic = false; for (auto& p : d->params) if (p.name == "$_") bindsTopic = true;
            if (bindsTopic) topics.push_back(mangleVar("$_"));   // `method name($_)`: the parameter IS the topic
            else { string t = newTopic(); fn().declared.push_back("let " + t + " = R.Any;"); topics.push_back(t); }
            emitStmts(d->body, 2, true);
            topics.pop_back();
        }, 2);
        selfName = savedSelf;
        return string(isAsync ? "async " : "") + "function " + jsName + "(" + params + ") {\n" + body + "    }";
    }
    static bool hasWhereOrType(const std::vector<Param>& ps) { for (auto& p : ps) if (!p.type.empty() || p.whereExpr || p.defConstraint || p.coerce) return true; return false; }

    void subDecl(SubDecl* d, int ind) {
        if (d->isMethod || d->isSubmethod) refuse("a method outside a class", d->line);
        if (d->name.empty()) refuse("an anonymous sub at statement level", d->line);
        if (d->isProto) return;
        SubInfo& info = subs[d->name];
        if (info.isMulti) {
            if (emittedMulti.count(d->name)) return;
            emittedMulti.insert(d->name);
            emitMulti(d->name, info, ind, false, "");
            return;
        }
        string who = d->name;
        string text = routineFn(mangleSub(d->name), d, false, who);
        emitFnText(text, ind);
        if (d->name == "MAIN") { hasMain = true; mainCands.push_back({ mangleSub("MAIN"), &d->params }); }
    }
    void emitFnText(const string& text, int ind) {
        // fnBody produced lines at indent 2; re-indent to `ind`
        std::istringstream in(text); string l; bool first = true;
        while (std::getline(in, l)) { line(first ? ind : 0, first ? l : reindent(l, 2, ind + 1)); first = false; }
    }
    static string reindent(const string& l, int from, int to) {
        size_t sp = 0; while (sp < l.size() && l[sp] == ' ') sp++;
        int lvl = (int)sp / 4 - from + to; if (lvl < 0) lvl = 0;
        return string(lvl * 4, ' ') + l.substr(sp);
    }
    // multi: every candidate as its own function, then a dispatcher sorted by narrowness
    // How narrow a type constraint is: its depth in the core hierarchy (Mu 0,
    // Any 1, … Int 5); a user class or a subset sits below the core it extends.
    int typeDepth(const string& t) {
        static const std::map<string, int> depth = {
            {"Mu", 0}, {"Any", 1}, {"Cool", 2}, {"Numeric", 3}, {"Stringy", 3}, {"Positional", 3}, {"Associative", 3}, {"Callable", 3}, {"Iterable", 3}, {"Setty", 3}, {"Baggy", 3},
            {"Real", 4}, {"Str", 4}, {"List", 4}, {"Map", 4}, {"Code", 4}, {"Set", 4}, {"Bag", 4}, {"Exception", 2}, {"Junction", 1}, {"Pair", 4}, {"Range", 4}, {"Seq", 4}, {"Complex", 4},
            {"Int", 5}, {"Num", 5}, {"Rational", 5}, {"Array", 5}, {"Hash", 5}, {"Block", 5}, {"Mix", 5}, {"Rat", 6}, {"Bool", 6}, {"Routine", 6}, {"FatRat", 6}, {"Sub", 7}, {"Method", 7},
        };
        string base = t; size_t c = base.find(':'); if (c != string::npos && base.compare(c, 2, "::") != 0) base = base.substr(0, c);
        auto it = depth.find(base);
        if (it != depth.end()) return it->second;
        if (subsetNames.count(base)) return 8;
        if (classNames.count(base)) return 7;
        return 4;
    }
    void emitMulti(const string& name, SubInfo& info, int ind, bool isMethod, const string& cls) {
        std::vector<std::pair<int, SubDecl*>> order;
        for (size_t i = 0; i < info.cands.size(); i++) {
            SubDecl* d = info.cands[i];
            int score = 0;
            for (auto& p : d->params) { if (p.named || p.invocant) continue; if (p.litVal) score += 20; score += p.type.empty() ? (p.sigil == '$' ? 1 : 0) : typeDepth(p.type); if (p.whereExpr) score += 1; if (p.defConstraint) score += 1; }   // an untyped $ is Any
            order.push_back({ score, d });
        }
        std::stable_sort(order.begin(), order.end(), [](const std::pair<int, SubDecl*>& a, const std::pair<int, SubDecl*>& b) { return a.first > b.first; });
        string base = isMethod ? "m_" + mangleBody(cls) + "__" + mangleBody(name) : mangleSub(name);
        std::vector<string> guards;
        for (size_t k = 0; k < info.cands.size(); k++) {
            SubDecl* d = info.cands[k];
            if (d->isProto) continue;
            string jsName = base + "__" + std::to_string(k);
            emitFnText(routineFn(jsName, d, isMethod, name), ind);
            if (name == "MAIN" && !isMethod) { hasMain = true; mainCands.push_back({ jsName, &d->params }); }
        }
        bool anyAsync = false; for (auto* d : info.cands) if (stmtsAwait(d->body)) anyAsync = true;
        string disp = string(anyAsync ? "async " : "") + "function " + base + "(" + (isMethod ? "self, " : "") + "..._args) {\n";
        disp += "        const [_pos, _named] = R.splitArgs(_args);\n";
        for (auto& oc : order) {
            SubDecl* d = oc.second;
            if (d->isProto) continue;
            size_t k = std::find(info.cands.begin(), info.cands.end(), d) - info.cands.begin();
            int req = 0, opt = 0; bool slurpy = false; std::vector<string> g;
            int pi = 0;
            for (auto& p : d->params) {
                if (p.invocant) continue;
                if (p.named) { if (p.required) g.push_back("_named.has(" + jsStr(p.namedKey.empty() ? p.name.substr(1) : p.namedKey) + ")"); continue; }
                if (p.slurpy) { slurpy = true; continue; }
                if (p.optional || p.defaultVal) opt++; else req++;
                string a = "_pos[" + std::to_string(pi) + "]";
                string cond;
                if (p.litVal) cond = "R.smartmatch(" + a + ", " + exArg(p.litVal.get()) + ")";
                else if (!p.type.empty() || p.defConstraint) cond = "R.typeMatches(" + a + ", " + (p.type.empty() || p.type == "Any" || p.type == "Mu" ? "null" : typeObj(p.type, d->line)) + ", " + std::to_string(p.defConstraint) + ")" + ((p.type.empty() || p.type == "Any") && p.sigil == '$' ? " && R.isAny(" + a + ")" : "");
                else if (p.sigil == '$' && !p.slurpy) cond = "R.isAny(" + a + ")";   // untyped: implicitly Any (a Junction is not)
                if (p.whereExpr) {
                    string w = "R.truthy(R.matcherOf(" + exArg(p.whereExpr.get()) + ")(" + a + "))";
                    if (pi > 0) {   // a `where` may read the EARLIER parameters (`$n where * > $l`): bind them around the test
                        string names, vals; int j = 0;
                        for (auto& q : d->params) { if (q.invocant || q.named || q.slurpy) continue; if (j > pi) break; if (q.name.size() > 1 && q.sigil == '$') { names += (names.empty() ? "" : ", ") + mangleVar(q.name); vals += (vals.empty() ? "" : ", ") + string("_pos[") + std::to_string(j) + "]"; } j++; }
                        if (!names.empty()) w = "((" + names + ") => " + w + ")(" + vals + ")";
                    }
                    cond = cond.empty() ? w : cond + " && " + w;
                }
                if (!cond.empty()) g.push_back((p.optional || p.defaultVal) ? "(_pos.length <= " + std::to_string(pi) + " || " + cond + ")" : cond);
                pi++;
            }
            string ar = "_pos.length >= " + std::to_string(req) + (slurpy ? "" : " && _pos.length <= " + std::to_string(req + opt));
            string guard = ar; for (auto& x : g) guard += " && " + x;
            bool simple = simpleSig(d->params) && !hasWhereOrType(d->params);
            string fwd = simple ? "(" + (isMethod ? string("self") : "") : "(" + (isMethod ? string("self, ") : "") + "..._args";
            if (simple) { for (int i = 0; i < pi; i++) fwd += (isMethod || i ? ", " : "") + string("_pos[") + std::to_string(i) + "]"; }
            fwd += ")";
            disp += "        if (" + guard + ") return " + (anyAsync ? "await " : "") + base + "__" + std::to_string(k) + fwd + ";\n";
        }
        disp += "        R.noMatch(" + jsStr(name) + ", _args);\n    }";
        emitFnText(disp, ind);
    }

    // -------------------------------------------------------------- classes --
    void classDecl(ClassDecl* c, int ind) {
        if (c->isPackage) { if (moduleName.empty()) moduleName = c->name; emitStmts(c->body, ind, false); return; }   // `module X { … }`: its body, here
        // a grammar: its rules are compiled patterns the runtime's matcher resolves by name
        if (c->isAugment) refuse("augment", c->line);
        if (c->nameExpr) refuse("a class with a computed name", c->line);
        if (c->parameterized || !c->roleParams.empty()) refuse("a parameterized role", c->line);
        if (c->isMonitor) refuse("a monitor", c->line);
        if (!c->repr.empty()) refuse("is repr", c->line);
        if (!c->roleArgs.empty()) for (auto& ra : c->roleArgs) if (!ra.second.empty()) refuse("a role with arguments", c->line);
        if (c->isStubDecl) return;
        string name = c->name;
        string jsName = name.empty() ? "c__anon" + std::to_string(++labelN) : mangleType(name);
        string savedClass = curClass; curClass = name;
        // class-body statements (`my $count`, `constant`, a sub the methods share):
        // emitted here, in the enclosing scope, which is what the methods close over
        if (!c->body.empty()) emitStmts(c->body, ind, false);
        string parents, roles;
        auto addParent = [&](const string& pn, bool asRole) {
            string ref = (classNames.count(pn) ? mangleType(pn) : typeObj(pn, c->line));
            if (asRole) roles += (roles.empty() ? "" : ", ") + ref; else parents += (parents.empty() ? "" : ", ") + ref;
        };
        if (!c->parent.empty()) addParent(c->parent, c->parentIsDoes);
        for (auto& p : c->extraParents) addParent(p, false);
        for (auto& r : c->roles) addParent(r, true);
        // methods first (function declarations, hoisted), then the class object
        std::map<string, SubInfo> methods; std::vector<string> methodOrder;
        for (auto& m : c->methods) {
            if (m->isProto) continue;
            string key = m->isPrivate ? "!" + m->name : m->name;
            if (m->nameExpr) refuse("a method with a computed name", m->line);
            auto& mi = methods[key];
            if (!mi.cands.size()) methodOrder.push_back(key);
            mi.cands.push_back(m.get());
            if (m->isMulti) mi.isMulti = true;
        }
        string table;
        string savedSelf = selfName; selfName = "self";
        for (auto& key : methodOrder) {
            SubInfo& mi = methods[key];
            string jsName = "m_" + mangleBody(name) + "__" + mangleBody(key);
            if (mi.isMulti) emitMulti(key, mi, ind, true, name);
            else emitFnText(routineFn(jsName, mi.cands[0], true, name + "." + key), ind);
            table += (table.empty() ? "" : ", ") + jsStr(key) + ": " + jsName;
        }
        string attrs;
        for (auto& a : c->attrs) {
            if (a.inlined) refuse("HAS", c->line);
            if (a.objKeyed) refuse("an object-keyed hash attribute", c->line);
            if (!a.userTraits.empty()) refuse("a user trait on an attribute", c->line);
            if (a.whereExpr) refuse("a where clause on an attribute", c->line);
            string def;
            if (a.def) {
                string t = newTopic(); topics.push_back(t);
                def = fnBody(false, [&]() { line(2, "let " + t + " = R.Any;"); line(2, "return " + (a.sigil == '@' ? "R.newArray(" + listSource(a.def.get()) + ")" : a.sigil == '%' ? "R.newHash(" + listSource(a.def.get()) + ")" : exArg(a.def.get())) + ";"); }, 2);
                topics.pop_back();
                def = "(self) => {\n" + def + "    }";
            }
            string handles; for (size_t i = 0; i < a.handles.size(); i++) handles += (i ? ", " : "") + jsStr(a.handles[i]);
            string handlesTo; for (size_t i = 0; i < a.handlesTo.size(); i++) handlesTo += (i ? ", " : "") + jsStr(a.handlesTo[i]);
            attrs += (attrs.empty() ? "" : ", ") + string("{ name: ") + jsStr(a.name) + ", sigil: \"" + a.sigil + "\", pub: " + (a.pub ? "true" : "false") + ", rw: " + (a.rw || c->classRw ? "true" : "false") +
                     ", required: " + (a.required ? "true" : "false") + ", built: " + (a.built ? "true" : "false") + (a.type.empty() ? "" : ", type: " + jsStr(a.type)) + (a.coerce ? ", coerce: true" : "") +
                     (def.empty() ? "" : ", def: " + def) + (handles.empty() ? "" : ", handles: [" + handles + "]") + (handlesTo.empty() ? "" : ", handlesTo: [" + handlesTo + "]") + " }";
        }
        string rules;
        for (auto& r : c->rules) {
            string body = r.pattern; while (!body.empty() && (body.back() == ' ' || body.back() == '\n')) body.pop_back(); while (!body.empty() && (body[0] == ' ' || body[0] == '\n')) body.erase(0, 1);
            if (body == "*" || body == "{*}") { rules += (rules.empty() ? "" : ", ") + jsStr(r.name) + ": { kind: " + jsStr(r.kind) + ", proto: true }"; continue; }   // `proto token x {*}`
            if (!r.params.empty()) {   // `token kw($k) { $k … }`: the pattern is a function of its arguments
                string params; for (auto& p : r.params) params += (params.empty() ? "" : ", ") + mangleVar(p);
                rules += (rules.empty() ? "" : ", ") + jsStr(r.name) + ": { kind: " + jsStr(r.kind) + ", mk: (" + params + ") => " + compileRegex(r.pattern, r.kind == "token" ? "r" : r.kind == "rule" ? "sr" : "", c->line) + " }";
                continue;
            }
            rules += (rules.empty() ? "" : ", ") + jsStr(r.name) + ": { kind: " + jsStr(r.kind) + ", rx: " + compileRegex(r.pattern, r.kind == "token" ? "r" : r.kind == "rule" ? "sr" : "", c->line) + " }";
        }
        selfName = savedSelf; curClass = savedClass;
        line(ind, "const " + jsName + " = R.defClass(" + jsStr(name) + ", { parents: [" + parents + "], roles: [" + roles + "], isRole: " + (c->isRole ? "true" : "false") +
                  (c->isGrammar || !c->rules.empty() ? ", isGrammar: true, rules: { " + rules + " }" : "") +
                  ", attrs: [" + attrs + "], methods: { " + table + " } });");
        lastClassJs = jsName;
    }
    string lastClassJs;
    string moduleName;
    // --module: what the ES module exports — `is export` subs (every named sub when none is
    // marked), classes, grammars, enums, and MAIN as a function of its argv
    struct ExportEntry { string name, js, dts; };
    std::vector<ExportEntry> exportsOf(const std::vector<StmtPtr>& ss) {
        std::vector<ExportEntry> out;
        bool anyMarked = false;
        std::function<void(const std::vector<StmtPtr>&, bool)> walk = [&](const std::vector<StmtPtr>& stmts, bool count) {
            for (auto& sp : stmts) {
                Stmt* s = sp.get();
                if (s->kind == NK::SubDecl) { auto* d = static_cast<SubDecl*>(s); if (!d->name.empty() && !d->isMethod && !d->isSubmethod && d->isExport) anyMarked = true; }
                if (s->kind == NK::ClassDecl && static_cast<ClassDecl*>(s)->isPackage) walk(static_cast<ClassDecl*>(s)->body, count);
            }
        };
        walk(ss, true);
        std::set<string> seen;
        std::function<void(const std::vector<StmtPtr>&)> collect = [&](const std::vector<StmtPtr>& stmts) {
            for (auto& sp : stmts) {
                Stmt* s = sp.get();
                if (s->kind == NK::SubDecl) {
                    auto* d = static_cast<SubDecl*>(s);
                    if (d->name.empty() || d->isMethod || d->isSubmethod || d->name == "MAIN" || seen.count(d->name)) continue;
                    if (anyMarked && !d->isExport) continue;
                    seen.insert(d->name); out.push_back({ d->name, "R.exportFn(" + mangleSub(d->name) + ", " + jsStr(d->name) + ")", dtsSub(d) });
                } else if (s->kind == NK::ClassDecl) {
                    auto* c = static_cast<ClassDecl*>(s);
                    if (c->isPackage) { collect(c->body); continue; }
                    if (c->name.empty() || c->isStubDecl || seen.count(c->name)) continue;
                    seen.insert(c->name); out.push_back({ c->name, "R.exportType(" + mangleType(c->name) + ")", (c->isGrammar || !c->rules.empty()) ? "RakuGrammar" : "RakuClass" });
                } else if (s->kind == NK::EnumDecl) {
                    auto* e = static_cast<EnumDecl*>(s);
                    if (e->name.empty() || seen.count(e->name)) continue;
                    seen.insert(e->name); out.push_back({ e->name, "R.exportType(" + mangleType(e->name) + ")", "RakuEnum" });
                }
            }
        };
        collect(ss);
        return out;
    }
    std::vector<ExportEntry> exportTable;   // filled by program() under --module
    // the TypeScript type of a parameter / return: the core scalars by name, containers by sigil, the rest `any`
    static string dtsType(const Param& p) {
        if (p.sigil == '@') return "any[]";
        if (p.sigil == '%') return "Record<string, any>";
        if (p.sigil == '&') return "(...args: any[]) => any";
        string t = p.type; if (t.size() > 2 && (t.compare(t.size() - 2, 2, ":D") == 0 || t.compare(t.size() - 2, 2, ":U") == 0)) t = t.substr(0, t.size() - 2);
        if (t == "Int" || t == "Num" || t == "Rat" || t == "Numeric" || t == "Real" || t == "UInt" || t == "int" || t == "num") return "number";
        if (t == "Str" || t == "str") return "string";
        if (t == "Bool") return "boolean";
        if (t == "Positional" || t == "List" || t == "Array") return "any[]";
        if (t == "Associative" || t == "Hash" || t == "Map") return "Record<string, any>";
        if (t == "Callable" || t == "Code" || t == "Sub" || t == "Block") return "(...args: any[]) => any";
        return "any";
    }
    static string dtsIdent(const string& raku) { string o; for (unsigned char c : raku) o += (ascii::isalnum(c) || c == '_') ? (char)c : '_'; if (o.empty() || ascii::isdigit((unsigned char)o[0])) o = "_" + o; return o; }
    string dtsSub(SubDecl* d) {
        if (d->isMulti) return "(...args: any[]) => any";
        string pos, named; bool slurpy = false;
        for (auto& p : d->params) {
            if (p.invocant) continue;
            if (p.named) { named += (named.empty() ? "" : "; ") + dtsIdent(p.namedKey.empty() ? (p.name.size() > 1 ? p.name.substr(1) : p.name) : p.namedKey) + "?: " + dtsType(p); continue; }
            if (p.slurpy) { slurpy = true; continue; }
            pos += (pos.empty() ? "" : ", ") + dtsIdent(p.name.size() > 1 ? p.name.substr(1) : "arg") + (p.optional || p.defaultVal ? "?" : "") + ": " + dtsType(p);
        }
        if (slurpy) pos += (pos.empty() ? "" : ", ") + string("...rest: any[]");
        if (!named.empty()) pos += (pos.empty() ? "" : ", ") + string("named?: { ") + named + " }";
        return "(" + pos + ") => any";
    }
    void enumDecl(EnumDecl* e, int ind) {
        string tyName = e->name.empty() ? "_anon_enum" + std::to_string(++labelN) : e->name;
        string ty = mangleType(tyName);
        std::vector<std::pair<string, string>> members;   // key, value expr
        Expr* v = e->values.get();
        std::vector<Expr*> items;
        if (v->kind == NK::ArrayLit) for (auto& it : static_cast<ArrayLit*>(v)->items) items.push_back(it.get());
        else if (v->kind == NK::ListExpr) for (auto& it : static_cast<ListExpr*>(v)->items) items.push_back(it.get());
        else items.push_back(v);
        string prev = "-1";
        for (auto* it : items) {
            if (it->kind == NK::StrLit) { string k = static_cast<StrLit*>(it)->v; members.push_back({ k, "R.inc(" + prev + ")" }); }
            else if (it->kind == NK::NameTerm) { string k = static_cast<NameTerm*>(it)->name; members.push_back({ k, "R.inc(" + prev + ")" }); }
            else if (it->kind == NK::Pair) { auto* p = static_cast<PairExpr*>(it); if (p->keyExpr || !p->value) refuse("an enum member with a computed key", e->line); members.push_back({ p->key, exArg(p->value.get()) }); }
            else if (it->kind == NK::AllomorphLit) { string k = static_cast<AllomorphLit*>(it)->str; members.push_back({ k, "R.inc(" + prev + ")" }); }
            else refuse("an enum member of this form", e->line);
            prev = "_enumv" + ty;
        }
        line(ind, "let _enumv" + ty + " = -1;");
        string list;
        for (auto& m : members) list += (list.empty() ? "" : ", ") + string("[") + jsStr(m.first) + ", (_enumv" + ty + " = " + m.second + ")]";
        line(ind, "const " + ty + " = R.enumType(" + jsStr(tyName) + ", [" + list + "]);");
        for (size_t i = 0; i < members.size(); i++) {
            string cn = "n_" + mangleBody(tyName) + "_" + mangleBody(members[i].first);
            line(ind, "const " + cn + " = " + ty + ".enumValues[" + std::to_string(i) + "];");
            enumKeys[members[i].first] = cn;
        }
        classNames.insert(tyName);
    }
    void subsetDecl(SubsetDecl* s, int ind) {
        string base = s->baseType.empty() ? "R.T.Any" : typeObj(s->baseType, s->line);
        string check = s->where ? "R.matcherOf(" + exArg(s->where.get()) + ")" : "null";
        line(ind, "const " + mangleType(s->name) + " = R.subset(" + jsStr(s->name) + ", " + base + ", " + check + ", " + std::to_string(s->defConstraint) + ");");
        subsetNames.insert(s->name);
    }

    // --------------------------------------------------------------- program --
    void prepass() {
        std::function<void(Node*, bool)> walk = [&](Node* n, bool inClass) {
            if (!n) return;
            if (n->kind == NK::SubDecl && !inClass) {
                auto* d = static_cast<SubDecl*>(n);
                if (!d->name.empty() && !d->isMethod && !d->isSubmethod) {
                    SubInfo& info = subs[d->name];
                    info.cands.push_back(d);
                    if (d->isMulti || d->isProto) info.isMulti = true;
                    int pi = 0;
                    for (auto& p : d->params) { if (p.named) continue; if (p.isRw && !p.slurpy) info.rwIdx.push_back(pi); pi++; }
                }
            }
            if (n->kind == NK::ClassDecl) { auto* c = static_cast<ClassDecl*>(n); if (!c->name.empty()) classNames.insert(c->name); }
            if (n->kind == NK::EnumDecl) { auto* e = static_cast<EnumDecl*>(n); if (!e->name.empty()) classNames.insert(e->name); }
            if (n->kind == NK::SubsetDecl) subsetNames.insert(static_cast<SubsetDecl*>(n)->name);
            if (n->kind == NK::VarExpr) { auto* v = static_cast<VarExpr*>(n); if (v->declare && v->name.size() > 1 && v->name[0] == '&') codeVars.insert(v->name.substr(1)); }
            auto sigless = [&](const std::vector<Param>& ps) { for (auto& p : ps) { if (p.sigil == '&' && p.name.size() > 1) codeVars.insert(p.name.substr(1)); } };
            if (n->kind == NK::SubDecl) sigless(static_cast<SubDecl*>(n)->params);
            if (n->kind == NK::BlockExpr) sigless(static_cast<BlockExpr*>(n)->params);
            if (n->kind == NK::ForStmt) sigless(static_cast<ForStmt*>(n)->params);
            if (n->kind == NK::VarDecl) for (auto& nm : static_cast<VarDecl*>(n)->names) if (nm.size() > 1 && nm[0] == '&') codeVars.insert(nm.substr(1));
            forEachChild(n, [&](Node* c) { walk(c, inClass || n->kind == NK::ClassDecl); });
        };
        for (auto& s : prog.stmts) walk(s.get(), false);
        for (auto& kv : subs) if (kv.second.isMulti) { bool anyMulti = false; for (auto* d : kv.second.cands) if (d->isMulti) anyMulti = true; if (!anyMulti) kv.second.isMulti = false; }
        // colouring, to a fixpoint: a routine awaits directly, or calls one that does
        std::vector<SubDecl*> methods;
        std::function<void(Node*)> collect = [&](Node* n) { if (!n) return; if (n->kind == NK::ClassDecl) for (auto& m : static_cast<ClassDecl*>(n)->methods) methods.push_back(m.get()); forEachChild(n, collect); };
        for (auto& s : prog.stmts) collect(s.get());
        for (bool changed = true; changed;) {
            changed = false;
            for (auto& kv : subs) if (!asyncSubs.count(kv.first)) for (auto* d : kv.second.cands) if (stmtsAwait(d->body)) { asyncSubs.insert(kv.first); changed = true; break; }
            for (auto* m : methods) if (!asyncMethods.count(m->name) && stmtsAwait(m->body)) { asyncMethods.insert(m->name); changed = true; }
        }
    }
    bool mainAsync = false;
    string program() {
        usesChannel = opt.srcText.find("Channel") != string::npos;
        prepass();
        mainAsync = stmtsAwait(prog.stmts);
        fns.emplace_back();
        fn().isRoutine = false;
        string body = capture([&]() {
            line(1, "let v___0 = R.Any;");
            topics.push_back("v___0");
            emitStmts(prog.stmts, 1, false);
            topics.pop_back();
            if (opt.module) {   // hand the exports back instead of running MAIN
                exportTable = exportsOf(prog.stmts);
                string table;
                for (auto& e : exportTable) table += (table.empty() ? "" : ", ") + jsStr(e.name) + ": " + e.js;
                if (hasMain) {
                    string cands;
                    for (auto& mc : mainCands) cands += (cands.empty() ? "" : ", ") + string("{ fn: ") + mc.fn + ", params: [" + mainParams(*mc.params) + "] }";
                    table += (table.empty() ? "" : ", ") + string("MAIN: R.exportMain([") + cands + "])";
                    exportTable.push_back({ "MAIN", "" });
                }
                line(1, "return { " + table + " };");
            } else if (hasMain) {
                string cands;
                for (auto& mc : mainCands) cands += (cands.empty() ? "" : ", ") + string("{ fn: ") + mc.fn + ", params: [" + mainParams(*mc.params) + "] }";
                line(1, "return R.runMain([" + cands + "], R.host.argv);");
            }
        });
        FnCtx ctx = fns.back(); fns.pop_back();
        std::ostringstream o;
        for (auto& p : prelude) o << "    " << p << "\n";
        if (!ctx.temps.empty()) { o << "    let "; for (size_t i = 0; i < ctx.temps.size(); i++) o << (i ? ", " : "") << ctx.temps[i]; o << ";\n"; }
        if (ctx.usesBang) o << "    let v__bang = R.Nil;\n";
        if (ctx.usesSlash) o << "    let v__slash = R.Nil;\n";
        for (auto& d : ctx.declared) o << "    " << d << "\n";
        for (auto& e : endBlocks) o << reindentBlock(e, 1);
        o << body;
        return o.str();
    }
    static string reindentBlock(const string& text, int ind) { std::istringstream in(text); string l, out; while (std::getline(in, l)) out += string(ind * 4, ' ') + l + "\n"; return out; }
    string mainParams(const std::vector<Param>& ps) {
        string s;
        for (auto& p : ps) {
            if (p.invocant) continue;
            string name = p.name.size() > 1 ? p.name.substr(1) : "";
            if (!p.namedKey.empty()) name = p.namedKey;
            string ty = p.type;
            if (ty.size() > 2 && ty.compare(ty.size() - 2, 2, ":D") == 0) ty = ty.substr(0, ty.size() - 2);
            s += (s.empty() ? "" : ", ") + string("{ name: ") + jsStr(name) + ", named: " + (p.named ? "true" : "false") + ", slurpy: " + (p.slurpy ? "true" : "false") +
                 ", optional: " + (p.optional ? "true" : "false") + ", hasDefault: " + (p.defaultVal ? "true" : "false") + ", type: " + (ty.empty() ? "null" : jsStr(ty)) +
                 ", isBool: " + string((ty == "Bool" || (p.named && ty.empty() && p.sigil == '$' && !p.defaultVal)) ? "true" : "false") +
                 (p.litVal && p.litVal->kind == NK::StrLit ? ", lit: " + jsStr(static_cast<StrLit*>(p.litVal.get())->v) : "") + " }";
        }
        return s;
    }
};

// A small stable hash for the manifest (FNV-1a, 64-bit): identifies the source, no more.
string sourceHash(const string& s) {
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char b[24]; std::snprintf(b, sizeof b, "%016llx", h); return b;
}
string manifest(const JsOptions& opt, const char* mode) {
    return string("{\"rakupp\":\"") + opt.version + "\",\"mode\":\"" + mode + "\",\"source\":\"" + sourceHash(opt.srcText) + "\"}";
}
} // namespace

std::string jsManifestLine(const JsOptions& opt, const char* mode) {
    return "// " + (string("RAKUPP-EXE-") + "MANIFEST ") + manifest(opt, mode) + "\n";
}
std::string jsRuntimeModule() {
    // an ES module has no `require`: give the host adapter one (Node, Bun); Deno and browsers ignore it
    return "import { createRequire as __rakupp_createRequire } from 'module';\n"
           "const require = typeof __rakupp_createRequire === 'function' ? __rakupp_createRequire(import.meta.url) : undefined;\n"
           + jsRuntimeSource() + "\nexport default R;\n";
}

// ---- source maps ----------------------------------------------------------
// Each generated line maps to the Raku line of its statement (column 0), the
// standard's "one segment per line" shape: enough for stack traces and a
// debugger's line stepping. Fields are base64 VLQ, relative to the previous
// segment as the format requires.
static void vlq(std::string& o, long v) {
    static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned long u = v < 0 ? ((unsigned long)(-v) << 1) | 1 : ((unsigned long)v << 1);
    do { unsigned d = u & 31; u >>= 5; if (u) d |= 32; o += B64[d]; } while (u);
}
static std::string jsonStr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\"; else if (c == '\n') o += "\\n"; else if (c == '\r') o += "\\r"; else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += (char)c;
    }
    return o + "\"";
}
// strip the ` //@Lnn` markers, collecting (generated line → source line)
static std::string stripLineMarkers(const std::string& text, std::vector<int>& srcLines) {
    std::string out; out.reserve(text.size());
    size_t pos = 0; int cur = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos); if (nl == std::string::npos) nl = text.size();
        std::string l = text.substr(pos, nl - pos);
        size_t m = l.rfind(" //@L");
        if (m != std::string::npos && m + 5 < l.size() && l.find_first_not_of("0123456789", m + 5) == std::string::npos) { cur = std::atoi(l.c_str() + m + 5); l.erase(m); }
        srcLines.push_back(cur);
        out += l; out += '\n';
        pos = nl + 1;
    }
    return out;
}
static std::string sourceMapJson(const std::vector<int>& srcLines, const std::string& file, const std::string& srcName, const std::string& srcText) {
    std::string mappings; long prevSrc = 0;
    for (size_t i = 0; i < srcLines.size(); i++) {
        if (i) mappings += ';';
        if (srcLines[i] <= 0) continue;
        long s = srcLines[i] - 1;
        vlq(mappings, 0); vlq(mappings, 0); vlq(mappings, s - prevSrc); vlq(mappings, 0);
        prevSrc = s;
    }
    return "{\"version\":3,\"file\":" + jsonStr(file) + ",\"sources\":[" + jsonStr(srcName) + "],\"sourcesContent\":[" + jsonStr(srcText) + "],\"names\":[],\"mappings\":\"" + mappings + "\"}\n";
}
std::string transpileToJs(Program& prog, const JsOptions& opt, std::string* dts, std::string* map) {
    JsGen g(prog, opt);
    string body = g.program();
    if (dts && opt.module) {
        std::ostringstream d;
        d << "// Generated by `rakupp --target=js --module` — rakupp " << opt.version << ", source " << sourceHash(opt.srcText) << "\n";
        d << "export interface RakuObject { [method: string]: any; toString(): string; }\n";
        d << "export interface RakuMatch extends RakuObject { made: any; [capture: string]: any; }\n";
        d << "export interface RakuClass { (...args: any[]): RakuObject; new: (...args: any[]) => RakuObject; type: any; }\n";
        d << "export interface RakuGrammar extends RakuClass { parse(text: string, opts?: Record<string, any>): RakuMatch | undefined; subparse(text: string, opts?: Record<string, any>): RakuMatch | undefined; }\n";
        d << "export interface RakuEnum extends RakuClass { [member: string]: any; }\n";
        for (auto& e : g.exportTable) {
            string ty = e.name == "MAIN" ? "(...argv: string[]) => any" : e.dts;
            if (jsIdent(e.name)) d << "export const " << e.name << ": " << ty << ";\n";
            else d << "declare const " << JsGen::dtsIdent(e.name) << ": " << ty << ";\nexport { " << JsGen::dtsIdent(e.name) << " as " << jsStr(e.name) << " };\n";
        }
        d << "declare const _default: { " ;
        bool first = true;
        for (auto& e : g.exportTable) { d << (first ? "" : "; ") << jsStr(e.name) << ": " << (e.name == "MAIN" ? "(...argv: string[]) => any" : e.dts); first = false; }
        d << " };\nexport default _default;\n";
        *dts = d.str();
    }
    std::ostringstream o;
    o << "// Generated by `rakupp --target=js` — rakupp " << opt.version << ", source " << sourceHash(opt.srcText) << "\n";
    o << jsManifestLine(opt, "js");
    if (opt.standalone) {
        o << "function __rakupp_program(R) {\n'use strict';\n";
        o << "R.main(" << (g.mainAsync ? "async " : "") << "() => {\n" << body << "}, { mainExit: true });\n";
        o << "}\n";
        o << "// ---- rakupp-rt.js (embedded by --standalone) ----\n";
        o << jsRuntimeSource();
        o << "\n__rakupp_program(R);\n";
    } else if (opt.module) {
        // an ES module: the mainline runs at import (top-level await), then the exports
        o << "import R from " << jsStr(opt.rtPath) << ";\n";
        o << "const __m = await R.module(" << (g.mainAsync ? "async " : "") << "() => {\n" << body << "});\n";
        int i = 0;
        for (auto& e : g.exportTable) {
            string tmp = "__e" + std::to_string(++i);
            o << "const " << tmp << " = __m[" << jsStr(e.name) << "];\n";
            o << "export { " << tmp << " as " << (jsIdent(e.name) ? e.name : jsStr(e.name)) << " };\n";
        }
        o << "export default __m;\n";
    } else {
        o << "import R from " << jsStr(opt.rtPath) << ";\n";
        o << "R.main(" << (g.mainAsync ? "async " : "") << "() => {\n" << body << "}, { mainExit: true });\n";
    }
    std::vector<int> srcLines;
    std::string text = stripLineMarkers(o.str(), srcLines);
    if (map) {
        std::string file = opt.mapUrl.size() > 4 ? opt.mapUrl.substr(0, opt.mapUrl.size() - 4) : "";
        std::string srcName = opt.srcPath; size_t sl = srcName.find_last_of("/\\"); if (sl != std::string::npos) srcName = srcName.substr(sl + 1);
        *map = sourceMapJson(srcLines, file, srcName, opt.srcText);
    }
    if (!opt.mapUrl.empty()) text += "//# sourceMappingURL=" + opt.mapUrl + "\n";
    return text;
}

std::string jsWasmWrapper(const std::string& src, const JsOptions& opt) {
    std::ostringstream o;
    o << "// Generated by `rakupp --target=js --fallback=wasm` — rakupp " << opt.version << ", source " << sourceHash(src) << "\n";
    o << "// This program is outside the JavaScript core; it runs its Raku source through\n";
    o << "// Raku.js, the interpreter compiled to WebAssembly (rakujs.js + rakujs.wasm from\n";
    o << "// the rakujs-<version>.zip release asset), found next to this file or via RAKUJS.\n";
    o << jsManifestLine(opt, "js-wasm");
    o << "const SOURCE = " << jsStr(src) << ";\n";
    o << R"JS((async () => {
  const isNode = typeof process !== 'undefined' && process.versions && (process.versions.node || process.versions.bun);
  const rakuStr = s => '"' + s.replace(/[\\"$@{]/g, m => '\\' + m).replace(/\n/g, '\\n') + '"';
  if (isNode) {
    const path = require('path'), fs = require('fs');
    const here = typeof __dirname !== 'undefined' ? __dirname : path.dirname(process.argv[1] || '.');
    const candidates = [process.env.RAKUJS, path.join(here, 'rakujs.js'), path.join(here, 'rakujs', 'rakujs.js'), path.join(here, 'node-build', 'rakujs.js')].filter(Boolean);
    const found = candidates.find(p => fs.existsSync(p));
    if (!found) {
      process.stderr.write('rakujs.js not found: put the Raku.js engine (rakujs.js and rakujs.wasm, from the rakujs-<version>.zip release asset built with node support) next to this program, or set RAKUJS=/path/to/rakujs.js\n');
      process.exit(5);
    }
    const factory = require(found);
    const m = await factory({ print: t => process.stdout.write(t + '\n'), printErr: t => process.stderr.write(t + '\n') });
    let stdin = ''; try { stdin = fs.readFileSync(0, 'utf8'); } catch (e) { }
    const argv = process.argv.slice(2);
    const src = argv.length ? '@*ARGS = (' + argv.map(rakuStr).join(', ') + ',); ' + SOURCE : SOURCE;
    const rc = m.ccall('rakupp_run', 'number', ['string', 'string'], [src, stdin]);
    process.exitCode = rc;
  } else {
    const factory = globalThis.RakuJS;
    if (!factory) throw new Error('load rakujs.js (the Raku.js engine) before this program: it defines the RakuJS factory');
    const m = await factory({ print: t => console.log(t), printErr: t => console.error(t) });
    m.ccall('rakupp_run', 'number', ['string', 'string'], [SOURCE, '']);
  }
})();
)JS";
    return o.str();
}

} // namespace rakupp
