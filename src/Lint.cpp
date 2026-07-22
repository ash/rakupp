#include "Lint.h"
#include "Ast.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rakupp {
namespace {

// ---- small predicates ----------------------------------------------------

bool isSigil(char c) { return c == '$' || c == '@' || c == '%' || c == '&'; }

// A lexical variable eligible for the unused-variable rule: an ordinary named
// `$x`/`@y`/`%z`/`&f`. Rejects the anonymous sigil, the contextual/special
// vars ($_ $/ $!), numeric match vars ($0…), and every twigil form
// ($*dyn $?compile $.attr $!attr $^placeholder $:named) — those are set,
// captured, or resolved by machinery the linter can't see.
bool eligibleVarName(const std::string& n) {
    if (n.size() < 2 || !isSigil(n[0])) return false;
    if (n == "$_") return false;
    char t = n[1];
    return std::isalpha((unsigned char)t) != 0 || t == '_';
}

bool isNumericLiteralStr(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[i] == '+' || s[i] == '-') i++;
    bool dig = false, dot = false, exp = false;
    for (; i < s.size(); i++) {
        char c = s[i];
        if (std::isdigit((unsigned char)c)) { dig = true; continue; }
        if (c == '.' && !dot && !exp) { dot = true; continue; }
        if ((c == 'e' || c == 'E') && dig && !exp) { exp = true; dig = false; continue; }
        if ((c == '+' || c == '-') && (s[i - 1] == 'e' || s[i - 1] == 'E')) continue;
        if (c == '_') continue; // 1_000
        return false;
    }
    return dig;
}

// An unconditional flow terminator as a bare statement (statement modifiers
// wrap into IfStmt/WhileStmt/etc., so a bare Return/Last/Next/Redo here really
// is unconditional). `die`/`exit` as a plain call count too.
bool isTerminator(Stmt* s) {
    switch (s->kind) {
        case NK::ReturnStmt: case NK::LastStmt:
        case NK::NextStmt:   case NK::RedoStmt: return true;
        case NK::ExprStmt: {
            auto* es = static_cast<ExprStmt*>(s);
            if (es->e && es->e->kind == NK::Call) {
                auto* c = static_cast<Call*>(es->e.get());
                if (!c->callee && (c->name == "die" || c->name == "exit")) return true;
            }
            return false;
        }
        default: return false;
    }
}

// A statement that would actually run — i.e. one whose presence after a
// terminator is genuinely unreachable. Declarations/phasers/empties are
// installed or hoisted at compile time, so they don't count.
bool isRuntimeStmt(Stmt* s) {
    switch (s->kind) {
        case NK::SubDecl: case NK::ClassDecl: case NK::EnumDecl:
        case NK::SubsetDecl: case NK::UseStmt: case NK::EmptyStmt:
        case NK::NamedRegexDecl: return false;
        case NK::VarDecl:  // `my $x;` after return is hoisted, harmless
            return static_cast<VarDecl*>(s)->init != nullptr;
        case NK::Block:
            return static_cast<Block*>(s)->phaser.empty() &&
                   !static_cast<Block*>(s)->isCatch;
        default: return true;
    }
}

// ---- the linter ----------------------------------------------------------

struct Decl {
    std::string name;
    int line = 0;
    int uses = 0;
    char kind = 'v';   // 'v' variable, 'p' parameter, 's' sub
    bool emit = true;   // whether an unused finding should be reported for it
    bool exported = false;
};

struct Scope {
    std::vector<Decl> decls;
    Decl* find(const std::string& n) {
        for (auto it = decls.rbegin(); it != decls.rend(); ++it)
            if (it->name == n) return &*it;
        return nullptr;
    }
};

// Extract the constant string value of an operand that is a literal string —
// either a single-quoted StrLit or a double-quoted InterpStr with no actual
// interpolation. Returns false for anything dynamic.
bool constStr(Expr* e, std::string& out) {
    if (!e) return false;
    if (e->kind == NK::StrLit) { out = static_cast<StrLit*>(e)->v; return true; }
    if (e->kind == NK::InterpStr) {
        std::string flat;
        for (auto& p : static_cast<InterpStr*>(e)->parts) {
            if (p->kind != NK::StrLit) return false;
            flat += static_cast<StrLit*>(p.get())->v;
        }
        out = flat;
        return true;
    }
    return false;
}

// Collect declaring `my`/`state` VarExprs from an assignment target (a bare
// `my $x`, or `my ($a, $b)` = a parenned list of declarers).
void collectDeclTargets(Expr* e, std::vector<VarExpr*>& out) {
    if (!e) return;
    if (e->kind == NK::VarExpr) {
        auto* v = static_cast<VarExpr*>(e);
        if (v->declare) out.push_back(v);
    } else if (e->kind == NK::ListExpr) {
        for (auto& it : static_cast<ListExpr*>(e)->items) collectDeclTargets(it.get(), out);
    }
}

struct Linter {
    std::vector<LintFinding> out;
    std::vector<Scope> scopes;
    bool dynamicNames = false; // EVAL / symbolic refs seen → suppress "unused" rules
    int curLine = 0;           // line of the statement currently being walked (fallback)
    std::map<std::string, ClassDecl*> classes; // in-file class/role decls, for .new arg checks

    int lineOf(Node* n) const { return n && n->line ? n->line : curLine; }

    void warn(int line, const char* rule, std::string msg, char sev = 'W') {
        out.push_back({line, sev, rule, std::move(msg)});
    }

    Scope& cur() { return scopes.back(); }

    // Register a declaration in the current scope. Emits redeclaration if a
    // same-name declaration already exists at this level.
    Decl* declare(const std::string& name, int line, char kind, bool emit) {
        if (Decl* prev = cur().find(name)) {
            if (kind == 'v' && prev->kind == 'v')
                warn(line, "redeclaration",
                     "redeclaration of '" + name + "' (first declared on line " +
                         std::to_string(prev->line) + ")");
            return prev;
        }
        cur().decls.push_back({name, line, 0, kind, emit, false});
        return &cur().decls.back();
    }

    void markUse(const std::string& name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (Decl* d = it->find(name)) { d->uses++; return; }
    }

    // Regex/substitution/rule bodies are kept as raw pattern text, not parsed
    // into an AST, so a variable that appears only inside one (`/ $foo /`,
    // `(%bases)`, `@(%h.keys)`) would otherwise look unused. Scan the text for
    // interpolated `$x`/`@x`/`%x`/`&x` and count each as a use. Over-counting is
    // fine (it only suppresses a warning); `$<capture>` and the `$` anchor don't
    // match because a sigil must be followed by an identifier character.
    void markUsesInRegex(const std::string& pat) {
        for (size_t i = 0; i + 1 < pat.size(); i++) {
            char c = pat[i];
            if (c != '$' && c != '@' && c != '%' && c != '&') continue;
            char n = pat[i + 1];
            if (!(std::isalpha((unsigned char)n) || n == '_')) continue;
            size_t j = i + 1;
            while (j < pat.size() &&
                   (std::isalnum((unsigned char)pat[j]) || pat[j] == '_' ||
                    pat[j] == '-' || pat[j] == '\''))
                j++;
            markUse(pat.substr(i, j - i));
            i = j - 1;
        }
    }

    void popScope() {
        for (auto& d : cur().decls) {
            if (d.uses || !d.emit || d.exported) continue;
            if (dynamicNames) continue; // a name may be reached via EVAL / ::()
            if (d.kind == 'v')
                warn(d.line, "unused-variable",
                     "'" + d.name + "' is declared but never used");
            else if (d.kind == 'p')
                // advisory only: an unused parameter is frequently intentional
                // (callback/dispatch signatures, interface conformance).
                warn(d.line, "unused-parameter",
                     "parameter '" + d.name + "' is never used", 'N');
            else if (d.kind == 's')
                warn(d.line, "unused-routine",
                     "lexical routine '" + d.name + "' is never called");
        }
        scopes.pop_back();
    }

    // Pre-register statement-level declarations so a forward reference (a
    // closure capturing a variable declared later, or a call to a sub defined
    // below) resolves as a use rather than a phantom "unused".
    void predeclare(const std::vector<StmtPtr>& stmts) {
        for (auto& sp : stmts) {
            Stmt* s = sp.get();
            if (s->kind == NK::VarDecl) {
                auto* d = static_cast<VarDecl*>(s);
                if (d->scope != "my" && d->scope != "state") continue;
                for (auto& n : d->names)
                    if (eligibleVarName(n)) declare(n, s->line, 'v', true);
            }
            else if (s->kind == NK::ExprStmt) {
                auto* es = static_cast<ExprStmt*>(s);
                Expr* e = es->e.get();
                // `my $x = …` parses as an Assign whose target declares; `my $x;`
                // as a bare declaring VarExpr; `my ($a,$b) = …` as a list target.
                std::vector<VarExpr*> targets;
                if (e && e->kind == NK::Assign)
                    collectDeclTargets(static_cast<Assign*>(e)->target.get(), targets);
                else
                    collectDeclTargets(e, targets);
                for (auto* v : targets)
                    if ((v->declScope == "my" || v->declScope == "state") &&
                        eligibleVarName(v->name))
                        declare(v->name, s->line, 'v', true);
            }
            else if (s->kind == NK::SubDecl) {
                auto* sd = static_cast<SubDecl*>(s);
                // Only lexical `my sub` is safe to flag as unused; a plain or
                // `our`/`is export` sub may be an entry point or API surface.
                // MAIN/USAGE are invoked by the runtime, never by name.
                bool lexical = sd->name.size() && !sd->isOur && !sd->isExport &&
                               !sd->isMethod && !sd->isMulti &&
                               sd->name != "MAIN" && sd->name != "USAGE";
                if (lexical) {
                    std::string key = "&" + sd->name;
                    Decl* d = declare(key, s->line, 's', true);
                    if (d) d->exported = false;
                }
            }
        }
    }

    // ---- expression walk (counts uses) -----------------------------------

    void walkParams(std::vector<Param>& ps, bool emitUnused, int lineHint) {
        for (auto& p : ps) {
            if (p.subSig) { walkParams(*p.subSig, false, lineHint); }
            if (p.defaultVal) walkExpr(p.defaultVal.get());
            if (p.whereExpr) walkExpr(p.whereExpr.get());
            if (p.name.empty() || p.name.size() < 2) continue;
            bool elig = emitUnused && !p.slurpy && !p.invocant &&
                        eligibleVarName(p.name) && !p.subSig;
            declare(p.name, lineHint, 'p', elig);
        }
    }

    void walkBlockBody(std::vector<Param>& params, std::vector<StmtPtr>& body,
                       bool emitUnusedParams, int lineHint) {
        scopes.push_back({});
        walkParams(params, emitUnusedParams, lineHint);
        walkStmts(body);
        popScope();
    }

    void walkExpr(Expr* e) {
        if (!e) return;
        switch (e->kind) {
            case NK::VarExpr: {
                auto* v = static_cast<VarExpr*>(e);
                if (v->declDefault) walkExpr(v->declDefault.get());
                if (v->declare) {
                    // A mid-expression declaration (e.g. `if my $x = f()`); the
                    // statement-level ones are already predeclared.
                    if ((v->declScope == "my" || v->declScope == "state") &&
                        eligibleVarName(v->name) && !cur().find(v->name))
                        declare(v->name, lineOf(v), 'v', true);
                    return;
                }
                markUse(v->name);
                return;
            }
            case NK::NameTerm: {
                markUse("&" + static_cast<NameTerm*>(e)->name); // sub called w/o parens
                return;
            }
            case NK::Call: {
                auto* c = static_cast<Call*>(e);
                if (!c->name.empty()) {
                    if (c->name == "EVAL" || c->name == "EVALFILE") dynamicNames = true;
                    markUse("&" + c->name);
                }
                if (c->callee) walkExpr(c->callee.get());
                for (auto& a : c->args) walkExpr(a.get());
                return;
            }
            case NK::MethodCall: {
                auto* m = static_cast<MethodCall*>(e);
                checkNewArgs(m);
                walkExpr(m->inv.get());
                if (m->methodExpr) walkExpr(m->methodExpr.get());
                for (auto& a : m->args) walkExpr(a.get());
                return;
            }
            case NK::Assign: {
                auto* a = static_cast<Assign*>(e);
                // self-assignment: $x = $x
                if (a->op == "=" && a->target && a->value &&
                    a->target->kind == NK::VarExpr && a->value->kind == NK::VarExpr &&
                    static_cast<VarExpr*>(a->target.get())->name ==
                        static_cast<VarExpr*>(a->value.get())->name &&
                    !static_cast<VarExpr*>(a->target.get())->declare) {
                    warn(lineOf(a), "self-assignment",
                         "'" + static_cast<VarExpr*>(a->target.get())->name +
                             "' is assigned to itself");
                }
                walkExpr(a->target.get());
                walkExpr(a->value.get());
                return;
            }
            case NK::Binary: {
                auto* b = static_cast<Binary*>(e);
                checkNumericStringCmp(b);
                walkExpr(b->lhs.get());
                walkExpr(b->rhs.get());
                return;
            }
            case NK::Unary:   walkExpr(static_cast<Unary*>(e)->operand.get()); return;
            case NK::Index: {
                auto* ix = static_cast<Index*>(e);
                walkExpr(ix->base.get()); walkExpr(ix->index.get());
                return;
            }
            case NK::Ternary: {
                auto* t = static_cast<Ternary*>(e);
                walkExpr(t->cond.get()); walkExpr(t->then.get()); walkExpr(t->els.get());
                return;
            }
            case NK::Range: {
                auto* r = static_cast<RangeExpr*>(e);
                walkExpr(r->from.get()); walkExpr(r->to.get());
                return;
            }
            case NK::Pair: {
                auto* p = static_cast<PairExpr*>(e);
                if (p->keyExpr) walkExpr(p->keyExpr.get());
                walkExpr(p->value.get());
                return;
            }
            case NK::InterpStr:
                for (auto& p : static_cast<InterpStr*>(e)->parts) walkExpr(p.get());
                return;
            case NK::ListExpr:
                for (auto& it : static_cast<ListExpr*>(e)->items) walkExpr(it.get());
                return;
            case NK::ArrayLit:
                for (auto& it : static_cast<ArrayLit*>(e)->items) walkExpr(it.get());
                return;
            case NK::HashLit:
                for (auto& it : static_cast<HashLit*>(e)->items) walkExpr(it.get());
                return;
            case NK::ChainExpr:
                for (auto& op : static_cast<ChainExpr*>(e)->operands) walkExpr(op.get());
                return;
            case NK::SymbolicRef: {
                dynamicNames = true;
                walkExpr(static_cast<SymbolicRef*>(e)->nameExpr.get());
                return;
            }
            case NK::BlockExpr: {
                auto* be = static_cast<BlockExpr*>(e);
                walkBlockBody(be->params, be->body, /*emitUnusedParams*/ false, lineOf(be));
                return;
            }
            case NK::RegexLit:
                markUsesInRegex(static_cast<RegexLit*>(e)->pattern);
                return;
            case NK::SubstLit: {
                auto* sub = static_cast<SubstLit*>(e);
                markUsesInRegex(sub->pattern);
                markUsesInRegex(sub->repl); // s/…/$x/ interpolates the replacement
                return;
            }
            default: return; // literals, Whatever, SelfTerm — no children of interest
        }
    }

    void checkNumericStringCmp(Binary* b) {
        static const std::set<std::string> numCmp = {"==", "!=", "<", "<=", ">", ">="};
        if (!numCmp.count(b->op) || !b->lhs || !b->rhs) return;
        std::string sv;
        if ((constStr(b->lhs.get(), sv) || constStr(b->rhs.get(), sv)) &&
            !isNumericLiteralStr(sv))
            warn(lineOf(b), "numeric-cmp-of-string",
                 "numeric '" + b->op + "' compares the string literal \"" + sv +
                     "\"; use eq/ne/lt/gt for string comparison");
    }

    // ---- statement walk --------------------------------------------------

    void walkStmts(std::vector<StmtPtr>& stmts) {
        predeclare(stmts);
        bool reported = false;
        for (size_t i = 0; i < stmts.size(); i++) {
            Stmt* s = stmts[i].get();
            if (s->line) curLine = s->line;
            if (!reported && i + 1 < stmts.size() && isTerminator(s)) {
                for (size_t j = i + 1; j < stmts.size(); j++) {
                    if (isRuntimeStmt(stmts[j].get())) {
                        warn(stmts[j]->line, "unreachable-code",
                             "code after an unconditional flow statement on line " +
                                 std::to_string(s->line) + " is never reached");
                        reported = true;
                        break;
                    }
                }
            }
            walkStmt(s);
        }
    }

    void walkBlock(Block* b) {
        if (!b) return;
        scopes.push_back({});
        walkStmts(b->stmts);
        popScope();
    }

    void constCond(Expr* c, bool isUnless, int line) {
        if (!c) return;
        int truth = -1; // -1 unknown, 0 false, 1 true
        switch (c->kind) {
            case NK::BoolLit: truth = static_cast<BoolLit*>(c)->v; break;
            case NK::IntLit:  truth = static_cast<IntLit*>(c)->v != 0; break;
            case NK::NumLit:  truth = static_cast<NumLit*>(c)->v != 0; break;
            case NK::StrLit: {
                const std::string& v = static_cast<StrLit*>(c)->v;
                truth = !(v.empty() || v == "0"); break;
            }
            case NK::NameTerm: {
                const std::string& n = static_cast<NameTerm*>(c)->name;
                if (n == "True") truth = 1; else if (n == "False") truth = 0;
                break;
            }
            default: return;
        }
        if (truth < 0) return;
        bool always = isUnless ? (truth == 0) : (truth == 1);
        warn(line, "constant-condition",
             std::string("condition is a constant; the branch ") +
                 (always ? "always runs" : "never runs"));
    }

    void walkStmt(Stmt* s) {
        if (!s) return;
        switch (s->kind) {
            case NK::ExprStmt: {
                auto* es = static_cast<ExprStmt*>(s);
                walkExpr(es->e.get());
                return;
            }
            case NK::VarDecl: {
                // Statement-level `my`/`state` names are already predeclared;
                // `our`/`has` aren't tracked. Only the initializer needs walking.
                auto* d = static_cast<VarDecl*>(s);
                if (d->init) walkExpr(d->init.get());
                return;
            }
            case NK::SubDecl: {
                auto* sd = static_cast<SubDecl*>(s);
                if (sd->retLiteral) walkExpr(sd->retLiteral.get());
                for (auto& a : sd->immediateArgs) walkExpr(a.get());
                for (auto& tr : sd->traits) if (tr.arg) walkExpr(tr.arg.get());
                // emit unused-parameter only for a plain named sub (not methods,
                // multis, or anonymous — those routinely carry contract params).
                bool emitParams = sd->name.size() && !sd->isMethod && !sd->isMulti;
                walkBlockBody(sd->params, sd->body, emitParams, lineOf(sd));
                redundantReturn(sd->body);
                return;
            }
            case NK::Block:   walkBlock(static_cast<Block*>(s)); return;
            case NK::IfStmt: {
                auto* f = static_cast<IfStmt*>(s);
                for (auto& br : f->branches) {
                    // only the FIRST clause is `if`/`unless`; elsif conditions are
                    // ordinary, so a constant there is less clearly a mistake.
                    walkExpr(br.first.get());
                    walkBlock(br.second.get());
                }
                if (!f->branches.empty())
                    constCond(f->branches.front().first.get(), f->isUnless, s->line);
                walkBlock(f->elseBlock.get());
                return;
            }
            case NK::WhileStmt: {
                auto* w = static_cast<WhileStmt*>(s);
                walkExpr(w->cond.get());
                scopes.push_back({});
                if (!w->var.empty() && eligibleVarName(w->var))
                    declare(w->var, s->line, 'p', false);
                if (w->body) walkStmts(w->body->stmts);
                popScope();
                return;
            }
            case NK::RepeatStmt: {
                auto* r = static_cast<RepeatStmt*>(s);
                walkExpr(r->cond.get());
                walkBlock(r->body.get());
                return;
            }
            case NK::ForStmt: {
                auto* f = static_cast<ForStmt*>(s);
                walkExpr(f->list.get());
                scopes.push_back({});
                for (auto& v : f->vars)
                    if (eligibleVarName(v)) declare(v, s->line, 'p', false);
                walkParams(f->params, false, s->line);
                if (f->body) walkStmts(f->body->stmts);
                popScope();
                return;
            }
            case NK::LoopStmt: {
                auto* l = static_cast<LoopStmt*>(s);
                scopes.push_back({}); // a `loop (my $i…)` init is scoped to the loop
                walkExpr(l->init.get());
                walkExpr(l->cond.get());
                walkExpr(l->incr.get());
                if (l->body) walkStmts(l->body->stmts);
                popScope();
                return;
            }
            case NK::GivenStmt: {
                auto* g = static_cast<GivenStmt*>(s);
                walkExpr(g->topic.get());
                scopes.push_back({});
                if (!g->var.empty() && eligibleVarName(g->var))
                    declare(g->var, s->line, 'p', false);
                if (g->body) walkStmts(g->body->stmts);
                popScope();
                if (g->elseBody) walkBlock(g->elseBody.get());
                return;
            }
            case NK::WhenStmt: {
                auto* w = static_cast<WhenStmt*>(s);
                walkExpr(w->cond.get());
                walkBlock(w->body.get());
                return;
            }
            case NK::ReturnStmt: walkExpr(static_cast<ReturnStmt*>(s)->value.get()); return;
            case NK::ClassDecl: {
                auto* c = static_cast<ClassDecl*>(s);
                scopes.push_back({});
                for (auto& a : c->attrs) if (a.def) walkExpr(a.def.get());
                for (auto& rule : c->rules) markUsesInRegex(rule.pattern);
                // Walk the package body first so any lexical routines it declares
                // are registered before the methods (which may call them) run; the
                // scope isn't closed — and unused checked — until after both.
                walkStmts(c->body);
                for (auto& m : c->methods) {
                    if (m->retLiteral) walkExpr(m->retLiteral.get());
                    walkBlockBody(m->params, m->body, /*emitParams*/ false, lineOf(m.get()));
                    redundantReturn(m->body);
                }
                popScope();
                return;
            }
            case NK::NamedRegexDecl:
                markUsesInRegex(static_cast<NamedRegexDecl*>(s)->pattern);
                return;
            case NK::EnumDecl:  walkExpr(static_cast<EnumDecl*>(s)->values.get()); return;
            case NK::UseStmt:   walkExpr(static_cast<UseStmt*>(s)->argExpr.get()); return;
            case NK::SubsetDecl: walkExpr(static_cast<SubsetDecl*>(s)->where.get()); return;
            default: return;
        }
    }

    // The default `.new` binds named args to PUBLIC attributes and silently
    // ignores the rest — a typo'd attribute name is invisible at runtime.
    // Warn when a literal named arg to `LocalClass.new(...)` matches no public
    // attribute. Fires only when construction is fully understood from this
    // file: the class and its whole in-file ancestry declare no custom
    // new/BUILD/TWEAK (any of which may consume arbitrary nameds), and every
    // parent/role is itself declared in this file.
    bool defaultNewAttrs(ClassDecl* c, std::set<std::string>& attrs, int depth = 0) {
        if (!c || depth > 8) return false;
        for (auto& m : c->methods)
            if (m->name == "new" || m->name == "BUILD" || m->name == "TWEAK") return false;
        for (auto& a : c->attrs) if (a.pub) attrs.insert(a.name);
        std::vector<std::string> supers;
        if (!c->parent.empty()) supers.push_back(c->parent);
        for (auto& p : c->extraParents) supers.push_back(p);
        for (auto& r : c->roles) supers.push_back(r);
        for (auto& s : supers) {
            auto it = classes.find(s);
            if (it == classes.end()) return false; // unknown ancestry — stand down
            if (!defaultNewAttrs(it->second, attrs, depth + 1)) return false;
        }
        return true;
    }

    void checkNewArgs(MethodCall* m) {
        if (m->method != "new" || m->methodExpr || m->meta || m->hyper || m->bang) return;
        // the invocant must be a bare type name — a NameTerm (`Cafe.new`), or
        // in some contexts a VarExpr / zero-arg Call
        std::string cls;
        if (m->inv && m->inv->kind == NK::NameTerm) {
            auto* n = static_cast<NameTerm*>(m->inv.get());
            if (n->ofType.empty()) cls = n->name;
        } else if (m->inv && m->inv->kind == NK::VarExpr) {
            auto* v = static_cast<VarExpr*>(m->inv.get());
            if (!v->declare) cls = v->name;
        } else if (m->inv && m->inv->kind == NK::Call) {
            auto* c = static_cast<Call*>(m->inv.get());
            if (!c->callee && c->args.empty()) cls = c->name;
        }
        if (cls.empty() || !std::isupper((unsigned char)cls[0])) return;
        auto it = classes.find(cls);
        if (it == classes.end() || it->second->isRole) return;
        std::set<std::string> attrs;
        if (!defaultNewAttrs(it->second, attrs)) return;
        for (auto& a : m->args) {
            if (!a || a->kind != NK::Pair) continue;
            auto* p = static_cast<PairExpr*>(a.get());
            if (p->keyExpr || p->quotedKey || p->key.empty()) continue;
            if (!attrs.count(p->key)) {
                std::string have;
                for (auto& s : attrs) { if (!have.empty()) have += " "; have += s; }
                warn(lineOf(p), "new-arg-matches-no-attribute",
                     "named argument '" + p->key + "' to " + cls +
                         ".new matches no public attribute" +
                         (have.empty() ? "" : " (has: " + have + ")") +
                         " — the default constructor silently ignores it");
            }
        }
    }

    void redundantReturn(std::vector<StmtPtr>& body) {
        if (body.empty()) return;
        Stmt* last = body.back().get();
        if (last->kind == NK::ReturnStmt)
            warn(last->line, "redundant-return",
                 "'return' as the final statement is redundant; the block's last "
                 "value is returned automatically", 'N');
    }
};

} // namespace

std::vector<LintFinding> lintProgram(Program& prog) {
    Linter L;
    // prepass: register in-file class/role declarations (top level and nested
    // in package bodies) so `.new` named-arg checks can see them regardless of
    // declaration-vs-use order
    std::function<void(std::vector<StmtPtr>&)> collect = [&](std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts)
            if (s->kind == NK::ClassDecl) {
                auto* c = static_cast<ClassDecl*>(s.get());
                L.classes.emplace(c->name, c); // first declaration wins
                collect(c->body);
            }
    };
    collect(prog.stmts);
    L.scopes.push_back({}); // unit scope
    L.walkStmts(prog.stmts);
    L.popScope();
    std::sort(L.out.begin(), L.out.end(), [](const LintFinding& a, const LintFinding& b) {
        if (a.line != b.line) return a.line < b.line;
        return a.rule < b.rule;
    });
    return L.out;
}

} // namespace rakupp
