// A binary's compile-time values (#190) — see CompileTime.h.
#include "CompileTime.h"
#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"
#include "Runtime.h"
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>

namespace rakupp {

void collectMentionedE(const Expr* e, std::set<std::string>& out);   // Interpreter.cpp
void collectMentionedS(const Stmt* s, std::set<std::string>& out);

namespace {

bool isCtPhaser(const std::string& p) { return p == "BEGIN" || p == "CHECK"; }

// `constant X = …` — `my`/`our constant` and `:=` too
Assign* constDecl(Expr* e) {
    if (!e || e->kind != NK::Assign) return nullptr;
    auto* a = static_cast<Assign*>(e);
    if (!a->target || a->target->kind != NK::VarExpr || !a->value || (a->op != "=" && a->op != ":=")) return nullptr;
    auto* v = static_cast<VarExpr*>(a->target.get());
    return v->declare && v->d().declScope == "constant" ? a : nullptr;
}

// `BEGIN EXPR`, and `BEGIN { … }` in an expression (a `do` around a phaser block)
bool isBeginValue(const Expr* e) {
    if (!e || e->kind != NK::Unary) return false;
    auto* u = static_cast<const Unary*>(e);
    if (isCtPhaser(u->op)) return true;
    return u->op == "do" && u->operand && u->operand->kind == NK::BlockExpr &&
           isCtPhaser(static_cast<const BlockExpr*>(u->operand.get())->phaser);
}

// An initializer that needs nothing to compute: literals, and operators over
// them. The binary computes it as cheaply as it would read it back, so such a
// constant is left as written (and a program with only these is left alone).
bool plainLiteral(const Expr* e) {
    if (!e) return true;
    auto all = [](const std::vector<ExprPtr>& v) {
        for (auto& x : v) if (!plainLiteral(x.get())) return false;
        return true;
    };
    switch (e->kind) {
        case NK::IntLit: case NK::NumLit: case NK::StrLit: case NK::BoolLit: case NK::AllomorphLit:
            return true;
        case NK::Unary: { auto* u = static_cast<const Unary*>(e);
            return (u->op == "-" || u->op == "+" || u->op == "~" || u->op == "!") && plainLiteral(u->operand.get()); }
        case NK::Binary: { auto* b = static_cast<const Binary*>(e);
            return plainLiteral(b->lhs.get()) && plainLiteral(b->rhs.get()); }
        case NK::Range: { auto* r = static_cast<const RangeExpr*>(e);
            return plainLiteral(r->from.get()) && plainLiteral(r->to.get()); }
        case NK::Pair: { auto* p = static_cast<const PairExpr*>(e);
            return plainLiteral(p->keyExpr.get()) && plainLiteral(p->value.get()); }
        case NK::ListExpr: return all(static_cast<const ListExpr*>(e)->items);
        case NK::ArrayLit: return all(static_cast<const ArrayLit*>(e)->items);
        default: return false;
    }
}

// The expressions directly under E, but not a block's body
template <class F> void eachChild(Expr* e, F&& f) {
    auto all = [&](std::vector<ExprPtr>& v) { for (auto& x : v) f(x); };
    switch (e->kind) {
        case NK::Assign: { auto* a = static_cast<Assign*>(e); f(a->target); f(a->value); return; }
        case NK::Binary: { auto* b = static_cast<Binary*>(e); f(b->lhs); f(b->rhs); return; }
        case NK::Unary: f(static_cast<Unary*>(e)->operand); return;
        case NK::Call: { auto* c = static_cast<Call*>(e); f(c->callee); all(c->args); return; }
        case NK::MethodCall: { auto* m = static_cast<MethodCall*>(e); f(m->inv); all(m->args); return; }
        case NK::Ternary: { auto* t = static_cast<Ternary*>(e); f(t->cond); f(t->then); f(t->els); return; }
        case NK::Index: { auto* i = static_cast<Index*>(e); f(i->base); f(i->index); return; }
        case NK::Range: { auto* r = static_cast<RangeExpr*>(e); f(r->from); f(r->to); return; }
        case NK::Pair: { auto* p = static_cast<PairExpr*>(e); f(p->keyExpr); f(p->value); return; }
        case NK::ListExpr: all(static_cast<ListExpr*>(e)->items); return;
        case NK::ArrayLit: all(static_cast<ArrayLit*>(e)->items); return;
        case NK::HashLit: all(static_cast<HashLit*>(e)->items); return;
        case NK::InterpStr: all(static_cast<InterpStr*>(e)->parts); return;
        case NK::ChainExpr: all(static_cast<ChainExpr*>(e)->operands); return;
        default: return;
    }
}

// Every compile-time site of a program, in one order: a constant's initializer
// ('C') and a BEGIN or CHECK value ('B'), at any depth this reaches; and the
// BEGIN and CHECK blocks, at the top level and below it. A site's own code is
// not walked into, nor is a class body (it runs when the binary does).
struct CtSite {
    char kind;
    int line;
    ExprPtr* slot;          // what a value replaces
    Expr* node;             // the Assign or the Unary: what the build-time run keys it by
    const void* staticKey;  // a nested `BEGIN { … }` value's block, as runStaticPhasers keys it
    bool plain;             // a constant whose initializer is a literal already
};
struct CtWalk {
    std::vector<CtSite> sites;
    std::vector<StmtPtr*> topBegins;
    std::vector<Block*> nestedBegins;
    int line = 0;                    // the statement's, for a node that has none

    void program(Program& p) { for (auto& s : p.stmts) stmt(s, true); }
    void body(std::vector<StmtPtr>& v) { for (auto& s : v) stmt(s, false); }
    void block(std::unique_ptr<Block>& b) { if (b) body(b->stmts); }
    void expr(ExprPtr& ep) {
        Expr* e = ep.get();
        if (!e) return;
        const int at = e->line ? e->line : line;
        if (Assign* a = constDecl(e)) { sites.push_back({'C', at, &a->value, a, nullptr, plainLiteral(a->value.get())}); return; }
        if (isBeginValue(e)) {
            auto* u = static_cast<Unary*>(e);
            sites.push_back({'B', at, &ep, u, u->op == "do" ? u->operand.get() : nullptr, false});
            return;
        }
        if (e->kind == NK::BlockExpr) {
            auto* be = static_cast<BlockExpr*>(e);
            if (!be->phaser.empty()) return;   // an INIT: run time
            for (auto& p : be->params) expr(p.defaultVal);
            body(be->body);
            return;
        }
        eachChild(e, [&](ExprPtr& c) { expr(c); });
    }
    void stmt(StmtPtr& sp, bool top) {
        Stmt* s = sp.get();
        if (!s) return;
        if (s->line) line = s->line;
        switch (s->kind) {
            case NK::Block: { auto* b = static_cast<Block*>(s);
                if (isCtPhaser(b->phaser)) { if (top) topBegins.push_back(&sp); else nestedBegins.push_back(b); return; }
                if (b->phaser.empty() && !b->isCatch) body(b->stmts);
                return; }
            case NK::ExprStmt: expr(static_cast<ExprStmt*>(s)->e); return;
            case NK::ReturnStmt: expr(static_cast<ReturnStmt*>(s)->value); return;
            case NK::IfStmt: { auto* f = static_cast<IfStmt*>(s);
                for (auto& br : f->branches) { expr(br.first); block(br.second); }
                block(f->elseBlock); return; }
            case NK::WhileStmt: { auto* w = static_cast<WhileStmt*>(s); expr(w->cond); block(w->body); return; }
            case NK::RepeatStmt: { auto* r = static_cast<RepeatStmt*>(s); block(r->body); expr(r->cond); return; }
            case NK::ForStmt: { auto* f = static_cast<ForStmt*>(s); expr(f->list); block(f->body); return; }
            case NK::LoopStmt: { auto* l = static_cast<LoopStmt*>(s);
                expr(l->init); expr(l->cond); expr(l->incr); block(l->body); return; }
            case NK::GivenStmt: { auto* g = static_cast<GivenStmt*>(s); expr(g->topic); block(g->body); block(g->elseBody); return; }
            case NK::WhenStmt: { auto* w = static_cast<WhenStmt*>(s); expr(w->cond); block(w->body); return; }
            case NK::SubDecl: { auto* d = static_cast<SubDecl*>(s);
                for (auto& p : d->params) expr(p.defaultVal);
                body(d->body); return; }
            default: return;
        }
    }
};

// The unit's own variables: what a top-level `my`/`our` declares, and a
// top-level `BEGIN my $x = …` (a `&` one too: set, it holds code, and keeps
// the BEGIN blocks running)
bool ctVarName(const std::string& n) {
    if (n.size() < 2 || !std::strchr("$@%&", n[0])) return false;
    const unsigned char c = (unsigned char)n[1];
    return c >= 0x80 || std::isalpha(c) || c == '_';   // no twigil, no $/, $_ is not declared
}
std::vector<const VarExpr*> ctUnitVars(const Program& prog) {
    std::vector<const VarExpr*> out;
    auto one = [&](const Expr* x) {
        if (!x || x->kind != NK::VarExpr) return;
        auto* v = static_cast<const VarExpr*>(x);
        if (v->declare && (v->d().declScope == "my" || v->d().declScope == "our") && ctVarName(v->name)) out.push_back(v);
    };
    auto decl = [&](const Expr* e) {
        if (e && e->kind == NK::Assign) e = static_cast<const Assign*>(e)->target.get();
        if (e && e->kind == NK::ListExpr) for (auto& it : static_cast<const ListExpr*>(e)->items) one(it.get());
        else one(e);
    };
    for (auto& s : prog.stmts) {
        if (!s) continue;
        if (s->kind == NK::ExprStmt) decl(static_cast<const ExprStmt*>(s.get())->e.get());
        else if (s->kind == NK::Block) {
            auto* b = static_cast<const Block*>(s.get());
            if (isCtPhaser(b->phaser) && b->stmtForm)
                for (auto& st : b->stmts)
                    if (st && st->kind == NK::ExprStmt) decl(static_cast<const ExprStmt*>(st.get())->e.get());
        }
    }
    return out;
}

// Raku itself decides what a value is and whether its `.raku` reads back: the
// pass's interpreter compiles this once. Answers "=LITERAL", "!TYPE" (not plain
// data, or it does not read back), or for a variable "-" (still as declared).
const char* const kCtCheck = R"RAKU(use MONKEY-SEE-NO-EVAL;
sub ($mode, Mu \v, $sigil, $type) {
    if $mode eq 'var' {
        return '-' unless $sigil eq '$' ?? (v.DEFINITE || v.^name ne ($type || 'Any'))
                       !! $sigil eq '&' ?? v.DEFINITE !! v.elems;
    }
    my @data = Str, Int, Num, Rat, FatRat, Complex, Version, Date, DateTime, Instant, Duration, Blob;
    my sub plain(Mu \x, Int $d) {
        return False if $d > 64;
        return True unless x.DEFINITE;
        return False if x ~~ Junction;
        return True if x ~~ Enumeration;
        return True if @data.grep({ x ~~ $_ });
        return plain(x.key, $d + 1) && plain(x.value, $d + 1) if x ~~ Pair;
        return plain(x.min, $d + 1) && plain(x.max, $d + 1) if x ~~ Range;
        if x ~~ Map || x ~~ Setty || x ~~ Baggy || x ~~ Mixy {
            for x.pairs -> $p { return False unless plain($p.key, $d + 1) && plain($p.value, $d + 1) }
            return True;
        }
        if x ~~ List || x ~~ Seq {
            return False if x.is-lazy;
            for x.list -> \e { return False unless plain(e, $d + 1) }
            return True;
        }
        False
    }
    my sub lit(Mu \x) {
        my $r = x.raku;
        my \back = EVAL $r;
        back.^name eq x.^name && back eqv x ?? $r !! Nil
    }
    my $lit = try { plain(v, 0) ?? lit(v) !! Nil };
    $lit ~~ Str ?? '=' ~ $lit !! '!' ~ v.^name
}
)RAKU";

struct CtCheck {
    Value fn;
    bool tried = false;
};
CtCheck g_ctCheck;   // the one build-time pass's

std::string ctLiteral(Interpreter& I, const Value& v, const char* mode, const std::string& sigil = "$",
                      const std::string& type = "") {
    try {
        if (!g_ctCheck.tried) { g_ctCheck.tried = true; g_ctCheck.fn = I.evalString(kCtCheck); }
        ValueList args;
        args.push_back(Value::str(mode));
        args.push_back(v);
        args.push_back(Value::str(sigil));
        args.push_back(Value::str(type));
        std::string r = I.callCallable(g_ctCheck.fn, std::move(args)).toStr();
        if (!r.empty() && std::strchr("=!-", r[0])) return r;
    } catch (...) {}
    return "!";
}
// …and what a note calls the type a "!" answer names
std::string aType(const std::string& r) { return r.size() > 1 ? "a " + r.substr(1) : "a value"; }

// The pass's own parse: as rakuppRunOn parses, bar the localized-keyword
// rewrite (a localized program is not one this pass can read)
bool ctParse(const std::string& src, const std::string& srcName, const std::vector<std::string>& libPaths,
             Program& out, std::string& finish) {
    try {
        Lexer lexer(src);
        lexer.tolerant_ = true;
        auto tokens = lexer.tokenize();
        finish = lexer.finishData();
        Parser parser(std::move(tokens));
        parser.declPod_ = lexer.declPod_;
        parser.leadPod_ = lexer.leadPod_;
        parser.libPaths_ = effectiveSearchPath(libPaths);
        parser.srcFile_ = srcName;
        parser.src_ = &src;
        out = parser.parseProgram();
        return true;
    } catch (...) {
        return false;
    }
}

// A value as a node: parsed as a constant's initializer is, so `{:a(1)}` is a
// hash and not a block
ExprPtr parseLiteral(const std::string& lit, int line) {
    try {
        std::string src = "constant ct-value = " + lit + ";";
        Lexer lexer(src);
        Parser parser(lexer.tokenize());
        Program p = parser.parseProgram();
        if (p.stmts.size() != 1 || !p.stmts[0] || p.stmts[0]->kind != NK::ExprStmt) return nullptr;
        Assign* a = constDecl(static_cast<ExprStmt*>(p.stmts[0].get())->e.get());
        if (!a) return nullptr;
        ExprPtr e = std::move(a->value);
        e->line = line;
        return e;
    } catch (...) {
        return nullptr;
    }
}

// A dropped `BEGIN my $x = …` still declares $x where it stood
bool declares(const Expr* e) {
    if (!e) return false;
    if (e->kind == NK::VarExpr) return static_cast<const VarExpr*>(e)->declare;
    if (e->kind != NK::ListExpr) return false;
    for (auto& it : static_cast<const ListExpr*>(e)->items) if (!declares(it.get())) return false;
    return !static_cast<const ListExpr*>(e)->items.empty();
}
StmtPtr withoutBegin(Block* b) {
    if (b->stmtForm && b->stmts.size() == 1 && b->stmts[0] && b->stmts[0]->kind == NK::ExprStmt) {
        auto* es = static_cast<ExprStmt*>(b->stmts[0].get());
        ExprPtr target;
        if (es->e && es->e->kind == NK::Assign && declares(static_cast<Assign*>(es->e.get())->target.get()))
            target = std::move(static_cast<Assign*>(es->e.get())->target);
        else if (declares(es->e.get()))
            target = std::move(es->e);
        if (target) {
            auto st = std::make_unique<ExprStmt>();
            st->line = b->line;
            st->e = std::move(target);
            return st;
        }
    }
    auto st = std::make_unique<EmptyStmt>();
    st->line = b->line;
    return st;
}

// A bundled binary's table, applied by rakuppRunOn (g_ctApply)
CtTable g_runTable;
void applyRunTable(Program& p) { applyCompileTimeValues(p, g_runTable); }

bool decodeCtTable(const std::string& s, CtTable& t) {
    std::istringstream in(s);
    std::string magic;
    int drop = 0;
    size_t nSubs = 0, nRestore = 0;
    if (!(in >> magic >> t.nSites >> t.nTopBegins >> drop >> nSubs >> nRestore) || magic != "CT1") return false;
    t.dropBegins = drop != 0;
    auto bytes = [&](size_t n, std::string& out) {
        out.assign(n, '\0');
        return n == 0 || (bool)in.read(&out[0], (std::streamsize)n);
    };
    for (size_t i = 0; i < nSubs; i++) {
        CtEntry e;
        size_t len = 0;
        if (!(in >> e.site >> e.kind >> e.line >> len) || in.get() != '\n' || !bytes(len, e.lit)) return false;
        t.subs.push_back(std::move(e));
    }
    for (size_t i = 0; i < nRestore; i++) {
        size_t ln = 0, ll = 0;
        std::string n, l;
        if (!(in >> ln >> ll) || in.get() != '\n' || !bytes(ln, n) || !bytes(ll, l)) return false;
        t.restore.emplace_back(std::move(n), std::move(l));
    }
    return true;
}

// Does a top-level BEGIN block do no more than compute values into the
// program's variables, which is all the snapshot puts back? It may not reach
// anything else: EVAL, a symbol named at run time or through a package
// (`::($n)`, `$Foo::x`, `Foo::f()`, a stash's `.WHO`), a dynamic variable it
// changes (`%*ENV<X> = 1`, `@*INC.push`), the metamodel (`.^add_method`), a
// routine's wrapper, `require`, a type it declares, or a routine neither the
// program nor the setting declares (an imported one may keep state of its
// own). The program's own routines it calls are held to the same, through
// their bodies. `why` says what it found first.
struct CtBeginScan {
    Interpreter& I;
    std::map<std::string, std::vector<const SubDecl*>> subs;   // the program's, and the block's own
    std::set<const SubDecl*> seen;
    std::string why;
    explicit CtBeginScan(Interpreter& interp) : I(interp) {}
    void fail(const std::string& w) { if (why.empty()) why = w; }
    void declare(const std::vector<StmtPtr>& v) {
        for (auto& st : v)
            if (st && st->kind == NK::SubDecl && !static_cast<const SubDecl*>(st.get())->name.empty())
                subs[static_cast<const SubDecl*>(st.get())->name].push_back(static_cast<const SubDecl*>(st.get()));
    }
    void body(const std::vector<StmtPtr>& v) { for (auto& st : v) stmt(st.get()); }
    void block(const Block* b) { if (b) body(b->stmts); }
    void routine(const SubDecl* d) {
        if (!seen.insert(d).second) return;
        for (auto& p : d->params) expr(p.defaultVal.get());
        body(d->body);
    }
    // the variable an Index or method chain starts from: a dynamic one?
    static bool dynamicRoot(const Expr* e) {
        while (e && (e->kind == NK::Index || e->kind == NK::MethodCall))
            e = e->kind == NK::Index ? static_cast<const Index*>(e)->base.get() : static_cast<const MethodCall*>(e)->inv.get();
        if (!e || e->kind != NK::VarExpr) return false;
        const std::string& n = static_cast<const VarExpr*>(e)->name;
        return n.size() > 1 && n[1] == '*';
    }
    void stmt(const Stmt* st) {
        if (!st || !why.empty()) return;
        switch (st->kind) {
            case NK::ExprStmt: expr(static_cast<const ExprStmt*>(st)->e.get()); return;
            case NK::ReturnStmt: expr(static_cast<const ReturnStmt*>(st)->value.get()); return;
            case NK::Block: block(static_cast<const Block*>(st)); return;
            case NK::IfStmt: { auto* f = static_cast<const IfStmt*>(st);
                for (auto& br : f->branches) { expr(br.first.get()); block(br.second.get()); }
                block(f->elseBlock.get()); return; }
            case NK::WhileStmt: { auto* w = static_cast<const WhileStmt*>(st); expr(w->cond.get()); block(w->body.get()); return; }
            case NK::RepeatStmt: { auto* r = static_cast<const RepeatStmt*>(st); block(r->body.get()); expr(r->cond.get()); return; }
            case NK::ForStmt: { auto* f = static_cast<const ForStmt*>(st); expr(f->list.get()); block(f->body.get()); return; }
            case NK::LoopStmt: { auto* l = static_cast<const LoopStmt*>(st);
                expr(l->init.get()); expr(l->cond.get()); expr(l->incr.get()); block(l->body.get()); return; }
            case NK::GivenStmt: { auto* g = static_cast<const GivenStmt*>(st);
                expr(g->topic.get()); block(g->body.get()); block(g->elseBody.get()); return; }
            case NK::WhenStmt: { auto* w = static_cast<const WhenStmt*>(st); expr(w->cond.get()); block(w->body.get()); return; }
            case NK::SubDecl: routine(static_cast<const SubDecl*>(st)); return;
            case NK::UseStmt: if (static_cast<const UseStmt*>(st)->isRequire) fail("loads a module at run time (require)"); return;
            case NK::ClassDecl: case NK::EnumDecl: case NK::SubsetDecl: fail("declares a type"); return;
            default: return;
        }
    }
    void expr(const Expr* e) {
        if (!e || !why.empty()) return;
        static const std::set<std::string> metaReads = {"name", "ver", "auth", "api", "methods", "attributes", "can",
            "lookup", "find_method", "parents", "roles", "mro", "does", "isa", "shortname", "enum_values", "enum_value_list"};
        static const std::set<std::string> mutators = {"push", "append", "unshift", "prepend", "splice", "pop", "shift",
            "ASSIGN-KEY", "BIND-KEY", "DELETE-KEY", "ASSIGN-POS", "BIND-POS", "DELETE-POS", "STORE", "chdir"};
        switch (e->kind) {
            case NK::SymbolicRef: fail("names a symbol as it runs (::(…))"); return;
            case NK::VarExpr: {
                const std::string& n = static_cast<const VarExpr*>(e)->name;
                if (n.find("::") != std::string::npos) fail("names " + n + " through its package");
                return; }
            case NK::NameTerm: {
                const std::string& n = static_cast<const NameTerm*>(e)->name;
                if (n.size() > 2 && n.compare(n.size() - 2, 2, "::") == 0) fail("reaches the stash " + n);
                return; }
            case NK::BlockExpr: { auto* be = static_cast<const BlockExpr*>(e);
                for (auto& p : be->params) expr(p.defaultVal.get());
                body(be->body); return; }
            case NK::Assign: { auto* a = static_cast<const Assign*>(e);
                if (dynamicRoot(a->target.get())) fail("changes a dynamic variable");
                break; }
            case NK::Call: { auto* c = static_cast<const Call*>(e);
                if (c->callee || c->name.empty()) break;
                const std::string& n = c->name;
                if (n == "EVAL" || n == "EVALFILE") fail("calls " + n);
                else if (n.find("::") != std::string::npos) fail("calls " + n + " through its package");
                else if (auto it = subs.find(n); it != subs.end()) for (auto* d : it->second) routine(d);
                else if (!I.builtinRef(n) && !isKnownTypeName(n) && !I.classes_.count(n))
                    fail("calls " + n + ", which the program does not declare");
                break; }
            case NK::MethodCall: { auto* m = static_cast<const MethodCall*>(e);
                const std::string& n = m->method;
                if (m->meta && !metaReads.count(n)) fail("calls ." + std::string("^") + n);
                else if (n == "WHO") fail("reaches a stash (.WHO)");
                else if (n == "EVAL" || n == "EVALFILE" || n == "wrap" || n == "unwrap") fail("calls ." + n);
                else if (mutators.count(n) && dynamicRoot(m->inv.get())) fail("changes a dynamic variable");
                expr(m->methodExpr.get());
                break; }
            default: break;
        }
        eachChild(const_cast<Expr*>(e), [&](ExprPtr& c) { expr(c.get()); });
    }
};

// what a build-time note calls a site
std::string siteName(const CtSite& s) {
    if (s.kind == 'C')
        return "constant " + static_cast<VarExpr*>(static_cast<Assign*>(s.node)->target.get())->name +
               " (line " + std::to_string(s.line) + ")";
    return "the " + static_cast<Unary*>(s.node)->op + " value at line " + std::to_string(s.line);
}

} // namespace

// The build-time pass's mainline (run() asks of each statement): what is
// compile time runs, and nothing else. `use`, `need` and `import` load; a
// declaration declares; a constant is computed (unless the phasers already
// bound it) and kept; and a BEGIN value in any other statement is computed in
// place and kept, the statement itself left for the binary.
static void ctBeginValues(Interpreter& I, Expr* e, std::unordered_map<const void*, Value>& out) {
    if (!e || e->kind == NK::BlockExpr || constDecl(e)) return;   // a closure runs later; a constant is its own site
    if (isBeginValue(e)) { out[e] = I.eval(e); return; }
    eachChild(e, [&](ExprPtr& c) { ctBeginValues(I, c.get(), out); });
}
bool ctMainlineStmt(Interpreter& I, Stmt* s, bool boundEarly) {
    switch (s->kind) {
        case NK::UseStmt: return !static_cast<UseStmt*>(s)->isRequire;   // `require` is run time
        case NK::SubDecl: case NK::ClassDecl: case NK::EnumDecl: case NK::SubsetDecl:
        case NK::NamedRegexDecl: case NK::EmptyStmt:
            return true;
        case NK::ExprStmt: break;
        default: return false;
    }
    Expr* e = static_cast<ExprStmt*>(s)->e.get();
    if (Assign* a = constDecl(e)) {
        if (!boundEarly) I.exec(s, /*sink=*/true);
        if (Value* v = I.tctx().cur->find(static_cast<VarExpr*>(a->target.get())->name)) (*I.ctValues_)[a] = *v;
        return false;
    }
    ctBeginValues(I, e, *I.ctValues_);
    return false;
}
// …and, once BEGIN and CHECK have run, the unit variables they set, as Raku:
// taken now, before a class body or anything else the binary runs again does
void ctAfterCheck(Interpreter& I, Program& prog) {
    bool any = false;
    for (auto& s : prog.stmts)
        if (s && s->kind == NK::Block && isCtPhaser(static_cast<Block*>(s.get())->phaser)) any = true;
    if (!any) return;
    for (const VarExpr* v : ctUnitVars(prog))
        if (Value* p = I.tctx().cur->find(v->name))
            (*I.ctValues_)[v] = Value::str(ctLiteral(I, *p, "var", v->name.substr(0, 1), v->d().declType));
}

CtTable computeCompileTimeValues(const std::string& src, const std::string& srcName,
                                 const std::vector<std::string>& libPaths) {
    CtTable t;
    std::unique_ptr<Interpreter> owner;   // made first, so it outlives the tree, as in rakuppRunOn
    std::unordered_map<const void*, Value> vals;
    Program prog;
    std::string finish;
    if (!ctParse(src, srcName, libPaths, prog, finish)) return t;   // the binary parses it, and says why not
    CtWalk w;
    w.program(prog);
    bool worth = !w.topBegins.empty();
    for (auto& s : w.sites) if (!s.plain) worth = true;
    if (!worth) return t;

    owner = std::make_unique<Interpreter>();
    Interpreter& interp = *owner;
    struct CheckReset { ~CheckReset() { g_ctCheck = CtCheck{}; } } checkReset;   // its closure is interp's
    rakuppPrepareInterp(interp, src, srcName, libPaths, finish);
    interp.ctValues_ = &vals;
    int rc = 1;
    try { rc = interp.run(prog); } catch (...) { rc = 1; }
    std::cout.flush();
    if (rc != 0 || !vals.count(nullptr)) {
        std::cerr << "note: the compile-time code of " << srcName << " failed while building; its constants "
                     "and BEGIN blocks run when the binary starts instead.\n";
        return t;
    }
    t.nSites = (int)w.sites.size();
    t.nTopBegins = (int)w.topBegins.size();
    auto cannot = [&](const std::string& what, const std::string& r) {
        std::cerr << "note: " << what << " is " << aType(r)
                  << ", which a binary cannot carry; it is computed when the binary starts.\n";
    };
    // A site that reads one of the unit's variables is left to the binary:
    // `my $n = 5; constant C = $n` is 5 in Rakudo (C binds $n's container)
    // and here, where the pass never ran the `my`; and one a BEGIN set is
    // put back before the site runs
    std::set<std::string> unitVars;
    for (const VarExpr* v : ctUnitVars(prog)) unitVars.insert(v->name);
    auto readsUnitVar = [&](const CtSite& s) {
        std::set<std::string> m;
        collectMentionedE(s.kind == 'C' ? static_cast<Assign*>(s.node)->value.get() : s.node, m);
        for (auto& n : m) if (unitVars.count(n)) return true;
        return false;
    };
    for (size_t i = 0; i < w.sites.size(); i++) {
        const CtSite& s = w.sites[i];
        if (s.plain || readsUnitVar(s)) continue;
        const Value* v = nullptr;
        if (auto it = vals.find(s.node); it != vals.end()) v = &it->second;
        else if (s.staticKey)
            if (auto st = interp.staticPhaserVal_.find(s.staticKey); st != interp.staticPhaserVal_.end()) v = &st->second;
        if (!v) continue;   // not computed while building (a constant in a routine): the binary computes it, as before
        std::string r = ctLiteral(interp, *v, "val");
        if (r[0] == '=') t.subs.push_back({(int)i, s.kind, s.line, r.substr(1)});
        else cannot(siteName(s), r);
    }
    // The top-level BEGIN and CHECK blocks go, if what they left behind can
    // be put back. One inside a routine or block stays, and runs again when
    // the binary starts: if it touches the unit's variables, so do they all,
    // or it would run a second time on what they restored
    if (!w.topBegins.empty()) {
        bool ok = true;
        for (StmtPtr* sp : w.topBegins) {
            CtBeginScan scan(interp);
            scan.declare(prog.stmts);
            auto* b = static_cast<Block*>(sp->get());
            scan.declare(b->stmts);
            scan.block(b);
            if (scan.why.empty()) continue;
            ok = false;
            std::cerr << "note: the " << b->phaser << " block at line " << b->line << " " << scan.why
                      << ", which a binary cannot carry; the BEGIN blocks run when the binary starts.\n";
            break;
        }
        for (Block* b : w.nestedBegins) {
            if (!ok) break;
            std::set<std::string> m;
            collectMentionedS(b, m);
            bool touches = false;
            for (auto& n : m) if (unitVars.count(n)) touches = true;
            if (!touches) continue;
            ok = false;
            std::cerr << "note: the BEGIN block at line " << b->line << " is inside a routine or block and uses the "
                         "program's variables; it runs again when the binary starts, so the BEGIN blocks all do.\n";
            break;
        }
        for (const VarExpr* v : ctUnitVars(prog)) {
            auto it = vals.find(v);
            if (it == vals.end() || !ok) continue;
            std::string r = it->second.toStr();
            if (r[0] == '=') t.restore.emplace_back(v->name, r.substr(1));
            else if (r[0] == '!') {
                ok = false;
                std::cerr << "note: " << v->name << " holds " << aType(r) << " once BEGIN has run, which a "
                             "binary cannot carry; the BEGIN blocks run when the binary starts.\n";
            }
        }
        t.dropBegins = ok;
        if (!ok) t.restore.clear();
    }
    return t;
}

void applyCompileTimeValues(Program& prog, const CtTable& t) {
    if (t.empty()) return;
    CtWalk w;
    w.program(prog);
    if ((int)w.sites.size() != t.nSites || (int)w.topBegins.size() != t.nTopBegins) return;   // another tree
    for (auto& e : t.subs)
        if (e.site < 0 || e.site >= t.nSites || w.sites[e.site].kind != e.kind || w.sites[e.site].line != e.line) return;
    std::vector<ExprPtr> lits;
    for (auto& e : t.subs) {
        lits.push_back(parseLiteral(e.lit, e.line));
        if (!lits.back()) return;
    }
    for (size_t i = 0; i < t.subs.size(); i++) *w.sites[t.subs[i].site].slot = std::move(lits[i]);
    if (!t.dropBegins) return;
    for (StmtPtr* sp : w.topBegins) *sp = withoutBegin(static_cast<Block*>(sp->get()));
    if (t.restore.empty()) return;
    std::string src = "BEGIN {\n";
    for (auto& r : t.restore) src += "    " + r.first + " = " + r.second + ";\n";
    src += "}\n";
    try {
        Lexer lexer(src);
        Parser parser(lexer.tokenize());
        Program rp = parser.parseProgram();
        for (auto& s : rp.stmts) prog.stmts.push_back(std::move(s));
    } catch (...) {}
}

std::string encodeCtTable(const CtTable& t) {
    std::ostringstream o;
    o << "CT1 " << t.nSites << " " << t.nTopBegins << " " << (t.dropBegins ? 1 : 0) << " "
      << t.subs.size() << " " << t.restore.size() << "\n";
    for (auto& e : t.subs) o << e.site << " " << e.kind << " " << e.line << " " << e.lit.size() << "\n" << e.lit;
    for (auto& r : t.restore) o << r.first.size() << " " << r.second.size() << "\n" << r.first << r.second;
    return o.str();
}

void rakuppSetCtTable(const char* data, std::size_t len) {
    g_runTable = CtTable{};
    if (decodeCtTable(std::string(data, len), g_runTable)) g_ctApply = &applyRunTable;
}

} // namespace rakupp
