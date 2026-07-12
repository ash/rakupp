#include "Codegen.h"
#include <functional>
#include <memory>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace rakupp {

std::vector<std::string> computePlaceholders(const std::vector<StmtPtr>& body); // Interpreter.cpp
bool isKnownTypeName(const std::string& n); // Interpreter.cpp

namespace {

[[noreturn]] void unsupported(const std::string& what) { throw CodegenError{what}; }

// Human-readable name for an AST node kind (for fallback messages).
std::string nkName(NK k) {
    switch (k) {
        case NK::ClassDecl:   return "a class/role/grammar declaration";
        case NK::EnumDecl:    return "an enum declaration";
        case NK::WhenStmt:    return "a `when` outside `given`";
        case NK::HashLit:     return "a hash literal `{ ... }`";
        case NK::SelfTerm:    return "`self`";
        case NK::SubstLit:    return "an `s///` substitution";
        case NK::SymbolicRef: return "a symbolic reference (::(\"…\")) — native locals have no runtime names";
        case NK::ChainExpr:   return "a chained comparison";
        case NK::Pair:        return "a pair";
        default:              return "an unsupported construct (NK " + std::to_string((int)k) + ")";
    }
}

// C++ string-literal escape.
std::string cesc(const std::string& s) {
    bool hasNul = s.find('\0') != std::string::npos;
    std::string o = "\"";
    bool afterHex = false; // a hex escape must not swallow a following hex-digit char
    for (unsigned char c : s) {
        bool hex = false;
        if (afterHex && std::isxdigit(c)) o += "\" \""; // break the literal: "\x0d" "e"
        if (c == '\\' || c == '"') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') o += "\\r";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\x%02x", c); o += b; hex = true; }
        else o += (char)c;
        afterHex = hex;
    }
    o += "\"";
    // an embedded NUL truncates char*-based construction — use the length-aware form
    if (hasNul) return "std::string(" + o + ", " + std::to_string(s.size()) + ")";
    return o;
}

// $foo / @foo -> v_foo (sigil dropped); non-identifier chars -> '_'.
// Injective byte encoding: alnum passes through, every other byte (incl. '_')
// becomes "_HH" (2 hex). So distinct Raku names never collide onto one C++
// identifier — `$a-b` (a_2db) and `$a_b` (a_5fb) stay separate, and `_` can't
// masquerade as an escaped byte.
static std::string mangleBody(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c)) o += (char)c;
        else { char b[4]; std::snprintf(b, sizeof b, "_%02x", c); o += b; }
    }
    return o;
}
std::string mangleVar(const std::string& name) {
    // keep the sigil (tagged) so `$x` / `@x` / `%x` / `&x` are distinct C++ names
    char sigil = (!name.empty() && (name[0] == '$' || name[0] == '@' || name[0] == '%' || name[0] == '&')) ? name[0] : 0;
    std::string body = sigil ? name.substr(1) : name;
    char tag = sigil == '@' ? 'a' : sigil == '%' ? 'h' : sigil == '&' ? 'c' : 's'; // sigilless → 's'
    return std::string("v_") + tag + mangleBody(body);
}
std::string mangleSub(const std::string& name) {
    return "u_" + mangleBody(name);
}

struct Codegen {
    std::ostringstream out;
    std::map<std::string, int> userSubs; // sub name -> arity (positional params)
    std::map<std::string, int> fastSubs; // -O: fixed-arity subs with direct Value params (name -> arity)
    bool optimize_ = false;              // -O codegen pass enabled
    std::set<std::string> enumKeys;      // enum value names (bound as globals)
    std::set<std::string> classNames;    // user class/role names (resolve as type objects)
    std::set<std::string> multiNames;    // names that are multi subs (dispatched at runtime)
    std::string self_;                   // C++ expr for `self` inside a method ("" outside)
    std::vector<std::string> topics;  // stack of C++ var names bound to $_
    int tmp = 0;
    std::string gensym(const char* p) { return std::string(p) + std::to_string(tmp++); }
    void line(int ind, const std::string& s) { out << std::string(ind * 4, ' ') << s << "\n"; }

    std::set<std::string> codeVars; // `my &name = …` seen so far — calls go through the Value
    int wcDepth = 0;               // nesting level of WhateverCode closures (0 = not in one)
    std::vector<int> wcArity;      // per-level count of `*` slots consumed

    // Emit statements produced by `emit` into a fresh buffer and return them.
    std::string capture(const std::function<void()>& emit) {
        std::ostringstream tmp;
        std::swap(out, tmp);   // out is now empty; tmp holds prior content
        emit();
        std::string body = out.str();
        std::swap(out, tmp);   // restore prior content to out
        return body;
    }

    static bool hasWhatever(Expr* e) {
        if (!e) return false;
        switch (e->kind) {
            case NK::Whatever: return true;
            case NK::Binary: { auto* b = static_cast<Binary*>(e); return hasWhatever(b->lhs.get()) || hasWhatever(b->rhs.get()); }
            case NK::Unary:  return hasWhatever(static_cast<Unary*>(e)->operand.get());
            case NK::Ternary: { auto* t = static_cast<Ternary*>(e); return hasWhatever(t->cond.get()) || hasWhatever(t->then.get()) || hasWhatever(t->els.get()); }
            // Only the invocant/callee is part of THIS whatever-curry; arguments are their
            // own closure scopes (e.g. `.grep(* %% 3)`), handled when each arg is emitted.
            case NK::MethodCall: return hasWhatever(static_cast<MethodCall*>(e)->inv.get());
            case NK::Call:       return hasWhatever(static_cast<Call*>(e)->callee.get());
            case NK::Index:      return hasWhatever(static_cast<Index*>(e)->base.get()); // *<key> / *[0]
            default: return false;
        }
    }

    // Expression in value position: a `*`-bearing expression becomes a WhateverCode closure.
    std::string exArg(Expr* e) {
        // A regex literal in argument position is the Regex object (not a $_ match).
        if (e->kind == NK::RegexLit)
            return "Value::regex(" + cesc(static_cast<RegexLit*>(e)->pattern) + ")";
        // The sequence op consumes `*` itself (seed closures / infinite endpoint) —
        // it is never a WhateverCode lambda.
        if (e->kind == NK::Binary) {
            auto* b = static_cast<Binary*>(e);
            if (b->op == "..." || b->op == "...^") return ex(e);
        }
        if (!hasWhatever(e)) return ex(e);
        wcDepth++; wcArity.push_back(0);
        std::string an = "__w" + std::to_string(wcDepth);
        std::string body = ex(e);
        int arity = wcArity.back();
        wcDepth--; wcArity.pop_back();
        std::string mk = "Value::closure([=](ValueList& " + an + ")->Value{ return " + body + "; })";
        if (arity <= 1) return mk;
        // multi-`*` lambda: the sequence op / sort reads the arity off the Code value
        return "([&]()->Value{ Value _c = " + mk + "; _c.code->isWhateverCode = true; "
               "_c.code->whateverArity = " + std::to_string(arity) + "; return _c; }())";
    }

    static bool isSlip(Expr* e) { return e->kind == NK::Unary && static_cast<Unary*>(e)->op == "|" && !static_cast<Unary*>(e)->postfix; }
    std::string argList(const std::vector<ExprPtr>& args) {
        std::string s;
        for (size_t i = 0; i < args.size(); i++) {
            if (i) s += ", ";
            // a |slip in a list literal splices via listToArray (rtSlipVal pre-spreads it)
            s += isSlip(args[i].get()) ? "rtSlipVal(" + ex(static_cast<Unary*>(args[i].get())->operand.get()) + ")"
                                       : exArg(args[i].get());
        }
        return s;
    }
    static bool identKey(const std::string& k) {
        if (k.empty() || !(std::isalpha((unsigned char)k[0]) || k[0] == '_')) return false;
        for (unsigned char c : k) if (!(std::isalnum(c) || c == '-' || c == '_' || c == '\'')) return false;
        return true;
    }
    // An argument expression: a syntactic `k => v` / `:k(v)` with an identifier key
    // is a NAMED argument (mirrors evalArgs); everything else is exArg.
    std::string emitArg(Expr* a) {
        if (a->kind == NK::Pair) {
            auto* pr = static_cast<PairExpr*>(a);
            if (!pr->keyExpr && identKey(pr->key)) {
                std::string val = pr->value ? exArg(pr->value.get()) : "Value::boolean(true)";
                return "rtNamedPair(" + cesc(pr->key) + ", " + val + ")";
            }
        }
        return exArg(a);
    }
    // Full ValueList expression for call arguments; |slips spread positionally
    // (arrays/ranges) or as named args (hashes), like the interpreter's evalArgs.
    std::string argsVL(const std::vector<ExprPtr>& args) {
        bool anySlip = false;
        for (auto& a : args) if (isSlip(a.get())) anySlip = true;
        if (!anySlip) {
            std::string s;
            for (size_t i = 0; i < args.size(); i++) { if (i) s += ", "; s += emitArg(args[i].get()); }
            return "ValueList{" + s + "}";
        }
        std::string o = "([&]()->ValueList{ ValueList __as;";
        for (auto& a : args) {
            if (isSlip(a.get())) o += " rtSpreadArg(__as, " + ex(static_cast<Unary*>(a.get())->operand.get()) + ", true);";
            else o += " __as.push_back(" + emitArg(a.get()) + ");";
        }
        return o + " return __as; }())";
    }

    // Expression in boolean context (if/while/ternary conditions). A plain
    // comparison emits a native-bool helper directly, skipping the Bool Value +
    // RT.boolify round-trip; anything else falls back to RT.boolify.
    std::string exBool(Expr* e) {
        if (e->kind == NK::Binary && !hasWhatever(e)) {
            auto* b = static_cast<Binary*>(e);
            static const std::map<std::string, std::string> cmp = {
                {"<", "rtLtB"}, {"<=", "rtLeB"}, {">", "rtGtB"}, {">=", "rtGeB"}, {"==", "rtEqB"}, {"!=", "rtNeB"}};
            auto it = cmp.find(b->op);
            if (it != cmp.end())
                return it->second + "(" + ex(b->lhs.get()) + ", " + ex(b->rhs.get()) + ")";
        }
        return "RT.boolify(" + ex(e) + ")";
    }

    // A lexical `sub f (…) { … }` inside a block becomes a native closure stored
    // in the runtime env, so calls (including self-recursive ones) resolve by name.
    struct BodyScope {
        Codegen* g;
        std::set<std::string> savedHoisted, savedSpecials, savedEnvSubs;
        int savedDepth;
        BodyScope(Codegen* g_, bool closure) : g(g_),
            savedHoisted(g_->hoisted), savedSpecials(g_->boundSpecials),
            savedEnvSubs(g_->envSubs), savedDepth(g_->loopDepth_) {
            g->loopDepth_ = 0;               // break/continue never cross a C++ function boundary
            if (!closure) { g->hoisted.clear(); g->boundSpecials.clear(); }
            // a closure inherits the lexical sets (copy-in); additions are discarded on exit
        }
        ~BodyScope() {
            g->hoisted = std::move(savedHoisted);
            g->boundSpecials = std::move(savedSpecials);
            g->envSubs = std::move(savedEnvSubs);
            g->loopDepth_ = savedDepth;
        }
    };

    // Register a statement list's lexical subs at block entry, before any other
    // statement runs — mirroring the interpreter's hoistSubs (forward calls and
    // recursion resolve through the runtime env). Names enter envSubs before any
    // body is emitted so mutually-recursive subs call each other via the env.
    void hoistLexicalSubs(const std::vector<StmtPtr>& stmts, int ind, bool programTop = false) {
        auto lexical = [&](Stmt* st) -> SubDecl* {
            if (st->kind != NK::SubDecl) return nullptr;
            auto* d = static_cast<SubDecl*>(st);
            if (d->name.empty() || d->isMethod) return nullptr;
            if (programTop && (userSubs.count(d->name) || multiNames.count(d->name))) return nullptr; // pre-pass hoisted
            return d;
        };
        for (auto& st : stmts)
            if (SubDecl* d = lexical(st.get())) {
                if (d->isMulti) unsupported("a nested multi sub");
                envSubs.insert(d->name);
            }
        for (auto& st : stmts)
            if (SubDecl* d = lexical(st.get()))
                line(ind, "RT.dynVarRef(" + cesc("&" + d->name) + ") = " + subClosure(d) + ";");
    }

    void collectClosureLocals(const std::vector<StmtPtr>& body, std::set<std::string>& out) {
        std::function<void(Expr*)> ex = [&](Expr* e) {
            if (!e) return;
            if (e->kind == NK::VarExpr) { auto* v = static_cast<VarExpr*>(e); if (v->declare && v->name.size() > 1) out.insert(v->name); return; }
            if (e->kind == NK::Assign) { auto* a = static_cast<Assign*>(e); ex(a->target.get()); ex(a->value.get()); return; }
            if (e->kind == NK::ListExpr) { for (auto& it : static_cast<ListExpr*>(e)->items) ex(it.get()); return; }
            if (e->kind == NK::Binary) { auto* b = static_cast<Binary*>(e); ex(b->lhs.get()); ex(b->rhs.get()); return; }
            if (e->kind == NK::Unary) { ex(static_cast<Unary*>(e)->operand.get()); return; }
            // do NOT descend into a nested BlockExpr — it has its own scope
        };
        std::function<void(Stmt*)> st = [&](Stmt* s) {
            if (!s) return;
            switch (s->kind) {
                case NK::ExprStmt: ex(static_cast<ExprStmt*>(s)->e.get()); break;
                case NK::IfStmt: { auto* f = static_cast<IfStmt*>(s); for (auto& br : f->branches) { ex(br.first.get()); collectClosureLocals(br.second->stmts, out); } if (f->elseBlock) collectClosureLocals(f->elseBlock->stmts, out); break; }
                case NK::WhileStmt: case NK::LoopStmt: case NK::RepeatStmt: case NK::ForStmt: case NK::Block: case NK::GivenStmt: case NK::WhenStmt: {
                    // loop/block scopes: their inner `my` doesn't leak, but a `my` here
                    // is still local to the closure body — collect conservatively.
                    if (s->kind == NK::Block) collectClosureLocals(static_cast<Block*>(s)->stmts, out);
                    break;
                }
                default: break;
            }
        };
        for (auto& s : body) st(s.get());
    }
    bool exprAssignsCaptured(Expr* e, const std::set<std::string>& local) {
        if (!e) return false;
        auto isCapturedTarget = [&](Expr* t) -> bool {
            if (t->kind != NK::VarExpr) return false;
            auto* v = static_cast<VarExpr*>(t);
            if (v->declare) return false;
            const std::string& n = v->name;
            if (n.size() < 2 || !(n[0] == '$' || n[0] == '@' || n[0] == '%')) return false;
            if (!(std::isalpha((unsigned char)n[1]) || n[1] == '_')) return false; // skip $*/$!//etc.
            return !local.count(n) && !topVars_.count(n); // captured enclosing local
        };
        switch (e->kind) {
            case NK::Assign: { auto* a = static_cast<Assign*>(e); if (a->op.size() && a->op.back() == '=' && a->op != "==" && a->op != "!=" && a->op != ">=" && a->op != "<=" && isCapturedTarget(a->target.get())) return true; return exprAssignsCaptured(a->value.get(), local); }
            case NK::Unary: { auto* u = static_cast<Unary*>(e); if ((u->op == "++" || u->op == "--") && isCapturedTarget(u->operand.get())) return true; return exprAssignsCaptured(u->operand.get(), local); }
            case NK::Binary: { auto* b = static_cast<Binary*>(e); return exprAssignsCaptured(b->lhs.get(), local) || exprAssignsCaptured(b->rhs.get(), local); }
            case NK::Ternary: { auto* t = static_cast<Ternary*>(e); return exprAssignsCaptured(t->cond.get(), local) || exprAssignsCaptured(t->then.get(), local) || exprAssignsCaptured(t->els.get(), local); }
            case NK::Call: { auto* c = static_cast<Call*>(e); for (auto& x : c->args) if (exprAssignsCaptured(x.get(), local)) return true; return false; }
            case NK::MethodCall: { auto* m = static_cast<MethodCall*>(e); if (m->mutate && isCapturedTarget(m->inv.get())) return true; if (exprAssignsCaptured(m->inv.get(), local)) return true; for (auto& x : m->args) if (exprAssignsCaptured(x.get(), local)) return true; return false; }
            case NK::ListExpr: { for (auto& x : static_cast<ListExpr*>(e)->items) if (exprAssignsCaptured(x.get(), local)) return true; return false; }
            case NK::Index: { auto* ix = static_cast<Index*>(e); return exprAssignsCaptured(ix->base.get(), local) || (ix->index && exprAssignsCaptured(ix->index.get(), local)); }
            default: return false;
        }
    }
    bool stmtAssignsCaptured(Stmt* s, const std::set<std::string>& local) {
        if (!s) return false;
        switch (s->kind) {
            case NK::ExprStmt: return exprAssignsCaptured(static_cast<ExprStmt*>(s)->e.get(), local);
            case NK::ReturnStmt: { auto* r = static_cast<ReturnStmt*>(s); return r->value && exprAssignsCaptured(r->value.get(), local); }
            case NK::IfStmt: { auto* f = static_cast<IfStmt*>(s); for (auto& br : f->branches) { if (exprAssignsCaptured(br.first.get(), local)) return true; for (auto& x : br.second->stmts) if (stmtAssignsCaptured(x.get(), local)) return true; } if (f->elseBlock) for (auto& x : f->elseBlock->stmts) if (stmtAssignsCaptured(x.get(), local)) return true; return false; }
            case NK::WhileStmt: case NK::LoopStmt: case NK::RepeatStmt: case NK::ForStmt: { auto* b = loopBodyOf(s); if (b) for (auto& x : b->stmts) if (stmtAssignsCaptured(x.get(), local)) return true; return false; }
            case NK::Block: { for (auto& x : static_cast<Block*>(s)->stmts) if (stmtAssignsCaptured(x.get(), local)) return true; return false; }
            case NK::GivenStmt: { auto* g = static_cast<GivenStmt*>(s); if (g->body) for (auto& x : g->body->stmts) if (stmtAssignsCaptured(x.get(), local)) return true; return false; }
            default: return false;
        }
    }
    Block* loopBodyOf(Stmt* s) {
        switch (s->kind) {
            case NK::WhileStmt: return static_cast<WhileStmt*>(s)->body.get();
            case NK::LoopStmt: return static_cast<LoopStmt*>(s)->body.get();
            case NK::RepeatStmt: return static_cast<RepeatStmt*>(s)->body.get();
            case NK::ForStmt: return static_cast<ForStmt*>(s)->body.get();
            default: return nullptr;
        }
    }
    // bail if this closure body assigns to a captured (enclosing-local) variable —
    // [=] const-captures it, so the emitted C++ would not compile / would be wrong.
    void checkClosureCapture(const std::vector<StmtPtr>& body, const std::set<std::string>& seedLocal) {
        std::set<std::string> local = seedLocal;
        collectClosureLocals(body, local);
        for (auto& s : body)
            if (stmtAssignsCaptured(s.get(), local))
                unsupported("a closure that mutates a captured local variable");
    }

    std::string subClosure(SubDecl* d) {
        BodyScope __bs{this, /*closure=*/false};
        { std::set<std::string> loc; for (auto& p : d->params) if (!p.name.empty()) loc.insert(p.name); checkClosureCapture(d->body, loc); }
        std::string body = capture([&]() {
            bindParams(d->params, 0, false);
            hoistLexicalSubs(d->body, 0);
            for (size_t i = 0; i < d->body.size(); i++) {
                Stmt* st = d->body[i].get();
                if (i + 1 == d->body.size() && st->kind == NK::ExprStmt)
                    line(0, "return " + exArg(static_cast<ExprStmt*>(st)->e.get()) + ";");
                else if (i + 1 == d->body.size() && (st->kind == NK::IfStmt || st->kind == NK::GivenStmt)) {
                    std::string rv = gensym("__rv");
                    line(0, "Value " + rv + " = Value::any();");
                    stmtValue(st, 0, rv);
                    line(0, "return " + rv + ";");
                }
                else stmt(st, 0);
            }
            line(0, "return Value::any();");
        });
        return "Value::closure([=](ValueList& __a)->Value{\n" + body + "})";
    }

    // A block `{ ... }` / pointy `-> $x { ... }` becomes a native closure.
    std::string emitBlockClosure(BlockExpr* be) {
        BodyScope __bs{this, /*closure=*/true};
        bool pushed = false; std::string topic;
        std::vector<std::string> phs = be->params.empty() ? computePlaceholders(be->body)
                                                          : std::vector<std::string>{};
        { std::set<std::string> loc(phs.begin(), phs.end()); for (auto& p : be->params) if (!p.name.empty()) loc.insert(p.name); checkClosureCapture(be->body, loc); }
        std::string body = capture([&]() {
            if (!phs.empty()) { // $^a/$^b placeholders bind positionally, in sorted order
                for (size_t k = 0; k < phs.size(); k++)
                    line(0, "Value " + mangleVar(phs[k]) + " = (__a.size() > " + std::to_string(k) +
                            " ? __a[" + std::to_string(k) + "] : Value::any());");
            }
            else if (be->params.empty()) {
                topic = gensym("v__t");
                line(0, "Value " + topic + " = (__a.size() > 0 ? __a[0] : Value::any());");
                topics.push_back(topic); pushed = true;
            } else {
                bindParams(be->params, 0, false); // full signature forms, same as sub bodies
            }
            hoistLexicalSubs(be->body, 0);
            for (size_t i = 0; i < be->body.size(); i++) {
                Stmt* s = be->body[i].get();
                if (i + 1 == be->body.size() && s->kind == NK::ExprStmt)
                    line(0, "return " + exArg(static_cast<ExprStmt*>(s)->e.get()) + ";");
                else stmt(s, 0);
            }
            line(0, "return Value::any();");
        });
        if (pushed) topics.pop_back();
        std::string mk = "Value::closure([=](ValueList& __a)->Value{\n" + body + "})";
        // 2+-ary blocks (pointy params or $^a/$^b) must advertise their arity so
        // sort/map/for feed them the right number of elements per call.
        size_t nPos = phs.size();
        if (nPos <= 1) for (auto& p : be->params) if (!p.named && !p.slurpy) nPos++;
        if (nPos <= 1) return mk;
        std::string names;
        for (size_t k = 0; k < nPos; k++) names += std::string(k ? ", " : "") + "\"$^" + std::string(1, char('a' + k)) + "\"";
        return "([&]()->Value{ Value _c = " + mk + "; _c.code->placeholders = {" + names + "}; return _c; }())";
    }

    bool stmtHasRedo(Stmt* s) {
        if (!s) return false;
        switch (s->kind) {
            case NK::WhileStmt: case NK::ForStmt: case NK::LoopStmt: case NK::RepeatStmt:
                return false; // a nested loop owns its own redo
            case NK::ExprStmt: return exprHasRedo(static_cast<ExprStmt*>(s)->e.get());
            case NK::IfStmt: {
                auto* f = static_cast<IfStmt*>(s);
                for (auto& br : f->branches)
                    if (exprHasRedo(br.first.get()) || stmtsHaveRedo(br.second->stmts)) return true;
                return f->elseBlock && stmtsHaveRedo(f->elseBlock->stmts);
            }
            case NK::Block: return stmtsHaveRedo(static_cast<Block*>(s)->stmts);
            case NK::GivenStmt: {
                auto* g = static_cast<GivenStmt*>(s);
                if (exprHasRedo(g->topic.get())) return true;
                if (g->body && stmtsHaveRedo(g->body->stmts)) return true;
                return g->elseBody && stmtsHaveRedo(g->elseBody->stmts);
            }
            case NK::WhenStmt: {
                auto* w = static_cast<WhenStmt*>(s);
                if (w->cond && exprHasRedo(w->cond.get())) return true;
                return w->body && stmtsHaveRedo(w->body->stmts);
            }
            case NK::ReturnStmt: { auto* r = static_cast<ReturnStmt*>(s); return r->value && exprHasRedo(r->value.get()); }
            default: return false;
        }
    }
    bool stmtsHaveRedo(const std::vector<StmtPtr>& v) {
        for (auto& st : v) if (stmtHasRedo(st.get())) return true;
        return false;
    }

    // ---- expressions: return a C++ expression string of type Value ----
    std::string ex(Expr* e) {
        switch (e->kind) {
            case NK::IntLit: {
                auto* n = static_cast<IntLit*>(e);
                if (!n->big.empty()) return "Value::bigint(BigInt::fromString(" + cesc(n->big) + "))";
                return "Value::integer(" + std::to_string(n->v) + "LL)";
            }
            case NK::NumLit: {
                auto* n = static_cast<NumLit*>(e);
                if (n->imaginary) {
                    std::ostringstream c; c.precision(17); c << "Value::complex(0, " << n->v << ")";
                    return c.str();
                }
                if (n->isRat) {
                    // function-local static: build the Rat once, share its immutable
                    // BigInt parts on every evaluation (hot-loop literals!)
                    std::ostringstream s;
                    s << "([]() -> const Value& { static const Value __r = ";
                    if (n->bigNum.empty()) s << "Value::rat(BigInt(" << n->ratNum << "LL), BigInt(" << n->ratDen << "LL))";
                    else s << "Value::rat(BigInt::fromString(\"" << n->bigNum << "\"), BigInt::fromString(\"" << n->bigDen << "\"))";
                    s << "; return __r; }())";
                    return s.str();
                }
                std::ostringstream s; s.precision(17); s << "Value::number(" << n->v << ")";
                return s.str();
            }
            case NK::StrLit:  return "Value::str(" + cesc(static_cast<StrLit*>(e)->v) + ")";
            case NK::BoolLit: return std::string("Value::boolean(") + (static_cast<BoolLit*>(e)->v ? "true" : "false") + ")";
            case NK::VarExpr: {
                auto* v = static_cast<VarExpr*>(e);
                if (v->name == "$_") {
                    if (!topics.empty()) return topics.back();
                    return "RT.dynVar(\"$_\")"; // the runtime topic (mainline $_)
                }
                if (v->name == "@*ARGS") return "RT.getArgs()";
                if (v->name.size() > 2 && (v->name[0] == '$' || v->name[0] == '@' || v->name[0] == '%')
                    && (v->name[1] == '!' || v->name[1] == '.')) {
                    if (self_.empty()) unsupported("attribute access outside a method");
                    return "rtAttrGet(" + self_ + ", " + cesc(v->name.substr(2)) + ")"; // $!x / @.y / %!z
                }
                if (v->name.size() && v->name[0] == '&') { // &sub : a reference to a routine
                    std::string nm = v->name.substr(1);
                    if (userSubs.count(nm))
                        return "Value::closure([](ValueList& __a)->Value{ return " + mangleSub(nm) + "(__a); })";
                    if (multiNames.count(nm))
                        return "Value::closure([](ValueList& __a)->Value{ return " + mangleSub(nm) + "(ValueList(__a)); })";
                    if (codeVars.count(nm)) return mangleVar(v->name); // `my &f = …`
                    if (envSubs.count(nm)) return "RT.dynVar(" + cesc(v->name) + ")"; // lexical sub
                    if (nm.rfind("infix:", 0) == 0 || nm.rfind("prefix:", 0) == 0 || nm.rfind("postfix:", 0) == 0) {
                        // &infix:<op> — an operator as a callable
                        std::string op = nm.substr(nm.find('<') + 1);
                        if (!op.empty() && op.back() == '>') op.pop_back();
                        return "Value::closure([=](ValueList& __a)->Value{ return applyArith(" + cesc(op) +
                               ", (__a.size()>0?__a[0]:Value::any()), (__a.size()>1?__a[1]:Value::any())); })";
                    }
                    // a builtin (say, is_even, …) — dispatch by name at runtime
                    return "Value::closure([=](ValueList& __a)->Value{ return RT.callBuiltin(" + cesc(nm) + ", __a); })";
                }
                if (v->name == "$?LINE") return "Value::integer(" + std::to_string(v->line) + ")";
                {
                    // deterministic magicals compile natively; state-dependent ones
                    // ($*EXECUTABLE/$*PROGRAM/$?FILE — need runtime paths the compiled
                    //  binary doesn't carry) fall back to the (correct) interpreter bundle.
                    static const std::set<std::string> dyn = {
                        "$*CWD","$*RAKU","$*PERL","$?RAKU","$?PERL","$*OUT","$*ERR","$*IN",
                        "$*DISTRO","$*KERNEL","$*VM","$*SPEC","$*TMPDIR"};
                    if (dyn.count(v->name)) return "RT.dynVar(" + cesc(v->name) + ")";
                }
                if ((v->name == "$!" || v->name == "$/") && boundSpecials.count(v->name))
                    return mangleVar(v->name); // bound as a parameter in this body
                if ((v->name.size() > 1 && v->name[1] == '*') || v->name == "$!" || v->name == "$/")
                    return "RT.dynVar(" + cesc(v->name) + ")"; // resolved from the live env at runtime
                if (v->name.size() > 1 && (v->name[1] == '?' || v->name[1] == '!'))
                    unsupported("special/dynamic variable '" + v->name + "'");
                if (v->name.size() > 1 && v->name[0] == '$' &&
                    std::all_of(v->name.begin() + 1, v->name.end(), [](unsigned char c) { return std::isdigit(c); }))
                    // $0/$1/… are positional captures of the current match ($/)
                    return "rtIndexGet(" + mangleVar("$/") + ", Value::integer(" + v->name.substr(1) + "LL), false)";
                return mangleVar(v->name); // scalars, @arrays and %hashes are all C++ Value locals
            }
            case NK::SelfTerm:
                if (self_.empty()) unsupported("`self` outside a method");
                return self_;
            case NK::Whatever:
                if (wcDepth) {
                    int k = wcArity.back()++;
                    std::string an = "__w" + std::to_string(wcDepth);
                    return "(" + an + ".size()>" + std::to_string(k) + "?" + an + "[" + std::to_string(k) + "]:Value::any())";
                }
                return "Value::whatever()";
            case NK::BlockExpr: return emitBlockClosure(static_cast<BlockExpr*>(e));
            case NK::Range: {
                auto* r = static_cast<RangeExpr*>(e);
                // integer literal endpoints keep the direct construction; anything else
                // goes through rtRangeVal so string ranges ('a'..'z') materialise via succ
                if (r->from->kind == NK::IntLit && r->to->kind == NK::IntLit)
                    return "Value::range((" + ex(r->from.get()) + ").toInt(), (" + ex(r->to.get()) + ").toInt(), "
                         + (r->exFrom ? "true" : "false") + ", " + (r->exTo ? "true" : "false") + ")";
                return "rtRangeVal(" + ex(r->from.get()) + ", " + ex(r->to.get()) + ", "
                     + (r->exFrom ? "true" : "false") + ", " + (r->exTo ? "true" : "false") + ")";
            }
            case NK::RegexLit: {
                std::string topic = topics.empty() ? "RT.dynVar(\"$_\")" : topics.back();
                return "RT.regexMatch((" + topic + ").toStr(), " + cesc(static_cast<RegexLit*>(e)->pattern) + ")";
            }
            case NK::Index: {
                auto* ix = static_cast<Index*>(e);
                if (!ix->adverb.empty()) {
                    if (ix->adverb.find('$') != std::string::npos) unsupported("a conditional ($var) index adverb");
                    std::string keyE = exArg(ix->index.get());
                    if (ix->adverb.find("delete") != std::string::npos) // mutates: needs an lvalue base
                        return "rtIndexAdverb(" + lvalueExpr(ix->base.get()) + ", " + keyE + ", "
                             + (ix->isHash ? "true" : "false") + ", " + cesc(ix->adverb) + ")";
                    return "([&]()->Value{ Value __b = " + ex(ix->base.get()) + "; return rtIndexAdverb(__b, " + keyE + ", "
                         + (ix->isHash ? "true" : "false") + ", " + cesc(ix->adverb) + "); }())";
                }
                if (!ix->isHash && ix->index && ix->index->kind == NK::Range) {
                    auto* r = static_cast<RangeExpr*>(ix->index.get());
                    if (r->to && r->to->kind == NK::Whatever && r->from && !hasWhatever(r->from.get()))
                        return "rtSliceFrom(" + ex(ix->base.get()) + ", (" + ex(r->from.get()) + ").toInt(), "
                             + (r->exFrom ? "true" : "false") + ")"; // @a[$i .. *]
                }
                std::string fn = hasWhatever(ix->index.get()) ? "RT.idxW" : "rtIndexGet"; // @a[*-1] / @a[*]
                return fn + "(" + ex(ix->base.get()) + ", " + ex(ix->index.get()) + ", "
                     + (ix->isHash ? "true" : "false") + ")";
            }
            case NK::NameTerm: {
                const std::string& n = static_cast<NameTerm*>(e)->name;
                if (n == "True")  return "Value::boolean(true)";
                if (n == "False") return "Value::boolean(false)";
                if (n == "Nil")   return "Value::nil()";
                if (n == "Inf" || n == "\xe2\x88\x9e") return "Value::number(INFINITY)"; // Inf / ∞
                if (n == "NaN") return "Value::number(NAN)";
                if (n == "pi" || n == "\xcf\x80")  return "Value::number(3.14159265358979323846)";
                if (n == "e")     return "Value::number(2.71828182845904523536)";
                if (n == "tau")   return "Value::number(6.28318530717958647692)";
                if (n == "rand")  return "Value::number(randDouble())";
                if (enumKeys.count(n)) return mangleVar(n); // enum value (bound as a global)
                if (topVars_.count(n))  return mangleVar(n); // sigilless top-level var/constant (a global)
                // user-declared code wins over type names, matching the interpreter's
                // env-first NameTerm resolution (a `sub Date {…}` calls the sub).
                if (userSubs.count(n))   return mangleSub(n) + "(ValueList{})";     // zero-arg sub call
                if (multiNames.count(n)) return mangleSub(n) + "(ValueList{})";     // zero-arg multi dispatch
                if (envSubs.count(n))    return "RT.callCallable(RT.dynVar(" + cesc("&" + n) + "), ValueList{})"; // lexical sub
                if (classNames.count(n)) return "Value::typeObj(" + cesc(n) + ")";  // a user class: a type object
                // anything else resolves at runtime like the interpreter's NameTerm:
                // env value, zero-arg &routine/builtin call, else a type object
                return "RT.rtNameTerm(" + cesc(n) + ")";
            }
            case NK::Ternary: {
                auto* t = static_cast<Ternary*>(e);
                return "(" + exBool(t->cond.get()) + " ? (" + ex(t->then.get()) + ") : (" + ex(t->els.get()) + "))";
            }
            case NK::Unary: {
                auto* u = static_cast<Unary*>(e);
                if (u->op.size() >= 3 && u->op.front() == '[' && u->op.back() == ']') // reduce metaop [+] [*] …
                    return "rtReduce(" + cesc(u->op.substr(1, u->op.size() - 2)) + ", " + exArg(u->operand.get()) + ")";
                if (u->op == "do" || u->op == "try") { // do { } / try { }  (value = last statement)
                    std::string body;
                    if (u->operand->kind == NK::BlockExpr) {
                        auto* be = static_cast<BlockExpr*>(u->operand.get());
                        body = capture([&]() {
                            for (size_t i = 0; i < be->body.size(); i++) {
                                Stmt* s = be->body[i].get();
                                if (i + 1 == be->body.size() && s->kind == NK::ExprStmt)
                                    line(0, "return " + exArg(static_cast<ExprStmt*>(s)->e.get()) + ";");
                                else stmt(s, 0);
                            }
                            line(0, "return Value::any();");
                        });
                    } else body = "return " + exArg(u->operand.get()) + ";\n";
                    if (u->op == "do") return "([&]()->Value{\n" + body + "}())";
                    return "([&]()->Value{ try { Value __r = ([&]()->Value{\n" + body + "}()); "
                           "RT.dynVarRef(\"$!\") = Value::nil(); return __r; } "
                           "catch (const RakuError& __e) { RT.dynVarRef(\"$!\") = RT.exceptionFor(__e); return Value::nil(); } }())";
                }
                if (u->op == "gather") { // gather { … take … } — probe-and-double lazy, like the interp
                    std::string body;
                    if (u->operand->kind == NK::BlockExpr) {
                        auto* be = static_cast<BlockExpr*>(u->operand.get());
                        body = capture([&]() { for (auto& s : be->body) stmt(s.get(), 0); });
                    } else body = exArg(u->operand.get()) + ";\n";
                    return "RT.rtGather(Value::closure([=](ValueList&)->Value{\n" + body + "return Value::any(); }))";
                }
                if (u->op == "next" || u->op == "last" || u->op == "redo") {
                    if (u->operand) unsupported("labelled/valued loop control in expression position");
                    // `$x > 3 and last` — throw the interpreter's control signal; the
                    // enclosing native loop body catches it (loopBody)
                    return u->op == "next" ? "rtThrowNext()"
                         : u->op == "last" ? "rtThrowLast()" : "rtThrowRedo()";
                }
                if (u->postfix) { // $x++ / $x-- as an expression: yield the old value
                    if (u->op == "i") return "RT.postfixIPub(" + ex(u->operand.get()) + ")"; // (2+3)i
                    if (u->op != "++" && u->op != "--") unsupported("postfix " + u->op);
                    std::string delta = u->op == "++" ? "1" : "-1";
                    return "([&]()->Value{ Value& _r=" + lvalueExpr(u->operand.get()) +
                           "; Value _o=_r; _r=applyArith(\"+\", _o, Value::integer(" + delta + ")); return _o; }())";
                }
                if (u->op == "++" || u->op == "--") { // prefix: yield the new value
                    std::string delta = u->op == "++" ? "1" : "-1";
                    return "([&]()->Value{ Value& _r=" + lvalueExpr(u->operand.get()) +
                           "; _r=applyArith(\"+\", _r, Value::integer(" + delta + ")); return _r; }())";
                }
                if (u->op == "quietly") { // suppress warn() output in the operand
                    std::string body = u->operand->kind == NK::BlockExpr
                        ? "RT.callCallable(" + emitBlockClosure(static_cast<BlockExpr*>(u->operand.get())) + ", ValueList{})"
                        : exArg(u->operand.get());
                    return "([&]()->Value{ RT.quietDepth_++; try { Value __q = " + body +
                           "; RT.quietDepth_--; return __q; } catch (...) { RT.quietDepth_--; throw; } }())";
                }
                std::string x = ex(u->operand.get());
                if (u->op == "!" || u->op == "not") return "Value::boolean(!RT.boolify(" + x + "))";
                if (u->op == "?")  return "Value::boolean(RT.boolify(" + x + "))";
                if (u->op == "-")  return "applyArith(\"-\", Value::integer(0), " + x + ")";
                if (u->op == "+")  return "applyArith(\"+\", Value::integer(0), " + x + ")";
                if (u->op == "~")  return "Value::str((" + x + ").toStr())";
                if (u->op == "^")  return "Value::range(0, (" + x + ").toInt(), false, true)"; // ^N = 0..^N
                if (u->op == "ctx%") return "rtCoerceHash(" + x + ")"; // %(...) hash composer
                if (u->op == "ctx$") // $(...) — an array becomes one non-flattening item
                    return "([&]()->Value{ Value _v = " + x + "; if (_v.t==VT::Array) _v.itemized=true; return _v; }())";
                if (u->op == "ctx@") // @(...) — one-level list context
                    return "rtArrayVal(" + x + ")";
                if (u->op == "|") return "rtSlipShallow(" + x + ")"; // |x in value position: one-level marker
                unsupported("prefix operator '" + u->op + "'");
            }
            case NK::Binary: {
                auto* b = static_cast<Binary*>(e);
                // smartmatch against a regex literal: `$x ~~ /.../`
                if ((b->op == "~~" || b->op == "!~~") && b->rhs->kind == NK::RegexLit) {
                    std::string m = "RT.regexMatch((" + ex(b->lhs.get()) + ").toStr(), "
                                  + cesc(static_cast<RegexLit*>(b->rhs.get())->pattern) + ")";
                    return b->op == "~~" ? m : "Value::boolean(!(" + m + ").truthy())";
                }
                if (b->op == "xx") {
                    // list repetition thunks its left side (re-evaluate per copy)
                    std::string L = ex(b->lhs.get()), R = ex(b->rhs.get());
                    return "([&]()->Value{ long long _n=(" + R + ").toInt(); Value _o=Value::array(); _o.isList=true; "
                           "for(long long _i=0;_i<_n;_i++) _o.arr->push_back(" + L + "); return _o; }())";
                }
                if (b->op == "..." || b->op == "...^") {
                    // sequence operator: seeds emit per-element (a `* + *` seed becomes a
                    // generator closure); a bare `*`/Inf endpoint marks the sequence infinite.
                    std::string L;
                    if (b->lhs->kind == NK::ListExpr) {
                        auto* le = static_cast<ListExpr*>(b->lhs.get());
                        std::string items;
                        for (size_t i = 0; i < le->items.size(); i++) {
                            if (i) items += ", ";
                            items += exArg(le->items[i].get());
                        }
                        L = "listToArray({" + items + "})";
                    }
                    else L = exArg(b->lhs.get());
                    std::string R = b->rhs->kind == NK::Whatever ? "Value::whatever()" : exArg(b->rhs.get());
                    return "RT.seqOp(" + L + ", " + R + ", " + (b->op == "...^" ? "true" : "false") + ")";
                }
                if (b->op.size() > 1 && b->op[0] == 'R' && !std::isalnum((unsigned char)b->op[1])) {
                    // reverse metaop `a R/ b` == `b / a`
                    std::string L = ex(b->lhs.get()), R = ex(b->rhs.get());
                    return "applyArith(" + cesc(b->op.substr(1)) + ", " + R + ", " + L + ")";
                }
                std::string L = ex(b->lhs.get()), R = ex(b->rhs.get());
                if (b->op == "&&" || b->op == "and")
                    return "([&]()->Value{ Value _a=(" + L + "); return RT.boolify(_a)?(" + R + "):_a; }())";
                if (b->op == "||" || b->op == "or")
                    return "([&]()->Value{ Value _a=(" + L + "); return RT.boolify(_a)?_a:(" + R + "); }())";
                if (b->op == "//") // defined-or: Nil/Any/Type (incl. Failure) are undefined
                    return "([&]()->Value{ Value _a=(" + L + "); return (_a.t==VT::Nil||_a.t==VT::Any||_a.t==VT::Type)?(" + R + "):_a; }())";
                if (std::string f = fastBin(b->op); !f.empty()) return f + "(" + L + ", " + R + ")"; // -O
                return "applyArith(" + cesc(b->op) + ", " + L + ", " + R + ")";
            }
            case NK::InterpStr: {
                auto* s = static_cast<InterpStr*>(e);
                if (s->parts.empty()) return "Value::str(\"\")";
                std::string acc;
                for (size_t i = 0; i < s->parts.size(); i++) {
                    if (i) acc += " + ";
                    acc += "(" + ex(s->parts[i].get()) + ").toStr()";
                }
                return "Value::str(" + acc + ")";
            }
            case NK::Call: {
                auto* c = static_cast<Call*>(e);
                bool slip = false;
                for (auto& a : c->args) if (isSlip(a.get())) slip = true;
                std::string vl = argsVL(c->args);
                if (c->callee) return "RT.callCallable(" + ex(c->callee.get()) + ", " + vl + ")";
                if (codeVars.count(c->name)) // a `my &name = …` variable called by bare name
                    return "RT.callCallable(" + mangleVar("&" + c->name) + ", " + vl + ")";
                if (envSubs.count(c->name))  // a lexical sub registered in the runtime env
                    return "RT.callCallable(RT.dynVar(" + cesc("&" + c->name) + "), " + vl + ")";
                if (multiNames.count(c->name)) return mangleSub(c->name) + "(" + vl + ")"; // multi dispatcher
                if (userSubs.count(c->name)) {
                    // -O: call the direct-Value overload when the arity/args line up
                    auto fit = fastSubs.find(c->name);
                    if (!slip && fit != fastSubs.end() && (int)c->args.size() == fit->second && simpleArgs(c->args))
                        return mangleSub(c->name) + "(" + argList(c->args) + ")";
                    return mangleSub(c->name) + "(" + vl + ")"; // boxed adapter
                }
                return "RT.callBuiltin(" + cesc(c->name) + ", " + vl + ")";
            }
            case NK::MethodCall: {
                auto* m = static_cast<MethodCall*>(e);
                if (m->maybe) unsupported("method-call form (.?)");
                if (m->hyper) {
                    if (m->mutate) unsupported(">>.= hyper-mutate");
                    return "rtHyperMethod(RT, " + ex(m->inv.get()) + ", " + cesc(m->method) + ", " + argsVL(m->args) + ")";
                }
                std::string name = m->meta ? "^" + m->method : m->method;
                if (m->mutate) { // $x .= meth : rebind the invocant to the result
                    if (m->inv->kind != NK::VarExpr && m->inv->kind != NK::Index) unsupported(".= on this invocant");
                    return "([&]()->Value{ Value& __r = " + lvalueExpr(m->inv.get()) + "; __r = RT.methodCall(__r, "
                         + cesc(name) + ", " + argsVL(m->args) + "); return __r; }())";
                }
                return "RT.methodCall(" + ex(m->inv.get()) + ", " + cesc(name) + ", " + argsVL(m->args) + ")";
            }
            case NK::Assign: {
                auto* a = static_cast<Assign*>(e);
                if (a->target->kind == NK::VarExpr && static_cast<VarExpr*>(a->target.get())->declare) {
                    auto* v = static_cast<VarExpr*>(a->target.get());
                    if (v->name.size() <= 1) // `my $ = expr` — anonymous: the value passes through
                        return "(" + coerceFor(a->target.get(), exArg(a->value.get())) + ")";
                    if (hoisted.count(v->name)) // pre-declared by hoistExprDecls
                        return "(" + mangleVar(v->name) + " = " + coerceFor(a->target.get(), exArg(a->value.get())) + ")";
                    unsupported("declaration used as a sub-expression");
                }
                return "(" + assign(a) + ")"; // assignment yields the assigned value
            }
            case NK::ChainExpr: {
                auto* ch = static_cast<ChainExpr*>(e);
                std::string binds, cond;
                for (size_t k = 0; k < ch->operands.size(); k++)
                    binds += "Value _c" + std::to_string(k) + "=(" + ex(ch->operands[k].get()) + "); ";
                for (size_t k = 0; k < ch->ops.size(); k++) {
                    if (k) cond += " && ";
                    cond += "applyArith(" + cesc(ch->ops[k]) + ", _c" + std::to_string(k) + ", _c" + std::to_string(k + 1) + ").truthy()";
                }
                return "([&]()->Value{ " + binds + "return Value::boolean(" + cond + "); }())";
            }
            case NK::Pair: {
                auto* p = static_cast<PairExpr*>(e);
                std::string key = p->keyExpr ? "(" + ex(p->keyExpr.get()) + ").toStr()" : cesc(p->key);
                std::string val = p->value ? exArg(p->value.get()) : "Value::boolean(true)"; // :g  ==  g => True
                return "Value::pair(" + key + ", " + val + ")";
            }
            case NK::ListExpr: return "listToArray({" + argList(static_cast<ListExpr*>(e)->items) + "})";
            case NK::HashLit:  return "rtHashLit({" + argList(static_cast<HashLit*>(e)->items) + "})";
            case NK::ArrayLit: return "listToArray({" + argList(static_cast<ArrayLit*>(e)->items) + "})";
            default: unsupported(nkName(e->kind));
        }
    }

    // ---- statements ----
    std::vector<Block*> topLevelEnds; // top-level END phasers, run at program end
    int leaveCtr_ = 0;                // unique names for LEAVE scope guards
    std::set<std::string> topVars_;   // top-level `my` vars hoisted to C++ globals
    bool atTopLevel_ = false;         // emitting the mainline (not a sub body)

    void emitPhaserBody(Block* b, int ind) {
        line(ind, "{");
        for (auto& s : b->stmts) stmt(s.get(), ind + 1);
        line(ind, "}");
    }

    // Emit a statement sequence, honouring phasers: entry phasers first, the
    // body, then exit phasers (reverse); top-level END phasers are deferred.
    // A CATCH handler: bind $_ to the exception and run its when/default chain
    // (first match wins); unmatched exceptions are swallowed, matching rakupp.
    void emitCatchHandler(Block* cb, int ind) {
        std::string exv = gensym("v__ex"), done = gensym("__cdone");
        line(ind, "Value " + exv + " = __e.payload;");
        topics.push_back(exv);
        for (auto& s : cb->stmts) {
            if (s->kind == NK::WhenStmt) {
                auto* w = static_cast<WhenStmt*>(s.get());
                if (w->isDefault) line(ind, "{");
                else line(ind, "if (applyArith(\"~~\", " + exv + ", " + ex(w->cond.get()) + ").truthy()) {");
                block(w->body.get(), ind + 1);
                line(ind + 1, "goto " + done + ";");
                line(ind, "}");
            } else stmt(s.get(), ind);
        }
        topics.pop_back();
        line(ind, done + ": ;");
    }

    // Emit a statement sequence, honouring phasers (entry first, exit reverse,
    // top-level END deferred) and an embedded CATCH block (wraps the body).
    void emitSeq(const std::vector<StmtPtr>& stmts, int ind, bool topLevel = false) {
        std::vector<Block*> pre, post;
        std::vector<Stmt*> regular;
        Block* catchBlk = nullptr;
        for (auto& s : stmts) {
            if (s->kind == NK::Block) {
                auto* b = static_cast<Block*>(s.get());
                if (b->isCatch) { catchBlk = b; continue; }
                const std::string& ph = b->phaser;
                if (ph == "BEGIN" || ph == "CHECK" || ph == "INIT" || ph == "ENTER" || ph == "FIRST") { pre.push_back(b); continue; }
                if (ph == "LEAVE" || ph == "KEEP" || ph == "UNDO") { post.push_back(b); continue; }
                if (ph == "END") { if (!topLevel) unsupported("a nested END phaser"); topLevelEnds.push_back(b); continue; }
                if (!ph.empty()) unsupported("a " + ph + " phaser");
            }
            regular.push_back(s.get());
        }
        for (Block* b : pre) emitPhaserBody(b, ind);
        hoistLexicalSubs(stmts, ind, topLevel);
        // A CATCH guards its enclosing block (the mainline is the UNIT block).
        if (catchBlk) {
            line(ind, "try {");
            for (Stmt* s : regular) stmt(s, ind + 1);
            line(ind, "} catch (const RakuError& __e) {");
            emitCatchHandler(catchBlk, ind + 1);
            line(ind, "}");
        } else {
            for (Stmt* s : regular) stmt(s, ind);
        }
        for (auto it = post.rbegin(); it != post.rend(); ++it) emitPhaserBody(*it, ind);
    }

    void block(Block* b, int ind) { emitSeq(b->stmts, ind); }
    void stmts(const std::vector<StmtPtr>& v, int ind) { for (auto& s : v) stmt(s.get(), ind); }

    std::set<std::string> hoisted; // expression-position `my` names pre-declared in this body
    std::set<std::string> boundSpecials; // $/ or $! bound as a parameter in the current body (locals win over RT.dynVar)
    std::set<std::string> envSubs;  // lexical subs registered in the runtime env (`sub f {…}` inside a block)
    int loopDepth_ = 0;             // native loops enclosing the emission point IN THIS function body
    void collectExprDecls(Expr* e, std::vector<std::string>& out, bool root = true) {
        if (!e) return;
        if (e->kind == NK::Assign) {
            auto* a = static_cast<Assign*>(e);
            if (!root && a->target->kind == NK::VarExpr) {
                auto* v = static_cast<VarExpr*>(a->target.get());
                if (v->declare && v->name.size() > 1) out.push_back(v->name);
            }
            collectExprDecls(a->value.get(), out, false);
            return;
        }
        switch (e->kind) {
            case NK::Binary: { auto* b = static_cast<Binary*>(e); collectExprDecls(b->lhs.get(), out, false); collectExprDecls(b->rhs.get(), out, false); break; }
            case NK::Unary: collectExprDecls(static_cast<Unary*>(e)->operand.get(), out, false); break;
            case NK::Ternary: { auto* t = static_cast<Ternary*>(e); collectExprDecls(t->cond.get(), out, false); collectExprDecls(t->then.get(), out, false); collectExprDecls(t->els.get(), out, false); break; }
            case NK::Call: { auto* c = static_cast<Call*>(e); for (auto& x : c->args) collectExprDecls(x.get(), out, false); if (c->callee) collectExprDecls(c->callee.get(), out, false); break; }
            case NK::MethodCall: { auto* m = static_cast<MethodCall*>(e); collectExprDecls(m->inv.get(), out, false); for (auto& x : m->args) collectExprDecls(x.get(), out, false); break; }
            case NK::ListExpr: for (auto& x : static_cast<ListExpr*>(e)->items) collectExprDecls(x.get(), out, false); break;
            case NK::ArrayLit: for (auto& x : static_cast<ArrayLit*>(e)->items) collectExprDecls(x.get(), out, false); break;
            case NK::Index: { auto* ix = static_cast<Index*>(e); collectExprDecls(ix->base.get(), out, false); if (ix->index) collectExprDecls(ix->index.get(), out, false); break; }
            case NK::Pair: { auto* pr = static_cast<PairExpr*>(e); if (pr->value) collectExprDecls(pr->value.get(), out, false); break; }
            default: break; // no descent into BlockExpr — its body has its own scope
        }
    }
    void hoistExprDecls(Expr* e, int ind) {
        std::vector<std::string> names;
        collectExprDecls(e, names);
        for (auto& n : names)
            if (hoisted.insert(n).second)
                line(ind, "Value " + mangleVar(n) + " = Value::any(); // hoisted `my` from expression position");
    }

    void stmt(Stmt* s, int ind) {
        if (s->kind == NK::ExprStmt) hoistExprDecls(static_cast<ExprStmt*>(s)->e.get(), ind);
        switch (s->kind) {
            case NK::UseStmt: { // `use Test` / `use lib '…'` / `use Module` — runtime effects
                auto* u = static_cast<UseStmt*>(s);
                std::string arg = u->argExpr ? "(" + ex(u->argExpr.get()) + ").toStr()" : cesc(u->arg);
                line(ind, "RT.rtUse(" + cesc(u->module) + ", " + arg + ");");
                return;
            }
            case NK::SubDecl: return; // registered by hoistLexicalSubs at block entry
            case NK::EmptyStmt: case NK::EnumDecl:
            case NK::ClassDecl: return; // subs/enums/classes emitted separately
            case NK::ExprStmt: {
                Expr* e = static_cast<ExprStmt*>(s)->e.get();
                // `my Foo $x .= new(…)` — declare + mutate: the invocant starts as the type object
                if (e->kind == NK::MethodCall) {
                    auto* mc = static_cast<MethodCall*>(e);
                    if (mc->mutate && !mc->hyper && mc->inv->kind == NK::VarExpr &&
                        static_cast<VarExpr*>(mc->inv.get())->declare) {
                        auto* v = static_cast<VarExpr*>(mc->inv.get());
                        std::string init = v->declType.empty() ? "Value::any()"
                                         : "Value::typeObj(" + cesc(v->declType) + ")";
                        std::string name = mc->meta ? "^" + mc->method : mc->method;
                        line(ind, "Value " + mangleVar(v->name) + " = RT.methodCall(" + init + ", "
                                + cesc(name) + ", " + argsVL(mc->args) + ");");
                        return;
                    }
                }
                // my ($a, $b) = LIST — declaration-list assignment: declare each, bind from the flat RHS
                if (e->kind == NK::Assign && static_cast<Assign*>(e)->op == "=" &&
                    static_cast<Assign*>(e)->target->kind == NK::ListExpr) {
                    auto* a = static_cast<Assign*>(e);
                    auto* lst = static_cast<ListExpr*>(a->target.get());
                    // only all-scalar targets: `my ($a, $b, $c) = …` (a slurpy @rest tail
                    // has different semantics — leave that to the bundling fallback)
                    bool allDecl = !lst->items.empty();
                    for (auto& it : lst->items) {
                        if (it->kind != NK::VarExpr) { allDecl = false; break; }
                        auto* v = static_cast<VarExpr*>(it.get());
                        if (!v->declare || v->name.empty() || v->name[0] != '$') { allDecl = false; break; }
                    }
                    if (allDecl) {
                        std::string tmp = gensym("__la");
                        line(ind, "Value " + tmp + " = rtArrayVal(" + exArg(a->value.get()) + ");");
                        for (size_t k = 0; k < lst->items.size(); k++) {
                            auto* v = static_cast<VarExpr*>(lst->items[k].get());
                            line(ind, "Value " + mangleVar(v->name) + " = rtIndexGet(" + tmp +
                                      ", Value::integer(" + std::to_string(k) + "), false);");
                        }
                        return;
                    }
                }
                if (e->kind == NK::Assign) { line(ind, assign(static_cast<Assign*>(e)) + ";"); return; } // `my $x = ..` / `$x = ..`
                if (e->kind == NK::VarExpr && static_cast<VarExpr*>(e)->declare) { // bare `my $x;` / `my @a;` / `my %h;`
                    const std::string& nm = static_cast<VarExpr*>(e)->name;
                    if (atTopLevel_ && topVars_.count(nm)) { line(ind, "; // " + nm + " is a global"); return; }
                    char sigil = nm.empty() ? '$' : nm[0];
                    std::string def = sigil == '@' ? "Value::array()" : sigil == '%' ? "Value::makeHash()" : "Value::any()";
                    line(ind, "Value " + mangleVar(nm) + " = " + def + ";");
                    return;
                }
                // bare declaration list `my ($x, $y, $k);` — declare each, no value
                if (e->kind == NK::ListExpr) {
                    auto* le = static_cast<ListExpr*>(e);
                    bool allDecl = !le->items.empty();
                    for (auto& it : le->items)
                        if (it->kind != NK::VarExpr || !static_cast<VarExpr*>(it.get())->declare)
                            { allDecl = false; break; }
                    if (allDecl) {
                        for (auto& it : le->items) {
                            const std::string& nm = static_cast<VarExpr*>(it.get())->name;
                            if (atTopLevel_ && topVars_.count(nm)) continue; // global
                            char sigil = nm.empty() ? '$' : nm[0];
                            std::string def = sigil == '@' ? "Value::array()" : sigil == '%' ? "Value::makeHash()" : "Value::any()";
                            line(ind, "Value " + mangleVar(nm) + " = " + def + ";");
                        }
                        return;
                    }
                }
                line(ind, ex(e) + ";");
                return;
            }
            case NK::VarDecl: {
                auto* d = static_cast<VarDecl*>(s);
                if (d->names.size() != 1) { // my ($a, $b) = LIST
                    if (!d->init) unsupported("multi-variable declaration without initializer");
                    std::string tmp = gensym("__d");
                    line(ind, "Value " + tmp + " = rtArrayVal(" + exArg(d->init.get()) + ");");
                    for (size_t k = 0; k < d->names.size(); k++)
                        line(ind, "Value " + mangleVar(d->names[k]) + " = rtIndexGet(" + tmp +
                                  ", Value::integer(" + std::to_string(k) + "), false);");
                    return;
                }
                char sigil = d->names[0].empty() ? '$' : d->names[0][0];
                std::string init;
                if (d->init) init = sigil == '@' ? "rtArrayVal(" + exArg(d->init.get()) + ")" : exArg(d->init.get());
                else if (sigil == '@') init = "Value::array()";
                else if (sigil == '%') init = "Value::makeHash()";
                else init = "Value::any()";
                line(ind, "Value " + mangleVar(d->names[0]) + " = " + init + ";");
                return;
            }
            case NK::ReturnStmt: {
                auto* r = static_cast<ReturnStmt*>(s);
                line(ind, "return " + (r->value ? exArg(r->value.get()) : std::string("Value::any()")) + ";");
                return;
            }
            case NK::LastStmt: {
                const std::string& lb = static_cast<LastStmt*>(s)->target;
                if (!lb.empty()) line(ind, "rtThrowLast(" + cesc(lb) + ");");
                else line(ind, loopDepth_ ? "break;" : "rtThrowLast();");
                return;
            }
            case NK::NextStmt: {
                const std::string& lb = static_cast<NextStmt*>(s)->target;
                if (!lb.empty()) line(ind, "rtThrowNext(" + cesc(lb) + ");");
                else line(ind, loopDepth_ ? "continue;" : "rtThrowNext();");
                return;
            }
            case NK::Block: {
                auto* b = static_cast<Block*>(s);
                // LEAVE {…} maps exactly to a C++ scope guard (runs on any exit,
                // exceptions included)
                if (b->phaser == "LEAVE") {
                    std::string g = "__leave" + std::to_string(leaveCtr_++);
                    line(ind, "struct " + g + "_t { std::function<void()> f; ~" + g + "_t(){ try { f(); } catch (...) {} } } " + g + "{[&]{");
                    block(b, ind + 1);
                    line(ind, "}};");
                    return;
                }
                if (b->isCatch || !b->phaser.empty()) unsupported("phaser / CATCH block");
                line(ind, "{"); block(b, ind + 1); line(ind, "}");
                return;
            }
            case NK::IfStmt:   ifStmt(static_cast<IfStmt*>(s), ind); return;
            case NK::WhileStmt: {
                auto* w = static_cast<WhileStmt*>(s);
                if (!w->var.empty()) unsupported("while EXPR -> $x");
                std::string c = exBool(w->cond.get());
                line(ind, "while (" + (w->isUntil ? "!" + c : c) + ") {");
                loopBody(w->body.get(), ind + 1, w->label); line(ind, "}");
                return;
            }
            case NK::RepeatStmt: {
                auto* r = static_cast<RepeatStmt*>(s);
                std::string c = exBool(r->cond.get());
                line(ind, "do {"); loopBody(r->body.get(), ind + 1, r->label);
                line(ind, "} while (" + (r->isUntil ? "!" + c : c) + ");");
                return;
            }
            case NK::LoopStmt: {
                auto* l = static_cast<LoopStmt*>(s);
                line(ind, "{");
                if (l->init) line(ind + 1, ex(l->init.get()) + ";");
                std::string c = l->cond ? exBool(l->cond.get()) : "true";
                line(ind + 1, "for (; " + c + "; " + (l->incr ? ex(l->incr.get()) : std::string()) + ") {");
                loopBody(l->body.get(), ind + 2, l->label);
                line(ind + 1, "}");
                line(ind, "}");
                return;
            }
            case NK::NamedRegexDecl: { // my regex NAME { … } — register with the embedded engine
                auto* nr = static_cast<NamedRegexDecl*>(s);
                line(ind, "RT.registerNamedRegex(" + cesc(nr->name) + ", " + cesc(nr->pattern) + ", " + cesc(nr->kind) + ");");
                return;
            }
            case NK::ForStmt:  forStmt(static_cast<ForStmt*>(s), ind); return;
            case NK::GivenStmt: givenStmt(static_cast<GivenStmt*>(s), ind); return;
            default: unsupported(nkName(s->kind));
        }
    }

    // A C++ lvalue expression (Value& / assignable) for a variable or index target.
    std::string lvalueExpr(Expr* e) {
        if (e->kind == NK::VarExpr) {
            auto* v = static_cast<VarExpr*>(e);
            if (v->name.size() > 2 && (v->name[0] == '$' || v->name[0] == '@' || v->name[0] == '%')
                && (v->name[1] == '!' || v->name[1] == '.')) { // $!x = .. / @!y = ..
                if (self_.empty()) unsupported("attribute assignment outside a method");
                return "rtAttrRef(" + self_ + ", " + cesc(v->name.substr(2)) + ")";
            }
            if ((v->name == "$!" || v->name == "$/") && boundSpecials.count(v->name))
                return mangleVar(v->name);
            if ((v->name.size() > 1 && v->name[1] == '*' && v->name != "@*ARGS") || v->name == "$!" || v->name == "$/")
                return "RT.dynVarRef(" + cesc(v->name) + ")";
            if (v->name == "$_")
                return topics.empty() ? "RT.dynVarRef(\"$_\")" : topics.back();
            if (v->name == "@*ARGS" || (v->name.size() && v->name[0] == '&') ||
                (v->name.size() > 1 && v->name[1] == '?'))
                unsupported("assignment to '" + v->name + "'");
            return mangleVar(v->name);
        }
        if (e->kind == NK::Index) {
            auto* ix = static_cast<Index*>(e);
            if (!ix->adverb.empty()) unsupported("index adverb on assignment");
            // nested indices chain: @g[$r][$c] = v → rtIndexRef(rtIndexRef(v_g, r), c)
            // (rtIndexRef returns an autovivifying Value&, so the chain is natural)
            if (ix->base->kind != NK::VarExpr && ix->base->kind != NK::Index)
                unsupported("assignment to nested index");
            return "rtIndexRef(" + lvalueExpr(ix->base.get()) + ", " + ex(ix->index.get()) + ", "
                 + (ix->isHash ? "true" : "false") + ")";
        }
        if (e->kind == NK::MethodCall) { // $obj.accessor = v (rw accessors; RO check at runtime)
            auto* mc = static_cast<MethodCall*>(e);
            if (!mc->mutate && !mc->hyper && !mc->meta && mc->args.empty())
                return "RT.accessorRef(" + lvalueExpr(mc->inv.get()) + ", " + cesc(mc->method) + ")";
        }
        unsupported("assignment to this target");
    }

    // Assigning a list to an @-array materializes a fresh (bracket-gisting) Array.
    std::string coerceFor(Expr* tgt, const std::string& rhs) {
        if (tgt->kind == NK::VarExpr) {
            const std::string& n = static_cast<VarExpr*>(tgt)->name;
            if (!n.empty() && n[0] == '@') return "rtArrayVal(" + rhs + ")";
            if (!n.empty() && n[0] == '%') return "rtCoerceHash(" + rhs + ")"; // my %h = a=>1,…
        }
        return rhs;
    }

    std::string assign(Assign* a) {
        Expr* tgt = a->target.get();
        if (tgt->kind == NK::VarExpr && static_cast<VarExpr*>(tgt)->declare) { // `my $x = ..`
            const std::string& nm = static_cast<VarExpr*>(tgt)->name;
            if (nm.size() > 1 && nm[1] == '*') // `my $*X = ..`: dynamics live in the runtime env
                return "RT.dynVarRef(" + cesc(nm) + ") = " + coerceFor(tgt, exArg(a->value.get()));
            if (nm.size() > 1 && nm[0] == '&') codeVars.insert(nm.substr(1));
            if (atTopLevel_ && topVars_.count(nm)) // hoisted to a global: assign it
                return mangleVar(nm) + " = " + coerceFor(tgt, exArg(a->value.get()));
            return "Value " + mangleVar(nm) + " = " + coerceFor(tgt, exArg(a->value.get()));
        }
        // List-assignment target: `($a, $b) = …` / `my ($a, $b) = …` — RHS evaluates
        // fully into a temp first (so `($a, $b) = $b, $a` swaps), then assigns by position.
        if (tgt->kind == NK::ListExpr && a->op == "=") {
            auto* le = static_cast<ListExpr*>(tgt);
            bool allDecl = !le->items.empty(), anyDecl = false, allScalar = true;
            for (auto& it : le->items) {
                if (it->kind != NK::VarExpr) { allScalar = false; break; }
                auto* v = static_cast<VarExpr*>(it.get());
                if (v->name.empty() || v->name[0] != '$') allScalar = false;
                if (v->declare) anyDecl = true;
                else allDecl = false;
            }
            if (allScalar && le->items.size() && (!anyDecl || allDecl)) {
                std::string t = gensym("__lt");
                if (allDecl) { // `my ($a, $b) = …` — statement position only
                    std::string o = "Value " + t + " = rtArrayVal(" + exArg(a->value.get()) + ")";
                    for (size_t i = 0; i < le->items.size(); i++)
                        o += "; Value " + mangleVar(static_cast<VarExpr*>(le->items[i].get())->name)
                           + " = rtIndexGet(" + t + ", Value::integer(" + std::to_string(i) + "LL), false)";
                    return o;
                }
                std::string o = "([&]()->Value{ Value " + t + " = rtArrayVal(" + exArg(a->value.get()) + ");";
                for (size_t i = 0; i < le->items.size(); i++)
                    o += " " + lvalueExpr(le->items[i].get()) + " = rtIndexGet(" + t
                       + ", Value::integer(" + std::to_string(i) + "LL), false);";
                return o + " return " + t + "; }())";
            }
        }
        // Scalar `:=` binds ≈ assigns natively; container aliasing isn't modeled, so bundle that.
        if (a->op == ":=") {
            if (tgt->kind == NK::VarExpr && !static_cast<VarExpr*>(tgt)->name.empty() && static_cast<VarExpr*>(tgt)->name[0] == '$')
                return lvalueExpr(tgt) + " = " + exArg(a->value.get());
            unsupported("binding (:=) of a non-scalar");
        }
        std::string rhs = exArg(a->value.get());
        if (a->op == "=") return lvalueExpr(tgt) + " = " + coerceFor(tgt, rhs);
        std::string binop = a->op.substr(0, a->op.size() - 1);  // strip '='
        // compound assignment to an index binds the slot once (avoids double side effects)
        if (tgt->kind == NK::Index) {
            std::string ref = lvalueExpr(tgt);
            if (binop == "~") // in-place append (O(n) string building) — default, not -O-gated
                return "([&]()->Value{ Value& __r = " + ref + "; rtCatAssign(__r, " + rhs + "); return __r; }())";
            std::string fb = fastBin(binop);
            std::string nv = binop == "||" ? "RT.boolify(__r) ? __r : (" + rhs + ")"
                           : binop == "&&" ? "RT.boolify(__r) ? (" + rhs + ") : __r"
                           : !fb.empty() ? fb + "(__r, " + rhs + ")"
                           : "applyArith(" + cesc(binop) + ", __r, " + rhs + ")";
            return "([&]()->Value{ Value& __r = " + ref + "; __r = " + nv + "; return __r; }())";
        }
        std::string lhs = lvalueExpr(tgt);
        if (binop == "||") return lhs + " = RT.boolify(" + lhs + ") ? " + lhs + " : (" + rhs + ")";
        if (binop == "&&") return lhs + " = RT.boolify(" + lhs + ") ? (" + rhs + ") : " + lhs;
        if (binop == "~") // in-place append (O(n) string building) — default, not -O-gated
            return "([&]()->Value&{ rtCatAssign(" + lhs + ", " + rhs + "); return " + lhs + "; }())";
        if (std::string f = fastBin(binop); !f.empty()) return lhs + " = " + f + "(" + lhs + ", " + rhs + ")"; // -O
        return lhs + " = applyArith(" + cesc(binop) + ", " + lhs + ", " + rhs + ")";
    }

    void ifStmt(IfStmt* f, int ind) {
        if (!f->thenVar.empty()) { // if EXPR -> $x { … } — bind the condition value; only a single branch
            if (f->branches.size() != 1) unsupported("if EXPR -> $x with elsif");
            std::string v = mangleVar(f->thenVar);
            line(ind, "{");
            line(ind + 1, "Value " + v + " = " + ex(f->branches[0].first.get()) + ";");
            std::string c = "RT.boolify(" + v + ")";
            line(ind + 1, (f->isUnless ? "if (!" : "if (") + c + ") {");
            block(f->branches[0].second.get(), ind + 2);
            line(ind + 1, "}");
            if (f->elseBlock) { line(ind + 1, "else {"); block(f->elseBlock.get(), ind + 2); line(ind + 1, "}"); }
            line(ind, "}");
            return;
        }
        for (size_t i = 0; i < f->branches.size(); i++) {
            std::string c = exBool(f->branches[i].first.get());
            if (f->isUnless) c = "!" + c;
            line(ind, (i == 0 ? "if (" : "else if (") + c + ") {");
            block(f->branches[i].second.get(), ind + 1);
            line(ind, "}");
        }
        if (f->elseBlock) { line(ind, "else {"); block(f->elseBlock.get(), ind + 1); line(ind, "}"); }
    }

    // A native loop body: catches the thrown forms of next/last (expression
    // position, or propagated out of a closure) and maps them to continue/break.
    // Does this loop body contain an expression-position `redo` (its own, not a
    // nested loop's)? Descends into everything except nested loop statements.

    bool exprHasRedo(Expr* e) {
        if (!e) return false;
        switch (e->kind) {
            case NK::Unary: {
                auto* u = static_cast<Unary*>(e);
                if (u->op == "redo" && !u->postfix) return true;
                return exprHasRedo(u->operand.get());
            }
            case NK::Binary: { auto* b = static_cast<Binary*>(e); return exprHasRedo(b->lhs.get()) || exprHasRedo(b->rhs.get()); }
            case NK::Ternary: { auto* t = static_cast<Ternary*>(e); return exprHasRedo(t->cond.get()) || exprHasRedo(t->then.get()) || exprHasRedo(t->els.get()); }
            case NK::Assign: { auto* a = static_cast<Assign*>(e); return exprHasRedo(a->target.get()) || exprHasRedo(a->value.get()); }
            case NK::Call: { auto* c = static_cast<Call*>(e); for (auto& x : c->args) if (exprHasRedo(x.get())) return true; return c->callee && exprHasRedo(c->callee.get()); }
            case NK::MethodCall: { auto* m = static_cast<MethodCall*>(e); if (exprHasRedo(m->inv.get())) return true; for (auto& x : m->args) if (exprHasRedo(x.get())) return true; return false; }
            case NK::ListExpr: for (auto& x : static_cast<ListExpr*>(e)->items) if (exprHasRedo(x.get())) return true; return false;
            case NK::Index: { auto* ix = static_cast<Index*>(e); return exprHasRedo(ix->base.get()) || (ix->index && exprHasRedo(ix->index.get())); }
            default: return false;
        }
    }

    void loopBody(Block* b, int ind, const std::string& label = "") {
        bool hasRedo = false;
        for (auto& st : b->stmts) if (stmtHasRedo(st.get())) { hasRedo = true; break; }
        std::string pass = label.empty() ? "!__e.label.empty()"
                                         : "!__e.label.empty() && __e.label != " + cesc(label);
        if (hasRedo) {
            // retry shape: `redo` re-runs the body with the same topic. Statement
            // next/last inside must THROW (loopDepth_ 0) so the retry catches see
            // them and translate to outer-loop continue/break via __lc.
            std::string lc = gensym("__lc"), ag = gensym("__ag");
            line(ind, "{ int " + lc + " = 0;");
            line(ind, "for (bool " + ag + " = true; " + ag + "; ) { " + ag + " = false;");
            line(ind + 1, "try {");
            int savedDepth = loopDepth_; loopDepth_ = 0;
            block(b, ind + 2);
            loopDepth_ = savedDepth;
            line(ind + 1, "} catch (const RedoEx& __e) { if (" + pass + ") throw; " + ag + " = true; }");
            line(ind + 1, "catch (const NextEx& __e) { if (" + pass + ") throw; " + lc + " = 1; }");
            line(ind + 1, "catch (const LastEx& __e) { if (" + pass + ") throw; " + lc + " = 2; }");
            line(ind, "}");
            line(ind, "if (" + lc + " == 1) continue;");
            line(ind, "if (" + lc + " == 2) break;");
            line(ind, "}");
            return;
        }
        loopDepth_++;
        std::string body = capture([&]() { block(b, ind + 1); });
        loopDepth_--;
        // a body that provably cannot raise a control signal (no user-code calls,
        // no closures, no throw helpers) runs bare — the hot benchmark kernels
        // (loopsum) stay wrapper-free for the optimizer
        bool canSignal = body.find("RT.") != std::string::npos ||
                         body.find("rtThrow") != std::string::npos ||
                         body.find("u_") != std::string::npos ||
                         body.find("m_") != std::string::npos ||
                         body.find("Value::closure") != std::string::npos;
        if (!canSignal) { out << body; return; }
        line(ind, "try {");
        out << body;
        // an unlabelled signal stops here; a labelled one only if this loop wears it
        line(ind, "} catch (const NextEx& __e) { if (" + pass + ") throw; continue; }"
                  " catch (const LastEx& __e) { if (" + pass + ") throw; break; }"
                  " catch (const RedoEx& __e) { throw; }");
    }

    void forStmt(ForStmt* f, int ind) {
        if (f->destructure) { // for LIST -> ($a, $b) { … } : unpack each element
            std::string lst = gensym("__lst"), el = gensym("__e");
            line(ind, "{");
            line(ind + 1, "Value " + lst + " = rtArrayVal(" + ex(f->list.get()) + ");");
            line(ind + 1, "for (auto& " + el + " : *" + lst + ".arr) {");
            for (size_t k = 0; k < f->vars.size(); k++)
                line(ind + 2, "Value " + mangleVar(f->vars[k]) + " = rtIndexGet(" + el +
                              ", Value::integer(" + std::to_string(k) + "LL), false);");
            loopBody(f->body.get(), ind + 2, f->label);
            line(ind + 1, "}");
            line(ind, "}");
            return;
        }
        if (f->vars.size() > 1) { // for @a -> $x, $y { … } : take vars.size() elements per iteration
            size_t n = f->vars.size();
            std::string lst = gensym("__lst"), i = gensym("__fi");
            line(ind, "{");
            line(ind + 1, "Value " + lst + " = rtArrayVal(" + ex(f->list.get()) + ");");
            line(ind + 1, "for (size_t " + i + " = 0; " + i + " < " + lst + ".arr->size(); " + i + " += " + std::to_string(n) + ") {");
            for (size_t k = 0; k < n; k++)
                line(ind + 2, "Value " + mangleVar(f->vars[k]) + " = (" + i + "+" + std::to_string(k) + " < " + lst +
                              ".arr->size() ? (*" + lst + ".arr)[" + i + "+" + std::to_string(k) + "] : Value::any());");
            loopBody(f->body.get(), ind + 2, f->label);
            line(ind + 1, "}");
            line(ind, "}");
            return;
        }
        std::string topic = f->vars.empty() ? gensym("v__t") : mangleVar(f->vars[0]);
        line(ind, "{");
        if (f->list->kind == NK::Range) {
            auto* r = static_cast<RangeExpr*>(f->list.get());
            std::string lo = gensym("__lo"), hi = gensym("__hi"), i = gensym("__i");
            line(ind + 1, "long long " + lo + " = (" + ex(r->from.get()) + ").toInt()" + (r->exFrom ? " + 1" : "") + ";");
            line(ind + 1, "long long " + hi + " = (" + ex(r->to.get()) + ").toInt()" + (r->exTo ? " - 1" : "") + ";");
            line(ind + 1, "for (long long " + i + " = " + lo + "; " + i + " <= " + hi + "; " + i + "++) {");
            line(ind + 2, "Value " + topic + " = Value::integer(" + i + ");");
            topics.push_back(topic);
            loopBody(f->body.get(), ind + 2, f->label);
            topics.pop_back();
            line(ind + 1, "}");
        } else {
            std::string lst = gensym("__lst"), el = gensym("__e");
            line(ind + 1, "Value " + lst + " = rtArrayVal(" + ex(f->list.get()) + ");");
            line(ind + 1, "for (auto& " + el + " : *" + lst + ".arr) {");
            line(ind + 2, "Value " + topic + " = " + el + ";");
            topics.push_back(topic);
            loopBody(f->body.get(), ind + 2, f->label);
            topics.pop_back();
            line(ind + 1, "}");
        }
        line(ind, "}");
    }

    // ---- value-position statements: a sub body ending in if/given returns the branch value ----
    void stmtValue(Stmt* s, int ind, const std::string& dst) {
        if (s->kind == NK::ExprStmt)  { line(ind, dst + " = " + exArg(static_cast<ExprStmt*>(s)->e.get()) + ";"); return; }
        if (s->kind == NK::IfStmt)    { ifValue(static_cast<IfStmt*>(s), ind, dst); return; }
        if (s->kind == NK::GivenStmt) { givenValue(static_cast<GivenStmt*>(s), ind, dst); return; }
        stmt(s, ind); // no value to capture; dst keeps its prior content
    }
    void blockValue(Block* b, int ind, const std::string& dst) {
        if (b->stmts.empty()) return;
        hoistLexicalSubs(b->stmts, ind);
        for (size_t i = 0; i + 1 < b->stmts.size(); i++) stmt(b->stmts[i].get(), ind);
        stmtValue(b->stmts.back().get(), ind, dst);
    }
    void ifValue(IfStmt* f, int ind, const std::string& dst) {
        if (!f->thenVar.empty()) { ifStmt(f, ind); return; } // `if EXPR -> $x` in value position: not captured
        for (size_t i = 0; i < f->branches.size(); i++) {
            std::string c = exBool(f->branches[i].first.get());
            if (f->isUnless) c = "!" + c;
            line(ind, (i == 0 ? "if (" : "else if (") + c + ") {");
            blockValue(f->branches[i].second.get(), ind + 1, dst);
            line(ind, "}");
        }
        if (f->elseBlock) { line(ind, "else {"); blockValue(f->elseBlock.get(), ind + 1, dst); line(ind, "}"); }
    }
    void givenValue(GivenStmt* g, int ind, const std::string& dst) {
        if (g->defGuard != 0) { // with / without in value position
            std::string topic = gensym("v__w");
            line(ind, "{");
            line(ind + 1, "Value " + topic + " = " + ex(g->topic.get()) + ";");
            std::string def = "(" + topic + ".t != VT::Nil && " + topic + ".t != VT::Any && " + topic + ".t != VT::Type)";
            line(ind + 1, "if (" + (g->defGuard == 1 ? def : "!" + def) + ") {");
            topics.push_back(topic);
            blockValue(g->body.get(), ind + 2, dst);
            topics.pop_back();
            line(ind + 1, "}");
            if (g->hasElse && g->elseBody) {
                line(ind + 1, "else {");
                blockValue(g->elseBody.get(), ind + 2, dst);
                line(ind + 1, "}");
            }
            line(ind, "}");
            return;
        }
        std::string topic = gensym("v__g"), done = gensym("__gdone");
        line(ind, "{");
        line(ind + 1, "Value " + topic + " = " + ex(g->topic.get()) + ";");
        topics.push_back(topic);
        for (auto& st : g->body->stmts) {
            if (st->kind == NK::WhenStmt) {
                auto* w = static_cast<WhenStmt*>(st.get());
                if (w->isDefault) line(ind + 1, "{");
                else line(ind + 1, "if (applyArith(\"~~\", " + topic + ", " + ex(w->cond.get()) + ").truthy()) {");
                blockValue(w->body.get(), ind + 2, dst);
                line(ind + 2, "goto " + done + ";");
                line(ind + 1, "}");
            } else {
                stmt(st.get(), ind + 1);
            }
        }
        topics.pop_back();
        line(ind + 1, done + ": ;");
        line(ind, "}");
    }

    void givenStmt(GivenStmt* g, int ind) {
        if (g->defGuard != 0) {
            // with / without EXPR { body } [else { elseBody }]: run guarded on (un)definedness
            std::string topic = gensym("v__w");
            line(ind, "{");
            line(ind + 1, "Value " + topic + " = " + ex(g->topic.get()) + ";");
            std::string def = "(" + topic + ".t != VT::Nil && " + topic + ".t != VT::Any && " + topic + ".t != VT::Type)";
            line(ind + 1, "if (" + (g->defGuard == 1 ? def : "!" + def) + ") {");
            topics.push_back(topic);
            block(g->body.get(), ind + 2);
            topics.pop_back();
            line(ind + 1, "}");
            if (g->hasElse && g->elseBody) {
                line(ind + 1, "else {");
                block(g->elseBody.get(), ind + 2);
                line(ind + 1, "}");
            }
            line(ind, "}");
            return;
        }
        std::string topic = gensym("v__g"), done = gensym("__gdone");
        line(ind, "{");
        line(ind + 1, "Value " + topic + " = " + ex(g->topic.get()) + ";");
        topics.push_back(topic);
        for (auto& st : g->body->stmts) {
            if (st->kind == NK::WhenStmt) {
                auto* w = static_cast<WhenStmt*>(st.get());
                if (w->isDefault) line(ind + 1, "{");
                else line(ind + 1, "if (applyArith(\"~~\", " + topic + ", " + ex(w->cond.get()) + ").truthy()) {");
                block(w->body.get(), ind + 2);
                line(ind + 2, "goto " + done + ";");
                line(ind + 1, "}");
            } else {
                stmt(st.get(), ind + 1); // a bare statement inside given runs unconditionally
            }
        }
        topics.pop_back();
        line(ind + 1, done + ": ;");
        line(ind, "}");
    }

    // ---- -O fast-call eligibility ----
    // A sub qualifies for direct `Value` parameters when every param is a plain
    // required positional scalar (no named/slurpy/optional/default/destructuring).
    static bool simpleSig(const std::vector<Param>& ps) {
        for (const Param& p : ps)
            if (p.named || p.slurpy || p.invocant || p.defaultVal || p.subSig || p.sigil != '$') return false;
        return true;
    }
    // A call site can take the fast path only when it passes plain positional args
    // (no `:name(…)` pairs, no `|@slurp`).
    static bool simpleArgs(const std::vector<ExprPtr>& args) {
        for (const ExprPtr& a : args) {
            if (a->kind == NK::Pair) return false;
            if (a->kind == NK::Unary && static_cast<Unary*>(a.get())->op == "|") return false;
        }
        return true;
    }
    // -O: the inline int-fast-path helper for a binary op (empty = use applyArith)
    std::string fastBin(const std::string& op) {
        if (!optimize_) return "";
        static const std::map<std::string, std::string> m = {
            {"+", "rtAdd"}, {"-", "rtSub"}, {"*", "rtMul"}, {"~", "rtConcat"}, {"%", "rtMod"}, {"%%", "rtDivides"},
            {"**", "rtPow"}, {"div", "rtDiv"},
            {"<", "rtLt"}, {"<=", "rtLe"}, {">", "rtGt"}, {">=", "rtGe"}, {"==", "rtEq"}, {"!=", "rtNe"}};
        auto it = m.find(op);
        return it == m.end() ? "" : it->second;
    }

    // ---- sub definitions ----
    // Emit binding lines that pull each parameter out of the call's `__a`
    // ValueList — handling positional, named, optional/default, and slurpy.
    // hasSelf: `__a[0]` is the invocant (methods), so positionals start at 1.
    void bindParams(const std::vector<Param>& ps, int ind, bool hasSelf) {
        size_t pi = hasSelf ? 1 : 0;
        int anon = 0;
        for (const Param& p : ps) {
            std::string nm = p.name.empty() ? "__anon" + std::to_string(anon++) : mangleVar(p.name);
            std::string pos = std::to_string(pi);
            if (p.invocant) { line(ind, "Value " + nm + " = __self;"); continue; }
            if (p.slurpy) {
                line(ind, "Value " + nm + " = " + (p.sigil == '%' ? "rtSlurpyNamed(__a);" : "rtSlurpyPos(__a, " + pos + ");"));
                continue;
            }
            if (p.named) {
                std::string key = p.name.size() > 1 ? cesc(p.name.substr(1)) : cesc("");
                if (p.defaultVal) line(ind, "Value " + nm + " = rtHasNamed(__a, " + key + ") ? rtNamed(__a, " + key + ") : (" + ex(p.defaultVal.get()) + ");");
                else line(ind, "Value " + nm + " = rtNamed(__a, " + key + ");");
                continue;
            }
            if (p.defaultVal) line(ind, "Value " + nm + " = rtHasPos(__a, " + pos + ") ? rtPos(__a, " + pos + ") : (" + ex(p.defaultVal.get()) + ");");
            else line(ind, "Value " + nm + " = rtPos(__a, " + pos + ");");
            if (p.name == "$/" || p.name == "$!") boundSpecials.insert(p.name);
            pi++;
        }
    }

    // ---- classes ----
    std::string methodFn(const std::string& cls, const std::string& meth) {
        // "__" separates: mangleBody never emits two consecutive underscores
        // (each '_' is followed by 2 hex digits), so the boundary is unambiguous.
        return "m_" + mangleBody(cls) + "__" + mangleBody(meth);
    }

    // one body per multi-method candidate: m_Cls_name__K
    std::string methodCandFn(const std::string& cls, const std::string& meth, int k) {
        return methodFn(cls, meth) + "__" + std::to_string(k);
    }
    void classMethodDefs(ClassDecl* cd) {
        if (cd->isPackage) unsupported("a package declaration");
        std::map<std::string, int> multiSeq; // per-name candidate counter
        for (auto& mp : cd->methods) {
            SubDecl* md = mp.get();
            std::string fname = md->isMulti ? methodCandFn(cd->name, md->name, multiSeq[md->name]++)
                                            : methodFn(cd->name, md->name);
            BodyScope __bs{this, /*closure=*/false};
            line(0, "static Value " + fname + "(ValueList& __a) {");
            line(1, "Value __self = __a.size() > 0 ? __a[0] : Value::any();");
            bindParams(md->params, 1, true);
            std::string saved = self_; self_ = "__self";
            hoistLexicalSubs(md->body, 1);
            for (size_t i = 0; i < md->body.size(); i++) {
                Stmt* s = md->body[i].get();
                if (i + 1 == md->body.size() && s->kind == NK::ExprStmt)
                    line(1, "return " + exArg(static_cast<ExprStmt*>(s)->e.get()) + ";");
                else if (i + 1 == md->body.size() && (s->kind == NK::IfStmt || s->kind == NK::GivenStmt)) {
                    std::string rv = gensym("__rv");
                    line(1, "Value " + rv + " = Value::any();");
                    stmtValue(s, 1, rv);
                    line(1, "return " + rv + ";");
                }
                else stmt(s, 1);
            }
            line(1, "return Value::any();");
            self_ = saved;
            line(0, "}");
        }
    }

    void classRegister(ClassDecl* cd) {
        std::string ci = gensym("ci");
        line(1, "{ auto " + ci + " = std::make_shared<ClassInfo>(); " + ci + "->name = " + cesc(cd->name) + ";");
        if (!cd->parent.empty())
            line(1, "  { auto __p = RT.classes_.find(" + cesc(cd->parent) + "); if (__p != RT.classes_.end()) " + ci + "->parent = __p->second; }");
        for (auto& a : cd->attrs) {
            std::string d = "  { ClassAttr __at; __at.name = " + cesc(a.name) + "; __at.sigil = '"
                          + std::string(1, a.sigil) + "'; __at.pub = " + (a.pub ? "true" : "false")
                          + "; __at.rw = " + (a.rw ? "true" : "false") + ";";
            if (!a.type.empty()) d += " __at.type = " + cesc(a.type) + ";";
            if (a.def) d += " __at.hasDefVal = true; __at.defVal = " + ex(a.def.get()) + ";";
            d += " " + ci + "->attrs.push_back(__at); }";
            line(1, d);
        }
        {
            std::map<std::string, std::vector<SubDecl*>> multis;
            std::map<std::string, int> seq;
            for (auto& mp : cd->methods) {
                if (mp->isMulti) { multis[mp->name].push_back(mp.get()); continue; }
                line(1, "  " + ci + "->methods[" + cesc(mp->name) + "] = Value::closure(" + methodFn(cd->name, mp->name) + ");");
            }
            for (auto& kv : multis) {
                // dispatcher: try candidates in declaration order; arity floor from
                // required params (excluding self), ceiling unless slurpy; typed/literal
                // params guard with rtTypeMatch/eqv
                std::string d = "  " + ci + "->methods[" + cesc(kv.first) + "] = Value::closure([](ValueList& __a)->Value{ size_t __n = __a.size() > 0 ? rtPosCount(__a, 1) : 0;";
                for (size_t k = 0; k < kv.second.size(); k++) {
                    SubDecl* c = kv.second[k];
                    size_t req = 0, opt = 0; bool slurpy = false;
                    for (auto& pp : c->params) {
                        if (pp.invocant) continue;
                        if (pp.slurpy) { slurpy = true; continue; }
                        if (pp.named) continue;
                        if (pp.defaultVal || pp.optional) opt++;
                        else req++;
                    }
                    std::string guard = "__n >= " + std::to_string(req);
                    if (!slurpy) guard += " && __n <= " + std::to_string(req + opt);
                    size_t pi = 1; // positional index; __a[0] is self, so start at 1 (matches bindParams)
                    for (auto& pp : c->params) {
                        if (pp.invocant || pp.slurpy || pp.named) continue;
                        if (!pp.type.empty())
                            guard += " && rtTypeMatch(rtPos(__a, " + std::to_string(pi) + "), " + cesc(pp.type) + ")";
                        pi++;
                    }
                    d += " if (" + guard + ") return " + methodCandFn(cd->name, kv.first, (int)k) + "(__a);";
                }
                d += " throw RakuError{Value::nil(), \"No matching multi-method candidate for " + kv.first + "\"}; });";
                line(1, d);
            }
        }
        if (cd->isGrammar) { // grammar rules are pattern strings — the embedded engine runs them
            line(1, "  " + ci + "->isGrammar = true;");
            for (auto& r : cd->rules) {
                std::string reg = "  " + ci + "->rules[" + cesc(r.name) + "] = " + cesc(r.pattern) + "; "
                                + ci + "->ruleKind[" + cesc(r.name) + "] = " + cesc(r.kind) + ";";
                if (!r.params.empty()) {
                    reg += " " + ci + "->ruleParams[" + cesc(r.name) + "] = {";
                    for (size_t k = 0; k < r.params.size(); k++) reg += (k ? ", " : "") + cesc(r.params[k]);
                    reg += "};";
                }
                line(1, reg);
            }
        }
        line(1, "  RT.classes_[" + cesc(cd->name) + "] = " + ci + "; }");
    }

    // Emit the statements of a sub body (last ExprStmt becomes the return value).
    void emitBody(const std::vector<StmtPtr>& body) {
        hoistLexicalSubs(body, 1);
        for (size_t i = 0; i < body.size(); i++) {
            Stmt* s = body[i].get();
            if (i + 1 == body.size() && s->kind == NK::ExprStmt)
                line(1, "return " + exArg(static_cast<ExprStmt*>(s)->e.get()) + ";");
            else if (i + 1 == body.size() && (s->kind == NK::IfStmt || s->kind == NK::GivenStmt)) {
                std::string rv = gensym("__rv"); // trailing if/given: the matched branch's value
                line(1, "Value " + rv + " = Value::any();");
                stmtValue(s, 1, rv);
                line(1, "return " + rv + ";");
            }
            else stmt(s, 1);
        }
        line(1, "return Value::any();");
    }
    // Emit a sub/candidate body given its C++ function name.
    void bodyDef(const std::string& fnName, const std::vector<Param>& ps, const std::vector<StmtPtr>& body, bool fast = false) {
        BodyScope __bs{this, /*closure=*/false};
        if (fast) {
            // -O: direct-Value signature (params are the C++ args themselves — no ValueList)
            std::string sig, fwd;
            for (size_t i = 0; i < ps.size(); i++) {
                if (i) { sig += ", "; fwd += ", "; }
                sig += "Value " + mangleVar(ps[i].name);
                fwd += "rtPos(__a, " + std::to_string(i) + ")";
            }
            line(0, "static Value " + fnName + "(" + sig + ") {");
            emitBody(body);
            line(0, "}");
            // boxed adapter so named/slurpy/multi call sites still resolve
            line(0, "static Value " + fnName + "(ValueList __a) { return " + fnName + "(" + fwd + "); }");
            return;
        }
        line(0, "static Value " + fnName + "(ValueList __a) {");
        bindParams(ps, 1, false);
        emitBody(body);
        line(0, "}");
    }
    void subDef(SubDecl* d) { bodyDef(mangleSub(d->name), d->params, d->body, fastSubs.count(d->name) > 0); }

    // A multi: emit each candidate, then a dispatcher that tries candidates
    // most-specific first (most type constraints) and picks the first that matches.
    void multiDef(const std::string& name, std::vector<SubDecl*> cands) {
        std::map<SubDecl*, int> idx;
        for (size_t i = 0; i < cands.size(); i++) {
            idx[cands[i]] = (int)i;
            bodyDef(mangleSub(name) + "__" + std::to_string(i), cands[i]->params, cands[i]->body);
        }
        // specificity: literal params (base cases) beat typed params beat untyped
        std::stable_sort(cands.begin(), cands.end(), [](SubDecl* a, SubDecl* b) {
            auto spec = [](SubDecl* s) { int n = 0; for (auto& p : s->params) n += p.litVal ? 2 : !p.type.empty() ? 1 : 0; return n; };
            return spec(a) > spec(b);
        });
        line(0, "static Value " + mangleSub(name) + "(ValueList __a) {");
        line(1, "size_t __n = rtPosCount(__a);");
        for (SubDecl* c : cands) {
            // arity guard: required positionals set the floor; optional/default params
            // raise the ceiling; a slurpy removes the ceiling entirely
            size_t req = 0, opt = 0; bool slurpy = false;
            for (auto& p : c->params) {
                if (p.slurpy) { slurpy = true; continue; }
                if (p.named) continue;
                if (p.defaultVal || p.optional) opt++;
                else req++;
            }
            std::string guard = "__n >= " + std::to_string(req);
            if (!slurpy) guard += " && __n <= " + std::to_string(req + opt);
            size_t pi = 0; // positional index (named params don't consume a slot)
            for (auto& p : c->params) {
                if (p.slurpy || p.named) continue;
                if (p.litVal)
                    guard += " && applyArith(\"eqv\", rtPos(__a, " + std::to_string(pi) + "), " + ex(p.litVal.get()) + ").truthy()";
                else if (!p.type.empty())
                    guard += " && rtTypeMatch(rtPos(__a, " + std::to_string(pi) + "), " + cesc(p.type) + ")";
                pi++;
            }
            line(1, "if (" + guard + ") return " + mangleSub(name) + "__" + std::to_string(idx[c]) + "(__a);");
        }
        line(1, "throw RakuError{Value::nil(), \"No matching multi candidate for " + name + "\"};");
        line(0, "}");
    }
};

} // namespace

std::string transpileToCpp(Program& prog, bool optimize, const std::string& srcPath) {
    Codegen g;
    g.optimize_ = optimize;
    // pre-pass: collect top-level sub declarations (for forward refs) and enum
    // values (bound as globals so subs can see them).
    std::vector<SubDecl*> subs;
    std::vector<ClassDecl*> classes;
    std::map<std::string, std::vector<SubDecl*>> multiCands;
    std::vector<std::pair<std::string, long long>> enumConsts;
    for (auto& s : prog.stmts) {
        if (s->kind == NK::SubDecl) {
            auto* d = static_cast<SubDecl*>(s.get());
            if (d->isMethod) throw CodegenError{"a method sub at statement level"};
            if (d->name.empty()) throw CodegenError{"an anonymous sub at statement level"};
            if (d->isMulti) { multiCands[d->name].push_back(d); g.multiNames.insert(d->name); }
            else {
                g.userSubs[d->name] = (int)d->params.size(); subs.push_back(d);
                if (optimize && Codegen::simpleSig(d->params)) g.fastSubs[d->name] = (int)d->params.size();
            }
        } else if (s->kind == NK::ClassDecl) {
            auto* cd = static_cast<ClassDecl*>(s.get());
            if (cd->isRole || cd->isPackage) throw CodegenError{"a role/package"};
            g.classNames.insert(cd->name);
            classes.push_back(cd);
        } else if (s->kind == NK::ExprStmt) {
            // top-level `my` declarations become C++ globals so subs can see them
            Expr* e = static_cast<ExprStmt*>(s.get())->e.get();
            auto collectDecl = [&](Expr* x) {
                if (x->kind == NK::VarExpr && static_cast<VarExpr*>(x)->declare) {
                    const std::string& nm = static_cast<VarExpr*>(x)->name;
                    // sigilled vars need a name beyond the sigil; sigilless (constant \W) are fine at 1 char
                    bool sigilled = !nm.empty() && (nm[0] == '$' || nm[0] == '@' || nm[0] == '%' || nm[0] == '&');
                    if (nm.size() > 1 && nm[1] == '*') return; // dynamics live in the runtime env
                    if (sigilled ? nm.size() > 1 : !nm.empty()) g.topVars_.insert(nm);
                }
            };
            if (e) {
                if (e->kind == NK::Assign) collectDecl(static_cast<Assign*>(e)->target.get());
                else if (e->kind == NK::ListExpr)
                    for (auto& it : static_cast<ListExpr*>(e)->items) collectDecl(it.get());
                else collectDecl(e);
            }
        } else if (s->kind == NK::EnumDecl) {
            auto* ed = static_cast<EnumDecl*>(s.get());
            Expr* v = ed->values.get();
            std::vector<ExprPtr>* items = v && v->kind == NK::ArrayLit ? &static_cast<ArrayLit*>(v)->items
                                        : v && v->kind == NK::ListExpr ? &static_cast<ListExpr*>(v)->items : nullptr;
            if (!items) throw CodegenError{"a non-literal enum"};
            long long idx = 0;
            for (auto& it : *items) {
                if (it->kind != NK::StrLit) throw CodegenError{"a non-literal enum value"};
                std::string key = static_cast<StrLit*>(it.get())->v;
                enumConsts.push_back({key, idx++});
                g.enumKeys.insert(key);
            }
        }
    }

    g.out << "// Generated by `rakupp --exe` — native transpilation of a Raku program.\n"
             "#include \"Interpreter.h\"\n#include \"Value.h\"\n#include <cmath>\n#include <vector>\n#include <string>\n#include <iostream>\n"
             "using namespace rakupp;\n"
             "static Interpreter RT;   // provides builtins, method dispatch, coercions\n\n";

    // top-level `my` vars as globals (initialised in program order inside main)
    for (auto& nm : g.topVars_) {
        char sg = nm[0];
        g.out << "static Value " << mangleVar(nm) << " = "
              << (sg == '@' ? "Value::array()" : sg == '%' ? "Value::makeHash()" : "Value::any()")
              << ";\n";
    }
    if (!g.topVars_.empty()) g.out << "\n";
    // enum values as globals
    for (auto& e : enumConsts)
        g.out << "static Value " << mangleVar(e.first) << " = Value::enumVal(" << cesc(e.first)
              << ", " << e.second << "LL);\n";
    if (!enumConsts.empty()) g.out << "\n";

    // forward declarations (subs + multis + class methods)
    for (SubDecl* d : subs) {
        auto fit = g.fastSubs.find(d->name);
        if (fit != g.fastSubs.end()) { // -O: both the direct-Value overload and the boxed adapter
            g.out << "static Value " << mangleSub(d->name) << "(";
            for (int i = 0; i < fit->second; i++) g.out << (i ? ", Value" : "Value");
            g.out << ");\n";
        }
        g.out << "static Value " << mangleSub(d->name) << "(ValueList);\n";
    }
    for (auto& mc : multiCands)
        g.out << "static Value " << mangleSub(mc.first) << "(ValueList);\n";
    for (ClassDecl* cd : classes)
        for (auto& mp : cd->methods)
            g.out << "static Value " << g.methodFn(cd->name, mp->name) << "(ValueList&);\n";
    g.out << "\n";

    // definitions
    for (SubDecl* d : subs) { g.subDef(d); g.out << "\n"; }
    for (auto& mc : multiCands) { g.multiDef(mc.first, mc.second); g.out << "\n"; }
    for (ClassDecl* cd : classes) { g.classMethodDefs(cd); g.out << "\n"; }

    // class registration (runs before the program body)
    g.out << "static void __rakupp_register() {\n";
    for (ClassDecl* cd : classes) g.classRegister(cd);
    g.out << "}\n\n";

    // main()
    g.out << "int main(int argc, char** argv) {\n"
             "    { std::vector<std::string> a; for (int i = 1; i < argc; i++) a.push_back(argv[i]); RT.setArgs(a); }\n"
             "    RT.srcFile_ = " + cesc(srcPath) + ";\n    __rakupp_register();\n"
             "    try {\n";
    std::string body = g.capture([&]() {
        g.atTopLevel_ = true;
        g.emitSeq(prog.stmts, 2, /*topLevel=*/true);
        g.atTopLevel_ = false;
        // auto-invoke MAIN with the CLI args (--opt args become named, like the interpreter)
        bool hasMain = false;
        for (auto& s : prog.stmts)
            if (s->kind == NK::SubDecl && static_cast<SubDecl*>(s.get())->name == "MAIN")
                hasMain = true;
        if (hasMain)
            g.line(2, "{ std::vector<std::string> __argv(argv + 1, argv + argc); "
                      "ValueList __margs = rtMainArgs(__argv); " +
                      mangleSub("MAIN") + "(__margs); }");
        for (auto it = g.topLevelEnds.rbegin(); it != g.topLevelEnds.rend(); ++it) g.emitPhaserBody(*it, 2);
    });
    g.out << body;
    g.out << "    } catch (const ExitEx& e) { std::cout.flush(); return e.code; }\n"
             "    catch (const LastEx&) { std::cerr << \"last without loop construct\\n\"; return 1; }\n"
             "    catch (const NextEx&) { std::cerr << \"next without loop construct\\n\"; return 1; }\n"
             "    catch (const RedoEx&) { std::cerr << \"redo without loop construct\\n\"; return 1; }\n"
             "    catch (const RakuError& e) { std::cerr << e.message << \"\\n\"; return 1; }\n"
             "    catch (const std::exception& e) { std::cerr << \"Internal error: \" << e.what() << \"\\n\"; return 3; }\n"
             "    return 0;\n}\n";
    return g.out.str();
}

}
