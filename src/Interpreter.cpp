#include "Interpreter.h"
#include <unistd.h>
#include "Regex.h"
#include "Lexer.h"
#include "Parser.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <dirent.h>

namespace rakupp {

// SHA-1 (uppercase hex) — used to resolve module names against a Rakudo CURI `short/` index.
static std::string sha1hex(const std::string& msg) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    std::string m = msg;
    uint64_t ml = (uint64_t)m.size() * 8;
    m += (char)0x80;
    while (m.size() % 64 != 56) m += (char)0;
    for (int i = 7; i >= 0; i--) m += (char)((ml >> (i * 8)) & 0xff);
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((unsigned char)m[off + i * 4] << 24) | ((unsigned char)m[off + i * 4 + 1] << 16) |
                   ((unsigned char)m[off + i * 4 + 2] << 8) | ((unsigned char)m[off + i * 4 + 3]);
        for (int i = 16; i < 80; i++) { uint32_t v = w[i-3]^w[i-8]^w[i-14]^w[i-16]; w[i] = (v << 1) | (v >> 31); }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    char buf[41];
    snprintf(buf, sizeof buf, "%08X%08X%08X%08X%08X", h0, h1, h2, h3, h4);
    return buf;
}

Value applyArith(const std::string& op, const Value& l, const Value& r);

static bool isDefined(const Value& v) { return v.t != VT::Nil && v.t != VT::Any && v.t != VT::Type; }

// Variables that may be used without an explicit `my` declaration (never "undeclared").
static bool isSpecialVar(const std::string& n) {
    if (n.size() < 2) return true;          // bare sigil
    char sig = n[0];
    if (sig == '&') return true;            // &foo code refs (builtins not in env)
    char c = n[1];
    if (c == '*' || c == '?' || c == '.' || c == '!' || c == '<' ||
        c == '=' || c == '~' || c == ':' || c == '^' || c == '/' || c == '_')
        return true;                        // twigils, $_, $/, $!, attribute/placeholder
    if (std::isdigit((unsigned char)c)) return true; // $0, $1, ... match vars
    if (n == "$a" || n == "$b") return true;          // implicit block/sort params
    if (n == "@_" || n == "%_") return true;
    return false;
}

static bool valueEqv(const Value& a, const Value& b) {
    if (a.t != b.t) {
        if (a.isNumeric() && b.isNumeric()) return a.toNum() == b.toNum();
        return false;
    }
    switch (a.t) {
        case VT::Array:
            if (!a.arr || !b.arr || a.arr->size() != b.arr->size()) return false;
            for (size_t i = 0; i < a.arr->size(); i++) if (!valueEqv((*a.arr)[i], (*b.arr)[i])) return false;
            return true;
        case VT::Hash:
            if (!a.hash || !b.hash || a.hash->size() != b.hash->size()) return false;
            for (auto& kv : *a.hash) { auto it = b.hash->find(kv.first); if (it == b.hash->end() || !valueEqv(kv.second, it->second)) return false; }
            return true;
        case VT::Pair:
            return a.s == b.s && valueEqv(a.pairVal ? *a.pairVal : Value::any(), b.pairVal ? *b.pairVal : Value::any());
        case VT::Object: return a.obj == b.obj;
        default: return a.toStr() == b.toStr();
    }
}

// Numify a string with Raku-correct result type (Int vs Num); undefined if non-numeric.
static Value numifyStr(const std::string& in) {
    size_t a = in.find_first_not_of(" \t\n\r\f\v");
    if (a == std::string::npos) return Value::integer(0); // empty/whitespace -> 0
    size_t b = in.find_last_not_of(" \t\n\r\f\v");
    std::string s = in.substr(a, b - a + 1);
    // strip underscores that sit between two digits (numeric separators)
    if (s.find('_') != std::string::npos) {
        std::string t;
        for (size_t k = 0; k < s.size(); k++) {
            if (s[k] == '_' && k > 0 && k + 1 < s.size() &&
                std::isalnum((unsigned char)s[k-1]) && std::isalnum((unsigned char)s[k+1])) continue;
            t += s[k];
        }
        s = t;
    }
    if (s == "Inf" || s == "+Inf") return Value::number(INFINITY);
    if (s == "-Inf") return Value::number(-INFINITY);
    if (s == "NaN") return Value::number(NAN);
    static const std::regex reInt(R"(^[+-]?\d+$)");
    static const std::regex reRadix(R"(^[+-]?0[xob][0-9a-fA-F_]+$)");
    static const std::regex reFloat(R"(^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$)");
    static const std::regex reRat(R"(^[+-]?\d+/\d+$)");
    try {
        if (std::regex_match(s, reInt)) {
            try { return Value::integer(std::stoll(s)); }
            catch (...) { return Value::bigint(BigInt::fromString(s)); }
        }
        if (std::regex_match(s, reRadix)) {
            bool neg = s[0] == '-'; size_t off = (s[0] == '+' || s[0] == '-') ? 1 : 0;
            int base = s[off + 1] == 'x' ? 16 : s[off + 1] == 'o' ? 8 : 2;
            std::string digits; for (size_t k = off + 2; k < s.size(); k++) if (s[k] != '_') digits += s[k];
            long long v = std::strtoll(digits.c_str(), nullptr, base);
            return Value::integer(neg ? -v : v);
        }
        if (std::regex_match(s, reRat)) {
            size_t sl = s.find('/');
            double num = std::stod(s.substr(0, sl)), den = std::stod(s.substr(sl + 1));
            return Value::number(den == 0 ? 0 : num / den);
        }
        if (std::regex_match(s, reFloat)) return Value::number(std::stod(s));
    } catch (...) {}
    return Value::any(); // not numeric -> undefined (so .defined is False)
}

static Value defaultFor(char sigil) {
    if (sigil == '@') return Value::array();
    if (sigil == '%') return Value::makeHash();
    return Value::any();
}

// ---- placeholder ($^a) collection ----
static void collectPHExpr(const Expr* e, std::set<std::string>& out);
static void collectPHStmt(const Stmt* s, std::set<std::string>& out);

static void addIfPlaceholder(const std::string& name, std::set<std::string>& out) {
    if (name.size() > 2 && (name[1] == '^')) out.insert(name);
}

static void collectPHExpr(const Expr* e, std::set<std::string>& out) {
    if (!e) return;
    switch (e->kind) {
        case NK::VarExpr: addIfPlaceholder(static_cast<const VarExpr*>(e)->name, out); break;
        case NK::Binary: collectPHExpr(static_cast<const Binary*>(e)->lhs.get(), out);
                         collectPHExpr(static_cast<const Binary*>(e)->rhs.get(), out); break;
        case NK::Unary: collectPHExpr(static_cast<const Unary*>(e)->operand.get(), out); break;
        case NK::Assign: collectPHExpr(static_cast<const Assign*>(e)->target.get(), out);
                         collectPHExpr(static_cast<const Assign*>(e)->value.get(), out); break;
        case NK::Call: { auto* c = static_cast<const Call*>(e); collectPHExpr(c->callee.get(), out);
                         for (auto& a : c->args) collectPHExpr(a.get(), out); break; }
        case NK::MethodCall: { auto* m = static_cast<const MethodCall*>(e); collectPHExpr(m->inv.get(), out);
                         for (auto& a : m->args) collectPHExpr(a.get(), out); break; }
        case NK::Index: collectPHExpr(static_cast<const Index*>(e)->base.get(), out);
                        collectPHExpr(static_cast<const Index*>(e)->index.get(), out); break;
        case NK::Ternary: { auto* t = static_cast<const Ternary*>(e); collectPHExpr(t->cond.get(), out);
                         collectPHExpr(t->then.get(), out); collectPHExpr(t->els.get(), out); break; }
        case NK::Range: collectPHExpr(static_cast<const RangeExpr*>(e)->from.get(), out);
                        collectPHExpr(static_cast<const RangeExpr*>(e)->to.get(), out); break;
        case NK::Pair: {
            auto* p = static_cast<const PairExpr*>(e);
            if (p->keyExpr) collectPHExpr(p->keyExpr.get(), out);
            collectPHExpr(p->value.get(), out); break;
        }
        case NK::ListExpr: for (auto& it : static_cast<const ListExpr*>(e)->items) collectPHExpr(it.get(), out); break;
        case NK::ArrayLit: for (auto& it : static_cast<const ArrayLit*>(e)->items) collectPHExpr(it.get(), out); break;
        case NK::HashLit: for (auto& it : static_cast<const HashLit*>(e)->items) collectPHExpr(it.get(), out); break;
        case NK::InterpStr: for (auto& it : static_cast<const InterpStr*>(e)->parts) collectPHExpr(it.get(), out); break;
        default: break; // do NOT descend into nested BlockExpr (own scope)
    }
}

static void collectPHStmt(const Stmt* s, std::set<std::string>& out) {
    if (!s) return;
    switch (s->kind) {
        case NK::ExprStmt: collectPHExpr(static_cast<const ExprStmt*>(s)->e.get(), out); break;
        case NK::ReturnStmt: collectPHExpr(static_cast<const ReturnStmt*>(s)->value.get(), out); break;
        case NK::Block: for (auto& st : static_cast<const Block*>(s)->stmts) collectPHStmt(st.get(), out); break;
        case NK::IfStmt: { auto* i = static_cast<const IfStmt*>(s);
            for (auto& br : i->branches) { collectPHExpr(br.first.get(), out); collectPHStmt(br.second.get(), out); }
            if (i->elseBlock) collectPHStmt(i->elseBlock.get(), out); break; }
        case NK::WhileStmt: collectPHExpr(static_cast<const WhileStmt*>(s)->cond.get(), out);
                            collectPHStmt(static_cast<const WhileStmt*>(s)->body.get(), out); break;
        case NK::ForStmt: collectPHExpr(static_cast<const ForStmt*>(s)->list.get(), out);
                          collectPHStmt(static_cast<const ForStmt*>(s)->body.get(), out); break;
        default: break;
    }
}

static std::vector<std::string> computePlaceholders(const std::vector<StmtPtr>& body) {
    std::set<std::string> ph;
    for (auto& s : body) collectPHStmt(s.get(), ph);
    return std::vector<std::string>(ph.begin(), ph.end()); // std::set is sorted
}

Value listToArray(const ValueList& items) {
    Value v = Value::array();
    for (auto& it : items) {
        if (it.t == VT::Range) {
            ValueList sub = it.flatten();
            v.arr->insert(v.arr->end(), sub.begin(), sub.end());
        } else {
            v.arr->push_back(it);
        }
    }
    return v;
}

static Value coerceArray(const Value& v) {
    if (v.t == VT::Array) { if (!v.isList) return v; Value r = v; r.isList = false; return r; } // @-container is an Array
    if (v.t == VT::Range) return Value::array(v.flatten());
    Value a = Value::array();
    if (v.t != VT::Nil && v.t != VT::Any) a.arr->push_back(v);
    return a;
}

static Value coerceHash(const Value& v) {
    if (v.t == VT::Hash) { // already a hash: copy entries (value semantics for my %h = %other)
        Value h = Value::makeHash(); h.hashKind = v.hashKind;
        if (v.hash) *h.hash = *v.hash;
        return h;
    }
    Value h = Value::makeHash();
    ValueList items;
    if (v.t == VT::Array) items = *v.arr;
    else if (v.t == VT::Pair) items.push_back(v);
    else if (v.t != VT::Nil && v.t != VT::Any) items.push_back(v);
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].t == VT::Pair) {
            (*h.hash)[items[i].s] = items[i].pairVal ? *items[i].pairVal : Value::any();
        } else if (i + 1 < items.size()) {
            (*h.hash)[items[i].toStr()] = items[i + 1];
            i++;
        }
    }
    return h;
}

Interpreter::Interpreter() {
    global_ = std::make_shared<Env>();
    cur_ = global_;
    // Module search paths. "lib"/"."/"rakulib" are relative to the CWD; the rest
    // come from the environment so a checkout works anywhere:
    //   RAKULIB  colon-separated extra module dirs
    //   ROAST    a Roast checkout, adds its Test-Helpers lib (for the test suite)
    if (const char* rl = std::getenv("RAKULIB")) {
        std::string s = rl, cur;
        for (char c : s) {
            if (c == ':') { if (!cur.empty()) libPaths_.push_back(cur); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) libPaths_.push_back(cur);
    }
    if (const char* ro = std::getenv("ROAST"))
        libPaths_.push_back(std::string(ro) + "/packages/Test-Helpers/lib");
    registerBuiltins();
}

void Interpreter::hoistSubs(const std::vector<StmtPtr>& stmts) {
    // Named subs are visible across their whole enclosing scope regardless of
    // textual position, so register them before executing the statements.
    for (auto& s : stmts) {
        if (s->kind == NK::SubDecl) {
            auto* sd = static_cast<SubDecl*>(s.get());
            if (!sd->isMethod && !sd->name.empty()) exec(s.get());
        }
    }
}

int Interpreter::run(Program& prog) {
    int code = 0;
    bool crashed = false;
    {
        Value args = Value::array();
        for (auto& s : argv_) args.arr->push_back(Value::str(s));
        cur_->define("@*ARGS", args);
    }
    // Partition top-level phasers (BEGIN/CHECK/INIT run before mainline; END after).
    std::vector<Block*> beginP, checkP, initP, endP;
    std::vector<Stmt*> mainline;
    Block* topCatch = nullptr; // a CATCH in the mainline (the UNIT block) guards it
    for (auto& s : prog.stmts) {
        if (s->kind == NK::Block) {
            auto* b = static_cast<Block*>(s.get());
            if (b->isCatch)          { topCatch = b;        continue; }
            if (b->phaser == "BEGIN") { beginP.push_back(b); continue; }
            if (b->phaser == "CHECK") { checkP.push_back(b); continue; }
            if (b->phaser == "INIT")  { initP.push_back(b);  continue; }
            if (b->phaser == "END")   { endP.push_back(b);   continue; }
        }
        mainline.push_back(s.get());
    }
    auto runPhaser = [&](Block* b) { auto sc = std::make_shared<Env>(); sc->parent = cur_; execBlock(b, sc); };
    // END phasers run in REVERSE source order, on any exit path.
    auto runEnds = [&]() { for (auto it = endP.rbegin(); it != endP.rend(); ++it) { try { runPhaser(*it); } catch (...) {} } };
    // Extract a single top-level lexical declaration (name + whether it has an initializer).
    auto topDecl = [](Stmt* s, bool& hasInit) -> std::string {
        hasInit = true;
        if (s->kind == NK::VarDecl) { auto* vd = static_cast<VarDecl*>(s); hasInit = (bool)vd->init; return vd->names.size() == 1 ? vd->names[0] : ""; }
        if (s->kind == NK::ExprStmt) {
            Expr* e = static_cast<ExprStmt*>(s)->e.get();
            if (e && e->kind == NK::VarExpr && static_cast<VarExpr*>(e)->declare) { hasInit = false; return static_cast<VarExpr*>(e)->name; }
            if (e && e->kind == NK::Assign) { auto* a = static_cast<Assign*>(e);
                if (a->target && a->target->kind == NK::VarExpr && static_cast<VarExpr*>(a->target.get())->declare)
                    return static_cast<VarExpr*>(a->target.get())->name; }
        }
        return "";
    };
    try {
        hoistSubs(prog.stmts);
        // Pre-declare top-level lexicals so compile-time phasers (BEGIN/CHECK) can see them.
        bool hasInit;
        for (auto* s : mainline) { std::string nm = topDecl(s, hasInit);
            if (!nm.empty() && !global_->vars.count(nm)) global_->define(nm, defaultFor(nm.empty() ? '$' : nm[0])); }
        for (auto* b : beginP) runPhaser(b);                                      // BEGIN: source order
        for (auto it = checkP.rbegin(); it != checkP.rend(); ++it) runPhaser(*it); // CHECK: reverse
        for (auto* b : initP) runPhaser(b);                                       // INIT: source order
        for (auto* s : mainline) {
            if (s->kind == NK::SubDecl && !static_cast<SubDecl*>(s)->name.empty() &&
                !static_cast<SubDecl*>(s)->isMethod) continue; // hoisted
            // a bare `my $x;` (no init) must not clobber a value a phaser already set
            std::string nm = topDecl(s, hasInit);
            if (!nm.empty() && !hasInit && global_->vars.count(nm)) continue;
            exec(s);
        }
        // auto-invoke MAIN with command-line arguments, if defined
        if (Value* mainSub = cur_->find("&MAIN")) {
            ValueList margs;
            for (auto& a : argv_) {
                if (a.rfind("--", 0) == 0 && a.size() > 2) {
                    std::string rest = a.substr(2);
                    if (rest.rfind("/", 0) == 0) margs.push_back(Value::pair(rest.substr(1), Value::boolean(false)));
                    else {
                        auto eq = rest.find('=');
                        if (eq != std::string::npos) margs.push_back(Value::pair(rest.substr(0, eq), Value::str(rest.substr(eq + 1))));
                        else margs.push_back(Value::pair(rest, Value::boolean(true)));
                    }
                } else {
                    margs.push_back(Value::str(a));
                }
            }
            // Decide up front whether any MAIN candidate matches the argv. Checking
            // BEFORE the call means a nested X::Multi::NoMatch thrown from inside a
            // matched MAIN body propagates as a real error instead of being mistaken
            // for "no MAIN candidate" and silently printing Usage.
            bool mainMatches = true;
            if (mainSub->code && mainSub->code->isMultiDispatcher) {
                mainMatches = false;
                for (auto& cand : mainSub->code->candidates)
                    if (scoreCandidate(cand, margs) >= 0) { mainMatches = true; break; }
            }
            if (!mainMatches) {
                std::cerr << "Usage:\n";
                for (auto& cand : mainSub->code->candidates) {
                    if (!cand.code || !cand.code->params) continue;
                    std::string line = "  rakupp <program>";
                    for (auto& p : *cand.code->params) {
                        if (p.litVal) line += " " + eval(p.litVal.get()).toStr();
                        else if (p.named) { std::string nm = p.name.size() > 1 ? p.name.substr(1) : p.name; line += " [--" + nm + (p.type == "Bool" || p.type.empty() ? "" : "=<value>") + "]"; }
                        else line += " <" + (p.name.size() > 1 ? p.name.substr(1) : p.name) + ">";
                    }
                    std::cerr << line << "\n";
                }
                code = 2;
            } else {
                callCallable(*mainSub, margs);
            }
        }
    } catch (ExitEx& e) {
        code = e.code;
    } catch (RakuError& e) {
        if (topCatch) { // mainline CATCH: bind $_/$! to the exception and run its when/default
            cur_->define("$_", e.payload);
            cur_->define("$!", e.payload);
            try { for (auto& s : topCatch->stmts) exec(s.get()); }
            catch (BreakGivenEx&) {} catch (ExitEx& ex) { code = ex.code; } catch (...) {}
        } else {
            std::cerr << e.message << "\n";
            code = 1;
            crashed = true;
        }
    } catch (ReturnEx&) {
    } catch (LastEx&) {
    } catch (NextEx&) {
    } catch (RedoEx&) {
    }
    runEnds(); // END phasers (reverse source order), after the mainline
    // Emit a trailing plan only on normal completion — never fabricate `1..0`
    // after an uncaught error (that would look like a passing empty/skip-all plan).
    if (usedTest_ && planned_ < 0 && !crashed) {
        std::cout << "1.." << testNum_ << "\n";
    }
    if (failCount_ > 0 && code == 0) code = 1;
    return code;
}

// Discover installed Rakudo CompUnit::Repository::Installation prefixes (site/vendor + ~/.raku).
static const std::vector<std::string>& rakuRepoPrefixes() {
    static std::vector<std::string> cached;
    static bool init = false;
    if (init) return cached;
    init = true;
    std::vector<std::string>& repos = cached;
    if (const char* home = getenv("HOME")) repos.push_back(std::string(home) + "/.raku");
    // /usr/local/Cellar/rakudo/<ver>/share/perl6/{site,vendor}
    static const char* cellars[] = {"/usr/local/Cellar/rakudo", "/opt/homebrew/Cellar/rakudo"};
    for (auto* cellar : cellars) {
        if (DIR* d = opendir(cellar)) {
            while (struct dirent* e = readdir(d)) {
                std::string v = e->d_name;
                if (v == "." || v == "..") continue;
                repos.push_back(std::string(cellar) + "/" + v + "/share/perl6/site");
                repos.push_back(std::string(cellar) + "/" + v + "/share/perl6/vendor");
            }
            closedir(d);
        }
    }
    return repos;
}

void Interpreter::loadModule(const std::string& name) {
    if (loadedModules_.count(name)) return;
    loadedModules_.insert(name);

    auto loadSource = [&](const std::string& src) {
        auto prog = std::make_shared<Program>();
        std::string finish;
        try {
            Lexer lx(src);
            Parser parser(lx.tokenize());
            *prog = parser.parseProgram();
            finish = lx.finishData();
        } catch (ParseError& e) {
            // Non-fatal: the importing program continues without this module.
            // Grammar slangs (Slang::*) are compile-time grammar mutators rakupp
            // cannot apply anyway, so ignore them silently.
            if (name.rfind("Slang::", 0) != 0)
                std::cerr << "===WARNING=== Module " << name << " parse error at line " << e.line << ": " << e.what() << " (use ignored)\n";
            return;
        }
        keptPrograms_.push_back(prog);
        auto saved = cur_;
        std::string savedFinish = finishData_;
        finishData_ = finish; // this module's $=finish data block
        cur_ = global_; // module definitions become globally visible (no real export yet)
        // A runtime failure in a module's load-time code (often a deep dependency
        // using an unimplemented primitive, e.g. Lock) is non-fatal: warn and keep
        // going so the importing program can still run paths that don't need it.
        try {
            hoistSubs(prog->stmts);
            for (auto& st : prog->stmts) {
                if (st->kind == NK::SubDecl && !static_cast<SubDecl*>(st.get())->name.empty() &&
                    !static_cast<SubDecl*>(st.get())->isMethod) continue; // hoisted
                exec(st.get());
            }
        }
        catch (RakuError& e) {
            cur_ = saved; finishData_ = savedFinish;
            std::cerr << "===WARNING=== Module " << name << " failed during load: " << e.message << "\n";
            return;
        }
        catch (...) { cur_ = saved; finishData_ = savedFinish; throw; }
        cur_ = saved; finishData_ = savedFinish;
    };

    std::string rel = name;
    for (size_t p = rel.find("::"); p != std::string::npos; p = rel.find("::")) rel.replace(p, 2, "/");
    // 1. local lib paths (project lib, ., rakupp rakulib)
    static const char* exts[] = {".rakumod", ".pm6", ".raku", ".pm"};
    for (auto& base : libPaths_) {
        for (auto ext : exts) {
            std::ifstream in(base + "/" + rel + ext);
            if (!in) continue;
            std::ostringstream ss; ss << in.rdbuf();
            loadSource(ss.str());
            return;
        }
    }
    // 2. installed Rakudo/zef modules: resolve name via the CURI short/ index
    std::string nameSha = sha1hex(name);
    for (auto& repo : rakuRepoPrefixes()) {
        std::string shortDir = repo + "/short/" + nameSha;
        DIR* dd = opendir(shortDir.c_str());
        if (!dd) continue;
        std::string entry;
        while (struct dirent* e = readdir(dd)) { std::string n = e->d_name; if (n != "." && n != "..") { entry = n; break; } }
        closedir(dd);
        if (entry.empty()) continue;
        std::ifstream meta(shortDir + "/" + entry);
        std::vector<std::string> lines; std::string ln;
        while (std::getline(meta, ln)) lines.push_back(ln);
        if (lines.size() < 4 || lines[3].empty()) continue; // line 4 = source SHA
        std::ifstream src(repo + "/sources/" + lines[3]);
        if (!src) continue;
        std::ostringstream ss; ss << src.rdbuf();
        loadSource(ss.str());
        return;
    }
    // module file not found. Pragmas / version literals are expected to have no file;
    // anything else is a genuinely unresolved dependency — warn (but keep going).
    static const std::set<std::string> pragmas = {
        "strict", "fatal", "lib", "isms", "nqp", "soft", "worries", "experimental",
        "variables", "attributes", "cur", "Slang", "MONKEY-SEE-NO-EVAL", "MONKEY-TYPING",
        "MONKEY", "Test", "v6", "v6.c", "v6.d", "v6.e",
    };
    bool versionLit = name.size() >= 2 && name[0] == 'v' && std::isdigit((unsigned char)name[1]);
    if (!pragmas.count(name) && !versionLit)
        std::cerr << "===WARNING=== Could not find module '" << name << "' (use ignored)\n";
}

Value Interpreter::evalString(const std::string& src) {
    Lexer lexer(src);
    auto prog = std::make_shared<Program>();
    try {
        Parser parser(lexer.tokenize());
        *prog = parser.parseProgram();
    } catch (ParseError& e) {
        throw RakuError{Value::str(e.what()), std::string("EVAL parse error: ") + e.what()};
    }
    keptPrograms_.push_back(prog); // keep AST alive for closures defined within
    Value last = Value::any();
    for (auto& s : prog->stmts) last = exec(s.get());
    return last;
}

void Interpreter::emitTest(bool ok, const std::string& desc, const std::string& directive) {
    if (subtestDepth_ > 0) {
        if (!ok && directive.empty()) subtestFailed_ = true;
        std::cout << "    " << (ok ? "ok" : "not ok");
        if (!desc.empty()) std::cout << " - " << desc;
        std::cout << "\n";
        return;
    }
    testNum_++;
    std::ostringstream os;
    os << (ok ? "ok " : "not ok ") << testNum_;
    if (!desc.empty()) os << " - " << desc;
    if (!directive.empty()) os << " # " << directive;
    std::cout << os.str() << "\n";
    if (!ok && directive.empty()) failCount_++;
}

// ----------------- statements -----------------
// A block-scoped phaser we run at entry/exit rather than in-place.
static bool isBlockPhaser(Stmt* s) {
    if (s->kind != NK::Block) return false;
    const std::string& p = static_cast<Block*>(s)->phaser;
    return p == "ENTER" || p == "LEAVE" || p == "KEEP" || p == "UNDO" || p == "FIRST";
}
void Interpreter::runEnterPhasers(const std::vector<StmtPtr>& stmts) {
    for (auto& s : stmts) if (s->kind == NK::Block) { auto* b = static_cast<Block*>(s.get());
        if (b->phaser == "ENTER" || b->phaser == "FIRST") { auto sc = std::make_shared<Env>(); sc->parent = cur_; execBlock(b, sc); } }
}
void Interpreter::runLeavePhasers(const std::vector<StmtPtr>& stmts) {
    // reverse source order
    std::vector<Block*> leaves;
    for (auto& s : stmts) if (s->kind == NK::Block) { auto* b = static_cast<Block*>(s.get());
        if (b->phaser == "LEAVE" || b->phaser == "KEEP" || b->phaser == "UNDO") leaves.push_back(b); }
    for (auto it = leaves.rbegin(); it != leaves.rend(); ++it) { auto sc = std::make_shared<Env>(); sc->parent = cur_; try { execBlock(*it, sc); } catch (...) {} }
}

Value Interpreter::execBlock(Block* b, std::shared_ptr<Env> scope) {
    auto saved = cur_;
    cur_ = std::move(scope);
    Value last = Value::any();
    // a CATCH {} anywhere in the block handles exceptions from the whole block
    Block* catchBlk = nullptr;
    for (auto& s : b->stmts)
        if (s->kind == NK::Block && static_cast<Block*>(s.get())->isCatch) catchBlk = static_cast<Block*>(s.get());
    hoistSubs(b->stmts);
    runEnterPhasers(b->stmts);
    try {
        for (auto& s : b->stmts) {
            if (s->kind == NK::Block && static_cast<Block*>(s.get())->isCatch) continue;
            if (isBlockPhaser(s.get())) continue; // ENTER/LEAVE handled at entry/exit
            if (s->kind == NK::SubDecl && !static_cast<SubDecl*>(s.get())->name.empty() &&
                !static_cast<SubDecl*>(s.get())->isMethod) continue; // hoisted
            last = exec(s.get());
        }
    } catch (RakuError& e) {
        if (catchBlk) {
            cur_->define("$_", e.payload);
            cur_->define("$!", e.payload);
            try {
                for (auto& s : catchBlk->stmts) exec(s.get());
            } catch (BreakGivenEx&) { /* when/default matched */ }
            runLeavePhasers(b->stmts);
            cur_ = saved;
            return Value::nil();
        }
        runLeavePhasers(b->stmts);
        cur_ = saved;
        throw;
    } catch (...) {
        runLeavePhasers(b->stmts);
        cur_ = saved;
        throw;
    }
    runLeavePhasers(b->stmts);
    cur_ = saved;
    return last;
}

bool Interpreter::runLoopBody(Block* body, std::shared_ptr<Env> scope) {
    for (;;) {
        try { execBlock(body, scope); return true; }
        catch (RedoEx&) { continue; }
        catch (NextEx&) { return true; }
        catch (BreakGivenEx&) { return true; }
        catch (LastEx&) { return false; }
    }
}

Value Interpreter::exec(Stmt* s) {
    switch (s->kind) {
        case NK::ExprStmt: return eval(static_cast<ExprStmt*>(s)->e.get());
        case NK::EmptyStmt: return Value::any();
        case NK::UseStmt: {
            auto* u = static_cast<UseStmt*>(s);
            if (u->module == "Test") usedTest_ = true;
            else if (u->module == "lib") {
                std::string path = u->arg;
                if (path.empty() && u->argExpr) path = eval(u->argExpr.get()).toStr();
                if (!path.empty()) libPaths_.insert(libPaths_.begin(), path);
            }
            else if (!u->module.empty()) loadModule(u->module);
            return Value::any();
        }
        case NK::Block: {
            auto* b = static_cast<Block*>(s);
            auto scope = std::make_shared<Env>();
            scope->parent = cur_;
            return execBlock(b, scope);
        }
        case NK::SubDecl: {
            auto* sd = static_cast<SubDecl*>(s);
            Value code; code.t = VT::Code;
            code.code = std::make_shared<Callable>();
            code.code->name = sd->name;
            code.code->params = &sd->params;
            code.code->body = &sd->body;
            code.code->closure = cur_;
            if (sd->params.empty()) code.code->placeholders = computePlaceholders(sd->body);
            if (!sd->name.empty()) {
                if (sd->isMulti) {
                    std::string key = "&" + sd->name;
                    Value* existing = cur_->find(key);
                    if (existing && existing->t == VT::Code && existing->code && existing->code->isMultiDispatcher) {
                        existing->code->candidates.push_back(code);
                        return *existing;
                    }
                    Value disp; disp.t = VT::Code; disp.code = std::make_shared<Callable>();
                    disp.code->name = sd->name;
                    disp.code->isMultiDispatcher = true;
                    disp.code->candidates.push_back(code);
                    cur_->define(key, disp);
                    return disp;
                }
                cur_->define("&" + sd->name, code);
            }
            return code;
        }
        case NK::EnumDecl: {
            auto* ed = static_cast<EnumDecl*>(s);
            ValueList items = ed->values ? eval(ed->values.get()).flatten() : ValueList{};
            long long counter = 0;
            Value pairs = Value::array();
            for (auto& it : items) {
                std::string key; Value val;
                if (it.t == VT::Pair) { key = it.s; val = it.pairVal ? *it.pairVal : Value::integer(counter); counter = val.toInt() + 1; }
                else { key = it.toStr(); val = Value::integer(counter++); }
                Value ev = Value::enumVal(key, val.toInt());
                cur_->define(key, ev);
                if (!ed->name.empty()) cur_->define(ed->name + "::" + key, ev);
                pairs.arr->push_back(Value::pair(key, val));
            }
            if (!ed->name.empty()) cur_->define(ed->name, pairs);
            return Value::any();
        }
        case NK::ClassDecl: {
            auto* cd = static_cast<ClassDecl*>(s);
            if (cd->isPackage) {
                // file-scoped `unit module Foo;` (empty body): just register the name;
                // the rest of the file runs in the enclosing scope.
                if (cd->body.empty()) {
                    if (!cd->name.empty()) cur_->define(cd->name, Value::typeObj(cd->name));
                    return Value::any();
                }
                // braced `module Foo { ... }`: run body in a child scope, then publish
                // its symbols globally under qualified names ($Foo::bar, &Foo::sub).
                if (!cd->name.empty()) cur_->define(cd->name, Value::typeObj(cd->name));
                std::string savedPrefix = pkgPrefix_;
                pkgPrefix_ += cd->name + "::";
                auto pkgEnv = std::make_shared<Env>();
                pkgEnv->parent = cur_;
                auto saved = cur_; cur_ = pkgEnv;
                for (auto& st : cd->body) exec(st.get());
                cur_ = saved;
                for (auto& kv : pkgEnv->vars) {
                    const std::string& sym = kv.first;
                    std::string qual;
                    if (!sym.empty() && (sym[0]=='$'||sym[0]=='@'||sym[0]=='%'||sym[0]=='&'))
                        qual = std::string(1, sym[0]) + pkgPrefix_ + sym.substr(1);
                    else qual = pkgPrefix_ + sym;
                    global_->define(qual, kv.second);
                }
                pkgPrefix_ = savedPrefix;
                return Value::any();
            }
            auto ci = std::make_shared<ClassInfo>();
            ci->name = cd->name;
            if (!cd->parent.empty()) {
                auto it = classes_.find(cd->parent);
                if (it != classes_.end()) ci->parent = it->second;
            }
            ci->isGrammar = cd->isGrammar;
            for (auto& r : cd->rules) { ci->rules[r.name] = r.pattern; ci->ruleKind[r.name] = r.kind; }
            for (auto& a : cd->attrs) {
                ClassAttr ca; ca.name = a.name; ca.sigil = a.sigil; ca.pub = a.pub;
                ca.def = a.def.get();
                ci->attrs.push_back(ca);
            }
            for (auto& md : cd->methods) {
                Value code; code.t = VT::Code;
                code.code = std::make_shared<Callable>();
                code.code->name = md->name;
                code.code->params = &md->params;
                code.code->body = &md->body;
                code.code->closure = cur_;
                code.code->isMethod = true; // invoked via .() binds the 1st arg as self
                if (md->params.empty()) code.code->placeholders = computePlaceholders(md->body);
                if (md->isMulti) {
                    auto it = ci->methods.find(md->name);
                    if (it != ci->methods.end() && it->second.code && it->second.code->isMultiDispatcher) {
                        it->second.code->candidates.push_back(code);
                    } else {
                        Value disp; disp.t = VT::Code; disp.code = std::make_shared<Callable>();
                        disp.code->name = md->name; disp.code->isMultiDispatcher = true;
                        disp.code->candidates.push_back(code);
                        ci->methods[md->name] = disp;
                    }
                } else {
                    ci->methods[md->name] = code;
                }
            }
            classes_[cd->name] = ci;
            cur_->define(cd->name, Value::typeObj(cd->name));
            // register nested classes/enums (and static subs) declared in the body
            for (auto& st : cd->body) exec(st.get());
            return Value::any();
        }
        case NK::ReturnStmt: {
            auto* r = static_cast<ReturnStmt*>(s);
            Value v = r->value ? eval(r->value.get()) : Value::any();
            throw ReturnEx{v};
        }
        case NK::LastStmt: throw LastEx{};
        case NK::NextStmt: throw NextEx{};
        case NK::RedoStmt: throw RedoEx{};
        case NK::IfStmt: {
            auto* is = static_cast<IfStmt*>(s);
            for (size_t bi = 0; bi < is->branches.size(); bi++) {
                auto& br = is->branches[bi];
                Value cv = eval(br.first.get());
                bool c = boolify(cv);
                if (is->isUnless) c = !c;
                if (c) {
                    auto scope = std::make_shared<Env>(); scope->parent = cur_;
                    if (bi == 0 && !is->thenVar.empty()) scope->define(is->thenVar, cv); // if EXPR -> $x
                    return execBlock(br.second.get(), scope);
                }
                if (is->isUnless) break; // unless has single branch
            }
            if (is->elseBlock) {
                auto scope = std::make_shared<Env>(); scope->parent = cur_;
                return execBlock(is->elseBlock.get(), scope);
            }
            return Value::any();
        }
        case NK::WhileStmt: {
            auto* ws = static_cast<WhileStmt*>(s);
            for (;;) {
                Value cv = eval(ws->cond.get());
                bool c = boolify(cv);
                if (ws->isUntil) c = !c;
                if (!c) break;
                auto scope = std::make_shared<Env>(); scope->parent = cur_;
                if (!ws->var.empty()) scope->define(ws->var, cv); // while EXPR -> $x { }
                if (!runLoopBody(ws->body.get(), scope)) break;
            }
            return Value::any();
        }
        case NK::ForStmt: {
            auto* fs = static_cast<ForStmt*>(s);
            Value listv = eval(fs->list.get());
            // Fast paths for the common single-topic loop: avoid materializing the
            // whole sequence up front (a Range of N ints or a copy of an N-elem array).
            if (!fs->destructure && fs->vars.size() <= 1) {
                const std::string var = fs->vars.empty() ? "$_" : fs->vars[0];
                // Reuse one Env across iterations for speed. This is only safe when
                // nothing captured the previous iteration's scope (a closure would
                // hold a reference, bumping use_count); in that case we allocate a
                // fresh Env so each closure keeps its own binding — exact semantics.
                std::shared_ptr<Env> scope;
                auto freshScope = [&]() {
                    if (!scope || scope.use_count() > 1) {
                        scope = std::make_shared<Env>();
                        scope->parent = cur_;
                    } else {
                        scope->vars.clear(); // reuse buckets, drop last iteration's bindings
                    }
                };
                if (listv.t == VT::Range) {
                    long long lo = listv.rFrom + (listv.rExFrom ? 1 : 0);
                    long long hi = listv.rTo - (listv.rExTo ? 1 : 0);
                    for (long long k = lo; k <= hi; k++) {
                        freshScope();
                        scope->define(var, Value::integer(k));
                        if (!runLoopBody(fs->body.get(), scope)) break;
                    }
                    return Value::any();
                }
                if (listv.t == VT::Array && listv.arr) {
                    auto arr = listv.arr; // share, don't copy the elements
                    for (size_t i = 0; i < arr->size(); i++) {
                        freshScope();
                        scope->define(var, (*arr)[i]);
                        if (!runLoopBody(fs->body.get(), scope)) break;
                    }
                    return Value::any();
                }
            }
            ValueList items;
            if (listv.t == VT::Array && listv.arr) items = *listv.arr; // one-level
            else if (listv.t == VT::Range) items = listv.flatten();
            else items.push_back(listv);
            // `-> ($a,$b,$c)`: each element is unpacked into the vars (one item/iteration).
            if (fs->destructure && !fs->vars.empty()) {
                for (size_t i = 0; i < items.size(); i++) {
                    auto scope = std::make_shared<Env>(); scope->parent = cur_;
                    ValueList row = items[i].t == VT::Array ? *items[i].arr : items[i].flatten();
                    for (size_t k = 0; k < fs->vars.size(); k++)
                        scope->define(fs->vars[k], k < row.size() ? row[k] : Value::any());
                    if (!runLoopBody(fs->body.get(), scope)) break;
                }
                return Value::any();
            }
            size_t nvars = fs->vars.empty() ? 1 : fs->vars.size();
            for (size_t i = 0; i < items.size(); i += nvars) {
                auto scope = std::make_shared<Env>(); scope->parent = cur_;
                if (fs->vars.empty()) {
                    scope->define("$_", items[i]);
                } else {
                    for (size_t k = 0; k < fs->vars.size(); k++) {
                        scope->define(fs->vars[k], (i + k < items.size()) ? items[i + k] : Value::any());
                    }
                }
                if (!runLoopBody(fs->body.get(), scope)) break;
            }
            return Value::any();
        }
        case NK::GivenStmt: {
            auto* g = static_cast<GivenStmt*>(s);
            Value topic = eval(g->topic.get());
            // with/without definedness guard
            bool skip = (g->defGuard == 1 && !isDefined(topic)) || (g->defGuard == 2 && isDefined(topic));
            auto scope = std::make_shared<Env>(); scope->parent = cur_;
            scope->define("$_", topic);
            if (skip) { if (g->hasElse) { try { execBlock(g->elseBody.get(), scope); } catch (BreakGivenEx&) {} } return Value::any(); }
            try { execBlock(g->body.get(), scope); }
            catch (BreakGivenEx&) {}
            return Value::any();
        }
        case NK::WhenStmt: {
            auto* w = static_cast<WhenStmt*>(s);
            bool match = w->isDefault;
            if (!w->isDefault) {
                Value* tp = cur_->find("$_");
                Value topic = tp ? *tp : Value::any();
                Value cv = eval(w->cond.get());
                match = applyArith("~~", topic, cv).truthy();
            }
            if (match) {
                auto scope = std::make_shared<Env>(); scope->parent = cur_;
                execBlock(w->body.get(), scope);
                throw BreakGivenEx{};
            }
            return Value::any();
        }
        case NK::LoopStmt: {
            auto* ls = static_cast<LoopStmt*>(s);
            auto outer = std::make_shared<Env>(); outer->parent = cur_;
            auto saved = cur_; cur_ = outer;
            try {
                if (ls->init) eval(ls->init.get());
                for (;;) {
                    if (ls->cond && !boolify(eval(ls->cond.get()))) break;
                    auto scope = std::make_shared<Env>(); scope->parent = cur_;
                    if (!runLoopBody(ls->body.get(), scope)) break;
                    if (ls->incr) eval(ls->incr.get());
                }
            } catch (...) { cur_ = saved; throw; }
            cur_ = saved;
            return Value::any();
        }
        case NK::RepeatStmt: {
            auto* r = static_cast<RepeatStmt*>(s);
            for (;;) {
                auto scope = std::make_shared<Env>(); scope->parent = cur_;
                if (!runLoopBody(r->body.get(), scope)) break;
                bool c = r->cond ? boolify(eval(r->cond.get())) : false;
                if (r->isUntil) c = !c;
                if (!c) break;
            }
            return Value::any();
        }
        default:
            // expression-like fallthrough
            return Value::any();
    }
}

// ----------------- expressions -----------------
Value Interpreter::makeClosure(BlockExpr* be) {
    Value code; code.t = VT::Code;
    code.code = std::make_shared<Callable>();
    code.code->params = &be->params;
    code.code->body = &be->body;
    code.code->closure = cur_;
    if (be->params.empty()) code.code->placeholders = computePlaceholders(be->body);
    return code;
}

void Interpreter::bindParams(const std::vector<Param>& params, ValueList& args,
                             std::shared_ptr<Env>& env) {
    // split named vs positional
    ValueList positional;
    std::map<std::string, Value> named;
    for (auto& a : args) {
        if (a.t == VT::Pair) named[a.s] = a.pairVal ? *a.pairVal : Value::any();
        else positional.push_back(a);
    }
    // names captured by explicit named params are NOT collected by a slurpy *%hash
    std::set<std::string> explicitNamed;
    for (auto& p : params)
        if (p.named && !p.slurpy)
            explicitNamed.insert(p.name.size() > 1 ? p.name.substr(1) : p.name);
    size_t pi = 0;
    for (auto& p : params) {
        std::string bareName = p.name.size() > 1 ? p.name.substr(1) : p.name;
        if (p.slurpy) {
            if (p.sigil == '%') {
                Value h = Value::makeHash();
                for (auto& kv : named) if (!explicitNamed.count(kv.first)) (*h.hash)[kv.first] = kv.second;
                env->define(p.name, h);
            } else {
                Value a = Value::array();
                size_t remaining = positional.size() - pi;
                // single-argument rule: a lone Iterable arg flattens into the slurpy;
                // multiple args are kept as-is (so f(@a,@b) is two elements).
                if (remaining == 1 && !positional[pi].itemized && (positional[pi].t == VT::Array || positional[pi].t == VT::Range)) {
                    for (auto& x : positional[pi].flatten()) a.arr->push_back(x);
                    pi++;
                } else {
                    for (; pi < positional.size(); pi++) a.arr->push_back(positional[pi]);
                }
                env->define(p.name, a);
            }
            continue;
        }
        if (p.named) {
            auto it = named.find(bareName);
            if (it != named.end()) env->define(p.name, it->second);
            else if (p.defaultVal) env->define(p.name, eval(p.defaultVal.get()));
            else env->define(p.name, defaultFor(p.sigil));
            continue;
        }
        if (pi < positional.size()) {
            Value v = positional[pi++];
            if (p.sigil == '@') v = coerceArray(v);
            else if (p.sigil == '%') v = coerceHash(v);
            env->define(p.name, v);
        } else if (p.defaultVal) {
            env->define(p.name, eval(p.defaultVal.get()));
        } else {
            env->define(p.name, defaultFor(p.sigil));
        }
    }
}

// Does an argument satisfy a parameter type-constraint name?
static bool typeMatchesArg(const Value& arg, const std::string& type) {
    if (type.empty() || type == "Any" || type == "Mu") return true;
    switch (arg.t) {
        case VT::Int:  return type == "Int" || type == "Cool" || type == "Numeric" || type == "Real" || type == "Rat";
        case VT::Num:  return type == "Num" || type == "Cool" || type == "Numeric" || type == "Real";
        case VT::Complex: return type == "Complex" || type == "Cool" || type == "Numeric";
        case VT::Rat:  return type == "Rat" || type == "Cool" || type == "Numeric" || type == "Real";
        case VT::Bool: return type == "Bool";
        case VT::Str:  return type == "Str" || type == "Cool" || type == "Stringy";
        case VT::Array: return type == "Array" || type == "List" || type == "Positional" || type == "Iterable" || (arg.isList && arg.s == "Seq" && type == "Seq");
        case VT::Hash:
            if (arg.hashKind == "FileHandle" && (type == "IO::Handle" || type == "IO" || type == "Handle")) return true;
            return type == "Hash" || type == "Map" || type == "Associative" || (arg.hashKind == type);
        case VT::Pair: return type == "Pair";
        case VT::Code: return type == "Code" || type == "Callable" || type == "Routine" || type == "Block" || type == "Sub";
        case VT::Regex: return type == "Regex";
        case VT::Match: return type == "Match";
        case VT::Range: return type == "Range" || type == "Iterable";
        case VT::Object:
            for (ClassInfo* ci = arg.obj ? arg.obj->cls.get() : nullptr; ci; ci = ci->parent.get())
                if (ci->name == type) return true;
            return false;
        default: return true; // Nil/Any/Type/unknown subset/enum: lenient
    }
}

int Interpreter::scoreCandidate(const Value& cand, const ValueList& args) {
    if (cand.t != VT::Code || !cand.code || !cand.code->params) return 0; // no signature: lowest specificity
    const auto& params = *cand.code->params;
    ValueList pos; for (auto& a : args) if (a.t != VT::Pair) pos.push_back(a);
    size_t required = 0, total = 0; bool slurpy = false;
    std::vector<const Param*> positional;
    for (auto& p : params) {
        if (p.named) continue;
        if (p.slurpy) { slurpy = true; continue; }
        positional.push_back(&p);
        total++;
        if (!p.optional && !p.defaultVal) required++;
    }
    if (pos.size() < required) return -1;
    if (!slurpy && pos.size() > total) return -1;
    int score = 0;
    for (size_t i = 0; i < positional.size() && i < pos.size(); i++) {
        const Param* p = positional[i];
        if (p->litVal) { // literal parameter: arg must equal the literal
            Value lv = eval(p->litVal.get());
            bool eq = (pos[i].isNumeric() && lv.isNumeric()) ? (pos[i].toNum() == lv.toNum())
                                                             : (pos[i].toStr() == lv.toStr());
            if (!eq) return -1;
            score += 3; // a literal match is very specific
            continue;
        }
        if (!typeMatchesArg(pos[i], p->type)) return -1;
        // type smiley: :D requires a defined arg, :U requires an undefined one
        if (p->defConstraint == 1 && !isDefined(pos[i])) return -1;
        if (p->defConstraint == 2 && isDefined(pos[i])) return -1;
        if (p->defConstraint) score++; // a smiley is more specific
        if (!p->type.empty() && p->type != "Any" && p->type != "Mu") score++;
        if (p->whereExpr) {
            auto env = std::make_shared<Env>(); env->parent = cur_;
            if (!p->name.empty()) env->define(p->name, pos[i]);
            env->define("$_", pos[i]);
            auto saved = cur_; cur_ = env;
            bool ok = false;
            try {
                Value cv = eval(p->whereExpr.get());
                // `where EXPR` is a smartmatch: a WhateverCode/Code constraint is called with the value
                if (cv.t == VT::Code && cv.code) cv = callCallable(cv, ValueList{pos[i]});
                ok = boolify(cv);
            } catch (...) { cur_ = saved; return -1; }
            cur_ = saved;
            if (!ok) return -1;
            score += 2; // a satisfied where-constraint is more specific
        }
    }
    return score;
}

struct DepthGuard {
    int& d;
    explicit DepthGuard(int& dd) : d(dd) {
        if (++d > 60000) { --d; throw RakuError{Value::str("X::Recursion"), "Too many levels of recursion"}; }
    }
    ~DepthGuard() { --d; }
};

bool Interpreter::boolify(const Value& v) {
    if (v.t == VT::Object && v.obj && v.obj->cls) {
        if (Value* b = v.obj->cls->findMethod("Bool"))
            return invokeMethod(*b, v, {}).truthy();
    }
    return v.truthy();
}

Value Interpreter::callBuiltin(const std::string& name, ValueList args) {
    auto it = builtins_.find(name);
    if (it == builtins_.end())
        throw RakuError{Value::nil(), "Undefined routine '" + name + "'"};
    return it->second(*this, args);
}

Value Interpreter::getArgs() {
    Value a = Value::array(); a.isList = true;
    for (auto& s : argv_) a.arr->push_back(Value::str(s));
    return a;
}

// Value-level indexing for native codegen (no AST). Read returns Nil when absent.
Value rtIndexGet(const Value& base, const Value& key, bool isHash) {
    if (isHash) {
        if (base.t == VT::Hash && base.hash) {
            auto it = base.hash->find(key.toStr());
            if (it != base.hash->end()) return it->second;
        }
        return Value::nil();
    }
    if (base.t == VT::Range) {
        ValueList f = base.flatten();
        long long i = key.toInt(); if (i < 0) i += (long long)f.size();
        if (i >= 0 && i < (long long)f.size()) return f[i];
        return Value::nil();
    }
    if ((base.t == VT::Array) && base.arr) {
        long long i = key.toInt(), n = (long long)base.arr->size();
        if (i < 0) i += n;
        if (i >= 0 && i < n) return (*base.arr)[i];
    }
    return Value::nil();
}

// Attribute access on `self` for native codegen ($!x / $.x inside a method).
Value rtAttrGet(const Value& self, const std::string& name) {
    if (self.t == VT::Object && self.obj) {
        auto it = self.obj->attrs.find(name);
        if (it != self.obj->attrs.end()) return it->second;
    }
    return Value::any();
}
Value& rtAttrRef(Value& self, const std::string& name) {
    if (self.t != VT::Object || !self.obj) { // shouldn't happen; keep it safe
        self = Value::object(std::make_shared<ObjectData>());
    }
    return self.obj->attrs[name];
}

// Nominal type check for native multi-dispatch.
bool rtTypeMatch(const Value& v, const std::string& type) {
    if (type.empty() || type == "Any" || type == "Mu" || type == "Cool") return true;
    switch (v.t) {
        case VT::Int:     return type == "Int" || type == "Numeric" || type == "Real";
        case VT::Num:     return type == "Num" || type == "Numeric" || type == "Real";
        case VT::Rat:     return type == "Rat" || type == "Numeric" || type == "Real";
        case VT::Complex: return type == "Complex" || type == "Numeric";
        case VT::Str:     return type == "Str" || type == "Stringy";
        case VT::Bool:    return type == "Bool";
        case VT::Array:   return type == "Array" || type == "List" || type == "Positional" || type == "Seq" || type == "Iterable";
        case VT::Hash:    return type == "Hash" || type == "Associative" || type == "Map";
        case VT::Code:    return type == "Code" || type == "Callable" || type == "Routine" || type == "Block";
        case VT::Object:
            for (ClassInfo* c = v.obj && v.obj->cls ? v.obj->cls.get() : nullptr; c; c = c->parent.get())
                if (c->name == type) return true;
            return false;
        default: return false;
    }
}

// Reduction metaop for native codegen: fold `op` over a flattened list.
Value rtReduce(const std::string& op, const Value& list) {
    ValueList items = list.flatten();
    if (items.empty()) {
        if (op == "+" || op == "-") return Value::integer(0);
        if (op == "*" || op == "/") return Value::integer(1);
        if (op == "~") return Value::str("");
        return Value::any();
    }
    Value acc = items[0];
    for (size_t k = 1; k < items.size(); k++) acc = applyArith(op, acc, items[k]);
    return acc;
}

// Writable element reference for native codegen (autovivifies base and slot).
Value& rtIndexRef(Value& base, const Value& key, bool isHash) {
    if (isHash) {
        if (base.t != VT::Hash || !base.hash) base = Value::makeHash();
        return (*base.hash)[key.toStr()];
    }
    if (base.t != VT::Array || !base.arr) base = Value::array();
    long long i = key.toInt(), n = (long long)base.arr->size();
    if (i < 0) i += n;
    if (i < 0) i = 0;
    if (i >= (long long)base.arr->size()) base.arr->resize(i + 1, Value::any());
    return (*base.arr)[i];
}

Value Interpreter::callCallable(const Value& codeVal, ValueList args) {
    if (codeVal.t != VT::Code || !codeVal.code)
        throw RakuError{Value::str("Not callable"), "Cannot invoke non-Callable value of type " + codeVal.typeName()};
    DepthGuard guard(callDepth_);
    Callable& c = *codeVal.code;
    if (c.isMultiDispatcher) {
        const Value* best = nullptr; int bestScore = -1;
        for (auto& cand : c.candidates) {
            int s = scoreCandidate(cand, args);
            if (s > bestScore) { bestScore = s; best = &cand; }
        }
        if (best && bestScore >= 0) return callCallable(*best, args);
        throw RakuError{Value::str("X::Multi::NoMatch"),
                        "Cannot resolve caller " + c.name + "(); no matching multi candidate"};
    }
    if (c.builtin) return c.builtin(*this, args);

    auto env = std::make_shared<Env>();
    env->parent = c.closure ? c.closure : global_;
    // `state` vars live in a per-callable persistent env spliced into the lookup chain
    if (!c.stateEnv) c.stateEnv = std::make_shared<Env>();
    c.stateEnv->parent = env->parent;
    env->parent = c.stateEnv;
    // a method invoked via .() takes its invocant as the first positional arg
    if (c.isMethod && !args.empty()) {
        env->define("self", args[0]);
        args.erase(args.begin());
    }
    if (c.params && !c.params->empty()) {
        bindParams(*c.params, args, env);
    } else if (!c.placeholders.empty()) {
        for (size_t k = 0; k < c.placeholders.size(); k++) {
            Value v = k < args.size() ? args[k] : Value::any();
            env->define(c.placeholders[k], v);
            // $^foo is also visible as $foo within the block
            const std::string& pn = c.placeholders[k];
            if (pn.size() > 2 && pn[1] == '^') env->define(std::string(1, pn[0]) + pn.substr(2), v);
        }
        env->define("@_", Value::array(args));
    } else {
        // implicit $_ / @_
        env->define("$_", args.empty() ? Value::any() : args[0]);
        env->define("@_", Value::array(args));
        if (!args.empty()) {
            env->define("$a", args[0]);
            if (args.size() > 1) env->define("$b", args[1]);
        }
    }
    auto saved = cur_;
    Env* savedState = curStateEnv_;
    cur_ = env;
    curStateEnv_ = c.stateEnv.get();
    Value last = Value::any();
    if (c.body) runEnterPhasers(*c.body);
    try {
        if (c.body) for (auto& s : *c.body) { if (isBlockPhaser(s.get())) continue; last = exec(s.get()); }
    } catch (ReturnEx& r) {
        if (c.body) runLeavePhasers(*c.body);
        cur_ = saved; curStateEnv_ = savedState;
        return r.v;
    } catch (...) {
        if (c.body) runLeavePhasers(*c.body);
        cur_ = saved; curStateEnv_ = savedState;
        throw;
    }
    if (c.body) runLeavePhasers(*c.body);
    cur_ = saved; curStateEnv_ = savedState;
    return last;
}

// Copy `is rw` parameter final values back to the caller's argument lvalues.
void Interpreter::copyOutRw(const std::vector<Param>* params, std::shared_ptr<Env>& env,
                            const std::vector<ExprPtr>* rwArgs, bool /*methodCtx*/) {
    if (!params || !rwArgs) return;
    bool any = false;
    for (auto& p : *params) if (p.isRw) any = true;
    if (!any) return;
    size_t pi = 0;
    for (auto& p : *params) {
        if (p.named) continue;
        if (p.slurpy) break;
        if (p.isRw && pi < rwArgs->size()) {
            auto it = env->vars.find(p.name);
            if (it != env->vars.end()) {
                try { if (Value* lv = lvalue((*rwArgs)[pi].get())) *lv = it->second; } catch (...) {}
            }
        }
        pi++;
    }
}

Value Interpreter::invokeMethod(const Value& codeVal, const Value& self, ValueList args, const std::vector<ExprPtr>* rwArgs) {
    if (codeVal.t != VT::Code || !codeVal.code) return Value::any();
    DepthGuard guard(callDepth_);
    Callable& c = *codeVal.code;
    if (c.isMultiDispatcher) {
        const Value* best = nullptr; int bestScore = -1;
        for (auto& cand : c.candidates) {
            int s = scoreCandidate(cand, args);
            if (s > bestScore) { bestScore = s; best = &cand; }
        }
        if (best && bestScore >= 0) return invokeMethod(*best, self, args, rwArgs);
        throw RakuError{Value::str("X::Multi::NoMatch"),
                        "No matching multi candidate for method " + c.name};
    }
    if (c.builtin) { // native-codegen method: receives self as the first argument
        ValueList a2; a2.reserve(args.size() + 1);
        a2.push_back(self);
        for (auto& x : args) a2.push_back(std::move(x));
        return c.builtin(*this, a2);
    }
    auto env = std::make_shared<Env>();
    env->parent = c.closure ? c.closure : global_;
    env->define("self", self);
    if (c.params && !c.params->empty()) bindParams(*c.params, args, env);
    else if (!c.placeholders.empty()) {
        for (size_t k = 0; k < c.placeholders.size(); k++) {
            Value v = k < args.size() ? args[k] : Value::any();
            env->define(c.placeholders[k], v);
            const std::string& pn = c.placeholders[k];
            if (pn.size() > 2 && pn[1] == '^') env->define(std::string(1, pn[0]) + pn.substr(2), v);
        }
        env->define("@_", Value::array(args));
    } else env->define("@_", Value::array(args));
    auto saved = cur_;
    cur_ = env;
    Value last = Value::any();
    try {
        if (c.body) for (auto& s : *c.body) last = exec(s.get());
    } catch (ReturnEx& r) { cur_ = saved; copyOutRw(c.params, env, rwArgs, true); return r.v; }
    catch (...) { cur_ = saved; throw; }
    cur_ = saved;
    copyOutRw(c.params, env, rwArgs, true);
    return last;
}

Value Interpreter::evalInterp(InterpStr* s) {
    std::string out;
    for (auto& p : s->parts) out += eval(p.get()).toStr();
    return Value::str(out);
}

Value* Interpreter::lvalue(Expr* e) {
    if (e->kind == NK::VarExpr) {
        auto* ve = static_cast<VarExpr*>(e);
        char sigil = ve->name.empty() ? '$' : ve->name[0];
        if (ve->declare) {
            if (ve->declScope == "state" && curStateEnv_) { // persistent across calls
                if (!curStateEnv_->vars.count(ve->name)) curStateEnv_->define(ve->name, defaultFor(sigil));
                return &curStateEnv_->vars[ve->name];
            }
            cur_->define(ve->name, defaultFor(sigil));
            return &cur_->vars[ve->name];
        }
        // attribute lvalue: $.x / $!x
        if (ve->name.size() > 2 && (ve->name[1] == '.' || ve->name[1] == '!')) {
            Value* selfp = cur_->find("self");
            if (selfp && selfp->t == VT::Object && selfp->obj)
                return &selfp->obj->attrs[ve->name.substr(2)];
        }
        Value* p = cur_->find(ve->name);
        if (p) return p;
        if (!isSpecialVar(ve->name))
            throw RakuError{Value::typeObj("X::Undeclared"),
                            "Variable '" + ve->name + "' is not declared"};
        cur_->define(ve->name, defaultFor(sigil));
        return &cur_->vars[ve->name];
    }
    if (e->kind == NK::Index) {
        auto* idx = static_cast<Index*>(e);
        Value* base = lvalue(idx->base.get());
        if (idx->isHash) {
            if (base->t != VT::Hash) *base = Value::makeHash();
            std::string key = eval(idx->index.get()).toStr();
            return &(*base->hash)[key];
        } else {
            if (base->t != VT::Array) *base = Value::array();
            long long i = eval(idx->index.get()).toInt();
            if (i < 0) i += (long long)base->arr->size();
            if (i < 0) i = 0;
            while ((long long)base->arr->size() <= i) base->arr->push_back(Value::any());
            return &(*base->arr)[i];
        }
    }
    // method-call lvalue: $obj.accessor = value  (rw accessors)
    if (e->kind == NK::MethodCall) {
        auto* mc = static_cast<MethodCall*>(e);
        Value* base = lvalue(mc->inv.get());
        if (base->t == VT::Hash && base->hashKind == "FileHandle") {
            if (!base->hash) base->hash = std::make_shared<std::map<std::string, Value>>();
            return &(*base->hash)[mc->method];
        }
        if (base->t == VT::Object && base->obj)
            return &base->obj->attrs[mc->method];
    }
    throw RakuError{Value::str("Cannot assign"), "Target is not assignable"};
}

Value applyArith(const std::string& op, const Value& l, const Value& r);

Value Interpreter::evalAssign(Assign* a) {
    if (a->op == "=" && a->target->kind == NK::ListExpr) {
        auto* lst = static_cast<ListExpr*>(a->target.get());
        Value rhs = eval(a->value.get());
        ValueList vals = rhs.flatten();
        for (size_t i = 0; i < lst->items.size(); i++) {
            Value* lv = lvalue(lst->items[i].get());
            *lv = (i < vals.size()) ? vals[i] : Value::any();
        }
        return rhs;
    }

    char sigil = '$';
    if (a->target->kind == NK::VarExpr) {
        auto* ve = static_cast<VarExpr*>(a->target.get());
        if (!ve->name.empty()) sigil = ve->name[0];
        // `state $x = INIT` initializes ONCE: if already set from a prior call, skip re-init
        if (a->op == "=" && ve->declare && ve->declScope == "state" && curStateEnv_ && curStateEnv_->vars.count(ve->name))
            return curStateEnv_->vars[ve->name];
    }

    if (a->op == "=" || a->op == ":=") {
        Value rhs = eval(a->value.get());
        Value* lv = lvalue(a->target.get());
        if (sigil == '@') *lv = coerceArray(rhs);
        else if (sigil == '%') *lv = coerceHash(rhs);
        else *lv = rhs;
        return *lv;
    }

    // compound assignment
    Value* lv = lvalue(a->target.get());
    Value rhs = eval(a->value.get());
    std::string binop = a->op.substr(0, a->op.size() - 1); // strip '='
    if (binop == "||") { if (!lv->truthy()) *lv = rhs; return *lv; }
    if (binop == "&&") { if (lv->truthy()) *lv = rhs; return *lv; }
    if (binop == "//") { if (!isDefined(*lv)) *lv = rhs; return *lv; }
    *lv = applyArith(binop, *lv, rhs);
    return *lv;
}

static bool isSetOpStr(const std::string& o) {
    static const std::set<std::string> ops = {
        "(|)", "∪", "(&)", "∩", "(-)", "∖", "(^)", "⊖", "(+)", "⊎", "(.)", "⊍",
        "(elem)", "∈", "(!elem)", "∉", "(cont)", "∋", "(!cont)", "∌",
        "(<=)", "⊆", "(<)", "⊂", "(>=)", "⊇", "(>)", "⊃", "(==)", "(!=)", "(<>)",
    };
    return ops.count(o) > 0;
}

static std::map<std::string, long long> setCounts(const Value& v) {
    std::map<std::string, long long> m;
    if (v.t == VT::Hash && v.hash) {
        bool isSet = v.hashKind.find("Set") == 0;
        bool baggy = v.hashKind.find("Bag") == 0 || v.hashKind.find("Mix") == 0;
        for (auto& kv : *v.hash) m[kv.first] = (baggy && !isSet) ? kv.second.toInt() : 1;
    } else if (v.t == VT::Array || v.t == VT::Range) {
        for (auto& x : v.flatten()) {
            if (x.t == VT::Pair) m[x.s] += x.pairVal ? x.pairVal->toInt() : 0;
            else m[x.toStr()] += 1;
        }
    } else if (v.t == VT::Pair) {
        m[v.s] = v.pairVal ? v.pairVal->toInt() : 0;
    } else if (v.t != VT::Nil && v.t != VT::Any && v.t != VT::Type) {
        m[v.toStr()] = 1;
    }
    return m;
}

static Value setOp(const std::string& op, const Value& l, const Value& r) {
    auto isBaggy = [](const Value& v) {
        return v.t == VT::Hash && (v.hashKind.find("Bag") == 0 || v.hashKind.find("Mix") == 0);
    };
    if (op == "(elem)" || op == "∈" || op == "(!elem)" || op == "∉") {
        auto b = setCounts(r); std::string k = l.toStr();
        bool in = b.count(k) && b[k] > 0;
        return Value::boolean((op == "(!elem)" || op == "∉") ? !in : in);
    }
    if (op == "(cont)" || op == "∋" || op == "(!cont)" || op == "∌") {
        auto a = setCounts(l); std::string k = r.toStr();
        bool in = a.count(k) && a[k] > 0;
        return Value::boolean((op == "(!cont)" || op == "∌") ? !in : in);
    }
    auto a = setCounts(l), b = setCounts(r);
    auto at = [](std::map<std::string, long long>& m, const std::string& k) { return m.count(k) ? m[k] : 0; };
    if (op == "(<=)" || op == "⊆" || op == "(<)" || op == "⊂" || op == "(>=)" || op == "⊇" ||
        op == "(>)" || op == "⊃" || op == "(==)" || op == "(!=)" || op == "(<>)") {
        bool aSubB = true, bSubA = true;
        for (auto& kv : a) if (kv.second > at(b, kv.first)) { aSubB = false; break; }
        for (auto& kv : b) if (kv.second > at(a, kv.first)) { bSubA = false; break; }
        bool eq = aSubB && bSubA;
        if (op == "(==)") return Value::boolean(eq);
        if (op == "(!=)" || op == "(<>)") return Value::boolean(!eq);
        if (op == "(<=)" || op == "⊆") return Value::boolean(aSubB);
        if (op == "(>=)" || op == "⊇") return Value::boolean(bSubA);
        if (op == "(<)" || op == "⊂") return Value::boolean(aSubB && !eq);
        return Value::boolean(bSubA && !eq); // (>) ⊃
    }
    bool wantBag = isBaggy(l) || isBaggy(r) || op == "(+)" || op == "⊎" || op == "(.)" || op == "⊍";
    std::map<std::string, long long> res;
    if (op == "(|)" || op == "∪") { res = a; for (auto& kv : b) res[kv.first] = std::max(at(res, kv.first), kv.second); }
    else if (op == "(&)" || op == "∩") { for (auto& kv : a) if (b.count(kv.first)) res[kv.first] = std::min(kv.second, b[kv.first]); }
    else if (op == "(-)" || op == "∖") { for (auto& kv : a) { long long d = kv.second - at(b, kv.first); if (d > 0) res[kv.first] = d; } }
    else if (op == "(^)" || op == "⊖") { for (auto& kv : a) if (!b.count(kv.first)) res[kv.first] = kv.second; for (auto& kv : b) if (!a.count(kv.first)) res[kv.first] = kv.second; }
    else if (op == "(+)" || op == "⊎") { res = a; for (auto& kv : b) res[kv.first] += kv.second; }
    else if (op == "(.)" || op == "⊍") { for (auto& kv : a) if (b.count(kv.first)) res[kv.first] = kv.second * b[kv.first]; }
    Value h = Value::makeHash(); h.hashKind = wantBag ? "Bag" : "Set";
    for (auto& kv : res) if (kv.second > 0) (*h.hash)[kv.first] = wantBag ? Value::integer(kv.second) : Value::boolean(true);
    return h;
}

static bool isJunction(const Value& v) {
    return v.t == VT::Array && (v.enumName == "any" || v.enumName == "all" || v.enumName == "one" || v.enumName == "none");
}

Value applyArith(const std::string& op, const Value& l, const Value& r) {
    if (op == "!%%") return Value::boolean(!applyArith("%%", l, r).truthy()); // negated divisibility
    if (isSetOpStr(op)) return setOp(op, l, r);
    // hyper binary metaop  >>OP>>  : element-wise apply OP over the two lists
    if (op.size() >= 5 && (op.substr(0, 2) == ">>" || op.substr(0, 2) == "<<") &&
        (op.substr(op.size() - 2) == ">>" || op.substr(op.size() - 2) == "<<")) {
        std::string inner = op.substr(2, op.size() - 4);
        ValueList a = l.flatten(), b = r.flatten();
        Value out = Value::array(); out.isList = true;
        size_t n = a.size() > b.size() ? a.size() : b.size();
        if (a.empty() || b.empty()) return out;
        for (size_t i = 0; i < n; i++) out.arr->push_back(applyArith(inner, a[i % a.size()], b[i % b.size()]));
        return out;
    }
    // junction constructors: 1|2 (any), 1&2 (all), 1^2 (one)
    if (op == "|" || op == "&" || op == "^") {
        std::string jt = op == "|" ? "any" : op == "&" ? "all" : "one";
        Value j = Value::array(); j.enumName = jt;
        auto add = [&](const Value& v) { if (v.t == VT::Array && v.enumName == jt) { for (auto& x : *v.arr) j.arr->push_back(x); } else j.arr->push_back(v); };
        add(l); add(r);
        return j;
    }
    // autothreading over a junction operand
    if (isJunction(l) || isJunction(r)) {
        const Value& j = isJunction(l) ? l : r;
        bool jleft = isJunction(l);
        static const std::set<std::string> cmp = {"==", "!=", "eq", "ne", "<", ">", "<=", ">=", "~~", "!~~", "<=>", "cmp", "lt", "gt", "le", "ge", "===", "eqv"};
        if (cmp.count(op)) {
            int t = 0, total = 0;
            for (auto& e : *j.arr) { total++; if (applyArith(op, jleft ? e : l, jleft ? r : e).truthy()) t++; }
            bool res = j.enumName == "any" ? t > 0 : j.enumName == "all" ? t == total : j.enumName == "one" ? t == 1 : t == 0;
            return Value::boolean(res);
        }
        Value out = Value::array(); out.enumName = j.enumName;
        for (auto& e : *j.arr) out.arr->push_back(applyArith(op, jleft ? e : l, jleft ? r : e));
        return out;
    }
    if (op == "..." || op == "...^") { // sequence operator (v1: integer step ±1)
        long long a = l.toInt(), b = r.toInt();
        Value out = Value::array(); out.isList = true;
        if (a <= b) { for (long long i = a; i <= b; i++) out.arr->push_back(Value::integer(i)); }
        else { for (long long i = a; i >= b; i--) out.arr->push_back(Value::integer(i)); }
        if (op == "...^" && !out.arr->empty()) out.arr->pop_back();
        return out;
    }
    if (op == "Z" || (op.size() > 1 && op[0] == 'Z')) { // zip; Z<op> applies op pairwise
        std::string sub = op.substr(1); // "" -> tuples, "=>" -> pairs, else infix op
        ValueList a = l.flatten(), b = r.flatten();
        Value out = Value::array(); out.isList = true;
        for (size_t i = 0; i < a.size() && i < b.size(); i++) {
            if (sub.empty()) out.arr->push_back(Value::array({a[i], b[i]}));
            else if (sub == "=>") out.arr->push_back(Value::pair(a[i].toStr(), b[i]));
            else out.arr->push_back(applyArith(sub, a[i], b[i]));
        }
        return out;
    }
    if (op == "X" || (op.size() > 1 && op[0] == 'X')) { // cross; X<op> applies op
        std::string sub = op.substr(1);
        ValueList a = l.flatten(), b = r.flatten();
        Value out = Value::array(); out.isList = true;
        for (auto& x : a) for (auto& y : b) {
            if (sub.empty()) out.arr->push_back(Value::array({x, y}));
            else if (sub == "=>") out.arr->push_back(Value::pair(x.toStr(), y));
            else out.arr->push_back(applyArith(sub, x, y));
        }
        return out;
    }
    // Whatever-currying: `* + 1`, `*.elems == 2`, `2 * *`, etc. yield a WhateverCode
    auto isWhateverish = [](const Value& v) {
        return v.t == VT::Whatever || (v.t == VT::Code && v.code && v.code->isWhateverCode);
    };
    if (isWhateverish(l) || isWhateverish(r)) {
        Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
        code.code->isWhateverCode = true;
        std::string opc = op; Value lc = l, rc = r;
        code.code->builtin = [opc, lc, rc](Interpreter& I, ValueList& a) -> Value {
            Value arg = a.empty() ? Value::any() : a[0];
            auto resolve = [&](const Value& v) -> Value {
                if (v.t == VT::Whatever) return arg;
                if (v.t == VT::Code && v.code && v.code->isWhateverCode) { ValueList one{arg}; return I.callCallable(v, one); }
                return v;
            };
            return applyArith(opc, resolve(lc), resolve(rc));
        };
        return code;
    }

    // ---- Complex arithmetic ----
    if (l.t == VT::Complex || r.t == VT::Complex) {
        auto toC = [](const Value& v) {
            return v.t == VT::Complex ? std::complex<double>(v.n, v.im)
                                      : std::complex<double>(v.toNum(), 0.0);
        };
        std::complex<double> a = toC(l), b = toC(r);
        auto mk = [](std::complex<double> z) { return Value::complex(z.real(), z.imag()); };
        if (op == "+") return mk(a + b);
        if (op == "-") return mk(a - b);
        if (op == "*") return mk(a * b);
        if (op == "/") return mk(a / b);
        if (op == "**") return mk(std::pow(a, b));
        if (op == "==" || op == "===" || op == "eqv") return Value::boolean(a == b);
        if (op == "!=") return Value::boolean(a != b);
    }

    // ---- exact numeric tower: Int (bignum) and Rat ----
    auto isExact = [](const Value& v) { return v.t == VT::Int || v.t == VT::Bool || v.t == VT::Rat; };
    if (isExact(l) && isExact(r)) {
        bool anyRat = (l.t == VT::Rat || r.t == VT::Rat);
        bool smallInt = !anyRat && !l.big && !r.big;
        auto getN = [](const Value& v) { return v.t == VT::Rat ? *v.ratN : v.toBig(); };
        auto getD = [](const Value& v) { return v.t == VT::Rat ? *v.ratD : BigInt(1); };
        if (op == "+" || op == "-" || op == "*") {
            if (smallInt) {
                long long a = l.toInt(), b = r.toInt(), res;
                if (op == "+" && !__builtin_add_overflow(a, b, &res)) return Value::integer(res);
                if (op == "-" && !__builtin_sub_overflow(a, b, &res)) return Value::integer(res);
                if (op == "*" && !__builtin_mul_overflow(a, b, &res)) return Value::integer(res);
            }
            if (!anyRat) {
                BigInt a = l.toBig(), b = r.toBig();
                return Value::bigint(op == "+" ? a + b : op == "-" ? a - b : a * b);
            }
            BigInt n1 = getN(l), d1 = getD(l), n2 = getN(r), d2 = getD(r), n, d;
            if (op == "*") { n = n1 * n2; d = d1 * d2; }
            else { d = d1 * d2; n = (op == "+") ? n1 * d2 + n2 * d1 : n1 * d2 - n2 * d1; }
            return Value::rat(n, d);
        }
        if (op == "/") {
            BigInt n1 = getN(l), d1 = getD(l), n2 = getN(r), d2 = getD(r);
            if (n2.isZero()) return Value::typeObj("Failure");
            return Value::rat(n1 * d2, d1 * n2);
        }
        if (op == "**" && (r.t == VT::Int || r.t == VT::Bool) && !r.big) {
            long long e = r.toInt();
            BigInt bn = getN(l), bd = getD(l);
            if (e >= 0) { BigInt rn = bn.pow(e), rd = bd.pow(e); return anyRat ? Value::rat(rn, rd) : Value::bigint(rn); }
            BigInt rn = bd.pow(-e), rd = bn.pow(-e);
            if (rd.isZero()) return Value::typeObj("Failure");
            return Value::rat(rn, rd);
        }
        if (op == "%" || op == "div" || op == "mod" || op == "%%") {
            if (smallInt && op != "div") { // native fast path for small ints (div stays on BigInt for identical rounding)
                long long a = l.toInt(), b = r.toInt();
                if (b == 0) return Value::typeObj("Failure");
                long long rem = a % b;
                if (op == "%%") return Value::boolean(rem == 0); // divisibility is sign-independent
                if (rem != 0 && ((rem < 0) != (b < 0))) rem += b; // sign follows divisor (matches BigInt path)
                return Value::integer(rem); // % / mod
            }
            BigInt a = l.toBig(), b = r.toBig();
            if (b.isZero()) return Value::typeObj("Failure");
            BigInt q, rem; BigInt::divmod(a, b, q, rem);
            if (op == "div") return Value::bigint(q);
            if (!rem.isZero() && ((rem.sign < 0) != (b.sign < 0))) rem = rem + b; // sign follows divisor
            if (op == "%%") return Value::boolean(rem.isZero());
            return Value::bigint(rem);
        }
        if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=" ||
            op == "<=>" || op == "cmp" || op == "leg") {
            int c;
            if (smallInt) { long long a = l.toInt(), b = r.toInt(); c = a < b ? -1 : a > b ? 1 : 0; }
            else c = BigInt::cmp(getN(l) * getD(r), getN(r) * getD(l));
            if (op == "==") return Value::boolean(c == 0);
            if (op == "!=") return Value::boolean(c != 0);
            if (op == "<")  return Value::boolean(c < 0);
            if (op == "<=") return Value::boolean(c <= 0);
            if (op == ">")  return Value::boolean(c > 0);
            if (op == ">=") return Value::boolean(c >= 0);
            return Value::enumVal(c < 0 ? "Less" : c > 0 ? "More" : "Same", c < 0 ? -1 : c > 0 ? 1 : 0);
        }
    }

    auto bothInt = [&]() {
        return (l.t == VT::Int || l.t == VT::Bool) && (r.t == VT::Int || r.t == VT::Bool);
    };
    if (op == "+") {
        if (bothInt()) return Value::integer(l.toInt() + r.toInt());
        return Value::number(l.toNum() + r.toNum());
    }
    if (op == "-") {
        if (bothInt()) return Value::integer(l.toInt() - r.toInt());
        return Value::number(l.toNum() - r.toNum());
    }
    if (op == "*") {
        if (bothInt()) return Value::integer(l.toInt() * r.toInt());
        return Value::number(l.toNum() * r.toNum());
    }
    if (op == "/") {
        double d = r.toNum();
        double res = (d == 0.0) ? 0.0 : l.toNum() / d;
        return Value::number(res);
    }
    if (op == "%") {
        long long b = r.toInt();
        if (b == 0) return Value::integer(0);
        long long m = l.toInt() % b;
        if ((m != 0) && ((m < 0) != (b < 0))) m += b; // Raku modulo sign follows divisor
        return Value::integer(m);
    }
    if (op == "div") {
        long long b = r.toInt();
        return Value::integer(b == 0 ? 0 : l.toInt() / b);
    }
    if (op == "mod") {
        long long b = r.toInt();
        return Value::integer(b == 0 ? 0 : l.toInt() % b);
    }
    if (op == "**") {
        double res = std::pow(l.toNum(), r.toNum());
        if (bothInt() && r.toInt() >= 0 && std::fabs(res) < 9e18)
            return Value::integer((long long)llround(res));
        return Value::number(res);
    }
    if (op == "~") return Value::str(l.toStr() + r.toStr());
    if (op == "x") {
        std::string base = l.toStr(), out;
        long long n = r.toInt();
        for (long long k = 0; k < n; k++) out += base;
        return Value::str(out);
    }
    if (op == "xx") {
        Value a = Value::array();
        long long n = r.toInt();
        for (long long k = 0; k < n; k++) a.arr->push_back(l);
        return a;
    }
    if (op == "gcd") { long long x = std::llabs(l.toInt()), y = std::llabs(r.toInt()); while (y) { long long t = x % y; x = y; y = t; } return Value::integer(x); }
    if (op == "lcm") { long long x = l.toInt(), y = r.toInt(); if (!x || !y) return Value::integer(0); long long g = std::llabs(x), h = std::llabs(y); while (h) { long long t = g % h; g = h; h = t; } return Value::integer(std::llabs(x / g * y)); }

    // comparisons -> Bool
    if (op == "==") return Value::boolean(l.toNum() == r.toNum());
    if (op == "!=") return Value::boolean(l.toNum() != r.toNum());
    if (op == "=~=" || op == "≅") { // approximately-equal (relative tolerance 1e-15)
        double a = l.toNum(), b = r.toNum();
        if (a == b) return Value::boolean(true);
        double scale = std::max(std::fabs(a), std::fabs(b));
        return Value::boolean(std::fabs(a - b) <= 1e-15 * scale);
    }
    if (op == "<")  return Value::boolean(l.toNum() <  r.toNum());
    if (op == "<=") return Value::boolean(l.toNum() <= r.toNum());
    if (op == ">")  return Value::boolean(l.toNum() >  r.toNum());
    if (op == ">=") return Value::boolean(l.toNum() >= r.toNum());
    if (op == "eq") return Value::boolean(l.toStr() == r.toStr());
    if (op == "ne") return Value::boolean(l.toStr() != r.toStr());
    if (op == "lt") return Value::boolean(l.toStr() <  r.toStr());
    if (op == "gt") return Value::boolean(l.toStr() >  r.toStr());
    if (op == "le") return Value::boolean(l.toStr() <= r.toStr());
    if (op == "ge") return Value::boolean(l.toStr() >= r.toStr());
    auto orderVal = [](int c) {
        return Value::enumVal(c < 0 ? "Less" : c > 0 ? "More" : "Same", c < 0 ? -1 : c > 0 ? 1 : 0);
    };
    if (op == "<=>") { double a = l.toNum(), b = r.toNum(); return orderVal(a < b ? -1 : a > b ? 1 : 0); }
    if (op == "cmp" || op == "leg") { return orderVal(valueCmp(l, r)); }
    if (op == "before") return Value::boolean(valueCmp(l, r) < 0);
    if (op == "after") return Value::boolean(valueCmp(l, r) > 0);
    if (op == "eqv") return Value::boolean(valueEqv(l, r));
    if (op == "===" || op == "!==") {
        bool same;
        if (l.t != r.t) same = false;
        else if (l.t == VT::Object) same = (l.obj == r.obj);
        else if (l.t == VT::Type) same = (l.s == r.s);
        else if (l.t == VT::Code) same = (l.code == r.code);
        else if (l.t == VT::Array) same = (l.arr == r.arr); // Lists/Arrays: reference identity
        else if (l.t == VT::Hash) same = l.hashKind.empty() ? (l.hash == r.hash) // plain Hash: reference
                                                            : (l.toStr() == r.toStr()); // Set/Bag/Mix: value
        else same = (l.toStr() == r.toStr()); // value types (Int/Str/Num/Rat/...)
        return Value::boolean(op == "===" ? same : !same);
    }
    if (op == "%%") { long long b = r.toInt(); return Value::boolean(b != 0 && l.toInt() % b == 0); }
    if (op == "=:=") return Value::boolean(l.t == r.t && valueEq(l, r));
    if (op == "~~" || op == "!~~") {
        bool res;
        if (r.t == VT::Range) {
            double v = l.toNum();
            double lo = r.rFrom, hi = r.rTo;
            res = v >= lo && (r.rExTo ? v < hi : v <= hi);
        } else if (r.t == VT::Type) {
            res = (l.typeName() == r.s) || r.s == "Any" || r.s == "Mu" ||
                  (r.s == "Numeric" && l.isNumeric()) || (r.s == "Cool") ||
                  (l.t == VT::Hash && l.hashKind == "FileHandle" && (r.s == "IO::Handle" || r.s == "IO"));
            // object: match against its class / ancestor names
            if (!res && l.t == VT::Object && l.obj)
                for (ClassInfo* ci = l.obj->cls.get(); ci; ci = ci->parent.get())
                    if (ci->name == r.s) { res = true; break; }
        } else if (r.t == VT::Bool) {
            res = r.b; // $x ~~ True/False
        } else if (r.t == VT::Hash) {
            if (l.t == VT::Array) { // @a ~~ %h : any element is a key
                res = false;
                if (l.arr) for (auto& e : *l.arr) if (r.hash && r.hash->count(e.toStr())) { res = true; break; }
            } else res = r.hash && r.hash->count(l.toStr()) > 0; // Cool ~~ Hash : key exists
        } else if ((l.t == VT::Complex || r.t == VT::Complex) &&
                   (l.isNumeric() || l.t == VT::Complex) && (r.isNumeric() || r.t == VT::Complex)) {
            res = applyArith("==", l, r).truthy(); // numeric smartmatch incl. Complex (3 ~~ 3+0i)
        } else if (r.t == VT::Object) {
            res = (l.t == VT::Object && l.obj.get() == r.obj.get()); // object identity
        } else {
            res = valueEq(l, r);
        }
        return Value::boolean(op == "~~" ? res : !res);
    }
    throw RakuError{Value::str("op"), "Unsupported operator '" + op + "'"};
}

Value Interpreter::regexMatch(const std::string& subject, const std::string& pattern) {
    Regex re(pattern);
    RxMatch m;
    Value mv;
    if (re.ok() && re.search(subject, 0, m)) {
        mv = Value::matchVal(subject.substr(m.from, m.to - m.from), m.from, m.to);
        for (auto& c : m.caps) {
            if (c.first < 0) mv.arr->push_back(Value::nil());
            else mv.arr->push_back(Value::matchVal(subject.substr(c.first, c.second - c.first), c.first, c.second));
        }
        for (auto& kv : m.named)
            (*mv.hash)[kv.first] = Value::matchVal(subject.substr(kv.second.first, kv.second.second - kv.second.first), kv.second.first, kv.second.second);
    } else {
        mv = Value::nil();
    }
    cur_->define("$/", mv);
    if (mv.t == VT::Match) {
        for (size_t k = 0; k < mv.arr->size(); k++) cur_->define("$" + std::to_string(k), (*mv.arr)[k]);
        for (auto& kv : *mv.hash) cur_->define("$<" + kv.first + ">", kv.second);
    }
    return mv;
}

Value Interpreter::regexSubst(const std::string& subject, const std::string& pattern,
                              const std::string& repl, std::string& out, bool& changed) {
    // global if a leading :g adverb was prepended to the pattern
    bool global = false;
    { std::istringstream is(pattern); std::string tok;
      while (is >> tok) { if (tok[0] != ':') break; if (tok == ":g" || tok == ":global") global = true; } }
    Regex re(pattern);
    out.clear(); changed = false;
    Value last = Value::nil();
    long pos = 0;
    RxMatch m;
    while (re.ok() && pos <= (long)subject.size() && re.search(subject, pos, m)) {
        out += subject.substr(pos, m.from - pos);
        out += repl; // (no $0 interpolation in replacement yet)
        changed = true;
        last = Value::matchVal(subject.substr(m.from, m.to - m.from), m.from, m.to);
        if (m.to == m.from) { // empty match: emit one char to make progress
            if (m.to < (long)subject.size()) out += subject[m.to];
            pos = m.to + 1;
        } else {
            pos = m.to;
        }
        if (!global) break;
    }
    if (pos < (long)subject.size()) out += subject.substr(pos);
    cur_->define("$/", last);
    return last;
}

Value Interpreter::grammarParse(ClassInfo* g, const std::string& input, bool subparse,
                                const std::string& startRule, Value actions) {
    bool haveActions = (actions.t == VT::Object || actions.t == VT::Type);
    ClassInfo* actCls = nullptr;
    if (actions.t == VT::Object && actions.obj) actCls = actions.obj->cls.get();
    else if (actions.t == VT::Type) { auto it = classes_.find(actions.s); if (it != classes_.end()) actCls = it->second.get(); }

    // stash of fully-built sub-Match values (with .made), keyed for the parent to attach
    struct Stashed { std::string name; long from, to; Value v; };
    auto stash = std::make_shared<std::vector<Stashed>>();

    // run the actions method for `name` (if any) on a freshly built Match, setting .made
    auto runAction = [&](const std::string& name, Value& mv) {
        if (!haveActions || !actCls) return;
        Value* method = actCls->findMethod(name);
        if (!method) return;
        makeTargets_.push_back(&mv);
        try { invokeMethod(*method, actions, {mv}); } catch (...) { makeTargets_.pop_back(); throw; }
        makeTargets_.pop_back();
    };
    // build a Match value for rule `name` from RxMatch `m`, attaching stashed children, then act
    auto buildTree = [&](const std::string& name, const RxMatch& m, const std::string& subj) -> Value {
        Value mv = Value::matchVal(subj.substr(m.from, m.to - m.from), m.from, m.to);
        for (auto& c : m.caps) {
            if (c.first < 0) mv.arr->push_back(Value::nil());
            else mv.arr->push_back(Value::matchVal(subj.substr(c.first, c.second - c.first), c.first, c.second));
        }
        for (auto& kv : m.named) {
            Value child;
            bool found = false;
            for (auto it = stash->rbegin(); it != stash->rend(); ++it)
                if (it->name == kv.first && it->from == kv.second.first && it->to == kv.second.second) { child = it->v; found = true; break; }
            if (!found) child = Value::matchVal(subj.substr(kv.second.first, kv.second.second - kv.second.first), kv.second.first, kv.second.second);
            (*mv.hash)[kv.first] = child;
        }
        runAction(name, mv);
        return mv;
    };

    SubResolver resolver;
    resolver = [&](const std::string& name, const std::string& subj, long pos, RxMatch& out) -> bool {
        if (name == "ws") { // optional whitespace
            long p = pos; while (p < (long)subj.size() && std::isspace((unsigned char)subj[p])) p++;
            out.from = pos; out.to = p; out.matched = true; return true;
        }
        const std::string* pat = g->findRule(name);
        if (!pat) return false;
        std::string kind = g->ruleKind.count(name) ? g->ruleKind[name] : "token";
        Regex re(*pat, kind == "rule" ? "s" : "");
        if (!re.matchAt(subj, pos, out, resolver)) return false;
        Value child = buildTree(name, out, subj);
        stash->push_back({name, out.from, out.to, child});
        return true;
    };
    const std::string* top = g->findRule(startRule);
    if (!top) { cur_->define("$/", Value::nil()); return Value::nil(); }
    std::string kind = g->ruleKind.count(startRule) ? g->ruleKind[startRule] : "token";
    Regex re(*top, kind == "rule" ? "s" : "");
    RxMatch m;
    bool ok = re.matchAt(input, 0, m, resolver);
    if (!ok || (!subparse && m.to != (long)input.size())) { cur_->define("$/", Value::nil()); return Value::nil(); }
    Value mv = buildTree(startRule, m, input);
    cur_->define("$/", mv);
    return mv;
}

Value Interpreter::evalBinary(Binary* b) {
    const std::string& op = b->op;
    if (op == "~~" || op == "!~~") {
        // regex match: $str ~~ /pat/   /   $str ~~ s/pat/repl/
        if (b->rhs->kind == NK::RegexLit) {
            Value l = eval(b->lhs.get());
            Value m = regexMatch(l.toStr(), static_cast<RegexLit*>(b->rhs.get())->pattern);
            return Value::boolean((op == "~~") == m.truthy());
        }
        if (b->rhs->kind == NK::SubstLit) {
            auto* sub = static_cast<SubstLit*>(b->rhs.get());
            Value l = eval(b->lhs.get());
            std::string out; bool changed;
            Value m = regexSubst(l.toStr(), sub->pattern, sub->repl, out, changed);
            if (sub->nonMut) return Value::str(out);        // S/// : return new string, leave lhs intact
            if (Value* lv = lvalue(b->lhs.get())) *lv = Value::str(out);
            return m;
        }
        // `X ~~ Y` topicalizes: $_ is bound to X while Y is evaluated (so `$x ~~ .so` works)
        Value lTopic = eval(b->lhs.get());
        Value savedTopic = cur_->vars.count("$_") ? cur_->vars["$_"] : Value::any();
        cur_->define("$_", lTopic);
        Value r;
        try { r = eval(b->rhs.get()); } catch (...) { cur_->vars["$_"] = savedTopic; throw; }
        cur_->vars["$_"] = savedTopic;
        if (r.t == VT::Regex) {
            Value m = regexMatch(lTopic.toStr(), r.s);
            return Value::boolean((op == "~~") == m.truthy());
        }
        return applyArith(op, lTopic, r); // generic smartmatch on the already-evaluated operands
    }
    if (op == "&&" || op == "and") {
        Value l = eval(b->lhs.get());
        if (!boolify(l)) return l;
        return eval(b->rhs.get());
    }
    if (op == "||" || op == "or") {
        Value l = eval(b->lhs.get());
        if (boolify(l)) return l;
        return eval(b->rhs.get());
    }
    if (op == "andthen" || op == "orelse") {
        Value l = eval(b->lhs.get());
        bool def = isDefined(l);
        if ((op == "andthen") != def) return l; // andthen: skip if undefined; orelse: skip if defined
        auto scope = std::make_shared<Env>(); scope->parent = cur_;
        scope->define("$_", l);
        auto saved = cur_; cur_ = scope;
        Value r; try { r = eval(b->rhs.get()); } catch (...) { cur_ = saved; throw; }
        cur_ = saved; return r;
    }
    if (op == "//") {
        Value l = eval(b->lhs.get());
        if (isDefined(l)) return l;
        return eval(b->rhs.get());
    }
    if (op == "^^" || op == "xor") {
        Value l = eval(b->lhs.get());
        Value r = eval(b->rhs.get());
        return Value::boolean(boolify(l) != boolify(r));
    }
    Value l = eval(b->lhs.get());
    Value r = eval(b->rhs.get());
    return applyArith(op, l, r);
}

Value Interpreter::evalUnary(Unary* u) {
    // control-flow in expression position: return/last/next/redo
    if (u->op == "return") throw ReturnEx{u->operand ? eval(u->operand.get()) : Value::any()};
    if (u->op == "last") throw LastEx{};
    if (u->op == "next") throw NextEx{};
    if (u->op == "redo") throw RedoEx{};
    // reduction metaoperator [op]
    if (u->op.size() >= 3 && u->op.front() == '[' && u->op.back() == ']') {
        std::string op = u->op.substr(1, u->op.size() - 2);
        ValueList items = eval(u->operand.get()).flatten();
        if (items.empty()) {
            if (op == "+" || op == "-") return Value::integer(0);
            if (op == "*" || op == "/") return Value::integer(1);
            if (op == "~") return Value::str("");
            return Value::any();
        }
        Value acc = items[0];
        for (size_t k = 1; k < items.size(); k++) acc = applyArith(op, acc, items[k]);
        return acc;
    }
    if (u->op == "ctx$" || u->op == "ctx@" || u->op == "ctx%") {
        Value v = eval(u->operand.get());
        if (u->op == "ctx@") return Value::array(v.flatten());
        if (u->op == "ctx%") return v.t == VT::Hash ? v : coerceHash(v); // %(...) hash composer
        if (v.t == VT::Array) v.itemized = true; // $[...] / $(...): array becomes one non-flattening item
        return v; // item context
    }
    if (u->op == "do") {
        if (u->operand->kind == NK::BlockExpr)
            return callCallable(makeClosure(static_cast<BlockExpr*>(u->operand.get())), {});
        return eval(u->operand.get());
    }
    if (u->op == "try") {
        try {
            Value r;
            if (u->operand->kind == NK::BlockExpr)
                r = callCallable(makeClosure(static_cast<BlockExpr*>(u->operand.get())), {});
            else r = eval(u->operand.get());
            cur_->define("$!", Value::nil());
            return r;
        } catch (RakuError& e) {
            cur_->define("$!", e.payload);
            return Value::nil();
        }
    }
    if (u->op == "quietly") { // suppress warnings (we don't emit any) — just run
        if (u->operand->kind == NK::BlockExpr)
            return callCallable(makeClosure(static_cast<BlockExpr*>(u->operand.get())), {});
        return eval(u->operand.get());
    }
    if (u->op == "gather") {
        auto collector = std::make_shared<ValueList>();
        gatherStack_.push_back(collector);
        try {
            if (u->operand->kind == NK::BlockExpr)
                callCallable(makeClosure(static_cast<BlockExpr*>(u->operand.get())), {});
            else eval(u->operand.get());
        } catch (...) { gatherStack_.pop_back(); throw; }
        gatherStack_.pop_back();
        return Value::array(*collector);
    }
    if (u->op == "++" || u->op == "--") {
        Value* lv = lvalue(u->operand.get());
        Value oldv = *lv;
        Value newv;
        bool strMagic = lv->t == VT::Str && !lv->s.empty() &&
                        std::any_of(lv->s.begin(), lv->s.end(), [](char c){ return std::isalpha((unsigned char)c); });
        if (strMagic && u->op == "++") {
            newv = Value::str(strSucc(lv->s));
        } else if (strMagic && u->op == "--") {
            bool ok; std::string r = strPred(lv->s, ok);
            newv = ok ? Value::str(r) : Value::typeObj("Failure");
        } else {
            newv = applyArith(u->op == "++" ? "+" : "-", *lv, Value::integer(1));
        }
        *lv = newv;
        return u->postfix ? oldv : newv;
    }
    Value v = eval(u->operand.get());
    // Numeric context of a list/array/hash/range is its element count.
    if ((u->op == "+" || u->op == "-") &&
        (v.t == VT::Array || v.t == VT::Hash || v.t == VT::Range)) {
        long long n;
        if (v.t == VT::Array) n = (long long)v.arr->size();
        else if (v.t == VT::Hash) n = (long long)v.hash->size();
        else n = (long long)v.flatten().size();
        return Value::integer(u->op == "-" ? -n : n);
    }
    if (u->op == "-") {
        if (v.t == VT::Complex) return Value::complex(-v.n, -v.im);
        if (v.t == VT::Int && v.big) return Value::bigint(-(*v.big));
        if (v.t == VT::Int || v.t == VT::Bool) return Value::integer(-v.toInt());
        if (v.t == VT::Rat) return Value::rat(-(*v.ratN), *v.ratD);
        if (v.t == VT::Str) { Value n = numifyStr(v.s); return n.t==VT::Rat ? Value::rat(-(*n.ratN),*n.ratD) : Value::number(-n.toNum()); }
        return Value::number(-v.toNum());
    }
    if (u->op == "+") return v.isNumeric() ? v : (v.t == VT::Str ? numifyStr(v.s) : Value::number(v.toNum()));
    if (u->op == "~") return Value::str(v.toStr());
    if (u->op == "!") return Value::boolean(!boolify(v));
    if (u->op == "?") return Value::boolean(boolify(v));
    if (u->op == "^") return Value::range(0, v.toInt(), false, true);
    if (u->op == "|") return v; // slip: spread handled in evalArgs
    throw RakuError{Value::str("op"), "Unsupported prefix '" + u->op + "'"};
}

ValueList Interpreter::evalArgs(const std::vector<ExprPtr>& exprs) {
    ValueList args;
    for (auto& a : exprs) {
        if (a->kind == NK::RegexLit) {
            // a regex literal passed as an argument is a Regex object, not a match
            args.push_back(Value::regex(static_cast<RegexLit*>(a.get())->pattern));
        } else if (a->kind == NK::Unary && static_cast<Unary*>(a.get())->op == "|") {
            Value v = eval(static_cast<Unary*>(a.get())->operand.get());
            if (v.t == VT::Array || v.t == VT::Range) { for (auto& x : v.flatten()) args.push_back(x); }
            else if (v.t == VT::Hash && v.hash) { for (auto& kv : *v.hash) args.push_back(Value::pair(kv.first, kv.second)); }
            else args.push_back(v);
        } else {
            args.push_back(eval(a.get()));
        }
    }
    return args;
}

Value Interpreter::evalCall(Call* c) {
    ValueList args = evalArgs(c->args);
    if (c->callee) {
        Value f = eval(c->callee.get());
        return callCallable(f, args);
    }
    if (!c->name.empty()) {
        if (Value* f = cur_->find("&" + c->name)) return callCallable(*f, args);
        auto it = builtins_.find(c->name);
        if (it != builtins_.end()) return it->second(*this, args);
    }
    throw RakuError{Value::str("Undefined routine &" + c->name),
                    "Undefined routine '" + c->name + "'"};
}

Value Interpreter::evalIndex(Index* idx) {
    Value base = eval(idx->base.get());

    // Match object indexing: $/[n] positional, $/{key} / $/<key> named
    if (base.t == VT::Match) {
        if (idx->isHash) {
            std::string key = eval(idx->index.get()).toStr();
            if (base.hash) { auto it = base.hash->find(key); if (it != base.hash->end()) return it->second; }
            return Value::nil();
        }
        long long n = eval(idx->index.get()).toInt();
        if (base.arr && n >= 0 && n < (long long)base.arr->size()) return (*base.arr)[n];
        return Value::nil();
    }

    if (!idx->adverb.empty()) {
        std::string adv = idx->adverb;
        bool neg = false;
        if (!adv.empty() && adv[0] == '!') { neg = true; adv = adv.substr(1); }
        bool exists = false;
        Value val;
        std::string key;
        long long ai = 0;
        if (idx->isHash) {
            key = eval(idx->index.get()).toStr();
            if (base.t == VT::Hash && base.hash) {
                auto it = base.hash->find(key);
                if (it != base.hash->end()) { exists = true; val = it->second; }
            }
        } else {
            ai = eval(idx->index.get()).toInt();
            if (base.t == VT::Array && base.arr) {
                if (ai < 0) ai += (long long)base.arr->size();
                if (ai >= 0 && ai < (long long)base.arr->size()) { exists = isDefined((*base.arr)[ai]); val = (*base.arr)[ai]; }
            }
        }
        Value keyV = idx->isHash ? Value::str(key) : Value::integer(ai);
        if (adv == "exists") return Value::boolean(neg ? !exists : exists);
        if (adv == "delete") {
            if (exists) {
                if (idx->isHash) base.hash->erase(key);
                else (*base.arr)[ai] = Value::any();
            }
            return exists ? val : Value::any();
        }
        if (adv == "k") return exists ? keyV : Value::array();
        if (adv == "v") return exists ? val : Value::array();
        if (adv == "kv") return exists ? Value::array({keyV, val}) : Value::array();
        if (adv == "p") return exists ? Value::pair(keyV.toStr(), val) : Value::array();
        // unknown adverb: fall through to plain lookup
    }

    if (idx->isHash) {
        std::string key = eval(idx->index.get()).toStr();
        if (base.t == VT::Hash && base.hash) {
            auto it = base.hash->find(key);
            if (it != base.hash->end()) return it->second;
        }
        // Set/Bag/Mix: absent key has a typed default
        if (base.t == VT::Hash && !base.hashKind.empty()) {
            if (base.hashKind.find("Set") == 0) return Value::boolean(false);
            return Value::integer(0); // Bag/Mix
        }
        return Value::any();
    }
    // array/list slice: @a[1..*], @a[0,2,4], @a[@indices]
    if (!idx->isHash && (base.t == VT::Array || base.t == VT::Range || base.t == VT::Str)) {
        ValueList src = (base.t == VT::Array && base.arr) ? *base.arr : base.flatten();
        long long n = (long long)src.size();
        std::vector<long long> indices;
        bool isSlice = false;
        if (idx->index->kind == NK::Range) {
            auto* re = static_cast<RangeExpr*>(idx->index.get());
            long long from = eval(re->from.get()).toInt();
            Value toV = eval(re->to.get());
            long long to = (toV.t == VT::Whatever || std::isinf(toV.toNum())) ? n - 1 : toV.toInt();
            if (re->exTo) to--;
            if (from < 0) from += n;
            isSlice = true;
            for (long long k = from; k <= to; k++) indices.push_back(k);
        } else {
            Value iv = eval(idx->index.get());
            if (iv.t == VT::Range || iv.t == VT::Array) {
                isSlice = true;
                for (auto& e : iv.flatten()) indices.push_back(e.toInt());
            } else {
                long long i = iv.toInt();
                if (base.t == VT::Str) { if (i < 0) i += (long long)base.s.size(); return (i >= 0 && i < (long long)base.s.size()) ? Value::str(std::string(1, base.s[i])) : Value::any(); }
                if (i < 0) i += n;
                return (i >= 0 && i < n) ? src[i] : Value::any();
            }
        }
        if (isSlice) {
            Value out = Value::array(); out.isList = true;
            for (long long k : indices) { if (k < 0) k += n; if (k >= 0 && k < n) out.arr->push_back(src[k]); }
            return out;
        }
    }
    long long i = eval(idx->index.get()).toInt();
    if (base.t == VT::Array && base.arr) {
        if (i < 0) i += (long long)base.arr->size();
        if (i >= 0 && i < (long long)base.arr->size()) return (*base.arr)[i];
    } else if (base.t == VT::Str) {
        if (i >= 0 && i < (long long)base.s.size()) return Value::str(std::string(1, base.s[i]));
    } else if (base.t == VT::Range) {
        ValueList f = base.flatten();
        if (i >= 0 && i < (long long)f.size()) return f[i];
    }
    return Value::any();
}

Value Interpreter::eval(Expr* e) {
    switch (e->kind) {
        case NK::IntLit: {
            auto* il = static_cast<IntLit*>(e);
            return il->big.empty() ? Value::integer(il->v) : Value::bigint(BigInt::fromString(il->big));
        }
        case NK::NumLit: { auto* nl = static_cast<NumLit*>(e); return nl->imaginary ? Value::complex(0, nl->v) : Value::number(nl->v); }
        case NK::StrLit: return Value::str(static_cast<StrLit*>(e)->v);
        case NK::RegexLit: {
            // standalone regex matches against $_
            auto* rl = static_cast<RegexLit*>(e);
            Value topic; if (Value* p = cur_->find("$_")) topic = *p;
            return regexMatch(topic.toStr(), rl->pattern);
        }
        case NK::SubstLit: {
            auto* sl = static_cast<SubstLit*>(e);
            Value topic; if (Value* p = cur_->find("$_")) topic = *p;
            std::string out; bool changed;
            Value m = regexSubst(topic.toStr(), sl->pattern, sl->repl, out, changed);
            if (sl->nonMut) return Value::str(out);        // S/// : return new string, leave $_ intact
            if (Value* p = cur_->find("$_")) *p = Value::str(out);
            return m;
        }
        case NK::BoolLit: return Value::boolean(static_cast<BoolLit*>(e)->v);
        case NK::InterpStr: return evalInterp(static_cast<InterpStr*>(e));
        case NK::VarExpr: {
            auto* ve = static_cast<VarExpr*>(e);
            char sigil = ve->name.empty() ? '$' : ve->name[0];
            if (ve->name == "$=finish") return Value::str(finishData_); // =finish data block
            if (ve->name == "$*CWD") { char buf[4096]; Value p = Value::str(getcwd(buf, sizeof buf) ? buf : "."); p.hashKind = "IO"; return p; }
            if (ve->name == "$*RAKU" || ve->name == "$*PERL" || ve->name == "$?RAKU" || ve->name == "$?PERL") {
                Value r = Value::makeHash(); r.hashKind = "Raku"; return r;
            }
            if (ve->name == "$?LINE") return Value::integer(ve->line);
            if (ve->name == "$?FILE") return Value::str(srcFile_);
            if (ve->name == "$*PROGRAM") { Value p = Value::str(srcFile_); p.hashKind = "IO"; return p; } // running script, as IO::Path
            if (ve->name == "$*PROGRAM-NAME") return Value::str(srcFile_);
            if (ve->name == "$*EXECUTABLE" || ve->name == "$*EXECUTABLE-NAME") { Value p = Value::str(execPath_); p.hashKind = "IO"; return p; }
            if (ve->declare) {
                if (ve->declScope == "state" && curStateEnv_) { // persistent across calls
                    if (!curStateEnv_->vars.count(ve->name)) curStateEnv_->define(ve->name, defaultFor(sigil));
                    return curStateEnv_->vars[ve->name];
                }
                cur_->define(ve->name, defaultFor(sigil));
                return cur_->vars[ve->name];
            }
            if (ve->name.size() > 2 && (ve->name[1] == '.' || ve->name[1] == '!')) {
                Value* selfp = cur_->find("self");
                if (selfp && selfp->t == VT::Object && selfp->obj) {
                    auto it = selfp->obj->attrs.find(ve->name.substr(2));
                    if (it != selfp->obj->attrs.end()) return it->second;
                }
                return defaultFor(sigil);
            }
            Value* p = cur_->find(ve->name);
            if (p) return *p;
            if (!isSpecialVar(ve->name))
                throw RakuError{Value::typeObj("X::Undeclared"),
                                "Variable '" + ve->name + "' is not declared"};
            return defaultFor(sigil);
        }
        case NK::SelfTerm: {
            Value* p = cur_->find("self");
            return p ? *p : Value::any();
        }
        case NK::NameTerm: {
            auto* nt = static_cast<NameTerm*>(e);
            const std::string& n = nt->name;
            if (n == "next") throw NextEx{};
            if (n == "last") throw LastEx{};
            if (n == "redo") throw RedoEx{};
            if (n == "Nil") return Value::nil();
            if (n == "True") return Value::boolean(true);
            if (n == "False") return Value::boolean(false);
            if (n == "Inf") return Value::number(INFINITY);
            if (n == "NaN") return Value::number(NAN);
            if (n == "Order::Same" || n == "Same") return Value::enumVal("Same", 0);
            if (n == "Order::Less" || n == "Less") return Value::enumVal("Less", -1);
            if (n == "Order::More" || n == "More") return Value::enumVal("More", 1);
            if (n == "pi" || n == "π") return Value::number(M_PI);
            if (n == "e") return Value::number(M_E);
            if (n == "i") return Value::complex(0, 1); // imaginary unit
            if (n == "tau" || n == "τ") return Value::number(2 * M_PI);
            if (n == "now") { // Instant: high-resolution seconds since the epoch
                auto d = std::chrono::system_clock::now().time_since_epoch();
                return Value::number(std::chrono::duration<double>(d).count());
            }
            if (n == "time") return Value::integer((long long)::time(nullptr)); // POSIX seconds (Int)
            static const std::set<std::string> types = {
                "Int", "Str", "Num", "Bool", "Any", "Mu", "Cool", "Numeric", "Real",
                "Array", "Hash", "List", "Rat", "Complex", "Nil", "Pair", "Range",
                "Code", "Sub", "Block", "Junction", "Whatever", "Capture", "Stringy",
                "Set", "SetHash", "Bag", "BagHash", "Mix", "MixHash",
                "Baggy", "Setty", "Mixy", "QuantHash", "Map", "Associative", "Positional",
                "Version", "Blob", "Buf", "Compiler", "Seq", "IO::Path", "Iterable",
                "Uni", "NFC", "NFD", "NFKC", "NFKD",
            };
            if (types.count(n)) return Value::typeObj(n);
            if (Value* p = cur_->find(n)) return *p;
            if (Value* f = cur_->find("&" + n)) return callCallable(*f, {});
            auto it = builtins_.find(n);
            if (it != builtins_.end()) { ValueList none; return it->second(*this, none); }
            return Value::typeObj(n);
        }
        case NK::ListExpr: {
            auto* l = static_cast<ListExpr*>(e);
            ValueList items;
            for (auto& it : l->items) items.push_back(eval(it.get()));
            return listToArray(items);
        }
        case NK::ArrayLit: {
            auto* l = static_cast<ArrayLit*>(e);
            Value a = Value::array();
            for (auto& it : l->items) {
                Value v = eval(it.get());
                // a bare @-variable (or a |slip) flattens into the array literal;
                // nested [...] literals stay as single items.
                bool flatten = (it->kind == NK::VarExpr && !static_cast<VarExpr*>(it.get())->name.empty() &&
                                static_cast<VarExpr*>(it.get())->name[0] == '@') ||
                               (v.t == VT::Array && v.isList);
                if (flatten && v.t == VT::Array) { for (auto& x : *v.arr) a.arr->push_back(x); }
                else a.arr->push_back(v);
            }
            return a;
        }
        case NK::HashLit: {
            auto* l = static_cast<HashLit*>(e);
            Value items = Value::array();
            for (auto& it : l->items) {
                Value v = eval(it.get());
                if (v.t == VT::Array) { for (auto& x : *v.arr) items.arr->push_back(x); }
                else items.arr->push_back(v);
            }
            return coerceHash(items);
        }
        case NK::Assign: return evalAssign(static_cast<Assign*>(e));
        case NK::Binary: return evalBinary(static_cast<Binary*>(e));
        case NK::ChainExpr: {
            auto* ch = static_cast<ChainExpr*>(e);
            Value prev = eval(ch->operands[0].get());
            for (size_t k = 0; k < ch->ops.size(); k++) {
                Value next = eval(ch->operands[k + 1].get());
                if (!applyArith(ch->ops[k], prev, next).truthy()) return Value::boolean(false);
                prev = next;
            }
            return Value::boolean(true);
        }
        case NK::Unary: return evalUnary(static_cast<Unary*>(e));
        case NK::Call: return evalCall(static_cast<Call*>(e));
        case NK::Index: return evalIndex(static_cast<Index*>(e));
        case NK::MethodCall: {
            auto* mc = static_cast<MethodCall*>(e);
            Value inv = eval(mc->inv.get());
            ValueList args = evalArgs(mc->args);
            // Whatever-currying: `*.method(...)` yields a WhateverCode
            if (inv.t == VT::Whatever || (inv.t == VT::Code && inv.code && inv.code->isWhateverCode)) {
                Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
                code.code->isWhateverCode = true;
                Value self = inv; std::string method = mc->method; ValueList margs = args;
                code.code->builtin = [self, method, margs](Interpreter& I, ValueList& a) -> Value {
                    Value arg = a.empty() ? Value::any() : a[0];
                    Value base = arg;
                    if (self.t != VT::Whatever) {
                        if (self.t == VT::Code && self.code && self.code->isWhateverCode) base = I.callCallable(self, ValueList{arg});
                        else base = self;
                    }
                    ValueList ma = margs;
                    return I.methodCall(base, method, ma);
                };
                return code;
            }
            if (mc->hyper) { // >>.method : apply to each top-level element (structure-preserving, no deep flatten)
                Value out = Value::array();
                if (inv.t == VT::Array && inv.arr)
                    for (auto& el : *inv.arr) out.arr->push_back(methodCall(el, mc->method, args));
                else
                    for (auto& el : inv.flatten()) out.arr->push_back(methodCall(el, mc->method, args));
                out.isList = true;
                if (mc->mutate) { if (Value* lv = lvalue(mc->inv.get())) { out.isList = false; *lv = out; } }
                return out;
            }
            Value res = methodCall(inv, mc->meta ? "^" + mc->method : mc->method, args, &mc->args);
            if (mc->mutate) { if (Value* lv = lvalue(mc->inv.get())) *lv = res; }
            return res;
        }
        case NK::Ternary: {
            auto* t = static_cast<Ternary*>(e);
            return boolify(eval(t->cond.get())) ? eval(t->then.get()) : eval(t->els.get());
        }
        case NK::Range: {
            auto* r = static_cast<RangeExpr*>(e);
            Value from = eval(r->from.get());
            Value to = eval(r->to.get());
            if (from.t == VT::Str && to.t == VT::Str) {
                Value arr = Value::array();
                std::string cur = from.s, end = to.s;
                for (int g = 0; g < 1000000; g++) {
                    if (cur.length() > end.length()) break;
                    if (cur == end) { if (!r->exTo) arr.arr->push_back(Value::str(cur)); break; }
                    arr.arr->push_back(Value::str(cur));
                    cur = strSucc(cur);
                }
                return arr;
            }
            return Value::range(from.toInt(), to.toInt(), r->exFrom, r->exTo);
        }
        case NK::Pair: {
            auto* p = static_cast<PairExpr*>(e);
            std::string key = p->keyExpr ? eval(p->keyExpr.get()).toStr() : p->key;
            return Value::pair(key, eval(p->value.get()));
        }
        case NK::BlockExpr: return makeClosure(static_cast<BlockExpr*>(e));
        case NK::Whatever: return Value::whatever();
        default:
            return Value::any();
    }
}

} // namespace rakupp
