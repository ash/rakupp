// `rakupp --doc-links` — see DocLinks.h for what it is and how it decides.
#include "DocLinks.h"
#include "Ast.h"
#include "Highlight.h"
#include "Lexer.h"
#include "Parser.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <unordered_map>

namespace rakupp {

// Builtins.cpp: the built-in type lattice, narrowest first (Array, List, Cool,
// Positional, Iterable, Any, Mu). A method documented on List is the one an
// Array runs, so a lookup walks it. Declared here rather than by including
// BuiltinsShared.h, which brings the whole value model with it.
const std::vector<std::string>& typeAncestry(const std::string& t);

namespace doclinks {
namespace {

const char kSite[] = "https://docs.raku.org";

// ---------------------------------------------------------------------------
// The index, keyed per kind. Built once; kIndex is sorted, so the first row
// for a key is the same row on every build.

struct IndexMaps {
    std::unordered_map<std::string, std::vector<const IndexRow*>> by[128];
    IndexMaps() {
        for (size_t i = 0; i < kIndexSize; i++)
            by[(unsigned char)kIndex[i].kind & 127][kIndex[i].key].push_back(&kIndex[i]);
    }
};
const IndexMaps& ix() { static const IndexMaps maps; return maps; }

const IndexRow* find(char kind, const std::string& key) {
    const auto& m = ix().by[(unsigned char)kind];
    auto it = m.find(key);
    return it == m.end() ? nullptr : it->second.front();
}
const std::vector<const IndexRow*>* findAll(char kind, const std::string& key) {
    const auto& m = ix().by[(unsigned char)kind];
    auto it = m.find(key);
    return it == m.end() ? nullptr : &it->second;
}

// A site-relative URL as an absolute one that is safe in an href and in an
// OSC 8 link: the site writes anchors as their heading spells them (`#infix_~`,
// `#infix_!=,_infix_≠`), so anything outside the URL alphabet is escaped, a
// `%` that does not start an escape included. Browsers match the decoded
// fragment against the page's ids.
std::string absUrl(const std::string& rel) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out = kSite;
    bool inFragment = false;
    for (size_t i = 0; i < rel.size(); i++) {
        unsigned char c = (unsigned char)rel[i];
        bool keep = ascii::isalnum(c) || std::strchr("-._~!$&'()*+,;=:@/?", c) != nullptr;
        if (c == '#' && !inFragment) { inFragment = true; keep = true; }
        if (c == '%' && i + 2 < rel.size() &&
            ascii::isxdigit((unsigned char)rel[i + 1]) && ascii::isxdigit((unsigned char)rel[i + 2]))
            keep = true;
        if (keep) out += (char)c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

// ---------------------------------------------------------------------------
// What the AST says, per line. The token pass asks it the questions a word
// cannot answer alone.

struct MethodFact { std::string name, type; bool used = false; };

struct Facts {
    std::map<int, std::vector<MethodFact>> methods;
    std::map<int, std::set<std::string>> calls, infix, prefix, postfix, terms;
    std::map<int, std::set<std::string>> names;       // bare words used as terms: `pi`, `done`, `now`
    std::map<int, std::vector<bool>> listAssign;      // per `=`, in order: a list assignment?
    // the program's own declarations: name -> declaring line
    std::map<std::string, int> subs, types;
    std::map<std::string, std::string> typeKind;      // class / role / grammar / enum / subset
    std::map<std::string, std::vector<std::string>> parents; // `is` and `does`, in order
    std::map<std::string, std::map<std::string, int>> members; // type -> method or accessor -> line
    std::map<std::string, int> anyMember;             // a method name declared on any type
    std::map<std::string, std::string> vars;          // variable -> static type ("" = ambiguous)
    std::set<std::string> fixedVars;                  // …declared with that type, not inferred
    std::set<std::string> modules;                    // `use`d names with a page (Test …)
};

// What a method returns whatever its invocant — the names whose every
// implementation agrees. Used to carry a type along a chain (`$s.uc.chars`).
const char* returnsAlways(const std::string& m) {
    static const std::map<std::string, const char*> R = {
        {"Str", "Str"}, {"uc", "Str"}, {"lc", "Str"}, {"tc", "Str"}, {"fc", "Str"}, {"tclc", "Str"},
        {"wordcase", "Str"}, {"flip", "Str"}, {"trim", "Str"}, {"trim-leading", "Str"},
        {"trim-trailing", "Str"}, {"chomp", "Str"}, {"chop", "Str"}, {"gist", "Str"}, {"raku", "Str"},
        {"fmt", "Str"}, {"join", "Str"}, {"substr", "Str"}, {"subst", "Str"}, {"chr", "Str"},
        {"chars", "Int"}, {"codes", "Int"}, {"elems", "Int"}, {"Int", "Int"}, {"ord", "Int"},
        {"floor", "Int"}, {"ceiling", "Int"}, {"truncate", "Int"}, {"end", "Int"},
        {"Bool", "Bool"}, {"so", "Bool"}, {"not", "Bool"}, {"defined", "Bool"}, {"is-prime", "Bool"},
        {"contains", "Bool"}, {"starts-with", "Bool"}, {"ends-with", "Bool"},
        {"Num", "Num"}, {"Rat", "Rat"}, {"IO", "IO::Path"},
        {"Array", "Array"}, {"List", "List"}, {"list", "List"}, {"Hash", "Hash"}, {"Map", "Map"},
        {"Set", "Set"}, {"Bag", "Bag"}, {"Mix", "Mix"}, {"Seq", "Seq"}, {"Range", "Range"},
        {"Date", "Date"}, {"DateTime", "DateTime"}, {"Version", "Version"},
    };
    auto it = R.find(m);
    return it == R.end() ? nullptr : it->second;
}
// …and what a method returns on a list-ish or string invocant: a Seq. Not on a
// Supply, whose map and grep make another Supply, so only for these.
bool returnsSeq(const std::string& m, const std::string& inv) {
    static const std::set<std::string> S = {
        "map", "grep", "words", "lines", "comb", "kv", "keys", "values", "pairs", "antipairs",
        "flat", "unique", "squish", "reverse", "sort", "split", "rotor", "batch", "skip", "head",
        "tail", "pick", "roll", "permutations", "combinations", "classify", "toggle",
    };
    static const std::set<std::string> on = {
        "Str", "Array", "List", "Seq", "Range", "Hash", "Map", "Set", "Bag", "Mix", "IO::Path", "IO::Handle"};
    return S.count(m) && on.count(inv);
}
// Subroutines with one return type, for `my $line = prompt …` and kin.
const char* subReturns(const std::string& s) {
    static const std::map<std::string, const char*> R = {
        {"prompt", "Str"}, {"slurp", "Str"}, {"join", "Str"}, {"sprintf", "Str"}, {"chr", "Str"},
        {"ord", "Int"}, {"chars", "Int"}, {"elems", "Int"}, {"rand", "Num"}, {"sqrt", "Num"},
        {"lines", "Seq"}, {"words", "Seq"}, {"dir", "Seq"}, {"open", "IO::Handle"},
        {"run", "Proc"}, {"shell", "Proc"}, {"start", "Promise"}, {"now", "Instant"},
    };
    auto it = R.find(s);
    return it == R.end() ? nullptr : it->second;
}

bool isNumericType(const std::string& t) { return t == "Int" || t == "Num" || t == "Rat"; }

// Walks the tree three ways: DECLS collects the program's own names and the
// types it writes down; VARS infers the variables it does not (twice, so a
// variable assigned from an earlier one picks its type up); FACTS records,
// per line, what each construct is.
struct Walker {
    enum Phase { DECLS, VARS, FACTS } phase = DECLS;
    Facts& f;
    std::vector<std::string> cls;   // the class being walked, for `self` and `$!x`
    explicit Walker(Facts& facts) : f(facts) {}

    static int at(const Node* n, int outer) { return n && n->line > 0 ? n->line : outer; }

    bool isType(const std::string& n) const { return f.types.count(n) || find('T', n); }

    // Only a `$` variable (or a sigilless one) takes the type it is given: an
    // `@` one is an Array and a `%` one a Hash whatever is assigned to it, and
    // the type in `my Int @a` is its elements'.
    void noteVar(const std::string& name, const std::string& type, bool fixed) {
        if (name.empty() || type.empty() || name[0] == '@' || name[0] == '%' || name[0] == '&') return;
        if (fixed) { f.vars[name] = type; f.fixedVars.insert(name); return; }
        if (f.fixedVars.count(name)) return;
        auto it = f.vars.find(name);
        if (it == f.vars.end()) f.vars[name] = type;
        else if (it->second != type) it->second = "";
    }

    std::string varType(const std::string& name) const {
        auto it = f.vars.find(name);
        if (it != f.vars.end()) return it->second;
        static const std::map<std::string, std::string> special = {
            {"$*OUT", "IO::Handle"}, {"$*ERR", "IO::Handle"}, {"$*IN", "IO::Handle"},
            {"$*PROGRAM", "IO::Path"}, {"$*CWD", "IO::Path"}, {"$*TMPDIR", "IO::Path"},
            {"$*HOME", "IO::Path"}, {"$*PROGRAM-NAME", "Str"}, {"@*ARGS", "Array"},
            {"%*ENV", "Hash"}, {"$/", "Match"}, {"$*KERNEL", "Kernel"}, {"$*VM", "VM"},
            {"$*DISTRO", "Distro"}, {"$*RAKU", "Raku"}, {"$*SCHEDULER", "Scheduler"},
            {"$*THREAD", "Thread"}, {"$*TZ", "Int"}, {"$*USER", "IntStr"},
        };
        auto s = special.find(name);
        if (s != special.end()) return s->second;
        if (name.size() > 1 && name[0] == '@') return "Array";
        if (name.size() > 1 && name[0] == '%') return "Hash";
        return "";
    }

    // The type an expression statically has, or "".
    std::string typeOf(const Expr* e) const {
        if (!e) return "";
        switch (e->kind) {
            case NK::IntLit: return "Int";
            case NK::NumLit: {
                auto* n = static_cast<const NumLit*>(e);
                return n->imaginary ? "Complex" : n->isRat ? "Rat" : "Num";
            }
            case NK::StrLit: case NK::InterpStr: return "Str";
            case NK::BoolLit: return "Bool";
            case NK::RegexLit: return "Regex";
            case NK::ArrayLit: return static_cast<const ArrayLit*>(e)->isList ? "List" : "Array";
            case NK::HashLit: return "Hash";
            case NK::Range: return "Range";
            case NK::Pair: return "Pair";
            case NK::ListExpr: return "List";
            case NK::Whatever: return "Whatever";
            case NK::BlockExpr: return static_cast<const BlockExpr*>(e)->isSub ? "Sub" : "Block";
            case NK::SelfTerm: return cls.empty() ? "" : cls.back();
            case NK::VarExpr: {
                auto* v = static_cast<const VarExpr*>(e);
                if (!v->d().declType.empty()) return v->d().declType;
                return varType(v->name);
            }
            case NK::NameTerm: {
                auto* n = static_cast<const NameTerm*>(e);
                if (n->name == "True" || n->name == "False") return "Bool";
                return isType(n->name) ? n->name : "";
            }
            case NK::MethodCall: {
                auto* m = static_cast<const MethodCall*>(e);
                if (m->meta || m->hyper || m->methodExpr) return "";
                std::string inv = typeOf(m->inv.get());
                if (m->method == "new" && m->inv && m->inv->kind == NK::NameTerm) return inv;
                if (m->method == "now" && (inv == "DateTime" || inv == "Instant")) return inv;
                if (const char* r = returnsAlways(m->method)) return r;
                if (returnsSeq(m->method, inv)) return "Seq";
                return "";
            }
            case NK::Call: {
                auto* c = static_cast<const Call*>(e);
                if (f.subs.count(c->name)) return "";
                if (const char* r = subReturns(c->name)) return r;
                return "";
            }
            case NK::Assign: return typeOf(static_cast<const Assign*>(e)->value.get());
            case NK::Unary: {
                auto* u = static_cast<const Unary*>(e);
                if (u->op == "gather" || u->op == "lazy") return "Seq";
                if (u->op == "~") return "Str";
                if (u->op == "?" || u->op == "!" || u->op == "so" || u->op == "not") return "Bool";
                if (u->op == "-" || u->op == "+" || u->op == "++" || u->op == "--") {
                    std::string t = typeOf(u->operand.get());
                    return isNumericType(t) ? t : "";
                }
                return "";
            }
            case NK::Binary: {
                auto* b = static_cast<const Binary*>(e);
                const std::string& op = b->op;
                if (op == "~" || op == "x") return "Str";
                if (op == "xx") return "Seq";
                static const std::set<std::string> cmp = {
                    "==", "!=", "<", ">", "<=", ">=", "eq", "ne", "lt", "gt", "le", "ge",
                    "~~", "!~~", "===", "eqv", "=:=", "before", "after", "%%"};
                if (cmp.count(uniOpFold(op))) return "Bool";
                if (op == "cmp" || op == "<=>" || op == "leg") return "Order";
                if (op == "div" || op == "mod" || op == "gcd" || op == "lcm") return "Int";
                static const std::set<std::string> arith = {"+", "-", "*", "/", "**", "%"};
                if (arith.count(uniOpFold(op))) {
                    std::string l = typeOf(b->lhs.get()), r = typeOf(b->rhs.get());
                    if (!isNumericType(l) || !isNumericType(r)) return "";
                    if (l == "Num" || r == "Num") return "Num";
                    if (uniOpFold(op) == "/") return "Rat";
                    if (l == "Rat" || r == "Rat") return "Rat";
                    return op == "**" ? "" : "Int";   // 2 ** -1 is a Rat
                }
                return "";
            }
            case NK::Ternary: {
                auto* t = static_cast<const Ternary*>(e);
                std::string a = typeOf(t->then.get()), b = typeOf(t->els.get());
                return a == b ? a : "";
            }
            default: return "";
        }
    }

    void params(const std::vector<Param>& ps, int line) {
        for (auto& p : ps) {
            if (phase == DECLS && !p.type.empty() && !p.typeCapture) noteVar(p.name, p.type, true);
            if (phase == FACTS && p.slurpy)
                f.terms[line].insert(std::string(p.slurpyKind == 'n' ? "**" : p.slurpyKind == '1' ? "+" : "*") + p.sigil);
            expr(p.defaultVal.get(), line);
            expr(p.whereExpr.get(), line);
        }
    }
    void body(const std::vector<StmtPtr>& ss, int line) { for (auto& s : ss) stmt(s.get(), line); }

    void expr(const Expr* e, int outer) {
        if (!e) return;
        const int line = at(e, outer);
        switch (e->kind) {
            case NK::VarExpr: {
                auto* v = static_cast<const VarExpr*>(e);
                if (phase == DECLS && v->declare && !v->d().declType.empty())
                    noteVar(v->name, v->d().declType, true);
                return;
            }
            case NK::InterpStr:
                for (auto& p : static_cast<const InterpStr*>(e)->parts) expr(p.get(), line);
                return;
            case NK::ListExpr: for (auto& i : static_cast<const ListExpr*>(e)->items) expr(i.get(), line); return;
            case NK::ArrayLit: for (auto& i : static_cast<const ArrayLit*>(e)->items) expr(i.get(), line); return;
            case NK::HashLit: for (auto& i : static_cast<const HashLit*>(e)->items) expr(i.get(), line); return;
            case NK::Assign: {
                auto* a = static_cast<const Assign*>(e);
                expr(a->target.get(), line);
                expr(a->value.get(), line);
                const Expr* t = a->target.get();
                if (phase == VARS && a->op == "=" && t && t->kind == NK::VarExpr)
                    noteVar(static_cast<const VarExpr*>(t)->name, typeOf(a->value.get()), false);
                if (phase == FACTS) {
                    f.infix[line].insert(a->written());
                    if (a->op == "=") {
                        bool list = t && (t->kind == NK::ListExpr ||
                            (t->kind == NK::VarExpr && !static_cast<const VarExpr*>(t)->name.empty() &&
                             (static_cast<const VarExpr*>(t)->name[0] == '@' ||
                              static_cast<const VarExpr*>(t)->name[0] == '%')));
                        f.listAssign[line].push_back(list);
                    }
                }
                return;
            }
            case NK::Binary: {
                auto* b = static_cast<const Binary*>(e);
                expr(b->lhs.get(), line);
                expr(b->rhs.get(), line);
                if (phase == FACTS) {
                    const std::string& op = b->written();
                    f.infix[line].insert(op);
                    // a hyper operator is kept as `>>+<<` however it was written
                    if (op.find(">>") != std::string::npos || op.find("<<") != std::string::npos) {
                        std::string uni = op;
                        for (size_t at; (at = uni.find(">>")) != std::string::npos;) uni.replace(at, 2, "»");
                        for (size_t at; (at = uni.find("<<")) != std::string::npos;) uni.replace(at, 2, "«");
                        f.infix[line].insert(uni);
                    }
                    // a meta-operator's base operator is spelled in the source too
                    if (op.size() > 1 && std::strchr("ZXRS", op[0]) && !ascii::isalpha((unsigned char)op[1]))
                        f.infix[line].insert(op.substr(1));
                }
                return;
            }
            case NK::ChainExpr: {
                auto* c = static_cast<const ChainExpr*>(e);
                for (auto& o : c->operands) expr(o.get(), line);
                if (phase == FACTS) for (auto& op : c->ops) f.infix[line].insert(op);
                return;
            }
            case NK::Unary: {
                auto* u = static_cast<const Unary*>(e);
                expr(u->operand.get(), line);
                if (phase == FACTS) (u->postfix ? f.postfix : f.prefix)[line].insert(u->op);
                return;
            }
            case NK::Ternary: {
                auto* t = static_cast<const Ternary*>(e);
                expr(t->cond.get(), line); expr(t->then.get(), line); expr(t->els.get(), line);
                if (phase == FACTS) { f.infix[line].insert("??"); f.infix[line].insert("!!"); }
                return;
            }
            case NK::Range: {
                auto* r = static_cast<const RangeExpr*>(e);
                expr(r->from.get(), line); expr(r->to.get(), line);
                if (phase == FACTS)
                    f.infix[line].insert(std::string(r->exFrom ? "^" : "") + ".." + (r->exTo ? "^" : ""));
                return;
            }
            case NK::Pair: {
                auto* p = static_cast<const PairExpr*>(e);
                expr(p->keyExpr.get(), line); expr(p->value.get(), line);
                if (phase == FACTS) {
                    if (p->colonForm) f.terms[line].insert(":");
                    else f.infix[line].insert("=>");
                }
                return;
            }
            case NK::Call: {
                auto* c = static_cast<const Call*>(e);
                expr(c->callee.get(), line);
                for (auto& a : c->args) expr(a.get(), line);
                if (phase == FACTS && !c->name.empty()) f.calls[line].insert(c->name);
                return;
            }
            case NK::MethodCall: {
                auto* m = static_cast<const MethodCall*>(e);
                expr(m->inv.get(), line);
                expr(m->methodExpr.get(), line);
                for (auto& a : m->args) expr(a.get(), line);
                // `».m` and `*.m` call m on values the program has not got yet
                bool later = m->hyper || (m->inv && m->inv->kind == NK::Whatever);
                if (phase == FACTS && !m->method.empty())
                    f.methods[line].push_back({m->method, later ? "" : typeOf(m->inv.get())});
                return;
            }
            case NK::Index: {
                auto* x = static_cast<const Index*>(e);
                expr(x->base.get(), line); expr(x->index.get(), line);
                return;
            }
            case NK::BlockExpr: {
                auto* b = static_cast<const BlockExpr*>(e);
                params(b->params, line);
                body(b->body, line);
                if (phase == FACTS && b->isPointy) f.terms[line].insert("->");
                return;
            }
            case NK::Whatever:
                if (phase == FACTS) f.terms[line].insert("*");
                return;
            case NK::NameTerm:
                if (phase == FACTS) f.names[line].insert(static_cast<const NameTerm*>(e)->name);
                return;
            case NK::AllomorphLit: expr(static_cast<const AllomorphLit*>(e)->num.get(), line); return;
            case NK::SymbolicRef: expr(static_cast<const SymbolicRef*>(e)->nameExpr.get(), line); return;
            default: return;
        }
    }

    void classDecl(const ClassDecl* c, int line) {
        if (phase == DECLS && !c->name.empty()) {
            f.types.emplace(c->name, line);
            f.typeKind.emplace(c->name, c->isRole ? "role" : c->isGrammar ? "grammar"
                                       : c->isModuleDecl ? "module" : c->isPackage ? "package" : "class");
            auto& ps = f.parents[c->name];
            if (!c->parent.empty()) ps.push_back(c->parent);
            for (auto& p : c->extraParents) ps.push_back(p);
            for (auto& r : c->roles) ps.push_back(r);
            for (auto& a : c->attrs) {
                if (!a.type.empty()) {
                    noteVar(std::string(1, a.sigil) + "!" + a.name, a.type, true);
                    noteVar(std::string(1, a.sigil) + "." + a.name, a.type, true);
                }
                if (a.pub) {
                    f.members[c->name].emplace(a.name, a.declLine ? a.declLine : line);
                    f.anyMember.emplace(a.name, a.declLine ? a.declLine : line);
                }
            }
            for (auto& r : c->rules) {
                f.members[c->name].emplace(r.name, r.declLine ? r.declLine : line);
                f.anyMember.emplace(r.name, r.declLine ? r.declLine : line);
            }
        }
        cls.push_back(c->name);
        for (auto& a : c->attrs) { expr(a.def.get(), a.declLine ? a.declLine : line); }
        for (auto& m : c->methods) stmt(m.get(), line);
        body(c->body, line);
        cls.pop_back();
    }

    void stmt(const Stmt* s, int outer) {
        if (!s) return;
        const int line = at(s, outer);
        switch (s->kind) {
            case NK::ExprStmt: expr(static_cast<const ExprStmt*>(s)->e.get(), line); return;
            case NK::VarDecl: {
                auto* d = static_cast<const VarDecl*>(s);
                expr(d->init.get(), line);
                if (phase == VARS && d->names.size() == 1 && d->op == "=")
                    noteVar(d->names[0], typeOf(d->init.get()), false);
                if (phase == FACTS && d->init) {
                    f.infix[line].insert(d->op);
                    if (d->op == "=")
                        f.listAssign[line].push_back(d->names.size() > 1 ||
                            (!d->names.empty() && !d->names[0].empty() && (d->names[0][0] == '@' || d->names[0][0] == '%')));
                }
                return;
            }
            case NK::SubDecl: {
                auto* d = static_cast<const SubDecl*>(s);
                if (phase == DECLS && !d->name.empty()) {
                    if (d->isMethod || d->isSubmethod) {
                        if (!cls.empty()) f.members[cls.back()].emplace(d->name, line);
                        f.anyMember.emplace(d->name, line);
                    } else {
                        f.subs.emplace(d->name, line);
                    }
                }
                params(d->params, line);
                body(d->body, line);
                return;
            }
            case NK::IfStmt: {
                auto* i = static_cast<const IfStmt*>(s);
                for (auto& br : i->branches) { expr(br.first.get(), line); if (br.second) stmt(br.second.get(), line); }
                for (auto& bp : i->branchParams) params(bp, line);
                if (i->elseBlock) stmt(i->elseBlock.get(), line);
                return;
            }
            case NK::WhileStmt: {
                auto* w = static_cast<const WhileStmt*>(s);
                expr(w->cond.get(), line); params(w->params, line);
                if (w->body) stmt(w->body.get(), line);
                return;
            }
            case NK::ForStmt: {
                auto* fs = static_cast<const ForStmt*>(s);
                expr(fs->list.get(), line); params(fs->params, line);
                if (phase == FACTS && !fs->vars.empty()) f.terms[line].insert("->");
                if (fs->body) stmt(fs->body.get(), line);
                return;
            }
            case NK::LoopStmt: {
                auto* l = static_cast<const LoopStmt*>(s);
                expr(l->init.get(), line); expr(l->cond.get(), line); expr(l->incr.get(), line);
                if (l->body) stmt(l->body.get(), line);
                return;
            }
            case NK::RepeatStmt: {
                auto* r = static_cast<const RepeatStmt*>(s);
                if (r->body) stmt(r->body.get(), line);
                expr(r->cond.get(), line);
                return;
            }
            case NK::GivenStmt: {
                auto* g = static_cast<const GivenStmt*>(s);
                expr(g->topic.get(), line); params(g->params, line);
                if (g->body) stmt(g->body.get(), line);
                if (g->elseBody) stmt(g->elseBody.get(), line);
                return;
            }
            case NK::WhenStmt: {
                auto* w = static_cast<const WhenStmt*>(s);
                expr(w->cond.get(), line);
                if (w->body) stmt(w->body.get(), line);
                return;
            }
            case NK::Block: body(static_cast<const Block*>(s)->stmts, line); return;
            case NK::ReturnStmt: expr(static_cast<const ReturnStmt*>(s)->value.get(), line); return;
            case NK::UseStmt: {
                auto* u = static_cast<const UseStmt*>(s);
                if (phase == DECLS && find('T', u->module)) f.modules.insert(u->module);
                expr(u->argExpr.get(), line);
                return;
            }
            case NK::ClassDecl: classDecl(static_cast<const ClassDecl*>(s), line); return;
            case NK::EnumDecl: {
                auto* d = static_cast<const EnumDecl*>(s);
                if (phase == DECLS && !d->name.empty()) { f.types.emplace(d->name, line); f.typeKind.emplace(d->name, "enum"); }
                expr(d->values.get(), line);
                return;
            }
            case NK::SubsetDecl: {
                auto* d = static_cast<const SubsetDecl*>(s);
                if (phase == DECLS && !d->name.empty()) {
                    f.types.emplace(d->name, line); f.typeKind.emplace(d->name, "subset");
                    if (!d->baseType.empty()) f.parents[d->name].push_back(d->baseType);
                }
                expr(d->where.get(), line);
                return;
            }
            case NK::NamedRegexDecl: {
                auto* d = static_cast<const NamedRegexDecl*>(s);
                if (phase == DECLS && !d->name.empty()) f.subs.emplace(d->name, line);
                return;
            }
            default: return;
        }
    }

    void program(const Program& p) {
        phase = DECLS; body(p.stmts, 1);
        phase = VARS;  body(p.stmts, 1); body(p.stmts, 1);
        phase = FACTS; body(p.stmts, 1);
    }
};

// ---------------------------------------------------------------------------
// The token pass.

struct Tok {
    size_t start = 0, end = 0;
    std::string text;
    const char* cls = "";
    int line = 1;
};

bool isSpace(const std::string& s) {
    for (char c : s) if (!ascii::isspace((unsigned char)c)) return false;
    return true;
}
bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }
bool endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
std::string ltrim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && ascii::isspace((unsigned char)s[b])) b++;
    return s.substr(b);
}
bool identStart(unsigned char c) { return ascii::isalpha(c) || c == '_' || c >= 0x80; }
bool identChar(unsigned char c) { return ascii::isalnum(c) || c == '_' || c == '-' || c == '\'' || c >= 0x80; }

// The operators the fallback recognises when there is no AST to ask (the
// program does not parse): the common ones, by their usual reading.
const std::vector<std::string>& fallbackOps() {
    static const std::vector<std::string> ops = {
        "+", "-", "*", "/", "**", "%", "%%", "~", "==", "!=", "<", ">", "<=", ">=", "<=>",
        "&&", "||", "//", "!", "?", "^", "=", ":=", "+=", "-=", "*=", "/=", "~=", "++", "--",
        "x", "xx", "eq", "ne", "lt", "gt", "le", "ge", "cmp", "leg", "and", "or", "not",
    };
    return ops;
}

struct Linker {
    const std::string& src;
    const Facts& f;
    bool parsed;
    Result& r;
    std::vector<Tok> toks;
    std::map<int, std::vector<MethodFact>> methods;   // a mutable copy: facts are claimed in order
    std::map<int, size_t> assignSeen;                  // `=`s consumed per line
    std::set<std::string> unknown;

    Linker(const std::string& s, const Facts& facts, bool p, Result& res)
        : src(s), f(facts), parsed(p), r(res), methods(facts.methods) {}

    // -- emitting --------------------------------------------------------
    void add(size_t start, size_t end, int line, const std::string& group, const std::string& label,
             const IndexRow* row, const std::string& note = "", bool exact = true) {
        if (!row || start >= end) return;
        Ref ref;
        ref.start = start; ref.end = end; ref.line = line;
        ref.text = src.substr(start, end - start);
        ref.group = group; ref.label = label; ref.url = absUrl(row->url);
        ref.note = note; ref.exact = exact;
        r.refs.push_back(std::move(ref));
    }
    void addLocal(size_t start, size_t end, int line, const std::string& label, int declLine) {
        Ref ref;
        ref.start = start; ref.end = end; ref.line = line;
        ref.text = src.substr(start, end - start);
        ref.group = "local"; ref.label = label; ref.declLine = declLine;
        ref.note = "declared on line " + std::to_string(declLine);
        r.refs.push_back(std::move(ref));
    }

    // -- lookups ---------------------------------------------------------
    static const IndexRow* syntax(const std::string& k) { return find('S', k); }
    static const IndexRow* heading(const std::string& page, const std::string& h) { return find('H', page + ":" + h); }

    // The member row for `name` on `type` or the nearest type it inherits
    // from; `owner` says which type documents it.
    const IndexRow* member(const std::string& type, const std::string& name, std::string& owner,
                           bool wantSub = false) const {
        std::vector<std::string> chain{type};
        for (auto& a : typeAncestry(type)) if (a != type) chain.push_back(a);
        for (auto& t : chain) {
            auto* rows = findAll('M', t + " " + name);
            if (!rows) continue;
            const IndexRow* best = nullptr;
            for (auto* row : *rows) {
                bool isSub = std::strcmp(row->info, "sub") == 0;
                if (isSub == wantSub || std::strcmp(row->info, "routine") == 0) { best = row; break; }
                if (!best) best = row;
            }
            if (best) { owner = t; return best; }
        }
        return nullptr;
    }

    // A method on one of the program's own types: the declaring line, walking
    // its parents and roles. 0 when none of them declares it; `core` is then the
    // first built-in type up the chain (or Any, every class's default parent).
    int ownMember(const std::string& type, const std::string& name, std::string& core, int depth = 0) const {
        if (depth > 16) return 0;
        auto m = f.members.find(type);
        if (m != f.members.end()) {
            auto it = m->second.find(name);
            if (it != m->second.end()) return it->second;
        }
        auto ps = f.parents.find(type);
        if (ps != f.parents.end()) {
            for (auto& p : ps->second) {
                if (f.types.count(p)) { if (int l = ownMember(p, name, core, depth + 1)) return l; }
                else if (core.empty()) core = p;
            }
        }
        if (core.empty()) core = "Any";
        return 0;
    }

    std::string candidates(const IndexRow* routine) const {
        std::string types = routine->info;
        std::string out;
        size_t n = 0, i = 0;
        while (i < types.size()) {
            size_t sp = types.find(' ', i);
            std::string t = types.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            i = sp == std::string::npos ? types.size() : sp + 1;
            if (t.empty() || t.find('/') != std::string::npos) continue;
            if (n == 6) { out += ", …"; break; }
            out += (n++ ? ", " : "") + t;
        }
        return out;
    }

    void method(const Tok& t, bool meta, bool priv) {
        const std::string& name = t.text;
        // the AST's fact for this call: on its own line, or — in a chain
        // written over several lines — on a line above, where the parser may
        // have dated the node
        std::string type;
        bool found = false;
        for (int l = t.line; l >= std::max(1, t.line - 4) && !found; l--) {
            auto it = methods.find(l);
            if (it == methods.end()) continue;
            for (auto& mf : it->second)
                if (!mf.used && mf.name == name) { mf.used = true; type = mf.type; found = true; break; }
        }

        if (priv) {
            for (auto& [cn, mem] : f.members) {
                auto it = mem.find(name);
                if (it != mem.end()) { addLocal(t.start, t.end, t.line, "method !" + name + " (" + cn + ")", it->second); return; }
            }
            return;
        }
        if (meta) {
            // `.^name` is the type's HOW's: Metamodel::ClassHOW, which gets
            // most of its methods from the Metamodel roles it does
            const IndexRow* row = find('M', "Metamodel::ClassHOW " + name);
            for (size_t i = 0; !row && i < kIndexSize; i++) {
                const IndexRow& m = kIndex[i];
                if (m.kind == 'M' && startsWith(m.key, "Metamodel::") && endsWith(m.key, " " + name)) row = &m;
            }
            if (row) add(t.start, t.end, t.line, "routine", ".^" + name, row, "a metamodel method");
            return;
        }
        std::string shown = type;
        if (!type.empty() && f.types.count(type)) {
            std::string core;
            if (int l = ownMember(type, name, core)) {
                addLocal(t.start, t.end, t.line, "method " + name + " (" + type + ")", l);
                return;
            }
            type = core;   // inherited from a built-in: look it up there
        }
        if (!type.empty()) {
            std::string owner;
            if (const IndexRow* row = member(type, name, owner)) {
                add(t.start, t.end, t.line, "routine", shown + "." + name, row,
                    owner == shown ? "" : "documented on " + owner);
                return;
            }
        }
        const IndexRow* composite = find('R', name);
        auto own = f.anyMember.find(name);
        if (own != f.anyMember.end() && (!composite || type.empty())) {
            addLocal(t.start, t.end, t.line, "method " + name, own->second);
            return;
        }
        if (composite) {
            std::string why = type.empty() ? "invocant not known" : "not documented on " + type;
            std::string c = candidates(composite);
            add(t.start, t.end, t.line, "routine", "." + name, composite, c.empty() ? why : why + ": " + c, false);
            return;
        }
        unknown.insert("." + name);
    }

    // A sub call, or a name used as a term.
    bool sub(const Tok& t) {
        const std::string& name = t.text;
        auto own = f.subs.find(name);
        if (own != f.subs.end()) {
            if (name == "MAIN") { add(t.start, t.end, t.line, "syntax", "sub MAIN", find('L', "create-cli")); return true; }
            if (own->second != t.line) addLocal(t.start, t.end, t.line, "sub " + name, own->second);
            return true;
        }
        for (auto& m : f.modules) {
            if (const IndexRow* row = find('M', m + " " + name)) {
                add(t.start, t.end, t.line, "routine", name, row, "from " + m);
                return true;
            }
        }
        if (const IndexRow* row = heading("terms", "term " + name)) {
            add(t.start, t.end, t.line, "routine", name, row);
            return true;
        }
        std::string owner;
        if (const IndexRow* row = member("independent-routines", name, owner, true)) {
            add(t.start, t.end, t.line, "routine", name, row);
            return true;
        }
        if (const IndexRow* row = find('R', name)) {
            add(t.start, t.end, t.line, "routine", name, row);
            return true;
        }
        return false;
    }

    bool type(const Tok& t) {
        const std::string& name = t.text;
        auto own = f.types.find(name);
        if (own != f.types.end()) {
            if (own->second != t.line) {
                auto k = f.typeKind.find(name);
                addLocal(t.start, t.end, t.line, (k == f.typeKind.end() ? "class" : k->second) + " " + name, own->second);
            }
            return true;
        }
        std::string n = name;
        if (n == "True" || n == "False") n = "Bool";
        else if (n == "Less" || n == "Same" || n == "More") n = "Order";
        else if (n == "Inf" || n == "NaN") n = "Num";
        if (const IndexRow* row = find('T', n)) {
            if (!typeNameLead((unsigned char)name[0])) return false;
            add(t.start, t.end, t.line, "type", n, row);
            return true;
        }
        return false;
    }

    const IndexRow* operatorRow(const std::string& kind, const std::string& op, std::string& label, int line) {
        label = kind + " " + op;
        if (kind == "infix") {
            if (op == "??" || op == "!!") { label = "infix ?? !!"; return find('O', "infix ?? !!"); }
            if (op == "=") {
                auto la = f.listAssign.find(line);
                size_t i = assignSeen[line]++;
                bool list = la != f.listAssign.end() && i < la->second.size() && la->second[i];
                label = list ? "list assignment =" : "item assignment =";
                return find('O', list ? "infix = (list assignment)" : "infix = (item assignment)");
            }
            if (const IndexRow* row = find('O', "infix " + op)) return row;
            if (op == "!~~") return find('O', "infix ~~");
            if (op.size() >= 2 && op.back() == '=' && op != "==" && op != "<=" && op != ">=" && op != "!=") {
                label = "assignment " + op;
                return heading("operators", "Assignment operators");
            }
            if (op.size() >= 2 && op[0] == '!') { label = "negated " + op; return syntax("! (negation metaoperator)"); }
            if (startsWith(op, "»") || startsWith(op, ">>") || startsWith(op, "«") || startsWith(op, "<<")) {
                label = "hyper " + op; return heading("operators", "Hyper operators");
            }
            return nullptr;
        }
        if (kind == "prefix") {
            if (op.size() > 2 && op.front() == '[' && op.back() == ']') {
                label = "reduction " + op; return heading("operators", "Reduction operators");
            }
            return find('O', "prefix " + op);
        }
        return find('O', kind + " " + op);
    }

    // -- the walk --------------------------------------------------------
    // Is a term expected next — i.e. would `-` here be prefix?
    bool termExpected = true;

    // The operators the AST saw on `line`, plus the ones that read one way
    // wherever they are written.
    std::set<std::string> opsOn(int line) const {
        std::set<std::string> cand;
        auto addAll = [&](const std::map<int, std::set<std::string>>& m) {
            auto it = m.find(line);
            if (it != m.end()) cand.insert(it->second.begin(), it->second.end());
        };
        addAll(f.infix); addAll(f.prefix); addAll(f.postfix); addAll(f.terms);
        if (!parsed) for (auto& o : fallbackOps()) cand.insert(o);
        static const char* const fixed[] = {"->", "<->", "-->", "=>", "??", "!!", "~~", "..", "...", "^..", "..^", "^..^", ":="};
        for (auto* o : fixed) cand.insert(o);
        return cand;
    }

    void plain(const Tok& t, size_t ti) {
        const std::string& p = t.text;
        int line = t.line;
        std::set<std::string> cand = opsOn(line);

        // what follows this span: a method name makes `.^` / `».` method
        // operators, an array or hash variable makes `*` a slurpy
        const Tok* next = ti + 1 < toks.size() ? &toks[ti + 1] : nullptr;
        bool nameNext = next && (std::strcmp(next->cls, "n") == 0 || std::strcmp(next->cls, "nb") == 0);
        bool slurpyNext = next && std::strcmp(next->cls, "nv") == 0 && !next->text.empty() &&
                          (next->text[0] == '@' || next->text[0] == '%' || next->text[0] == '$');

        size_t j = 0;
        while (j < p.size()) {
            unsigned char c = (unsigned char)p[j];
            if (c == '\n') { line++; cand = opsOn(line); j++; continue; }
            if (ascii::isspace(c)) { j++; continue; }

            // `*@rest`, `**@lol`, `+@one` in a signature
            if (slurpyNext && termExpected) {
                static const std::pair<const char*, const char*> slurpies[] = {
                    {"**", "Non-flattening slurpy: **@"}, {"*", "Flattening slurpy: *@"},
                    {"+", "Single argument rule slurpy: +@"}};
                bool done = false;
                auto ts = f.terms.find(line);
                for (auto& [mark, h] : slurpies) {
                    size_t n = std::strlen(mark);
                    bool declared = ts != f.terms.end() && ts->second.count(mark + next->text.substr(0, 1));
                    if (j + n == p.size() && p.compare(j, n, mark) == 0 && (declared || !parsed) && next->text[0] != '$') {
                        add(t.start + j, t.start + j + n, line, "syntax", std::string("slurpy ") + mark + next->text[0],
                            heading("signatures", h));
                        j += n; done = true; break;
                    }
                }
                if (done) break;
            }

            // a method-call operator right before a method name
            if (nameNext) {
                static const char* const mops[] = {"».", ">>.", ".^", ".?", ".=", ".&"};
                bool done = false;
                for (auto* mo : mops) {
                    size_t n = std::strlen(mo);
                    if (j + n == p.size() && p.compare(j, n, mo) == 0) {
                        add(t.start + j, t.start + j + n, line, "operator", std::string("methodop ") + mo,
                            find('O', std::string("methodop ") + mo));
                        j += n; done = true; break;
                    }
                }
                if (done) break;
                if (c == '.' && j + 1 == p.size()) break;
            }

            // the longest candidate operator spelled here (`[+]` before `[`)
            std::string best;
            for (auto& o : cand)
                if (o.size() > best.size() && p.compare(j, o.size(), o) == 0 && !ascii::isalpha((unsigned char)o[0]))
                    best = o;
            if (best.empty()) {
                if (c == '(' || c == '[' || c == '{' || c == ',' || c == ';' || c == '}') termExpected = true;
                else if (c == ')' || c == ']') termExpected = false;
                else termExpected = true;   // an operator no fact names
                j++;
                while (j < p.size() && ((unsigned char)p[j] & 0xC0) == 0x80) j++;
                continue;
            }

            std::string label;
            const IndexRow* row = nullptr;
            std::string group = "operator";
            bool isTerm = termExpected;
            auto has = [&](const std::map<int, std::set<std::string>>& m) {
                auto it = m.find(line);
                return it != m.end() && it->second.count(best) > 0;
            };
            if (best == "->" || best == "<->") {
                group = "syntax"; label = "->"; row = syntax("->");
            } else if (best == "-->") {
                group = "syntax"; label = "-->"; row = heading("signatures", "Return type arrow: -->");
            } else if (best == ":" ) {
                group = "syntax"; label = "colon pair"; row = heading("syntax", "Pair literals");
            } else if (best == "*" && isTerm && (has(f.terms) || !parsed)) {
                group = "type"; label = "Whatever"; row = find('T', "Whatever");
            } else if ((best == "++" || best == "--") && !isTerm) {
                row = operatorRow("postfix", best, label, line); isTerm = true;
            } else if (isTerm && (has(f.prefix) || (!parsed && find('O', "prefix " + best)) ||
                                  (best.size() > 2 && best[0] == '[') || best == "^")) {
                row = operatorRow("prefix", best, label, line);
            } else {
                row = operatorRow("infix", best, label, line);
            }
            if (row) add(t.start + j, t.start + j + best.size(), line, group, label, row);
            j += best.size();
            // after a postfix the term is complete; after anything else one is expected
            termExpected = (label.compare(0, 7, "postfix") == 0) ? false : (best == "*" && isTerm) ? false : true;
        }
    }

    // `.method` and `{ sub }` inside a double-quoted string
    void interpolation(const Tok& t) {
        const std::string& s = t.text;
        int line = t.line;
        for (size_t k = 0; k + 1 < s.size(); k++) {
            if (s[k] == '\n') { line++; continue; }
            if ((s[k] == '.' || s[k] == '{') && identStart((unsigned char)s[k + 1])) {
                size_t e = k + 1;
                while (e < s.size() && identChar((unsigned char)s[e])) e++;
                Tok sub{t.start + k + 1, t.start + e, s.substr(k + 1, e - k - 1), "n", line};
                if (s[k] == '.') {
                    auto it = methods.find(line);
                    bool known = false;
                    if (it != methods.end())
                        for (auto& mf : it->second) if (!mf.used && mf.name == sub.text) { known = true; break; }
                    if (known) method(sub, false, false);
                } else {
                    auto it = f.calls.find(line);
                    if (it != f.calls.end() && it->second.count(sub.text)) this->sub(sub);
                }
                k = e - 1;
            }
        }
    }

    void string(const Tok& t, const Tok* prev) {
        const std::string& s = t.text;
        std::string label;
        const IndexRow* row = nullptr;
        bool interpolates = false;
        size_t mark = 1;   // how much of the span the link covers: the opening
        if (startsWith(s, "'")) { label = "q (single quotes)"; row = syntax("q"); }
        else if (startsWith(s, "\"")) { label = "qq (double quotes)"; row = syntax("qq"); interpolates = true; }
        else if (startsWith(s, "｢")) { label = "Q (corner brackets)"; row = syntax("Q"); mark = std::strlen("｢"); }
        else if (startsWith(s, "«")) { label = "« » (word quoting)"; row = syntax("qqww"); mark = std::strlen("«"); }
        else if (startsWith(s, "<<")) { label = "<< >> (word quoting)"; row = syntax("qqww"); mark = 2; }
        else if (startsWith(s, "<")) {
            bool subscript = prev && prev->end == t.start &&
                (std::strcmp(prev->cls, "nv") == 0 || endsWith(prev->text, "]") || endsWith(prev->text, "}") || endsWith(prev->text, ")"));
            if (subscript) { label = "postcircumfix < >"; row = find('O', "postcircumfix < >"); }
            else { label = "< > (word quoting)"; row = heading("quoting", "Word quoting: < >"); }
        }
        else if (s.size() > 1 && (s[0] == 'q' || s[0] == 'Q')) {
            size_t w = 0;
            while (w < s.size() && ascii::isalpha((unsigned char)s[w])) w++;
            std::string word = s.substr(0, w);
            mark = w;
            if (s.find(":to") != std::string::npos || s.find(":heredoc") != std::string::npos) {
                label = "heredoc :to"; row = syntax("heredocs :to"); mark = s.find(':') + 3;
            } else if (word == "qw") { label = "qw"; row = heading("quoting", "Word quoting: qw"); }
            else if (word == "q" || word == "qq" || word == "Q" || word == "qqw" || word == "qww" ||
                     word == "qqww" || word == "qx" || word == "qqx") { label = word; row = syntax(word); }
            interpolates = word.size() > 1 && word[0] == 'q' && word[1] == 'q';
        }
        // the whole literal is the link, unless something inside it has a link
        // of its own: then the opening is
        size_t before = r.refs.size();
        if (interpolates) interpolation(t);
        if (row) add(t.start, t.start + (r.refs.size() == before ? s.size() : std::min(mark, s.size())), t.line,
                     "syntax", label, row);
    }

    void regex(const Tok& t) {
        const std::string& s = t.text;
        size_t w = 0;
        while (w < s.size() && ascii::isalpha((unsigned char)s[w])) w++;
        std::string word = s.substr(0, w);
        std::string label = "/ /";
        const IndexRow* row = syntax("/ /");
        if (word == "s") { label = "s/ / /"; row = syntax("s/ / /"); }
        else if (word == "S") { label = "S/ / /"; row = syntax("S/// non-destructive substitution"); }
        else if (word == "tr" || word == "TR") { label = word + "/ / /"; row = find('R', "trans"); }
        else if (word == "rx" || word == "m") label = word + "/ /";
        else if (!word.empty() || s.empty() || s[0] != '/') row = nullptr;   // a regex/token/rule body: the keyword's
        size_t before = r.refs.size();

        // adverbs right after the word: m:i/…/, s:g/…/
        static const std::map<std::string, std::string> adverb = {
            {"i", ":ignorecase"}, {"ignorecase", ":ignorecase"}, {"m", ":ignoremark"}, {"ignoremark", ":ignoremark"},
            {"g", ":global"}, {"global", ":global"}, {"s", ":sigspace"}, {"sigspace", ":sigspace"},
            {"r", ":ratchet"}, {"ratchet", ":ratchet"}, {"ex", ":exhaustive"}, {"exhaustive", ":exhaustive"},
            {"ov", ":overlap"}, {"overlap", ":overlap"}, {"c", ":continue"}, {"continue", ":continue"},
            {"p", ":pos"}, {"pos", ":pos"}, {"x", ":x"}, {"ii", ":samecase"}, {"samecase", ":samecase"},
            {"mm", ":samemark"}, {"samemark", ":samemark"}, {"ss", ":samespace"}, {"samespace", ":samespace"},
        };
        size_t k = w;
        int line = t.line;
        while (k < s.size() && s[k] == ':') {
            size_t e = k + 1;
            while (e < s.size() && ascii::isalnum((unsigned char)s[e])) e++;
            auto it = adverb.find(s.substr(k + 1, e - k - 1));
            if (it != adverb.end()) add(t.start + k, t.start + e, line, "syntax", it->second, syntax(it->second));
            if (e < s.size() && s[e] == '(') { while (e < s.size() && s[e] != ')') e++; if (e < s.size()) e++; }
            k = e;
        }
        // inside: backslash classes and the bracketed forms
        for (; k + 1 < s.size(); k++) {
            if (s[k] == '\n') { line++; continue; }
            if (s[k] == '\\') {
                char c = (char)ascii::tolower((unsigned char)s[k + 1]);
                if (std::strchr("dwshvntx", c)) {
                    std::string key = std::string("\\") + c;
                    add(t.start + k, t.start + k + 2, line, "syntax", key, syntax(key));
                }
                k++;
                continue;
            }
            if (s[k] == '<' && s[k + 1] == '[') { add(t.start + k, t.start + k + 2, line, "syntax", "<[ ]>", syntax("<[ ]>")); k++; continue; }
            if (s[k] == '<' && s[k + 1] == '(') { add(t.start + k, t.start + k + 2, line, "syntax", "<( )>", syntax("<( )>")); k++; continue; }
        }
        // as for a string: the whole regex is the link unless its insides have theirs
        if (row) add(t.start, t.start + (r.refs.size() == before ? s.size() : std::max<size_t>(w, 1)), t.line,
                     "syntax", label, row);
    }

    void variable(const Tok& t) {
        const std::string& v = t.text;
        if (v.size() < 2) return;
        std::string label = v;
        const IndexRow* row = nullptr;
        if (v == "$_" || v == "$/" || v == "$!" || v == "@_" || v == "$¢") row = syntax(v);
        else if (v[0] == '$' && ascii::isdigit((unsigned char)v[1])) { label = "$0, $1 …"; row = syntax("$0"); }
        else if (v[0] == '$' && v[1] == '<') { label = "$<named>"; row = syntax("$<named>"); }
        else {
            char tw = v[1];
            static const std::string twigils = "*?.!^:=~";
            if (twigils.find(tw) != std::string::npos) {
                row = heading("variables", v);
                if (!row && (v == "$*IN" || v == "$*OUT" || v == "$*ERR"))
                    row = heading("variables", "Special filehandles: STDIN, STDOUT and STDERR");
                if (!row) { label = std::string("twigil ") + tw; row = heading("variables", std::string("The ") + tw + " twigil"); }
            }
        }
        if (row) add(t.start, t.end, t.line, "variable", label, row);
    }

    void number(const Tok& t) {
        const std::string& n = t.text;
        std::string type = "Int";
        if (std::strcmp(t.cls, "mf") == 0) {
            if (!n.empty() && n.back() == 'i') type = "Complex";
            else if (n.find_first_of("eE") != std::string::npos && !startsWith(n, "0x")) type = "Num";
            else type = "Rat";
        } else if (!n.empty() && n.back() == 'i') type = "Complex";
        add(t.start, t.end, t.line, "type", type, find('T', type));
    }

    // A keyword: a statement, a declarator, a phaser, a prefix — or a word
    // operator the highlighter colours as one (`so`, `and`).
    void keyword(const Tok& t, const Tok* nextSig) {
        const std::string& k = t.text;
        auto has = [&](const std::map<int, std::set<std::string>>& m) {
            auto it = m.find(t.line);
            return it != m.end() && it->second.count(k) > 0;
        };
        auto callsIt = f.calls.find(t.line);
        bool called = callsIt != f.calls.end() && callsIt->second.count(k);
        if (has(f.infix)) { add(t.start, t.end, t.line, "operator", "infix " + k, find('O', "infix " + k)); return; }
        if ((has(f.prefix) || called) && find('O', "prefix " + k)) {
            add(t.start, t.end, t.line, "operator", "prefix " + k, find('O', "prefix " + k)); return;
        }
        static const std::map<std::string, std::string> alias = {
            {"while", "while until"}, {"until", "while until"}, {"with", "with orwith without"},
            {"orwith", "with orwith without"}, {"without", "with orwith without"}, {"gather", "gather take"},
            {"take", "gather take"}, {"else", "else elsif"}, {"elsif", "else elsif"}, {"default", "default when"},
            {"supply", "supply emit"}, {"emit", "supply emit"}, {"do", "do (statement prefix)"},
            {"try", "try (statement prefix)"}, {"lazy", "lazy (statement prefix)"}, {"eager", "eager (statement prefix)"},
            {"quietly", "quietly (statement prefix)"}, {"sink", "sink (statement prefix)"},
            {"race", "race (statement prefix)"}, {"submethod", "Submethods"}, {"handles", "delegation (trait handles)"},
            {"proceed", "succeed"},
        };
        const IndexRow* row = nullptr;
        auto a = alias.find(k);
        if (a != alias.end()) row = syntax(a->second);
        if (!row && k == "is" && nextSig && (std::strcmp(nextSig->cls, "n") == 0 || std::strcmp(nextSig->cls, "nb") == 0 || std::strcmp(nextSig->cls, "k") == 0)) {
            row = syntax("is " + nextSig->text);
            if (!row && f.types.count(nextSig->text) == 0 && !find('T', nextSig->text)) row = find('L', "traits");
            if (!row) row = syntax("is (class)");
        }
        if (!row && k == "self") row = heading("terms", "term self");
        if (!row && k == "method") row = heading("objects", "Methods");
        if (!row && k == "grammar") row = find('L', "grammars");
        if (!row && (k == "enum" || k == "subset")) row = heading("typesystem", k);
        if (!row && (k == "package" || k == "module")) row = find('L', "packages");
        if (!row && k == "where") row = heading("signatures", "Type constraints");
        if (!row) row = syntax(k);
        if (!row) row = heading("variables", "The " + k + " declarator");
        if (!row) row = heading("variables", "The " + k + " keyword");
        if (!row) row = heading("phasers", k);
        if (!row) row = find('O', "prefix " + k);
        if (!row) row = find('O', "infix " + k);
        if (!row) row = find('R', k);
        if (row) add(t.start, t.end, t.line, "syntax", k, row);
    }

    void name(const Tok& t, const Tok* prev, const Tok* nextSig, const Tok* prevSig) {
        const std::string& n = t.text;
        // a method: `.name`, `.?name`, `».name`, `.^name`, `!name` on self
        if (prev && prev->end == t.start && std::strcmp(prev->cls, "") == 0) {
            std::string p = prev->text;
            bool meta = endsWith(p, ".^");
            bool dotted = !endsWith(p, "..") && (endsWith(p, ".") || endsWith(p, ".?") || endsWith(p, ".=") ||
                                                  endsWith(p, ".+") || endsWith(p, ".*") || endsWith(p, ".&") || meta);
            if (dotted) { method(t, meta, false); termExpected = false; return; }
            if (endsWith(p, "!") && p.size() == 1 && prevSig && (prevSig->text == "self" || std::strcmp(prevSig->cls, "nv") == 0)) {
                method(t, false, true); termExpected = false; return;
            }
        }
        // a word operator, or the letter of a meta-operator (Z+, X~, R-)
        auto inSet = [&](const std::map<int, std::set<std::string>>& m, const std::string& key) {
            auto it = m.find(t.line);
            return it != m.end() && it->second.count(key) > 0;
        };
        if (inSet(f.infix, n) || inSet(f.prefix, n)) {
            bool pre = inSet(f.prefix, n) && (!inSet(f.infix, n) || termExpected);
            if (n == "Z" || n == "X") {
                add(t.start, t.end, t.line, "operator", "infix " + n, find('O', "infix " + n));
            } else {
                std::string kind = pre ? "prefix" : "infix";
                add(t.start, t.end, t.line, "operator", kind + " " + n, find('O', kind + " " + n));
            }
            termExpected = true;
            return;
        }
        if (n.size() == 1 && std::strchr("ZXRS", n[0])) {
            auto it = f.infix.find(t.line);
            if (it != f.infix.end())
                for (auto& op : it->second)
                    if (op.size() > 1 && op[0] == n[0] && !ascii::isalpha((unsigned char)op[1])) {
                        static const std::map<char, std::pair<const char*, const char*>> meta = {
                            {'Z', {"zip metaoperator Z", "Z (zip metaoperator)"}},
                            {'X', {"cross metaoperator X", "X (cross metaoperator)"}},
                        };
                        auto m = meta.find(n[0]);
                        if (m != meta.end()) add(t.start, t.end, t.line, "operator", m->second.first, syntax(m->second.second));
                        else add(t.start, t.end, t.line, "operator", n == "R" ? "reverse metaoperator R" : "sequential metaoperator S",
                                 heading("operators", n == "R" ? "Reversed operators" : "Sequential operators"));
                        termExpected = true;
                        return;
                    }
        }
        // a declaration's own name, a pair key, a named argument: not a use
        if (prevSig && std::strcmp(prevSig->cls, "k") == 0) {
            static const std::set<std::string> decl = {
                "sub", "method", "submethod", "multi", "proto", "only", "class", "role", "grammar",
                "enum", "subset", "module", "package", "token", "rule", "regex"};
            if (decl.count(prevSig->text)) {
                // the few declarations whose NAME is documented: MAIN, TWEAK …
                static const std::map<std::string, std::pair<char, const char*>> named = {
                    {"MAIN", {'L', "create-cli"}}, {"USAGE", {'L', "create-cli"}}, {"TWEAK", {'S', "TWEAK"}},
                    {"BUILD", {'S', "BUILD"}}, {"EXPORT", {'S', "sub EXPORT"}}, {"DESTROY", {'S', "DESTROY"}},
                    {"TOP", {'S', "TOP"}}};
                auto it = named.find(n);
                if (it != named.end())
                    add(t.start, t.end, t.line, "syntax", prevSig->text + " " + n, find(it->second.first, it->second.second));
                termExpected = false;
                return;
            }
        }
        if (nextSig && startsWith(ltrim(nextSig->text), "=>") && std::strcmp(nextSig->cls, "") == 0) { termExpected = false; return; }
        if (prev && prev->end == t.start && endsWith(prev->text, ":") && !endsWith(prev->text, "::")) { termExpected = false; return; }

        if (type(t)) { termExpected = false; return; }
        auto callsIt = f.calls.find(t.line);
        bool called = callsIt != f.calls.end() && callsIt->second.count(n);
        auto namesIt = f.names.find(t.line);
        called = called || (namesIt != f.names.end() && namesIt->second.count(n));
        if (called || f.subs.count(n) || std::strcmp(t.cls, "nb") == 0 || !parsed) {
            if (sub(t)) { termExpected = true; return; }
            if (std::strcmp(t.cls, "nb") == 0 || called) unknown.insert(n);
        }
        termExpected = false;
    }

    void run() {
        for (auto& sp : scanSpans(src)) {
            // the scanner may cut a run of plain text anywhere (`<=>` can
            // arrive as `<`, `=`, `>`); operators are matched over the whole run
            if (!sp.cls[0] && !toks.empty() && !toks.back().cls[0]) {
                toks.back().text += sp.text;
                toks.back().end += sp.text.size();
                continue;
            }
            Tok t;
            t.start = toks.empty() ? 0 : toks.back().end;
            t.end = t.start + sp.text.size();
            t.text = sp.text;
            t.cls = sp.cls;
            t.line = toks.empty() ? 1 : toks.back().line + (int)std::count(toks.back().text.begin(), toks.back().text.end(), '\n');
            toks.push_back(std::move(t));
        }
        const Tok* prevSig = nullptr;   // the previous token that is not whitespace
        for (size_t i = 0; i < toks.size(); i++) {
            const Tok& t = toks[i];
            const Tok* prev = i ? &toks[i - 1] : nullptr;
            const Tok* nextSig = nullptr;
            for (size_t j = i + 1; j < toks.size(); j++) if (!isSpace(toks[j].text)) { nextSig = &toks[j]; break; }
            const std::string cls = t.cls;
            if (cls.empty()) plain(t, i);
            else if (cls == "k") { keyword(t, nextSig); termExpected = true; }
            else if (cls == "n" || cls == "nb") name(t, prev, nextSig, prevSig);
            else if (cls == "nv") { variable(t); termExpected = false; }
            else if (cls == "s") { string(t, prev); termExpected = false; }
            else if (cls == "sr") { regex(t); termExpected = false; }
            else if (cls[0] == 'm') { number(t); termExpected = false; }
            else if (cls == "cm" && startsWith(t.text, "=")) add(t.start, t.start + 1, t.line, "syntax", "Pod", find('L', "pod"));
            if (!isSpace(t.text)) prevSig = &toks[i];
        }
        std::stable_sort(r.refs.begin(), r.refs.end(), [](const Ref& a, const Ref& b) { return a.start < b.start; });
        // non-overlapping, first wins (a sub-span never overlaps its parent's mark)
        std::vector<Ref> kept;
        for (auto& ref : r.refs) if (kept.empty() || ref.start >= kept.back().end) kept.push_back(std::move(ref));
        r.refs = std::move(kept);
        r.unknown.assign(unknown.begin(), unknown.end());
    }
};

// Byte offset -> UTF-16 offset and 1-based code-point column, for every ref.
// One pass: the refs are sorted and do not overlap, so their boundaries come
// in order.
void positions(const std::string& src, std::vector<Ref>& refs) {
    size_t u16 = 0, k = 0;
    int col = 1;
    bool atEnd = false;
    for (size_t i = 0;;) {
        while (k < refs.size() && (atEnd ? refs[k].end : refs[k].start) <= i) {
            if (atEnd) { refs[k].uend = u16; k++; atEnd = false; }
            else { refs[k].ustart = u16; refs[k].col = col; atEnd = true; }
        }
        if (i >= src.size() || k >= refs.size()) break;
        unsigned char c = (unsigned char)src[i];
        size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        u16 += len == 4 ? 2 : 1;
        col = c == '\n' ? 1 : col + 1;
        i += len;
    }
}

} // namespace

Result analyze(const std::string& source) {
    Result r;
    Facts facts;
    try {
        Lexer lexer(source);
        lexer.tolerant_ = true;
        Parser parser(lexer.tokenize());
        parser.src_ = &source;
        Program prog = parser.parseProgram();
        Walker w(facts);
        w.program(prog);
    } catch (const ParseError& e) {
        r.parsed = false; r.errorLine = e.line; r.error = e.what();
        facts = Facts();
    } catch (const std::exception& e) {
        r.parsed = false; r.error = e.what();
        facts = Facts();
    }
    Linker l(source, facts, r.parsed, r);
    l.run();
    positions(source, r.refs);
    return r;
}

// ---------------------------------------------------------------------------
// Rendering.

namespace {

// One page and every line that links to it: the list the renderers print.
struct Page {
    std::string group, label, url, note;
    bool exact = true;
    int declLine = 0;
    std::vector<int> lines;
    size_t id = 0;   // its position in the list, shared by its refs (data-k)
};

const char* const kGroups[] = {"syntax", "type", "routine", "operator", "variable", "local"};
const char* groupTitle(const std::string& g) {
    if (g == "syntax") return "Syntax";
    if (g == "type") return "Types";
    if (g == "routine") return "Routines";
    if (g == "operator") return "Operators";
    if (g == "variable") return "Variables";
    return "Declared in this program";
}
int groupRank(const std::string& g) {
    for (int i = 0; i < 6; i++) if (g == kGroups[i]) return i;
    return 6;
}

std::string pageKey(const Ref& r) { return r.group + '\x1f' + r.label + '\x1f' + r.url; }

// The pages in group order, each group in order of first use; `idOf` maps a
// ref's pageKey to its page's id.
std::vector<Page> pages(const Result& r, std::map<std::string, size_t>* idOf = nullptr) {
    std::vector<Page> out;
    std::map<std::string, size_t> at;
    for (auto& ref : r.refs) {
        auto [it, fresh] = at.emplace(pageKey(ref), out.size());
        if (fresh) {
            Page p;
            p.group = ref.group; p.label = ref.label; p.url = ref.url; p.note = ref.note;
            p.exact = ref.exact; p.declLine = ref.declLine;
            out.push_back(std::move(p));
        }
        auto& lines = out[it->second].lines;
        if (lines.empty() || lines.back() != ref.line) lines.push_back(ref.line);
    }
    std::vector<size_t> order(out.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return groupRank(out[a].group) < groupRank(out[b].group); });
    std::vector<Page> sorted;
    std::vector<size_t> newId(out.size());
    for (size_t i = 0; i < order.size(); i++) { newId[order[i]] = i; sorted.push_back(std::move(out[order[i]])); sorted.back().id = i; }
    if (idOf) for (auto& [k, v] : at) (*idOf)[k] = newId[v];
    return sorted;
}

size_t width(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++;
    return n;
}
std::string pad(const std::string& s, size_t w) {
    size_t n = width(s);
    return n >= w ? s : s + std::string(w - n, ' ');
}
std::string lineList(const std::vector<int>& lines) {
    std::string out;
    size_t shown = std::min<size_t>(lines.size(), 5);
    for (size_t i = 0; i < shown; i++) out += (i ? ", " : "") + std::to_string(lines[i]);
    if (lines.size() > shown) out += " +" + std::to_string(lines.size() - shown);
    return out;
}

std::string summary(const Result& r, const std::vector<Page>& ps) {
    size_t external = 0;
    for (auto& p : ps) if (!p.url.empty()) external++;
    return std::to_string(external) + (external == 1 ? " page" : " pages") + " on docs.raku.org for " +
           std::to_string(r.refs.size()) + (r.refs.size() == 1 ? " construct" : " constructs") +
           " (index of " + kIndexDate + ")";
}
std::string parseNote(const Result& r) {
    if (r.parsed) return "";
    return "the program does not parse" + (r.errorLine ? " (line " + std::to_string(r.errorLine) + ": " + r.error + ")"
                                                        : r.error.empty() ? "" : " (" + r.error + ")") +
           ", so links come from the token stream alone";
}

void jsonStr(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += (char)c;
        }
    }
    out += '"';
}

void htmlEsc(std::string& out, const std::string& s) {
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
}

// The source cut into pieces that each lie in one highlighter span, in at most
// one ref, and on one line — what both annotated renderers walk. A piece that
// is a newline has neither span class nor ref.
struct Piece { size_t start, end; const char* cls; int ref; };
std::vector<Piece> pieces(const std::string& src, const std::vector<Ref>& refs) {
    std::vector<Piece> out;
    size_t pos = 0, ri = 0;
    for (auto& sp : scanSpans(src)) {
        size_t end = pos + sp.text.size();
        size_t i = pos;
        while (i < end) {
            while (ri < refs.size() && refs[ri].end <= i) ri++;
            int ref = -1;
            size_t stop = end;
            if (ri < refs.size()) {
                if (refs[ri].start <= i) { ref = (int)ri; stop = std::min(stop, refs[ri].end); }
                else stop = std::min(stop, refs[ri].start);
            }
            size_t nl = src.find('\n', i);
            if (nl == i) { out.push_back({i, i + 1, "", -1}); i++; continue; }
            if (nl != std::string::npos && nl < stop) stop = nl;
            out.push_back({i, stop, sp.cls, ref});
            i = stop;
        }
        pos = end;
    }
    return out;
}

const char* ansiColor(const char* cls) {   // Highlight.cpp's palette, so the two agree
    if (!cls[0]) return "";
    switch (cls[0]) {
        case 'k': return "\x1b[32m";
        case 'c': return "\x1b[3;36m";
        case 's': return cls[1] == 'r' ? "\x1b[35m" : "\x1b[31m";
        case 'm': return "\x1b[36m";
        case 'n': return cls[1] == 'v' ? "\x1b[34m" : cls[1] == 'b' ? "\x1b[32m" : "";
        default: return "";
    }
}

} // namespace

std::string renderText(const Result& r, const std::string& file) {
    auto ps = pages(r);
    std::string out = file + ": " + summary(r, ps) + "\n";
    if (!r.parsed) out += "note: " + parseNote(r) + "\n";
    size_t i = 0;
    while (i < ps.size()) {
        size_t j = i;
        size_t wl = 0, wn = 0;
        while (j < ps.size() && ps[j].group == ps[i].group) {
            wl = std::max(wl, std::min<size_t>(width(ps[j].label), 26));
            wn = std::max(wn, std::min<size_t>(width(lineList(ps[j].lines)), 18));
            j++;
        }
        out += "\n";
        out += groupTitle(ps[i].group);
        out += "\n";
        for (size_t k = i; k < j; k++) {
            const Page& p = ps[k];
            out += "  " + pad(p.label, wl) + "  " + pad(lineList(p.lines), wn) + "  ";
            if (p.url.empty()) out += "declared on line " + std::to_string(p.declLine);
            else { out += p.url; if (!p.note.empty()) out += "  (" + p.note + ")"; }
            out += "\n";
        }
        i = j;
    }
    if (!r.unknown.empty()) {
        out += "\nNo page on docs.raku.org: ";
        for (size_t k = 0; k < r.unknown.size(); k++) out += (k ? ", " : "") + r.unknown[k];
        out += "\n";
    }
    return out;
}

std::string renderJson(const Result& r, const std::string& file) {
    std::map<std::string, size_t> idOf;
    auto ps = pages(r, &idOf);
    std::string out = "{\n  \"file\": ";
    jsonStr(out, file);
    out += ",\n  \"index\": ";
    jsonStr(out, kIndexDate);
    out += ",\n  \"parsed\": ";
    out += r.parsed ? "true" : "false";
    if (!r.parsed) {
        out += ",\n  \"error\": {\"line\": " + std::to_string(r.errorLine) + ", \"message\": ";
        jsonStr(out, r.error);
        out += "}";
    }
    out += ",\n  \"refs\": [";
    for (size_t i = 0; i < r.refs.size(); i++) {
        const Ref& x = r.refs[i];
        out += i ? ",\n    " : "\n    ";
        out += "{\"line\": " + std::to_string(x.line) + ", \"col\": " + std::to_string(x.col) +
               ", \"start\": " + std::to_string(x.start) + ", \"end\": " + std::to_string(x.end) +
               ", \"from\": " + std::to_string(x.ustart) + ", \"to\": " + std::to_string(x.uend) +
               ", \"page\": " + std::to_string(idOf[pageKey(x)]) + ", \"text\": ";
        jsonStr(out, x.text);
        out += ", \"group\": "; jsonStr(out, x.group);
        out += ", \"label\": "; jsonStr(out, x.label);
        out += ", \"url\": "; jsonStr(out, x.url);
        out += ", \"note\": "; jsonStr(out, x.note);
        out += ", \"exact\": "; out += x.exact ? "true" : "false";
        if (x.declLine) out += ", \"declLine\": " + std::to_string(x.declLine);
        out += "}";
    }
    out += r.refs.empty() ? "],\n  \"pages\": [" : "\n  ],\n  \"pages\": [";
    for (size_t i = 0; i < ps.size(); i++) {
        const Page& p = ps[i];
        out += i ? ",\n    " : "\n    ";
        out += "{\"group\": "; jsonStr(out, p.group);
        out += ", \"label\": "; jsonStr(out, p.label);
        out += ", \"url\": "; jsonStr(out, p.url);
        out += ", \"note\": "; jsonStr(out, p.note);
        out += ", \"exact\": "; out += p.exact ? "true" : "false";
        if (p.declLine) out += ", \"declLine\": " + std::to_string(p.declLine);
        out += ", \"lines\": [";
        for (size_t k = 0; k < p.lines.size(); k++) out += (k ? ", " : "") + std::to_string(p.lines[k]);
        out += "]}";
    }
    out += ps.empty() ? "],\n  \"unknown\": [" : "\n  ],\n  \"unknown\": [";
    for (size_t i = 0; i < r.unknown.size(); i++) { if (i) out += ", "; jsonStr(out, r.unknown[i]); }
    out += "]\n}\n";
    return out;
}

std::string renderHtml(const Result& r, const std::string& source, const std::string& file) {
    std::map<std::string, size_t> idOf;
    auto ps = pages(r, &idOf);
    std::string out;
    out += "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>";
    htmlEsc(out, file);
    out += " · doc links</title>\n<style>\n"
R"CSS(:root {
  --bg: #ffffff; --fg: #1f2328; --muted: #59636e; --line: #d1d9e0; --panel: #f6f8fa;
  --accent: #0969da; --hot: #fff1a8; --k: #cf222e; --nb: #8250df; --nv: #0550ae;
  --s: #0a3069; --sr: #116329; --c: #6e7781; --m: #0550ae;
  color-scheme: light dark;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #0d1117; --fg: #e6edf3; --muted: #9198a1; --line: #3d444d; --panel: #151b23;
    --accent: #4493f8; --hot: #4d3d00; --k: #ff7b72; --nb: #d2a8ff; --nv: #79c0ff;
    --s: #a5d6ff; --sr: #7ee787; --c: #8b949e; --m: #79c0ff;
  }
}
:root[data-theme="dark"] {
  --bg: #0d1117; --fg: #e6edf3; --muted: #9198a1; --line: #3d444d; --panel: #151b23;
  --accent: #4493f8; --hot: #4d3d00; --k: #ff7b72; --nb: #d2a8ff; --nv: #79c0ff;
  --s: #a5d6ff; --sr: #7ee787; --c: #8b949e; --m: #79c0ff;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--fg);
       font: 15px/1.5 -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif; }
header { padding: 20px 24px 12px; border-bottom: 1px solid var(--line); }
h1 { margin: 0; font-size: 20px; font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; overflow-wrap: anywhere; }
@media (max-width: 600px) { header, footer, aside { padding-left: 16px; padding-right: 16px; } .ln { width: 3em; padding-right: .8em; } }
.meta { margin: 4px 0 0; color: var(--muted); font-size: 14px; }
.warn { margin: 10px 0 0; padding: 8px 12px; border-left: 3px solid var(--k); background: var(--panel); font-size: 14px; }
main { display: grid; grid-template-columns: minmax(0, 1fr) 400px; }
@media (max-width: 960px) { main { grid-template-columns: minmax(0, 1fr); } aside { border-left: 0; border-top: 1px solid var(--line); } }
.code { overflow-x: auto; }
pre { margin: 0; padding: 16px 0; font: 13.5px/1.6 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; tab-size: 4; }
.ln { display: inline-block; width: 4.5em; padding-right: 1.2em; text-align: right; color: var(--muted);
      user-select: none; -webkit-user-select: none; }
.ln:target { color: var(--accent); font-weight: 600; }
.k { color: var(--k); } .nb { color: var(--nb); } .nv { color: var(--nv); } .n { color: var(--fg); }
.s { color: var(--s); } .sr { color: var(--sr); } .c1, .cm { color: var(--c); font-style: italic; }
.mi, .mf, .mh, .mo, .mb { color: var(--m); }
a.dl { color: inherit; text-decoration: underline; text-decoration-color: color-mix(in srgb, var(--accent) 55%, transparent);
       text-underline-offset: 3px; text-decoration-thickness: 1px; border-radius: 2px; }
a.dl.inexact { text-decoration-style: dashed; }
a.dl.local { text-decoration-style: dotted; text-decoration-color: var(--muted); }
a.dl:hover, a.dl.hot, li.hot { background: var(--hot); }
aside { border-left: 1px solid var(--line); padding: 8px 20px 24px; font-size: 14px; }
h2 { font-size: 13px; text-transform: uppercase; letter-spacing: .06em; color: var(--muted); margin: 18px 0 6px; }
ul { list-style: none; margin: 0; padding: 0; }
li { padding: 4px 6px; border-radius: 4px; }
li > a { color: var(--accent); text-decoration: none; font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: 13.5px; }
li > a:hover { text-decoration: underline; }
.where { color: var(--muted); font-size: 12.5px; margin-left: 6px; }
.where a { color: var(--muted); }
.note { display: block; color: var(--muted); font-size: 12.5px; }
.unknown { margin-top: 18px; color: var(--muted); font-size: 13px; }
footer { padding: 12px 24px; border-top: 1px solid var(--line); color: var(--muted); font-size: 13px; }
footer a { color: var(--muted); }
)CSS"
           "</style>\n</head>\n<body>\n<header>\n<h1>";
    htmlEsc(out, file);
    out += "</h1>\n<p class=\"meta\">";
    htmlEsc(out, summary(r, ps));
    out += "</p>\n";
    if (!r.parsed) { out += "<p class=\"warn\">Note: "; htmlEsc(out, parseNote(r)); out += ".</p>\n"; }
    out += "</header>\n<main>\n<section class=\"code\"><pre>";

    int line = 1;
    out += "<span class=\"ln\" id=\"L1\">1</span>";
    int open = -1;   // the ref whose <a> is open
    auto closeA = [&]() { if (open >= 0) { out += "</a>"; open = -1; } };
    for (auto& pc : pieces(source, r.refs)) {
        if (source[pc.start] == '\n') {
            closeA();
            line++;
            out += "\n<span class=\"ln\" id=\"L" + std::to_string(line) + "\">" + std::to_string(line) + "</span>";
            continue;
        }
        if (pc.ref != open) {
            closeA();
            if (pc.ref >= 0) {
                const Ref& x = r.refs[pc.ref];
                out += "<a class=\"dl";
                if (x.group == "local") out += " local";
                else if (!x.exact) out += " inexact";
                out += "\" data-k=\"" + std::to_string(idOf[pageKey(x)]) + "\" href=\"";
                if (x.url.empty()) out += "#L" + std::to_string(x.declLine);
                else htmlEsc(out, x.url);
                out += "\" title=\"";
                htmlEsc(out, x.label + (x.note.empty() ? "" : " — " + x.note));
                out += x.url.empty() ? "\">" : "\" target=\"_blank\" rel=\"noopener\">";
                open = pc.ref;
            }
        }
        std::string text = source.substr(pc.start, pc.end - pc.start);
        if (pc.cls[0]) { out += "<span class=\""; out += pc.cls; out += "\">"; htmlEsc(out, text); out += "</span>"; }
        else htmlEsc(out, text);
    }
    closeA();
    out += "</pre></section>\n<aside>\n";

    std::string group;
    for (auto& p : ps) {
        if (p.group != group) {
            if (!group.empty()) out += "</ul>\n";
            group = p.group;
            out += "<h2>"; out += groupTitle(group); out += "</h2>\n<ul>\n";
        }
        out += "<li data-k=\"" + std::to_string(p.id) + "\"><a href=\"";
        if (p.url.empty()) out += "#L" + std::to_string(p.declLine);
        else htmlEsc(out, p.url);
        out += p.url.empty() ? "\">" : "\" target=\"_blank\" rel=\"noopener\">";
        htmlEsc(out, p.label);
        out += "</a><span class=\"where\">";
        out += p.lines.size() == 1 ? "line " : "lines ";
        for (size_t k = 0; k < p.lines.size(); k++) {
            if (k == 8) { out += " +" + std::to_string(p.lines.size() - 8); break; }
            out += (k ? ", " : "");
            out += "<a href=\"#L" + std::to_string(p.lines[k]) + "\">" + std::to_string(p.lines[k]) + "</a>";
        }
        out += "</span>";
        if (!p.note.empty()) { out += "<span class=\"note\">"; htmlEsc(out, p.note); out += "</span>"; }
        out += "</li>\n";
    }
    if (!group.empty()) out += "</ul>\n";
    if (!r.unknown.empty()) {
        out += "<p class=\"unknown\">No page on docs.raku.org: ";
        for (size_t k = 0; k < r.unknown.size(); k++) { if (k) out += ", "; out += "<code>"; htmlEsc(out, r.unknown[k]); out += "</code>"; }
        out += "</p>\n";
    }
    out += "</aside>\n</main>\n<footer>Made by <code>rakupp --doc-links</code>. Links point at "
           "<a href=\"https://docs.raku.org\">docs.raku.org</a>, which documents Rakudo.</footer>\n"
           "<script>\n"
           "// Point at a construct or a list entry: every occurrence of the same page lights up.\n"
           "document.addEventListener('mouseover', e => {\n"
           "  const el = e.target.closest('[data-k]');\n"
           "  document.querySelectorAll('.hot').forEach(x => x.classList.remove('hot'));\n"
           "  if (el) document.querySelectorAll('[data-k=\"' + el.dataset.k + '\"]').forEach(x => x.classList.add('hot'));\n"
           "});\n"
           "</script>\n</body>\n</html>\n";
    return out;
}

std::string renderAnsi(const Result& r, const std::string& source, const std::string& file) {
    std::string out;
    int line = 1;
    char num[16];
    std::snprintf(num, sizeof num, "%5d  ", line);
    out += std::string("\x1b[90m") + num + "\x1b[0m";
    int open = -1;
    auto closeLink = [&]() { if (open >= 0) { out += "\x1b]8;;\x1b\\"; open = -1; } };
    for (auto& pc : pieces(source, r.refs)) {
        if (source[pc.start] == '\n') {
            closeLink();
            line++;
            std::snprintf(num, sizeof num, "%5d  ", line);
            out += std::string("\n\x1b[90m") + num + "\x1b[0m";
            continue;
        }
        if (pc.ref != open) {
            closeLink();
            if (pc.ref >= 0 && !r.refs[pc.ref].url.empty()) {
                out += "\x1b]8;;" + r.refs[pc.ref].url + "\x1b\\";
                open = pc.ref;
            }
        }
        const char* color = ansiColor(pc.cls);
        bool linked = pc.ref >= 0 && !r.refs[pc.ref].url.empty();
        if (linked) out += "\x1b[4m";
        out += color;
        out += source.substr(pc.start, pc.end - pc.start);
        if (linked || color[0]) out += "\x1b[0m";
    }
    closeLink();
    out += "\n\n";
    out += renderText(r, file);
    return out;
}

} // namespace doclinks

int runDocLinks(const std::string& source, const std::string& file, const std::string& fmt) {
    doclinks::Result r = doclinks::analyze(source);
    if (fmt == "json") std::cout << doclinks::renderJson(r, file);
    else if (fmt == "html") std::cout << doclinks::renderHtml(r, source, file);
    else if (fmt == "ansi") std::cout << doclinks::renderAnsi(r, source, file);
    else std::cout << doclinks::renderText(r, file);
    return 0;
}

} // namespace rakupp
