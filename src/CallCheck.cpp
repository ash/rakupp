#include "CallCheck.h"
#include "AsciiCtype.h"
#include "Ast.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace rakupp {

std::string renderSignatureParam(const Param& p, bool inSignature);   // Builtins.cpp
std::shared_ptr<Param> signatureParamCopy(const Param& p);            // Builtins.cpp
bool isPragmaName(const std::string& n);                              // Interpreter.cpp
bool importsMaySupplyName(const std::set<std::string>& imports,       // DeclCheck.cpp
                          const std::vector<std::string>& searchPath, bool sixE,
                          const std::string& name);

namespace {

// ---- the type lattice ------------------------------------------------------

// Rakudo's nqp::istype over the type objects of the core types this pass can
// place, as Rakudo 2026.09 answers it: each type, then every type it is. Two
// answers are less obvious than the rest — every ROLE's type object is Cool
// (`Positional ~~ Cool`, which is why a Cool argument never rules out a Numeric
// parameter), and so is Nil. A type missing from this table is one the pass
// cannot place, and a call that needs it is left to run.
const char* const kCoreLattice[][2] = {
    {"Mu", ""},
    {"Any", "Mu"},
    {"Cool", "Mu Any"},
    {"Junction", "Mu"},
    {"Whatever", "Mu Any"},
    {"WhateverCode", "Mu Any Callable Code"},
    {"HyperWhatever", "Mu Any"},
    {"Nil", "Mu Any Cool"},
    {"Failure", "Mu Any Cool Nil"},
    {"Exception", "Mu Any"},
    {"Int", "Mu Any Cool Numeric Real"},
    {"Num", "Mu Any Cool Numeric Real"},
    {"Rat", "Mu Any Cool Numeric Real Rational"},
    {"FatRat", "Mu Any Cool Numeric Real Rational"},
    {"Complex", "Mu Any Cool Numeric"},
    {"Bool", "Mu Any Cool Int Numeric Real"},
    {"Str", "Mu Any Cool Stringy"},
    {"Numeric", "Mu Any Cool"},
    {"Real", "Mu Any Cool Numeric"},
    {"Rational", "Mu Any Cool Numeric Real"},
    {"Stringy", "Mu Any Cool"},
    {"Dateish", "Mu Any Cool"},
    {"IntStr", "Mu Any Cool Int Str Numeric Real Stringy Allomorph"},
    {"NumStr", "Mu Any Cool Num Str Numeric Real Stringy Allomorph"},
    {"RatStr", "Mu Any Cool Rat Str Numeric Real Rational Stringy Allomorph"},
    {"ComplexStr", "Mu Any Cool Complex Str Numeric Stringy Allomorph"},
    {"Allomorph", "Mu Any Cool Str Stringy"},
    {"Positional", "Mu Any Cool"},
    {"Associative", "Mu Any Cool"},
    {"Callable", "Mu Any Cool"},
    {"Iterable", "Mu Any Cool"},
    {"Iterator", "Mu Any Cool"},
    {"PositionalBindFailover", "Mu Any Cool"},
    {"Sequence", "Mu Any Cool PositionalBindFailover"},
    {"List", "Mu Any Cool Positional Iterable"},
    {"Array", "Mu Any Cool Positional Iterable List"},
    {"Hash", "Mu Any Cool Associative Iterable Map"},
    {"Map", "Mu Any Cool Associative Iterable"},
    {"Seq", "Mu Any Cool Iterable PositionalBindFailover Sequence"},
    {"Slip", "Mu Any Cool Positional Iterable List"},
    {"Range", "Mu Any Cool Positional Iterable"},
    {"Pair", "Mu Any Associative"},
    {"Capture", "Mu Any"},
    {"Signature", "Mu Any"},
    {"Parameter", "Mu Any"},
    {"Code", "Mu Any Callable"},
    {"Block", "Mu Any Callable Code"},
    {"Routine", "Mu Any Callable Code Block"},
    {"Sub", "Mu Any Callable Code Block Routine"},
    {"Method", "Mu Any Callable Code Block Routine"},
    {"Regex", "Mu Any Callable Code Block Routine Method"},
    {"Submethod", "Mu Any Callable Code Block Routine"},
    {"Match", "Mu Any Cool Capture"},
    {"Grammar", "Mu Any Cool Capture Match"},
    {"Set", "Mu Any Associative Setty QuantHash"},
    {"Bag", "Mu Any Associative Baggy QuantHash"},
    {"Mix", "Mu Any Associative Baggy Mixy QuantHash"},
    {"SetHash", "Mu Any Associative Setty QuantHash"},
    {"BagHash", "Mu Any Associative Baggy QuantHash"},
    {"MixHash", "Mu Any Associative Baggy Mixy QuantHash"},
    {"Setty", "Mu Any Cool Associative QuantHash"},
    {"Baggy", "Mu Any Cool Associative QuantHash"},
    {"Mixy", "Mu Any Cool Associative Baggy QuantHash"},
    {"QuantHash", "Mu Any Cool Associative"},
    {"Date", "Mu Any Dateish"},
    {"DateTime", "Mu Any Dateish"},
    {"Instant", "Mu Any Cool Numeric Real"},
    {"Duration", "Mu Any Cool Numeric Real"},
    {"IO", "Mu Any Cool"},
    {"IO::Path", "Mu Any Cool IO"},
    {"IO::Handle", "Mu Any"},
    {"Proc", "Mu Any"},
    {"Promise", "Mu Any"},
    {"Supply", "Mu Any"},
    {"Supplier", "Mu Any"},
    {"Channel", "Mu Any"},
    {"Thread", "Mu Any"},
    {"Lock", "Mu Any"},
    {"Semaphore", "Mu Any"},
    {"Version", "Mu Any"},
    {"Blob", "Mu Any Cool Stringy Positional"},
    {"Buf", "Mu Any Cool Stringy Positional Blob"},
    {"Uni", "Mu Any Stringy Positional"},
    {"Order", "Mu Any Cool Int Numeric Real Enumeration"},
    {"Enumeration", "Mu Any Cool"},
    {"Label", "Mu Any"},
    {"Attribute", "Mu Any"},
    {"Stash", "Mu Any Cool Associative Iterable Hash Map"},
    {"Scalar", "Mu Any"},
    {"ObjAt", "Mu Any"},
};

const std::map<std::string, std::set<std::string>>& coreLattice() {
    static const auto* table = [] {
        auto* t = new std::map<std::string, std::set<std::string>>;
        for (auto& row : kCoreLattice) {
            auto& sup = (*t)[row[0]];
            std::string w;
            for (const char* c = row[1];; c++) {
                if (*c && *c != ' ') { w += *c; continue; }
                if (!w.empty()) sup.insert(w);
                w.clear();
                if (!*c) break;
            }
        }
        return t;
    }();
    return *table;
}

// The concrete core types a value can be when all that is known of it is the
// ROLE it was declared with: an `@a` parameter holds one of these, never a bare
// Positional. Rakudo's verdict reads the role itself; whether a call is truly
// doomed is asked of these (and of the unit's own classes that do the role).
const std::map<std::string, std::vector<std::string>>& roleDoers() {
    static const std::map<std::string, std::vector<std::string>> doers = {
        {"Positional", {"List", "Array", "Slip", "Range", "Blob", "Buf", "Uni"}},
        {"Associative", {"Map", "Hash", "Pair", "Set", "Bag", "Mix", "SetHash", "BagHash", "MixHash", "Stash"}},
        {"Callable", {"Code", "Block", "Routine", "Sub", "Method", "Regex", "Submethod", "WhateverCode"}},
    };
    return doers;
}

// The native kinds as Rakudo's binder tells them apart: 'i' int, 'u' uint,
// 'n' num, 's' str; 0 for a type that is not native.
char nativeKind(const std::string& t) {
    static const std::map<std::string, char> kinds = {
        {"int", 'i'},   {"int8", 'i'},   {"int16", 'i'},  {"int32", 'i'},  {"int64", 'i'},
        {"uint", 'u'},  {"uint8", 'u'},  {"uint16", 'u'}, {"uint32", 'u'}, {"uint64", 'u'},
        {"byte", 'u'},  {"num", 'n'},    {"num32", 'n'},  {"num64", 'n'},  {"str", 's'}};
    auto it = kinds.find(t);
    return it == kinds.end() ? 0 : it->second;
}
const char* boxedName(char k) { return k == 'n' ? "Num" : k == 's' ? "Str" : "Int"; }
const char* nativeName(char k) { return k == 'u' ? "uint" : k == 'n' ? "num" : k == 's' ? "str" : "int"; }

// `Positional[Int]` → base `Positional`, element `Int`
bool splitParametric(const std::string& t, std::string& base, std::string& elem) {
    size_t b = t.find('[');
    if (b == std::string::npos || t.back() != ']') return false;
    base = t.substr(0, b);
    elem = t.substr(b + 1, t.size() - b - 2);
    return true;
}

// A type this unit declares: what its declaration says it is.
struct UnitType {
    char kind = 0;                     // 'c' class, 'r' role, 's' subset
    std::vector<std::string> parents;  // `is` parents
    std::vector<std::string> roles;    // `does` roles
    std::string base;                  // a subset's base type ("" = Any)
    bool constrained = false;          // a subset with a `where`
    bool opaque = false;               // declared in a way this pass cannot place
};

struct Types {
    std::map<std::string, UnitType> unit;
    std::map<std::string, std::unique_ptr<std::set<std::string>>> memo;  // null: cannot be placed
    std::set<std::string> resolving;

    void declare(const std::string& name, UnitType t) {
        if (name.empty()) return;
        auto it = unit.find(name);
        // Declared twice, or over a core name: which one a mention means is a
        // question of scope this pass does not track.
        if (it != unit.end() || coreLattice().count(name) || nativeKind(name) || name == "UInt") {
            unit[name].opaque = true;
            return;
        }
        unit.emplace(name, std::move(t));
    }
    void opaque(const std::string& name) {
        if (!name.empty()) unit[name].opaque = true;
    }

    void addRole(const std::string& r, std::set<std::string>& out, bool& ok, int depth = 0) {
        if (depth > 32) { ok = false; return; }
        auto ut = unit.find(r);
        if (ut != unit.end()) {
            const UnitType& u = ut->second;
            if (u.kind != 'r' || u.opaque || !u.parents.empty()) { ok = false; return; }
            out.insert(r);
            for (auto& rr : u.roles) addRole(rr, out, ok, depth + 1);
            return;
        }
        // a core role: a class that does it is it, and whatever it is — save
        // Cool, which only the role's own type object claims
        auto c = coreLattice().find(r);
        if (c == coreLattice().end()) { ok = false; return; }
        out.insert(r);
        for (auto& s : c->second)
            if (s != "Cool") out.insert(s);
    }

    // Every type `t` is (itself excluded), or null for a type this pass cannot place.
    const std::set<std::string>* supers(const std::string& t) {
        auto ut = unit.find(t);
        if (ut == unit.end()) {
            auto c = coreLattice().find(t);
            return c == coreLattice().end() ? nullptr : &c->second;
        }
        if (ut->second.opaque || ut->second.kind == 's') return nullptr;
        auto m = memo.find(t);
        if (m != memo.end()) return m->second.get();
        if (!resolving.insert(t).second) return nullptr;   // an inheritance cycle
        auto out = std::make_unique<std::set<std::string>>();
        bool ok = true;
        const UnitType u = ut->second;
        if (u.kind == 'r') {
            // a role's type object: Mu, Any and — as every role's is — Cool. What
            // one that does other roles answers is not pinned down here.
            out->insert({"Mu", "Any", "Cool"});
            if (!u.roles.empty() || !u.parents.empty()) ok = false;
        }
        else {
            out->insert({"Mu", "Any"});
            for (auto& p : u.parents) {
                auto pu = unit.find(p);
                const std::set<std::string>* s = supers(p);
                if (!s || (pu != unit.end() && pu->second.kind != 'c')) { ok = false; break; }
                out->insert(p);
                out->insert(s->begin(), s->end());
            }
            for (auto& r : u.roles) addRole(r, *out, ok);
        }
        resolving.erase(t);
        if (!ok) out.reset();
        const std::set<std::string>* res = out.get();
        memo[t] = std::move(out);
        return res;
    }

    // Rakudo's nqp::istype for two type objects: 1 yes, 0 no, -1 a question
    // this pass cannot answer.
    int isa(const std::string& a, const std::string& b) {
        if (a == b || b == "Mu") return 1;
        std::string ab, ae, bb, be;
        const bool ap = splitParametric(a, ab, ae), bp = splitParametric(b, bb, be);
        if (bp) {
            // `Positional[Int]`: a plain type can only be it if it is the role
            if (!ap) return isa(a, bb) == 0 ? 0 : -1;
            return ae == be && isa(ab, bb) == 1 ? 1 : -1;
        }
        if (ap) return isa(ab, b);   // `Array[Int]` is whatever Array is
        const std::set<std::string>* s = supers(a);
        if (!s || !supers(b)) return -1;
        return s->count(b) ? 1 : 0;
    }

    bool isSubset(const std::string& t) const {
        auto ut = unit.find(t);
        return ut != unit.end() ? ut->second.kind == 's' : t == "UInt";
    }

    // The nominal type a parameter or a variable checks against, for a type
    // NAME as declared: a subset is its base type, all the way down (UInt is
    // Int), and says so through `constrained`. "" for a name this pass cannot
    // place.
    std::string nominal(const std::string& t, bool& constrained, int depth = 0) {
        if (t.empty()) return "Any";
        if (depth > 16) return "";
        if (t == "UInt") { constrained = true; return "Int"; }
        // `CORE::Int` is the setting's Int, never a type of the unit's own name
        if (t.size() > 6 && t.compare(0, 6, "CORE::") == 0) return coreLattice().count(t.substr(6)) ? t.substr(6) : "";
        auto ut = unit.find(t);
        if (ut != unit.end()) {
            const UnitType& u = ut->second;
            if (u.opaque) return "";
            if (u.kind == 's') {
                constrained = constrained || u.constrained;
                return nominal(u.base, constrained, depth + 1);
            }
            return supers(t) ? t : "";
        }
        return coreLattice().count(t) ? t : "";
    }
};

// ---- what a call passes ------------------------------------------------------

struct ArgT {
    std::string type;    // what Rakudo's optimizer knows the argument to be
    std::string shown;   // how the message lists it
    char prim = 0;       // a native: a native variable's kind, or a lone literal's
    char lit = 0;        // a literal Rakudo compiles to a constant with a native reading
    // The concrete types its value can have at run time, when that is not
    // `type` itself: `my @a` is typed Positional and IS an Array.
    std::vector<std::string> truth;
};

enum { NO_WAY = -1, NOT_SURE = 0, BINDS = 1 };
const int kSlurpyArity = 1 << 30;

bool hasWhere(const Param& p) { return p.whereExpr || p.hadWhere; }
bool isCapture(const Param& p) {
    return p.slurpy && p.slurpyKind == 0 && (p.sigil == '|' || p.sigil == '\\');
}
bool hasDefault(const Param& p) { return p.defaultVal || !p.defaultRaku.empty(); }

// A multi candidate as Rakudo's dispatcher sorts and matches it.
struct Cand {
    const SubDecl* sd = nullptr;
    std::vector<std::string> types;
    std::vector<char> constraints, rw, native, defcon;
    int minArity = 0, maxArity = 0, numTypes = 0;
    bool bindCheck = false;
};

struct Routines {     // what one scope declares under one name
    const SubDecl* only = nullptr;
    const SubDecl* proto = nullptr;
    std::vector<const SubDecl*> multis;
    int onlys = 0, protos = 0;
};

struct Scope {
    std::map<std::string, Routines> subs;
    std::map<std::string, ArgT> vars;   // a variable the scope declares; type "" = untyped
    std::set<std::string> terms;        // names that are not routines here: `my &f`, `my \f`, a constant
};

} // namespace

// The names `use Test` puts in scope: a sub of the same name may be meeting
// Test's own candidates, which this pass does not see. Outside the anonymous
// namespace because the --exe backend (Codegen.cpp) asks it too: a compiled
// Test call names its line.
const std::set<std::string>& testExports() {
    static const std::set<std::string> names = {
        "plan", "done-testing", "ok", "nok", "is", "isnt", "is-approx", "is-approx-rel", "cmp-ok",
        "like", "unlike", "isa-ok", "does-ok", "can-ok", "use-ok", "dies-ok", "lives-ok",
        "eval-dies-ok", "eval-lives-ok", "throws-like", "fails-like", "is-deeply", "diag", "skip",
        "skip-rest", "todo", "pass", "flunk", "subtest", "bail-out", "isa_ok", "is_deeply",
        "is_approx", "cmp_ok", "dies_ok", "lives_ok", "eval_dies_ok", "eval_lives_ok", "use_ok",
        "throws_like", "done_testing", "skip_rest", "bail_out", "output-is", "output-like",
        "is-run", "is_run", "warns-like", "doesn't-warn", "doesn't-hang"};
    return names;
}

namespace {

// ---- a walk over every node, for the facts gathered before the real walk ---

struct Visitor {
    std::function<void(const Stmt*)> onStmt;
    std::function<void(const Expr*)> onExpr;

    void params(const std::vector<Param>& ps) {
        for (auto& p : ps) {
            expr(p.defaultVal.get());
            expr(p.whereExpr.get());
            expr(p.litVal.get());
            for (auto& t : p.userTraits) expr(t.second.get());
            if (p.subSig) params(*p.subSig);
        }
    }
    void stmts(const std::vector<StmtPtr>& ss) { for (auto& s : ss) stmt(s.get()); }
    void block(const Block* b) { if (b) stmts(b->stmts); }

    void expr(const Expr* e) {
        if (!e) return;
        if (onExpr) onExpr(e);
        switch (e->kind) {
            case NK::VarExpr: {
                auto* v = static_cast<const VarExpr*>(e);
                expr(v->d().declDefault.get()); expr(v->d().declShape.get()); expr(v->d().declTypeExpr.get());
                expr(v->d().declWhereExpr);
                return;
            }
            case NK::Call: {
                auto* c = static_cast<const Call*>(e);
                expr(c->callee.get());
                for (auto& a : c->args) expr(a.get());
                return;
            }
            case NK::MethodCall: {
                auto* m = static_cast<const MethodCall*>(e);
                expr(m->inv.get()); expr(m->methodExpr.get());
                for (auto& a : m->args) expr(a.get());
                return;
            }
            case NK::Unary: expr(static_cast<const Unary*>(e)->operand.get()); return;
            case NK::Assign: {
                auto* a = static_cast<const Assign*>(e);
                expr(a->target.get()); expr(a->value.get()); return;
            }
            case NK::Binary: {
                auto* b = static_cast<const Binary*>(e);
                expr(b->lhs.get()); expr(b->rhs.get()); return;
            }
            case NK::Index: {
                auto* i = static_cast<const Index*>(e);
                expr(i->base.get()); expr(i->index.get()); return;
            }
            case NK::Ternary: {
                auto* t = static_cast<const Ternary*>(e);
                expr(t->cond.get()); expr(t->then.get()); expr(t->els.get()); return;
            }
            case NK::Range: {
                auto* r = static_cast<const RangeExpr*>(e);
                expr(r->from.get()); expr(r->to.get()); return;
            }
            case NK::Pair: {
                auto* p = static_cast<const PairExpr*>(e);
                expr(p->keyExpr.get()); expr(p->value.get()); return;
            }
            case NK::BlockExpr: {
                auto* be = static_cast<const BlockExpr*>(e);
                params(be->params);
                for (auto& t : be->userTraits) expr(t.arg.get());
                stmts(be->body);
                return;
            }
            case NK::SymbolicRef: {
                auto* s = static_cast<const SymbolicRef*>(e);
                expr(s->nameExpr.get());
                for (auto& x : s->segs) expr(x.get());
                return;
            }
            case NK::AllomorphLit: expr(static_cast<const AllomorphLit*>(e)->num.get()); return;
            case NK::ListExpr:  for (auto& x : static_cast<const ListExpr*>(e)->items) expr(x.get()); return;
            case NK::ArrayLit:  for (auto& x : static_cast<const ArrayLit*>(e)->items) expr(x.get()); return;
            case NK::HashLit:   for (auto& x : static_cast<const HashLit*>(e)->items) expr(x.get()); return;
            case NK::InterpStr: for (auto& x : static_cast<const InterpStr*>(e)->parts) expr(x.get()); return;
            case NK::ChainExpr: for (auto& x : static_cast<const ChainExpr*>(e)->operands) expr(x.get()); return;
            case NK::NqpOp:     for (auto& x : static_cast<const NqpOp*>(e)->args) expr(x.get()); return;
            default: return;
        }
    }

    void sub(const SubDecl* sd) {
        expr(sd->nameExpr.get());
        params(sd->params);
        for (auto& a : sd->altParams) params(a);
        for (auto& t : sd->traits) expr(t.arg.get());
        for (auto& a : sd->immediateArgs) expr(a.get());
        expr(sd->nativeLibExpr.get()); expr(sd->nativeSymExpr.get()); expr(sd->deprecatedWith.get());
        expr(sd->retLiteral.get());
        stmts(sd->body);
    }

    void stmt(const Stmt* s) {
        if (!s) return;
        if (onStmt) onStmt(s);
        switch (s->kind) {
            case NK::ExprStmt: expr(static_cast<const ExprStmt*>(s)->e.get()); return;
            case NK::VarDecl:  expr(static_cast<const VarDecl*>(s)->init.get()); return;
            case NK::SubDecl:  sub(static_cast<const SubDecl*>(s)); return;
            case NK::Block:    block(static_cast<const Block*>(s)); return;
            case NK::IfStmt: {
                auto* f = static_cast<const IfStmt*>(s);
                for (auto& br : f->branches) { expr(br.first.get()); block(br.second.get()); }
                for (auto& bp : f->branchParams) params(bp);
                params(f->elseParams);
                block(f->elseBlock.get());
                return;
            }
            case NK::WhileStmt: {
                auto* w = static_cast<const WhileStmt*>(s);
                expr(w->cond.get()); params(w->params); block(w->body.get()); return;
            }
            case NK::ForStmt: {
                auto* f = static_cast<const ForStmt*>(s);
                expr(f->list.get()); params(f->params); block(f->body.get()); return;
            }
            case NK::LoopStmt: {
                auto* l = static_cast<const LoopStmt*>(s);
                expr(l->init.get()); expr(l->cond.get()); expr(l->incr.get()); block(l->body.get()); return;
            }
            case NK::RepeatStmt: {
                auto* r = static_cast<const RepeatStmt*>(s);
                block(r->body.get()); expr(r->cond.get()); return;
            }
            case NK::GivenStmt: {
                auto* g = static_cast<const GivenStmt*>(s);
                expr(g->topic.get()); params(g->params); params(g->elseParams);
                block(g->body.get()); block(g->elseBody.get());
                return;
            }
            case NK::WhenStmt: {
                auto* w = static_cast<const WhenStmt*>(s);
                expr(w->cond.get()); block(w->body.get()); return;
            }
            case NK::ReturnStmt: expr(static_cast<const ReturnStmt*>(s)->value.get()); return;
            case NK::UseStmt: {
                auto* u = static_cast<const UseStmt*>(s);
                expr(u->argExpr.get()); expr(u->ifCond.get()); expr(u->fileExpr.get()); return;
            }
            case NK::EnumDecl:   expr(static_cast<const EnumDecl*>(s)->values.get()); return;
            case NK::SubsetDecl: expr(static_cast<const SubsetDecl*>(s)->where.get()); return;
            case NK::ClassDecl: {
                auto* c = static_cast<const ClassDecl*>(s);
                expr(c->nameExpr.get());
                expr(c->verExpr.get()); expr(c->authExpr.get()); expr(c->apiExpr.get());
                params(c->roleParams);
                for (auto& ra : c->roleArgs) for (auto& x : ra.second) expr(x.get());
                for (auto& t : c->userTraits) expr(t.second.get());
                for (auto& a : c->attrs) {
                    expr(a.def.get()); expr(a.whereExpr.get()); expr(a.defaultTrait.get()); expr(a.shape.get());
                    for (auto& t : a.userTraits) expr(t.second.get());
                }
                stmts(c->body);
                for (auto& m : c->methods) if (m) sub(m.get());
                return;
            }
            default: return;
        }
    }
};

// ---- the check ---------------------------------------------------------------

struct Checker {
    Types T;
    std::vector<Scope> scopes;
    std::vector<DoomedCall> out;
    std::set<std::string> imports;       // non-pragma modules the unit `use`s
    std::vector<std::string> extraLibs;  // literal `use lib` directories
    bool usesTest = false;
    bool standDown = false;              // the unit can conjure routines at compile time
    std::set<const SubDecl*> implicitArgs;   // sig-less subs whose body reads @_, %_ or a placeholder
    std::set<const Expr*> feedTargets;        // the call a feed (`==>`, `<==`) appends its list to
    const std::function<bool(const std::string&)>* isCore = nullptr;
    int curLine = 0;
    int langRev = 0;                     // the unit's language revision (2 = 6.e)

    int lineOf(const Node* n) const { return n && n->line ? n->line : curLine; }

    // ---- facts about the whole unit, gathered first --------------------------

    void survey(const std::vector<StmtPtr>& stmts) {
        Visitor v;
        v.onStmt = [&](const Stmt* s) {
            if (s->kind == NK::ClassDecl) {
                auto* c = static_cast<const ClassDecl*>(s);
                if (c->isStubDecl) return;   // a forward declaration names what follows
                if (c->nameExpr || c->isAugment || c->isPackage || c->isModuleDecl || c->parameterized ||
                    c->isAnonDecl || !c->howName.empty() || !c->userTraits.empty()) {
                    T.opaque(c->name);
                    return;
                }
                UnitType u;
                u.kind = c->isRole ? 'r' : 'c';
                if (!c->parent.empty()) (c->parentIsDoes ? u.roles : u.parents).push_back(c->parent);
                for (auto& p : c->extraParents) u.parents.push_back(p);
                for (auto& r : c->roles) u.roles.push_back(r);
                if (c->isGrammar && u.parents.empty()) u.parents.push_back("Grammar");
                T.declare(c->name, std::move(u));
            }
            else if (s->kind == NK::SubsetDecl) {
                auto* d = static_cast<const SubsetDecl*>(s);
                UnitType u;
                u.kind = 's';
                u.base = d->baseType;
                u.constrained = d->where != nullptr;
                if (d->coerceBase || d->defConstraint) u.opaque = true;
                T.declare(d->name, std::move(u));
            }
            else if (s->kind == NK::EnumDecl) {
                // `enum E <a b>` is an Int enumeration, which its type object
                // says as Order does; any other enum is left unplaced
                auto* d = static_cast<const EnumDecl*>(s);
                bool words = d->ofType.empty() && d->values && d->values->kind == NK::ArrayLit;
                if (words)
                    for (auto& x : static_cast<const ArrayLit*>(d->values.get())->items)
                        if (!x || x->kind != NK::StrLit) words = false;
                if (!words) { T.opaque(d->name); return; }
                UnitType u;
                u.kind = 'c';
                u.parents = {"Int"};
                u.roles = {"Enumeration"};
                T.declare(d->name, std::move(u));
            }
            else if (s->kind == NK::UseStmt) {
                auto* u = static_cast<const UseStmt*>(s);
                // `require` and `import` bring names in where this pass cannot follow
                if (u->isRequire || u->isImport) { standDown = true; return; }
                if (u->module == "lib") {
                    if (!u->arg.empty()) extraLibs.push_back(u->arg);
                    for (auto& a : u->importArgs) extraLibs.push_back(a);
                    if (u->argExpr && u->arg.empty()) standDown = true;
                    return;
                }
                if (u->module == "Test") usesTest = true;
                if (!u->isNeed && !u->isNo && !u->module.empty() && !isPragmaName(u->module))
                    imports.insert(u->module);
            }
            else if (s->kind == NK::SubDecl) {
                auto* sd = static_cast<const SubDecl*>(s);
                if (!sd->hadSig) noteImplicitArgs(sd);
            }
        };
        v.onExpr = [&](const Expr* e) {
            // `require` as a term, and a symbolic lookup that could name a sub
            if (e->kind == NK::Unary && static_cast<const Unary*>(e)->op == "require") standDown = true;
        };
        v.stmts(stmts);
    }

    // A sub with no signature takes no arguments — unless its body reads @_,
    // %_ or a placeholder, anywhere at all, which gives it one.
    void noteImplicitArgs(const SubDecl* sd) {
        bool found = false;
        Visitor v;
        v.onExpr = [&](const Expr* e) {
            if (e->kind != NK::VarExpr) return;
            const std::string& n = static_cast<const VarExpr*>(e)->name;
            if (n == "@_" || n == "%_" || (n.size() > 2 && (n[1] == '^' || n[1] == ':'))) found = true;
        };
        v.stmts(sd->body);
        if (found) implicitArgs.insert(sd);
    }

    // ---- declarations --------------------------------------------------------

    Scope& cur() { return scopes.back(); }

    void declareSub(const SubDecl* sd) {
        if (sd->isMethod || sd->isSubmethod || sd->name.empty() || sd->nameExpr) return;
        Routines& r = cur().subs[sd->name];
        if (sd->isProto) { r.proto = sd; r.protos++; }
        else if (sd->isMulti) r.multis.push_back(sd);
        else { r.only = sd; r.onlys++; }
    }

    // What Rakudo's optimizer knows a declared variable to hold: its nominal
    // type, for a `$` variable typed with a class or role (a subset, a
    // coercion or a `where` leaves it unknown); Positional / Associative for
    // `@` / `%`; Callable for `&`.
    ArgT declaredVarType(const VarExpr* v) {
        ArgT a;
        const std::string& n = v->name;
        if (n.size() < 2 || !v->d().declCoerce.empty() || v->d().declWhereExpr || v->d().declHasWhere || v->d().declTypeExpr ||
            !v->d().containerIs.empty() || !v->d().declStubType.empty() || v->d().declShape || v->d().declScope == "constant")
            return a;
        containerType(n[0], v->d().declType, /*exact=*/true, a);
        return a;
    }

    // The type a `$`/`@`/`%`/`&` container declared with `t` is known to hold.
    // `exact` says the container IS the default Array/Hash (a declared
    // variable); a parameter's could be any Positional/Associative.
    void containerType(char sigil, const std::string& t, bool exact, ArgT& a) {
        bool constrained = false;
        if (sigil == '$') {
            if (t.empty()) return;
            if (char k = nativeKind(t)) {
                a.type = boxedName(k); a.shown = nativeName(k); a.prim = k;
                return;
            }
            if (exact && T.isSubset(t)) return;   // a subset-typed VARIABLE is untyped to Rakudo
            std::string nt = T.nominal(t, constrained);
            if (nt.empty()) return;
            a.type = a.shown = nt;
            return;
        }
        if (sigil == '@' || sigil == '%') {
            const std::string base = sigil == '@' ? "Positional" : "Associative";
            const std::string actual = sigil == '@' ? "Array" : "Hash";
            std::string elem;
            if (!t.empty()) {
                elem = nativeKind(t) ? t : T.nominal(t, constrained);
                if (elem.empty() || constrained) return;
            }
            if (exact) {
                a.type = a.shown = elem.empty() ? base : base + "[" + elem + "]";
                a.truth = {elem.empty() ? actual : actual + "[" + elem + "]"};
            }
            else if (elem.empty()) {   // an untyped `@a` / `%h` parameter
                a.type = a.shown = base;
                a.truth = doersOf(base);
            }
            return;
        }
        if (sigil == '&' && t.empty()) {
            a.type = a.shown = "Callable";
            a.truth = doersOf("Callable");
        }
    }

    std::vector<std::string> doersOf(const std::string& role) {
        std::vector<std::string> d = roleDoers().at(role);
        for (auto& kv : T.unit)
            if (kv.second.kind == 'c') {
                const std::set<std::string>* s = T.supers(kv.first);
                // a unit class that does the role — or one this pass cannot place, which might
                if (!s || s->count(role)) d.push_back(kv.first);
            }
        return d;
    }

    void declareVar(const std::string& name, const ArgT& a) {
        if (name.size() > 1 && name[0] == '&') cur().terms.insert(name.substr(1));
        else if (!name.empty() && !std::strchr("$@%", name[0])) {   // a sigilless name
            cur().terms.insert(name[0] == '\\' ? name.substr(1) : name);
            return;
        }
        cur().vars[name] = a;
    }

    void declareParam(const Param& p) {
        if (p.name.empty()) return;
        ArgT a;
        // A parameter is typed by its nominal type — through a subset, a `where`
        // and a smiley alike; a coercion, a capture, a slurpy or a sigilless
        // name leaves it unknown
        if (!p.coerce && !p.typeCapture && !p.typeFromCapture && !p.slurpy && !p.subSig &&
            !p.codeSig && p.sigil != '\\' && p.type.find("::?") == std::string::npos)
            containerType(p.sigil, p.type, /*exact=*/false, a);
        declareVar(p.name, a);
        if (p.subSig) for (auto& q : *p.subSig) declareParam(q);
    }

    void collectExpr(const Expr* e) {
        if (!e) return;
        switch (e->kind) {
            case NK::VarExpr: {
                auto* v = static_cast<const VarExpr*>(e);
                if (v->declare) {
                    if (v->d().declScope == "constant") {
                        std::string n = v->name;
                        if (!n.empty() && std::strchr("$@%&\\", n[0])) n = n.substr(1);
                        cur().terms.insert(n);
                    }
                    else declareVar(v->name, declaredVarType(v));
                }
                collectExpr(v->d().declDefault.get());
                return;
            }
            case NK::Assign: {
                auto* a = static_cast<const Assign*>(e);
                collectExpr(a->target.get()); collectExpr(a->value.get()); return;
            }
            case NK::Binary: {
                auto* b = static_cast<const Binary*>(e);
                collectExpr(b->lhs.get()); collectExpr(b->rhs.get()); return;
            }
            case NK::Unary: collectExpr(static_cast<const Unary*>(e)->operand.get()); return;
            case NK::Call: {
                auto* c = static_cast<const Call*>(e);
                collectExpr(c->callee.get());
                for (auto& a : c->args) collectExpr(a.get());
                return;
            }
            case NK::MethodCall: {
                auto* m = static_cast<const MethodCall*>(e);
                collectExpr(m->inv.get());
                for (auto& a : m->args) collectExpr(a.get());
                return;
            }
            case NK::Index: {
                auto* i = static_cast<const Index*>(e);
                collectExpr(i->base.get()); collectExpr(i->index.get()); return;
            }
            case NK::Ternary: {
                auto* t = static_cast<const Ternary*>(e);
                collectExpr(t->cond.get()); collectExpr(t->then.get()); collectExpr(t->els.get()); return;
            }
            case NK::ListExpr: for (auto& x : static_cast<const ListExpr*>(e)->items) collectExpr(x.get()); return;
            case NK::Pair: collectExpr(static_cast<const PairExpr*>(e)->value.get()); return;
            default: return;
        }
    }

    void collectStmt(const Stmt* s) {
        if (!s) return;
        auto shared = [&](const Block* b) { if (b) for (auto& x : b->stmts) collectStmt(x.get()); };
        switch (s->kind) {
            case NK::ExprStmt: collectExpr(static_cast<const ExprStmt*>(s)->e.get()); return;
            case NK::VarDecl: {
                auto* d = static_cast<const VarDecl*>(s);
                for (auto& n : d->names) declareVar(n, ArgT{});
                collectExpr(d->init.get());
                return;
            }
            case NK::SubDecl: declareSub(static_cast<const SubDecl*>(s)); return;
            case NK::Block: {
                auto* b = static_cast<const Block*>(s);
                if (b->stmtForm) shared(b);
                return;
            }
            case NK::IfStmt: {
                auto* f = static_cast<const IfStmt*>(s);
                for (auto& br : f->branches) {
                    collectExpr(br.first.get());
                    if (f->modifier) shared(br.second.get());
                }
                if (f->modifier) shared(f->elseBlock.get());
                return;
            }
            case NK::WhileStmt: {
                auto* w = static_cast<const WhileStmt*>(s);
                collectExpr(w->cond.get());
                if (w->modifier) shared(w->body.get());
                return;
            }
            case NK::ForStmt: {
                auto* f = static_cast<const ForStmt*>(s);
                collectExpr(f->list.get());
                if (f->modifier) shared(f->body.get());
                return;
            }
            case NK::GivenStmt: {
                auto* g = static_cast<const GivenStmt*>(s);
                collectExpr(g->topic.get());
                if (g->modifier) shared(g->body.get());
                return;
            }
            case NK::LoopStmt: {
                auto* l = static_cast<const LoopStmt*>(s);
                collectExpr(l->init.get());
                return;
            }
            case NK::EnumDecl: {
                // the value names are terms here, and a term is no routine
                auto* d = static_cast<const EnumDecl*>(s);
                if (d->values && d->values->kind == NK::ArrayLit)
                    for (auto& x : static_cast<const ArrayLit*>(d->values.get())->items)
                        if (x && x->kind == NK::StrLit) cur().terms.insert(static_cast<const StrLit*>(x.get())->v);
                return;
            }
            default: return;
        }
    }

    // ---- the walk ------------------------------------------------------------

    void walkParamExprs(const std::vector<Param>& ps) {
        for (auto& p : ps) {
            walkExpr(p.defaultVal.get());
            walkExpr(p.whereExpr.get());
            if (p.subSig) walkParamExprs(*p.subSig);
        }
    }

    void walkBody(const std::vector<Param>& ps, const std::vector<StmtPtr>& body,
                  const std::vector<std::vector<Param>>* alts = nullptr) {
        scopes.emplace_back();
        for (auto& p : ps) declareParam(p);
        if (alts) for (auto& a : *alts) for (auto& p : a) declareVar(p.name, ArgT{});
        walkParamExprs(ps);
        walkStmts(body);
        scopes.pop_back();
    }

    void walkStmts(const std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts) collectStmt(s.get());
        for (auto& s : stmts) {
            if (s && s->line) curLine = s->line;
            walkStmt(s.get());
        }
    }

    void walkBlock(const Block* b) {
        if (!b) return;
        if (b->stmtForm) { walkStmts(b->stmts); return; }
        scopes.emplace_back();
        walkStmts(b->stmts);
        scopes.pop_back();
    }

    // a block with binders of its own: `for … -> Int $i { }`, `if … -> $x { }`
    void walkBound(const Block* b, const std::vector<Param>* ps, const std::vector<std::string>& names) {
        scopes.emplace_back();
        for (auto& n : names) if (!n.empty()) declareVar(n, ArgT{});
        if (ps) { for (auto& p : *ps) declareParam(p); walkParamExprs(*ps); }
        walkBlock(b);
        scopes.pop_back();
    }

    void walkExpr(const Expr* e) {
        if (!e) return;
        switch (e->kind) {
            case NK::Call: {
                auto* c = static_cast<const Call*>(e);
                if (!c->callee && !c->dotAmp && !c->name.empty() && !feedTargets.count(e))
                    judge(c->name, &c->args, lineOf(c));
                // `next(42)` / `last $x` / `redo(1)`: the parser reads the
                // keyword as the control op and the arguments as a call on it
                if (c->callee && c->callee->kind == NK::Unary) {
                    auto* u = static_cast<const Unary*>(c->callee.get());
                    if (!u->operand && (u->op == "next" || u->op == "last" || u->op == "redo"))
                        judgeLoopControl(u->op, &c->args, lineOf(c));
                }
                walkExpr(c->callee.get());
                for (auto& a : c->args) walkExpr(a.get());
                return;
            }
            case NK::NameTerm: {
                // a bare routine name is a call with no arguments
                auto* n = static_cast<const NameTerm*>(e);
                if (!n->defConstraint && n->ofType.empty() && !n->pkgSelf && !n->symbolicStrict && !feedTargets.count(e))
                    judge(n->name, nullptr, lineOf(n));
                return;
            }
            case NK::VarExpr: {
                auto* v = static_cast<const VarExpr*>(e);
                walkExpr(v->d().declDefault.get()); walkExpr(v->d().declShape.get()); walkExpr(v->d().declTypeExpr.get());
                return;
            }
            case NK::MethodCall: {
                auto* m = static_cast<const MethodCall*>(e);
                walkExpr(m->inv.get()); walkExpr(m->methodExpr.get());
                for (auto& a : m->args) walkExpr(a.get());
                return;
            }
            case NK::Unary: walkExpr(static_cast<const Unary*>(e)->operand.get()); return;
            case NK::Assign: {
                auto* a = static_cast<const Assign*>(e);
                if (a->target && a->target->kind == NK::VarExpr) earlyInitializer(a);
                walkExpr(a->target.get()); walkExpr(a->value.get()); return;
            }
            case NK::Binary: {
                auto* b = static_cast<const Binary*>(e);
                // a feed hands its list to the call it points at, as that call's
                // last argument: `(4, 5) ==> total()` passes two things, not none
                if (b->op == "==>" || b->op == "==>>") feedTargets.insert(b->rhs.get());
                else if (b->op == "<==" || b->op == "<<==") feedTargets.insert(b->lhs.get());
                walkExpr(b->lhs.get()); walkExpr(b->rhs.get()); return;
            }
            case NK::Index: {
                auto* i = static_cast<const Index*>(e);
                walkExpr(i->base.get()); walkExpr(i->index.get()); return;
            }
            case NK::Ternary: {
                auto* t = static_cast<const Ternary*>(e);
                walkExpr(t->cond.get()); walkExpr(t->then.get()); walkExpr(t->els.get()); return;
            }
            case NK::Range: {
                auto* r = static_cast<const RangeExpr*>(e);
                walkExpr(r->from.get()); walkExpr(r->to.get()); return;
            }
            case NK::Pair: {
                auto* p = static_cast<const PairExpr*>(e);
                walkExpr(p->keyExpr.get()); walkExpr(p->value.get()); return;
            }
            case NK::BlockExpr: {
                auto* be = static_cast<const BlockExpr*>(e);
                walkBody(be->params, be->body);
                return;
            }
            case NK::ListExpr:  for (auto& x : static_cast<const ListExpr*>(e)->items) walkExpr(x.get()); return;
            case NK::ArrayLit:  for (auto& x : static_cast<const ArrayLit*>(e)->items) walkExpr(x.get()); return;
            case NK::HashLit:   for (auto& x : static_cast<const HashLit*>(e)->items) walkExpr(x.get()); return;
            case NK::InterpStr: for (auto& x : static_cast<const InterpStr*>(e)->parts) walkExpr(x.get()); return;
            case NK::ChainExpr: for (auto& x : static_cast<const ChainExpr*>(e)->operands) walkExpr(x.get()); return;
            case NK::NqpOp:     for (auto& x : static_cast<const NqpOp*>(e)->args) walkExpr(x.get()); return;
            default: return;
        }
    }

    void walkStmt(const Stmt* s) {
        if (!s) return;
        switch (s->kind) {
            case NK::ExprStmt: walkExpr(static_cast<const ExprStmt*>(s)->e.get()); return;
            case NK::VarDecl:  walkExpr(static_cast<const VarDecl*>(s)->init.get()); return;
            case NK::SubDecl: {
                auto* sd = static_cast<const SubDecl*>(s);
                for (auto& a : sd->immediateArgs) walkExpr(a.get());
                walkBody(sd->params, sd->body, &sd->altParams);
                return;
            }
            case NK::Block:
                if (static_cast<const Block*>(s)->phaser == "BEGIN") earlyBegin(static_cast<const Block*>(s));
                walkBlock(static_cast<const Block*>(s));
                return;
            case NK::IfStmt: {
                auto* f = static_cast<const IfStmt*>(s);
                for (size_t i = 0; i < f->branches.size(); i++) {
                    walkExpr(f->branches[i].first.get());
                    std::vector<std::string> names{f->thenVar};
                    if (i < f->branchVars.size()) names.push_back(f->branchVars[i]);
                    walkBound(f->branches[i].second.get(),
                              i < f->branchParams.size() ? &f->branchParams[i] : nullptr, names);
                }
                walkBound(f->elseBlock.get(), &f->elseParams, {f->elseVar});
                return;
            }
            case NK::WhileStmt: {
                auto* w = static_cast<const WhileStmt*>(s);
                walkExpr(w->cond.get());
                walkBound(w->body.get(), &w->params, {w->var});
                return;
            }
            case NK::RepeatStmt: {
                auto* r = static_cast<const RepeatStmt*>(s);
                scopes.emplace_back();
                if (r->body) walkStmts(r->body->stmts);
                walkExpr(r->cond.get());
                scopes.pop_back();
                return;
            }
            case NK::ForStmt: {
                auto* f = static_cast<const ForStmt*>(s);
                walkExpr(f->list.get());
                walkBound(f->body.get(), &f->params, f->vars);
                return;
            }
            case NK::LoopStmt: {
                auto* l = static_cast<const LoopStmt*>(s);
                walkExpr(l->init.get()); walkExpr(l->cond.get()); walkExpr(l->incr.get());
                walkBlock(l->body.get());
                return;
            }
            case NK::GivenStmt: {
                auto* g = static_cast<const GivenStmt*>(s);
                walkExpr(g->topic.get());
                walkBound(g->body.get(), &g->params, {g->var});
                walkBound(g->elseBody.get(), &g->elseParams, {g->elseVar});
                return;
            }
            case NK::WhenStmt: {
                auto* w = static_cast<const WhenStmt*>(s);
                walkExpr(w->cond.get());
                walkBlock(w->body.get());
                return;
            }
            case NK::ReturnStmt: walkExpr(static_cast<const ReturnStmt*>(s)->value.get()); return;
            case NK::ClassDecl: {
                auto* c = static_cast<const ClassDecl*>(s);
                scopes.emplace_back();
                // `$!x` reads an attribute: typed as it was declared
                for (auto& a : c->attrs) {
                    ArgT t;
                    if (!a.coerce && !a.whereExpr && a.containerIs.empty() && !a.shape && !a.objKeyed &&
                        a.doesRoles.empty() && !a.defConstraint)
                        containerType(a.sigil, a.type, /*exact=*/true, t);
                    declareVar(std::string(1, a.sigil) + "!" + a.name, t);
                    if (!a.twigilWritten) declareVar(std::string(1, a.sigil) + a.name, ArgT{});
                }
                for (auto& a : c->attrs) { walkExpr(a.def.get()); walkExpr(a.whereExpr.get()); }
                walkStmts(c->body);
                for (auto& m : c->methods) {
                    if (!m) continue;
                    if (m->line) curLine = m->line;
                    walkBody(m->params, m->body, &m->altParams);
                }
                scopes.pop_back();
                return;
            }
            default: return;
        }
    }

    // ---- a call made while the unit compiles (issue #196) --------------------
    //
    // A constant's initializer and a BEGIN block run as soon as they are
    // compiled, and Rakudo looks every routine they name up then, so one the
    // unit declares only further down is "Undeclared routine": `constant A =
    // f(); sub f { 1 }`. What the probes against Rakudo 2026.09 showed:
    //   · in the initializer, every name outside a nested block is looked up,
    //     run or not: `1 || f()`, `0 ?? f() !! 1`, `try f()`; a block's are
    //     not, `[1].map({ f() })` builds and runs later;
    //   · a routine declared above that the code calls is compiled then, and
    //     the names in its body are looked up too, nested blocks and `try`
    //     aside, even past a `return`;
    //   · a BEGIN block's body is such a body;
    //   · an outer routine declared above answers before an inner one declared
    //     below, and so does a builtin.
    // Only what is certain is reported: a called routine is followed only from
    // a place that surely runs, and a `do` block's body only where it surely
    // runs. "Above" is source order (stmtSeq), so a one-line program counts.

    std::unordered_map<const Node*, int> seq_;   // pre-order position, built on first need
    const std::vector<StmtPtr>* unitStmts = nullptr;
    std::set<std::string> earlyReported;

    int seqOf(const Node* n) {
        if (seq_.empty() && unitStmts) {
            int k = 0;
            Visitor v;
            v.onStmt = [&](const Stmt* s) { seq_.emplace(s, k++); };
            v.onExpr = [&](const Expr* e) { seq_.emplace(e, k++); };
            v.stmts(*unitStmts);
        }
        auto it = seq_.find(n);
        return it == seq_.end() ? -1 : it->second;
    }

    struct Early {
        int at = -1;               // the position the code runs at
        int from = 0;              // the innermost scope its names are looked up in
        std::set<std::string> locals;   // names the routine being followed declares itself
        bool lookAll = true;       // every name is looked up, or only what surely runs
        bool initializer = false;  // the initializer itself, not a body it runs
        int line = 0;              // the statement being read, for a node that has no line
        int depth = 0;
        std::set<const SubDecl*>* seen = nullptr;
    };
    enum { E_UNKNOWN, E_ABOVE, E_BELOW };

    // Where `name` stands relative to `x.at`: declared above it in the nearest
    // scope that has it (with that sub and its scope when it is the only one),
    // only below it in every scope that has it, or nothing this pass can say.
    int earlyResolve(const std::string& name, const Early& x, const SubDecl** only, int* where) {
        if (x.locals.count(name)) return E_UNKNOWN;
        bool below = false;
        for (int i = x.from; i >= 0; i--) {
            const Scope& sc = scopes[i];
            if (sc.terms.count(name) || sc.vars.count("&" + name)) return below ? E_BELOW : E_UNKNOWN;
            auto r = sc.subs.find(name);
            if (r == sc.subs.end()) continue;
            const Routines& rs = r->second;
            std::vector<const SubDecl*> all(rs.multis.begin(), rs.multis.end());
            if (rs.only) all.push_back(rs.only);
            if (rs.proto) all.push_back(rs.proto);
            int above = 0;
            for (auto* sd : all) {
                const int s = seqOf(sd);
                if (s < 0) return E_UNKNOWN;
                if (s < x.at) above++;
            }
            if (above) {
                if (rs.only && all.size() == 1) { *only = rs.only; *where = i; }
                return E_ABOVE;
            }
            below = true;   // (an outer one declared above would answer)
        }
        return below ? E_BELOW : E_UNKNOWN;
    }

    // `name`, written at `line`, is looked up by code running at `x.at`; a
    // routine declared above is followed when the place surely runs
    void earlyName(const std::string& name, int line, bool sure, Early& x) {
        if (name.empty() || name.find(':') != std::string::npos) return;
        const SubDecl* only = nullptr;
        int where = 0;
        const int r = earlyResolve(name, x, &only, &where);
        if (r == E_BELOW) {
            if ((*isCore)(name) || !earlyReported.insert(name).second) return;   // a builtin answers before
            DoomedCall d;
            d.undeclared = true;
            d.name = name;
            d.line = line ? line : x.line ? x.line : curLine;
            out.push_back(std::move(d));
        }
        else if (r == E_ABOVE && only && sure) earlyBody(only->params, only->body, where, x, only);
    }

    // a routine or block body run now: the names its statements hold, outside
    // the blocks nested in them, are looked up as it compiles
    void earlyBody(const std::vector<Param>& ps, const std::vector<StmtPtr>& body, int from, Early& outer,
                   const SubDecl* sd = nullptr, bool lookAll = true) {
        if (outer.depth > 16 || (sd && !outer.seen->insert(sd).second)) return;
        Early x;
        x.at = outer.at; x.from = from; x.lookAll = lookAll; x.depth = outer.depth + 1; x.seen = outer.seen;
        x.locals = outer.locals;
        x.line = outer.line;
        // what the body declares itself is its own, wherever it is declared
        auto local = [&](std::string n) {
            if (n.empty()) return;
            if (n[0] == '&' || n[0] == '\\') n = n.substr(1);
            else if (std::strchr("$@%", n[0])) return;
            x.locals.insert(n);
        };
        for (auto& p : ps) local(p.name);
        Visitor v;
        v.onStmt = [&](const Stmt* s) { if (s->kind == NK::SubDecl) local(static_cast<const SubDecl*>(s)->name); };
        v.onExpr = [&](const Expr* e) {
            if (e->kind == NK::VarExpr && static_cast<const VarExpr*>(e)->declare) local(static_cast<const VarExpr*>(e)->name);
        };
        v.stmts(body);
        bool sure = true;
        for (auto& st : body) {
            const Stmt* s = st.get();
            if (!s) continue;
            if (s->line) x.line = s->line;
            if (s->kind == NK::ExprStmt) {
                const Expr* e = static_cast<const ExprStmt*>(s)->e.get();
                earlyExpr(e, sure, x);
                // (what follows a `die`, a `fail` or an `exit` may not run)
                if (e && e->kind == NK::Call) {
                    const std::string& n = static_cast<const Call*>(e)->name;
                    if (n == "die" || n == "fail" || n == "exit" || n == "return" || n == "return-rw") sure = false;
                }
            }
            else if (s->kind == NK::VarDecl) earlyExpr(static_cast<const VarDecl*>(s)->init.get(), sure, x);
            else if (s->kind == NK::ReturnStmt) { earlyExpr(static_cast<const ReturnStmt*>(s)->value.get(), sure, x); sure = false; }
            else if (s->kind != NK::SubDecl && s->kind != NK::UseStmt) sure = false;   // a branch, a loop, a block
        }
    }

    void earlyExpr(const Expr* e, bool sure, Early& x) {
        if (!e) return;
        auto look = [&](const std::string& name, int line) { if (x.lookAll || sure) earlyName(name, line, sure, x); };
        switch (e->kind) {
            case NK::Call: {
                auto* c = static_cast<const Call*>(e);
                if (!c->callee && !c->name.empty()) look(c->name, c->line);
                // `(sub { … })()` runs the block now
                if (c->callee && c->callee->kind == NK::BlockExpr) {
                    auto* be = static_cast<const BlockExpr*>(c->callee.get());
                    if (sure) earlyBody(be->params, be->body, x.from, x, nullptr, x.lookAll);
                }
                else earlyExpr(c->callee.get(), sure, x);
                const bool later = c->name == "lazy" || c->name == "start";
                for (auto& a : c->args) earlyExpr(a.get(), sure && !later, x);
                return;
            }
            case NK::NameTerm: {
                auto* n = static_cast<const NameTerm*>(e);
                if (!n->defConstraint && n->ofType.empty() && !n->pkgSelf && !n->symbolicStrict) look(n->name, n->line);
                return;
            }
            case NK::VarExpr: {
                // `&f`, `.map(&f)`, `.&f`: a reference is looked up too
                auto* v = static_cast<const VarExpr*>(e);
                if (!v->declare && v->name.size() > 1 && v->name[0] == '&' && !std::strchr("*?!.^:=~<", v->name[1]))
                    if (x.lookAll || sure) earlyName(v->name.substr(1), v->line, /*sure=*/false, x);
                return;
            }
            case NK::MethodCall: {
                auto* m = static_cast<const MethodCall*>(e);
                if (m->method.size() > 1 && m->method[0] == '&' && (x.lookAll || sure))
                    earlyName(m->method.substr(1), m->line, false, x);
                earlyExpr(m->inv.get(), sure, x); earlyExpr(m->methodExpr.get(), sure, x);
                for (auto& a : m->args) earlyExpr(a.get(), sure, x);
                return;
            }
            case NK::Unary: {
                auto* u = static_cast<const Unary*>(e);
                if (u->op == "do" && u->operand && u->operand->kind == NK::BlockExpr) {
                    auto* be = static_cast<const BlockExpr*>(u->operand.get());
                    if (sure) earlyBody(be->params, be->body, x.from, x, nullptr, /*lookAll=*/false);
                    return;
                }
                // a routine's `try` compiles its operand when it runs; the
                // initializer's own is looked up with the rest of it
                if (u->op == "try" && !x.initializer) return;
                const bool later = u->op == "try" || u->op == "lazy" || u->op == "gather" || u->op == "start";
                earlyExpr(u->operand.get(), sure && !later, x);
                return;
            }
            case NK::Binary: {
                auto* b = static_cast<const Binary*>(e);
                static const std::set<std::string> kThunky = {
                    "&&", "||", "//", "^^", "and", "or", "xor", "andthen", "orelse", "notandthen",
                    "ff", "^ff", "ff^", "^ff^", "fff", "^fff", "fff^", "^fff^"};
                const bool thunk = kThunky.count(b->op) > 0;
                earlyExpr(b->lhs.get(), sure && b->op != "xx", x);
                earlyExpr(b->rhs.get(), sure && !thunk, x);
                return;
            }
            case NK::Ternary: {
                auto* t = static_cast<const Ternary*>(e);
                earlyExpr(t->cond.get(), sure, x);
                earlyExpr(t->then.get(), false, x); earlyExpr(t->els.get(), false, x);
                return;
            }
            case NK::ChainExpr: {
                auto& ops = static_cast<const ChainExpr*>(e)->operands;
                for (size_t i = 0; i < ops.size(); i++) earlyExpr(ops[i].get(), sure && i < 2, x);
                return;
            }
            case NK::Assign: {
                auto* a = static_cast<const Assign*>(e);
                earlyExpr(a->target.get(), sure, x); earlyExpr(a->value.get(), sure, x); return;
            }
            case NK::Index: {
                auto* i = static_cast<const Index*>(e);
                earlyExpr(i->base.get(), sure, x); earlyExpr(i->index.get(), sure, x); return;
            }
            case NK::Range: {
                auto* r = static_cast<const RangeExpr*>(e);
                earlyExpr(r->from.get(), sure, x); earlyExpr(r->to.get(), sure, x); return;
            }
            case NK::Pair: {
                auto* p = static_cast<const PairExpr*>(e);
                earlyExpr(p->keyExpr.get(), sure, x); earlyExpr(p->value.get(), sure, x); return;
            }
            case NK::AllomorphLit: earlyExpr(static_cast<const AllomorphLit*>(e)->num.get(), sure, x); return;
            case NK::ListExpr:  for (auto& i : static_cast<const ListExpr*>(e)->items) earlyExpr(i.get(), sure, x); return;
            case NK::ArrayLit:  for (auto& i : static_cast<const ArrayLit*>(e)->items) earlyExpr(i.get(), sure, x); return;
            case NK::HashLit:   for (auto& i : static_cast<const HashLit*>(e)->items) earlyExpr(i.get(), sure, x); return;
            case NK::InterpStr: for (auto& i : static_cast<const InterpStr*>(e)->parts) earlyExpr(i.get(), sure, x); return;
            default: return;   // a block's body runs later, if at all
        }
    }

    // the two places compiled code runs at once
    void earlyInitializer(const Assign* a) {
        auto* v = static_cast<const VarExpr*>(a->target.get());
        if (!v->declare || v->d().declScope != "constant") return;
        std::set<const SubDecl*> seen;
        Early x;
        x.at = seqOf(a); x.from = (int)scopes.size() - 1; x.seen = &seen; x.initializer = true; x.line = a->line;
        if (x.at >= 0) earlyExpr(a->value.get(), true, x);
    }
    void earlyBegin(const Block* b) {
        std::set<const SubDecl*> seen;
        Early x;
        x.at = seqOf(b); x.from = (int)scopes.size() - 1; x.seen = &seen; x.line = b->line;
        if (x.at >= 0) earlyBody({}, b->stmts, x.from, x);
    }

    // ---- judging one call ------------------------------------------------------

    struct Target {
        const SubDecl* only = nullptr;
        const SubDecl* proto = nullptr;
        std::vector<const SubDecl*> cands;
    };

    // The routine a call names, looked up lexically from where the call stands:
    // the nearest sub of that name, or the multi candidates of every scope out
    // to the proto (or to the unit) — Rakudo derives an inner dispatcher from
    // the outer one, so an inner candidate joins the outer ones.
    //
    // A routine declared at or below the call that shadows one further out is
    // left alone: Rakudo judges such a call against the OUTER routine — the one
    // in scope where the call is written — and then runs the inner one, so
    // neither verdict this pass could give would be Rakudo's.
    bool resolve(const std::string& name, int line, Target& t) {
        std::vector<const std::vector<const SubDecl*>*> layers;   // innermost first
        auto declaredAbove = [&](const Routines& rs) {
            auto above = [&](const SubDecl* sd) { return !sd || (sd->line > 0 && sd->line < line); };
            if (!above(rs.only) || !above(rs.proto)) return false;
            for (auto* m : rs.multis) if (!above(m)) return false;
            return true;
        };
        auto shadowsOuter = [&](std::vector<Scope>::reverse_iterator from) {
            for (auto it = std::next(from); it != scopes.rend(); ++it)
                if (it->subs.count(name) || it->terms.count(name)) return true;
            return false;
        };
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            if (it->terms.count(name)) return false;
            auto r = it->subs.find(name);
            if (r == it->subs.end()) continue;
            const Routines& rs = r->second;
            if (rs.onlys > 1 || rs.protos > 1) return false;
            if (!declaredAbove(rs) && shadowsOuter(it)) return false;
            if (rs.only) {
                if (!layers.empty() || !rs.multis.empty() || rs.proto) return false;
                t.only = rs.only;
                return true;
            }
            layers.push_back(&rs.multis);
            if (rs.proto) { t.proto = rs.proto; break; }
        }
        for (auto it = layers.rbegin(); it != layers.rend(); ++it)
            t.cands.insert(t.cands.end(), (*it)->begin(), (*it)->end());
        return !t.cands.empty();
    }

    // What Rakudo's optimizer knows an argument to be, or false when it knows
    // nothing (and a call with such an argument is never judged).
    bool argType(const Expr* e, ArgT& a) {
        switch (e->kind) {
            case NK::IntLit:
                a.type = a.shown = "Int";
                if (static_cast<const IntLit*>(e)->big.empty()) a.lit = 'i';
                return true;
            case NK::NumLit: {
                auto* n = static_cast<const NumLit*>(e);
                if (n->imaginary) return false;   // `1i` is a call to a postfix
                if (n->isRat) { a.type = a.shown = "Rat"; return true; }
                a.type = a.shown = "Num"; a.lit = 'n';
                return true;
            }
            case NK::StrLit:
                if (static_cast<const StrLit*>(e)->wordQuote) return false;   // `<x>` is no constant to it
                a.type = a.shown = "Str"; a.lit = 's';
                return true;
            case NK::InterpStr: {
                a.type = a.shown = "Str";
                bool constant = true;
                for (auto& p : static_cast<const InterpStr*>(e)->parts)
                    if (!p || p->kind != NK::StrLit) constant = false;
                if (constant) a.lit = 's';
                return true;
            }
            case NK::NameTerm: {
                auto* n = static_cast<const NameTerm*>(e);
                if (n->defConstraint || !n->ofType.empty() || n->pkgSelf) return false;
                if (n->name == "Inf" || n->name == "NaN") { a.type = a.shown = "Num"; a.lit = 'n'; return true; }
                // a type object, unless a routine or term here goes by its name
                for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
                    if (it->terms.count(n->name) || it->subs.count(n->name)) return false;
                auto ut = T.unit.find(n->name);
                if (ut != T.unit.end() ? ut->second.kind == 's' : n->name == "UInt") return false;
                if (!T.supers(n->name)) return false;
                a.type = a.shown = n->name;
                return true;
            }
            case NK::VarExpr: {
                auto* v = static_cast<const VarExpr*>(e);
                if (v->declare || v->viaPseudoPkg || v->pkgSymbol || v->processScoped || v->heredocOuter) return false;
                // `$*x`, `$?x`, `$.x`…: only an attribute's `$!x` is read as declared
                if (v->name.size() > 2 && std::strchr("*?.^:=~", v->name[1])) return false;
                for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
                    auto f = it->vars.find(v->name);
                    if (f == it->vars.end()) continue;
                    if (f->second.type.empty()) return false;
                    a = f->second;
                    return true;
                }
                return false;
            }
            default: return false;
        }
    }

    // Rakudo's trial binder: does `args` bind to `ps`, as far as types and
    // counts can tell? A parameter it does not analyse makes it unsure.
    int trialBind(const std::vector<Param>& ps, const std::vector<ArgT>& args) {
        std::vector<const Param*> sig;
        for (auto& p : ps) if (!p.invocant) sig.push_back(&p);
        if (sig.size() == 1 && isCapture(*sig[0]) && !hasWhere(*sig[0])) return BINDS;
        size_t cur = 0;
        for (const Param* pp : sig) {
            const Param& p = *pp;
            if (p.slurpy && p.sigil == '%' && !hasWhere(p)) continue;   // `*%h`
            if (p.slurpy || p.sigil == '&' || p.sigil == '|' || p.isRw || p.defConstraint || p.typeCapture ||
                p.typeFromCapture || p.typeMayBeUndeclared || p.coerce || hasDefault(p) || p.subSig ||
                p.codeSig || p.litVal || !p.shapeDims.empty() || !p.userTraits.empty())
                return NOT_SURE;
            // Rakudo's binder walks positional ARGUMENTS against every
            // parameter in turn, named ones included: `(:$n!)` with nothing to
            // take is a missing argument, and `(:$x)` takes `f(1)`'s 1.
            if (cur >= args.size()) {
                if (p.named ? p.required : !p.optional) return NO_WAY;
                cur++;
                continue;
            }
            const ArgT& a = args[cur++];
            const char want = (p.sigil == '$' || p.sigil == '\\') ? nativeKind(p.type) : 0;
            if (want) {
                if (!a.prim) return NOT_SURE;
                if (want == 'u' ? a.prim != 'u' && a.prim != 'i' : a.prim != want) return NO_WAY;
                continue;
            }
            bool constrained = false;
            const std::string pt = paramType(p, constrained);
            if (pt.empty()) return NOT_SURE;
            if (pt == "Mu") continue;
            const std::string at = a.prim ? boxedName(a.prim) : a.type;
            const int r = T.isa(at, pt);
            if (r < 0) return NOT_SURE;
            // A Junction that does not fit may auto-thread instead. Otherwise an
            // Any may still be an Int at run time; an Int is never a Str.
            if (r == 0) return at != "Junction" && T.isa(pt, at) == 0 ? NO_WAY : NOT_SURE;
        }
        return cur < args.size() ? NO_WAY : BINDS;
    }

    // The type a parameter checks its argument against, Rakudo's way: `@a` is
    // Positional, `Int @a` Positional[Int], `%h` Associative, `&c` Callable.
    std::string paramType(const Param& p, bool& constrained) {
        if (p.type.find("::?") != std::string::npos) return "";
        if (p.sigil == '@' || p.sigil == '%') {
            const std::string base = p.sigil == '@' ? "Positional" : "Associative";
            if (p.type.empty()) return base;
            bool elemConstrained = false;
            std::string elem = nativeKind(p.type) ? p.type : T.nominal(p.typeIsSetting ? "CORE::" + p.type : p.type, elemConstrained);
            if (elem.empty() || elemConstrained) return "";
            return base + "[" + elem + "]";
        }
        if (p.sigil == '&') return p.type.empty() ? "Callable" : "";
        return T.nominal(p.typeIsSetting ? "CORE::" + p.type : p.type, constrained);
    }

    // Rakudo's dispatch_info for one candidate; false for a parameter beyond this pass.
    bool candInfo(const SubDecl& sd, Cand& c) {
        c.sd = &sd;
        if (sd.isNative || !sd.altParams.empty() || !sd.hadSig) return false;
        for (auto& p : sd.params) {
            if (p.invocant) continue;
            if (p.coerce || p.litVal || !p.shapeDims.empty() || !p.userTraits.empty() || p.codeSig ||
                p.typeMayBeUndeclared || isCapture(p) || p.sigil == '|')
                return false;
            if (p.subSig || hasWhere(p)) c.bindCheck = true;
            if (p.slurpy && p.sigil == '%') break;   // `*%h` ends what dispatch reads
            if (p.named) { c.bindCheck = true; continue; }
            if (p.slurpy) { c.maxArity = kSlurpyArity; continue; }
            if (c.maxArity != kSlurpyArity) c.maxArity++;
            if (!p.optional && !hasDefault(p)) c.minArity++;
            bool constrained = p.subSig || hasWhere(p);
            std::string t;
            char nat = 0;
            if (p.typeCapture || p.typeFromCapture) { c.bindCheck = true; constrained = true; t = "Any"; }
            else if ((p.sigil == '$' || p.sigil == '\\') && (nat = nativeKind(p.type))) t = p.type;
            else t = paramType(p, constrained);
            if (t.empty()) return false;
            if (constrained) c.bindCheck = true;
            c.types.push_back(t);
            c.constraints.push_back(constrained);
            c.rw.push_back(p.isRw);
            c.native.push_back(nat);
            c.defcon.push_back((char)p.defConstraint);
            if (!p.pastDoubleSemi) c.numTypes++;
        }
        return true;
    }

    // Rakudo's is_narrower(a, b); -1 when it needs a relation beyond this pass.
    int narrower(const Cand& a, const Cand& b) {
        int check = a.numTypes;
        if (a.numTypes == b.numTypes) {}
        else if (a.minArity == b.minArity) check = std::min(check, b.numTypes);
        else return a.maxArity != kSlurpyArity && b.maxArity == kSlurpyArity;
        int narrow = 0, tied = 0;
        for (int i = 0; i < check; i++) {
            const std::string &ta = a.types[i], &tb = b.types[i];
            if (ta == tb) {
                if (a.constraints[i] && !b.constraints[i]) narrow++;
                else if (a.rw[i] > b.rw[i]) narrow++;
                else if (a.constraints[i] == b.constraints[i]) tied++;
            }
            else if (a.native[i] && b.native[i]) tied++;   // two natives: unrelated
            else if (a.native[i]) narrow++;                 // a native is always narrower
            else if (b.native[i]) {}
            else {
                const int ab = tb == "Mu" ? 1 : T.isa(ta, tb);
                if (ab < 0) return -1;
                if (ab == 1) narrow++;
                else if (ta != "Mu") {
                    const int ba = T.isa(tb, ta);
                    if (ba < 0) return -1;
                    if (ba == 0) tied++;
                }
            }
        }
        if (narrow && narrow + tied == check) return 1;
        if (tied != check) return 0;
        if (a.maxArity != kSlurpyArity && b.maxArity == kSlurpyArity) return 1;
        return !(b.maxArity != kSlurpyArity && a.maxArity == kSlurpyArity) && a.bindCheck && !b.bindCheck;
    }

    // Rakudo's candidate sort: tiers of mutually unordered candidates,
    // narrowest first. False when a relation or the order cannot be settled.
    bool tiers(const std::vector<Cand>& cs, std::vector<std::vector<int>>& order) {
        const int n = (int)cs.size();
        std::vector<std::vector<int>> edges(n);
        std::vector<int> in(n, 0);
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                if (i == j) continue;
                const int r = narrower(cs[i], cs[j]);
                if (r < 0) return false;
                if (r) { edges[i].push_back(j); in[j]++; }
            }
        std::vector<char> done(n, 0);
        for (int left = n; left > 0;) {
            std::vector<int> tier;
            for (int i = 0; i < n; i++) if (!done[i] && in[i] == 0) tier.push_back(i);
            if (tier.empty()) return false;   // "Circularity detected in multi sub types"
            for (int i : tier) { done[i] = 1; left--; }
            for (int i : tier) for (int j : edges[i]) in[j]--;
            order.push_back(std::move(tier));
        }
        return true;
    }

    // Rakudo's compile-time multi dispatch (analyze_dispatch): NO_WAY only once
    // every candidate it read was ruled out — and it reads past the first tier
    // only while that tier is all native, so a wider candidate behind a
    // narrower one leaves the call to run.
    int analyzeDispatch(const std::vector<Cand>& cs, const std::vector<std::vector<int>>& order,
                        const std::vector<ArgT>& args) {
        const int n = (int)args.size();
        bool allNative = true, seenAll = false, arityPossible = false, typePossible = false;
        const Cand* result = nullptr;
        for (size_t t = 0; t < order.size(); t++) {
            for (int ci : order[t]) {
                const Cand& c = cs[ci];
                if (n < c.minArity || n > c.maxArity) continue;
                arityPossible = true;
                const int count = std::min(c.numTypes, n);
                bool usedDefcon = false, impossible = false, mismatch = false;
                for (int i = 0; i < count && !impossible; i++) {
                    const ArgT& a = args[i];
                    if (c.native[i]) {
                        if (!a.prim) { mismatch = true; break; }   // an object: maybe unboxable, maybe not
                        if (a.prim != c.native[i] || (a.lit && c.rw[i])) { mismatch = impossible = true; break; }
                        continue;
                    }
                    allNative = false;
                    if (a.lit && c.rw[i]) { mismatch = impossible = true; break; }   // a literal is no container
                    const std::string& pt = c.types[i];
                    if (pt != "Mu") {
                        const std::string at = a.prim ? boxedName(a.prim) : a.type;
                        const int r = T.isa(at, pt);
                        if (r < 0) return NOT_SURE;
                        if (r == 0) {
                            mismatch = true;
                            const int rev = T.isa(pt, at);
                            if (rev < 0) return NOT_SURE;
                            if (rev == 0) impossible = true;
                            continue;
                        }
                    }
                    if (c.defcon[i]) usedDefcon = true;
                }
                if (!impossible) typePossible = true;
                if (mismatch) continue;
                if (usedDefcon || c.bindCheck || result) return NOT_SURE;
                result = &c;
            }
            const bool more = t + 1 < order.size();
            if (more && allNative && !result) continue;
            seenAll = !more;
            break;
        }
        if (seenAll && !result && (!arityPossible || !typePossible)) {
            // …unless a Junction is among the arguments: it may auto-thread
            for (auto& a : args) if (a.type == "Junction") return NOT_SURE;
            return NO_WAY;
        }
        return result ? BINDS : NOT_SURE;
    }

    // The signature as Rakudo's message shows it: `(Str $x)`, and a subset-typed
    // parameter as its base type with the constraint, `(Int $x where { ... })`.
    std::string gist(const SubDecl& sd) {
        std::string s = "(";
        bool first = true, prevPast = false;
        for (auto& p : sd.params) {
            if (p.invocant) continue;
            if (!first) s += p.pastDoubleSemi && !prevPast ? ";; " : ", ";
            else if (p.pastDoubleSemi) s += ";; ";
            first = false;
            prevPast = p.pastDoubleSemi;
            bool constrained = false;
            if (!p.type.empty() && !p.coerce && (p.sigil == '$' || p.sigil == '\\')) {
                auto ut = T.unit.find(p.type);
                if (p.type == "UInt" || (ut != T.unit.end() && ut->second.kind == 's')) {
                    std::string base = T.nominal(p.typeIsSetting ? "CORE::" + p.type : p.type, constrained);
                    if (!base.empty()) {
                        auto q = signatureParamCopy(p);
                        q->type = base;
                        q->hadWhere = true;
                        s += renderSignatureParam(*q, true);
                        continue;
                    }
                }
            }
            s += renderSignatureParam(p, true);
        }
        if (!sd.retType.empty()) s += " --> " + sd.retType;
        return s + ")";
    }

    // Every combination of the concrete types the arguments can have must be
    // refused too, or the call is not truly doomed.
    bool doomedForEveryValue(const std::vector<ArgT>& args,
                             const std::function<int(const std::vector<ArgT>&)>& verdict) {
        std::vector<size_t> idx;
        size_t combos = 1;
        for (size_t i = 0; i < args.size(); i++)
            if (!args[i].truth.empty()) {
                idx.push_back(i);
                combos *= args[i].truth.size();
                if (combos > 256) return false;
            }
        if (idx.empty()) return true;
        std::vector<ArgT> trial = args;
        for (size_t k = 0; k < combos; k++) {
            size_t rest = k;
            for (size_t i : idx) {
                const auto& tv = args[i].truth;
                trial[i].type = tv[rest % tv.size()];
                rest /= tv.size();
            }
            if (verdict(trial) != NO_WAY) return false;
        }
        return true;
    }

    // `next`, `last` and `redo` are the setting's multis `( --> Nil)` and
    // `(Label:D $x --> Nil)`; from 6.e `next` and `last` also take any value
    // (`(\x --> Nil)`), `redo` never does. An argument of a type the compiler
    // knows, which no Label can be, never binds: Rakudo refuses `next(42)`,
    // `last("x")` or `redo(1)` while compiling, and a program that would have
    // printed half its output first runs nothing. A type a Label is (Mu, Any)
    // may hold one, and is left to the binder, as Rakudo leaves it.
    void judgeLoopControl(const std::string& name, const std::vector<ExprPtr>* argExprs, int line) {
        if (name != "next" && name != "last" && name != "redo") return;
        if (name != "redo" && langRev >= 2) return;
        if (!argExprs || argExprs->size() != 1) return;
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (it->subs.count(name) || it->terms.count(name)) return;
        const Expr* ae = (*argExprs)[0].get();
        if (!ae || ae->kind == NK::Pair) return;
        if (ae->kind == NK::Unary && static_cast<const Unary*>(ae)->op == "|") return;
        ArgT a;
        if (!argType(ae, a)) return;
        auto mayBeLabel = [](const std::string& t) { return t == "Label" || t == "Mu" || t == "Any"; };
        if (mayBeLabel(a.type)) return;
        for (auto& t : a.truth) if (mayBeLabel(t)) return;
        DoomedCall d;
        d.line = line;
        d.name = name;
        d.arguments.push_back(a.shown);
        d.multi = true;
        d.signatures = {"( --> Nil)", "(Label:D $x --> Nil)"};
        out.push_back(std::move(d));
    }

    void judge(const std::string& name, const std::vector<ExprPtr>* argExprs, int line) {
        if (name.empty() || name.find(':') != std::string::npos) return;   // a package path, an operator
        Target tg;
        if (!resolve(name, line, tg)) { judgeLoopControl(name, argExprs, line); return; }
        std::vector<ArgT> args;
        if (argExprs)
            for (auto& ae : *argExprs) {
                if (!ae || ae->kind == NK::Pair) return;   // named — or a Pair, which Rakudo does not type
                if (ae->kind == NK::Unary && static_cast<const Unary*>(ae.get())->op == "|") return;
                ArgT a;
                if (!argType(ae.get(), a)) return;
                args.push_back(std::move(a));
            }
        // A lone literal argument is read the way it would bind to a native
        // parameter: `f("x")` against `(int $x)` can never work.
        if (args.size() == 1 && args[0].lit && !args[0].prim) args[0].prim = args[0].lit;

        DoomedCall d;
        d.line = line;
        d.name = name;
        for (auto& a : args) d.arguments.push_back(a.shown);
        // (the verdict is asked again below, so what it reads lives out here)
        std::function<int(const std::vector<ArgT>&)> verdict;
        std::vector<Cand> cs;
        std::vector<std::vector<int>> order;
        if (tg.only) {
            const SubDecl& sd = *tg.only;
            if (sd.isNative || !sd.altParams.empty()) return;
            if (!sd.hadSig && (!sd.params.empty() || implicitArgs.count(&sd))) return;
            verdict = [this, ps = &sd.params](const std::vector<ArgT>& as) { return trialBind(*ps, as); };
            if (verdict(args) != NO_WAY) return;
            d.signatures.push_back(gist(sd));
        }
        else {
            // An explicit proto must be only `{*}`: Rakudo reads a proto with a
            // body of its own differently, and this pass does not follow it there.
            if (tg.proto) {
                const SubDecl& pr = *tg.proto;
                const bool onlyStar = pr.body.size() == 1 && pr.body[0] && pr.body[0]->kind == NK::ExprStmt &&
                    static_cast<const ExprStmt*>(pr.body[0].get())->e &&
                    static_cast<const ExprStmt*>(pr.body[0].get())->e->kind == NK::Whatever;
                if (!onlyStar || !pr.hadSig || pr.isNative || !pr.altParams.empty()) return;
            }
            cs.resize(tg.cands.size());
            for (size_t i = 0; i < cs.size(); i++)
                if (!candInfo(*tg.cands[i], cs[i])) return;
            if (!tiers(cs, order)) return;
            const SubDecl* proto = tg.proto;
            bool protoguilt = false;
            verdict = [&, proto](const std::vector<ArgT>& as) -> int {
                if (proto && trialBind(proto->params, as) == NO_WAY) return NO_WAY;
                return analyzeDispatch(cs, order, as);
            };
            if (proto && trialBind(proto->params, args) == NO_WAY) protoguilt = true;
            else if (analyzeDispatch(cs, order, args) != NO_WAY) return;
            // With no proto in the unit, a name the setting also has may be
            // meeting candidates from there.
            if (!proto && isCore && (*isCore)(name)) return;
            d.multi = true;
            d.protoguilt = protoguilt;
            if (protoguilt) d.signatures.push_back(gist(*proto));
            else for (auto* c : tg.cands) d.signatures.push_back(gist(*c));
        }
        if (!doomedForEveryValue(args, verdict)) return;
        out.push_back(std::move(d));
    }
};

} // namespace

std::string DoomedCall::message() const {
    if (undeclared) return "Undeclared routine:\n    " + name + " used at line " + std::to_string(line);
    std::string args;
    for (auto& a : arguments) args += (args.empty() ? "" : ", ") + a;
    std::string m = "Calling " + name + "(" + args + ") will never work with ";
    if (multi && !protoguilt) {
        m += "any of these multi signatures:";
        for (auto& s : signatures) m += "\n    " + s;
    }
    else m += std::string(protoguilt ? "signature of the proto " : "declared signature ") +
              (signatures.empty() ? std::string("()") : signatures[0]);
    return m;
}

std::vector<DoomedCall> findDoomedCalls(const Program& prog, const std::vector<std::string>& searchPath,
                                        const std::function<bool(const std::string&)>& isCoreRoutine) {
    Checker C;
    C.isCore = &isCoreRoutine;
    C.unitStmts = &prog.stmts;
    C.langRev = prog.langRev;
    C.survey(prog.stmts);
    if (C.standDown) return {};
    C.scopes.emplace_back();   // the unit
    C.walkStmts(prog.stmts);
    C.scopes.pop_back();
    if (C.out.empty()) return {};
    // A name an imported module could supply might be meeting candidates — or
    // a shadowing routine — that this pass never saw.
    std::vector<std::string> sp(C.extraLibs.begin(), C.extraLibs.end());
    sp.insert(sp.end(), searchPath.begin(), searchPath.end());
    std::vector<DoomedCall> kept;
    for (auto& d : C.out) {
        if (C.usesTest && testExports().count(d.name)) continue;
        if (!C.imports.empty() && importsMaySupplyName(C.imports, sp, prog.langRev >= 2, d.name)) continue;
        kept.push_back(std::move(d));
    }
    std::stable_sort(kept.begin(), kept.end(),
                     [](const DoomedCall& a, const DoomedCall& b) { return a.line < b.line; });
    return kept;
}

int reportDoomedCalls(const std::vector<DoomedCall>& findings, const std::string& fileName,
                      const std::string& src) {
    auto sourceLine = [&](int line) -> std::string {
        if (src.empty() || line <= 0) return "";
        size_t pos = 0;
        for (int i = 1; i < line; i++) {
            pos = src.find('\n', pos);
            if (pos == std::string::npos) return "";
            pos++;
        }
        size_t end = src.find('\n', pos);
        std::string s = src.substr(pos, end == std::string::npos ? end : end - pos);
        while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
        size_t lead = s.find_first_not_of(" \t");
        return lead == std::string::npos ? "" : s.substr(lead);
    };
    auto identChar = [](char c) {
        return ascii::isalnum((unsigned char)c) != 0 || c == '_' || c == '-' || c == '\'';
    };
    // Where the call starts: the routine's name as a whole word, past any
    // occurrence that declares it (`sub f(Str $x) { }; f(42)`) or that is a
    // method, a sigilled name or a longer word.
    auto callAt = [&](const std::string& ln, const std::string& name) -> size_t {
        for (size_t p = ln.find(name); p != std::string::npos; p = ln.find(name, p + 1)) {
            size_t e = p + name.size();
            if (e < ln.size() && identChar(ln[e])) continue;
            if (p && (identChar(ln[p - 1]) || std::strchr(".&$@%!^:", ln[p - 1]))) continue;
            size_t b = p;
            while (b && ln[b - 1] == ' ') b--;
            size_t w = b;
            while (w && identChar(ln[w - 1])) w--;
            const std::string before = ln.substr(w, b - w);
            if (before == "sub" || before == "multi" || before == "proto" || before == "only" ||
                before == "method" || before == "submethod") continue;
            return p;
        }
        return std::string::npos;
    };
    std::cerr << "===SORRY!===";
    if (findings.size() == 1) std::cerr << " Error while compiling " << fileName;
    std::cerr << "\n";
    for (auto& f : findings) {
        std::cerr << f.message() << "\n" << "at " << fileName << ":" << f.line << "\n";
        std::string ln = sourceLine(f.line);
        if (ln.empty()) continue;
        size_t at = callAt(ln, f.name);
        if (at != std::string::npos)
            std::cerr << "------> " << ln.substr(0, at) << "⏏" << ln.substr(at) << "\n";
        else
            std::cerr << "------> " << ln << "\n";
    }
    return 1;
}

bool callCheckEnabled() {
    const char* off = std::getenv("RAKUPP_NO_CALLCHECK");
    return !(off && *off && std::string(off) != "0");
}

} // namespace rakupp
