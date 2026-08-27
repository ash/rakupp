#include "CNumeric.h"
#include "AsciiCtype.h"
#include "Codegen.h"
#include "AstSerial.h"
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

// A pattern the embedded engine can run correctly WITHOUT the surrounding
// compiled frame. In a native binary the engine resolves variables and code
// blocks against the interpreter-side environment, while the enclosing
// program's lexicals are C++ locals it cannot see — so `/a $x c/` silently
// fails to match, and `/ a { $n = 42 } b /` silently skips the assignment
// (both measured, both the wrong kind of wrong). Until codegen can bridge
// its frame into the engine, any expression-position regex that mentions a
// program variable, an interpolated subregex, or a code block falls back to
// bundling, where the interpreter owns every scope and all of it works.
//
// Grammar rules and named-regex declarations are NOT checked: their blocks
// run in match context ($/, `make`), which lives interpreter-side and works
// natively (verified; the slim differential suite re-verifies).
//
// The scan is char-class aware (`<[…]>` contents are literal, `<[a]+[b]>`
// set arithmetic included) and escape-aware (`\$` is a literal). `$`/`^` as
// anchors, `$0` backrefs and `$<name>` capture refs are match-state, not
// program variables, and stay allowed.
void requireEngineOnlyRegex(const std::string& pat, const char* what) {
    int cls = 0; bool esc = false;
    auto identStart = [](char c) { return ascii::isalpha((unsigned char)c) || c == '_'; };
    for (size_t i = 0; i < pat.size(); i++) {
        char c = pat[i];
        if (esc) { esc = false; continue; }
        if (c == '\\') { esc = true; continue; }
        if (cls > 0) {
            if (c == ']') {
                size_t j = i + 1;                  // `<[a]+[b]>` continues the class
                while (j < pat.size() && (pat[j] == '+' || pat[j] == '-')) j++;
                if (j < pat.size() && pat[j] == '[') { i = j; continue; }
                cls = 0;
            }
            continue;
        }
        if (c == '<' && i + 1 < pat.size()) {
            size_t j = i + 1;
            while (j < pat.size() && (pat[j] == '+' || pat[j] == '-' || pat[j] == '!' || pat[j] == '?')) j++;
            if (j < pat.size() && pat[j] == '[') { cls = 1; i = j; continue; }
            if (j < pat.size() && (pat[j] == '$' || pat[j] == '{'))
                unsupported(std::string(what) + " with an interpolated subregex (<$…>/<{…}>)");
            continue;
        }
        if (c == '{')
            unsupported(std::string(what) + " with an embedded code block ({…})");
        if ((c == '$' || c == '@' || c == '%') && i + 1 < pat.size()) {
            char n = pat[i + 1];
            if (identStart(n) || n == '*' || n == '!' || n == '.' || n == '?')
                unsupported(std::string(what) + " that interpolates a variable (" +
                            std::string(1, c) + " — native locals live outside the engine's scope)");
        }
    }
}

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
        if (afterHex && ascii::isxdigit(c)) o += "\" \""; // break the literal: "\x0d" "e"
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
        if (ascii::isalnum(c)) o += (char)c;
        else { char b[4]; std::snprintf(b, sizeof b, "_%02x", c); o += b; }
    }
    return o;
}
// The initial value of a DECLARED variable. An untyped one is the bare empty
// container; a typed one has to go through the interpreter's own typedDefault, or
// `my Int @a` compiles to an Array of Mu and `my %h{Int}` loses its key type
// entirely — the declared type was simply dropped here before.
// `my @a[3;2]` — the dimension list, as a runtime ValueList. The dimensions are
// EXPRESSIONS (`my @a[$n;2]` is legal), so they are emitted, not folded here.
// Declared out of line because the emitter needs `ex()`; see shapedInit below.

static std::string declInit(const std::string& type, char sigil) {
    if (type.empty())
        return sigil == '@' ? "Value::array()" : sigil == '%' ? "Value::makeHash()" : "Value::any()";
    return "rtTypedDefault(" + cesc(type) + ", '" + std::string(1, sigil) + "')";
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
    std::map<std::string, std::vector<int>> rwSubs; // sub name -> positional indices that are `is rw`
    std::map<std::string, int> fastSubs; // -O: fixed-arity subs with direct Value params (name -> arity)
    bool optimize_ = false;              // -O codegen pass enabled
    std::set<std::string> enumKeys;      // enum value names (bound as globals)
    std::set<std::string> classNames;    // user class/role names (resolve as type objects)
    std::map<std::string, ClassDecl*> classDecls_; // name → declaration, for ancestry questions
    std::set<std::string> multiNames;    // names that are multi subs (dispatched at runtime)
    std::string self_;                   // C++ expr for `self` inside a method ("" outside)
    std::vector<std::string> topics;  // stack of C++ var names bound to $_
    int tmp = 0;
    std::string gensym(const char* p) { return std::string(p) + std::to_string(tmp++); }
    void line(int ind, const std::string& s) { out << std::string(ind * 4, ' ') << s << "\n"; }

    std::set<std::string> codeVars; // `my &name = …` seen so far — calls go through the Value
    int wcDepth = 0;               // nesting level of WhateverCode closures (0 = not in one)
    std::vector<int> wcArity;      // per-level count of `*` slots consumed

    // Builtin calls go through a per-name pointer resolved ONCE at startup
    // (skipping callBuiltin's per-call name hash + map lookup). Names are
    // collected here as call sites are emitted; transpileToCpp emits the
    // `__bfN` declarations and the one-time resolution after all code has
    // been generated. rtCallB falls back to the by-name path on null.
    std::map<std::string, int> usedBuiltins_; // name -> __bfN id

    // Names a `use`d module exports (`is export`). Resolving a call by name at
    // compile time is exactly what a module export defeats: the interpreter's
    // evalCall checks the environment BEFORE the builtin table, so an exported
    // `sub val` wins over the built-in `val` — while compiled code called the
    // built-in, because the cached pointer meant the env lookup never happened.
    // Calls to these names go through callEnvFirst instead (and skip -O's direct
    // named-builtin calls, which bypass even the pointer).
    std::set<std::string> moduleExports_;

    std::string builtinCall(const std::string& name, const std::string& vl) {
        if (moduleExports_.count(name))
            return "RT.callEnvFirst(" + cesc(name) + ", " + vl + ")";
        auto it = usedBuiltins_.emplace(name, (int)usedBuiltins_.size()).first;
        return "rtCallB(RT, __bfp" + std::to_string(it->second) + ", " + cesc(name) + ", " + vl + ")";
    }

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
            // The sequence op CONSUMES its own `*` (seed generators, an infinite or
            // predicate endpoint), so it is never part of a surrounding curry —
            // `say (1 ... * > 5).List` is a say of a Seq, not a say of a closure.
            case NK::Binary: { auto* b = static_cast<Binary*>(e);
                if (b->op == "..." || b->op == "...^" || b->op == "^..." || b->op == "^...^") return false;
                // `xx` never curries either: a RHS `*` means an endless repetition
                // and a LHS `*` is repeated as a plain Whatever VALUE (`* xx 3` is
                // (* * *)) — both handled at runtime, like the interpreter.
                if (b->op == "xx") return false;
                return hasWhatever(b->lhs.get()) || hasWhatever(b->rhs.get()); }
            case NK::Unary:  return hasWhatever(static_cast<Unary*>(e)->operand.get());
            case NK::Ternary: { auto* t = static_cast<Ternary*>(e); return hasWhatever(t->cond.get()) || hasWhatever(t->then.get()) || hasWhatever(t->els.get()); }
            // Only the invocant/callee is part of THIS whatever-curry; arguments are their
            // own closure scopes (e.g. `.grep(* %% 3)`), handled when each arg is emitted.
            case NK::MethodCall: return hasWhatever(static_cast<MethodCall*>(e)->inv.get());
            case NK::Call:       return hasWhatever(static_cast<Call*>(e)->callee.get());
            case NK::NqpOp:      { for (auto& x : static_cast<NqpOp*>(e)->args) if (hasWhatever(x.get())) return true; return false; }
            case NK::Index:      return hasWhatever(static_cast<Index*>(e)->base.get()); // *<key> / *[0]
            default: return false;
        }
    }

    // Expression in value position: a `*`-bearing expression becomes a WhateverCode closure.
    std::string exArg(Expr* e) {
        // A regex literal in argument position is the Regex object (not a $_ match).
        if (e->kind == NK::RegexLit) {
            requireEngineOnlyRegex(static_cast<RegexLit*>(e)->pattern, "a regex");
            return "Value::regex(" + cesc(static_cast<RegexLit*>(e)->pattern) + ")";
        }
        // (the sequence op consumes `*` itself — hasWhatever already answers
        // false for all four `...` forms, so no special case is needed here)
        if (!hasWhatever(e)) return ex(e);
        wcDepth++; wcArity.push_back(0);
        std::string an = "__w" + std::to_string(wcDepth);
        std::string body = ex(e);
        int arity = wcArity.back();
        wcDepth--; wcArity.pop_back();
        std::string mk = "Value::closure([=](ValueList& " + an + ")->Value{ return " + body + "; })";
        if (arity <= 1) return mk;
        // multi-`*` lambda: the sequence op / sort reads the arity off the Code value
        return "([&]()->Value{ Value _c = " + mk + "; _c.code()->isWhateverCode = true; "
               "_c.code()->whateverArity = " + std::to_string(arity) + "; return _c; }())";
    }

    static bool isSlip(Expr* e) { return e->kind == NK::Unary && static_cast<Unary*>(e)->op == "|" && !static_cast<Unary*>(e)->postfix; }
    std::string argList(const std::vector<ExprPtr>& args) {
        std::string s;
        for (size_t i = 0; i < args.size(); i++) {
            if (i) s += ", ";
            // A |slip in a list literal splices ONE level via listToArray — nested
            // arrays stay single elements (rtSlipShallow marks, doesn't flatten).
            // Deep flattening is call-arg semantics (argsVL/rtSpreadArg), not this.
            s += isSlip(args[i].get()) ? "rtSlipShallow(" + ex(static_cast<Unary*>(args[i].get())->operand.get()) + ")"
                                       : exArg(args[i].get());
        }
        return s;
    }
    static bool identKey(const std::string& k) {
        // non-ASCII bytes belong to a Unicode identifier the lexer already
        // accepted (`:μ(5)`), so they count as identifier material here too —
        // the interpreter's evalArgs makes the same call
        if (k.empty() || !(ascii::isalpha((unsigned char)k[0]) || k[0] == '_' ||
                           (unsigned char)k[0] >= 0x80)) return false;
        for (unsigned char c : k)
            if (!(ascii::isalnum(c) || c == '-' || c == '_' || c == '\'' || c >= 0x80)) return false;
        return true;
    }
    // An argument expression: a syntactic `k => v` / `:k(v)` with an identifier key
    // is a NAMED argument (mirrors evalArgs); everything else is exArg.
    std::string emitArg(Expr* a) {
        if (a->kind == NK::Pair) {
            auto* pr = static_cast<PairExpr*>(a);
            if (!pr->keyExpr && !pr->quotedKey && identKey(pr->key)) { // f('a' => 1) stays positional
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
        // -O int lane: an all-int comparison (or `%%`) evaluates guard-checked in
        // raw int64; on any guard/domain failure the boxed form below re-evaluates.
        if (optimize_) {
            Lane L;
            std::string c = laneBool(e, L);
            if (!c.empty()) {
                std::string out = "([&]() -> bool { do { ";
                if (!L.guards.empty()) out += "if (!(" + joinAnd(L.guards) + ")) break; ";
                for (auto& s : L.steps) out += s + " ";
                out += "return " + c + "; } while (0); return " + exBoolBoxed(e) + "; }())";
                return out;
            }
        }
        return exBoolBoxed(e);
    }
    std::string exBoolBoxed(Expr* e) {
        if (e->kind == NK::Binary && !hasWhatever(e)) {
            auto* b = static_cast<Binary*>(e);
            static const std::map<std::string, std::string> cmp = {
                {"<", "rtLtB"}, {"<=", "rtLeB"}, {">", "rtGtB"}, {">=", "rtGeB"}, {"==", "rtEqB"}, {"!=", "rtNeB"},
                {"eq", "rtEqSB"}, {"ne", "rtNeSB"}, {"lt", "rtLtSB"}, {"gt", "rtGtSB"}, {"le", "rtLeSB"}, {"ge", "rtGeSB"}};
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
        std::set<std::string> savedHoisted, savedSpecials, savedEnvSubs, savedCells, savedCodeVars;
        std::vector<std::string> savedCellsLive;
        int savedDepth;
        BodyScope(Codegen* g_, bool closure) : g(g_),
            savedHoisted(g_->hoisted), savedSpecials(g_->boundSpecials),
            savedEnvSubs(g_->envSubs), savedCells(g_->cellVars_),
            savedCodeVars(g_->codeVars), savedCellsLive(g_->cellsLive_), savedDepth(g_->loopDepth_) {
            g->loopDepth_ = 0;               // break/continue never cross a C++ function boundary
            if (!closure) { g->hoisted.clear(); g->boundSpecials.clear(); g->cellVars_.clear(); g->cellsLive_.clear(); }
            // a closure inherits the lexical sets (copy-in); additions are discarded on exit
        }
        ~BodyScope() {
            g->hoisted = std::move(savedHoisted);
            g->boundSpecials = std::move(savedSpecials);
            g->envSubs = std::move(savedEnvSubs);
            g->cellVars_ = std::move(savedCells);
            g->codeVars = std::move(savedCodeVars);
            g->cellsLive_ = std::move(savedCellsLive);
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
            if (!(ascii::isalpha((unsigned char)n[1]) || n[1] == '_')) return false; // skip $*/$!//etc.
            return !local.count(n) && !topVars_.count(n); // captured enclosing local
        };
        switch (e->kind) {
            case NK::Assign: { auto* a = static_cast<Assign*>(e); if (a->op.size() && a->op.back() == '=' && a->op != "==" && a->op != "!=" && a->op != ">=" && a->op != "<=" && isCapturedTarget(a->target.get())) return true; return exprAssignsCaptured(a->value.get(), local); }
            case NK::Unary: { auto* u = static_cast<Unary*>(e); if ((u->op == "++" || u->op == "--") && isCapturedTarget(u->operand.get())) return true; return exprAssignsCaptured(u->operand.get(), local); }
            case NK::Binary: { auto* b = static_cast<Binary*>(e); return exprAssignsCaptured(b->lhs.get(), local) || exprAssignsCaptured(b->rhs.get(), local); }
            case NK::Ternary: { auto* t = static_cast<Ternary*>(e); return exprAssignsCaptured(t->cond.get(), local) || exprAssignsCaptured(t->then.get(), local) || exprAssignsCaptured(t->els.get(), local); }
            case NK::Call: { auto* c = static_cast<Call*>(e); for (auto& x : c->args) if (exprAssignsCaptured(x.get(), local)) return true; return false; }
            case NK::NqpOp: { for (auto& x : static_cast<NqpOp*>(e)->args) if (exprAssignsCaptured(x.get(), local)) return true; return false; }
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
    // bail if this closure body assigns to a captured (enclosing-local) variable
    // that was NOT promoted to a shared cell — [=] const-captures plain locals,
    // so the emitted C++ would not compile / would be wrong. Cell-promoted names
    // (cellVars_) are shared boxes and mutate correctly.
    void checkClosureCapture(const std::vector<StmtPtr>& body, const std::set<std::string>& seedLocal) {
        std::set<std::string> local = seedLocal;
        for (auto& n : cellVars_) local.insert(n); // promoted: mutation is fine
        collectClosureLocals(body, local);
        for (auto& s : body)
            if (stmtAssignsCaptured(s.get(), local))
                unsupported("a closure that mutates a captured local variable");
    }

    // ---- shared-cell promotion for mutated closure captures ----
    // A local that some closure in the scope assigns must outlive and be shared
    // with every closure that captured it (Raku closure semantics). Such names
    // are declared as heap cells — `auto __c_X = std::make_shared<Value>(init);
    // Value& v_X = *__c_X;` — and every closure body re-aliases `Value& v_X =
    // *__c_X;`, so [=] captures the shared_ptr and all code (including the -O
    // int lanes) keeps using the plain name through the reference.
    std::set<std::string> cellVars_;   // names needing cells in the current function scope
    std::vector<std::string> cellsLive_; // cells declared so far (emission order), Raku names
    // Collect names assigned inside any (transitively) nested closure, that are
    // not local to that closure — i.e. mutated captures.
    void collectMutatedCaptures(const std::vector<StmtPtr>& body, std::set<std::string> local,
                                bool inClosure, std::set<std::string>& out) {
        auto record = [&](Expr* t) {
            if (!inClosure || !t) return;
            // Mutating THROUGH a subscript mutates the variable: `%bag{$k}++`,
            // `@result[$i] += 1`, `@ready[$p].push(…)` all write to the container.
            // Only a bare VarExpr counted before, so a hash or array a closure
            // updated by key was not made a cell — it was captured by value, came
            // out `const` inside the lambda, and the generated C++ would not
            // compile ("binding reference of type Value& to const Value").
            while (t->kind == NK::Index) t = static_cast<Index*>(t)->base.get();
            if (!t || t->kind != NK::VarExpr) return;
            auto* v = static_cast<VarExpr*>(t);
            if (v->declare) return;
            const std::string& n = v->name;
            if (n == "$_") return; // topics rebind per closure; keep the old error path
            if (n.size() < 2 || !(n[0] == '$' || n[0] == '@' || n[0] == '%')) return;
            if (!(ascii::isalpha((unsigned char)n[1]) || n[1] == '_')) return;
            if (!local.count(n) && !topVars_.count(n)) out.insert(n);
        };
        std::function<void(Expr*)> we = [&](Expr* e) {
            if (!e) return;
            switch (e->kind) {
                case NK::VarExpr: if (static_cast<VarExpr*>(e)->declare) local.insert(static_cast<VarExpr*>(e)->name); break;
                case NK::Assign: { auto* a = static_cast<Assign*>(e);
                    if (a->op.size() && a->op.back() == '=' && a->op != "==" && a->op != "!=" && a->op != ">=" && a->op != "<=") record(a->target.get());
                    we(a->target.get()); we(a->value.get()); break; }
                case NK::Unary: { auto* u = static_cast<Unary*>(e);
                    if (u->op == "++" || u->op == "--") record(u->operand.get());
                    we(u->operand.get()); break; }
                case NK::Binary: { auto* b = static_cast<Binary*>(e); we(b->lhs.get()); we(b->rhs.get()); break; }
                case NK::Ternary: { auto* t = static_cast<Ternary*>(e); we(t->cond.get()); we(t->then.get()); we(t->els.get()); break; }
                case NK::Call: { auto* c = static_cast<Call*>(e); if (c->callee) we(c->callee.get()); for (auto& x : c->args) we(x.get()); break; }
                case NK::NqpOp: { for (auto& x : static_cast<NqpOp*>(e)->args) we(x.get()); break; }
                case NK::MethodCall: { auto* m = static_cast<MethodCall*>(e);
                    if (m->mutate) record(m->inv.get());
                    we(m->inv.get()); for (auto& x : m->args) we(x.get()); break; }
                case NK::ListExpr: for (auto& x : static_cast<ListExpr*>(e)->items) we(x.get()); break;
                case NK::ArrayLit: for (auto& x : static_cast<ArrayLit*>(e)->items) we(x.get()); break;
                case NK::Index: { auto* ix = static_cast<Index*>(e); we(ix->base.get()); if (ix->index) we(ix->index.get()); break; }
                case NK::Pair: { auto* p = static_cast<PairExpr*>(e); if (p->value) we(p->value.get()); break; }
                case NK::Range: { auto* r = static_cast<RangeExpr*>(e); we(r->from.get()); we(r->to.get()); break; }
                case NK::BlockExpr: { // a nested closure: ONLY its own params/placeholders
                    // (and its own `my`s, inserted during the recursive walk) are local
                    // to it — everything else it assigns is a mutated capture.
                    auto* be = static_cast<BlockExpr*>(e);
                    std::set<std::string> inner;
                    for (auto& p : be->params) if (!p.name.empty()) inner.insert(p.name);
                    for (auto& ph : computePlaceholders(be->body)) inner.insert(ph);
                    collectMutatedCaptures(be->body, inner, /*inClosure=*/true, out);
                    break; }
                default: break;
            }
        };
        std::function<void(Stmt*)> ws = [&](Stmt* s) {
            if (!s) return;
            switch (s->kind) {
                case NK::ExprStmt: we(static_cast<ExprStmt*>(s)->e.get()); break;
                case NK::ReturnStmt: { auto* r = static_cast<ReturnStmt*>(s); if (r->value) we(r->value.get()); break; }
                case NK::IfStmt: { auto* f = static_cast<IfStmt*>(s);
                    for (auto& br : f->branches) { we(br.first.get()); for (auto& x : br.second->stmts) ws(x.get()); }
                    if (f->elseBlock) for (auto& x : f->elseBlock->stmts) ws(x.get()); break; }
                case NK::WhileStmt: { auto* w = static_cast<WhileStmt*>(s); we(w->cond.get()); for (auto& x : w->body->stmts) ws(x.get()); break; }
                case NK::ForStmt: { auto* f = static_cast<ForStmt*>(s); we(f->list.get());
                    for (auto& n : f->vars) local.insert(n);
                    for (auto& x : f->body->stmts) ws(x.get()); break; }
                case NK::GivenStmt: { auto* g = static_cast<GivenStmt*>(s); we(g->topic.get()); if (g->body) for (auto& x : g->body->stmts) ws(x.get()); break; }
                case NK::WhenStmt: { auto* w = static_cast<WhenStmt*>(s); if (w->cond) we(w->cond.get()); if (w->body) for (auto& x : w->body->stmts) ws(x.get()); break; }
                case NK::Block: for (auto& x : static_cast<Block*>(s)->stmts) ws(x.get()); break;
                case NK::SubDecl: { // a lexical sub is a closure over the enclosing scope
                    auto* d = static_cast<SubDecl*>(s);
                    if (d->isMethod) break;
                    std::set<std::string> inner; // its own params only — see BlockExpr note
                    for (auto& p : d->params) if (!p.name.empty()) inner.insert(p.name);
                    collectMutatedCaptures(d->body, inner, /*inClosure=*/true, out);
                    break; }
                default: {
                    if (Block* b = loopBodyOf(s)) for (auto& x : b->stmts) ws(x.get());
                    break; }
            }
        };
        for (auto& s : body) ws(s.get());
    }
    // Run the analysis at a function-body boundary and install the result.
    void analyzeCells(const std::vector<StmtPtr>& body, const std::set<std::string>& params) {
        std::set<std::string> out;
        collectMutatedCaptures(body, params, /*inClosure=*/false, out);
        for (auto& n : out) cellVars_.insert(n);
    }
    // Declaration text for a possibly-cell local; used by every decl site.
    // `my @a[3;2]` — the initialiser for a SHAPED declaration, or "" when the
    // declaration has no shape. The dimensions are expressions (`my @a[$n;2]` is
    // legal), so they are emitted rather than folded. With a right-hand side,
    // the store goes through rtShapedStore — the interpreter's own routine, so
    // the shape checks and the element defaults are not a second implementation.
    // The dimensions of `@a[$i; $j]`, as C++ initialisers. Refuses the forms the
    // native path does not answer — a Whatever slices a dimension (`@a[1; *]`),
    // which is a view, not an element — so they fall back to the bundled
    // interpreter instead of being answered wrongly.
    std::string multiDimArgs(Index* ix) {
        if (!ix->index || ix->index->kind != NK::ListExpr)
            unsupported("this multi-dimensional subscript");
        auto* dims = static_cast<ListExpr*>(ix->index.get());
        for (auto& d : dims->items)
            if (hasWhatever(d.get())) unsupported("a Whatever in a multi-dimensional index");
        return argList(dims->items);
    }

    std::string shapedInit(VarExpr* v, const std::string& rhs = "") {
        if (!v->declShape || v->name.empty() || v->name[0] != '@') return "";
        std::vector<Expr*> dims;
        if (v->declShape->kind == NK::ListExpr)
            for (auto& d : static_cast<ListExpr*>(v->declShape.get())->items) dims.push_back(d.get());
        else dims.push_back(v->declShape.get());
        std::string mk = "rtShapedArray(ValueList{";
        for (size_t i = 0; i < dims.size(); i++) { if (i) mk += ", "; mk += ex(dims[i]); }
        mk += "}, " + cesc(v->declType) + ")";
        if (rhs.empty()) return mk;
        return "([&]()->Value{ Value __sh = " + mk + "; rtShapedStore(__sh, " + rhs
             + ", " + cesc(v->declType) + "); return __sh; }())";
    }

    std::string declVar(const std::string& rakuName, const std::string& init) {
        std::string v = mangleVar(rakuName);
        if (cellVars_.count(rakuName)) {
            if (std::find(cellsLive_.begin(), cellsLive_.end(), rakuName) == cellsLive_.end())
                cellsLive_.push_back(rakuName);
            return "auto __c" + v + " = std::make_shared<Value>(" + init + "); Value& " + v + " = *__c" + v;
        }
        return "Value " + v + " = " + init;
    }
    // Re-alias every live cell at the top of a closure body, so [=] captures the
    // shared_ptr (not the outer alias) and mutation is shared. Shadowing makes
    // all later uses of the name bind to the local reference.
    void emitCellAliases(int ind, const std::set<std::string>& skip = {}) {
        for (auto& n : cellsLive_) {
            if (skip.count(n)) continue; // a same-named param/placeholder shadows the cell
            std::string v = mangleVar(n);
            line(ind, "Value& " + v + " = *__c" + v + "; (void)" + v + ";");
        }
    }

    // `is native` sub → a closure that calls the C symbol through the runtime's
    // own marshaller (RT.callNative — the same code the interpreter uses, with
    // its per-Callable dlopen/dlsym cache). Compiled here: a literal or absent
    // library name and no write-back marshalling. Everything else — `is
    // native(&sub)` / expression libs (the env they need doesn't exist in
    // compiled code), `is rw` out-params and buffer params (their copy-back
    // writes through caller LVALUES, which native locals don't have) — raises
    // CodegenError, so --exe falls back to bundling and stays CORRECT. Before
    // this, the stub body compiled silently and every native call returned Any.
    std::string nativeBridgeStmts(SubDecl* d) {
        if (!d->nativeLibSub.empty()) unsupported("a NativeCall sub with `is native(&sub)`");
        // `is native(Str)` / any bare type object = the default dlsym namespace;
        // every other expression needs the module env, which compiled code lacks
        if (d->nativeLibExpr &&
            !(d->nativeLibExpr->kind == NK::NameTerm &&
              isKnownTypeName(static_cast<NameTerm*>(d->nativeLibExpr.get())->name)))
            unsupported("a NativeCall sub whose library name is an expression");
        if (d->isMulti)               unsupported("a multi NativeCall sub");
        for (auto& p : d->params) {
            if (p.isRw) unsupported("a NativeCall sub with an `is rw` out-parameter");
            const std::string& t = p.type;
            if (t == "Buf" || t == "Blob" || t.rfind("Buf[", 0) == 0 || t.rfind("Blob[", 0) == 0 ||
                t.rfind("blob", 0) == 0 || t.rfind("buf", 0) == 0 || t.rfind("CArray", 0) == 0)
                unsupported("a NativeCall sub with a buffer/CArray parameter (needs copy-back)");
        }
        std::string sym = d->nativeSym.empty() ? d->name : d->nativeSym;
        std::ostringstream b;
        b << "    static const std::vector<Param> __np = []{ std::vector<Param> v;\n";
        for (auto& p : d->params)
            // `slurpy` has to travel too: it is what marks where C's `...`
            // begins, and without it the compiled bridge prepares a variadic
            // call as a fixed one — which the interpreter gets right and the
            // binary gets wrong, on exactly the ABIs that pass `...` on the
            // stack.
            b << "        { Param p; p.name = " << cesc(p.name) << "; p.sigil = '"
              << (p.sigil ? p.sigil : '$') << "'; p.type = " << cesc(p.type)
              << (p.slurpy ? "; p.slurpy = true" : "")
              << "; v.push_back(std::move(p)); }\n";
        b << "        return v; }();\n";
        b << "    static Callable __nc; static const bool __nci = []{ __nc.isNative = true;\n";
        b << "        __nc.name = " << cesc(d->name) << "; __nc.nativeLib = " << cesc(d->nativeLib) << ";\n";
        b << "        __nc.nativeSym = " << cesc(sym) << "; __nc.retType = " << cesc(d->retType) << ";\n";
        b << "        __nc.params = &__np; return true; }(); (void)__nci;\n";
        b << "    return RT.callNative(__nc, __a, nullptr);\n";
        return b.str();
    }
    std::string nativeSubClosure(SubDecl* d) {
        return "Value::closure([=](ValueList& __a)->Value{\n" + nativeBridgeStmts(d) + "})";
    }
    // Top-level `is native` sub → the same bridge as a named function (matches
    // the `static Value f(ValueList);` prototype every call site expects).
    void nativeSubDef(SubDecl* d) {
        out << "static Value " << mangleSub(d->name) << "(ValueList __a) {\n"
            << nativeBridgeStmts(d) << "}\n";
    }

    std::string subClosure(SubDecl* d) {
        if (d->isNative) return nativeSubClosure(d);
        std::set<std::string> params;
        for (auto& p : d->params) if (!p.name.empty()) params.insert(p.name);
        checkClosureCapture(d->body, params); // against the OUTER scope's cells
        std::vector<std::string> outerCells = cellsLive_;
        BodyScope __bs{this, /*closure=*/false};
        cellsLive_ = outerCells;   // outer cells stay reachable (aliased below)
        analyzeCells(d->body, params);
        std::string body = capture([&]() {
            emitCellAliases(0, params);
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
        // a SUB body is a ReturnEx boundary (same rule as bodyDef): without the
        // catch, a `return`/`fail` in a lexical sub unwound into the CALLER's
        // frame — or clean out of main() as an uncaught-exception abort
        return "Value::closure([=](ValueList& __a)->Value{ try {\n" + body +
               "} catch (ReturnEx& __r) { return __r.v; } })";
    }

    // A block `{ ... }` / pointy `-> $x { ... }` becomes a native closure.
    std::string emitBlockClosure(BlockExpr* be) {
        BodyScope __bs{this, /*closure=*/true};
        bool pushed = false; std::string topic;
        std::vector<std::string> phs = be->params.empty() ? computePlaceholders(be->body)
                                                          : std::vector<std::string>{};
        std::set<std::string> own(phs.begin(), phs.end());
        for (auto& p : be->params) if (!p.name.empty()) own.insert(p.name);
        checkClosureCapture(be->body, own);
        analyzeCells(be->body, own);
        std::string body = capture([&]() {
            emitCellAliases(0, own);
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
                else if (i + 1 == be->body.size() && (s->kind == NK::IfStmt || s->kind == NK::GivenStmt)) {
                    // a block-final if/elsif/else (or given) is the block's value,
                    // exactly as in sub bodies
                    std::string rv = gensym("__rv");
                    line(0, "Value " + rv + " = Value::any();");
                    stmtValue(s, 0, rv);
                    line(0, "return " + rv + ";");
                }
                else stmt(s, 0);
            }
            line(0, "return Value::any();");
        });
        if (pushed) topics.pop_back();
        // an anonymous `sub {…}` term is a ReturnEx boundary like any routine;
        // a plain block stays TRANSPARENT so `return` reaches the enclosing sub
        std::string mk = be->isSub
            ? "Value::closure([=](ValueList& __a)->Value{ try {\n" + body +
              "} catch (ReturnEx& __r) { return __r.v; } })"
            : "Value::closure([=](ValueList& __a)->Value{\n" + body + "})";
        // 2+-ary blocks (pointy params or $^a/$^b) must advertise their arity so
        // sort/map/for feed them the right number of elements per call.
        size_t nPos = phs.size();
        if (nPos <= 1) for (auto& p : be->params) if (!p.named && !p.slurpy) nPos++;
        if (nPos <= 1) return mk;
        std::string names;
        for (size_t k = 0; k < nPos; k++) names += std::string(k ? ", " : "") + "\"$^" + std::string(1, char('a' + k)) + "\"";
        return "([&]()->Value{ Value _c = " + mk + "; _c.code()->placeholders = {" + names + "}; return _c; }())";
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
                    // cnum, not a stream: a host's locale must never put a comma
                    // into GENERATED SOURCE (it would not compile)
                    char nb[40]; cnum::snprintf(nb, sizeof nb, "%.17g", n->v);
                    return "Value::complex(0, " + std::string(nb) + ")";
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
                char nb[40]; cnum::snprintf(nb, sizeof nb, "%.17g", n->v);
                return "Value::number(" + std::string(nb) + ")";
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
                    return "Value::closure([=](ValueList& __a)->Value{ return " + builtinCall(nm, "__a") + "; })";
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
                if (v->name == "$?FILE") return "Value::str(RT.srcFile_)";       // compile-time constant ($?LINE answered above)
                if (v->name.size() > 1 && (v->name[1] == '?' || v->name[1] == '!'))
                    unsupported("special/dynamic variable '" + v->name + "'");
                if (v->name.size() > 1 && v->name[0] == '$' &&
                    std::all_of(v->name.begin() + 1, v->name.end(), [](unsigned char c) { return ascii::isdigit(c); })) {
                    // $0/$1/… are positional captures of the current match ($/) —
                    // read through the live env (RT.regexMatch stores it there),
                    // except when $/ is a bound parameter (action methods)
                    std::string slash = boundSpecials.count("$/") ? mangleVar("$/")
                                      : "RT.dynVar(" + cesc("$/") + ")";
                    return "rtIndexGet(" + slash + ", Value::integer(" + v->name.substr(1) + "LL), false)";
                }
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
                if (r->from->kind == NK::IntLit && r->to->kind == NK::IntLit &&
                    static_cast<IntLit*>(r->from.get())->big.empty() &&
                    static_cast<IntLit*>(r->to.get())->big.empty()) // a bigint endpoint must not truncate
                    return "Value::range((" + ex(r->from.get()) + ").toInt(), (" + ex(r->to.get()) + ").toInt(), "
                         + (r->exFrom ? "true" : "false") + ", " + (r->exTo ? "true" : "false") + ")";
                return "rtRangeVal(" + ex(r->from.get()) + ", " + ex(r->to.get()) + ", "
                     + (r->exFrom ? "true" : "false") + ", " + (r->exTo ? "true" : "false") + ")";
            }
            case NK::RegexLit: {
                requireEngineOnlyRegex(static_cast<RegexLit*>(e)->pattern, "a regex");
                std::string topic = topics.empty() ? "RT.dynVar(\"$_\")" : topics.back();
                return "RT.regexMatch((" + topic + ").toStr(), " + cesc(static_cast<RegexLit*>(e)->pattern) + ")";
            }
            case NK::SubstLit: { // bare s/// (or tr///): mutates the topic, like `$_ ~~ s///`
                auto* sub = static_cast<SubstLit*>(e);
                requireEngineOnlyRegex(sub->pattern, "an `s///` pattern");
                requireEngineOnlyRegex(sub->repl, "an `s///` replacement");
                std::string tgt = topics.empty() ? "RT.dynVarRef(\"$_\")" : topics.back();
                return "RT.substApply(&(" + tgt + "), " + cesc(sub->pattern) + ", "
                     + cesc(sub->repl) + ", " + (sub->nonMut ? "true" : "false") + ")";
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
                // `@a[$i; $j]` on a shaped array walks a level per index, which is
                // AT-POS's job — the same one the interpreter calls, so a compiled
                // multi-dim read cannot drift from an interpreted one. (Slicing a
                // dimension, `@a[1; *]`, is refused in shapedDims below rather than
                // answered wrongly.)
                if (ix->multiDim)
                    return "RT.methodCall(" + ex(ix->base.get()) + ", \"AT-POS\", ValueList{"
                         + multiDimArgs(ix) + "})";
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
                if (userSubs.count(n))
                    return rwSubs.count(n) ? "([&]()->Value{ ValueList __rw{}; return " + mangleSub(n) + "(__rw); }())"
                                           : mangleSub(n) + "(ValueList{})";        // zero-arg sub call
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
            case NK::NqpOp: {
                // `use nqp` subset. Control forms emit native C++ (their args must
                // not all evaluate eagerly); every leaf op evaluates its args and
                // calls the shared runtime entry rtNqpOp — so the compiled binary
                // uses the exact same op logic as the interpreter.
                auto* nq = static_cast<NqpOp*>(e);
                auto& a = nq->args;
                switch (nq->op) {
                    case NqpOpc::Stmts: {                 // eval each, value = last
                        std::string o = "([&]()->Value{ Value __r = Value::nil();";
                        for (auto& x : a) o += " __r = (" + ex(x.get()) + ");";
                        return o + " return __r; }())";
                    }
                    case NqpOpc::While:                   // loop while/until cond
                    case NqpOpc::Until: {
                        if (a.size() < 2) return "Value::nil()";
                        std::string body;
                        for (size_t i = 1; i < a.size(); i++) body += " (void)(" + ex(a[i].get()) + ");";
                        std::string neg = nq->op == NqpOpc::Until ? "!" : "";
                        return "([&]()->Value{ while (" + neg + exBool(a[0].get()) + ") {" + body +
                               " } return Value::nil(); }())";
                    }
                    case NqpOpc::IfNull: {                 // arg0 unless it's null
                        std::string alt = a.size() > 1 ? ex(a[1].get()) : "Value::nil()";
                        return "([&]()->Value{ Value __v = (" + ex(a[0].get()) +
                               "); return (__v.t==VT::Nil||__v.t==VT::Any) ? (" + alt + ") : __v; }())";
                    }
                    default:                              // eager leaf op
                        // rtNqpOp takes ValueList& (push/bindpos mutate the array
                        // arg in place) — bind a named local to hand it an lvalue
                        return "([&]()->Value{ ValueList __na = " + argsVL(a) +
                               "; return rtNqpOp(NqpOpc(" + std::to_string((int)nq->op) +
                               "), __na); }())";
                }
            }
            case NK::Unary: {
                auto* u = static_cast<Unary*>(e);
                if (u->op.size() >= 3 && u->op.front() == '[' && u->op.back() == ']') // reduce metaop [+] [*] …
                    return "rtReduce(RT, " + cesc(u->op.substr(1, u->op.size() - 2)) + ", " + exArg(u->operand.get()) + ")";
                if (u->op == "do" || u->op == "try") { // do { } / try { }  (value = last statement)
                    std::string body;
                    if (u->operand->kind == NK::BlockExpr) {
                        auto* be = static_cast<BlockExpr*>(u->operand.get());
                        body = capture([&]() {
                            for (size_t i = 0; i < be->body.size(); i++) {
                                Stmt* s = be->body[i].get();
                                if (i + 1 == be->body.size() && s->kind == NK::ExprStmt)
                                    line(0, "return " + exArg(static_cast<ExprStmt*>(s)->e.get()) + ";");
                                else {
                                    // a value-context loop collects per-iteration values —
                                    // stmt() would discard them (`my @a = do for 1..3 {…}`)
                                    if ((s->kind == NK::ForStmt   && static_cast<ForStmt*>(s)->asExpr) ||
                                        (s->kind == NK::WhileStmt && static_cast<WhileStmt*>(s)->asExpr) ||
                                        (s->kind == NK::LoopStmt  && static_cast<LoopStmt*>(s)->asExpr))
                                        unsupported("a do-loop collecting values");
                                    stmt(s, 0);
                                }
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
                    std::string add = optimize_ ? "rtAdd(_o, Value::integer(" + delta + "))"
                                                : "applyArith(\"+\", _o, Value::integer(" + delta + "))";
                    return "([&]()->Value{ Value& _r=" + lvalueExpr(u->operand.get()) +
                           "; Value _o=_r; _r=" + add + "; return _o; }())";
                }
                if (u->op == "++" || u->op == "--") { // prefix: yield the new value
                    std::string delta = u->op == "++" ? "1" : "-1";
                    std::string add = optimize_ ? "rtAdd(_r, Value::integer(" + delta + "))"
                                                : "applyArith(\"+\", _r, Value::integer(" + delta + "))";
                    return "([&]()->Value{ Value& _r=" + lvalueExpr(u->operand.get()) +
                           "; _r=" + add + "; return _r; }())";
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
                if (u->op == "+^") return "Value::integer(~(" + x + ").toInt())";          // bitwise NOT
                if (u->op == "?^") return "Value::boolean(!RT.boolify(" + x + "))";        // boolean NOT (xor form)
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
                    requireEngineOnlyRegex(static_cast<RegexLit*>(b->rhs.get())->pattern, "a regex");
                    std::string m = "RT.regexMatch((" + ex(b->lhs.get()) + ").toStr(), "
                                  + cesc(static_cast<RegexLit*>(b->rhs.get())->pattern) + ")";
                    return b->op == "~~" ? m : "Value::boolean(!(" + m + ").truthy())";
                }
                // substitution: `$x ~~ s/pat/repl/` (and tr///, S///) — one runtime call
                // that mutates the target through its lvalue like the interpreter does
                if (b->op == "~~" && b->rhs->kind == NK::SubstLit) {
                    auto* sub = static_cast<SubstLit*>(b->rhs.get());
                    requireEngineOnlyRegex(sub->pattern, "an `s///` pattern");
                    requireEngineOnlyRegex(sub->repl, "an `s///` replacement");
                    return "RT.substApply(&(" + lvalueExpr(b->lhs.get()) + "), "
                         + cesc(sub->pattern) + ", " + cesc(sub->repl) + ", "
                         + (sub->nonMut ? "true" : "false") + ")";
                }
                if (b->op == "xx") {
                    // `EXPR xx *` is an endless LAZY sequence whose thunk must
                    // outlive this function's locals — no native arm; fall back
                    // to bundling, where the interpreter's lazy path is correct.
                    if (b->rhs->kind == NK::Whatever ||
                        (b->rhs->kind == NK::NameTerm &&
                         (static_cast<NameTerm*>(b->rhs.get())->name == "Inf" ||
                          static_cast<NameTerm*>(b->rhs.get())->name == "\xe2\x88\x9e")))
                        unsupported("an endless `xx *` repetition");
                    // list repetition thunks its left side (re-evaluate per copy);
                    // rtXxAppend splices a Slip's elements (`|(1,2) xx 2` is 4 elems)
                    std::string L = ex(b->lhs.get()), R = ex(b->rhs.get());
                    return "([&]()->Value{ long long _n=(" + R + ").toInt(); Value _o=Value::array(); _o.isList=true; _o.s=\"Seq\"; "
                           "for(long long _i=0;_i<_n;_i++) rtXxAppend(*_o.arr(), " + L + "); return _o; }())";
                }
                if (b->op == "^..." || b->op == "^...^")
                    unsupported("a ^...-form sequence"); // no native arm: fall back rather than die in applyArith
                if (b->op == "..." || b->op == "...^") {
                    // sequence operator: seeds emit per-element (a `* + *` seed becomes a
                    // generator closure); a bare `*`/Inf endpoint marks the sequence infinite.
                    // `...` is a LIST infix, so a chain (`1 ... 5 ... 1`) or a comma group
                    // on the right (`'A'...'Z', 'a'...'z'`) is ONE operator over a list of
                    // lists — the same walk the interpreter does, into the shared
                    // RT.seqOpGroups.
                    auto isSeqOp = [](const std::string& o) { return o == "..." || o == "...^"; };
                    std::vector<Binary*> spine;
                    for (Expr* n = const_cast<Binary*>(b); n && n->kind == NK::Binary &&
                             isSeqOp(static_cast<Binary*>(n)->op); n = static_cast<Binary*>(n)->lhs.get())
                        spine.push_back(static_cast<Binary*>(n));
                    std::reverse(spine.begin(), spine.end());
                    bool grouped = spine.size() > 1;
                    for (auto* sn : spine)
                        if (sn->rhs->kind == NK::ListExpr &&
                            !static_cast<ListExpr*>(sn->rhs.get())->parenned) grouped = true;
                    auto seedOf = [&](Expr* e) {
                        if (e->kind == NK::ListExpr) {
                            auto* le = static_cast<ListExpr*>(e);
                            std::string items;
                            for (size_t i = 0; i < le->items.size(); i++) {
                                if (i) items += ", ";
                                items += exArg(le->items[i].get());
                            }
                            return "listToArray({" + items + "})";
                        }
                        return exArg(e);
                    };
                    if (grouped) {
                        std::string groups, excl;
                        for (size_t i = 0; i < spine.size(); i++) {
                            std::string items;
                            Expr* r = spine[i]->rhs.get();
                            if (r->kind == NK::ListExpr && !static_cast<ListExpr*>(r)->parenned) {
                                auto* le = static_cast<ListExpr*>(r);
                                for (size_t k = 0; k < le->items.size(); k++) {
                                    if (k) items += ", ";
                                    items += le->items[k]->kind == NK::Whatever ? "Value::whatever()"
                                                                                : exArg(le->items[k].get());
                                }
                            }
                            else items = r->kind == NK::Whatever ? "Value::whatever()" : exArg(r);
                            if (i) { groups += ", "; excl += ", "; }
                            groups += "ValueList{" + items + "}";
                            excl += spine[i]->op == "...^" ? "1" : "0";
                        }
                        return "RT.seqOpGroups(" + seedOf(spine.front()->lhs.get()) +
                               ", std::vector<ValueList>{" + groups + "}, std::vector<char>{" + excl + "}, false)";
                    }
                    std::string L = seedOf(b->lhs.get());
                    std::string R = b->rhs->kind == NK::Whatever ? "Value::whatever()" : exArg(b->rhs.get());
                    return "RT.seqOp(" + L + ", " + R + ", " + (b->op == "...^" ? "true" : "false") + ")";
                }
                if (b->op.size() > 1 && b->op[0] == 'R' && !ascii::isalnum((unsigned char)b->op[1])) {
                    // reverse metaop `a R/ b` == `b / a`
                    std::string L = ex(b->lhs.get()), R = ex(b->rhs.get());
                    return "applyArith(" + cesc(b->op.substr(1)) + ", " + R + ", " + L + ")";
                }
                std::string L = ex(b->lhs.get()), R = ex(b->rhs.get());
                if (b->op == "&&" || b->op == "and")
                    return "([&]()->Value{ Value _a=(" + L + "); return RT.boolify(_a)?(" + R + "):_a; }())";
                if (b->op == "||" || b->op == "or")
                    return "([&]()->Value{ Value _a=(" + L + "); return RT.boolify(_a)?_a:(" + R + "); }())";
                if (b->op == "//") // defined-or — including a Failure, which rtIsDefined knows about
                    return "([&]()->Value{ Value _a=(" + L + "); return !rtIsDefined(_a)?(" + R + "):_a; }())";
                if (std::string f = fastBin(b->op); !f.empty()) return f + "(" + L + ", " + R + ")"; // -O
                return "applyArith(" + cesc(b->op) + ", " + L + ", " + R + ")";
            }
            case NK::InterpStr: {
                auto* s = static_cast<InterpStr*>(e);
                if (s->parts.empty()) return "Value::str(\"\")";
                // A literal part contributes its C string directly — routing it
                // through Value::str(…).toStr() built and destroyed a whole
                // Value per part on every evaluation ("key$i" in a fill loop).
                // A leading literal is wrapped in std::string so operator+ has
                // a string operand whatever follows it.
                std::string acc;
                for (size_t i = 0; i < s->parts.size(); i++) {
                    std::string piece;
                    if (s->parts[i]->kind == NK::StrLit) {
                        piece = cesc(static_cast<StrLit*>(s->parts[i].get())->v);
                        if (i == 0) piece = "std::string(" + piece + ")";
                    }
                    else piece = "(" + ex(s->parts[i].get()) + ").toStr()";
                    if (i) acc += " + ";
                    acc += piece;
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
                    // `is rw` params: pass a named ValueList (the callee binds references
                    // into it) and copy mutated slots back into lvalue arguments after.
                    if (auto rit = rwSubs.find(c->name); rit != rwSubs.end()) {
                        bool anyPair = false;
                        for (auto& a : c->args) if (a->kind == NK::Pair) anyPair = true;
                        std::string o = "([&]()->Value{ ValueList __rw = " + vl + "; Value __r = "
                                      + mangleSub(c->name) + "(__rw);";
                        if (!slip && !anyPair)
                            for (int i : rit->second)
                                if (i < (int)c->args.size()) {
                                    Expr* a = c->args[i].get();
                                    bool lv = (a->kind == NK::VarExpr && !static_cast<VarExpr*>(a)->declare)
                                           || a->kind == NK::Index;
                                    if (lv) o += " " + lvalueExpr(a) + " = __rw[" + std::to_string(i) + "];";
                                }
                        return o + " return __r; }())";
                    }
                    // -O: call the direct-Value overload when the arity/args line up
                    auto fit = fastSubs.find(c->name);
                    if (!slip && fit != fastSubs.end() && (int)c->args.size() == fit->second && simpleArgs(c->args))
                        return mangleSub(c->name) + "(" + argList(c->args) + ")";
                    return mangleSub(c->name) + "(" + vl + ")"; // boxed adapter
                }
                // -O: true named builtins — a direct C++ call (no ValueList, no
                // lambda; the hot path of rtBAbs inlines at the call site).
                // Only for a plain single positional arg; anything else takes
                // the generic cached-pointer path below.
                if (optimize_ && c->args.size() == 1 && simpleArgs(c->args) &&
                    !moduleExports_.count(c->name)) {   // a module export owns this name
                    static const std::map<std::string, const char*> fastB = {
                        {"abs", "rtBAbs"}, {"chr", "rtBChr"}, {"ord", "rtBOrd"},
                        {"say", "rtBSay"}, {"print", "rtBPrint"}, {"put", "rtBPut"}, {"note", "rtBNote"},
                        {"sign", "rtBSign"}, {"floor", "rtBFloor"}, {"ceiling", "rtBCeiling"},
                        {"round", "rtBRound"}, {"truncate", "rtBTruncate"}, {"sqrt", "rtBSqrt"},
                        {"exp", "rtBExp"}, {"log", "rtBLog"}, {"log10", "rtBLog10"}, {"log2", "rtBLog2"},
                        {"is-prime", "rtBIsPrime"},
                        {"uc", "rtBUc"}, {"lc", "rtBLc"}, {"chars", "rtBChars"}, {"flip", "rtBFlip"},
                        {"trim", "rtBTrim"}, {"chomp", "rtBChomp"}, {"chop", "rtBChop"},
                        {"sin", "rtBSin"}, {"cos", "rtBCos"}, {"tan", "rtBTan"},
                        {"asin", "rtBAsin"}, {"acos", "rtBAcos"}, {"atan", "rtBAtan"},
                        {"sinh", "rtBSinh"}, {"cosh", "rtBCosh"}, {"tanh", "rtBTanh"},
                        {"asinh", "rtBAsinh"}, {"acosh", "rtBAcosh"}, {"atanh", "rtBAtanh"}};
                    auto fb = fastB.find(c->name);
                    if (fb != fastB.end())
                        return std::string(fb->second) + "(RT, " + ex(c->args[0].get()) + ")";
                }
                return builtinCall(c->name, vl);
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
                // AUTOVIVIFY through a subscript for the methods that grow a
                // container. `@ready[2].push(10)` must create the inner Array in the
                // ARRAY, not in a temporary: ex() on an Index yields rtIndexGet,
                // which returns a fresh Any for a missing element, so the push
                // landed in a copy and vanished — @ready stayed empty and the
                // element read back as Nil. (A plain `@a.push` works either way,
                // because the Value copy shares the same arr shared_ptr; only the
                // not-there-yet case needs the lvalue.) Mirrors the interpreter,
                // which takes an lvalue invocant for exactly this method set.
                if (m->inv->kind == NK::Index &&
                    (name == "push" || name == "append" || name == "unshift" ||
                     name == "prepend" || name == "ASSIGN-KEY" || name == "BIND-KEY")) {
                    bool hashy = (name == "ASSIGN-KEY" || name == "BIND-KEY");
                    return "([&]()->Value{ Value& __s = " + lvalueExpr(m->inv.get()) + "; "
                           "if (__s.t == VT::Any || __s.t == VT::Nil || __s.t == VT::Type) __s = " +
                           std::string(hashy ? "Value::makeHash()" : "Value::array()") + "; "
                           "return RT.methodCall(__s, " + cesc(name) + ", " + argsVL(m->args) + "); }())";
                }
                return "RT.methodCall(" + ex(m->inv.get()) + ", " + cesc(name) + ", " + argsVL(m->args) + ")";
            }
            case NK::Assign: {
                auto* a = static_cast<Assign*>(e);
                if (a->target->kind == NK::VarExpr && static_cast<VarExpr*>(a->target.get())->declare) {
                    auto* v = static_cast<VarExpr*>(a->target.get());
                    if (v->name.size() <= 1) // `my $ = expr` — anonymous: the value passes through
                        return "(" + coerceFor(a->target.get(), exArg(a->value.get())) + ")";
                    if (hoisted.count(v->name)) { // pre-declared by hoistExprDecls
                        // …but a hoisted declaration was declared WITHOUT its shape
                        if (v->declShape) unsupported("a shaped declaration in expression position");
                        return "(" + mangleVar(v->name) + " = " + coerceFor(a->target.get(), exArg(a->value.get())) + ")";
                    }
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
            case NK::ListExpr: {
                auto* le = static_cast<ListExpr*>(e);
                std::string built = "listToArray({" + argList(le->items) + "})";
                if (le->parenned) // (1, 2, 3) is a List — mirrors the interpreter's eval
                    return "([&]()->Value{ Value _l = " + built + "; _l.isList = true; return _l; }())";
                return built;
            }
            case NK::HashLit:  return "rtHashLit({" + argList(static_cast<HashLit*>(e)->items) + "})";
            case NK::ArrayLit: { // mirror the interpreter's per-item splice rules
                auto* l = static_cast<ArrayLit*>(e);
                std::string s;
                for (size_t i = 0; i < l->items.size(); i++) {
                    if (i) s += ", ";
                    Expr* it = l->items[i].get();
                    bool one = l->items.size() == 1;
                    bool isHyper = it->kind == NK::MethodCall && static_cast<MethodCall*>(it)->hyper;
                    bool atVar = it->kind == NK::VarExpr && !static_cast<VarExpr*>(it)->name.empty()
                              && static_cast<VarExpr*>(it)->name[0] == '@';
                    if (isSlip(it))
                        s += "rtSlipShallow(" + ex(static_cast<Unary*>(it)->operand.get()) + ")";
                    else if (atVar)      // a bare @-variable flattens into the literal
                        s += "rtSlipShallow(" + exArg(it) + ")";
                    else if (isHyper)    // hyper results stay one (itemized) element
                        s += (one ? "rtOneArgItem(" : "rtHyperItem(") + exArg(it) + ")";
                    else if (one)        // single list-valued item spreads (one-arg rule)
                        s += "rtOneArgItem(" + exArg(it) + ")";
                    else if (!l->fromCommaList) // non-comma members: a List splices
                        s += "rtSpliceIfList(" + exArg(it) + ")";
                    else
                        s += exArg(it);
                }
                std::string built = "listToArray({" + s + "})";
                return l->isList ? "rtMarkList(" + built + ")" : built;
            }
            default: unsupported(nkName(e->kind));
        }
    }

    // ---- statements ----
    std::vector<Block*> topLevelEnds; // top-level END phasers, run at program end
    int leaveCtr_ = 0;                // unique names for LEAVE scope guards
    std::set<std::string> topVars_;   // top-level `my` vars hoisted to C++ globals
    std::map<std::string, std::string> topVarTypes_; // …and their declared types, if any
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
        // exceptionFor builds a real exception object (message attr and all) from a
        // builtin RakuError; a bare __e.payload is just the TYPE OBJECT, so
        // `.message` inside CATCH would die and mask the original error
        line(ind, "Value " + exv + " = RT.exceptionFor(__e);");
        line(ind, "RT.dynVarRef(\"$!\") = " + exv + ";");
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
            case NK::NqpOp: { for (auto& x : static_cast<NqpOp*>(e)->args) collectExprDecls(x.get(), out, false); break; }
            case NK::MethodCall: { auto* m = static_cast<MethodCall*>(e); collectExprDecls(m->inv.get(), out, false); for (auto& x : m->args) collectExprDecls(x.get(), out, false); break; }
            case NK::ListExpr: for (auto& x : static_cast<ListExpr*>(e)->items) collectExprDecls(x.get(), out, false); break;
            case NK::ArrayLit: for (auto& x : static_cast<ArrayLit*>(e)->items) collectExprDecls(x.get(), out, false); break;
            case NK::Index: { auto* ix = static_cast<Index*>(e); collectExprDecls(ix->base.get(), out, false); if (ix->index) collectExprDecls(ix->index.get(), out, false); break; }
            case NK::Pair: { auto* pr = static_cast<PairExpr*>(e); if (pr->value) collectExprDecls(pr->value.get(), out, false); break; }
            default: break; // no descent into BlockExpr — its body has its own scope
        }
    }
    void hoistExprDecls(Expr* e, int ind, bool includeRoot = false) {
        std::vector<std::string> names;
        collectExprDecls(e, names, /*root=*/!includeRoot);
        for (auto& n : names)
            if (hoisted.insert(n).second)
                line(ind, declVar(n, "Value::any()") + "; // hoisted `my` from expression position");
    }

    void stmt(Stmt* s, int ind) {
        // Pre-declare expression-position `my`s from every expression slot a
        // statement evaluates — `while (my $line = prompt).defined {…}`,
        // `if my $m = …`, `for my-producing-list`, `return my $x = …`. Raku
        // scopes such a `my` to the enclosing block, which is exactly what
        // hoisting to the current emission point produces.
        switch (s->kind) {
            case NK::ExprStmt:  hoistExprDecls(static_cast<ExprStmt*>(s)->e.get(), ind); break;
            case NK::WhileStmt: hoistExprDecls(static_cast<WhileStmt*>(s)->cond.get(), ind); break;
            case NK::LoopStmt: { auto* l = static_cast<LoopStmt*>(s); // loop (my $i = 0; …; …)
                if (l->init) hoistExprDecls(l->init.get(), ind, /*includeRoot=*/true);
                if (l->cond) hoistExprDecls(l->cond.get(), ind, /*includeRoot=*/true);
                if (l->incr) hoistExprDecls(l->incr.get(), ind, /*includeRoot=*/true);
                break; }
            case NK::ForStmt:   hoistExprDecls(static_cast<ForStmt*>(s)->list.get(), ind); break;
            case NK::ReturnStmt: if (auto* r = static_cast<ReturnStmt*>(s); r->value) hoistExprDecls(r->value.get(), ind); break;
            case NK::IfStmt:
                for (auto& br : static_cast<IfStmt*>(s)->branches) hoistExprDecls(br.first.get(), ind);
                break;
            default: break;
        }
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
                        line(ind, declVar(v->name, "RT.methodCall(" + init + ", "
                                + cesc(name) + ", " + argsVL(mc->args) + ")") + ";");
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
                            line(ind, declVar(v->name, "rtIndexGet(" + tmp +
                                      ", Value::integer(" + std::to_string(k) + "), false)") + ";");
                        }
                        return;
                    }
                }
                // -O int lanes: statement-position int assignment / ++ / -- on plain scalars
                if (optimize_ && e->kind == NK::Assign && tryLaneAssign(static_cast<Assign*>(e), ind)) return;
                if (optimize_ && e->kind == NK::Unary && tryLaneIncDec(static_cast<Unary*>(e), ind)) return;
                if (e->kind == NK::Assign) { line(ind, assign(static_cast<Assign*>(e)) + ";"); return; } // `my $x = ..` / `$x = ..`
                if (e->kind == NK::VarExpr && static_cast<VarExpr*>(e)->declare) { // bare `my $x;` / `my @a;` / `my %h;`
                    auto* dv = static_cast<VarExpr*>(e);
                    const std::string& nm = dv->name;
                    char sigil = nm.empty() ? '$' : nm[0];
                    std::string sh = shapedInit(dv);            // `my @a[3;2];`
                    if (atTopLevel_ && topVars_.count(nm)) {
                        // A global is declared at file scope with a plain default and
                        // initialised here, in program order. A SHAPED one has to be
                        // built here too: its dimensions may be expressions, and the
                        // file-scope initialiser has nowhere to evaluate them.
                        if (!sh.empty()) line(ind, mangleVar(nm) + " = " + sh + ";");
                        else             line(ind, "; // " + nm + " is a global");
                        return;
                    }
                    line(ind, declVar(nm, sh.empty() ? declInit(dv->declType, sigil) : sh) + ";");
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
                            line(ind, declVar(nm, declInit(static_cast<VarExpr*>(it.get())->declType, sigil)) + ";");
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
                        line(ind, declVar(d->names[k], "rtIndexGet(" + tmp +
                                  ", Value::integer(" + std::to_string(k) + "), false)") + ";");
                    return;
                }
                char sigil = d->names[0].empty() ? '$' : d->names[0][0];
                std::string init;
                if (d->init) init = sigil == '@' ? "rtArrayVal(" + exArg(d->init.get()) + ")" : exArg(d->init.get());
                else init = declInit("", sigil); // VarDecl carries no declared type
                line(ind, declVar(d->names[0], init) + ";");
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
                if (!w->var.empty() || !w->params.empty()) unsupported("while EXPR -> $x"); // incl. pointy signatures
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
            // A multi-dim slot is not a plain reference into the top-level buffer;
            // assign() routes `@a[i;j] = v` through ASSIGN-POS before reaching here.
            if (ix->multiDim) unsupported("a multi-dimensional index in this position");
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
            auto* dv = static_cast<VarExpr*>(tgt);
            if (std::string sh = shapedInit(dv, exArg(a->value.get())); !sh.empty()) // `my @a[3;2] = …`
                return atTopLevel_ && topVars_.count(dv->name) ? mangleVar(dv->name) + " = " + sh
                                                               : declVar(dv->name, sh);
            const std::string& nm = static_cast<VarExpr*>(tgt)->name;
            if (nm.size() > 1 && nm[1] == '*') // `my $*X = ..`: dynamics live in the runtime env
                return "RT.dynVarRef(" + cesc(nm) + ") = " + coerceFor(tgt, exArg(a->value.get()));
            if (nm.size() > 1 && nm[0] == '&') codeVars.insert(nm.substr(1));
            if (atTopLevel_ && topVars_.count(nm)) // hoisted to a global: assign it
                return mangleVar(nm) + " = " + coerceFor(tgt, exArg(a->value.get()));
            return declVar(nm, coerceFor(tgt, exArg(a->value.get())));
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
        // `@a[$i; $j] = v` — ASSIGN-POS descends the dimensions and writes the
        // leaf, which is what the interpreter does with the same node.
        if (a->op == "=" && tgt->kind == NK::Index && static_cast<Index*>(tgt)->multiDim) {
            auto* ix = static_cast<Index*>(tgt);
            return "RT.methodCall(" + lvalueExpr(ix->base.get()) + ", \"ASSIGN-POS\", ValueList{"
                 + multiDimArgs(ix) + ", " + rhs + "})";
        }
        if (a->op == "=") return lvalueExpr(tgt) + " = " + coerceFor(tgt, rhs);
        std::string binop = a->op.substr(0, a->op.size() - 1);  // strip '='
        // `@a[$y; $x] += 1` — a multi-dim slot is not a reference into a buffer, so
        // the read and the write are AT-POS and ASSIGN-POS around the operator.
        // The indices are evaluated ONCE, into a list both calls use.
        if (tgt->kind == NK::Index && static_cast<Index*>(tgt)->multiDim) {
            auto* ix = static_cast<Index*>(tgt);
            std::string fb = fastBin(binop);
            std::string nv = binop == "||" ? "RT.boolify(__r) ? __r : (" + rhs + ")"
                           : binop == "&&" ? "RT.boolify(__r) ? (" + rhs + ") : __r"
                           : binop == "//" ? "!rtIsDefined(__r) ? (" + rhs + ") : __r"
                           : binop == "~"  ? "applyArith(\"~\", __r, " + rhs + ")"
                           : !fb.empty()   ? fb + "(__r, " + rhs + ")"
                           : "applyArith(" + cesc(binop) + ", __r, " + rhs + ")";
            return "([&]()->Value{ Value& __b = " + lvalueExpr(ix->base.get()) + ";"
                   " ValueList __ix{" + multiDimArgs(ix) + "};"
                   " Value __r = RT.methodCall(__b, \"AT-POS\", __ix); __r = " + nv + ";"
                   " ValueList __as = __ix; __as.push_back(__r);"
                   " RT.methodCall(__b, \"ASSIGN-POS\", __as); return __r; }())";
        }
        // compound assignment to an index binds the slot once (avoids double side effects)
        if (tgt->kind == NK::Index) {
            std::string ref = lvalueExpr(tgt);
            if (binop == "~") // in-place append (O(n) string building) — default, not -O-gated
                return "([&]()->Value{ Value& __r = " + ref + "; rtCatAssign(__r, " + rhs + "); return __r; }())";
            std::string fb = fastBin(binop);
            std::string nv = binop == "||" ? "RT.boolify(__r) ? __r : (" + rhs + ")"
                           : binop == "&&" ? "RT.boolify(__r) ? (" + rhs + ") : __r"
                           : binop == "//" ? "!rtIsDefined(__r) ? (" + rhs + ") : __r"
                           : !fb.empty() ? fb + "(__r, " + rhs + ")"
                           : "applyArith(" + cesc(binop) + ", __r, " + rhs + ")";
            return "([&]()->Value{ Value& __r = " + ref + "; __r = " + nv + "; return __r; }())";
        }
        std::string lhs = lvalueExpr(tgt);
        if (binop == "||") return lhs + " = RT.boolify(" + lhs + ") ? " + lhs + " : (" + rhs + ")";
        if (binop == "&&") return lhs + " = RT.boolify(" + lhs + ") ? (" + rhs + ") : " + lhs;
        if (binop == "//") return lhs + " = !rtIsDefined(" + lhs + ") ? (" + rhs + ") : " + lhs;
        if (binop == "~") // in-place append (O(n) string building) — default, not -O-gated
            return "([&]()->Value&{ rtCatAssign(" + lhs + ", " + rhs + "); return " + lhs + "; }())";
        if (std::string f = fastBin(binop); !f.empty()) return lhs + " = " + f + "(" + lhs + ", " + rhs + ")"; // -O
        return lhs + " = applyArith(" + cesc(binop) + ", " + lhs + ", " + rhs + ")";
    }

    void ifStmt(IfStmt* f, int ind) {
        for (auto& bp : f->branchParams)
            if (!bp.empty()) unsupported("if EXPR -> ($a, $b) destructuring binder");
        if (!f->elseParams.empty()) unsupported("else -> ($a, $b) destructuring binder");
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
                         body.find("rtCallB(") != std::string::npos || // a stored closure invoked via a builtin can signal too
                         body.find("v_c") != std::string::npos ||      // code-var reads (grep(&f, @a) where &f says `last`)
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
        if (f->rwVars) unsupported("a read-write (<->) loop parameter"); // every branch below binds a COPY
        if (f->destructure) { // for LIST -> ($a, $b) { … } : unpack each element
            // names live in f->vars, or — when the parser produced a real signature
            // (ForStmt.params, one param with a sub-signature) — in that sub-signature
            std::vector<std::string> names = f->vars;
            if (names.empty() && !f->params.empty() && f->params[0].subSig)
                for (auto& sp : *f->params[0].subSig)
                    if (!sp.name.empty()) names.push_back(sp.name);
            std::string lst = gensym("__lst"), el = gensym("__e");
            line(ind, "{");
            line(ind + 1, "Value " + lst + " = rtArrayVal(" + ex(f->list.get()) + ");");
            line(ind + 1, "for (auto& " + el + " : *" + lst + ".arr()) {");
            for (size_t k = 0; k < names.size(); k++)
                line(ind + 2, declVar(names[k], "rtIndexGet(" + el +
                              ", Value::integer(" + std::to_string(k) + "LL), false)") + ";");
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
            line(ind + 1, "for (size_t " + i + " = 0; " + i + " < " + lst + ".arr()->size(); " + i + " += " + std::to_string(n) + ") {");
            for (size_t k = 0; k < n; k++)
                line(ind + 2, declVar(f->vars[k], "(" + i + "+" + std::to_string(k) + " < " + lst +
                              ".arr()->size() ? (*" + lst + ".arr())[" + i + "+" + std::to_string(k) + "] : Value::any())") + ";");
            loopBody(f->body.get(), ind + 2, f->label);
            line(ind + 1, "}");
            line(ind, "}");
            return;
        }
        std::string topic = f->vars.empty() ? gensym("v__t") : mangleVar(f->vars[0]);
        line(ind, "{");
        if (f->list->kind == NK::Range) {
            // `for A..B` counts with a raw long long — but ONLY when the range is
            // numeric. A Str range ('a'..'c', or two Str variables) used to reach
            // this loop too, where `.toInt()` made both endpoints 0 and the body
            // ran exactly once with the topic 0: `for 'a'..'c' { .say }` printed
            // "0" natively and "a b c" interpreted. The endpoints are only known
            // at runtime ($x..$y), so the kind test is a runtime one; the counter
            // then indexes either the integers or a materialised element list.
            auto* r = static_cast<RangeExpr*>(f->list.get());
            std::string rv = gensym("__rv"), lst = gensym("__rl"), isInt = gensym("__ri");
            std::string lo = gensym("__lo"), hi = gensym("__hi"), i = gensym("__i");
            line(ind + 1, "Value " + rv + " = rtRangeVal(" + ex(r->from.get()) + ", " + ex(r->to.get()) +
                          ", " + (r->exFrom ? "true" : "false") + ", " + (r->exTo ? "true" : "false") + ");");
            line(ind + 1, "bool " + isInt + " = (" + rv + ".t == VT::Range && " + rv + ".ofType().empty());");
            line(ind + 1, "Value " + lst + "; long long " + lo + ", " + hi + ";");
            line(ind + 1, "if (" + isInt + ") { " + lo + " = " + rv + ".rFrom() + (" + rv + ".rExFrom() ? 1 : 0); "
                                                 + hi + " = " + rv + ".rTo() - (" + rv + ".rExTo() ? 1 : 0); }");
            line(ind + 1, "else { " + lst + " = rtArrayVal(" + rv + "); " + lo + " = 0; "
                                    + hi + " = (long long)" + lst + ".arr()->size() - 1; }");
            line(ind + 1, "for (long long " + i + " = " + lo + "; " + i + " <= " + hi + "; " + i + "++) {");
            std::string elem = isInt + " ? Value::integer(" + i + ") : (*" + lst + ".arr())[" + i + "]";
            if (!f->vars.empty()) line(ind + 2, declVar(f->vars[0], elem) + ";");
            else line(ind + 2, "Value " + topic + " = " + elem + ";");
            topics.push_back(topic);
            loopBody(f->body.get(), ind + 2, f->label);
            topics.pop_back();
            line(ind + 1, "}");
        } else {
            std::string lst = gensym("__lst"), el = gensym("__e");
            line(ind + 1, "Value " + lst + " = rtArrayVal(" + ex(f->list.get()) + ");");
            line(ind + 1, "for (auto& " + el + " : *" + lst + ".arr()) {");
            if (!f->vars.empty()) line(ind + 2, declVar(f->vars[0], el) + ";");
            else line(ind + 2, "Value " + topic + " = " + el + ";");
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
        if (!g->params.empty() || !g->elseParams.empty())
            unsupported("given/with EXPR -> ($a, $b) destructuring binder");
        if (g->defGuard != 0) { // with / without in value position
            std::string topic = gensym("v__w");
            line(ind, "{");
            line(ind + 1, "Value " + topic + " = " + ex(g->topic.get()) + ";");
            std::string def = "rtIsDefined(" + topic + ")"; // one definedness rule (Failure/enum-type aware), same as `//`
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
        if (!g->params.empty() || !g->elseParams.empty())
            unsupported("given/with EXPR -> ($a, $b) destructuring binder");
        if (g->defGuard != 0) {
            // with / without EXPR { body } [else { elseBody }]: run guarded on (un)definedness
            std::string topic = gensym("v__w");
            line(ind, "{");
            line(ind + 1, "Value " + topic + " = " + ex(g->topic.get()) + ";");
            std::string def = "rtIsDefined(" + topic + ")"; // one definedness rule (Failure/enum-type aware), same as `//`
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
            if (p.named || p.slurpy || p.invocant || p.defaultVal || p.subSig || p.isRw || p.sigil != '$') return false;
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
            {"<", "rtLt"}, {"<=", "rtLe"}, {">", "rtGt"}, {">=", "rtGe"}, {"==", "rtEq"}, {"!=", "rtNe"},
            // string comparisons: plain Str/Str compares byte-wise inline; tagged
            // values (Version/enum/junction/…) fall back to the full chain
            {"eq", "rtEqS"}, {"ne", "rtNeS"}, {"lt", "rtLtS"}, {"gt", "rtGtS"}, {"le", "rtLeS"}, {"ge", "rtGeS"}};
        auto it = m.find(op);
        return it == m.end() ? "" : it->second;
    }

    // ---- -O pass 3: guarded native-int expression lanes ----
    // Straight-line integer arithmetic whose leaves are int literals and plain
    // scalar variables is computed in raw int64: leaf boxes are tag-guarded at
    // runtime, each op is overflow-checked, and the result is stored back into
    // the target's existing box (.i) with no Value construction at all. Any
    // guard, overflow, or domain failure falls through to the untouched boxed
    // emission — the leaves are pure (literals and variable reads), so
    // re-evaluating them on the slow path is safe. Semantics of `%`/`%%` mirror
    // rtMod/rtDivides' int cases exactly (floored mod; 0 divisor → boxed path).
    struct Lane {
        std::vector<std::string> guards; // rtIntBox(...) checks on Value leaves
        std::vector<std::string> steps;  // "…; if (…) break;" tmp defs + overflow/domain checks
    };
    // A scalar a lane may read: a plain lexical/global `$name`, or the current
    // topic. Returns the C++ lvalue name ("" = not laneable).
    std::string laneVar(VarExpr* v) {
        const std::string& n = v->name;
        if (n == "$_") return topics.empty() ? "" : topics.back();
        if (n.size() < 2 || n[0] != '$') return "";
        char c1 = n[1];
        if (!(ascii::isalpha((unsigned char)c1) || c1 == '_')) return ""; // $!, $/, $0, $*X, $?X, $.x …
        return mangleVar(n);
    }
    // Compile e into the lane; returns a C++ `long long` rvalue ("" = lane fails).
    std::string laneInt(Expr* e, Lane& L) {
        switch (e->kind) {
            case NK::IntLit: {
                auto* n = static_cast<IntLit*>(e);
                if (!n->big.empty()) return "";
                return std::to_string(n->v) + "LL";
            }
            case NK::VarExpr: {
                std::string lv = laneVar(static_cast<VarExpr*>(e));
                if (lv.empty()) return "";
                L.guards.push_back("rtIntBox(" + lv + ")");
                return lv + ".i";
            }
            case NK::Unary: {
                auto* u = static_cast<Unary*>(e);
                if (u->postfix || u->op != "-") return "";
                std::string a = laneInt(u->operand.get(), L);
                if (a.empty()) return "";
                std::string t = gensym("__ln");
                L.steps.push_back("long long " + t + "; if (rakupp::sub_ovf(0LL, " + a + ", &" + t + ")) break;");
                return t;
            }
            case NK::Binary: {
                auto* b = static_cast<Binary*>(e);
                const std::string& op = b->op;
                std::string ovf = op == "+" ? "add_ovf" : op == "-" ? "sub_ovf" : op == "*" ? "mul_ovf" : "";
                if (ovf.empty() && op != "%") return "";
                std::string a = laneInt(b->lhs.get(), L); if (a.empty()) return "";
                std::string c = laneInt(b->rhs.get(), L); if (c.empty()) return "";
                std::string t = gensym("__ln");
                if (op == "%") { // rtMod's int case: floored modulo; 0 divisor → boxed path
                    L.steps.push_back("if (" + c + " == 0) break;");
                    L.steps.push_back("long long " + t + " = " + a + " % " + c + "; if (" + t
                                    + " != 0 && ((" + t + " < 0) != (" + c + " < 0))) " + t + " += " + c + ";");
                } else {
                    L.steps.push_back("long long " + t + "; if (rakupp::" + ovf + "(" + a + ", " + c + ", &" + t + ")) break;");
                }
                return t;
            }
            default: return "";
        }
    }
    // Compile a boolean condition into the lane: int comparisons and `%%`.
    std::string laneBool(Expr* e, Lane& L) {
        if (e->kind != NK::Binary || hasWhatever(e)) return "";
        auto* b = static_cast<Binary*>(e);
        static const std::set<std::string> cmp = {"<", "<=", ">", ">=", "==", "!="};
        if (cmp.count(b->op)) {
            std::string a = laneInt(b->lhs.get(), L); if (a.empty()) return "";
            std::string c = laneInt(b->rhs.get(), L); if (c.empty()) return "";
            return "(" + a + " " + b->op + " " + c + ")";
        }
        if (b->op == "%%") { // rtDivides' int case; 0 divisor → boxed path (throws)
            std::string a = laneInt(b->lhs.get(), L); if (a.empty()) return "";
            std::string c = laneInt(b->rhs.get(), L); if (c.empty()) return "";
            L.steps.push_back("if (" + c + " == 0) break;");
            return "(" + a + " % " + c + " == 0)";
        }
        return "";
    }
    static std::string joinAnd(const std::vector<std::string>& v) {
        std::string s;
        for (auto& g : v) { if (!s.empty()) s += " && "; s += g; }
        return s;
    }
    // Statement-position `$x = <int expr>` / `$x op= <int expr>` on a plain
    // scalar: compute native, store into the existing box; the boxed emission
    // is the fallback. Returns true if the lane was emitted.
    bool tryLaneAssign(Assign* a, int ind) {
        if (a->target->kind != NK::VarExpr) return false;
        auto* tv = static_cast<VarExpr*>(a->target.get());
        if (tv->declare) return false;                 // `my $x = …` declares a C++ var
        std::string lv = laneVar(tv);
        if (lv.empty()) return false;
        const std::string& op = a->op;
        bool plain = op == "=";
        if (!plain && op != "+=" && op != "-=" && op != "*=" && op != "%=") return false;
        Lane L;
        std::string r = laneInt(a->value.get(), L);
        if (r.empty()) return false;
        std::string store;
        if (plain) {
            // overwrite: reuse the box when it's a plain int slot, else replace it
            store = "if (rtIntSlot(" + lv + ")) " + lv + ".i = " + r + "; else " + lv + " = Value::integer(" + r + ");";
        } else {
            L.guards.push_back("rtIntSlot(" + lv + ")");
            std::string binop = op.substr(0, op.size() - 1), t = gensym("__ln");
            if (binop == "%") {
                L.steps.push_back("if (" + r + " == 0) break;");
                L.steps.push_back("long long " + t + " = " + lv + ".i % " + r + "; if (" + t
                                + " != 0 && ((" + t + " < 0) != (" + r + " < 0))) " + t + " += " + r + ";");
            } else {
                std::string ovf = binop == "+" ? "add_ovf" : binop == "-" ? "sub_ovf" : "mul_ovf";
                L.steps.push_back("long long " + t + "; if (rakupp::" + ovf + "(" + lv + ".i, " + r + ", &" + t + ")) break;");
            }
            store = lv + ".i = " + t + ";";
        }
        std::string ok = gensym("__lok");
        line(ind, "{ bool " + ok + " = false; do { // -O int lane");
        if (!L.guards.empty()) line(ind + 1, "if (!(" + joinAnd(L.guards) + ")) break;");
        for (auto& s : L.steps) line(ind + 1, s);
        line(ind + 1, store + " " + ok + " = true;");
        line(ind, "} while (0);");
        line(ind, "if (!" + ok + ") { " + assign(a) + "; } }");
        return true;
    }
    // Statement-position `$x++` / `$x--` / `++$x` / `--$x` on a plain scalar.
    bool tryLaneIncDec(Unary* u, int ind) {
        if (u->op != "++" && u->op != "--") return false;
        if (u->operand->kind != NK::VarExpr) return false;
        std::string lv = laneVar(static_cast<VarExpr*>(u->operand.get()));
        if (lv.empty()) return false;
        std::string ok = gensym("__lok"), t = gensym("__ln");
        std::string ovf = u->op == "++" ? "add_ovf" : "sub_ovf";
        line(ind, "{ bool " + ok + " = false; do { // -O int lane: " + u->op);
        line(ind + 1, "if (!rtIntSlot(" + lv + ")) break;");
        line(ind + 1, "long long " + t + "; if (rakupp::" + ovf + "(" + lv + ".i, 1LL, &" + t + ")) break;");
        line(ind + 1, lv + ".i = " + t + "; " + ok + " = true;");
        line(ind, "} while (0);");
        line(ind, "if (!" + ok + ") { " + ex(u) + "; } }");
        return true;
    }

    // ---- sub definitions ----
    // Emit binding lines that pull each parameter out of the call's `__a`
    // ValueList — handling positional, named, optional/default, and slurpy.
    // hasSelf: `__a[0]` is the invocant (methods), so positionals start at 1.
    void bindParams(const std::vector<Param>& ps, int ind, bool hasSelf) {
        size_t pi = hasSelf ? 1 : 0;
        int anon = 0;
        for (const Param& p : ps) {
            // a param mutated by an inner closure binds as a shared cell (declVar)
            auto bind = [&](const std::string& init) {
                if (p.name.empty()) line(ind, "Value __anon" + std::to_string(anon++) + " = " + init + ";");
                else line(ind, declVar(p.name, init) + ";");
            };
            std::string pos = std::to_string(pi);
            if (p.isRw && !p.named && !p.slurpy && !p.invocant && !p.name.empty()) {
                // `is rw`: bind a reference into the caller-visible ValueList slot;
                // the call site copies it back into the argument's lvalue.
                if (cellVars_.count(p.name)) unsupported("an `is rw` parameter mutated by a closure");
                line(ind, "Value& " + mangleVar(p.name) + " = rtPosRef(__a, " + pos + ");");
                pi++;
                continue;
            }
            if (p.invocant) { bind("__self"); continue; }
            if (p.slurpy) {
                bind(p.sigil == '%' ? "rtSlurpyNamed(__a)" : "rtSlurpyPos(__a, " + pos + ")");
                continue;
            }
            if (p.named) {
                // Every key this param answers to, in the interpreter's lookup
                // order: the primary (`:x($v)` renames to x — the var name does
                // NOT answer), then the var name when `:x(:$v)` aliases both,
                // then the nested layers of `:x(:y(:z($a)))`. Emitted as a
                // rtHasNamed ternary chain, so `:r(:$string)` binds -r and
                // --string alike — it used to look up only "string".
                std::vector<std::string> keys;
                std::string bare = p.name.size() > 1 ? p.name.substr(1) : "";
                keys.push_back(p.namedKey.empty() ? bare : p.namedKey);
                if (p.aliasBoth && !p.namedKey.empty() && !bare.empty() && bare != p.namedKey)
                    keys.push_back(bare);
                for (auto& ak : p.aliasKeys)
                    if (std::find(keys.begin(), keys.end(), ak) == keys.end()) keys.push_back(ak);
                std::string chain;
                for (size_t i = 0; i + 1 < keys.size(); i++)
                    chain += "rtHasNamed(__a, " + cesc(keys[i]) + ") ? rtNamed(__a, " + cesc(keys[i]) + ") : ";
                std::string lastKey = cesc(keys.back());
                if (p.defaultVal) bind(chain + "(rtHasNamed(__a, " + lastKey + ") ? rtNamed(__a, " + lastKey + ") : (" + ex(p.defaultVal.get()) + "))");
                else bind(chain + "rtNamed(__a, " + lastKey + ")");
                continue;
            }
            if (p.defaultVal) bind("rtHasPos(__a, " + pos + ") ? rtPos(__a, " + pos + ") : (" + ex(p.defaultVal.get()) + ")");
            else bind("rtPos(__a, " + pos + ")");
            if (p.name == "$/" || p.name == "$!") boundSpecials.insert(p.name);
            if (p.name.size() > 1 && p.name[0] == '&') codeVars.insert(p.name.substr(1)); // sub bin(&op) — op(...) calls the param
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
        // an indirect ::() name exists only when the declaration RUNS — the
        // AOT path evaluates it; native emission cannot
        if (cd->nameExpr) unsupported("a type with an indirect ::() name");
        for (auto& mp : cd->methods)
            if (mp->nameExpr) unsupported("a method with an indirect ::() name");
        std::map<std::string, int> multiSeq; // per-name candidate counter
        for (auto& mp : cd->methods) {
            SubDecl* md = mp.get();
            std::string fname = md->isMulti ? methodCandFn(cd->name, md->name, multiSeq[md->name]++)
                                            : methodFn(cd->name, md->name);
            BodyScope __bs{this, /*closure=*/false};
            line(0, "static Value " + fname + "(ValueList& __a) {");
            line(1, "try {"); // a METHOD body is a ReturnEx boundary too (same rule as bodyDef)
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
            line(1, "} catch (ReturnEx& __r) { return __r.v; }");
            self_ = saved;
            line(0, "}");
        }
    }

    // Does an ancestor of `cd` declare a method named `m`? `unknown` is set when
    // the ancestry leaves the set of classes this program declares — a built-in
    // parent, or one that came from a module — because there we cannot prove it
    // does not. `seen` breaks a declaration cycle.
    bool ancestorDeclares(ClassDecl* cd, const std::string& m, bool& unknown,
                          std::set<ClassDecl*>& seen) {
        if (!seen.insert(cd).second) return false;
        std::vector<std::string> ps;
        if (!cd->parent.empty()) ps.push_back(cd->parent);
        for (auto& p : cd->extraParents) ps.push_back(p);
        for (auto& pn : ps) {
            auto it = classDecls_.find(pn);
            if (it == classDecls_.end()) { unknown = true; continue; }
            // a submethod is NOT inherited, so it is never the candidate a child's
            // failed dispatch would fall through to
            for (auto& mp : it->second->methods)
                if (mp->name == m && !mp->isSubmethod) return true;
            if (ancestorDeclares(it->second, m, unknown, seen)) return true;
        }
        return false;
    }

    // The multi-method dispatcher emitted below guards on POSITIONAL arity and
    // nominal type, nothing else. Whatever it cannot decide must go to the
    // interpreter rather than be decided WRONGLY — the same call multiDef()
    // already makes for multi subs. Three things it cannot decide:
    //   · a `where` clause or a :D/:U smiley never enters the guard;
    //   · a named parameter is invisible to it, so a candidate with a REQUIRED
    //     named matches a call that passes none and binds it to Any — that is
    //     how `K.new.g` returned "k" where the interpreter returns "k1";
    //   · a candidate declared in an ANCESTOR is unreachable, and in Rakudo a
    //     multi's candidate set spans the MRO (the interpreter defers up the
    //     chain — the parentNext branch in Interpreter::invokeMethod).
    const char* undecidableMulti(ClassDecl* cd, const std::string& mname,
                                 const std::vector<SubDecl*>& cands) {
        for (SubDecl* c : cands)
            for (auto& pp : c->params) {
                if (pp.whereExpr || pp.defConstraint) return "a where/:D constraint";
                if (pp.named && !pp.slurpy)           return "a named parameter";
            }
        bool unknown = false;
        std::set<ClassDecl*> seen;
        if (ancestorDeclares(cd, mname, unknown, seen)) return "a candidate in a parent class";
        if (unknown) return "a parent class this compilation cannot see";
        return nullptr;
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
                if (const char* why = undecidableMulti(cd, kv.first, kv.second))
                    unsupported(std::string("a multi method with ") + why +
                                " (" + cd->name + "." + kv.first + ")");
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
                d += " throw RakuError{Value::typeObj(\"X::Multi::NoMatch\"), \"No matching multi-method candidate for " + kv.first + "\"}; });";
                line(1, d);
            }
        }
        if (cd->isGrammar) { // grammar rules are pattern strings — the embedded engine runs them
            line(1, "  " + ci + "->isGrammar = true;");
            // same implicit ancestor the interpreter gives a parentless grammar
            if (cd->parent.empty())
                line(1, "  if (!" + ci + "->parent && " + ci + "->nativeParent.empty()) " + ci + "->nativeParent = \"Grammar\";");
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
        // The class BODY's `my` initialisers. The variables were hoisted to
        // globals in the pre-pass; run their initialisers here, at registration
        // time — the interpreter runs a class body when the declaration runs,
        // which is likewise before the program body. atTopLevel_ makes the
        // assign() path target the global rather than declaring a C++ local
        // that would immediately go out of scope.
        bool savedTop = atTopLevel_;
        atTopLevel_ = true;
        for (auto& bs : cd->body)
            if (bs->kind == NK::ExprStmt) stmt(bs.get(), 1);
        atTopLevel_ = savedTop;
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
        std::set<std::string> params;
        for (auto& p : ps) if (!p.name.empty()) params.insert(p.name);
        analyzeCells(body, params);
        if (fast) {
            // -O: direct-Value signature (params are the C++ args themselves — no
            // ValueList). A param mutated by an inner closure takes the sig slot
            // under a synthetic name and re-binds through a shared cell.
            bool anyCell = false;
            for (auto& p : ps) if (cellVars_.count(p.name)) anyCell = true;
            std::string sig, fwd;
            for (size_t i = 0; i < ps.size(); i++) {
                if (i) { sig += ", "; fwd += ", "; }
                sig += "Value " + (anyCell ? "__p" + std::to_string(i) : mangleVar(ps[i].name));
                fwd += "rtPos(__a, " + std::to_string(i) + ")";
            }
            line(0, "static Value " + fnName + "(" + sig + ") {");
            line(1, "try {");
            if (anyCell)
                for (size_t i = 0; i < ps.size(); i++)
                    line(2, declVar(ps[i].name, "std::move(__p" + std::to_string(i) + ")") + ";");
            emitBody(body);
            line(1, "} catch (ReturnEx& __r) { return __r.v; }");
            line(0, "}");
            // boxed adapter so named/slurpy/multi call sites still resolve
            line(0, "static Value " + fnName + "(ValueList __a) { return " + fnName + "(" + fwd + "); }");
            return;
        }
        bool hasRw = false;
        for (auto& p : ps) if (p.isRw && !p.named && !p.slurpy && !p.invocant) hasRw = true;
        line(0, "static Value " + fnName + "(ValueList" + (hasRw ? "&" : "") + " __a) {");
        // A ROUTINE BODY IS A ReturnEx BOUNDARY. `return` itself compiles to a C++
        // return, so this catches the ones thrown from elsewhere — a builtin such
        // as `fail`, or an interpreter-evaluated callback. Without it they escaped
        // main() and the binary died with "terminating due to uncaught exception of
        // type rakupp::ReturnEx", where the interpreter answered normally. The
        // interpreter states the same boundary at Interpreter.cpp:6524.
        line(1, "try {");
        bindParams(ps, 2, false);
        emitBody(body);
        line(1, "} catch (ReturnEx& __r) { return __r.v; }");
        line(0, "}");
    }
    void subDef(SubDecl* d) {
        if (d->isNative) { nativeSubDef(d); return; }
        bodyDef(mangleSub(d->name), d->params, d->body, fastSubs.count(d->name) > 0);
    }

    // A multi: emit each candidate, then a dispatcher that tries candidates
    // most-specific first (most type constraints) and picks the first that matches.
    void multiDef(const std::string& name, std::vector<SubDecl*> cands) {
        // a `where` clause or :D/:U smiley never enters the guards below, so the
        // dispatcher would pick by declaration order — silently the WRONG candidate.
        // Fall back to the interpreter for such multis instead.
        for (SubDecl* c : cands)
            for (auto& p : c->params)
                if (p.whereExpr || p.defConstraint)
                    unsupported("a multi candidate with a where/:D constraint");
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
        line(1, "throw RakuError{Value::typeObj(\"X::Multi::NoMatch\"), \"No matching multi candidate for " + name + "\"};");
        line(0, "}");
    }
};

} // namespace

// The signature blob a compiled binary hands RT.registerCompiledMain: the MAIN
// candidates with their bodies detached, written by the SAME serializer as the
// module cache — so the compiled dispatcher scores and prints usage from the
// very Params the interpreter would build for this source. The decls are moved
// into a scratch Program for the write and restored afterwards; on any
// serializer error the AST is restored and "" is returned, which downgrades
// the binary to the legacy direct call instead of failing the compile.
static std::string mainSigBlob(Program& prog) {
    std::vector<std::pair<size_t, std::vector<StmtPtr>>> stash; // stmt index + its body
    // `where` clauses stay OUT of the blob: scoring evaluates them, and one that
    // reaches past its own parameter (`where * < $limit`) would look $limit up
    // in the runtime env, where compiled lexicals never go — the throw would
    // read as "no match" and refuse an argv the interpreter accepts. Unscored
    // is the pre-protocol permissiveness; sometimes-wrong would be worse.
    std::vector<std::pair<Param*, ExprPtr>> wheres;
    for (size_t i = 0; i < prog.stmts.size(); i++) {
        if (prog.stmts[i]->kind != NK::SubDecl) continue;
        auto* d = static_cast<SubDecl*>(prog.stmts[i].get());
        if (d->name != "MAIN" || d->isProto) continue; // a proto is not a candidate
        for (auto& p : d->params)
            if (p.whereExpr) {
                wheres.push_back({&p, std::move(p.whereExpr)});
                p.hadWhere = true; // the blob's copy still stringifies as `where { ... }`
            }
        stash.push_back({i, std::move(d->body)});
        d->body.clear();
    }
    if (stash.empty()) return "";
    Program sig;
    for (auto& s : stash) sig.stmts.push_back(std::move(prog.stmts[s.first]));
    std::string blob;
    try { blob = serializeAst(sig); } catch (...) { blob.clear(); }
    for (size_t k = 0; k < stash.size(); k++) {
        prog.stmts[stash[k].first] = std::move(sig.stmts[k]);
        static_cast<SubDecl*>(prog.stmts[stash[k].first].get())->body = std::move(stash[k].second);
    }
    for (auto& w : wheres) { w.first->whereExpr = std::move(w.second); w.first->hadWhere = false; }
    return blob;
}

std::string transpileToCpp(Program& prog, bool optimize, const std::string& srcPath,
                           const std::set<std::string>& moduleExports) {
    const std::string mainSig = mainSigBlob(prog);
    Codegen g;
    g.optimize_ = optimize;
    g.moduleExports_ = moduleExports;
    // pre-pass: collect top-level sub declarations (for forward refs) and enum
    // values (bound as globals so subs can see them).
    std::vector<SubDecl*> subs;
    std::vector<ClassDecl*> classes;
    std::map<std::string, std::vector<SubDecl*>> multiCands;
    std::vector<std::pair<std::string, long long>> enumConsts;
    // Walk a statement's `my` declarations, handing each (name, declared type)
    // to `fn`. Used for BOTH the top level and class bodies (see the ClassDecl
    // branch below), so the two cannot drift.
    using MyDeclFn = std::function<void(const std::string&, const std::string&)>;
    auto forEachMyDecl = [](Stmt* st, const MyDeclFn& fn) {
        auto one = [&](Expr* x) {
            if (x->kind != NK::VarExpr || !static_cast<VarExpr*>(x)->declare) return;
            const std::string& nm = static_cast<VarExpr*>(x)->name;
            // sigilled vars need a name beyond the sigil; sigilless (constant \W) are fine at 1 char
            bool sigilled = !nm.empty() && (nm[0] == '$' || nm[0] == '@' || nm[0] == '%' || nm[0] == '&');
            if (nm.size() > 1 && nm[1] == '*') return; // dynamics live in the runtime env
            if (sigilled ? nm.size() > 1 : !nm.empty()) fn(nm, static_cast<VarExpr*>(x)->declType);
        };
        if (!st || st->kind != NK::ExprStmt) return;
        Expr* e = static_cast<ExprStmt*>(st)->e.get();
        if (!e) return;
        if (e->kind == NK::Assign) one(static_cast<Assign*>(e)->target.get());
        else if (e->kind == NK::ListExpr)
            for (auto& it : static_cast<ListExpr*>(e)->items) one(it.get());
        else one(e);
    };
    MyDeclFn asTopVar = [&](const std::string& nm, const std::string& dt) {
        g.topVars_.insert(nm);
        if (!dt.empty()) g.topVarTypes_[nm] = dt;
    };
    // class-body `my` names, checked for collisions once the whole file is seen
    std::map<std::string, std::pair<std::string, std::string>> classBodyVars; // name -> (class, declType)
    for (auto& s : prog.stmts) {
        if (s->kind == NK::SubDecl) {
            auto* d = static_cast<SubDecl*>(s.get());
            if (d->isMethod) throw CodegenError{"a method sub at statement level"};
            if (d->name.empty()) throw CodegenError{"an anonymous sub at statement level"};
            if (d->isMulti) { multiCands[d->name].push_back(d); g.multiNames.insert(d->name); }
            else {
                g.userSubs[d->name] = (int)d->params.size(); subs.push_back(d);
                // native subs emit the ValueList bridge only — no fast-sig overload
                if (optimize && !d->isNative && Codegen::simpleSig(d->params)) g.fastSubs[d->name] = (int)d->params.size();
                { int pos = 0; std::vector<int> rw;
                  for (auto& p : d->params) { if (p.named || p.slurpy || p.invocant) continue;
                      if (p.isRw) rw.push_back(pos); pos++; }
                  if (!rw.empty()) g.rwSubs[d->name] = rw; }
            }
        } else if (s->kind == NK::ClassDecl) {
            auto* cd = static_cast<ClassDecl*>(s.get());
            if (cd->isRole || cd->isPackage) throw CodegenError{"a role/package"};
            g.classNames.insert(cd->name);
            g.classDecls_[cd->name] = cd;
            classes.push_back(cd);
            // A class-BODY `my` variable is lexically visible to that class's
            // methods (`class C { my %h = …; method m { %h<a> } }`). Methods are
            // emitted as free functions, so the variable has to live where they
            // can see it — the same globals table top-level `my` uses. Without
            // this the body statement was dropped entirely and the method
            // referenced an undeclared identifier: a hard C++ compile error
            // (found 2026-08-13 compiling a grammar's action class).
            for (auto& bs : cd->body)
                forEachMyDecl(bs.get(), [&](const std::string& nm, const std::string& dt) {
                    auto it = classBodyVars.find(nm);
                    if (it != classBodyVars.end() && it->second.first != cd->name)
                        throw CodegenError{"the same class-body `my " + nm + "` in two classes"};
                    classBodyVars[nm] = {cd->name, dt};
                });
        } else if (s->kind == NK::ExprStmt) {
            forEachMyDecl(s.get(), asTopVar); // top-level `my` → C++ global, so subs see it
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

    // Class-body `my` variables share the globals table with the top level,
    // and C++ scoping then does the right thing for anything a method declares
    // or binds itself (a local or a parameter of the same name shadows it,
    // which is also the Raku answer). What it CANNOT express is two live
    // bindings of one name — a class-body `my $n` alongside a top-level
    // `my $n` would collapse into a single global, and the top-level
    // initialiser (which runs later) would silently win inside the method.
    // Refuse that: CodegenError falls back to AOT bundling, which is slower
    // but correct — never a wrong answer.
    for (auto& kv : classBodyVars) {
        if (g.topVars_.count(kv.first))
            throw CodegenError{"a class-body `my " + kv.first + "` shadowing a top-level variable"};
        asTopVar(kv.first, kv.second.second);
    }

    g.out << "// Generated by `rakupp --exe` — native transpilation of a Raku program.\n"
             "#include \"Interpreter.h\"\n#include \"Value.h\"\n#include <cmath>\n#include <vector>\n#include <string>\n#include <iostream>\n#include <utility>\n"
             "using namespace rakupp;\n"
             "// The runtime interpreter is constructed on FIRST USE — which happens on\n"
             "// the big-stack body thread, not the OS main thread. Its constructor\n"
             "// initialises thread-local state (current scope, GIL registration) for\n"
             "// the constructing thread, so it must run where the program runs —\n"
             "// exactly as bundle mode builds its Interpreter inside the worker.\n"
             "static Interpreter& __rakupp_RT() { static Interpreter rt; return rt; }\n"
             "#define RT __rakupp_RT()\n\n";

    // top-level `my` vars as globals (initialised in program order inside main)
    for (auto& nm : g.topVars_) {
        char sg = nm[0];
        auto ty = g.topVarTypes_.find(nm);
        g.out << "static Value " << mangleVar(nm) << " = "
              << declInit(ty == g.topVarTypes_.end() ? "" : ty->second, sg) << ";\n";
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
        g.out << "static Value " << mangleSub(d->name) << (g.rwSubs.count(d->name) ? "(ValueList&);\n" : "(ValueList);\n");
    }
    for (auto& mc : multiCands)
        g.out << "static Value " << mangleSub(mc.first) << "(ValueList);\n";
    for (ClassDecl* cd : classes)
        for (auto& mp : cd->methods)
            g.out << "static Value " << g.methodFn(cd->name, mp->name) << "(ValueList&);\n";
    g.out << "\n";

    // Generate all code into buffers FIRST (definitions, class registration,
    // program body), so the full set of builtin call sites is known before we
    // emit the cached `__bfN` pointer declarations they reference.
    std::string defs = g.capture([&]() {
        for (SubDecl* d : subs) { g.subDef(d); g.out << "\n"; }
        for (auto& mc : multiCands) { g.multiDef(mc.first, mc.second); g.out << "\n"; }
        for (ClassDecl* cd : classes) { g.classMethodDefs(cd); g.out << "\n"; }
    });
    std::string reg = g.capture([&]() {
        for (ClassDecl* cd : classes) g.classRegister(cd);
    });
    std::string body = g.capture([&]() {
        g.atTopLevel_ = true;
        g.analyzeCells(prog.stmts, {});
        g.emitSeq(prog.stmts, 2, /*topLevel=*/true);
        g.atTopLevel_ = false;
        // auto-invoke MAIN through the interpreter's own command-line protocol
        // (pairing, candidate scoring, usage/--help) via the metadata that
        // __rakupp_register adopted — see mainSigBlob above. Without a blob
        // runCompiledMain degrades to the old direct call.
        bool hasMain = false;
        for (auto& s : prog.stmts)
            if (s->kind == NK::SubDecl && static_cast<SubDecl*>(s.get())->name == "MAIN")
                hasMain = true;
        if (hasMain)
            g.line(2, "__rakupp_exit = RT.runCompiledMain(&__rakupp_main_entry);");
        for (auto it = g.topLevelEnds.rbegin(); it != g.topLevelEnds.rend(); ++it) g.emitPhaserBody(*it, 2);
    });

    // cached builtin pointers: declared before the code that uses them,
    // resolved once at startup in __rakupp_register (see rtCallB)
    std::vector<const std::string*> bfNames(g.usedBuiltins_.size());
    for (auto& kv : g.usedBuiltins_) bfNames[kv.second] = &kv.first;
    for (size_t i = 0; i < bfNames.size(); i++)
        g.out << "static const BuiltinFn* __bfp" << i << " = nullptr; // " << *bfNames[i] << "\n";
    if (!bfNames.empty()) g.out << "\n";

    g.out << defs;

    // MAIN's uniform entry point plus its signature blob (single subs take
    // ValueList&, the multi dispatcher ValueList by value — the wrapper's
    // by-ref parameter converts for both). hasMainProg mirrors the body
    // emitter's own hasMain scan.
    bool hasMainProg = false;
    for (auto& s : prog.stmts)
        if (s->kind == NK::SubDecl && static_cast<SubDecl*>(s.get())->name == "MAIN")
            hasMainProg = true;
    if (hasMainProg) {
        g.out << "static Value __rakupp_main_entry(ValueList& __a) { return "
              << mangleSub("MAIN") << "(__a); }\n";
        if (!mainSig.empty()) {
            g.out << "static const unsigned char __rakupp_main_sig[] = {";
            for (size_t i = 0; i < mainSig.size(); i++)
                g.out << (i % 24 == 0 ? "\n    " : "") << (unsigned)(unsigned char)mainSig[i] << ",";
            g.out << "\n};\n";
        }
        g.out << "\n";
    }

    // startup registration: resolve builtin pointers first (class registration
    // may call builtins), then register classes/enums
    g.out << "static void __rakupp_register() {\n";
    for (size_t i = 0; i < bfNames.size(); i++)
        g.out << "    __bfp" << i << " = RT.builtinPtr(" << cesc(*bfNames[i]) << ");\n";
    // early, so $*USAGE inside the program body already answers with the real text
    if (hasMainProg && !mainSig.empty())
        g.out << "    RT.registerCompiledMain(__rakupp_main_sig, sizeof __rakupp_main_sig, &__rakupp_main_entry);\n";
    // a user &USAGE takes over the failed-dispatch text (mainProtocol looks it
    // up in the runtime env, where top-level compiled subs otherwise never go)
    if (hasMainProg)
        for (SubDecl* d : subs)
            if (d->name == "USAGE") {
                g.out << "    RT.dynVarRef(\"&USAGE\") = Value::closure([](ValueList& __a) -> Value { return "
                      << mangleSub("USAGE") << "(__a); });\n";
                break;
            }
    g.out << reg << "}\n\n";

    // main()
    g.out << "namespace rakupp { int rakuppMainOnBigStack(int (*)(void*), void*); void setConsoleUtf8(); }\n"
             "static int __rakupp_main_body(void* __ctxp) {\n"
             "    int argc = static_cast<std::pair<int, char**>*>(__ctxp)->first;\n"
             "    char** argv = static_cast<std::pair<int, char**>*>(__ctxp)->second;\n"
             "    { std::vector<std::string> a; for (int i = 1; i < argc; i++) a.push_back(argv[i]); RT.setArgs(a); }\n"
             "    RT.srcFile_ = " + cesc(srcPath) + ";\n    __rakupp_register();\n"
             "    int __rakupp_exit = 0; (void)__rakupp_exit;\n" // set by the MAIN protocol (usage exits 2, --help 0)
             "    try {\n";
    g.out << body;
    g.out << "    } catch (const ExitEx& e) { std::cout.flush(); return e.code; }\n"
             "    catch (const LastEx&) { std::cerr << \"last without loop construct\\n\"; return 1; }\n"
             "    catch (const NextEx&) { std::cerr << \"next without loop construct\\n\"; return 1; }\n"
             "    catch (const RedoEx&) { std::cerr << \"redo without loop construct\\n\"; return 1; }\n"
             "    catch (const RakuError& e) { std::cerr << e.message << \"\\n\"; return 1; }\n"
             "    catch (const std::exception& e) { std::cerr << \"Internal error: \" << e.what() << \"\\n\"; return 3; }\n"
             "    return __rakupp_exit;\n}\n"
             "// The whole program runs on the interpreter's 1 GiB big-stack thread:\n"
             "// the OS default main stack (8 MiB, 1 MB on Windows) would give native\n"
             "// recursion a far smaller budget than the interpreter, on every platform.\n"
             "int main(int argc, char** argv) {\n"
             "    rakupp::setConsoleUtf8();  // Windows: UTF-8 console output (no-op elsewhere)\n"
             "    std::pair<int, char**> __ctx{argc, argv};\n"
             "    return rakupp::rakuppMainOnBigStack(&__rakupp_main_body, &__ctx);\n}\n";
    return g.out.str();
}

}
