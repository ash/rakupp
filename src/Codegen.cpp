#include "Codegen.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace rakupp {

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
        case NK::ChainExpr:   return "a chained comparison";
        case NK::Pair:        return "a pair";
        default:              return "an unsupported construct";
    }
}

// C++ string-literal escape.
std::string cesc(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '\\' || c == '"') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') o += "\\r";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\x%02x", c); o += b; }
        else o += (char)c;
    }
    o += "\"";
    return o;
}

// $foo / @foo -> v_foo (sigil dropped); non-identifier chars -> '_'.
std::string mangleVar(const std::string& name) {
    std::string s = name;
    if (!s.empty() && (s[0] == '$' || s[0] == '@' || s[0] == '%' || s[0] == '&')) s = s.substr(1);
    std::string o = "v_";
    for (char c : s) o += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
    return o;
}
std::string mangleSub(const std::string& name) {
    std::string o = "u_";
    for (char c : name) o += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
    return o;
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

    bool inWC = false; // currently emitting a WhateverCode body
    int wcArity = 0;   // number of `*` slots consumed

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
            default: return false;
        }
    }

    // Expression in value position: a `*`-bearing expression becomes a WhateverCode closure.
    std::string exArg(Expr* e) {
        // A regex literal in argument position is the Regex object (not a $_ match).
        if (e->kind == NK::RegexLit)
            return "Value::regex(" + cesc(static_cast<RegexLit*>(e)->pattern) + ")";
        if (!hasWhatever(e)) return ex(e);
        if (inWC) unsupported("nested WhateverCode");
        inWC = true; wcArity = 0;
        std::string body = ex(e);
        inWC = false;
        return "Value::closure([=](ValueList& __w)->Value{ return " + body + "; })";
    }

    std::string argList(const std::vector<ExprPtr>& args) {
        std::string s;
        for (size_t i = 0; i < args.size(); i++) { if (i) s += ", "; s += exArg(args[i].get()); }
        return s;
    }

    // A block `{ ... }` / pointy `-> $x { ... }` becomes a native closure.
    std::string emitBlockClosure(BlockExpr* be) {
        bool pushed = false; std::string topic;
        std::string body = capture([&]() {
            if (be->params.empty()) {
                topic = gensym("v__t");
                line(0, "Value " + topic + " = (__a.size() > 0 ? __a[0] : Value::any());");
                topics.push_back(topic); pushed = true;
            } else {
                for (size_t k = 0; k < be->params.size(); k++) {
                    const Param& p = be->params[k];
                    if (p.sigil != '$' || p.slurpy || p.named) unsupported("block parameter form");
                    line(0, "Value " + mangleVar(p.name) + " = (__a.size() > " + std::to_string(k) +
                            " ? __a[" + std::to_string(k) + "] : Value::any());");
                }
            }
            for (size_t i = 0; i < be->body.size(); i++) {
                Stmt* s = be->body[i].get();
                if (i + 1 == be->body.size() && s->kind == NK::ExprStmt)
                    line(0, "return " + exArg(static_cast<ExprStmt*>(s)->e.get()) + ";");
                else stmt(s, 0);
            }
            line(0, "return Value::any();");
        });
        if (pushed) topics.pop_back();
        return "Value::closure([=](ValueList& __a)->Value{\n" + body + "})";
    }

    // ---- expressions: return a C++ expression string of type Value ----
    std::string ex(Expr* e) {
        switch (e->kind) {
            case NK::IntLit: {
                auto* n = static_cast<IntLit*>(e);
                if (!n->big.empty()) unsupported("big integer literal");
                return "Value::integer(" + std::to_string(n->v) + "LL)";
            }
            case NK::NumLit: {
                auto* n = static_cast<NumLit*>(e);
                if (n->imaginary) unsupported("imaginary literal");
                if (n->isRat) { std::ostringstream s; s << "Value::rat(BigInt(" << n->ratNum << "LL), BigInt(" << n->ratDen << "LL))"; return s.str(); }
                std::ostringstream s; s.precision(17); s << "Value::number(" << n->v << ")";
                return s.str();
            }
            case NK::StrLit:  return "Value::str(" + cesc(static_cast<StrLit*>(e)->v) + ")";
            case NK::BoolLit: return std::string("Value::boolean(") + (static_cast<BoolLit*>(e)->v ? "true" : "false") + ")";
            case NK::VarExpr: {
                auto* v = static_cast<VarExpr*>(e);
                if (v->name == "$_") {
                    if (topics.empty()) unsupported("$_ outside a topicalizing construct");
                    return topics.back();
                }
                if (v->name == "@*ARGS") return "RT.getArgs()";
                if (v->name.size() > 2 && (v->name[0] == '$' || v->name[0] == '@' || v->name[0] == '%')
                    && (v->name[1] == '!' || v->name[1] == '.')) {
                    if (self_.empty()) unsupported("attribute access outside a method");
                    return "rtAttrGet(" + self_ + ", " + cesc(v->name.substr(2)) + ")"; // $!x / @.y / %!z
                }
                if (v->name.size() && v->name[0] == '&') { // &sub : a reference to a user sub
                    std::string nm = v->name.substr(1);
                    auto it = userSubs.find(nm);
                    if (it == userSubs.end()) unsupported("reference to non-local routine '" + v->name + "'");
                    return "Value::closure([](ValueList& __a)->Value{ return " + mangleSub(nm) + "(__a); })";
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
                if (v->name.size() > 1 && (v->name[1] == '*' || v->name[1] == '?' || v->name[1] == '!'))
                    unsupported("special/dynamic variable '" + v->name + "'");
                return mangleVar(v->name); // scalars, @arrays and %hashes are all C++ Value locals
            }
            case NK::SelfTerm:
                if (self_.empty()) unsupported("`self` outside a method");
                return self_;
            case NK::Whatever:
                if (inWC) { int k = wcArity++; return "(__w.size()>" + std::to_string(k) + "?__w[" + std::to_string(k) + "]:Value::any())"; }
                return "Value::whatever()";
            case NK::BlockExpr: return emitBlockClosure(static_cast<BlockExpr*>(e));
            case NK::Range: {
                auto* r = static_cast<RangeExpr*>(e);
                return "Value::range((" + ex(r->from.get()) + ").toInt(), (" + ex(r->to.get()) + ").toInt(), "
                     + (r->exFrom ? "true" : "false") + ", " + (r->exTo ? "true" : "false") + ")";
            }
            case NK::RegexLit:
                if (topics.empty()) unsupported("regex match outside a topic ($_)");
                return "RT.regexMatch((" + topics.back() + ").toStr(), " + cesc(static_cast<RegexLit*>(e)->pattern) + ")";
            case NK::Index: {
                auto* ix = static_cast<Index*>(e);
                if (!ix->adverb.empty()) unsupported("index adverb (:exists/:delete/…)");
                std::string fn = hasWhatever(ix->index.get()) ? "RT.idxW" : "rtIndexGet"; // @a[*-1] / @a[*]
                return fn + "(" + ex(ix->base.get()) + ", " + ex(ix->index.get()) + ", "
                     + (ix->isHash ? "true" : "false") + ")";
            }
            case NK::NameTerm: {
                const std::string& n = static_cast<NameTerm*>(e)->name;
                if (n == "True")  return "Value::boolean(true)";
                if (n == "False") return "Value::boolean(false)";
                if (n == "Nil")   return "Value::nil()";
                if (n == "pi" || n == "\xcf\x80")  return "Value::number(3.14159265358979323846)";
                if (n == "e")     return "Value::number(2.71828182845904523536)";
                if (n == "tau")   return "Value::number(6.28318530717958647692)";
                if (n == "rand")  return "Value::number(randDouble())";
                if (enumKeys.count(n)) return mangleVar(n); // enum value (bound as a global)
                // Any other bareword is a type object (Int, Str, Supply, a user class, …) —
                // matching the interpreter, which resolves an unknown bareword to typeObj(n).
                return "Value::typeObj(" + cesc(n) + ")";
            }
            case NK::Ternary: {
                auto* t = static_cast<Ternary*>(e);
                return "(RT.boolify(" + ex(t->cond.get()) + ") ? (" + ex(t->then.get()) + ") : (" + ex(t->els.get()) + "))";
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
                    return "([&]()->Value{ try {\n" + body + "} catch (const RakuError&) { return Value::nil(); } }())";
                }
                if (u->op == "gather") { // gather { … take … } — `take` routes to the runtime collector
                    std::string body;
                    if (u->operand->kind == NK::BlockExpr) {
                        auto* be = static_cast<BlockExpr*>(u->operand.get());
                        body = capture([&]() { for (auto& s : be->body) stmt(s.get(), 0); });
                    } else body = exArg(u->operand.get()) + ";\n";
                    return "([&]()->Value{ auto __c = std::make_shared<ValueList>(); RT.tctx_.gatherStack.push_back(__c);\n"
                           "try {\n" + body + "} catch (...) { RT.tctx_.gatherStack.pop_back(); throw; }\n"
                           "RT.tctx_.gatherStack.pop_back(); return Value::array(*__c); }())";
                }
                if (u->postfix) { // $x++ / $x-- as an expression: yield the old value
                    if (u->op != "++" && u->op != "--") unsupported("postfix " + u->op);
                    std::string delta = u->op == "++" ? "1" : "-1";
                    return "([&]()->Value{ Value& _r=" + lvalueExpr(u->operand.get()) +
                           "; Value _o=_r; _r=applyArith(\"+\", _o, Value::integer(" + delta + ")); return _o; }())";
                }
                std::string x = ex(u->operand.get());
                if (u->op == "!" || u->op == "not") return "Value::boolean(!RT.boolify(" + x + "))";
                if (u->op == "?")  return "Value::boolean(RT.boolify(" + x + "))";
                if (u->op == "-")  return "applyArith(\"-\", Value::integer(0), " + x + ")";
                if (u->op == "+")  return "applyArith(\"+\", Value::integer(0), " + x + ")";
                if (u->op == "~")  return "Value::str((" + x + ").toStr())";
                if (u->op == "^")  return "Value::range(0, (" + x + ").toInt(), false, true)"; // ^N = 0..^N
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
                std::string args = argList(c->args);
                if (c->callee) return "RT.callCallable(" + ex(c->callee.get()) + ", {" + args + "})";
                if (multiNames.count(c->name)) return mangleSub(c->name) + "(ValueList{" + args + "})"; // multi dispatcher
                if (userSubs.count(c->name)) {
                    // -O: call the direct-Value overload when the arity/args line up
                    auto fit = fastSubs.find(c->name);
                    if (fit != fastSubs.end() && (int)c->args.size() == fit->second && simpleArgs(c->args))
                        return mangleSub(c->name) + "(" + args + ")";
                    return mangleSub(c->name) + "(ValueList{" + args + "})"; // boxed adapter
                }
                return "RT.callBuiltin(" + cesc(c->name) + ", {" + args + "})";
            }
            case NK::MethodCall: {
                auto* m = static_cast<MethodCall*>(e);
                if (m->mutate || m->hyper || m->meta || m->maybe)
                    unsupported("method-call form (.= / >>. / .^ / .?)");
                return "RT.methodCall(" + ex(m->inv.get()) + ", " + cesc(m->method) + ", {" + argList(m->args) + "})";
            }
            case NK::Assign: {
                auto* a = static_cast<Assign*>(e);
                if (a->target->kind == NK::VarExpr && static_cast<VarExpr*>(a->target.get())->declare)
                    unsupported("declaration used as a sub-expression");
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
            case NK::ArrayLit: return "listToArray({" + argList(static_cast<ArrayLit*>(e)->items) + "})";
            default: unsupported(nkName(e->kind));
        }
    }

    // ---- statements ----
    std::vector<Block*> topLevelEnds; // top-level END phasers, run at program end

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

    void stmt(Stmt* s, int ind) {
        switch (s->kind) {
            case NK::EmptyStmt: case NK::UseStmt: case NK::SubDecl: case NK::EnumDecl:
            case NK::ClassDecl: return; // subs/enums/classes emitted separately
            case NK::ExprStmt: {
                Expr* e = static_cast<ExprStmt*>(s)->e.get();
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
                        line(ind, "Value " + tmp + " = Value::array((" + exArg(a->value.get()) + ").flatten());");
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
                    char sigil = nm.empty() ? '$' : nm[0];
                    std::string def = sigil == '@' ? "Value::array()" : sigil == '%' ? "Value::makeHash()" : "Value::any()";
                    line(ind, "Value " + mangleVar(nm) + " = " + def + ";");
                    return;
                }
                line(ind, ex(e) + ";");
                return;
            }
            case NK::VarDecl: {
                auto* d = static_cast<VarDecl*>(s);
                if (d->names.size() != 1) { // my ($a, $b) = LIST
                    if (!d->init) unsupported("multi-variable declaration without initializer");
                    std::string tmp = gensym("__d");
                    line(ind, "Value " + tmp + " = Value::array((" + exArg(d->init.get()) + ").flatten());");
                    for (size_t k = 0; k < d->names.size(); k++)
                        line(ind, "Value " + mangleVar(d->names[k]) + " = rtIndexGet(" + tmp +
                                  ", Value::integer(" + std::to_string(k) + "), false);");
                    return;
                }
                char sigil = d->names[0].empty() ? '$' : d->names[0][0];
                std::string init;
                if (d->init) init = sigil == '@' ? "Value::array((" + exArg(d->init.get()) + ").flatten())" : exArg(d->init.get());
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
            case NK::LastStmt: line(ind, "break;"); return;
            case NK::NextStmt: line(ind, "continue;"); return;
            case NK::Block: {
                auto* b = static_cast<Block*>(s);
                if (b->isCatch || !b->phaser.empty()) unsupported("phaser / CATCH block");
                line(ind, "{"); block(b, ind + 1); line(ind, "}");
                return;
            }
            case NK::IfStmt:   ifStmt(static_cast<IfStmt*>(s), ind); return;
            case NK::WhileStmt: {
                auto* w = static_cast<WhileStmt*>(s);
                if (!w->var.empty()) unsupported("while EXPR -> $x");
                std::string c = "RT.boolify(" + ex(w->cond.get()) + ")";
                line(ind, "while (" + (w->isUntil ? "!" + c : c) + ") {");
                block(w->body.get(), ind + 1); line(ind, "}");
                return;
            }
            case NK::RepeatStmt: {
                auto* r = static_cast<RepeatStmt*>(s);
                std::string c = "RT.boolify(" + ex(r->cond.get()) + ")";
                line(ind, "do {"); block(r->body.get(), ind + 1);
                line(ind, "} while (" + (r->isUntil ? "!" + c : c) + ");");
                return;
            }
            case NK::LoopStmt: {
                auto* l = static_cast<LoopStmt*>(s);
                line(ind, "{");
                if (l->init) line(ind + 1, ex(l->init.get()) + ";");
                std::string c = l->cond ? "RT.boolify(" + ex(l->cond.get()) + ")" : "true";
                line(ind + 1, "for (; " + c + "; " + (l->incr ? ex(l->incr.get()) : std::string()) + ") {");
                block(l->body.get(), ind + 2);
                line(ind + 1, "}");
                line(ind, "}");
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
            if (v->name == "$_" || v->name == "@*ARGS" || (v->name.size() && v->name[0] == '&') ||
                (v->name.size() > 1 && (v->name[1] == '*' || v->name[1] == '?')))
                unsupported("assignment to '" + v->name + "'");
            return mangleVar(v->name);
        }
        if (e->kind == NK::Index) {
            auto* ix = static_cast<Index*>(e);
            if (!ix->adverb.empty()) unsupported("index adverb on assignment");
            if (ix->base->kind != NK::VarExpr) unsupported("assignment to nested index");
            return "rtIndexRef(" + lvalueExpr(ix->base.get()) + ", " + ex(ix->index.get()) + ", "
                 + (ix->isHash ? "true" : "false") + ")";
        }
        unsupported("assignment to this target");
    }

    // Assigning a list to an @-array materializes a fresh (bracket-gisting) Array.
    std::string coerceFor(Expr* tgt, const std::string& rhs) {
        if (tgt->kind == NK::VarExpr) {
            const std::string& n = static_cast<VarExpr*>(tgt)->name;
            if (!n.empty() && n[0] == '@') return "Value::array((" + rhs + ").flatten())";
            if (!n.empty() && n[0] == '%') return "rtCoerceHash(" + rhs + ")"; // my %h = a=>1,…
        }
        return rhs;
    }

    std::string assign(Assign* a) {
        Expr* tgt = a->target.get();
        if (tgt->kind == NK::VarExpr && static_cast<VarExpr*>(tgt)->declare)   // `my $x = ..`
            return "Value " + mangleVar(static_cast<VarExpr*>(tgt)->name) + " = " + coerceFor(tgt, exArg(a->value.get()));
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
            std::string c = "RT.boolify(" + ex(f->branches[i].first.get()) + ")";
            if (f->isUnless) c = "!" + c;
            line(ind, (i == 0 ? "if (" : "else if (") + c + ") {");
            block(f->branches[i].second.get(), ind + 1);
            line(ind, "}");
        }
        if (f->elseBlock) { line(ind, "else {"); block(f->elseBlock.get(), ind + 1); line(ind, "}"); }
    }

    void forStmt(ForStmt* f, int ind) {
        if (f->destructure) unsupported("for with destructuring");
        if (f->vars.size() > 1) { // for @a -> $x, $y { … } : take vars.size() elements per iteration
            size_t n = f->vars.size();
            std::string lst = gensym("__lst"), i = gensym("__fi");
            line(ind, "{");
            line(ind + 1, "Value " + lst + " = Value::array((" + ex(f->list.get()) + ").flatten());");
            line(ind + 1, "for (size_t " + i + " = 0; " + i + " < " + lst + ".arr->size(); " + i + " += " + std::to_string(n) + ") {");
            for (size_t k = 0; k < n; k++)
                line(ind + 2, "Value " + mangleVar(f->vars[k]) + " = (" + i + "+" + std::to_string(k) + " < " + lst +
                              ".arr->size() ? (*" + lst + ".arr)[" + i + "+" + std::to_string(k) + "] : Value::any());");
            block(f->body.get(), ind + 2);
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
            block(f->body.get(), ind + 2);
            topics.pop_back();
            line(ind + 1, "}");
        } else {
            std::string lst = gensym("__lst"), el = gensym("__e");
            line(ind + 1, "Value " + lst + " = " + ex(f->list.get()) + ";");
            line(ind + 1, "for (auto& " + el + " : " + lst + ".flatten()) {");
            line(ind + 2, "Value " + topic + " = " + el + ";");
            topics.push_back(topic);
            block(f->body.get(), ind + 2);
            topics.pop_back();
            line(ind + 1, "}");
        }
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
            {"**", "rtPow"},
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
            pi++;
        }
    }

    // ---- classes ----
    std::string methodFn(const std::string& cls, const std::string& meth) {
        std::string o = "m_";
        for (char c : cls)  o += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
        o += "_";
        for (char c : meth) o += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
        return o;
    }

    void classMethodDefs(ClassDecl* cd) {
        if (cd->isGrammar || cd->isPackage) unsupported("a grammar/package declaration");
        for (auto& mp : cd->methods) {
            SubDecl* md = mp.get();
            if (md->isMulti) unsupported("a multi method");
            line(0, "static Value " + methodFn(cd->name, md->name) + "(ValueList& __a) {");
            line(1, "Value __self = __a.size() > 0 ? __a[0] : Value::any();");
            bindParams(md->params, 1, true);
            std::string saved = self_; self_ = "__self";
            for (size_t i = 0; i < md->body.size(); i++) {
                Stmt* s = md->body[i].get();
                if (i + 1 == md->body.size() && s->kind == NK::ExprStmt)
                    line(1, "return " + exArg(static_cast<ExprStmt*>(s)->e.get()) + ";");
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
                          + std::string(1, a.sigil) + "'; __at.pub = " + (a.pub ? "true" : "false") + ";";
            if (a.def) d += " __at.hasDefVal = true; __at.defVal = " + ex(a.def.get()) + ";";
            d += " " + ci + "->attrs.push_back(__at); }";
            line(1, d);
        }
        for (auto& mp : cd->methods)
            line(1, "  " + ci + "->methods[" + cesc(mp->name) + "] = Value::closure(" + methodFn(cd->name, mp->name) + ");");
        line(1, "  RT.classes_[" + cesc(cd->name) + "] = " + ci + "; }");
    }

    // Emit the statements of a sub body (last ExprStmt becomes the return value).
    void emitBody(const std::vector<StmtPtr>& body) {
        for (size_t i = 0; i < body.size(); i++) {
            Stmt* s = body[i].get();
            if (i + 1 == body.size() && s->kind == NK::ExprStmt)
                line(1, "return " + exArg(static_cast<ExprStmt*>(s)->e.get()) + ";");
            else stmt(s, 1);
        }
        line(1, "return Value::any();");
    }
    // Emit a sub/candidate body given its C++ function name.
    void bodyDef(const std::string& fnName, const std::vector<Param>& ps, const std::vector<StmtPtr>& body, bool fast = false) {
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
        for (SubDecl* c : cands) {
            std::string guard = "__a.size() == " + std::to_string(c->params.size());
            for (size_t k = 0; k < c->params.size(); k++) {
                const Param& p = c->params[k];
                if (p.litVal)
                    guard += " && applyArith(\"eqv\", __a[" + std::to_string(k) + "], " + ex(p.litVal.get()) + ").truthy()";
                else if (!p.type.empty())
                    guard += " && rtTypeMatch(__a[" + std::to_string(k) + "], " + cesc(p.type) + ")";
            }
            line(1, "if (" + guard + ") return " + mangleSub(name) + "__" + std::to_string(idx[c]) + "(__a);");
        }
        line(1, "throw RakuError{Value::nil(), \"No matching multi candidate for " + name + "\"};");
        line(0, "}");
    }
};

} // namespace

std::string transpileToCpp(Program& prog, bool optimize) {
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
            if (cd->isRole || cd->isGrammar || cd->isPackage) throw CodegenError{"a role/grammar/package"};
            g.classNames.insert(cd->name);
            classes.push_back(cd);
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
             "#include \"Interpreter.h\"\n#include \"Value.h\"\n#include <vector>\n#include <string>\n#include <iostream>\n"
             "using namespace rakupp;\n"
             "static Interpreter RT;   // provides builtins, method dispatch, coercions\n\n";

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
             "    __rakupp_register();\n"
             "    try {\n";
    std::string body = g.capture([&]() {
        g.emitSeq(prog.stmts, 2, /*topLevel=*/true);
        for (auto it = g.topLevelEnds.rbegin(); it != g.topLevelEnds.rend(); ++it) g.emitPhaserBody(*it, 2);
    });
    g.out << body;
    g.out << "    } catch (const RakuError& e) { std::cerr << e.message << \"\\n\"; return 1; }\n"
             "    catch (const std::exception& e) { std::cerr << \"Internal error: \" << e.what() << \"\\n\"; return 3; }\n"
             "    return 0;\n}\n";
    return g.out.str();
}

}
