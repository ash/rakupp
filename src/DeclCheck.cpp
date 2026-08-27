#include "DeclCheck.h"
#include "AsciiCtype.h"
#include "Ast.h"
#include "Parser.h"      // rakuppFindModuleSource
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace rakupp {

bool isSpecialVar(const std::string& n);  // Interpreter.cpp — the runtime's own exemption list
bool isPragmaName(const std::string& n);  // Interpreter.cpp — `use` names with no file behind them

namespace {

// A name this pass may judge. isSpecialVar is the interpreter's own predicate
// for "usable without a `my`" — twigils, $_ $/ $! $0…, @_ %_, $a/$b, every
// `&`-sigil name, anything package-qualified — so deferring to it is what keeps
// the static answer and the runtime answer the same answer.
bool checkable(const std::string& n) {
    return n.size() >= 2 && (n[0] == '$' || n[0] == '@' || n[0] == '%') && !isSpecialVar(n);
}

struct Checker {
    std::vector<std::set<std::string>> scopes;
    std::vector<UndeclaredVar> out;
    std::set<std::string> reported;      // one finding per name, at its first use
    std::set<std::string> imports;       // non-pragma module names the unit `use`s
    std::vector<std::string> extraLibs;  // literal `use lib` directories
    bool standDown = false;
    int curLine = 0;                     // enclosing statement, when a node carries no line

    int lineOf(const Node* n) const { return n && n->line ? n->line : curLine; }

    // Only `$`/`@`/`%` names are ever asked about — `&foo` is exempt by
    // isSpecialVar — so registering anything else is work nobody reads.
    static bool worthTracking(const std::string& n) {
        return n.size() > 1 && (n[0] == '$' || n[0] == '@' || n[0] == '%');
    }
    void declare(const std::string& n) {
        if (worthTracking(n)) scopes.back().insert(n);
    }
    // `our` installs into the PACKAGE, so a sibling scope sees it — and the
    // parser rewrites the `$OUR::x` spelling of it down to a bare `$x`. Both
    // are answered by registering it at unit level rather than where it stands.
    void declareOur(const std::string& n) {
        if (worthTracking(n)) scopes.front().insert(n);
    }
    // A placeholder parameter names its own variable: `$^bb` IS the declaration
    // of `$bb` in the block that mentions it, and the rest of that block then
    // says `$bb`. Same for the named form `$:bb`.
    void declarePlaceholder(const std::string& n) {
        if (n.size() > 2 && (n[1] == '^' || n[1] == ':'))
            declare(n.substr(0, 1) + n.substr(2));
    }
    bool visible(const std::string& n) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (it->count(n)) return true;
        return false;
    }

    void use(const VarExpr* v) {
        // A package-symbol slot, a pseudo-package lookup and a PROCESS:: write
        // all ANSWER about a name rather than demanding one — the interpreter
        // lets each of them through, so this must too.
        if (v->pkgSymbol || v->viaPseudoPkg || v->processScoped) return;
        if (!checkable(v->name) || visible(v->name)) return;
        if (!reported.insert(v->name).second) return;
        out.push_back({v->name, lineOf(v)});
    }

    // ---- declaration collection -----------------------------------------
    //
    // Run over a statement list BEFORE walking it, so a closure that captures a
    // variable declared further down resolves. Only names that land in THIS
    // scope are collected: nested blocks, routine bodies and closures own
    // theirs, and are not descended into. The exceptions are the constructs
    // that deliberately share the enclosing scope — statement modifiers
    // (`my $x = 1 if $c` declares $x here even when $c is false) and the
    // statement form of a phaser (`INIT my $x = …`).

    void collectExpr(const Expr* e) {
        if (!e) return;
        switch (e->kind) {
            case NK::VarExpr: {
                auto* v = static_cast<const VarExpr*>(e);
                if (v->declare) (v->declScope == "our" ? declareOur(v->name) : declare(v->name));
                declarePlaceholder(v->name);
                collectExpr(v->declDefault.get());
                collectExpr(v->declShape.get());
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
                collectExpr(m->inv.get()); collectExpr(m->methodExpr.get());
                for (auto& a : m->args) collectExpr(a.get());
                return;
            }
            case NK::Index: {
                auto* i = static_cast<const Index*>(e);
                collectExpr(i->base.get()); collectExpr(i->index.get()); return;
            }
            case NK::Ternary: {
                auto* t = static_cast<const Ternary*>(e);
                collectExpr(t->cond.get()); collectExpr(t->then.get()); collectExpr(t->els.get());
                return;
            }
            case NK::Range: {
                auto* r = static_cast<const RangeExpr*>(e);
                collectExpr(r->from.get()); collectExpr(r->to.get()); return;
            }
            case NK::Pair: {
                auto* p = static_cast<const PairExpr*>(e);
                collectExpr(p->keyExpr.get()); collectExpr(p->value.get()); return;
            }
            case NK::ListExpr:  for (auto& x : static_cast<const ListExpr*>(e)->items) collectExpr(x.get()); return;
            case NK::ArrayLit:  for (auto& x : static_cast<const ArrayLit*>(e)->items) collectExpr(x.get()); return;
            case NK::HashLit:   for (auto& x : static_cast<const HashLit*>(e)->items) collectExpr(x.get()); return;
            case NK::InterpStr: for (auto& x : static_cast<const InterpStr*>(e)->parts) collectExpr(x.get()); return;
            case NK::ChainExpr: for (auto& x : static_cast<const ChainExpr*>(e)->operands) collectExpr(x.get()); return;
            case NK::NqpOp:     for (auto& x : static_cast<const NqpOp*>(e)->args) collectExpr(x.get()); return;
            // BlockExpr owns its scope; literals and regex text declare nothing.
            default: return;
        }
    }

    void collectStmt(const Stmt* s) {
        if (!s) return;
        auto sharedBody = [&](const Block* b) { if (b) for (auto& x : b->stmts) collectStmt(x.get()); };
        switch (s->kind) {
            case NK::ExprStmt: collectExpr(static_cast<const ExprStmt*>(s)->e.get()); return;
            case NK::VarDecl: {
                auto* d = static_cast<const VarDecl*>(s);
                for (auto& n : d->names) d->scope == "our" ? declareOur(n) : declare(n);
                collectExpr(d->init.get());
                return;
            }
            case NK::SubDecl:       return;  // a `&name` is exempt; nothing to register
            case NK::NamedRegexDecl: return;
            case NK::Block: {
                auto* b = static_cast<const Block*>(s);
                if (b->stmtForm) sharedBody(b);  // `INIT my $x = …` declares out here
                return;
            }
            // The condition/topic/list of a statement runs in the ENCLOSING
            // scope, so `if my $line = $fh.get { … }` declares $line here.
            case NK::IfStmt: {
                auto* f = static_cast<const IfStmt*>(s);
                for (auto& br : f->branches) {
                    collectExpr(br.first.get());
                    if (f->modifier) sharedBody(br.second.get());
                }
                if (f->modifier) sharedBody(f->elseBlock.get());
                return;
            }
            case NK::WhileStmt: {
                auto* w = static_cast<const WhileStmt*>(s);
                collectExpr(w->cond.get());
                if (w->modifier) sharedBody(w->body.get());
                return;
            }
            case NK::ForStmt: {
                auto* f = static_cast<const ForStmt*>(s);
                collectExpr(f->list.get());
                if (f->modifier) sharedBody(f->body.get());
                return;
            }
            case NK::GivenStmt: {
                auto* g = static_cast<const GivenStmt*>(s);
                collectExpr(g->topic.get());
                if (g->modifier) sharedBody(g->body.get());
                return;
            }
            case NK::RepeatStmt: collectExpr(static_cast<const RepeatStmt*>(s)->cond.get()); return;
            case NK::WhenStmt:   collectExpr(static_cast<const WhenStmt*>(s)->cond.get()); return;
            case NK::ReturnStmt: collectExpr(static_cast<const ReturnStmt*>(s)->value.get()); return;
            case NK::UseStmt:    collectExpr(static_cast<const UseStmt*>(s)->argExpr.get()); return;
            case NK::EnumDecl:   collectExpr(static_cast<const EnumDecl*>(s)->values.get()); return;
            case NK::SubsetDecl: collectExpr(static_cast<const SubsetDecl*>(s)->where.get()); return;
            // `loop (my $i = 0; …)` builds no implicit block, so its init declares
            // into THIS scope and `$i` outlives the loop (S04, no-implicit-block.t).
            case NK::LoopStmt: {
                auto* l = static_cast<const LoopStmt*>(s);
                collectExpr(l->init.get()); collectExpr(l->cond.get()); collectExpr(l->incr.get());
                return;
            }
            default: return;
        }
    }

    // ---- the walk --------------------------------------------------------

    void params(const std::vector<Param>& ps) {
        for (auto& p : ps) {
            declare(p.name);
            if (!p.namedKey.empty()) declare(std::string(1, p.sigil) + p.namedKey);
            if (p.subSig) params(*p.subSig);
        }
    }
    // Defaults and `where` clauses see the signature's own names, so they are
    // walked once the whole signature is declared.
    void paramExprs(const std::vector<Param>& ps) {
        for (auto& p : ps) {
            walkExpr(p.defaultVal.get());
            walkExpr(p.whereExpr.get());
            walkExpr(p.litVal.get());
            if (p.subSig) paramExprs(*p.subSig);
        }
    }

    void walkBody(const std::vector<Param>& ps, const std::vector<StmtPtr>& body,
                  const std::vector<std::vector<Param>>* alts = nullptr,
                  const Expr* retLiteral = nullptr) {
        scopes.push_back({});
        params(ps);
        if (alts) for (auto& a : *alts) params(a);   // `(sig1) | (sig2)` share the body
        paramExprs(ps);
        walkStmts(body);
        walkExpr(retLiteral);                        // `--> $x` sees the signature
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
        if (b->stmtForm) { walkStmts(b->stmts); return; }  // already collected out here
        scopes.push_back({});
        walkStmts(b->stmts);
        scopes.pop_back();
    }

    void walkExpr(const Expr* e) {
        if (!e || standDown) return;
        switch (e->kind) {
            case NK::VarExpr: {
                auto* v = static_cast<const VarExpr*>(e);
                walkExpr(v->declDefault.get());
                walkExpr(v->declShape.get());
                if (v->declare) { declare(v->name); return; }
                use(v);
                return;
            }
            case NK::SymbolicRef:
                // `::($name)` can name anything at run time, and assigning
                // through one CREATES a symbol. Nothing here is knowable.
                standDown = true;
                return;
            case NK::Call: {
                auto* c = static_cast<const Call*>(e);
                if (nameEvalsCode(c->name)) { standDown = true; return; }
                walkExpr(c->callee.get());
                for (auto& a : c->args) walkExpr(a.get());
                return;
            }
            case NK::MethodCall: {
                auto* m = static_cast<const MethodCall*>(e);
                if (nameEvalsCode(m->method)) { standDown = true; return; }
                walkExpr(m->inv.get()); walkExpr(m->methodExpr.get());
                for (auto& a : m->args) walkExpr(a.get());
                return;
            }
            case NK::Unary: {
                auto* u = static_cast<const Unary*>(e);
                // `require Foo` imports at run time, from a name this pass has
                // no way to resolve to a set of symbols.
                if (u->op == "require") { standDown = true; return; }
                walkExpr(u->operand.get());
                return;
            }
            case NK::Assign: {
                auto* a = static_cast<const Assign*>(e);
                walkExpr(a->target.get()); walkExpr(a->value.get()); return;
            }
            case NK::Binary: {
                auto* b = static_cast<const Binary*>(e);
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
            // A regex body is kept as raw pattern text, never parsed into an
            // AST, so it contributes no references to check — and `:my $*x`
            // inside one declares nothing this pass can see either.
            default: return;
        }
    }

    void walkStmt(const Stmt* s) {
        if (!s || standDown) return;
        switch (s->kind) {
            case NK::ExprStmt: walkExpr(static_cast<const ExprStmt*>(s)->e.get()); return;
            case NK::VarDecl:  walkExpr(static_cast<const VarDecl*>(s)->init.get()); return;
            case NK::SubDecl: {
                auto* sd = static_cast<const SubDecl*>(s);
                walkExpr(sd->nameExpr.get());
                for (auto& t : sd->traits) walkExpr(t.arg.get());
                for (auto& a : sd->immediateArgs) walkExpr(a.get());
                walkExpr(sd->nativeLibExpr.get());
                walkExpr(sd->nativeSymExpr.get());
                walkBody(sd->params, sd->body, &sd->altParams, sd->retLiteral.get());
                return;
            }
            case NK::Block: walkBlock(static_cast<const Block*>(s)); return;
            case NK::IfStmt: {
                auto* f = static_cast<const IfStmt*>(s);
                for (size_t i = 0; i < f->branches.size(); i++) {
                    walkExpr(f->branches[i].first.get());
                    scopes.push_back({});
                    declare(f->thenVar);                                   // `if E -> $x { }`
                    if (i < f->branchVars.size()) declare(f->branchVars[i]);
                    if (i < f->branchParams.size()) {                      // `if E -> ($a,$b) { }`
                        params(f->branchParams[i]);
                        paramExprs(f->branchParams[i]);
                    }
                    walkBlock(f->branches[i].second.get());
                    scopes.pop_back();
                }
                scopes.push_back({});
                declare(f->elseVar);
                params(f->elseParams); paramExprs(f->elseParams);
                walkBlock(f->elseBlock.get());
                scopes.pop_back();
                return;
            }
            case NK::WhileStmt: {
                auto* w = static_cast<const WhileStmt*>(s);
                walkExpr(w->cond.get());
                scopes.push_back({});
                declare(w->var);
                params(w->params);
                paramExprs(w->params);
                walkBlock(w->body.get());
                scopes.pop_back();
                return;
            }
            case NK::RepeatStmt: {
                auto* r = static_cast<const RepeatStmt*>(s);
                // `repeat { my $x = … } while $x` — the condition is evaluated
                // inside the body's scope, and Raku says so.
                scopes.push_back({});
                if (r->body) walkStmts(r->body->stmts);
                walkExpr(r->cond.get());
                scopes.pop_back();
                return;
            }
            case NK::ForStmt: {
                auto* f = static_cast<const ForStmt*>(s);
                walkExpr(f->list.get());
                scopes.push_back({});
                for (auto& v : f->vars) declare(v);
                params(f->params);
                paramExprs(f->params);
                walkBlock(f->body.get());
                scopes.pop_back();
                return;
            }
            case NK::LoopStmt: {
                auto* l = static_cast<const LoopStmt*>(s);
                walkExpr(l->init.get());            // already collected into this scope
                walkExpr(l->cond.get());
                walkExpr(l->incr.get());
                walkBlock(l->body.get());
                return;
            }
            case NK::GivenStmt: {
                auto* g = static_cast<const GivenStmt*>(s);
                walkExpr(g->topic.get());
                scopes.push_back({});
                declare(g->var);
                params(g->params); paramExprs(g->params);
                walkBlock(g->body.get());
                scopes.pop_back();
                scopes.push_back({});
                declare(g->elseVar);
                params(g->elseParams); paramExprs(g->elseParams);
                walkBlock(g->elseBody.get());
                scopes.pop_back();
                return;
            }
            case NK::WhenStmt: {
                auto* w = static_cast<const WhenStmt*>(s);
                walkExpr(w->cond.get());
                walkBlock(w->body.get());
                return;
            }
            case NK::ReturnStmt: walkExpr(static_cast<const ReturnStmt*>(s)->value.get()); return;
            case NK::EnumDecl:   walkExpr(static_cast<const EnumDecl*>(s)->values.get()); return;
            case NK::SubsetDecl: walkExpr(static_cast<const SubsetDecl*>(s)->where.get()); return;
            case NK::UseStmt: {
                auto* u = static_cast<const UseStmt*>(s);
                walkExpr(u->argExpr.get());
                walkExpr(u->ifCond.get());
                // `no strict` turns an undeclared variable into an auto-vivified
                // one, which is the whole point of it.
                if (u->isNo && u->module == "strict") { standDown = true; return; }
                // `use lib 'dir'` widens where an import will be found, so it
                // widens where this pass looks for one. A COMPUTED path
                // (`use lib $?FILE.IO.parent`) names a directory only the run
                // can know, and any module could be hiding in it.
                if (u->module == "lib") {
                    if (!u->arg.empty()) extraLibs.push_back(u->arg);
                    for (auto& a : u->importArgs) extraLibs.push_back(a);
                    if (u->argExpr && u->arg.empty()) standDown = true;
                    return;
                }
                if (!u->isNeed && !u->module.empty() && !isPragmaName(u->module))
                    imports.insert(u->module);
                return;
            }
            case NK::ClassDecl: {
                auto* c = static_cast<const ClassDecl*>(s);
                walkExpr(c->nameExpr.get());
                walkExpr(c->verExpr.get()); walkExpr(c->authExpr.get()); walkExpr(c->apiExpr.get());
                scopes.push_back({});
                params(c->roleParams);
                paramExprs(c->roleParams);
                // The package body's own lexicals are in scope for the attribute
                // defaults written above them (`my %H = …; has $.g = %H;`), so
                // they are registered before anything in this class is walked.
                for (auto& x : c->body) collectStmt(x.get());
                for (auto& a : c->attrs) {
                    // `has $x` (no twigil) is read as a plain `$x` throughout the
                    // class. The AST does not record which spelling was written,
                    // so the bare name is registered for every attribute — the
                    // safe direction, since the twigil forms are exempt anyway.
                    declare(std::string(1, a.sigil) + a.name);
                    // `has $.x` is reached as `$!x`/`$.x` — twigil forms the
                    // runtime exempts and checks by its own route.
                    walkExpr(a.def.get());
                    walkExpr(a.whereExpr.get());
                    for (auto& t : a.userTraits) walkExpr(t.second.get());
                }
                for (auto& ra : c->roleArgs) for (auto& x : ra.second) walkExpr(x.get());
                // The body first: a lexical the package declares must be in
                // scope for the methods that call it, whichever order they sit in.
                walkStmts(c->body);
                for (auto& m : c->methods) {
                    if (!m) continue;
                    if (m->line) curLine = m->line;
                    walkStmt(m.get());
                }
                scopes.pop_back();
                return;
            }
            default: return;  // Last/Next/Redo/Empty/NamedRegexDecl — no expressions to walk
        }
    }
};

bool identChar(char c) {
    return ascii::isalnum((unsigned char)c) != 0 || c == '_' || c == '-' || c == '\'';
}
bool wordAt(const std::string& hay, const std::string& word) {
    for (size_t p = hay.find(word); p != std::string::npos; p = hay.find(word, p + 1)) {
        if (p && identChar(hay[p - 1])) continue;
        size_t e = p + word.size();
        if (e < hay.size() && identChar(hay[e])) continue;
        return true;
    }
    return false;
}
// `hay` mentions the WHOLE sigilled name, not merely something starting with
// it. Without the trailing boundary `$s` matches inside `$setting`, and any
// short name would be cleared by any module large enough to contain it.
bool nameIn(const std::string& hay, const std::string& name) {
    for (size_t p = hay.find(name); p != std::string::npos; p = hay.find(name, p + 1)) {
        size_t e = p + name.size();
        if (e >= hay.size() || !identChar(hay[e])) return true;
    }
    return false;
}

// The last word on whether a name is declared: does the SOURCE ever spell it as
// one? The AST is not always able to say. rakupp's parser drops binders it
// cannot yet model — a `repeat … -> $x` binding, a destructuring `with … -> ($x)`
// — and erases the pseudo-package qualifier from `$OUR::x`, `$CALLER::y` down to
// a bare name; each of those makes a properly declared variable look undeclared,
// and refusing to run such a program would be much worse than the late error
// this check replaces. So a candidate is dropped unless the text agrees.
//
// The cost is the cross-scope case: a name declared in one routine and used in
// another is no longer reported. What is bought is that every finding means
// "this name is declared NOWHERE in this file", which no gap in the parser can
// falsify. The AST pass is still what produces the candidates — it knows about
// placeholder parameters and signatures, which no text scan could — this only
// ever removes them.
bool textDeclares(const std::string& src, const std::string& name) {
    if (src.empty() || name.size() < 2) return true;   // nothing to check against
    const std::string bare = name.substr(1);
    // Written package-qualified somewhere — `$OUR::x`, `$Foo::x`, `CALLER::<$x>`.
    if (nameIn(src, "::" + bare)) return true;
    for (size_t p = src.find(name); p != std::string::npos; p = src.find(name, p + 1)) {
        size_t e = p + name.size();
        if (e < src.size() && identChar(src[e])) continue;      // a longer name
        // What stands before it, back to the nearest statement or block
        // boundary — far enough to cover `my Int $x`, `sub f(Str $x where …`
        // and a signature that wrapped onto its own line. Comments are not
        // stripped, so a `# set my counter` above the file's FIRST statement
        // can clear a name it does not really declare; every other position has
        // a `;`/`{`/`}` between it and the comment. Left as is deliberately —
        // stripping could remove a real declarator that followed a `#` inside a
        // string, and losing a finding beats refusing a working program.
        size_t from = p > 240 ? p - 240 : 0;
        std::string pre = src.substr(from, p - from);
        size_t cut = pre.find_last_of(";{}");
        if (cut != std::string::npos) pre = pre.substr(cut + 1);
        if (pre.find("->") != std::string::npos) return true;   // a pointy signature
        for (const char* kw : {"my", "our", "state", "has", "constant",
                               "sub", "method", "multi", "submethod"})
            if (wordAt(pre, kw)) return true;
    }
    return false;
}

// A candidate survives only if no imported module could have supplied the name.
// The scan is deliberately blunt: an imported module's source is searched for
// the name as written, and any hit at all clears the candidate. It runs only
// once a candidate exists — i.e. on the way to refusing the program — so the
// file reads cost a working run nothing. A module whose source cannot be found,
// or one with a `sub EXPORT` that can export anything it likes, ends the check.
bool clearedByImports(std::vector<UndeclaredVar>& cands,
                      const std::set<std::string>& imports,
                      const std::vector<std::string>& searchPath, bool sixE) {
    for (auto& mod : imports) {
        std::string path, src;
        if (!rakuppFindModuleSource(mod, searchPath, path, src, sixE)) return false;
        if (src.find("sub EXPORT") != std::string::npos) return false;
        cands.erase(std::remove_if(cands.begin(), cands.end(),
                                   [&](const UndeclaredVar& c) { return nameIn(src, c.name); }),
                    cands.end());
        if (cands.empty()) return true;
    }
    return true;
}

} // namespace

std::vector<UndeclaredVar> findUndeclaredVars(const Program& prog, const std::string& src,
                                              const std::vector<std::string>& searchPath) {
    Checker C;
    C.scopes.push_back({});   // unit scope
    C.walkStmts(prog.stmts);
    C.scopes.pop_back();
    if (C.standDown || C.out.empty()) return {};
    C.out.erase(std::remove_if(C.out.begin(), C.out.end(),
                               [&](const UndeclaredVar& c) { return textDeclares(src, c.name); }),
                C.out.end());
    if (C.out.empty()) return {};
    if (!C.imports.empty()) {
        std::vector<std::string> sp(C.extraLibs.begin(), C.extraLibs.end());
        sp.insert(sp.end(), searchPath.begin(), searchPath.end());
        if (!clearedByImports(C.out, C.imports, sp, prog.langRev >= 2)) return {};
    }
    std::sort(C.out.begin(), C.out.end(),
              [](const UndeclaredVar& a, const UndeclaredVar& b) {
                  if (a.line != b.line) return a.line < b.line;
                  return a.name < b.name;
              });
    return C.out;
}

bool declCheckEnabled() {
    const char* off = std::getenv("RAKUPP_NO_DECLCHECK");
    return !(off && *off && std::string(off) != "0");
}

int reportUndeclaredVars(const std::vector<UndeclaredVar>& findings,
                         const std::string& fileName, const std::string& src) {
    // The source split lazily: only a reported line is ever looked up.
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
    // A variable inside an interpolated string is built by a sub-parser that
    // knows nothing of the file's line numbering, so its node carries none and
    // the finding lands on the line the STATEMENT started. Walk forward to the
    // first line that actually spells the name — for a single-line statement
    // that is the same line, and for a wrapped one it is the right one.
    auto locate = [&](const UndeclaredVar& f) {
        for (int l = f.line; l < f.line + 200; l++) {
            std::string ln = sourceLine(l);
            if (ln.empty() && l > f.line) continue;
            if (ln.find(f.name) != std::string::npos) return l;
        }
        return f.line;
    };
    std::cerr << "===SORRY!=== Error while compiling " << fileName << "\n";
    for (auto& f : findings) {
        int line = locate(f);
        std::cerr << "Variable '" << f.name << "' is not declared\n"
                  << "at " << fileName << ":" << line << "\n";
        std::string ln = sourceLine(line);
        if (ln.empty()) continue;
        // Point at the variable itself when the line says where it is; the AST
        // carries no column, so the name is located by searching the line.
        size_t at = ln.find(f.name);
        if (at != std::string::npos)
            std::cerr << "------> " << ln.substr(0, at) << "\u23CF" << ln.substr(at) << "\n";
        else
            std::cerr << "------> " << ln << "\n";
    }
    return 1;
}

} // namespace rakupp
