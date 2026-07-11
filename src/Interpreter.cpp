#include "Interpreter.h"
#include <functional>
#include <memory>
#include <cstring>
#include "Platform.h"
#ifdef __APPLE__
#include <crt_externs.h>
static char** rakupp_environ() { return *_NSGetEnviron(); }
#elif defined(_WIN32)
static char** rakupp_environ() { return _environ; }
#else
extern char** environ;
static char** rakupp_environ() { return environ; }
#endif
#include "Regex.h"
#include "Lexer.h"
#include "Parser.h"
#include "Unicode.h"
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

// Thread-local RNG state: drand48's process-global state is not thread-safe, so
// under parallel execution each thread keeps its own erand48 seed.
static thread_local bool g_rand_seeded = false;
static thread_local unsigned short g_rand_xs[3];
// `srand($seed)` reseeds the generator (Raku returns the seed used). NB rakupp's PRNG
// is erand48, not MoarVM's — the same seed does NOT reproduce Rakudo's exact sequence.
void srandSeed(long long s) {
    g_rand_seeded = true;
    g_rand_xs[0] = (unsigned short)s; g_rand_xs[1] = (unsigned short)(s >> 16); g_rand_xs[2] = (unsigned short)(s >> 32);
}
// Uniform random double in [0, 1). Seeded once from time+pid if srand wasn't called.
double randDouble() {
    if (!g_rand_seeded) {
        g_rand_seeded = true;
        unsigned long s = (unsigned long)::time(nullptr) ^ ((unsigned long)::getpid() << 16)
                        ^ (unsigned long)std::hash<std::thread::id>{}(std::this_thread::get_id());
        g_rand_xs[0] = (unsigned short)s; g_rand_xs[1] = (unsigned short)(s >> 16); g_rand_xs[2] = (unsigned short)(s >> 32);
    }
    return erand48(g_rand_xs);
}

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

static bool isDefined(const Value& v) { return v.t != VT::Nil && v.t != VT::Any && v.t != VT::Type && !(v.t == VT::Hash && v.hashKind == "Failure"); }

// Howard Hinnant's days<->civil algorithms (proleptic Gregorian, day 0 = 1970-01-01).
long long civilToDays(long long y, long long m, long long d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
void daysToCivil(long long z, long long& y, long long& m, long long& d) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
}
Value makeDate(long long days) {
    long long y, m, d; daysToCivil(days, y, m, d);
    Value v = Value::makeHash(); v.hashKind = "Date";
    (*v.hash)["year"] = Value::integer(y);
    (*v.hash)["month"] = Value::integer(m);
    (*v.hash)["day"] = Value::integer(d);
    return v;
}
static bool isDateVal(const Value& v) { return v.t == VT::Hash && v.hashKind == "Date"; }
static long long dateDays(const Value& v) {
    auto f = [&](const char* k) { auto it = v.hash->find(k); return it != v.hash->end() ? it->second.toInt() : 0; };
    return civilToDays(f("year"), f("month"), f("day"));
}

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
    if (n.find("::") != std::string::npos) return true; // package-qualified $Foo::bar (may be undefined)
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
// default value for a typed declaration: native lowercase types (num/int/str) have
// concrete defaults; a named type (`my Int $x`) defaults to that type object.
static Value typedDefault(const std::string& type, char sigil) {
    if (sigil == '$' && !type.empty()) {
        // sized native integers wrap to their bit width on assignment
        static const struct { const char* n; int bits; bool sgn; } nts[] = {
            {"uint8",8,false},{"int8",8,true},{"byte",8,false},{"uint16",16,false},{"int16",16,true},
            {"uint32",32,false},{"int32",32,true},{"uint64",64,false},{"int64",64,true}};
        for (auto& t : nts) if (type == t.n) { Value v = Value::integer(0); v.natBits = t.bits; v.natSigned = t.sgn; return v; }
        if (type == "num" || type.rfind("num", 0) == 0) return Value::number(0);
        if (type == "int" || type.rfind("int", 0) == 0 || type.rfind("uint", 0) == 0) return Value::integer(0);
        if (type == "str") return Value::str("");
        if (std::isupper((unsigned char)type[0])) return Value::typeObj(type); // my Int $x -> (Int)
    }
    // typed containers: `my Int @a` -> Array[Int], `my Int %h` -> Hash[Int]
    if ((sigil == '@' || sigil == '%') && !type.empty() && std::isupper((unsigned char)type[0])) {
        Value v = defaultFor(sigil);
        v.ofType = type;
        return v;
    }
    return defaultFor(sigil);
}
// Truncate an integer value to a native type's bit width (wraparound), keeping the tag.
static void wrapNative(Value& v, int bits, bool sign) {
    if (bits <= 0) return;
    long long x = v.toInt();
    if (bits < 64) {
        unsigned long long mask = (1ULL << bits) - 1;
        unsigned long long u = (unsigned long long)x & mask;
        if (sign && (u & (1ULL << (bits - 1)))) x = (long long)u - (long long)(1ULL << bits);
        else x = (long long)u;
    }
    v = Value::integer(x);
    v.natBits = bits; v.natSigned = sign;
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
        case NK::SubstLit: { auto* sl = static_cast<const SubstLit*>(e); // $^a lives in the raw pattern/repl text
            auto scan = [&](const std::string& str) {
                for (size_t i = 0; i + 2 < str.size(); i++)
                    if (str[i] == '$' && str[i + 1] == '^' && std::isalpha((unsigned char)str[i + 2])) {
                        size_t j = i + 2; std::string nm = "$^";
                        while (j < str.size() && (std::isalnum((unsigned char)str[j]) || str[j] == '_')) nm += str[j++];
                        out.insert(nm);
                    }
            };
            scan(sl->pattern); scan(sl->repl); break; }
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
        if (it.t == VT::Range && it.rTo < 9000000000000000000LL) { // an infinite range stays one lazy item
            ValueList sub = it.flatten();
            v.arr->insert(v.arr->end(), sub.begin(), sub.end());
        } else if (it.t == VT::Array && it.isList && it.arr) { // a List flattens in list context
            v.arr->insert(v.arr->end(), it.arr->begin(), it.arr->end());
        } else {
            v.arr->push_back(it);
        }
    }
    return v;
}

static Value coerceArray(const Value& v) {
    if (v.t == VT::Array) { if (!v.isList) return v; Value r = v; r.isList = false; return r; } // @-container is an Array
    if (v.t == VT::Range) {
        if (v.rTo >= 9000000000000000000LL) { // …..Inf : a lazy @-array, materialised on demand
            long long start = v.rFrom + (v.rExFrom ? 1 : 0);
            Value a = Value::array(); a.isList = false;
            auto st = std::make_shared<LazySeqState>(); st->infinite = true;
            auto next = std::make_shared<long long>(start);
            st->appendNext = [next](ValueList& cache) -> bool { cache.push_back(Value::integer((*next)++)); return true; };
            a.ext = st;
            return a;
        }
        return Value::array(v.flatten());
    }
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

// Per-thread execution registers. One instance per real thread; the GIL still
// serialises who runs. See the declaration in Interpreter.h.
thread_local ExecContext Interpreter::tctx_;
// Per-thread call-stack state (step 3a — see header).
thread_local std::vector<Interpreter::RedispatchCtx> Interpreter::redispatchStack_;
thread_local std::vector<std::shared_ptr<ReactCtx>> Interpreter::reactStack_;
thread_local int Interpreter::threadDepth_ = 0;

Interpreter::Interpreter() {
    mainThread_ = std::this_thread::get_id();
    parallelMode_ = std::getenv("RAKUPP_PARALLEL") != nullptr;
    global_ = std::make_shared<Env>();
    curPkgEnv_ = global_;
    tctx_.cur = global_;
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
    // %*ENV — the process environment, as a Hash (found via the normal env chain)
    {
        Value envh = Value::makeHash();
        for (char** ep = rakupp_environ(); ep && *ep; ++ep) {
            std::string kv = *ep; auto eq = kv.find('=');
            if (eq == std::string::npos) continue;
            (*envh.hash)[kv.substr(0, eq)] = Value::str(kv.substr(eq + 1));
        }
        global_->define("%*ENV", envh);
    }
    // X::AdHoc — the exception `die "message"` produces (so $_/$! in CATCH answer .message/.^name)
    {
        auto adhoc = std::make_shared<ClassInfo>();
        adhoc->name = "X::AdHoc";
        ClassAttr a; a.name = "message"; a.sigil = '$'; a.pub = true;
        adhoc->attrs.push_back(a);
        classes_["X::AdHoc"] = adhoc;
    }
    // CompUnit::Repository — a role a repository class must fully implement, and the
    // $*REPO instance that does it.
    {
        auto repoRole = std::make_shared<ClassInfo>();
        repoRole->name = "CompUnit::Repository"; repoRole->isRole = true;
        repoRole->requiredMethods = {"id", "need", "load", "loaded"};
        classes_["CompUnit::Repository"] = repoRole;
        auto fs = std::make_shared<ClassInfo>();
        fs->name = "CompUnit::Repository::FileSystem"; fs->parent = repoRole;
        classes_["CompUnit::Repository::FileSystem"] = fs;
        auto od = std::make_shared<ObjectData>(); od->cls = fs;
        global_->define("$*REPO", Value::object(od));
    }
    // The slang language-objects ($~MAIN and friends) exist as defined Grammar
    // objects. rakupp can't mutate its own grammar through them (that's the
    // compiler-internals frontier), but they are present and introspectable.
    {
        auto slangCls = std::make_shared<ClassInfo>();
        slangCls->name = "Grammar"; slangCls->isGrammar = true;
        for (const char* nm : {"$~MAIN", "$~Quote", "$~Q", "$~Regex", "$~P5Regex"}) {
            auto od = std::make_shared<ObjectData>(); od->cls = slangCls;
            global_->define(nm, Value::object(od));
        }
    }
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

// Move the live execution registers into `c` (used when a thread parks) …
void Interpreter::saveCtx(ExecContext& c) {
    c.cur         = std::move(tctx_.cur);
    c.dynStack    = std::move(tctx_.dynStack);
    c.callDepth   = tctx_.callDepth;
    c.curStateEnv = tctx_.curStateEnv;
    c.gatherStack = std::move(tctx_.gatherStack);
    c.gatherLimits = std::move(tctx_.gatherLimits);
    c.supplyStack = std::move(tctx_.supplyStack);
    c.makeTargets = std::move(tctx_.makeTargets);
    c.pkgPrefix   = std::move(tctx_.pkgPrefix);
}
// … and back out when it resumes (or when another thread is scheduled in).
void Interpreter::loadCtx(ExecContext& c) {
    tctx_.cur          = std::move(c.cur);
    tctx_.dynStack     = std::move(c.dynStack);
    tctx_.callDepth    = c.callDepth;
    tctx_.curStateEnv  = c.curStateEnv;
    tctx_.gatherStack  = std::move(c.gatherStack);
    tctx_.gatherLimits = std::move(c.gatherLimits);
    tctx_.supplyStack  = std::move(c.supplyStack);
    tctx_.makeTargets  = std::move(c.makeTargets);
    tctx_.pkgPrefix    = std::move(c.pkgPrefix);
}

// Engage the GIL the first time any asynchronous work appears. Before this the
// program is purely single-threaded and takes no locks at all. This is also the
// symbol-table freeze point: once concurrency can begin, the shared tables are
// meant to be immutable (see noteSymbolMutation / symbolsFrozen_).
void Interpreter::engageGil() {
    if (gilHeld_) return;
    gilHeld_ = true;
    symbolsFrozen_.store(true, std::memory_order_relaxed);
    if (!parallelMode_) gil_.lock();  // parallel mode never holds the GIL for compute
}

// Tripwire wired into every structural writer of a shared symbol table. Once the
// tables are frozen (concurrency engaged), a mutation means a worker thread could
// be reading a table another thread is restructuring — the race lock-free reads
// must avoid. Off by default (no behaviour change); set RAKUPP_FREEZE_TRACE to
// have each post-freeze mutation reported to stderr with the offending thread.
void Interpreter::noteSymbolMutation(const char* what) {
    if (!symbolsFrozen_.load(std::memory_order_relaxed)) return;
    static const bool trace = std::getenv("RAKUPP_FREEZE_TRACE") != nullptr;
    if (!trace) return;
    bool onMain = std::this_thread::get_id() == mainThread_;
    fprintf(stderr, "[freeze] post-freeze symbol mutation: %s  (thread=%s, file=%s)\n",
            what, onMain ? "main" : "worker", srcFile_.c_str());
}

// Release the GIL and wake any thread parked in yieldToWorker. Every place a
// thread hands the GIL off (worker finish, sleepYield, awaitPromise, react wait)
// goes through here so the spawner's cooperative yield sees the progress.
void Interpreter::gilYieldNotify() {
    gil_.unlock();
    { std::lock_guard<std::mutex> lk(gilRelMutex_); ++gilReleaseCount_; }
    gilReleased_.notify_all();
}

// Called by the spawner right after starting a worker: drop the GIL and wait
// until *some* thread has made progress (acquired then released the GIL), then
// reacquire. A pure-compute worker runs to completion in that window (so its
// effects are visible immediately, like the old eager model); a worker that
// blocks (sleep/await) releases the GIL at its first block, and we resume then —
// leaving it running concurrently.
void Interpreter::yieldToWorker() {
    if (!gilHeld_ || parallelMode_) return;  // parallel workers already run concurrently
    static thread_local ExecContext parked;
    saveCtx(parked);
    long before;
    { std::lock_guard<std::mutex> lk(gilRelMutex_); before = gilReleaseCount_; }
    gil_.unlock();
    { std::unique_lock<std::mutex> lk(gilRelMutex_); gilReleased_.wait(lk, [&] { return gilReleaseCount_ > before; }); }
    gil_.lock();
    loadCtx(parked);
}

// sleep with the GIL released, so sibling worker threads run (and sleep) at the
// same time — this is what makes concurrent-timing programs (e.g. sleep-sort)
// actually interleave. Capped so a runaway `sleep 10000` can't wedge the suite.
void Interpreter::sleepYield(double secs) {
    if (secs <= 0) return;
    if (secs > 1.0) secs = 1.0; // cap: preserves small relative delays (sleep-sort), bounded for the harness
    // No GIL held (single-threaded) or parallel mode (no GIL at all): just sleep.
    if (!gilHeld_ || parallelMode_) { std::this_thread::sleep_for(std::chrono::duration<double>(secs)); return; }
    static thread_local ExecContext parked;
    saveCtx(parked);
    gilYieldNotify();
    std::this_thread::sleep_for(std::chrono::duration<double>(secs));
    gil_.lock();
    loadCtx(parked);
}

// Release the GIL for a blocking syscall so other worker threads run concurrently.
// Modeled on sleepYield, but the caller does the blocking (child-process wait). The
// parked window must touch NO interpreter state — only this thread's own buffers.
static thread_local ExecContext g_gilParkCtx;
bool Interpreter::gilPark() {
    if (!gilHeld_ || parallelMode_) return false;  // parallel mode: no GIL to release, waits already overlap
    saveCtx(g_gilParkCtx);
    gilYieldNotify();
    return true;
}
void Interpreter::gilUnpark(bool wasParked) {
    if (!wasParked) return;
    gil_.lock();
    loadCtx(g_gilParkCtx);
}

// True only on `start`/async worker threads — gates the safe-point abort (defined
// inline in the header) so the main thread is never unwound.
thread_local bool t_isWorker = false;
thread_local unsigned t_safePtCtr = 0;

// Called from a worker's safe point every few thousand loop iterations: release the
// GIL (waking a main thread parked in yieldToWorker), give the scheduler a chance to
// hand the mutex over, then reacquire — mirroring sleepYield's save/park/restore.
void Interpreter::workerYield() {
    if (!gilHeld_ || parallelMode_) return;
    static thread_local ExecContext parked;
    saveCtx(parked);
    gilYieldNotify();            // unlock GIL + bump release counter + notify
    std::this_thread::yield();   // let the woken thread actually take the mutex
    gil_.lock();
    loadCtx(parked);
}

// Spawn a real worker thread that runs `code` and keeps/breaks a Promise backed
// by a PromiseState. The worker blocks on the GIL until the main thread yields
// (inside awaitPromise) or the program drains, so nothing runs truly in parallel.
Value Interpreter::spawnPromise(Value code) {
    engageGil();
    auto ps = std::make_shared<PromiseState>();
    Value p = Value::makeHash(); p.hashKind = "Promise";
    (*p.hash)["status"] = Value::str("Planned");
    p.ext = ps;
    Interpreter* self = this;
    if (parallelMode_) {
        // True-parallel worker: runs interpreter compute concurrently with the main
        // thread and its siblings — no GIL. Its registers/stacks are thread_local
        // (fresh & empty on this new thread, steps 1/3a); the symbol tables are frozen
        // (step 2); the promise settle and the workers_ push are mutex-guarded.
        std::lock_guard<std::mutex> wlk(sharedMut_);
        liveWorkers_++;
        workers_.emplace_back([self, code, ps]() mutable {
            t_isWorker = true;
            Value r; bool broke = false; Value cause;
            try {
                ValueList noargs;
                r = code.t == VT::Code ? self->callCallable(code, noargs) : code;
            }
            catch (const RakuError& e) { broke = true; cause = e.payload; ps->causeMsg = e.message; }
            catch (...) { broke = true; }
            {
                std::lock_guard<std::mutex> lk(ps->m);
                ps->result = r; ps->cause = cause; ps->broken = broke; ps->done = true;
            }
            ps->cv.notify_all();
            self->liveWorkers_--;
        });
        return p;
    }
    liveWorkers_++;
    workers_.emplace_back([self, code, ps]() mutable {
        t_isWorker = true;
        self->gil_.lock();                 // acquire the GIL (main must have yielded)
        ExecContext wctx;                  // fresh, empty registers for this worker
        self->loadCtx(wctx);
        Value r; bool broke = false; Value cause;
        try {
            ValueList noargs;
            r = code.t == VT::Code ? self->callCallable(code, noargs) : code;
        }
        catch (const RakuError& e) { broke = true; cause = e.payload; ps->causeMsg = e.message; }
        catch (...) { broke = true; }
        self->saveCtx(wctx);               // pull worker registers back out
        {
            std::lock_guard<std::mutex> lk(ps->m);
            ps->result = r; ps->cause = cause; ps->broken = broke; ps->done = true;
        }
        self->liveWorkers_--;
        self->gilYieldNotify();            // release the GIL (waking any cooperative yielder)
        ps->cv.notify_all();
    });
    return p;
}

// Block the caller until `ps` completes, dropping the GIL while parked so a
// worker can run, and handing over the live execution registers to it.
void Interpreter::awaitPromise(const std::shared_ptr<PromiseState>& ps) {
    if (!ps) return;
    std::unique_lock<std::mutex> plk(ps->m);
    if (ps->done) return;                  // already settled — no need to yield
    if (!gilHeld_) return;                 // no async workers exist; nothing could
                                           // ever settle it — don't deadlock/UB, just return
    if (parallelMode_) {                   // no GIL: just wait for the worker to settle it
        ps->cv.wait(plk, [&] { return ps->done; });
        return;
    }
    static thread_local ExecContext parked; // per-thread stash (supports nested await)
    saveCtx(parked);
    gilYieldNotify();
    ps->cv.wait(plk, [&] { return ps->done; });
    gil_.lock();
    loadCtx(parked);
}

// The `react` event loop: block until every live source has signalled done (or
// `done`/`last` closed the block). Same-thread emits already ran synchronously,
// so when no async emitter engaged the GIL there is nothing to wait for and we
// return at once. Otherwise drop the GIL and wait for an emitter thread to
// decrement liveSources / close the context.
void Interpreter::runReactLoop(const std::shared_ptr<ReactCtx>& ctx) {
    std::unique_lock<std::mutex> lk(ctx->m);
    while (ctx->liveSources > 0 && !ctx->closed) {
        if (!gilHeld_) break; // no async emitter can exist → don't hang the loop
        if (parallelMode_) {  // no GIL: wait for an emitter thread to close/drain
            ctx->cv.wait(lk, [&] { return ctx->liveSources <= 0 || ctx->closed; });
            break;
        }
        static thread_local ExecContext parked;
        saveCtx(parked);
        gilYieldNotify();
        ctx->cv.wait(lk, [&] { return ctx->liveSources <= 0 || ctx->closed; });
        gil_.lock();
        loadCtx(parked);
    }
}

// Let every outstanding worker finish before the interpreter goes away. Workers
// may spawn further workers, so loop until the queue is empty.
void Interpreter::drainWorkers() {
    if (!gilHeld_) return;
    if (parallelMode_) {
        // Workers already run freely; just join them. They may spawn more (guarded by
        // sharedMut_), so loop until the queue drains.
        for (;;) {
            std::vector<std::thread> batch;
            { std::lock_guard<std::mutex> lk(sharedMut_); batch.swap(workers_); }
            if (batch.empty()) break;
            for (auto& t : batch) if (t.joinable()) t.join();
        }
        gilHeld_ = false;
        return;
    }
    // Ask workers to unwind: a compute-bound worker (no I/O to yield at) hits the
    // safe point in its next loop iteration and throws WorkerAbortEx, releasing the
    // GIL and finishing. Workers blocked in a syscall (a server's accept/recv) can't
    // see this — they're handled by the grace-period abandon below.
    workerAbort_.store(true, std::memory_order_relaxed);
    gil_.unlock();                         // let workers run while we wait
    // Wait for outstanding workers to finish, but not forever: a fire-and-forget
    // `start {…}` that loops (a server's accept loop) is a daemon thread. Once the
    // mainline is done we give such workers a short grace period, then abandon them
    // so the program can exit — matching Rakudo's thread-pool (daemon) semantics.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (liveWorkers_.load() > 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (liveWorkers_.load() > 0) {
        for (auto& t : workers_) t.detach();   // daemon: don't wait on it
        workers_.clear();
        abandonedWorkers_ = true;
    } else {
        for (auto& t : workers_) if (t.joinable()) t.join(); // reap the finished threads
        workers_.clear();
    }
    gilHeld_ = false;
}

int Interpreter::run(Program& prog) {
    int code = 0;
    bool crashed = false;
    tctx_.curStateEnv = global_.get(); // mainline `state` vars persist here (e.g. across a top-level loop)
    {
        Value args = Value::array();
        for (auto& s : argv_) args.arr->push_back(Value::str(s));
        tctx_.cur->define("@*ARGS", args);
    }
    // Partition top-level phasers (BEGIN/CHECK/INIT run before mainline; END after).
    // LEAVE/KEEP/UNDO of the compilation unit run when the mainline exits, so they
    // are deferred here too rather than executed at their textual position.
    std::vector<Block*> beginP, checkP, initP, endP, leaveP, enterP;
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
            if (b->phaser == "ENTER") { enterP.push_back(b); continue; } // file scope: before the mainline body
            if (b->phaser == "LEAVE" || b->phaser == "KEEP" || b->phaser == "UNDO")
                                      { leaveP.push_back(b); continue; }
        }
        mainline.push_back(s.get());
    }
    auto runPhaser = [&](Block* b) { auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur; execBlock(b, sc); };
    // END phasers run in REVERSE source order, on any exit path.
    auto runEnds = [&]() {
        for (auto it = endP.rbegin(); it != endP.rend(); ++it) {
            try { runPhaser(*it); }
            catch (ExitEx& e) { code = e.code; }  // `exit` in an END block sets the exit status
            catch (...) {}
        }
        // END blocks registered from EVAL'd code, in reverse registration order
        for (auto it = deferredEnds_.rbegin(); it != deferredEnds_.rend(); ++it) {
            auto sc = std::make_shared<Env>(); sc->parent = it->second;
            try { execBlock(it->first, sc); }
            catch (ExitEx& e) { code = e.code; }
            catch (...) {}
        }
    };
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
        auto predeclare = [this](Expr* e) { // a declare-VarExpr, or a list declaration `my ($a, $b)`
            auto one = [this](Expr* x) {
                if (x && x->kind == NK::VarExpr && static_cast<VarExpr*>(x)->declare) {
                    auto* ve = static_cast<VarExpr*>(x);
                    if (!ve->name.empty() && !global_->vars.count(ve->name))
                        global_->define(ve->name, typedDefault(ve->declType, ve->name[0]));
                }
            };
            if (!e) return;
            if (e->kind == NK::ListExpr) { for (auto& it : static_cast<ListExpr*>(e)->items) one(it.get()); }
            else one(e);
        };
        for (auto* s : mainline) {
            std::string nm = topDecl(s, hasInit);
            if (s->kind == NK::ExprStmt) { Expr* e = static_cast<ExprStmt*>(s)->e.get();
                if (e && e->kind == NK::Assign) predeclare(static_cast<Assign*>(e)->target.get());
                else predeclare(e); }
            if (nm.empty() || global_->vars.count(nm)) continue;
            std::string dtype; // honor the declared type so `my num $n` pre-declares 0, not Any
            if (s->kind == NK::ExprStmt) { Expr* e = static_cast<ExprStmt*>(s)->e.get();
                if (e && e->kind == NK::VarExpr) dtype = static_cast<VarExpr*>(e)->declType;
                else if (e && e->kind == NK::Assign) { auto* a = static_cast<Assign*>(e); if (a->target && a->target->kind == NK::VarExpr) dtype = static_cast<VarExpr*>(a->target.get())->declType; } }
            global_->define(nm, typedDefault(dtype, nm[0])); }
        for (auto* b : beginP) runPhaser(b);                                      // BEGIN: source order
        for (auto it = checkP.rbegin(); it != checkP.rend(); ++it) runPhaser(*it); // CHECK: reverse
        for (auto* b : initP) runPhaser(b);                                       // INIT: source order
        for (auto* b : enterP) runPhaser(b);                                      // ENTER: on UNIT-block entry, before the mainline
        for (auto* s : mainline) {
            if (s->kind == NK::SubDecl && !static_cast<SubDecl*>(s)->name.empty() &&
                !static_cast<SubDecl*>(s)->isMethod) continue; // hoisted
            // a bare `my $x;` (no init) must not clobber a value a phaser already set
            std::string nm = topDecl(s, hasInit);
            if (!nm.empty() && !hasInit && global_->vars.count(nm)) continue;
            exec(s);
        }
        // auto-invoke MAIN with command-line arguments, if defined
        if (Value* mainSub = tctx_.cur->find("&MAIN")) {
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
        if (docMode_) std::cout << podData_; // --doc: print the rendered POD after the program runs
    } catch (ExitEx& e) {
        code = e.code;
    } catch (RakuError& e) {
        if (topCatch) { // mainline CATCH: bind $_/$! to the exception and run its when/default
            tctx_.cur->define("$_", exceptionFor(e));
            tctx_.cur->define("$!", exceptionFor(e));
            try { for (auto& s : topCatch->stmts) exec(s.get()); }
            catch (BreakGivenEx&) {} catch (ExitEx& ex) { code = ex.code; } catch (...) {}
        } else {
            // RAKU_EXCEPTIONS_HANDLER=JSON serializes an uncaught exception as JSON
            if (envStr("RAKU_EXCEPTIONS_HANDLER") == "JSON" && e.payload.t == VT::Object && e.payload.obj)
                std::cerr << exceptionToJson(e.payload);
            else std::cerr << e.message << "\n";
            code = 1;
            crashed = true;
        }
    } catch (ReturnEx&) {
    } catch (LastEx&) { // `last` outside any loop is a compile/run error, like Rakudo's
        std::cerr << "last without loop construct\n"; code = 1; crashed = true;
    } catch (NextEx&) {
        std::cerr << "next without loop construct\n"; code = 1; crashed = true;
    } catch (RedoEx&) {
        std::cerr << "redo without loop construct\n"; code = 1; crashed = true;
    }
    drainWorkers(); // join any outstanding async workers before we tear down
    // Compilation-unit LEAVE/KEEP/UNDO phasers run (reverse source order) on the
    // way out — after the mainline, before END.
    for (auto it = leaveP.rbegin(); it != leaveP.rend(); ++it) {
        try { runPhaser(*it); } catch (ExitEx& e) { code = e.code; } catch (...) {}
    }
    runEnds(); // END phasers (reverse source order), after the mainline
    // Emit a trailing plan only on normal completion — never fabricate `1..0`
    // after an uncaught error (that would look like a passing empty/skip-all plan).
    if (usedTest_ && planned_ < 0 && !crashed && !bailedOut_) {
        std::cout << "1.." << testNum_ << "\n";
    }
    // Rakudo's end-of-run summary when some tests failed.
    if (usedTest_ && failCount_ > 0 && !crashed && !bailedOut_) {
        std::cerr << "# Looks like you failed " << failCount_ << " test" << (failCount_ == 1 ? "" : "s")
                  << " of " << testNum_ << "\n";
    }
    // Rakudo test exit status: 255 ("dubious") if the ran count != the plan,
    // else the number of failed tests (capped at 254).
    if (usedTest_ && code == 0 && !crashed && !bailedOut_) {
        if (planned_ >= 0 && testNum_ != planned_) code = 255;
        else if (failCount_ > 0) code = failCount_ > 254 ? 254 : (int)failCount_;
    }
    else if (failCount_ > 0 && code == 0) code = 1;
    // A daemon `start {…}` (e.g. a server accept loop) was left running. We've
    // emitted all output; flush and hard-exit so the detached thread can't wedge
    // teardown (and isn't waited on), matching Rakudo abandoning thread-pool work.
    if (abandonedWorkers_) { std::cout.flush(); std::cerr.flush(); std::_Exit(code); }
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
    noteSymbolMutation("module load (use/need)");
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
        { std::unique_lock<std::mutex> kl(sharedMut_, std::defer_lock); if (parallelMode_) kl.lock(); keptPrograms_.push_back(prog); }
        auto saved = tctx_.cur;
        std::string savedFinish = finishData_;
        finishData_ = finish; // this module's $=finish data block
        // Load the module into its OWN scope (chained to global). Its subs see one
        // another there; then everything is republished to global EXCEPT a
        // non-`is export` sub whose name collides with a built-in — that one stays
        // module-private so it can't shadow the built-in for the importer. This is
        // what lets Test::Util's `our sub run` coexist with the built-in `run`:
        // Test::Util's own `is_run` still resolves `run` (via its module closure),
        // while an importer's bare `run(...)` reaches the built-in.
        auto moduleEnv = std::make_shared<Env>(); moduleEnv->parent = global_;
        std::set<std::string> exported;
        for (auto& st : prog->stmts)
            if (st->kind == NK::SubDecl) {
                auto* sd = static_cast<SubDecl*>(st.get());
                if (sd->isExport && !sd->name.empty()) exported.insert(sd->name);
            }
        tctx_.cur = moduleEnv;
        auto savedPkg = curPkgEnv_; curPkgEnv_ = moduleEnv; // `our sub` in the module installs here, not main's global
        auto publish = [&] {
            for (auto& kv : moduleEnv->vars) {
                const std::string& k = kv.first;
                if (k.size() > 1 && k[0] == '&') {
                    std::string bare = k.substr(1);
                    if (!exported.count(bare) && builtins_.count(bare)) continue; // withhold shadower
                }
                global_->define(k, kv.second);
            }
        };
        // A runtime failure in a module's load-time code (often a deep dependency
        // using an unimplemented primitive, e.g. Lock) is non-fatal: warn and keep
        // going so the importing program can still run paths that don't need it —
        // and still publish whatever the module managed to define before dying.
        try {
            hoistSubs(prog->stmts);
            for (auto& st : prog->stmts) {
                if (st->kind == NK::SubDecl && !static_cast<SubDecl*>(st.get())->name.empty() &&
                    !static_cast<SubDecl*>(st.get())->isMethod) continue; // hoisted
                exec(st.get());
            }
        }
        catch (RakuError& e) {
            publish();
            tctx_.cur = saved; curPkgEnv_ = savedPkg; finishData_ = savedFinish;
            std::cerr << "===WARNING=== Module " << name << " failed during load: " << e.message << "\n";
            return;
        }
        catch (...) { tctx_.cur = saved; curPkgEnv_ = savedPkg; finishData_ = savedFinish; throw; }
        publish();
        tctx_.cur = saved; curPkgEnv_ = savedPkg; finishData_ = savedFinish;
    };

    std::string rel = name;
    for (size_t p = rel.find("::"); p != std::string::npos; p = rel.find("::")) rel.replace(p, 2, "/");
    // 1. local lib paths (project lib, ., rakupp rakulib). A `use lib` may point at a
    // distribution root rather than its `lib/` dir (e.g. Roast's `use lib
    // $*PROGRAM.parent(2).add("packages/Test-Helpers")`), so try `<base>/lib/` too —
    // this is the common case Rakudo resolves via META6.json.
    static const char* exts[] = {".rakumod", ".pm6", ".raku", ".pm"};
    for (auto& base : libPaths_) {
        for (const std::string& dir : {base, base + "/lib"}) {
            for (auto ext : exts) {
                std::ifstream in(dir + "/" + rel + ext);
                if (!in) continue;
                std::ostringstream ss; ss << in.rdbuf();
                loadSource(ss.str());
                return;
            }
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
        "MONKEY", "MONKEY-GUTS", "Test", "v6", "v6.c", "v6.d", "v6.e",
        "NativeCall",  // its `is native` FFI is handled natively by the compiler
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
        // seed user-defined operators (sub infix:<…>) so EVAL'd custom operators parse
        for (Env* e = tctx_.cur.get(); e; e = e->parent.get())
            for (auto& kv : e->vars) {
                const std::string& n = kv.first; // e.g. "&infix:<fromplus>"
                size_t lt = n.find(":<");
                if (n.size() > 1 && n[0] == '&' && lt != std::string::npos && n.back() == '>') {
                    std::string kind = n.substr(1, lt - 1);
                    if (kind == "infix" || kind == "prefix" || kind == "postfix")
                        parser.declareUserOp(kind, n.substr(lt + 2, n.size() - lt - 3));
                }
            }
        *prog = parser.parseProgram();
    } catch (ParseError& e) {
        throw RakuError{Value::str(e.what()), std::string("EVAL parse error: ") + e.what()};
    }
    { std::unique_lock<std::mutex> kl(sharedMut_, std::defer_lock); if (parallelMode_) kl.lock(); keptPrograms_.push_back(prog); } // keep AST alive for closures defined within
    Value last = Value::any();
    for (auto& s : prog->stmts) {
        // An END block in EVAL'd code runs at the END of the whole program (not here),
        // capturing the EVAL scope so it still sees this EVAL's lexicals.
        if (s->kind == NK::Block && static_cast<Block*>(s.get())->phaser == "END") {
            deferredEnds_.push_back({static_cast<Block*>(s.get()), tctx_.cur});
            continue;
        }
        last = exec(s.get());
    }
    return last;
}

// If RAKU_TEST_DIE_ON_FAIL is a true value, a real (non-TODO) failure stops the
// whole suite: emit the Rakudo diagnostics and exit 255.
void Interpreter::maybeDieOnFail() {
    if (dieOnFail_ < 0) dieOnFail_ = envFlag("RAKU_TEST_DIE_ON_FAIL") ? 1 : 0;
    if (!dieOnFail_ || todoSubtestDepth_ > 0) return;
    std::cerr << "Stopping test suite, because value of RAKU_TEST_DIE_ON_FAIL "
                 "environment variable is set to a true value.\n";
    if (planned_ >= 0)
        std::cerr << "# You planned " << planned_ << " tests, but ran " << testNum_ << ".\n";
    std::cerr << "# Looks like you failed " << failCount_ << " test" << (failCount_ == 1 ? "" : "s")
              << " of " << testNum_ << "\n";
    bailedOut_ = true; // suppress the trailing auto-plan
    throw ExitEx{255};
}

void Interpreter::emitTest(bool ok, const std::string& desc, const std::string& directive_,
                           const std::string& extraDiag) {
    // In parallel mode a worker may emit test results concurrently with the main
    // thread; serialise the counters + TAP line so nothing interleaves or races.
    std::unique_lock<std::mutex> plk(sharedMut_, std::defer_lock);
    if (parallelMode_) plk.lock();
    std::string directive = directive_;
    // A bare `todo REASON, COUNT` statement marks the next COUNT tests TODO.
    if (directive.empty() && todoRemaining_ > 0) {
        directive = "TODO" + (todoReason_.empty() ? "" : " " + todoReason_);
        todoRemaining_--;
    }
    // Rakudo's proclaim always emits " - <description>" (description defaults to the
    // empty string) for ok/is/etc.; only skip() suppresses it (it emits "# SKIP …").
    bool isSkip = directive.rfind("SKIP", 0) == 0;
    bool realFail = !ok && directive.empty(); // a genuine failure (not TODO/SKIP)
    if (subtestDepth_ > 0) {
        if (realFail) subtestFailed_ = true;
        std::cout << "    " << (ok ? "ok" : "not ok");
        if (!isSkip) std::cout << " - " << desc;
        if (!directive.empty()) std::cout << " # " << directive;
        std::cout << "\n";
        if (realFail) {
            std::cerr << "# Failed test" << (desc.empty() ? "" : " '" + desc + "'")
                      << " at " << srcFile_ << " line " << curLine_ << "\n";
            if (!extraDiag.empty()) std::cerr << extraDiag;
            maybeDieOnFail();
        }
        return;
    }
    testNum_++;
    std::ostringstream os;
    os << (ok ? "ok " : "not ok ") << testNum_;
    if (!isSkip) os << " - " << desc;
    if (!directive.empty()) os << " # " << directive;
    std::cout << os.str() << "\n";
    if (realFail) {
        failCount_++;
        std::cerr << "# Failed test" << (desc.empty() ? "" : " '" + desc + "'")
                  << " at " << srcFile_ << " line " << curLine_ << "\n";
        if (!extraDiag.empty()) std::cerr << extraDiag;
        maybeDieOnFail();
    }
}

// ----------------- statements -----------------
// A block-scoped phaser we run at entry/exit rather than in-place.
static bool isBlockPhaser(Stmt* s) {
    if (s->kind != NK::Block) return false;
    const std::string& p = static_cast<Block*>(s)->phaser;
    return p == "ENTER" || p == "LEAVE" || p == "KEEP" || p == "UNDO" || p == "FIRST" ||
           p == "NEXT" || p == "LAST";
}
void Interpreter::runNextPhasers(const std::vector<StmtPtr>& stmts, std::shared_ptr<Env>& scope) {
    for (auto& s : stmts) if (s->kind == NK::Block) { auto* b = static_cast<Block*>(s.get());
        if (b->phaser == "NEXT") { auto sc = std::make_shared<Env>(); sc->parent = scope; execBlock(b, sc); } }
}
void Interpreter::runEnterPhasers(const std::vector<StmtPtr>& stmts) {
    for (auto& s : stmts) if (s->kind == NK::Block) { auto* b = static_cast<Block*>(s.get());
        // ENTER fires on every block entry; FIRST fires once — in a loop body the loop
        // drives FIRST (suppressLoopFirst_), elsewhere FIRST behaves like a one-shot ENTER.
        if (b->phaser == "ENTER" || (b->phaser == "FIRST" && !suppressLoopFirst_)) {
            auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur; execBlock(b, sc); } }
}
void Interpreter::runFirstPhasers(const std::vector<StmtPtr>& stmts) {
    for (auto& s : stmts) if (s->kind == NK::Block) { auto* b = static_cast<Block*>(s.get());
        if (b->phaser == "FIRST") { auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur; execBlock(b, sc); } }
}
void Interpreter::runLastPhasers(const std::vector<StmtPtr>& stmts) {
    for (auto& s : stmts) if (s->kind == NK::Block) { auto* b = static_cast<Block*>(s.get());
        if (b->phaser == "LAST") { auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur; execBlock(b, sc); } }
}
void Interpreter::runLeavePhasers(const std::vector<StmtPtr>& stmts) {
    // reverse source order
    std::vector<Block*> leaves;
    for (auto& s : stmts) if (s->kind == NK::Block) { auto* b = static_cast<Block*>(s.get());
        if (b->phaser == "LEAVE" || b->phaser == "KEEP" || b->phaser == "UNDO") leaves.push_back(b); }
    for (auto it = leaves.rbegin(); it != leaves.rend(); ++it) { auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur; try { execBlock(*it, sc); } catch (...) {} }
    // `temp`-saved containers are restored on scope exit (reverse order), after LEAVE blocks.
    if (tctx_.cur && !tctx_.cur->tempRestores.empty()) {
        for (auto it = tctx_.cur->tempRestores.rbegin(); it != tctx_.cur->tempRestores.rend(); ++it) (*it)();
        tctx_.cur->tempRestores.clear();
    }
}

Value Interpreter::execBlock(Block* b, std::shared_ptr<Env> scope, bool sink) {
    auto saved = tctx_.cur;
    tctx_.cur = std::move(scope);
    Value last = Value::any();
    // index of the last statement whose value becomes the block's value; earlier
    // statements are always sink (their value is discarded either way).
    size_t lastIdx = b->stmts.size();
    for (size_t i = b->stmts.size(); i-- > 0; ) {
        Stmt* s = b->stmts[i].get();
        if (s->kind == NK::Block && static_cast<Block*>(s)->isCatch) continue;
        if (isBlockPhaser(s)) continue;
        if (s->kind == NK::SubDecl && !static_cast<SubDecl*>(s)->name.empty() &&
            !static_cast<SubDecl*>(s)->isMethod) continue;
        lastIdx = i; break;
    }
    // a CATCH {} anywhere in the block handles exceptions from the whole block
    Block* catchBlk = nullptr;
    for (auto& s : b->stmts)
        if (s->kind == NK::Block && static_cast<Block*>(s.get())->isCatch) catchBlk = static_cast<Block*>(s.get());
    hoistSubs(b->stmts);
    runEnterPhasers(b->stmts);
    // Run the block's CATCH handler; returns true if `.resume` was called (so the
    // block should carry on after the throwing statement).
    auto runCatch = [&](RakuError& e) -> bool {
        tctx_.cur->define("$_", exceptionFor(e));
        tctx_.cur->define("$!", exceptionFor(e));
        try { for (auto& s : catchBlk->stmts) exec(s.get()); }
        catch (BreakGivenEx&) { /* when/default matched */ }
        catch (ResumeEx&) { return true; }
        return false;
    };
    try {
        for (size_t i = 0; i < b->stmts.size(); i++) {
            auto& s = b->stmts[i];
            if (s->kind == NK::Block && static_cast<Block*>(s.get())->isCatch) continue;
            if (isBlockPhaser(s.get())) continue; // ENTER/LEAVE handled at entry/exit
            if (s->kind == NK::SubDecl && !static_cast<SubDecl*>(s.get())->name.empty() &&
                !static_cast<SubDecl*>(s.get())->isMethod) continue; // hoisted
            // sink every statement whose value is discarded: all but the block's
            // final statement, and even that one when the whole block is sink.
            if (catchBlk) {
                // per-statement, so `.resume` can continue at the next statement
                try { last = exec(s.get(), sink || i != lastIdx); }
                catch (RakuError& e) {
                    if (runCatch(e)) continue;           // .resume → next statement
                    runLeavePhasers(b->stmts); tctx_.cur = saved; return Value::nil(); // handled
                }
            } else {
                last = exec(s.get(), sink || i != lastIdx);
            }
        }
    } catch (RakuError& e) {
        runLeavePhasers(b->stmts);
        tctx_.cur = saved;
        throw;
    } catch (...) {
        runLeavePhasers(b->stmts);
        tctx_.cur = saved;
        throw;
    }
    runLeavePhasers(b->stmts);
    tctx_.cur = saved;
    return last;
}

bool Interpreter::runLoopBody(Block* body, std::shared_ptr<Env> scope, const std::string& label,
                             bool isFirst, bool isLast, ValueList* collect) {
    safePoint(); // once per iteration: lets a shutting-down worker unwind out of a tight loop
    // FIRST/LAST run in the loop-body scope so the loop variable ($_) is visible.
    auto inScope = [&](void (Interpreter::*ph)(const std::vector<StmtPtr>&)) {
        auto saved = tctx_.cur; tctx_.cur = scope; try { (this->*ph)(body->stmts); } catch (...) { tctx_.cur = saved; throw; } tctx_.cur = saved;
    };
    if (isFirst) { // FIRST {…}: once, before the first iteration; `last` in it breaks the loop
        try { inScope(&Interpreter::runFirstPhasers); }
        catch (LastEx& e) { if (!e.label.empty() && e.label != label) throw; return false; }
    }
    bool savedSF = suppressLoopFirst_; suppressLoopFirst_ = true; // execBlock must not re-run FIRST
    auto runLast = [&]() { if (isLast) inScope(&Interpreter::runLastPhasers); }; // LAST {…}: once, after the last
    for (;;) {
        try { Value v = execBlock(body, scope, /*sink=*/collect == nullptr); if (collect) collect->push_back(v); runNextPhasers(body->stmts, scope); runLast(); suppressLoopFirst_ = savedSF; return true; }
        catch (RedoEx& e) { if (!e.label.empty() && e.label != label) { suppressLoopFirst_ = savedSF; throw; } continue; }
        catch (NextEx& e) { if (!e.label.empty() && e.label != label) { suppressLoopFirst_ = savedSF; throw; } runNextPhasers(body->stmts, scope); runLast(); suppressLoopFirst_ = savedSF; return true; }
        catch (BreakGivenEx&) { suppressLoopFirst_ = savedSF; return true; }
        catch (LastEx& e) { if (!e.label.empty() && e.label != label) { suppressLoopFirst_ = savedSF; throw; } runLast(); suppressLoopFirst_ = savedSF; return false; }
        catch (...) { suppressLoopFirst_ = savedSF; throw; }
    }
}

// Is `n` a built-in type name that a class may legitimately derive from?
// (Used to decide whether `class X is Y` with an unregistered Y is an error.)
static bool isKnownTypeName(const std::string& n) {
    if (n.empty()) return false;
    if (n.rfind("X::", 0) == 0) return true;         // exception types
    if (n.rfind("Metamodel::", 0) == 0) return true; // HOWs
    if (n.rfind("IO::", 0) == 0) return true;         // IO::Path::Unix, etc.
    static const std::set<std::string> t = {
        "Mu", "Any", "Cool", "Junction", "Whatever", "WhateverCode", "Nil",
        "Int", "UInt", "Num", "Rat", "FatRat", "Complex", "Numeric", "Real", "Bool",
        "Str", "Stringy", "Uni", "Blob", "Buf", "Stringy",
        "Array", "List", "Seq", "Slip", "Range", "Positional", "Iterable", "Iterator",
        "Hash", "Map", "Associative", "Pair", "Enum", "Bag", "Set", "Mix",
        "BagHash", "SetHash", "MixHash", "Baggy", "Setty", "Mixy", "QuantHash",
        "Code", "Sub", "Method", "Submethod", "Routine", "Block", "Callable",
        "Regex", "Match", "Capture", "Signature", "Parameter",
        "Exception", "Failure", "Backtrace", "Grammar", "Cursor",
        "Attribute", "Scalar", "Proxy", "Version", "Order", "Enumeration",
        "Date", "DateTime", "Instant", "Duration", "IO", "Proc", "Thread",
        "Promise", "Channel", "Supply", "Lock", "Semaphore", "Stash", "Compiler",
        "Distribution", "CompUnit", "Label", "Nd",
    };
    return t.count(n) > 0;
}

Value Interpreter::exec(Stmt* s, bool sink) {
    if (s->line > 0) curLine_ = s->line; // track for test-failure diagnostics
    switch (s->kind) {
        case NK::ExprStmt: {
            Expr* e = static_cast<ExprStmt*>(s)->e.get();
            if (e->line > 0) curLine_ = e->line; // ExprStmt itself carries no line; use its expression's
            // sink context: an assignment's result is discarded, so don't copy it
            if (sink && e->kind == NK::Assign) return evalAssign(static_cast<Assign*>(e), true);
            Value r = eval(e);
            // Rakudo sink semantics: a Proc from a bare `run`/`shell` statement that
            // failed and is discarded (never stored or inspected) throws when sunk.
            if (e->kind == NK::Call && r.t == VT::Hash && r.hashKind == "Proc") {
                const std::string& nm = static_cast<Call*>(e)->name;
                if (nm == "run" || nm == "shell") {
                    long long ec = r.hash->count("exitcode") ? (*r.hash)["exitcode"].toInt() : 0;
                    if (ec != 0) {
                        std::string cmd;
                        auto it = r.hash->find("argv");
                        if (it != r.hash->end() && it->second.arr && !it->second.arr->empty()) cmd = (*it->second.arr)[0].toStr();
                        throw RakuError{Value::typeObj("X::Proc::Unsuccessful"),
                            "The spawned command '" + cmd + "' exited unsuccessfully (exit code: " +
                            std::to_string(ec) + ", signal: 0)"};
                    }
                }
            }
            return r;
        }
        case NK::EmptyStmt: return Value::any();
        case NK::NamedRegexDecl: {
            auto* nr = static_cast<NamedRegexDecl*>(s);
            noteSymbolMutation("named-regex declaration");
            namedRegex_[nr->name] = nr->pattern;    // <NAME> resolvable from a plain /…/ regex
            namedRegexKind_[nr->name] = nr->kind;
            return Value::any();
        }
        case NK::UseStmt: {
            auto* u = static_cast<UseStmt*>(s);
            if (u->module == "Test") usedTest_ = true;
            else if (u->module.size() >= 2 && u->module[0] == 'v' && std::isdigit((unsigned char)u->module[1])) {
                // language version pragma: use v6.c / v6.d / v6.e[.PREVIEW]
                if (u->module.find("6.c") != std::string::npos) langRev_ = 0;
                else if (u->module.find("6.d") != std::string::npos) langRev_ = 1;
                else langRev_ = 2; // v6 / v6.e / future -> latest semantics
            }
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
            scope->parent = tctx_.cur;
            return execBlock(b, scope);
        }
        case NK::SubDecl: {
            auto* sd = static_cast<SubDecl*>(s);
            auto makeCand = [&](const std::vector<Param>* prms) {
                Value c; c.t = VT::Code; c.code = std::make_shared<Callable>();
                c.code->name = sd->name;
                c.code->params = prms;
                c.code->body = &sd->body;
                c.code->closure = tctx_.cur;
                c.code->retType = sd->retType;
                if (sd->isNative) { c.code->isNative = true; c.code->nativeLib = sd->nativeLib;
                                    c.code->nativeSym = sd->nativeSym.empty() ? sd->name : sd->nativeSym; }
                if (prms->empty()) c.code->placeholders = computePlaceholders(sd->body);
                return c;
            };
            Value code = makeCand(&sd->params);
            std::vector<Value> altCands;
            for (auto& ap : sd->altParams) altCands.push_back(makeCand(&ap));
            // `(sig1) | (sig2)` is ONE routine with alternative signatures — its
            // candidates share a single `state`-variable store.
            if (!sd->altParams.empty()) {
                auto shared = std::make_shared<Env>(); shared->parent = tctx_.cur;
                auto share = [&](Value& c) { std::call_once(c.code->stateInit, [&] { c.code->stateEnv = shared; }); };
                share(code);
                for (auto& c : altCands) share(c);
            }
            if (!sd->name.empty()) {
                if (sd->isMulti || !sd->altParams.empty()) {
                    std::string key = "&" + sd->name;
                    Value* existing = tctx_.cur->find(key);
                    Callable* disp;
                    Value dispVal;
                    if (existing && existing->t == VT::Code && existing->code && existing->code->isMultiDispatcher) {
                        disp = existing->code.get(); dispVal = *existing;
                    } else {
                        dispVal.t = VT::Code; dispVal.code = std::make_shared<Callable>();
                        dispVal.code->name = sd->name;
                        dispVal.code->isMultiDispatcher = true;
                        disp = dispVal.code.get();
                        tctx_.cur->define(key, dispVal);
                    }
                    disp->candidates.push_back(code);
                    for (auto& c : altCands) disp->candidates.push_back(c);
                    return dispVal;
                }
                tctx_.cur->define("&" + sd->name, code);
                // `our sub` is package-scoped: also install globally so a sibling block
                // (or an `our &name;` re-declaration) can reach it.
                if (sd->isOur && curPkgEnv_ && curPkgEnv_ != tctx_.cur) curPkgEnv_->define("&" + sd->name, code);
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
                ev.enumType = ed->name; // carry the enum's type identity (for .^name, ~~, .WHAT)
                tctx_.cur->define(key, ev);
                if (!ed->name.empty()) tctx_.cur->define(ed->name + "::" + key, ev);
                pairs.arr->push_back(Value::pair(key, val));
            }
            pairs.enumType = ed->name; // the type object itself is the tagged pair-list
            if (!ed->name.empty()) tctx_.cur->define(ed->name, pairs);
            return Value::any();
        }
        case NK::ClassDecl: {
            auto* cd = static_cast<ClassDecl*>(s);
            if (cd->isAugment) {
                // Reopen an existing type and add methods. Build each method into a
                // Callable Value (same shape as the main registration path).
                auto buildMethod = [&](SubDecl* md) {
                    Value code; code.t = VT::Code;
                    code.code = std::make_shared<Callable>();
                    code.code->name = md->name;
                    code.code->params = &md->params;
                    code.code->retType = md->retType;
                    code.code->body = &md->body;
                    code.code->closure = tctx_.cur;
                    code.code->isMethod = true;
                    if (md->params.empty()) code.code->placeholders = computePlaceholders(md->body);
                    return code;
                };
                auto addTo = [&](auto& tbl, SubDecl* md) {
                    Value code = buildMethod(md);
                    if (md->isMulti) {
                        auto it = tbl.find(md->name);
                        if (it != tbl.end() && it->second.code && it->second.code->isMultiDispatcher) {
                            it->second.code->candidates.push_back(code);
                            return;
                        }
                        Value disp; disp.t = VT::Code; disp.code = std::make_shared<Callable>();
                        disp.code->name = md->name; disp.code->isMultiDispatcher = true;
                        disp.code->candidates.push_back(code);
                        tbl[md->name] = disp;
                        return;
                    }
                    tbl[md->name] = code;
                };
                auto existing = classes_.find(cd->name);
                if (existing != classes_.end()) {
                    // augment a user-declared type — merge into its ClassInfo
                    ClassInfo* ci = existing->second.get();
                    for (auto& md : cd->methods) addTo(ci->methods, md.get());
                    for (auto& a : cd->attrs) {
                        ClassAttr ca; ca.name = a.name; ca.sigil = a.sigil;
                        ca.pub = a.pub; ca.rw = a.rw; ca.def = a.def.get(); ca.type = a.type;
                        ci->attrs.push_back(ca);
                    }
                    for (auto& r : cd->rules) { ci->rules[r.name] = r.pattern; ci->ruleKind[r.name] = r.kind; }
                    noteSymbolMutation("augment (user type)");
                } else {
                    // augment a built-in type — park methods in the extension table
                    for (auto& md : cd->methods) addTo(builtinExt_[cd->name], md.get());
                    noteSymbolMutation("augment (built-in type)");
                }
                for (auto& st : cd->body) exec(st.get()); // nested decls, if any
                return Value::typeObj(cd->name);
            }
            if (cd->isPackage) {
                // file-scoped `unit module Foo;` (empty body): just register the name;
                // the rest of the file runs in the enclosing scope.
                if (cd->body.empty()) {
                    if (!cd->name.empty()) tctx_.cur->define(cd->name, Value::typeObj(cd->name));
                    return Value::any();
                }
                // braced `module Foo { ... }`: run body in a child scope, then publish
                // its symbols globally under qualified names ($Foo::bar, &Foo::sub).
                if (!cd->name.empty()) tctx_.cur->define(cd->name, Value::typeObj(cd->name));
                std::string savedPrefix = tctx_.pkgPrefix;
                tctx_.pkgPrefix += cd->name + "::";
                auto pkgEnv = std::make_shared<Env>();
                pkgEnv->parent = tctx_.cur;
                auto saved = tctx_.cur; tctx_.cur = pkgEnv;
                auto savedPkg = curPkgEnv_; curPkgEnv_ = pkgEnv; // `our` inside the package installs here (published qualified)
                for (auto& st : cd->body) exec(st.get());
                tctx_.cur = saved; curPkgEnv_ = savedPkg;
                // Only `our`-declared sigil vars are visible by qualified name; `my` stays lexical.
                std::set<std::string> ourVars;
                for (auto& st : cd->body) {
                    Expr* e = st->kind == NK::ExprStmt ? static_cast<ExprStmt*>(st.get())->e.get() : nullptr;
                    if (e && e->kind == NK::Assign) e = static_cast<Assign*>(e)->target.get();
                    if (e && e->kind == NK::VarExpr) { auto* v = static_cast<VarExpr*>(e); if (v->declare && v->declScope == "our") ourVars.insert(v->name); }
                }
                for (auto& kv : pkgEnv->vars) {
                    const std::string& sym = kv.first;
                    bool sigilVar = !sym.empty() && (sym[0]=='$'||sym[0]=='@'||sym[0]=='%');
                    if (sigilVar && !ourVars.count(sym)) continue; // a `my` package var — not published
                    std::string qual;
                    if (!sym.empty() && (sym[0]=='$'||sym[0]=='@'||sym[0]=='%'||sym[0]=='&'))
                        qual = std::string(1, sym[0]) + tctx_.pkgPrefix + sym.substr(1);
                    else qual = tctx_.pkgPrefix + sym;
                    noteSymbolMutation("package-qualified global (our)");
                    global_->define(qual, kv.second);
                }
                tctx_.pkgPrefix = savedPrefix;
                return Value::any();
            }
            auto ci = std::make_shared<ClassInfo>();
            // anonymous `role {…}` / `class {…}` literals get a synthesized name so
            // they can be registered, mixed in (`does`/`but`), and introspected.
            std::string clsName = cd->name.empty()
                ? "<anon|" + std::to_string(++anonTypeCounter_) + ">" : cd->name;
            ci->name = clsName;
            if (!cd->parent.empty()) {
                // a type may not inherit from / compose itself:  class A is A / role A does A
                if (cd->parent == cd->name && !cd->name.empty())
                    throw RakuError{Value::typeObj(cd->isRole ? "X::InvalidType" : "X::Inheritance::SelfInherit"),
                        std::string(cd->isRole ? "Role" : "Class") + " '" + cd->name + "' cannot inherit from / compose itself"};
                auto it = classes_.find(cd->parent);
                if (it != classes_.end()) ci->parent = it->second;
                else if (isKnownTypeName(cd->parent)) ci->nativeParent = cd->parent; // is Str / is Cool / …
                else if (!cd->isRole && !cd->parentIsDoes && !isKnownTypeName(cd->parent))
                    // deriving from an undeclared class is a compile-time error
                    throw RakuError{Value::typeObj("X::Inheritance::UnknownParent"),
                        "Class '" + cd->name + "' cannot inherit from '" + cd->parent +
                        "' because it is unknown"};
            }
            // additional `is Parent` targets — multiple inheritance
            for (auto& pn : cd->extraParents) {
                if (pn == cd->name)
                    throw RakuError{Value::typeObj("X::Inheritance::SelfInherit"),
                        "Class '" + cd->name + "' cannot inherit from itself"};
                auto it = classes_.find(pn);
                if (it != classes_.end()) ci->extraParents.push_back(it->second);
                else if (!isKnownTypeName(pn))
                    throw RakuError{Value::typeObj("X::Inheritance::UnknownParent"),
                        "Class '" + cd->name + "' cannot inherit from '" + pn + "' because it is unknown"};
            }
            // compose additional `does Role`s: flatten their methods/attrs in (the
            // class's own methods, registered below, override by key)
            for (auto& rn : cd->roles) {
                ci->doneRoles.insert(rn); // record membership (for ~~ Role / .does), even if unknown
                auto it = classes_.find(rn);
                if (it == classes_.end()) continue;
                for (auto& kv : it->second->methods) ci->methods[kv.first] = kv.second;
                for (auto& a : it->second->attrs) ci->attrs.push_back(a);
                for (auto& sub : it->second->doneRoles) ci->doneRoles.insert(sub); // role-of-role
            }
            // a role used as a parent (`class C does R` where R lands as parent) also counts
            if (ci->parent && ci->parent->isRole) ci->doneRoles.insert(ci->parent->name);
            for (auto& p : ci->extraParents) if (p && p->isRole) ci->doneRoles.insert(p->name);
            ci->isGrammar = cd->isGrammar;
            ci->isRole = cd->isRole;
            ci->declEnv = tctx_.cur; // capture the declaration scope (attr-default closures)
            for (auto& r : cd->rules) { ci->rules[r.name] = r.pattern; ci->ruleKind[r.name] = r.kind; if (!r.params.empty()) ci->ruleParams[r.name] = r.params; }
            for (auto& a : cd->attrs) {
                ClassAttr ca; ca.name = a.name; ca.sigil = a.sigil; ca.pub = a.pub; ca.rw = a.rw; ca.type = a.type;
                ca.def = a.def.get();
                ci->attrs.push_back(ca);
            }
            for (auto& md : cd->methods) {
                Value code; code.t = VT::Code;
                code.code = std::make_shared<Callable>();
                code.code->name = md->name;
                code.code->params = &md->params;
                code.code->retType = md->retType;
                code.code->body = &md->body;
                code.code->closure = tctx_.cur;
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
            // role composition check: a non-role class that composes a role must
            // implement every method the role requires (e.g. CompUnit::Repository).
            if (!cd->isRole) {
                std::vector<ClassInfo*> composed;
                if (ci->parent && ci->parent->isRole) composed.push_back(ci->parent.get());
                for (auto& p : ci->extraParents) if (p && p->isRole) composed.push_back(p.get());
                for (auto& rn : cd->roles) { auto it = classes_.find(rn); if (it != classes_.end() && it->second->isRole) composed.push_back(it->second.get()); }
                for (ClassInfo* role : composed)
                    for (const std::string& req : role->requiredMethods)
                        if (!ci->findMethod(req))
                            throw RakuError{Value::typeObj("X::Role::Unimplemented"),
                                "Method '" + req + "' must be implemented by " +
                                (cd->name.empty() ? "<anon>" : "'" + cd->name + "'") +
                                " because it is required by role '" + role->name + "'"};
            }
            noteSymbolMutation("class/role/grammar declaration");
            classes_[clsName] = ci;
            if (!cd->name.empty()) tctx_.cur->define(cd->name, Value::typeObj(cd->name));
            // register nested classes/enums (and static subs) declared in the body
            for (auto& st : cd->body) exec(st.get());
            // evaluate to the type object, so `my class Foo {…}` / anon `role {…}` work as expressions
            return Value::typeObj(clsName);
        }
        case NK::ReturnStmt: {
            auto* r = static_cast<ReturnStmt*>(s);
            Value v = r->value ? eval(r->value.get()) : Value::any();
            throw ReturnEx{v};
        }
        case NK::LastStmt: throw LastEx{static_cast<LastStmt*>(s)->target};
        case NK::NextStmt: throw NextEx{static_cast<NextStmt*>(s)->target};
        case NK::RedoStmt: throw RedoEx{static_cast<RedoStmt*>(s)->target};
        case NK::IfStmt: {
            auto* is = static_cast<IfStmt*>(s);
            for (size_t bi = 0; bi < is->branches.size(); bi++) {
                auto& br = is->branches[bi];
                Value cv = eval(br.first.get());
                bool c = boolify(cv);
                if (is->isUnless) c = !c;
                if (c) {
                    auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
                    if (bi == 0 && !is->thenVar.empty()) scope->define(is->thenVar, cv); // if EXPR -> $x
                    return execBlock(br.second.get(), scope);
                }
                if (is->isUnless) break; // unless has single branch
            }
            if (is->elseBlock) {
                auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
                return execBlock(is->elseBlock.get(), scope);
            }
            return Value::any();
        }
        case NK::WhileStmt: {
            auto* ws = static_cast<WhileStmt*>(s);
            ValueList collected; ValueList* col = ws->asExpr ? &collected : nullptr;
            bool firstIter = true;
            for (;;) {
                Value cv = eval(ws->cond.get());
                bool c = boolify(cv);
                if (ws->isUntil) c = !c;
                if (!c) break;
                auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
                if (!ws->var.empty()) scope->define(ws->var, cv); // while EXPR -> $x { }
                // FIRST runs on the first iteration only; LAST would need lookahead, so it
                // is approximated as always-last (a single flag can't know the true last).
                if (!runLoopBody(ws->body.get(), scope, ws->label, firstIter, true, col)) break;
                firstIter = false;
            }
            return ws->asExpr ? Value::list(std::move(collected)) : Value::any();
        }
        case NK::ForStmt: {
            auto* fs = static_cast<ForStmt*>(s);
            ValueList collected; ValueList* col = fs->asExpr ? &collected : nullptr;
            auto forResult = [&]() { return fs->asExpr ? Value::list(std::move(collected)) : Value::any(); };
            // `EXPR for LIST` — a statement modifier makes no implicit block: the body
            // runs in the enclosing scope (so `my $x = … for …` leaves $x declared),
            // with $_ topicalized per iteration and restored afterward.
            if (fs->modifier && fs->vars.empty() && !fs->destructure) {
                Value lv = eval(fs->list.get());
                auto env = tctx_.cur;
                bool hadTopic = env->vars.count("$_");
                Value savedTopic = hadTopic ? env->vars["$_"] : Value::any();
                // `$_ *= 10 for @a` rw-aliases $_ to @a's elements (mutable @-variable).
                bool rw = lv.t == VT::Array && lv.arr && fs->list->kind == NK::VarExpr &&
                          !static_cast<VarExpr*>(fs->list.get())->name.empty() &&
                          static_cast<VarExpr*>(fs->list.get())->name[0] == '@';
                if (rw) {
                    auto arr = lv.arr;
                    for (size_t i = 0; i < arr->size(); i++) {
                        env->vars["$_"] = (*arr)[i];
                        bool cont = runLoopBody(fs->body.get(), env, fs->label, i == 0, i + 1 == arr->size(), col);
                        (*arr)[i] = env->vars["$_"];
                        if (!cont) break;
                    }
                } else {
                    ValueList items;
                    if (lv.t == VT::Array && lv.arr) items = *lv.arr;
                    else if (lv.t == VT::Range) items = lv.flatten();
                    else items.push_back(lv);
                    for (size_t i = 0; i < items.size(); i++) {
                        env->vars["$_"] = items[i];
                        if (!runLoopBody(fs->body.get(), env, fs->label, i == 0, i + 1 == items.size(), col)) break;
                    }
                }
                if (hadTopic) env->vars["$_"] = savedTopic; else env->vars.erase("$_");
                return forResult();
            }
            Value listv = eval(fs->list.get());
            // A `$`-sigil scalar source (or an explicitly itemized value) is a single item:
            // `for $scalar { }` runs once even when the scalar holds an Array/Range, because a
            // scalar container does not flatten in list context. `@a`, ranges, and lists still flatten.
            bool scalarItem = listv.itemized ||
                (fs->list->kind == NK::VarExpr && !static_cast<VarExpr*>(fs->list.get())->name.empty()
                 && static_cast<VarExpr*>(fs->list.get())->name[0] == '$');
            // Fast paths for the common single-topic loop: avoid materializing the
            // whole sequence up front (a Range of N ints or a copy of an N-elem array).
            if (!scalarItem && !fs->destructure && fs->vars.size() <= 1) {
                const std::string var = fs->vars.empty() ? "$_" : fs->vars[0];
                // Reuse one Env across iterations for speed. This is only safe when
                // nothing captured the previous iteration's scope (a closure would
                // hold a reference, bumping use_count); in that case we allocate a
                // fresh Env so each closure keeps its own binding — exact semantics.
                std::shared_ptr<Env> scope;
                auto freshScope = [&]() {
                    if (!scope || scope.use_count() > 1) {
                        scope = std::make_shared<Env>();
                        scope->parent = tctx_.cur;
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
                        if (!runLoopBody(fs->body.get(), scope, fs->label, k == lo, k == hi, col)) break;
                    }
                    return forResult();
                }
                if (listv.t == VT::Array && listv.arr) {
                    auto arr = listv.arr; // share, don't copy the elements
                    // `$_` is rw-aliased to the elements when the source is a mutable
                    // `@`-variable, so `for @a { $_ *= 10 }` writes back into @a.
                    bool rw = fs->vars.empty() && fs->list->kind == NK::VarExpr &&
                              !static_cast<VarExpr*>(fs->list.get())->name.empty() &&
                              static_cast<VarExpr*>(fs->list.get())->name[0] == '@';
                    for (size_t i = 0; i < arr->size(); i++) {
                        freshScope();
                        scope->define(var, (*arr)[i]);
                        bool cont = runLoopBody(fs->body.get(), scope, fs->label, i == 0, i + 1 == arr->size(), col);
                        if (rw) { auto it = scope->vars.find(var); if (it != scope->vars.end()) (*arr)[i] = it->second; }
                        if (!cont) break;
                    }
                    return forResult();
                }
            }
            ValueList items;
            if (scalarItem) items.push_back(listv); // a $-scalar / itemized source is one item
            else if (listv.t == VT::Array && listv.arr) items = *listv.arr; // one-level
            else if (listv.t == VT::Range) items = listv.flatten();
            else items.push_back(listv);
            // `-> ($a,$b,$c)`: each element is unpacked into the vars (one item/iteration).
            if (fs->destructure && !fs->vars.empty()) {
                for (size_t i = 0; i < items.size(); i++) {
                    auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
                    ValueList row = items[i].t == VT::Array ? *items[i].arr : items[i].flatten();
                    for (size_t k = 0; k < fs->vars.size(); k++)
                        scope->define(fs->vars[k], k < row.size() ? row[k] : Value::any());
                    if (!runLoopBody(fs->body.get(), scope, fs->label, i == 0, i + 1 == items.size(), col)) break;
                }
                return forResult();
            }
            size_t nvars = fs->vars.empty() ? 1 : fs->vars.size();
            for (size_t i = 0; i < items.size(); i += nvars) {
                auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
                if (fs->vars.empty()) {
                    scope->define("$_", items[i]);
                } else {
                    for (size_t k = 0; k < fs->vars.size(); k++) {
                        scope->define(fs->vars[k], (i + k < items.size()) ? items[i + k] : Value::any());
                    }
                }
                if (!runLoopBody(fs->body.get(), scope, fs->label, i == 0, i + nvars >= items.size(), col)) break;
            }
            return forResult();
        }
        case NK::GivenStmt: {
            auto* g = static_cast<GivenStmt*>(s);
            Value topic = eval(g->topic.get());
            // with/without definedness guard
            bool skip = (g->defGuard == 1 && !isDefined(topic)) || (g->defGuard == 2 && isDefined(topic));
            auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
            scope->define("$_", topic);
            if (!g->var.empty()) scope->define(g->var, topic);
            // `given`/`with` is an expression: it evaluates to the value of the matched
            // `when`/`default` block (delivered via BreakGivenEx), or, if nothing matches,
            // the value of the block's last statement.
            if (skip) {
                if (g->hasElse) { try { return execBlock(g->elseBody.get(), scope); } catch (BreakGivenEx& e) { return e.hasVal ? e.v : Value::any(); } }
                return Value::any();
            }
            try { return execBlock(g->body.get(), scope); }
            catch (BreakGivenEx& e) { return e.hasVal ? e.v : Value::any(); }
        }
        case NK::WhenStmt: {
            auto* w = static_cast<WhenStmt*>(s);
            bool match = w->isDefault;
            if (!w->isDefault) {
                Value* tp = tctx_.cur->find("$_");
                Value topic = tp ? *tp : Value::any();
                Value cv = eval(w->cond.get());
                // `when X` == `if $_ ~~ X`: a regex literal already matched $_ above;
                // a Regex/Callable value is invoked; else a value/type smartmatch.
                if (w->cond->kind == NK::RegexLit) match = boolify(cv);
                else if (cv.t == VT::Regex) match = regexMatch(topic.toStr(), cv.s).truthy();
                else if (cv.t == VT::Code) match = boolify(callCallable(cv, {topic}));
                else match = applyArith("~~", topic, cv).truthy();
            }
            if (match) {
                auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
                Value bv;
                try { bv = execBlock(w->body.get(), scope); }
                catch (ProceedEx&) { return Value::any(); } // `proceed`: try the next when
                throw BreakGivenEx{bv, true}; // matched `when` exits the given, carrying its value
            }
            return Value::any();
        }
        case NK::LoopStmt: {
            auto* ls = static_cast<LoopStmt*>(s);
            ValueList collected; ValueList* col = ls->asExpr ? &collected : nullptr;
            auto outer = std::make_shared<Env>(); outer->parent = tctx_.cur;
            auto saved = tctx_.cur; tctx_.cur = outer;
            try {
                if (ls->init) eval(ls->init.get());
                bool firstIter = true;
                for (;;) {
                    if (ls->cond && !boolify(eval(ls->cond.get()))) break;
                    auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
                    if (!runLoopBody(ls->body.get(), scope, ls->label, firstIter, true, col)) break;
                    firstIter = false;
                    if (ls->incr) eval(ls->incr.get());
                }
            } catch (...) { tctx_.cur = saved; throw; }
            tctx_.cur = saved;
            return ls->asExpr ? Value::list(std::move(collected)) : Value::any();
        }
        case NK::RepeatStmt: {
            auto* r = static_cast<RepeatStmt*>(s);
            bool firstIter = true;
            for (;;) {
                auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
                if (!runLoopBody(r->body.get(), scope, r->label, firstIter, true)) break;
                firstIter = false;
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
    code.code->closure = tctx_.cur;
    code.code->isBlock = !be->isSub; // a bare { } / pointy block is a Block; `sub {…}` stays a Sub
    if (be->params.empty()) code.code->placeholders = computePlaceholders(be->body);
    return code;
}

// A Pair argument counts as *named* only if it was written syntactically (k=>v / :k(v));
// a Pair that arrived as a value (from a variable, list, iteration, or return) is positional.
static inline bool isNamedArg(const Value& v) { return v.t == VT::Pair && v.namedArg; }

void Interpreter::bindParams(const std::vector<Param>& params, ValueList& args,
                             std::shared_ptr<Env>& env) {
    // Fast path: every parameter is a plain mandatory positional scalar and no
    // named arguments were passed — the overwhelmingly common signature. Bind
    // positionally, skipping the named-map / explicit-named-set / substr /
    // default-eval machinery below (all pure allocation for this case).
    {
        bool simple = true;
        for (auto& p : params)
            if (p.sigil != '$' || p.named || p.slurpy || p.optional || p.invocant ||
                p.isRw || p.isCopy || p.defaultVal || p.subSig || p.litVal ||
                p.whereExpr || p.defConstraint) { simple = false; break; }
        if (simple)
            for (auto& a : args) if (isNamedArg(a)) { simple = false; break; }
        if (simple) {
            for (size_t i = 0; i < params.size(); i++) {
                if (i < args.size()) {
                    Value v = args[i];
                    v.readonly = true;               // plain scalar param is readonly
                    env->define(params[i].name, std::move(v));
                } else {
                    env->define(params[i].name, typedDefault(params[i].type, '$'));
                }
            }
            return;
        }
    }
    // split named vs positional
    ValueList positional;
    std::map<std::string, Value> named;
    for (auto& a : args) {
        if (isNamedArg(a)) named[a.s] = a.pairVal ? *a.pairVal : Value::any();
        else positional.push_back(a);
    }
    // names captured by explicit named params are NOT collected by a slurpy *%hash
    std::set<std::string> explicitNamed;
    for (auto& p : params)
        if (p.named && !p.slurpy)
            explicitNamed.insert(p.name.size() > 1 ? p.name.substr(1) : p.name);
    // A default value is evaluated in the param scope being built, so it can refer
    // to earlier parameters (`sub f($g, $a = $g/2)`).
    auto evalDefault = [&](Expr* e) -> Value {
        auto saved = tctx_.cur; tctx_.cur = env;
        Value v; try { v = eval(e); } catch (...) { tctx_.cur = saved; throw; }
        tctx_.cur = saved; return v;
    };
    size_t pi = 0;
    for (auto& p : params) {
        std::string bareName = !p.namedKey.empty() ? p.namedKey
                             : (p.name.size() > 1 ? p.name.substr(1) : p.name);
        if (p.slurpy) {
            if (p.sigil == '%') {
                Value h = Value::makeHash();
                for (auto& kv : named) if (!explicitNamed.count(kv.first)) (*h.hash)[kv.first] = kv.second;
                env->define(p.name, h);
            } else {
                Value a = Value::array();
                size_t remaining = positional.size() - pi;
                if (p.slurpyKind == 'f') {
                    // *@a — flatten: dissolve every Iterable arg into the slurpy.
                    for (; pi < positional.size(); pi++) {
                        auto& x = positional[pi];
                        if (!x.itemized && (x.t == VT::Array || x.t == VT::Range))
                            for (auto& e : x.flatten()) a.arr->push_back(e);
                        else a.arr->push_back(x);
                    }
                } else if (p.slurpyKind == 'n') {
                    // **@a — no flatten: keep every arg as-is.
                    for (; pi < positional.size(); pi++) a.arr->push_back(positional[pi]);
                } else {
                    // +@a (and default) — single-argument rule: a lone Iterable arg
                    // flattens; multiple args are kept as-is (so f(@a,@b) is two elements).
                    if (remaining == 1 && !positional[pi].itemized && (positional[pi].t == VT::Array || positional[pi].t == VT::Range)) {
                        for (auto& x : positional[pi].flatten()) a.arr->push_back(x);
                        pi++;
                    } else {
                        for (; pi < positional.size(); pi++) a.arr->push_back(positional[pi]);
                    }
                }
                env->define(p.name, a);
                // capture sub-signature `|c($x, $y)` — unpack the slurped positionals
                if (p.subSig) bindParams(*p.subSig, *a.arr, env);
            }
            continue;
        }
        // destructure a value against p's sub-signature: positionals from the
        // elements, named inner params (`:key($k)`) from the value's accessors
        auto destructure = [&](const Param& sp, const Value& v) {
            ValueList inner = (v.t == VT::Array && v.arr) ? *v.arr
                            : (v.t == VT::Range) ? v.flatten() : ValueList{v};
            for (auto& ip : *sp.subSig)
                if (ip.named) {
                    std::string key = ip.namedKey.empty()
                        ? (ip.name.size() > 1 ? ip.name.substr(1) : ip.name) : ip.namedKey;
                    try {
                        Value got = methodCall(v, key, {});
                        Value na = Value::pair(key, got); na.namedArg = true;
                        inner.push_back(na);
                    } catch (RakuError&) {} // absent accessor → param falls to its default
                }
            bindParams(*sp.subSig, inner, env);
        };
        if (p.named) {
            auto it = named.find(bareName);
            if (it != named.end()) {
                if (p.subSig) destructure(p, it->second); // :value((Str :key($d), …))
                if (!p.name.empty() || !p.subSig) env->define(p.name, it->second);
            }
            else if (p.defaultVal) env->define(p.name, evalDefault(p.defaultVal.get()));
            else if (p.required)
                throw RakuError{Value::typeObj("X::Parameter::RequiredNamed"),
                                "Required named parameter '" + bareName + "' not passed"};
            else env->define(p.name, typedDefault(p.type, p.sigil));
            continue;
        }
        if (pi < positional.size()) {
            Value v = positional[pi++];
            if (p.subSig) {
                destructure(p, v);
                // a *named* destructuring param (`@a [$x, *@y]`) also binds the whole arg
                if (!p.name.empty())
                    env->define(p.name, p.sigil == '@' ? coerceArray(v) : p.sigil == '%' ? coerceHash(v) : v);
                continue;
            }
            if (p.sigil == '@') v = coerceArray(v);
            else if (p.sigil == '%') v = coerceHash(v);
            // a plain scalar param (no `is rw`/`is copy`) is readonly — mutating it (s///) dies
            if (p.sigil == '$' && !p.isRw && !p.isCopy && !p.invocant) v.readonly = true;
            env->define(p.name, v);
        } else if (p.subSig) {
            bindParams(*p.subSig, positional, env); // no arg → bind inner to (), fills defaults
        } else if (p.defaultVal) {
            env->define(p.name, evalDefault(p.defaultVal.get()));
        } else {
            env->define(p.name, typedDefault(p.type, p.sigil));
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
    ValueList pos; for (auto& a : args) if (!isNamedArg(a)) pos.push_back(a);
    size_t required = 0, total = 0; bool slurpy = false;
    std::vector<const Param*> positional;
    for (auto& p : params) {
        if (p.named) continue;
        if (p.invocant) continue; // the invocant (`Foo:D:`) is matched by the dispatch, not a positional arg
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
        // destructuring param `[$a,$b]`: the arg must be a list/array whose element
        // count matches the sub-signature's positional arity (so `foo([1,2])` picks
        // the two-element candidate over the one-element one).
        if (p->subSig) {
            if (pos[i].t != VT::Array || !pos[i].arr) return -1;
            size_t reqd = 0, tot = 0; bool sslurpy = false;
            for (auto& sp : *p->subSig) {
                if (sp.named) continue;
                if (sp.slurpy) { sslurpy = true; continue; }
                tot++;
                if (!sp.optional && !sp.defaultVal) reqd++;
            }
            size_t got = pos[i].arr->size();
            if (got < reqd) return -1;
            if (!sslurpy && got > tot) return -1;
            score += 3; // a matching-arity destructure is very specific
            continue;
        }
        if (!typeMatchesArg(pos[i], p->type)) return -1;
        // type smiley: :D requires a defined arg, :U requires an undefined one
        if (p->defConstraint == 1 && !isDefined(pos[i])) return -1;
        if (p->defConstraint == 2 && isDefined(pos[i])) return -1;
        if (p->defConstraint) score++; // a smiley is more specific
        if (!p->type.empty() && p->type != "Any" && p->type != "Mu") {
            score++;                                   // constrained at all beats unconstrained
            if (p->type == pos[i].typeName()) score++; // exact type is more specific than a supertype
                                                       // (so multi f(Int) beats multi f(Numeric) for an Int)
        }
        if (p->whereExpr) {
            auto env = std::make_shared<Env>(); env->parent = tctx_.cur;
            if (!p->name.empty()) env->define(p->name, pos[i]);
            env->define("$_", pos[i]);
            auto saved = tctx_.cur; tctx_.cur = env;
            bool ok = false;
            try {
                Value cv = eval(p->whereExpr.get());
                // `where EXPR` is a smartmatch: a WhateverCode/Code constraint is called with the value
                if (cv.t == VT::Code && cv.code) cv = callCallable(cv, ValueList{pos[i]});
                ok = boolify(cv);
            } catch (...) { tctx_.cur = saved; return -1; }
            tctx_.cur = saved;
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

static bool isJunction(const Value& v); // defined below

bool Interpreter::boolify(const Value& v) {
    if (v.t == VT::Object && v.obj && v.obj->cls) {
        if (Value* b = v.obj->cls->findMethod("Bool"))
            return invokeMethod(*b, v, {}).truthy();
    }
    if (isJunction(v)) { // collapse a junction to Bool per its kind
        int t = 0, total = 0;
        for (auto& e : *v.arr) { total++; if (boolify(e)) t++; }
        return v.enumName == "any" ? t > 0 : v.enumName == "all" ? t == total
             : v.enumName == "one" ? t == 1 : t == 0;
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

// Push the current %*ENV hash into the real process environment so a child
// launched by run()/shell() inherits any variables the program set/changed.
bool Interpreter::envFlag(const std::string& name) {
    auto it = global_->vars.find("%*ENV");
    if (it == global_->vars.end() || it->second.t != VT::Hash || !it->second.hash) return false;
    auto vit = it->second.hash->find(name);
    return vit != it->second.hash->end() && vit->second.truthy();
}

std::string Interpreter::envStr(const std::string& name) {
    auto it = global_->vars.find("%*ENV");
    if (it == global_->vars.end() || it->second.t != VT::Hash || !it->second.hash) return "";
    auto vit = it->second.hash->find(name);
    return vit != it->second.hash->end() ? vit->second.toStr() : "";
}

// Serialize an uncaught exception as JSON for RAKU_EXCEPTIONS_HANDLER=JSON:
// { "Type::Name" : { "attr" : value, …, "message" : <msg-or-null> } }
std::string Interpreter::exceptionToJson(const Value& ex) {
    auto jstr = [](const std::string& s) {
        std::string o = "\"";
        for (char c : s) {
            switch (c) { case '"': o += "\\\""; break; case '\\': o += "\\\\"; break;
                         case '\n': o += "\\n"; break; case '\t': o += "\\t"; break;
                         default: o += c; }
        }
        return o + "\"";
    };
    std::string tn = ex.obj && ex.obj->cls ? ex.obj->cls->name : "Exception";
    std::string o = "{\n  " + jstr(tn) + " : {\n";
    bool first = true;
    if (ex.obj) for (auto& kv : ex.obj->attrs) {
        if (kv.first == "message") continue; // message emitted last (may be null)
        o += (first ? "" : ",\n"); first = false;
        o += "    " + jstr(kv.first) + " : " + (kv.second.isNumeric() ? kv.second.toStr() : jstr(kv.second.toStr()));
    }
    // message: the value if present, else null
    std::string msg;
    bool hasMsg = ex.obj && ex.obj->attrs.count("message");
    if (hasMsg) msg = ex.obj->attrs.at("message").toStr();
    o += (first ? "" : ",\n");
    o += "    \"message\" : " + (hasMsg ? jstr(msg) : std::string("null")) + "\n";
    o += "  }\n}\n";
    return o;
}

void Interpreter::syncEnvToProcess() {
    auto it = global_->vars.find("%*ENV");
    if (it == global_->vars.end() || it->second.t != VT::Hash || !it->second.hash) return;
    for (auto& kv : *it->second.hash) {
        std::string val = kv.second.toStr();
        setenv(kv.first.c_str(), val.c_str(), 1);
    }
}

// Index with a Whatever/WhateverCode key, for native codegen: `@a[*-1]` (call
// the WhateverCode with the length), `@a[*]` (all elements as a list).
// Grow a lazy list's materialised prefix to at least `n` elements (bounded, so a
// runaway never truly loops). No-op for a normal (non-lazy) array.
void Interpreter::materializeLazy(const Value& v, size_t n) {
    if (!v.ext || !v.arr) return;
    auto st = std::static_pointer_cast<LazySeqState>(v.ext);
    if (!st->appendNext) return;
    const size_t CAP = 1000000;
    while (v.arr->size() < n && v.arr->size() < CAP)
        if (!st->appendNext(*v.arr)) break;
}

Value Interpreter::idxW(const Value& base, Value key, bool isHash) {
    // a `but`/`does` mixin over a Hash/Array delegates subscripting to the box
    if (base.t == VT::Object && base.obj && base.obj->hasBoxed)
        return idxW(base.obj->boxed, std::move(key), isHash);
    // @a[*-1] / @a[*] against an infinite lazy array can't know the end
    if (base.t == VT::Array && base.ext && std::static_pointer_cast<LazySeqState>(base.ext)->infinite
        && (key.t == VT::Whatever || (key.t == VT::Code && key.code && key.code->isWhateverCode)))
        throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot use a Whatever index on an infinite list"};
    long long n = (base.t == VT::Array && base.arr) ? (long long)base.arr->size()
                : base.t == VT::Range ? (long long)base.flatten().size() : 0;
    if (key.t == VT::Code && key.code && key.code->isWhateverCode)
        key = callCallable(key, ValueList{Value::integer(n)});
    if (key.t == VT::Whatever) { // @a[*] — the whole list / %h{*} — all values
        Value o = Value::array(); o.isList = true;
        if (isHash && base.t == VT::Hash && base.hash) { for (auto& kv : *base.hash) o.arr->push_back(kv.second); }
        else if (base.t == VT::Array && base.arr) *o.arr = *base.arr;
        else *o.arr = base.flatten();
        return o;
    }
    return rtIndexGet(base, key, isHash);
}

// $* / $? magical variables, for native codegen (mirrors the VarExpr evaluator).
Value Interpreter::dynVar(const std::string& name) {
    if (name == "$*CWD") { char buf[4096]; Value p = Value::str(getcwd(buf, sizeof buf) ? buf : "."); p.hashKind = "IO"; return p; }
    if (name == "$*RAKU" || name == "$*PERL" || name == "$?RAKU" || name == "$?PERL") { Value r = Value::makeHash(); r.hashKind = "Raku"; return r; }
    if (name == "$?FILE") return Value::str(srcFile_);
    if (name == "$*PROGRAM") { Value p = Value::str(srcFile_); p.hashKind = "IO"; return p; }
    if (name == "$*PROGRAM-NAME") return Value::str(srcFile_);
    if (name == "$*EXECUTABLE" || name == "$*EXECUTABLE-NAME") { Value p = Value::str(execPath_); p.hashKind = "IO"; return p; }
    if (name == "$*OUT" || name == "$*ERR" || name == "$*IN") { Value h = Value::makeHash(); h.hashKind = "FileHandle"; (*h.hash)["std"] = Value::str(name == "$*ERR" ? "err" : name == "$*IN" ? "in" : "out"); return h; }
    if (name == "$*DISTRO") { Value h = Value::makeHash(); h.hashKind = "Distro"; (*h.hash)["name"] = Value::str("macos"); return h; }
    if (name == "$*KERNEL") { Value h = Value::makeHash(); h.hashKind = "Kernel"; (*h.hash)["name"] = Value::str("darwin"); return h; }
    if (name == "$*VM")     { Value h = Value::makeHash(); h.hashKind = "VM";     (*h.hash)["name"] = Value::str("moar");   return h; }
    if (name == "$*SPEC") return Value::typeObj("IO::Spec::Unix");
    if (name == "$*PID") return Value::integer((long long)::getpid());
    if (name == "$*THREAD") { Value h = Value::makeHash(); h.hashKind = "Thread"; (*h.hash)["initial"] = Value::boolean(threadDepth_ == 0); return h; }
    if (name == "$*SCHEDULER") { Value s = Value::makeHash(); s.hashKind = "Scheduler"; (*s.hash)["name"] = Value::str("ThreadPoolScheduler"); return s; }
    if (name == "$*TMPDIR") { const char* t = std::getenv("TMPDIR"); std::string d = (t && *t) ? t : "/tmp"; while (d.size() > 1 && d.back() == '/') d.pop_back(); Value p = Value::str(d); p.hashKind = "IO"; return p; }
    return Value::any();
}

// Value-level indexing for native codegen (no AST). Read returns Nil when absent.
// The default value for a missing element of a (possibly typed) container:
// a typed container answers its element type object, else Nil.
static Value typedElemDefault(const Value& base) {
    if (base.ofType.empty()) return Value::nil();
    std::string first = base.ofType.substr(0, base.ofType.find(','));
    if (!first.empty() && std::isupper((unsigned char)first[0])) return Value::typeObj(first);
    return Value::nil();
}

Value rtIndexGet(const Value& base, const Value& key, bool isHash) {
    if (isHash) {
        if (base.t == VT::Hash && base.hash) {
            auto it = base.hash->find(key.toStr());
            if (it != base.hash->end()) return it->second;
        }
        return typedElemDefault(base);
    }
    if (base.t == VT::Range) {
        if (base.rTo >= 9000000000000000000LL) { // infinite range: index directly, don't materialise
            long long i = key.toInt(); if (i < 0) return Value::nil();
            return Value::integer(base.rFrom + (base.rExFrom ? 1 : 0) + i);
        }
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
    return typedElemDefault(base);
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

// --- argument binding for native codegen (flexible signatures) ---
Value rtPos(const ValueList& a, size_t idx) {
    size_t p = 0; for (auto& v : a) { if (isNamedArg(v)) continue; if (p == idx) return v; p++; }
    return Value::any();
}
bool rtHasPos(const ValueList& a, size_t idx) {
    size_t p = 0; for (auto& v : a) { if (isNamedArg(v)) continue; if (p == idx) return true; p++; }
    return false;
}
Value rtNamed(const ValueList& a, const std::string& key) {
    for (auto& v : a) if (isNamedArg(v) && v.s == key) return v.pairVal ? *v.pairVal : Value::any();
    return Value::any();
}
bool rtHasNamed(const ValueList& a, const std::string& key) {
    for (auto& v : a) if (isNamedArg(v) && v.s == key) return true;
    return false;
}
Value rtSlurpyPos(const ValueList& a, size_t from) {
    Value o = Value::array(); size_t p = 0;
    for (auto& v : a) { if (isNamedArg(v)) continue; if (p >= from) o.arr->push_back(v); p++; }
    return o;
}
Value rtSlurpyNamed(const ValueList& a) {
    Value o = Value::makeHash();
    for (auto& v : a) if (isNamedArg(v)) (*o.hash)[v.s] = v.pairVal ? *v.pairVal : Value::any();
    return o;
}
Value rtCoerceHash(const Value& v) {
    if (v.t == VT::Hash) return v;
    Value h = Value::makeHash();
    ValueList items = (v.t == VT::Array && v.arr) ? *v.arr : v.flatten();
    for (size_t i = 0; i < items.size(); ) {
        if (items[i].t == VT::Pair) { (*h.hash)[items[i].s] = items[i].pairVal ? *items[i].pairVal : Value::any(); i++; }
        else if (i + 1 < items.size()) { (*h.hash)[items[i].toStr()] = items[i + 1]; i += 2; } // flat key,value,…
        else i++;
    }
    return h;
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

Value Interpreter::callCallable(const Value& codeVal, ValueList args, const std::vector<ExprPtr>* rwArgs) {
    // A wrapped routine (&r.wrap({…})) runs its wrapper stack first. Each wrapper's
    // `callsame`/`nextsame` drops to the next inner wrapper, finally to the original
    // routine body (callCallableRaw). Wrappers are consulted outermost-first.
    if (codeVal.t == VT::Code && codeVal.code && !codeVal.code->wrappers.empty()) {
        const auto& wraps = codeVal.code->wrappers;
        std::function<Value(int, ValueList)> runLevel =
            [&](int level, ValueList as) -> Value {
                if (level < 0) return callCallableRaw(codeVal, std::move(as), rwArgs);
                RedispatchCtx rc;
                rc.sameArgs = as;
                rc.next = [&runLevel, level](ValueList na) -> Value { return runLevel(level - 1, std::move(na)); };
                rc.restart = [this, &codeVal, rwArgs](ValueList na) -> Value { return callCallable(codeVal, std::move(na), rwArgs); };
                redispatchStack_.push_back(std::move(rc));
                Value r;
                try { r = callCallable(wraps[level], as, rwArgs); }
                catch (...) { redispatchStack_.pop_back(); throw; }
                redispatchStack_.pop_back();
                return r;
            };
        return runLevel((int)wraps.size() - 1, std::move(args));
    }
    return callCallableRaw(codeVal, std::move(args), rwArgs);
}

static bool ncIsFloatType(const std::string& t) {
    return t == "num" || t == "num32" || t == "num64" || t == "Num" ||
           t == "Rat" || t == "Real" || t == "double" || t == "Numeric";
}

// One dispatch switch, arities 0..8: R = C return type, AT = C arg type, SRC = the
// arg array (`g` longs or `f` doubles), OUT = the result variable. The C++ compiler
// emits the correct ABI for each typed function-pointer cast — no assembly needed.
#define NC_DISPATCH(R, AT, SRC, OUT) switch (n) { \
    case 0: OUT = ((R(*)())sym)(); break; \
    case 1: OUT = ((R(*)(AT))sym)(SRC[0]); break; \
    case 2: OUT = ((R(*)(AT,AT))sym)(SRC[0],SRC[1]); break; \
    case 3: OUT = ((R(*)(AT,AT,AT))sym)(SRC[0],SRC[1],SRC[2]); break; \
    case 4: OUT = ((R(*)(AT,AT,AT,AT))sym)(SRC[0],SRC[1],SRC[2],SRC[3]); break; \
    case 5: OUT = ((R(*)(AT,AT,AT,AT,AT))sym)(SRC[0],SRC[1],SRC[2],SRC[3],SRC[4]); break; \
    case 6: OUT = ((R(*)(AT,AT,AT,AT,AT,AT))sym)(SRC[0],SRC[1],SRC[2],SRC[3],SRC[4],SRC[5]); break; \
    case 7: OUT = ((R(*)(AT,AT,AT,AT,AT,AT,AT))sym)(SRC[0],SRC[1],SRC[2],SRC[3],SRC[4],SRC[5],SRC[6]); break; \
    case 8: OUT = ((R(*)(AT,AT,AT,AT,AT,AT,AT,AT))sym)(SRC[0],SRC[1],SRC[2],SRC[3],SRC[4],SRC[5],SRC[6],SRC[7]); break; \
    default: throw RakuError{Value::typeObj("X::NYI"), "NativeCall: too many arguments (max 8)"}; \
}

// NativeCall (`is native`): resolve the C symbol via dlsym and call it. Arguments
// and the return are classified by their *declared* type — integer/pointer
// (`Str`→`char*`, the `int*`/`uint*`/`size_t`/`bool`/`Pointer` family → 64-bit) or
// floating-point (`num`/`num32`/`num64`) — so e.g. `sqrt(4)` correctly coerces its
// `Int` argument to a `double`. A call must be uniform (all integer/pointer *or*
// all floating-point) args; the return may be either. This covers all of libc's
// integer functions and the whole `<math.h>` surface. Not supported (they need
// libffi): mixed int/float arguments, C structs, `CArray`, and callbacks.
Value Interpreter::callNative(Callable& c, ValueList& args) {
    void* handle = RTLD_DEFAULT;
    if (!c.nativeLib.empty()) {
        // try the name as-is, then platform-decorated forms
        const std::string& l = c.nativeLib;
        for (const std::string& cand : {l, "lib" + l + ".dylib", "lib" + l + ".so", l + ".dylib", l + ".so"}) {
            if ((handle = dlopen(cand.c_str(), RTLD_LAZY | RTLD_GLOBAL))) break;
        }
        if (!handle) throw RakuError{Value::typeObj("X::Libc"), "Cannot load native library '" + c.nativeLib + "'"};
    }
    void* sym = dlsym(handle, c.nativeSym.c_str());
    if (!sym) throw RakuError{Value::typeObj("X::AdHoc"), "Cannot find native symbol '" + c.nativeSym + "'"};

    const std::vector<Param>* prm = c.params;
    std::vector<std::string> keep; keep.reserve(args.size()); // keep Str buffers alive across the call
    std::vector<long>   g;  // integer/pointer args
    std::vector<double> f;  // floating-point args
    bool anyFP = false, anyGP = false;
    for (size_t i = 0; i < args.size(); i++) {
        Value& v = args[i];
        std::string pt = (prm && i < prm->size()) ? (*prm)[i].type : "";
        bool fp = ncIsFloatType(pt) || (pt.empty() && (v.t == VT::Num || v.t == VT::Rat));
        if (v.t == VT::Str || (v.t == VT::Hash && v.hashKind == "IO")) { // Str → char*
            keep.push_back(v.toStr());
            g.push_back((long)(intptr_t)keep.back().c_str());
            anyGP = true;
        } else if (fp) { f.push_back(v.toNum()); anyFP = true; }
        else          { g.push_back(v.toInt()); anyGP = true; }
    }
    if (anyFP && anyGP)
        throw RakuError{Value::typeObj("X::NYI"), "NativeCall: mixed integer and floating-point arguments need libffi"};

    const std::string& rt = c.retType;
    bool retFP = ncIsFloatType(rt);
    size_t n = args.size();
    long ri = 0; double rd = 0;
    if (anyFP) { if (retFP) NC_DISPATCH(double, double, f, rd) else NC_DISPATCH(long, double, f, ri) }
    else       { if (retFP) NC_DISPATCH(double, long,   g, rd) else NC_DISPATCH(long, long,   g, ri) }

    if (rt.empty() || rt == "void" || rt == "Nil") return Value::nil();
    if (rt == "Str") return Value::str(ri ? std::string((const char*)(intptr_t)ri) : "");
    if (retFP) return Value::number(rd);
    if (rt == "bool" || rt == "Bool") return Value::boolean(ri != 0);
    return Value::integer(ri);
}
#undef NC_DISPATCH

Value Interpreter::callCallableRaw(const Value& codeVal, ValueList args, const std::vector<ExprPtr>* rwArgs) {
    // A Format template (q:o/…/) is callable: it applies as sprintf over the args.
    if (codeVal.t == VT::Code && codeVal.code && codeVal.code->isNative) return callNative(*codeVal.code, args);
    if (codeVal.t == VT::Hash && codeVal.hashKind == "Format") {
        std::string fmt = codeVal.hash && codeVal.hash->count("fmt") ? (*codeVal.hash)["fmt"].toStr() : "";
        return Value::str(doSprintf(fmt, args));
    }
    if (codeVal.t != VT::Code || !codeVal.code)
        throw RakuError{Value::str("Not callable"), "Cannot invoke non-Callable value of type " + codeVal.typeName()};
    DepthGuard guard(tctx_.callDepth);
    Callable& c = *codeVal.code;
    if (c.isMultiDispatcher) {
        // Dispatch to the best-scoring candidate not already tried for the given args,
        // pushing a redispatch context so callsame/nextsame (same args) and callwith/
        // nextwith (new args) re-dispatch to the next candidate. A `visited` set (shared
        // across the chain) picks the next-less-specific candidate and prevents loops.
        auto visited = std::make_shared<std::vector<const Value*>>();
        std::function<Value(ValueList)> dispatch = [this, &c, &codeVal, rwArgs, visited, &dispatch](ValueList as) -> Value {
            const Value* best = nullptr; int bestScore = -1;
            for (auto& cand : c.candidates) {
                bool seen = false; for (auto* v : *visited) if (v == &cand) { seen = true; break; }
                if (seen) continue;
                int s = scoreCandidate(cand, as);
                if (s > bestScore) { bestScore = s; best = &cand; }
            }
            if (!best || bestScore < 0) {
                // A redispatch (callsame/nextsame) that runs past the last same-class
                // candidate: for a METHOD multi, defer up the inheritance tree to the
                // outer dispatcher (the parent class's method pushed by
                // invokeMethodChain); otherwise → Nil.
                if (!visited->empty()) return Value::nil();
                throw RakuError{Value::str("X::Multi::NoMatch"),
                                "Cannot resolve caller " + c.name + "(); no matching multi candidate"};
            }
            visited->push_back(best);
            RedispatchCtx rc;
            rc.sameArgs = as;
            rc.next = [&dispatch](ValueList na) -> Value { return dispatch(std::move(na)); };
            rc.restart = [this, codeVal, rwArgs](ValueList na) -> Value { return callCallable(codeVal, std::move(na), rwArgs); };
            redispatchStack_.push_back(std::move(rc));
            Value r;
            try { r = callCallable(*best, as, rwArgs); }
            catch (...) { redispatchStack_.pop_back(); throw; }
            redispatchStack_.pop_back();
            return r;
        };
        return dispatch(args);
    }
    if (c.builtin) return c.builtin(*this, args);

    auto env = std::make_shared<Env>();
    // `state` vars live in a per-callable persistent env spliced into the lookup chain.
    // Its parent is constant (the closure scope, or global), so create it exactly once
    // — never rewrite it per call. That keeps concurrent invocations of the SAME closure
    // (parallel mode) from racing on c.stateEnv; call_once gives a lock-free fast path.
    std::call_once(c.stateInit, [&] {
        c.stateEnv = std::make_shared<Env>();
        c.stateEnv->parent = c.closure ? c.closure : global_;
    });
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
    auto saved = tctx_.cur;
    Env* savedState = tctx_.curStateEnv;
    tctx_.cur = env;
    tctx_.curStateEnv = c.stateEnv.get();
    tctx_.dynStack.push_back(saved.get()); // caller's scope, for dynamic $*var lookup
    Value last = Value::any();
    if (c.body) hoistSubs(*c.body); // nested named subs are visible throughout the body
    // an inline CATCH {} anywhere in the body handles exceptions from the whole block
    Block* catchBlk = nullptr;
    if (c.body) for (auto& s : *c.body)
        if (s->kind == NK::Block && static_cast<Block*>(s.get())->isCatch) catchBlk = static_cast<Block*>(s.get());
    if (c.body) runEnterPhasers(*c.body);
    try {
        if (c.body) {
            size_t nst = c.body->size();
            for (size_t i = 0; i < nst; i++) {
                auto* s = (*c.body)[i].get();
                if (isBlockPhaser(s)) continue;
                if (s->kind == NK::Block && static_cast<Block*>(s)->isCatch) continue;
                // Tail-position `return X` yields exactly X as the call result — evaluate
                // it directly instead of throwing+unwinding a ReturnEx (the hot path for
                // the many one-line accessor/action methods a grammar parse calls).
                if (i + 1 == nst && s->kind == NK::ReturnStmt) {
                    auto* r = static_cast<ReturnStmt*>(s);
                    last = r->value ? eval(r->value.get()) : Value::any();
                } else
                    last = exec(s);
            }
        }
    } catch (ReturnEx& r) {
        if (c.body) runLeavePhasers(*c.body);
        tctx_.cur = saved; tctx_.curStateEnv = savedState; tctx_.dynStack.pop_back();
        copyOutRw(c.params, env, rwArgs, false);
        return r.v;
    } catch (RakuError& e) {
        if (catchBlk) {
            tctx_.cur->define("$_", exceptionFor(e));
            tctx_.cur->define("$!", exceptionFor(e));
            try {
                for (auto& s : catchBlk->stmts) exec(s.get());
            } catch (BreakGivenEx&) { /* a when/default matched */ }
            if (c.body) runLeavePhasers(*c.body);
            tctx_.cur = saved; tctx_.curStateEnv = savedState; tctx_.dynStack.pop_back();
            return Value::nil();
        }
        if (c.body) runLeavePhasers(*c.body);
        tctx_.cur = saved; tctx_.curStateEnv = savedState; tctx_.dynStack.pop_back();
        throw;
    } catch (...) {
        if (c.body) runLeavePhasers(*c.body);
        tctx_.cur = saved; tctx_.curStateEnv = savedState; tctx_.dynStack.pop_back();
        throw;
    }
    if (c.body) runLeavePhasers(*c.body);
    tctx_.cur = saved; tctx_.curStateEnv = savedState; tctx_.dynStack.pop_back();
    copyOutRw(c.params, env, rwArgs, false);
    return last;
}

// Copy `is rw` parameter final values back to the caller's argument lvalues.
void Interpreter::copyOutRw(const std::vector<Param>* params, std::shared_ptr<Env>& env,
                            const std::vector<ExprPtr>* rwArgs, bool /*methodCtx*/) {
    if (!params || !rwArgs) return;
    // `is rw` params write back explicitly; a sigilless capture param (`\a`) binds
    // the caller's container, so an assignment through it must write back too
    // (copying back an unchanged value is a harmless no-op).
    bool any = false;
    for (auto& p : *params) if (p.isRw || p.sigil == '\\') any = true;
    if (!any) return;
    size_t pi = 0;
    for (auto& p : *params) {
        if (p.named) continue;
        if (p.slurpy) break;
        if ((p.isRw || p.sigil == '\\') && pi < rwArgs->size()) {
            auto it = env->vars.find(p.name);
            if (it != env->vars.end()) {
                try { if (Value* lv = lvalue((*rwArgs)[pi].get())) *lv = it->second; } catch (...) {}
            }
        }
        pi++;
    }
}

Value Interpreter::invokeMethodChain(const std::string& name, ClassInfo* startCls, const Value& self,
                                     ValueList args, const std::vector<ExprPtr>* rwArgs) {
    ClassInfo* owner = nullptr;
    Value* um = startCls ? startCls->findMethod(name, &owner) : nullptr;
    if (!um)
        throw RakuError{Value::typeObj("X::Method::NotFound"),
                        "No next method '" + name + "' to redispatch to (callsame/nextsame)"};
    // Only set up a redispatch frame when an ancestor also defines this method — most
    // calls have no override chain and shouldn't pay for a pushed closure.
    ClassInfo* nextStart = owner ? owner->parent.get() : nullptr;
    if (!nextStart || !nextStart->findMethod(name))
        return invokeMethod(*um, self, args, rwArgs);
    RedispatchCtx rc;
    rc.sameArgs = args;
    rc.fromChain = true; // marks the parent-class deferral frame for multi-method exhaustion
    Value selfCopy = self;
    rc.next = [this, name, nextStart, selfCopy, rwArgs](ValueList na) -> Value {
        return invokeMethodChain(name, nextStart, selfCopy, std::move(na), rwArgs);
    };
    rc.restart = [this, name, selfCopy, rwArgs](ValueList na) -> Value {
        return methodCall(selfCopy, name, std::move(na), rwArgs);  // samewith: re-dispatch from the top
    };
    redispatchStack_.push_back(std::move(rc));
    Value r;
    try { r = invokeMethod(*um, self, args, rwArgs); }
    catch (...) { redispatchStack_.pop_back(); throw; }
    redispatchStack_.pop_back();
    return r;
}

Value Interpreter::invokeMethod(const Value& codeVal, const Value& self, ValueList args, const std::vector<ExprPtr>* rwArgs) {
    if (codeVal.t != VT::Code || !codeVal.code) return Value::any();
    DepthGuard guard(tctx_.callDepth);
    Callable& c = *codeVal.code;
    if (c.isMultiDispatcher) {
        // Mirror the multi-sub dispatcher: push a redispatch frame around the
        // chosen candidate so callsame/nextsame (→ next candidate) and
        // samewith/nextwith (→ re-dispatch from the top) work inside multi methods.
        auto visited = std::make_shared<std::vector<const Value*>>();
        Value dispatcherVal = codeVal;
        Value selfCopy = self;
        // If we were reached via invokeMethodChain (a parent class also defines this
        // method), that frame is on top of the stack now — capture its `next` so that
        // when our same-class candidates are exhausted, nextsame/callsame defers up
        // the inheritance tree instead of returning Nil.
        std::function<Value(ValueList)> parentNext;
        if (!redispatchStack_.empty() && redispatchStack_.back().fromChain && !redispatchStack_.back().lastcall)
            parentNext = redispatchStack_.back().next;
        std::function<Value(ValueList)> dispatch =
            [this, &c, dispatcherVal, selfCopy, rwArgs, visited, parentNext, &dispatch](ValueList as) -> Value {
            const Value* best = nullptr; int bestScore = -1;
            for (auto& cand : c.candidates) {
                bool seen = false; for (auto* v : *visited) if (v == &cand) { seen = true; break; }
                if (seen) continue;
                int s = scoreCandidate(cand, as);
                if (s > bestScore) { bestScore = s; best = &cand; }
            }
            if (!best || bestScore < 0) {
                if (!visited->empty()) {                     // ran past the last same-class candidate
                    if (parentNext) return parentNext(as);   // defer up the inheritance tree
                    return Value::nil();
                }
                throw RakuError{Value::str("X::Multi::NoMatch"),
                                "No matching multi candidate for method " + c.name};
            }
            visited->push_back(best);
            RedispatchCtx rc;
            rc.sameArgs = as;
            rc.next = [&dispatch](ValueList na) -> Value { return dispatch(std::move(na)); };
            rc.restart = [this, dispatcherVal, selfCopy, rwArgs](ValueList na) -> Value {
                return invokeMethod(dispatcherVal, selfCopy, std::move(na), rwArgs);
            };
            redispatchStack_.push_back(std::move(rc));
            Value r;
            try { r = invokeMethod(*best, selfCopy, as, rwArgs); }
            catch (...) { redispatchStack_.pop_back(); throw; }
            redispatchStack_.pop_back();
            return r;
        };
        return dispatch(std::move(args));
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
    auto saved = tctx_.cur;
    tctx_.cur = env;
    Value last = Value::any();
    try {
        if (c.body) {
            size_t nst = c.body->size();
            for (size_t i = 0; i < nst; i++) {
                auto* s = (*c.body)[i].get();
                if (i + 1 == nst && s->kind == NK::ReturnStmt) { // tail return: no unwind
                    auto* r = static_cast<ReturnStmt*>(s);
                    last = r->value ? eval(r->value.get()) : Value::any();
                } else
                    last = exec(s);
            }
        }
    } catch (ReturnEx& r) { tctx_.cur = saved; copyOutRw(c.params, env, rwArgs, true); return r.v; }
    catch (...) { tctx_.cur = saved; throw; }
    tctx_.cur = saved;
    copyOutRw(c.params, env, rwArgs, true);
    return last;
}

Value Interpreter::evalInterp(InterpStr* s) {
    std::string out;
    for (auto& p : s->parts) out += strOf(eval(p.get())); // honour user `method Str`/`gist`
    return Value::str(out);
}

Value* Interpreter::lvalue(Expr* e) {
    if (e->kind == NK::VarExpr) {
        auto* ve = static_cast<VarExpr*>(e);
        char sigil = ve->name.empty() ? '$' : ve->name[0];
        if (ve->declare) {
            if (ve->declScope == "state" && tctx_.curStateEnv) { // persistent across calls
                if (!tctx_.curStateEnv->vars.count(ve->name)) tctx_.curStateEnv->define(ve->name, typedDefault(ve->declType, sigil));
                return &tctx_.curStateEnv->vars[ve->name];
            }
            tctx_.cur->define(ve->name, typedDefault(ve->declType, sigil));
            return &tctx_.cur->vars[ve->name];
        }
        // attribute lvalue: $.x / $!x
        if (ve->name.size() > 2 && (ve->name[1] == '.' || ve->name[1] == '!')) {
            Value* selfp = tctx_.cur->find("self");
            if (selfp && selfp->t == VT::Object && selfp->obj)
                return &selfp->obj->attrs[ve->name.substr(2)];
        }
        Value* p = tctx_.cur->find(ve->name);
        if (p) return p;
        if (!isSpecialVar(ve->name))
            throw RakuError{Value::typeObj("X::Undeclared"),
                            "Variable '" + ve->name + "' is not declared"};
        tctx_.cur->define(ve->name, defaultFor(sigil));
        return &tctx_.cur->vars[ve->name];
    }
    if (e->kind == NK::SymbolicRef) {
        auto* sr = static_cast<SymbolicRef*>(e);
        std::string nm;
        if (sr->nameExpr) nm = eval(sr->nameExpr.get()).toStr();
        if (!sr->sigil.empty()) nm = sr->sigil + nm;
        if (nm.empty())
            throw RakuError{Value::typeObj("X::NoSuchSymbol"), "Cannot look up empty name"};
        VarExpr tmp(nm); tmp.line = e->line;
        return lvalue(&tmp);
    }
    if (e->kind == NK::Assign) {
        // `(my $a = …)<key> = v` — a parenthesized assignment is an lvalue via its target
        auto* a = static_cast<Assign*>(e);
        evalAssign(a);
        return lvalue(a->target.get());
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
        if (base->t == VT::Object && base->obj) {
            // a public accessor is read-only unless declared `is rw`
            for (ClassInfo* ci = base->obj->cls.get(); ci; ci = ci->parent.get())
                for (auto& at : ci->attrs)
                    if (at.name == mc->method && at.pub && !at.rw)
                        throw RakuError{Value::typeObj("X::Assignment::RO"),
                            "Cannot modify an immutable '" + mc->method + "'"};
            return &base->obj->attrs[mc->method];
        }
    }
    // sigilless variable used as an lvalue (`my \a := $a; a = 10`): a bareword that
    // resolves to a bound lexical is assignable through its slot.
    if (e->kind == NK::NameTerm) {
        auto* nt = static_cast<NameTerm*>(e);
        if (Value* p = tctx_.cur->find(nt->name)) return p;
    }
    throw RakuError{Value::str("Cannot assign"), "Target is not assignable"};
}

Value applyArith(const std::string& op, const Value& l, const Value& r);

// In value context (assignment RHS, colon-pair value), a bare `/pat/` is a Regex
// OBJECT, not an immediate match against $_ — so `:err(/pat/)` and `my $rx = /pat/`
// store a Regex that can be smartmatched later.
Value Interpreter::evalValueOf(Expr* e) {
    if (e && e->kind == NK::RegexLit) return Value::regex(static_cast<RegexLit*>(e)->pattern);
    return eval(e);
}

Value Interpreter::evalAssign(Assign* a, bool sink) {
    if (a->op == "=" && a->target->kind == NK::ListExpr) {
        auto* lst = static_cast<ListExpr*>(a->target.get());
        Value rhs = eval(a->value.get());
        // one-level list flattening (Raku): a List/Range spreads, but an itemized
        // `[...]` Array stays one element — so `my ($a,$b) = M, [7,8]` gives $b = [7,8].
        auto spread = [](const Value& r) -> ValueList {
            ValueList vals;
            if (r.t == VT::Array && r.arr) {
                for (auto& it : *r.arr) {
                    if (it.t == VT::Range) { for (auto& e : it.flatten()) vals.push_back(e); }
                    else if (it.t == VT::Array && it.isList && it.arr) { for (auto& e : *it.arr) vals.push_back(e); }
                    else vals.push_back(it);
                }
            } else if (r.t == VT::Range) vals = r.flatten();
            else vals.push_back(r);
            return vals;
        };
        // Bind positionally; a nested list target (`my (\a, (\b, \c))`) recursively
        // destructures the corresponding element.
        std::function<void(ListExpr*, const Value&)> bind = [&](ListExpr* L, const Value& r) {
            ValueList vals = spread(r);
            for (size_t i = 0; i < L->items.size(); i++) {
                Value v = (i < vals.size()) ? vals[i] : Value::any();
                if (L->items[i]->kind == NK::ListExpr) bind(static_cast<ListExpr*>(L->items[i].get()), v);
                else { Value* lv = lvalue(L->items[i].get()); *lv = v; }
            }
        };
        bind(lst, rhs);
        return rhs;
    }

    char sigil = '$';
    if (a->target->kind == NK::VarExpr) {
        auto* ve = static_cast<VarExpr*>(a->target.get());
        if (!ve->name.empty()) sigil = ve->name[0];
        // `state $x = INIT` initializes ONCE: if already set from a prior call, skip re-init
        if (a->op == "=" && ve->declare && ve->declScope == "state" && tctx_.curStateEnv && tctx_.curStateEnv->vars.count(ve->name))
            return tctx_.curStateEnv->vars[ve->name];
    }

    if (a->op == "=" || a->op == ":=") {
        // quanthash element assignment: $sh<k> = False deletes from a SetHash,
        // $bh<k> = 0 deletes from a BagHash/MixHash; true/nonzero (re)sets
        if (a->op == "=" && a->target->kind == NK::Index &&
            static_cast<Index*>(a->target.get())->isHash) {
            auto* ix = static_cast<Index*>(a->target.get());
            Value* bp = nullptr;
            try { bp = lvalue(ix->base.get()); } catch (RakuError&) {}
            if (bp && bp->t == VT::Hash && bp->hash &&
                (bp->hashKind == "SetHash" || bp->hashKind == "BagHash" || bp->hashKind == "MixHash")) {
                std::string key = eval(ix->index.get()).toStr();
                Value rhs = evalValueOf(a->value.get());
                bool del = bp->hashKind == "SetHash" ? !rhs.truthy() : rhs.toNum() == 0.0;
                if (del) bp->hash->erase(key);
                else (*bp->hash)[key] = bp->hashKind == "SetHash" ? Value::boolean(true)
                                      : bp->hashKind == "BagHash" ? Value::integer(rhs.toInt()) : rhs;
                return sink ? Value::any() : rhs;
            }
        }
        Value rhs = evalValueOf(a->value.get()); // `$rx = /pat/` stores a Regex object
        // coercion-type container `my Int(Str) $x = '42'`: coerce the value to the target
        if (a->op == "=" && a->target->kind == NK::VarExpr) {
            const std::string& ct = static_cast<VarExpr*>(a->target.get())->declCoerce;
            if (!ct.empty()) { ValueList none; rhs = methodCall(rhs, ct, none); }
        }
        Value* lv = lvalue(a->target.get());
        // A Proxy container routes `= x` through its STORE method (`:=` still rebinds).
        if (a->op == "=" && lv->t == VT::Hash && lv->hashKind == "Proxy" && lv->hash) {
            auto it = lv->hash->find("STORE");
            if (it != lv->hash->end()) { Value r = callCallable(it->second, { rhs }); return sink ? Value::any() : r; }
        }
        int nb = lv->natBits; bool ns = lv->natSigned; // native-int container: preserve width & wrap
        if (sigil == '@') *lv = coerceArray(rhs);
        else if (sigil == '%') *lv = coerceHash(rhs);
        else *lv = rhs;
        if (nb) wrapNative(*lv, nb, ns);
        return sink ? Value::any() : *lv;
    }

    // compound assignment
    Value* lv = lvalue(a->target.get());
    Value rhs = eval(a->value.get());
    int nb = lv->natBits; bool ns = lv->natSigned;
    std::string binop = a->op.substr(0, a->op.size() - 1); // strip '='
    if (binop == "||") { if (!lv->truthy()) *lv = rhs; return sink ? Value::any() : *lv; }
    if (binop == "&&") { if (lv->truthy()) *lv = rhs; return sink ? Value::any() : *lv; }
    if (binop == "//") { if (!isDefined(*lv)) *lv = rhs; return sink ? Value::any() : *lv; }
    // `$obj OP= x` reuses a user `sub infix:<OP>` overload (Raku's `is deep` also
    // auto-generates OP= from OP), falling back to the built-in operator.
    bool overloaded = false;
    if (lv->t == VT::Object || rhs.t == VT::Object)
        if (Value* f = tctx_.cur->find("&infix:<" + binop + ">"))
            try { *lv = callCallable(*f, ValueList{*lv, rhs}); overloaded = true; }
            catch (RakuError&) {}
    // An undefined target autovivifies with the operator's NEUTRAL element:
    // `my $x; $x *= 2` is 1*2 == 2, `+=` starts from 0, `~=` from ''.
    if (!overloaded && (lv->t == VT::Any || lv->t == VT::Nil || lv->t == VT::Type)) {
        if (binop == "*" || binop == "**" || binop == "%%") *lv = Value::integer(1);
        else if (binop == "~" || binop == "x") *lv = Value::str("");
        else if (binop == "+" || binop == "-") *lv = Value::integer(0);
    }
    // `$s ~= …` appends into the existing buffer instead of rebuilding the whole
    // string each step — O(n) string building instead of O(n²).
    if (!overloaded && binop == "~" && lv->t == VT::Str && rhs.t == VT::Str) {
        lv->s += rhs.s;
        return sink ? Value::any() : *lv;
    }
    if (!overloaded) {
        // fall back to a user `sub infix:<OP>` when the operator isn't built-in
        // (so `$m mx= 9` works for any operands, not just objects)
        try { *lv = applyArith(binop, *lv, rhs); }
        catch (RakuError&) {
            if (Value* f = tctx_.cur->find("&infix:<" + binop + ">")) *lv = callCallable(*f, ValueList{*lv, rhs});
            else throw;
        }
    }
    if (nb) wrapNative(*lv, nb, ns);
    return sink ? Value::any() : *lv;
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
    // Hot path: 1–2-char arithmetic/comparison ops on plain Int/Int — the
    // overwhelmingly common case — dispatched by a single char, skipping the
    // set-op / hyper-metaop / boxed-object string-compare machinery below. Any
    // case it doesn't return (overflow, div-by-zero, other ops) falls through to
    // the general path, so results are identical.
    if (l.t == VT::Int && r.t == VT::Int && !l.big && !r.big && !op.empty() && op.size() <= 2) {
        long long a = l.i, b = r.i, z;
        char c0 = op[0], c1 = op.size() > 1 ? op[1] : '\0';
        switch (c0) {
            case '+': if (c1 == '\0' && !__builtin_add_overflow(a, b, &z)) return Value::integer(z); break;
            case '-': if (c1 == '\0' && !__builtin_sub_overflow(a, b, &z)) return Value::integer(z); break;
            case '*': if (c1 == '\0' && !__builtin_mul_overflow(a, b, &z)) return Value::integer(z); break;
            case '<': if (c1 == '\0') return Value::boolean(a < b);
                      if (c1 == '=') return Value::boolean(a <= b); break;
            case '>': if (c1 == '\0') return Value::boolean(a > b);
                      if (c1 == '=') return Value::boolean(a >= b); break;
            case '=': if (c1 == '=') return Value::boolean(a == b); break;
            case '!': if (c1 == '=') return Value::boolean(a != b); break;
            case '%': if (c1 == '\0' && b != 0) { long long m = a % b; if (m && ((m < 0) != (b < 0))) m += b; return Value::integer(m); } break;
        }
    }
    // A `but`/`does` mixin over a non-object base delegates value ops to the boxed
    // value — but identity/smartmatch/type ops must still see the object itself.
    if (op != "~~" && op != "!~~" && op != "===" && op != "!==" && op != "!===" && op != "=:=" &&
        ((l.t == VT::Object && l.obj && l.obj->hasBoxed) || (r.t == VT::Object && r.obj && r.obj->hasBoxed))) {
        Value lu = (l.t == VT::Object && l.obj && l.obj->hasBoxed) ? l.obj->boxed : l;
        Value ru = (r.t == VT::Object && r.obj && r.obj->hasBoxed) ? r.obj->boxed : r;
        return applyArith(op, lu, ru);
    }
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
    if (op == "..." || op == "...^") { // simple integer sequence (closure/list seeds handled in evalBinary)
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
        // each `*` consumes one argument left-to-right, so `* + *` has arity 2
        auto arityOf = [](const Value& v) -> long long {
            if (v.t == VT::Whatever) return 1;
            if (v.t == VT::Code && v.code && v.code->isWhateverCode) return v.code->whateverArity > 0 ? v.code->whateverArity : 1;
            return 0;
        };
        code.code->whateverArity = arityOf(l) + arityOf(r);
        std::string opc = op; Value lc = l, rc = r;
        code.code->builtin = [opc, lc, rc](Interpreter& I, ValueList& a) -> Value {
            size_t idx = 0;
            auto resolve = [&](const Value& v) -> Value {
                if (v.t == VT::Whatever) return idx < a.size() ? a[idx++] : Value::any();
                if (v.t == VT::Code && v.code && v.code->isWhateverCode) {
                    long long ar = v.code->whateverArity > 0 ? v.code->whateverArity : 1;
                    ValueList sub;
                    for (long long k = 0; k < ar && idx < a.size(); k++) sub.push_back(a[idx++]);
                    return I.callCallable(v, sub);
                }
                return v;
            };
            Value lv = resolve(lc); // resolve left before right — argument order matters
            Value rv = resolve(rc);
            return applyArith(opc, lv, rv);
        };
        return code;
    }

    // ---- Date arithmetic ----
    if (isDateVal(l) && isDateVal(r) && op == "-") return Value::integer(dateDays(l) - dateDays(r));
    if (isDateVal(l) && (r.t == VT::Int || r.t == VT::Bool) && (op == "+" || op == "-"))
        return makeDate(dateDays(l) + (op == "+" ? r.toInt() : -r.toInt()));
    if (isDateVal(r) && l.t == VT::Int && op == "+") return makeDate(dateDays(r) + l.toInt());

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
        if (op == "=~=" || op == "≅") { // approx-equal in the complex plane
            if (a == b) return Value::boolean(true);
            double scale = std::max(std::abs(a), std::abs(b));
            return Value::boolean(std::abs(a - b) <= 1e-15 * scale);
        }
        if (op == "cmp") { // by real part, then imaginary part; NaN sorts as More
            auto ncmp = [](double x, double y) {
                if (std::isnan(x)) return std::isnan(y) ? 0 : 1;
                if (std::isnan(y)) return -1;
                return x < y ? -1 : x > y ? 1 : 0;
            };
            int c = ncmp(a.real(), b.real()); if (!c) c = ncmp(a.imag(), b.imag());
            return Value::enumVal(c < 0 ? "Less" : c > 0 ? "More" : "Same", c);
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "<=>") {
            // arithmetic comparison coerces to Real: ok when |im| is within
            // $*TOLERANCE (relative), so exp(i*π) <=> -1 is Same — else it throws
            double tol = Interpreter::toleranceDyn();
            auto toReal = [&](const std::complex<double>& z, const Value& orig) -> double {
                if (std::fabs(z.imag()) > tol * std::max(1.0, std::fabs(z.real())))
                    throw RakuError{Value::typeObj("X::Numeric::Real"),
                                    "Cannot convert " + orig.toStr() + " to Real: imaginary part not zero"};
                return z.real();
            };
            return applyArith(op, Value::number(toReal(a, l)), Value::number(toReal(b, r)));
        }
    }

    // ---- Version comparison (S03): parts split on separators and digit/alpha
    // boundaries; numeric parts compare numerically (leading zeros dropped),
    // missing/trailing parts count as 0, '*' (Whatever) matches anything,
    // alpha parts sort before numeric (pre-release convention). ----
    if ((l.hashKind == "Version" || r.hashKind == "Version") &&
        l.t == VT::Str && r.t == VT::Str &&
        (op == "cmp" || op == "==" || op == "!=" || op == "<" || op == "<=" ||
         op == ">" || op == ">=" || op == "eqv" || op == "before" || op == "after" ||
         op == "~~" || op == "eq" || op == "ne")) {
        auto parts = [](const std::string& s) {
            std::vector<std::pair<bool, std::string>> out; // {isNumeric, text}
            size_t i = 0;
            while (i < s.size()) {
                unsigned char c = s[i];
                if (std::isdigit(c)) {
                    size_t j = i; while (j < s.size() && std::isdigit((unsigned char)s[j])) j++;
                    std::string d = s.substr(i, j - i);
                    size_t nz = d.find_first_not_of('0');
                    out.push_back({true, nz == std::string::npos ? "0" : d.substr(nz)});
                    i = j;
                } else if (std::isalpha(c)) {
                    size_t j = i; while (j < s.size() && std::isalpha((unsigned char)s[j])) j++;
                    out.push_back({false, s.substr(i, j - i)});
                    i = j;
                } else if (c == '*') { out.push_back({false, "*"}); i++; }
                else i++; // separator: . - + / _
            }
            return out;
        };
        auto pa = parts(l.s), pb = parts(r.s);
        int c = 0;
        size_t n = pa.size() > pb.size() ? pa.size() : pb.size();
        for (size_t k = 0; k < n && !c; k++) {
            std::pair<bool, std::string> a = k < pa.size() ? pa[k] : std::make_pair(true, std::string("0"));
            std::pair<bool, std::string> b = k < pb.size() ? pb[k] : std::make_pair(true, std::string("0"));
            if (a.second == "*" || b.second == "*") continue;
            if (a.first && b.first)
                c = a.second.size() != b.second.size()
                      ? (a.second.size() < b.second.size() ? -1 : 1)
                      : (a.second < b.second ? -1 : a.second > b.second ? 1 : 0);
            else if (!a.first && !b.first)
                c = a.second < b.second ? -1 : a.second > b.second ? 1 : 0;
            else c = a.first ? 1 : -1; // alpha before numeric
        }
        if (op == "cmp") return Value::enumVal(c < 0 ? "Less" : c > 0 ? "More" : "Same", c);
        if (op == "==" || op == "eqv" || op == "eq" || op == "~~") return Value::boolean(c == 0);
        if (op == "!=" || op == "ne") return Value::boolean(c != 0);
        if (op == "<"  || op == "before") return Value::boolean(c < 0);
        if (op == ">"  || op == "after")  return Value::boolean(c > 0);
        if (op == "<=") return Value::boolean(c <= 0);
        return Value::boolean(c >= 0); // >=
    }
    // ---- exact numeric tower: Int (bignum) and Rat ----
    auto isExact = [](const Value& v) { return v.t == VT::Int || v.t == VT::Bool || v.t == VT::Rat; };
    // an undefined operand (Any/Nil/bare type object) numifies to 0 in arithmetic,
    // keeping exactness: `my $a += 0.1` is a Rat, `my Int $x; $x += 5` works
    if (op == "+" || op == "-" || op == "*" || op == "/" || op == "**") {
        auto undef = [](const Value& v) { return v.t == VT::Any || v.t == VT::Nil || v.t == VT::Type; };
        if (undef(l) && isExact(r)) return applyArith(op, Value::integer(0), r);
        if (undef(r) && isExact(l)) return applyArith(op, l, Value::integer(0));
    }
    if (isExact(l) && isExact(r)) {
        bool anyRat = (l.t == VT::Rat || r.t == VT::Rat);
        bool smallInt = !anyRat && !l.big && !r.big;
        // a FatRat operand keeps the result a FatRat (type identity is contagious)
        bool fat = (l.t == VT::Rat && l.fatRat) || (r.t == VT::Rat && r.fatRat);
        auto mkRat = [&](BigInt n, BigInt d) {
            Value v = Value::rat(std::move(n), std::move(d)); v.fatRat = fat;
            // Rat denominators are capped at uint64: arithmetic that would grow
            // one past that spills to Num (FatRat is arbitrary-precision, never spills).
            if (!fat && v.ratD && !v.ratD->fitsU64()) return Value::number(v.toNum());
            return v;
        };
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
            return mkRat(n, d);
        }
        if (op == "/") {
            BigInt n1 = getN(l), d1 = getD(l), n2 = getN(r), d2 = getD(r);
            if (n2.isZero()) { // 1/0 is a zero-denominator Rat: Num → ±Inf, Str throws
                Value v = Value::ratZ(n1 * d2, BigInt(0)); v.fatRat = fat; return v;
            }
            return mkRat(n1 * d2, d1 * n2);
        }
        if (op == "**" && (r.t == VT::Int || r.t == VT::Bool)) {
            // a huge exponent produces an impractically large number: Rakudo
            // throws rather than compute it (base 0/±1 are trivial and excepted).
            // (A01-limits/overflow.t test 10 wants a Rat**10**8 to instead grind
            // exactly until a racing Promise exits the process — an implementation-
            // speed assertion we deliberately don't chase.)
            bool baseTrivial = l.t == VT::Int && !l.big && (l.toInt() == 0 || l.toInt() == 1 || l.toInt() == -1);
            bool hugeExp = (bool)r.big || std::llabs(r.toInt()) > 900000;
            if (!baseTrivial && hugeExp) {
                bool neg = !r.big && r.toInt() < 0;
                throw RakuError{Value::typeObj(neg ? "X::Numeric::Underflow" : "X::Numeric::Overflow"),
                                neg ? "Numeric underflow" : "Numeric overflow"};
            }
        }
        if (op == "**" && (r.t == VT::Int || r.t == VT::Bool) && !r.big) {
            long long e = r.toInt();
            BigInt bn = getN(l), bd = getD(l);
            if (e >= 0) { BigInt rn = bn.pow(e), rd = bd.pow(e); return anyRat ? mkRat(rn, rd) : Value::bigint(rn); }
            BigInt rn = bd.pow(-e), rd = bn.pow(-e);
            if (rd.isZero()) return Value::typeObj("Failure");
            return mkRat(rn, rd);
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
            // Raku `div` floors (rounds toward -∞); BigInt::divmod truncates toward
            // zero, so adjust when the remainder is nonzero and the signs differ.
            if (op == "div") {
                if (!rem.isZero() && ((a.sign < 0) != (b.sign < 0))) q = q - BigInt(1);
                return Value::bigint(q);
            }
            if (!rem.isZero() && ((rem.sign < 0) != (b.sign < 0))) rem = rem + b; // sign follows divisor
            if (op == "%%") return Value::boolean(rem.isZero());
            return Value::bigint(rem);
        }
        if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=" ||
            op == "<=>" || op == "cmp" || op == "leg") {
            // zero-denominator Rats compare as their Num (±Inf / NaN; NaN == NaN is False)
            auto zeroDen = [](const Value& v) { return v.t == VT::Rat && v.ratD && v.ratD->isZero(); };
            if (zeroDen(l) || zeroDen(r))
                return applyArith(op, Value::number(l.toNum()), Value::number(r.toNum()));
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
        a.isList = true; // `1 xx 5` is a flattening List, so `[1 xx 5]` spreads to 5 elems
        return a;
    }
    // numeric bitwise / shift
    if (op == "+&") return Value::integer(l.toInt() & r.toInt());
    if (op == "+|") return Value::integer(l.toInt() | r.toInt());
    if (op == "+^") return Value::integer(l.toInt() ^ r.toInt());
    if (op == "+<") return Value::integer(l.toInt() << r.toInt());
    if (op == "+>") return Value::integer(l.toInt() >> r.toInt());
    // boolean bitwise (return Bool)
    if (op == "?&") return Value::boolean(l.truthy() && r.truthy());
    if (op == "?|") return Value::boolean(l.truthy() || r.truthy());
    if (op == "?^") return Value::boolean(l.truthy() != r.truthy());
    // string bitwise — per-character on codepoints; the longer string's tail is
    // kept for | and ^, dropped for &
    if (op == "~&" || op == "~|" || op == "~^") {
        std::string a = l.toStr(), b = r.toStr(), out;
        size_t n = (op == "~&") ? std::min(a.size(), b.size()) : std::max(a.size(), b.size());
        for (size_t k = 0; k < n; k++) {
            unsigned char ca = k < a.size() ? (unsigned char)a[k] : 0;
            unsigned char cb = k < b.size() ? (unsigned char)b[k] : 0;
            out += (char)(op == "~&" ? (ca & cb) : op == "~|" ? (ca | cb) : (ca ^ cb));
        }
        return Value::str(out);
    }
    if (op == "gcd") { long long x = std::llabs(l.toInt()), y = std::llabs(r.toInt()); while (y) { long long t = x % y; x = y; y = t; } return Value::integer(x); }
    if (op == "lcm") { long long x = l.toInt(), y = r.toInt(); if (!x || !y) return Value::integer(0); long long g = std::llabs(x), h = std::llabs(y); while (h) { long long t = g % h; g = h; h = t; } return Value::integer(std::llabs(x / g * y)); }
    if (op == "min") return valueCmp(l, r) <= 0 ? l : r;
    if (op == "max") return valueCmp(l, r) >= 0 ? l : r;

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
    if (op == "unicmp" || op == "coll") { // UCA collation (DUCET) over the two strings
        auto decode = [](const std::string& str) {
            std::vector<uint32_t> cps;
            for (size_t i = 0; i < str.size(); ) {
                unsigned char b = str[i];
                int len = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 1;
                uint32_t cp = len == 1 ? b : (uint32_t)(b & (0xFF >> (len + 1)));
                for (int k = 1; k < len && i + k < str.size(); k++) cp = (cp << 6) | ((unsigned char)str[i + k] & 0x3F);
                cps.push_back(cp); i += len;
            }
            return cps;
        };
        int c = uniCollate(decode(l.toStr()), decode(r.toStr()));
        return Value::enumVal(c < 0 ? "Less" : c > 0 ? "More" : "Same", c);
    }
    if (op == "before") return Value::boolean(valueCmp(l, r) < 0);
    if (op == "after") return Value::boolean(valueCmp(l, r) > 0);
    if (op == "eqv") return Value::boolean(valueEqv(l, r));
    if (op == "===" || op == "!==" || op == "!===") {
        bool same;
        if (l.t != r.t) same = false;
        else if (l.t == VT::Object) same = (l.obj == r.obj);
        else if (l.t == VT::Type) same = (l.s == r.s && l.ofType == r.ofType);
        else if (l.t == VT::Code) same = (l.code == r.code);
        else if (l.t == VT::Array) same = (l.arr == r.arr); // Lists/Arrays: reference identity
        else if (l.t == VT::Hash) same = l.hashKind.empty() ? (l.hash == r.hash) // plain Hash: reference
                                                            : (l.toStr() == r.toStr()); // Set/Bag/Mix: value
        else same = (l.toStr() == r.toStr()); // value types (Int/Str/Num/Rat/...)
        return Value::boolean(op == "===" ? same : !same); // !== and !=== both negate identity
    }
    if (op == "%%") { long long b = r.toInt(); return Value::boolean(b != 0 && l.toInt() % b == 0); }
    if (op == "=:=") return Value::boolean(l.t == r.t && valueEq(l, r));
    if (op == "~~" || op == "!~~") {
        bool res;
        if (!r.enumType.empty() && r.t == VT::Array) {
            // $val ~~ EnumType : the enum type object is a tagged pair-list
            res = (!l.enumType.empty() && l.enumType == r.enumType) || l.typeName() == r.enumType;
            return Value::boolean(op == "~~" ? res : !res);
        }
        if (r.t == VT::Range) {
            if (l.t == VT::Rat) { // exact endpoint compare: 4.99…(45 digits) ~~ 0..^5
                res = applyArith(r.rExFrom ? ">" : ">=", l, Value::integer(r.rFrom)).truthy() &&
                      applyArith(r.rExTo ? "<" : "<=", l, Value::integer(r.rTo)).truthy();
            } else {
                double v = l.toNum();
                double lo = r.rFrom, hi = r.rTo;
                res = v >= lo && (r.rExTo ? v < hi : v <= hi);
            }
        } else if (r.t == VT::Type) {
            res = (l.typeName() == r.s) || r.s == "Any" || r.s == "Mu" ||
                  (r.s == "Numeric" && l.isNumeric()) || (r.s == "Cool") ||
                  (r.s == "Exception" && l.typeName().rfind("X::", 0) == 0) || // every X::* isa Exception
                  (l.t == VT::Hash && l.hashKind == "FileHandle" && (r.s == "IO::Handle" || r.s == "IO"));
            // role / container types (Positional, Associative, …) that a value does
            if (!res) {
                if ((r.s == "Positional" || r.s == "Iterable") && l.t == VT::Array) res = true;
                else if (r.s == "Iterable" && l.t == VT::Range) res = true;
                else if ((r.s == "Associative" || r.s == "Map") && l.t == VT::Hash) res = true;
                else if (r.s == "Callable" && l.t == VT::Code) res = true;
                else if (r.s == "Stringy" && l.t == VT::Str) res = true;
                else if (r.s == "Real" && l.isNumeric() && l.t != VT::Complex) res = true;
            }
            // native numeric types conform to Num/Int/Numeric/Real/Cool (`num64 ~~ Num`)
            if (!res && l.t == VT::Type) {
                static const std::set<std::string> natNum = {"num", "num32", "num64"};
                static const std::set<std::string> natInt = {"int", "int8", "int16", "int32", "int64",
                    "uint", "uint8", "uint16", "uint32", "uint64", "byte"};
                bool numeric = r.s == "Numeric" || r.s == "Real" || r.s == "Cool" || r.s == "Any" || r.s == "Mu";
                if ((r.s == "Num" || numeric) && natNum.count(l.s)) res = true;
                else if ((r.s == "Int" || numeric) && natInt.count(l.s)) res = true;
            }
            // object: match against its class / ancestor names, then composed roles
            if (!res && l.t == VT::Object && l.obj)
                for (ClassInfo* ci = l.obj->cls.get(); ci; ci = ci->parent.get())
                    if (ci->name == r.s) { res = true; break; }
            if (!res && l.t == VT::Object && l.obj && l.obj->cls && l.obj->cls->doesRole(r.s))
                res = true;
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
            if (!res) { // NaN ~~ NaN is True (ACCEPTS special-cases NaN, unlike ==)
                auto isnanV = [](const Value& v) {
                    return v.t == VT::Complex ? (std::isnan(v.n) || std::isnan(v.im))
                                              : (v.t == VT::Num && std::isnan(v.n));
                };
                res = isnanV(l) && isnanV(r);
            }
        } else if (r.t == VT::Object) {
            res = (l.t == VT::Object && l.obj.get() == r.obj.get()); // object identity
        } else if ((r.t == VT::Int || r.t == VT::Num || r.t == VT::Rat) && l.t == VT::Range) {
            // Range ~~ number: numeric smartmatch on the element count (+(2..4) == 3;
            // 1..Inf counts Inf, so `1..Inf ~~ 1/0` is True)
            double cnt;
            if (l.rTo >= LLONG_MAX - 1 || l.rFrom <= LLONG_MIN + 1) cnt = INFINITY;
            else { long long n = l.rTo - l.rFrom + 1 - (l.rExFrom ? 1 : 0) - (l.rExTo ? 1 : 0);
                   cnt = n < 0 ? 0 : (double)n; }
            res = applyArith("==", Value::number(cnt), r).truthy();
        } else if ((r.t == VT::Int || r.t == VT::Num || r.t == VT::Rat) &&
                   (l.t == VT::Int || l.t == VT::Num || l.t == VT::Rat || l.t == VT::Str || l.t == VT::Bool)) {
            res = applyArith("==", l, r).truthy(); // `$x ~~ 5` : numeric coercion ('05' ~~ 5 is True)
        } else {
            res = valueEq(l, r);
        }
        return Value::boolean(op == "~~" ? res : !res);
    }
    throw RakuError{Value::str("op"), "Unsupported operator '" + op + "'"};
}

// Escape regex metacharacters so an interpolated string matches literally.
static std::string quoteMetaRx(const std::string& s) {
    std::string out;
    for (char c : s) { if (std::strchr(".?*+^$()[]{}|\\<>-", c)) out += '\\'; out += c; }
    return out;
}

Value Interpreter::regexMatch(const std::string& subject, const std::string& pattern) {
    std::string pat = pattern;
    bool global = false;
    { size_t gp = pat.find(":g "); if (gp != std::string::npos) { global = true; pat.erase(gp, 3); } } // :g adverb
    // Interpolate $scalar variables into the pattern as their literal (quotemeta'd)
    // value — `/$x/` matches the contents of $x. Leaves $0.. backrefs, $<name>,
    // special vars, escaped \$, and the end-anchor $ untouched.
    if (pat.find('$') != std::string::npos && tctx_.cur) {
        std::string out;
        for (size_t i = 0; i < pat.size(); i++) {
            if (pat[i] == '\\' && i + 1 < pat.size()) { out += pat[i]; out += pat[i + 1]; i++; continue; }
            if (pat[i] == '$' && i + 1 < pat.size() && (std::isalpha((unsigned char)pat[i + 1]) || pat[i + 1] == '_')) {
                size_t j = i + 1;
                while (j < pat.size() && (std::isalnum((unsigned char)pat[j]) || pat[j] == '_')) j++;
                Value* v = tctx_.cur->find("$" + pat.substr(i + 1, j - i - 1));
                if (v) { out += quoteMetaRx(v->toStr()); i = j - 1; continue; }
            }
            out += pat[i];
        }
        pat = out;
    }
    Regex re(pat);
    // resolve <NAME> subrules against lexical `my regex/token NAME {…}`; unknown names stay
    // lenient (zero-width) so existing patterns with unhandled assertions don't start failing.
    SubResolver resolver;
    resolver = [&](const std::string& name, const std::string& subj, long pos, RxMatch& out) -> bool {
        if (name == "ws") { long p = pos; while (p < (long)subj.size() && std::isspace((unsigned char)subj[p])) p++; out.from = pos; out.to = p; out.matched = true; return true; }
        auto it = namedRegex_.find(name);
        if (it == namedRegex_.end()) { out.from = pos; out.to = pos; out.matched = true; return true; }
        Regex sub(it->second, namedRegexKind_[name] == "rule" ? "s" : "");
        return sub.matchAt(subj, pos, out, resolver);
    };
    auto build = [&](const RxMatch& m) {
        Value v = Value::matchVal(subject.substr(m.from, m.to - m.from), m.from, m.to);
        v.ext = std::make_shared<std::string>(subject); // the original, for .prematch/.postmatch/.orig
        for (auto& c : m.caps) {
            if (c.first < 0) v.arrRef().push_back(Value::nil());
            else v.arrRef().push_back(Value::matchVal(subject.substr(c.first, c.second - c.first), c.first, c.second));
        }
        for (auto& kv : m.named)
            if (!m.children.count(kv.first))
                v.hashRef()[kv.first] = Value::matchVal(subject.substr(kv.second.first, kv.second.second - kv.second.first), kv.second.first, kv.second.second);
        for (auto& kv : m.children) {
            // a capture repeated under a quantifier collates into a list of Matches
            if (kv.second.size() == 1) {
                auto& c = kv.second[0];
                v.hashRef()[kv.first] = Value::matchVal(subject.substr(c.from, c.to - c.from), c.from, c.to);
            } else {
                Value arr = Value::array(); arr.isList = true;
                for (auto& c : kv.second) arr.arr->push_back(Value::matchVal(subject.substr(c.from, c.to - c.from), c.from, c.to));
                v.hashRef()[kv.first] = arr;
            }
        }
        return v;
    };
    if (global) { // m:g// — a List of every match
        Value list = Value::array(); list.isList = true;
        long pos = 0;
        while (re.ok() && pos <= (long)subject.size()) {
            RxMatch m;
            if (!re.search(subject, pos, m, resolver)) break;
            list.arr->push_back(build(m));
            pos = (m.to > m.from) ? m.to : m.to + 1; // advance past zero-width matches
        }
        tctx_.cur->define("$/", list);
        return list;
    }
    RxMatch m;
    Value mv;
    if (re.ok() && re.search(subject, 0, m, resolver)) mv = build(m);
    else mv = Value::nil();
    tctx_.cur->define("$/", mv);
    if (mv.t == VT::Match) {
        if (mv.arr) for (size_t k = 0; k < mv.arr->size(); k++) tctx_.cur->define("$" + std::to_string(k), (*mv.arr)[k]);
        if (mv.hash) for (auto& kv : *mv.hash) tctx_.cur->define("$<" + kv.first + ">", kv.second);
    }
    return mv;
}

// tr///: expand `a..z` ranges in a transliteration spec, then map chars.
static std::string trExpand(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (i + 3 < s.size() && s[i + 1] == '.' && s[i + 2] == '.') {
            for (char c = s[i]; c <= s[i + 3]; c++) out += c;
            i += 3;
        } else out += s[i];
    }
    return out;
}
static std::string translit(const std::string& subj, const std::string& from, const std::string& to, long long& n) {
    std::string f = trExpand(from), t = trExpand(to), out;
    n = 0;
    for (char c : subj) {
        auto pos = f.find(c);
        if (pos != std::string::npos) { n++; out += pos < t.size() ? t[pos] : (t.empty() ? c : t.back()); }
        else out += c;
    }
    return out;
}
// tr-tagged SubstLit: pattern begins with '\x01'. Returns true and fills `result` (count) + `newStr`.
static bool isTrSubst(const std::string& pat) { return !pat.empty() && pat[0] == '\x01'; }

Value Interpreter::regexSubst(const std::string& subject, const std::string& pattern,
                              const std::string& repl, std::string& out, bool& changed) {
    // global if a leading :g adverb was prepended to the pattern
    bool global = false;
    { std::istringstream is(pattern); std::string tok;
      while (is >> tok) { if (tok[0] != ':') break; if (tok == ":g" || tok == ":global") global = true; } }
    // Strip the :g/:global adverb from the pattern (as regexMatch does) so the
    // Regex engine doesn't try to match the literal text ":g".
    std::string pat = pattern;
    for (const char* adv : {":g ", ":global "}) {
        std::string a = adv;
        for (size_t gp = pat.find(a); gp != std::string::npos; gp = pat.find(a)) pat.erase(gp, a.size());
    }
    Regex re(pat);
    out.clear(); changed = false;
    Value last = Value::nil();
    long pos = 0;
    RxMatch m;
    // Build a Match value (positional + named captures) for one raw match.
    auto build = [&](const RxMatch& mm) {
        Value v = Value::matchVal(subject.substr(mm.from, mm.to - mm.from), mm.from, mm.to);
        v.ext = std::make_shared<std::string>(subject); // the original, for .prematch/.postmatch/.orig
        for (auto& c : mm.caps) {
            if (c.first < 0) v.arrRef().push_back(Value::nil());
            else v.arrRef().push_back(Value::matchVal(subject.substr(c.first, c.second - c.first), c.first, c.second));
        }
        for (auto& kv : mm.named)
            if (!mm.children.count(kv.first))
                v.hashRef()[kv.first] = Value::matchVal(subject.substr(kv.second.first, kv.second.second - kv.second.first), kv.second.first, kv.second.second);
        for (auto& kv : mm.children) {
            if (kv.second.size() == 1) {
                auto& c = kv.second[0];
                v.hashRef()[kv.first] = Value::matchVal(subject.substr(c.from, c.to - c.from), c.from, c.to);
            } else {
                Value arr = Value::array(); arr.isList = true;
                for (auto& c : kv.second) arr.arr->push_back(Value::matchVal(subject.substr(c.from, c.to - c.from), c.from, c.to));
                v.hashRef()[kv.first] = arr;
            }
        }
        return v;
    };
    // Replacement is either a `{ CODE }` block (evaluated per match with the
    // captures bound) or a plain/interpolated string.
    std::string rtrim = repl;
    { size_t a = rtrim.find_first_not_of(" \t\n"); size_t b = rtrim.find_last_not_of(" \t\n");
      rtrim = (a == std::string::npos) ? "" : rtrim.substr(a, b - a + 1); }
    bool codeRepl = rtrim.size() >= 2 && rtrim.front() == '{' && rtrim.back() == '}';
    std::string codeInner = codeRepl ? rtrim.substr(1, rtrim.size() - 2) : std::string();
    // A replacement with @-array / %-hash interpolation (e.g. /@code[$0 - 1]/) is a
    // full qq-string — evaluate it as one (captures are bound per match below).
    bool fullInterp = !codeRepl && (repl.find('@') != std::string::npos || repl.find('%') != std::string::npos);
    char qqDelim = 0;
    if (fullInterp) { for (const char* c = "!#|~^,;/"; *c; c++) if (repl.find(*c) == std::string::npos) { qqDelim = *c; break; } if (!qqDelim) fullInterp = false; }
    auto interpolate = [&](const Value& mv) -> std::string {
        // Substitute $/, $0.., $<name> tokens in a non-code replacement.
        std::string r; const std::string& s = repl;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) { r += s[i + 1]; i++; continue; }
            if (s[i] == '$' && i + 1 < s.size()) {
                if (s[i + 1] == '/') { r += mv.toStr(); i++; continue; }
                if (std::isdigit((unsigned char)s[i + 1])) {
                    size_t j = i + 1; std::string num; while (j < s.size() && std::isdigit((unsigned char)s[j])) num += s[j++];
                    long idx = std::stol(num); if (mv.t == VT::Match && mv.arr && idx < (long)mv.arr->size()) r += (*mv.arr)[idx].toStr();
                    i = j - 1; continue;
                }
                if (s[i + 1] == '<') { size_t j = s.find('>', i + 2); if (j != std::string::npos) {
                    std::string nm = s.substr(i + 2, j - i - 2);
                    if (mv.t == VT::Match && mv.hash && mv.hash->count(nm)) r += (*mv.hash)[nm].toStr();
                    i = j; continue; } }
            }
            r += s[i];
        }
        return r;
    };
    while (re.ok() && pos <= (long)subject.size() && re.search(subject, pos, m)) {
        out += subject.substr(pos, m.from - pos);
        Value mv = build(m);
        tctx_.cur->define("$/", mv);
        for (size_t k = 0; k < mv.arr->size(); k++) tctx_.cur->define("$" + std::to_string(k), (*mv.arr)[k]);
        for (auto& kv : *mv.hash) tctx_.cur->define("$<" + kv.first + ">", kv.second);
        if (codeRepl) out += evalString(codeInner).toStr();
        else if (fullInterp) {
            try { out += evalString(std::string("qq") + qqDelim + repl + qqDelim).toStr(); }
            catch (...) { out += interpolate(mv); }
        }
        else out += interpolate(mv);
        changed = true;
        last = mv;
        if (m.to == m.from) { // empty match: emit one char to make progress
            if (m.to < (long)subject.size()) out += subject[m.to];
            pos = m.to + 1;
        } else {
            pos = m.to;
        }
        if (!global) break;
    }
    if (pos < (long)subject.size()) out += subject.substr(pos);
    tctx_.cur->define("$/", last);
    return last;
}

// ---- UTF-8 / grapheme helpers for :samemark / :ignoremark ----
static std::vector<uint32_t> smDecode(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i]; uint32_t cp; int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; n = 4; }
        else { cp = c; n = 1; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
        out.push_back(cp); i += n;
    }
    return out;
}
static std::string smEncode(uint32_t cp) {
    std::string r;
    if (cp < 0x80) r += (char)cp;
    else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 0x3F)); r += (char)(0x80 | (cp & 0x3F)); }
    else { r += (char)(0xF0 | (cp >> 18)); r += (char)(0x80 | ((cp >> 12) & 0x3F)); r += (char)(0x80 | ((cp >> 6) & 0x3F)); r += (char)(0x80 | (cp & 0x3F)); }
    return r;
}
static uint32_t smLower(uint32_t c) {
    if (c < 128) return std::tolower((int)c);
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7) ) return c + 0x20; // Latin-1 uppercase block
    if ((c & 1) == 0 && ((c >= 0x100 && c <= 0x17F) || (c >= 0x1E00 && c <= 0x1EFF))) return c + 1; // Latin Ext-A/Additional even=upper
    return c;
}
static uint32_t smUpper(uint32_t c) {
    if (c < 128) return std::toupper((int)c);
    if ((c >= 0xE0 && c <= 0xFE && c != 0xF7)) return c - 0x20;
    if ((c & 1) == 1 && ((c >= 0x100 && c <= 0x17F) || (c >= 0x1E00 && c <= 0x1EFF))) return c - 1;
    return c;
}
// Grapheme-aligned case (:samecase) and/or combining-mark (:samemark) transfer:
// each replacement grapheme takes the case and/or the combining marks of the
// positionally aligned grapheme of the match; the last seen case carries on.
static std::string applyCaseMark(const std::string& orig, const std::string& repl, bool doCase, bool doMark) {
    using namespace rakupp;
    auto split = [](const std::vector<uint32_t>& cps) {
        std::vector<std::vector<uint32_t>> gs;
        for (size_t i = 0; i < cps.size();) {
            std::vector<uint32_t> g; g.push_back(cps[i++]);
            while (i < cps.size() && uniCombiningClass(cps[i]) != 0) g.push_back(cps[i++]);
            gs.push_back(g);
        }
        return gs;
    };
    auto og = split(smDecode(orig));
    struct GI { uint32_t base; bool hasCase, upper; std::vector<uint32_t> marks; };
    std::vector<GI> oi;
    for (auto& g : og) {
        auto nfd = uniNormalize(g, 0); GI gi{}; gi.base = nfd.empty() ? 0 : nfd[0];
        for (size_t k = 1; k < nfd.size(); k++) if (uniCombiningClass(nfd[k]) != 0) gi.marks.push_back(nfd[k]);
        gi.hasCase = smLower(gi.base) != smUpper(gi.base);
        gi.upper = gi.hasCase && gi.base == smUpper(gi.base);
        oi.push_back(gi);
    }
    auto rg = split(smDecode(repl));
    std::string out; bool lastUpper = false, haveLast = false;
    for (size_t i = 0; i < rg.size(); i++) {
        std::vector<uint32_t> g = rg[i];
        const GI* o = i < oi.size() ? &oi[i] : nullptr;
        if (doCase) {
            if (o && o->hasCase) { lastUpper = o->upper; haveLast = true; }
            if (haveLast) g[0] = lastUpper ? smUpper(g[0]) : smLower(g[0]);
        }
        if (doMark && o) for (uint32_t mk : o->marks) g.push_back(mk);
        for (uint32_t cp : uniNormalize(g, 1)) out += smEncode(cp);
    }
    return out;
}

// `:samecase` — each replacement character takes the case of the positionally
// aligned character of the match (`oO` → `au` gives `aU`); once the match runs
// out, the last seen case carries on.
static std::string applySamecase(const std::string& orig, const std::string& repl) {
    std::string r = repl;
    bool lastUpper = false, haveLast = false; size_t oi = 0;
    for (size_t i = 0; i < r.size(); i++) {
        if (oi < orig.size()) {
            unsigned char oc = orig[oi++];
            if (std::isalpha(oc)) { lastUpper = std::isupper(oc); haveLast = true; }
        }
        if (haveLast && std::isalpha((unsigned char)r[i]))
            r[i] = lastUpper ? std::toupper((unsigned char)r[i]) : std::tolower((unsigned char)r[i]);
    }
    return r;
}

std::string Interpreter::substSelect(const std::string& subj, const std::string& pat,
                                     Value* replArg, ValueList& args, long& nsub, bool literal,
                                     const std::string* tmplRepl, Value* matchResult) {
    nsub = 0;
    bool global = false, samecase = false, samespace = false, samemark = false;
    bool icase = false, sigspace = false, ignoremark = false;
    bool haveX = false, haveNth = false, haveStart = false, posAnchored = false;
    Value xVal, nthVal; long startPos = 0;
    auto setAdverb = [&](const std::string& k, const Value& pv) {
        // ordinal / count adverbs written as one token: :1st :2nd :3rd :5th, :2x
        if (k.size() >= 2 && std::isdigit((unsigned char)k[0])) {
            size_t d = 0; while (d < k.size() && std::isdigit((unsigned char)k[d])) d++;
            std::string suf = k.substr(d);
            long num = std::stol(k.substr(0, d));
            if (suf == "st" || suf == "nd" || suf == "rd" || suf == "th") { haveNth = true; nthVal = Value::integer(num); return; }
            if (suf == "x") { haveX = true; xVal = Value::integer(num); return; }
        }
        if ((k == "g" || k == "global") && pv.truthy()) global = true;
        else if (k == "x") { haveX = true; xVal = pv; }
        else if (k == "nth" || k == "st" || k == "nd" || k == "rd" || k == "th") { haveNth = true; nthVal = pv; }
        else if (k == "p" || k == "pos") { haveStart = true; posAnchored = true; startPos = pv.toInt(); }
        else if (k == "c" || k == "continue") { haveStart = true; startPos = pv.toInt(); }
        else if (k == "i" || k == "ignorecase") icase = true;
        else if (k == "samecase") samecase = true; // implies :i for regex only (applied below)
        else if (k == "ii") { samecase = true; icase = true; } // :ii always implies :i
        else if (k == "s" || k == "sigspace") sigspace = true;
        else if (k == "samespace" || k == "ss") { sigspace = true; samespace = true; }
        else if (k == "samemark" || k == "mm") { samemark = true; ignoremark = true; } // :mm implies :m
        else if (k == "m" || k == "ignoremark") ignoremark = true;
        else throw RakuError{Value::typeObj("X::Syntax::Regex::Adverb"), "Unrecognized regex adverb: :" + k};
    };
    for (auto& a : args)
        if (a.t == VT::Pair) setAdverb(a.s, a.pairVal ? *a.pairVal : Value::boolean(true));
    // leading `:name` / `:name(arg)` adverbs baked into the pattern (s///, ss///)
    std::string realPat = pat;
    { size_t i = 0;
      while (i < realPat.size() && realPat[i] == ':') {
          size_t j = i + 1; std::string name;
          while (j < realPat.size() && std::isalnum((unsigned char)realPat[j])) name += realPat[j++];
          if (name.empty()) break;
          Value argv = Value::boolean(true);
          if (j < realPat.size() && realPat[j] == '(') {
              int d = 0; std::string arg;
              do { char c = realPat[j]; if (c == '(') d++; else if (c == ')') d--; arg += c; j++; } while (j < realPat.size() && d > 0);
              // the value of a match-mode adverb (:i, :m) must be a compile-time constant
              if ((name == "i" || name == "ignorecase" || name == "m" || name == "ignoremark")
                  && arg.find_first_of("$@%") != std::string::npos)
                  throw RakuError{Value::typeObj("X::Value::Dynamic"), "Value of :" + name + " must be known at compile time"};
              try { argv = evalString(arg); } catch (...) {}
          }
          while (j < realPat.size() && realPat[j] == ' ') j++;
          setAdverb(name, argv);
          i = j;
      }
      realPat = realPat.substr(i);
    }
    if (samecase && !literal) icase = true; // :samecase implies :i for a regex, not a Str pattern
    if (!literal && realPat.empty())
        throw RakuError{Value::typeObj("X::Syntax::Regex::NullRegex"), "Null regex not allowed"};
    if (haveX) { // the :x count must be an Int, a Range, or Whatever
        VT t = xVal.t;
        if (!(t == VT::Int || t == VT::Num || t == VT::Rat || t == VT::Range || t == VT::Whatever || t == VT::Bool))
            throw RakuError{Value::typeObj("X::Str::Match::x"), "Invalid :x argument"};
    }
    if (!literal) {
        // interpolate scalar variables ($foo, $^a) into the regex as literal (quotemeta'd) text
        std::string ip;
        for (size_t i = 0; i < realPat.size(); i++) {
            if (realPat[i] == '\\' && i + 1 < realPat.size()) { ip += realPat[i]; ip += realPat[i + 1]; i++; continue; }
            if (realPat[i] == '$' && i + 1 < realPat.size()) {
                size_t j = i + 1;
                if (realPat[j] == '^' && j + 1 < realPat.size()) j++; // $^a is visible as $a
                if (j < realPat.size() && (std::isalpha((unsigned char)realPat[j]) || realPat[j] == '_')) {
                    std::string nm; while (j < realPat.size() && (std::isalnum((unsigned char)realPat[j]) || realPat[j] == '_')) nm += realPat[j++];
                    if (Value* v = tctx_.cur->find("$" + nm)) {
                        for (char c : v->toStr()) { if (std::strchr(".\\+*?[^]$(){}=!<>|:-#", c)) ip += '\\'; ip += c; }
                        i = j - 1; continue;
                    }
                }
            }
            ip += realPat[i];
        }
        realPat = ip;
    }
    std::vector<RxMatch> matches;
    if (literal) {
        // plain string pattern: exact byte search (control chars, no regex metachars)
        std::string needle = realPat;
        if (icase) for (auto& c : needle) c = std::tolower((unsigned char)c);
        std::string hay = subj;
        if (icase) for (auto& c : hay) c = std::tolower((unsigned char)c);
        long pos = haveStart ? startPos : 0;
        while (needle.size() && pos <= (long)hay.size()) {
            size_t f = hay.find(needle, pos);
            if (f == std::string::npos) break;
            RxMatch mm; mm.matched = true; mm.from = (long)f; mm.to = (long)f + (long)needle.size();
            matches.push_back(mm);
            pos = mm.to > mm.from ? mm.to : mm.to + 1;
        }
    } else if (ignoremark) {
        // :m/:mm — fold subject & pattern to base characters (drop combining marks),
        // match on the folded text, then map matched ranges back to original coordinates.
        using namespace rakupp;
        auto nextGrapheme = [](const std::vector<uint32_t>& cps, size_t& i) {
            std::vector<uint32_t> g; g.push_back(cps[i++]);
            while (i < cps.size() && uniCombiningClass(cps[i]) != 0) g.push_back(cps[i++]);
            return g;
        };
        auto baseOf = [](const std::vector<uint32_t>& g) {
            std::string b; for (uint32_t cp : uniNormalize(g, 0)) if (uniCombiningClass(cp) == 0) b += smEncode(cp); return b;
        };
        std::vector<uint32_t> scps = smDecode(subj);
        std::string folded; std::vector<long> foldStart, origStart; long ob = 0;
        for (size_t i = 0; i < scps.size();) {
            foldStart.push_back((long)folded.size()); origStart.push_back(ob);
            std::string gtext; { size_t s = i; auto g = nextGrapheme(scps, i);
                for (uint32_t cp : g) gtext += smEncode(cp); folded += baseOf(g); (void)s; }
            ob += (long)gtext.size();
        }
        foldStart.push_back((long)folded.size()); origStart.push_back(ob);
        std::string fpat; { std::vector<uint32_t> pcps = smDecode(realPat);
            for (size_t i = 0; i < pcps.size();) fpat += baseOf(nextGrapheme(pcps, i)); }
        std::string flags = std::string(icase ? "i" : "") + (sigspace ? "s" : "");
        Regex re(fpat, flags);
        if (!re.ok()) return subj;
        auto toOrig = [&](long fb) -> long {
            size_t idx = std::lower_bound(foldStart.begin(), foldStart.end(), fb) - foldStart.begin();
            return origStart[std::min(idx, origStart.size() - 1)];
        };
        long pos = 0; RxMatch mm;
        while (pos >= 0 && pos <= (long)folded.size() && re.search(folded, pos, mm)) {
            RxMatch om; om.matched = true; om.from = toOrig(mm.from); om.to = toOrig(mm.to);
            matches.push_back(om);
            pos = mm.to > mm.from ? mm.to : mm.to + 1;
        }
    } else {
        std::string flags = std::string(icase ? "i" : "") + (sigspace ? "s" : "");
        Regex re(realPat, flags);
        if (!re.ok()) return subj;
        long pos = haveStart ? startPos : 0; RxMatch mm;
        while (pos >= 0 && pos <= (long)subj.size() && re.search(subj, pos, mm)) {
            matches.push_back(mm);
            pos = mm.to > mm.from ? mm.to : mm.to + 1;
        }
    }
    // :p(n) anchors the FIRST match exactly at position n (`:c` merely searches from n).
    if (posAnchored && (matches.empty() || matches[0].from != startPos)) matches.clear();
    long total = (long)matches.size();
    // occurrence selection (1-based). :x count, :nth indices, :g all, else first.
    auto bounds = [&](const Value& v, long& lo, long& hi) {
        if (v.t == VT::Range) { lo = v.rFrom + (v.rExFrom ? 1 : 0); hi = v.rTo - (v.rExTo ? 1 : 0); }
        else if (v.t == VT::Whatever || std::isinf(v.toNum())) { lo = 1; hi = total; }
        else { lo = hi = v.toInt(); }
    };
    std::set<long> sel;
    if (haveNth) {
        ValueList l = (nthVal.t == VT::Array && nthVal.arr) ? *nthVal.arr : nthVal.flatten();
        long xcap = total;
        if (haveX) { long lo, hi; bounds(xVal, lo, hi); xcap = hi; if ((long)l.size() < lo) l.clear(); }
        long cnt = 0, prev = 0;
        for (auto& v : l) { long i = v.toInt();
            if (i <= prev) throw RakuError{Value::typeObj("X::AdHoc"), "Attempt to fetch matches out of order with :nth"};
            prev = i;
            if (cnt >= xcap) break; if (i >= 1 && i <= total) { sel.insert(i); cnt++; } }
    } else if (haveX) {
        long lo, hi; bounds(xVal, lo, hi);
        long count = std::min(total, hi);
        if (count >= lo && count >= 1) for (long i = 1; i <= count; i++) sel.insert(i);
    } else if (global) {
        for (long i = 1; i <= total; i++) sel.insert(i);
    } else if (total >= 1) sel.insert(1);
    // Match value (positional + named captures) for one raw match.
    auto build = [&](const RxMatch& mm) {
        Value v = Value::matchVal(subj.substr(mm.from, mm.to - mm.from), mm.from, mm.to);
        for (auto& c : mm.caps) {
            if (c.first < 0) v.arrRef().push_back(Value::nil());
            else v.arrRef().push_back(Value::matchVal(subj.substr(c.first, c.second - c.first), c.first, c.second));
        }
        for (auto& kv : mm.named)
            v.hashRef()[kv.first] = Value::matchVal(subj.substr(kv.second.first, kv.second.second - kv.second.first), kv.second.first, kv.second.second);
        return v;
    };
    auto interp = [&](const std::string& s, const Value& mv) -> std::string {
        std::string r;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) { r += s[i + 1]; i++; continue; }
            if (s[i] == '$' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1])) {
                size_t j = i + 1; std::string num; while (j < s.size() && std::isdigit((unsigned char)s[j])) num += s[j++];
                long idx = std::stol(num); if (mv.arr && idx < (long)mv.arr->size()) r += (*mv.arr)[idx].toStr();
                i = j - 1; continue;
            }
            if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '<') {
                size_t j = s.find('>', i + 2);
                if (j != std::string::npos) { std::string nm = s.substr(i + 2, j - i - 2);
                    if (mv.hash && mv.hash->count(nm)) r += (*mv.hash)[nm].toStr(); i = j; continue; }
            }
            if (s[i] == '$' && i + 2 < s.size() && s[i + 1] == '^' && (std::isalpha((unsigned char)s[i + 2]) || s[i + 2] == '_')) {
                size_t j = i + 2; std::string nm; while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '_')) nm += s[j++];
                if (Value* v = tctx_.cur->find("$" + nm)) r += v->toStr(); // $^a placeholder (also visible as $a)
                i = j - 1; continue;
            }
            if (s[i] == '$' && i + 1 < s.size() && (std::isalpha((unsigned char)s[i + 1]) || s[i + 1] == '_')) {
                size_t j = i + 1; std::string nm; while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '_')) nm += s[j++];
                if (Value* v = tctx_.cur->find("$" + nm)) r += v->toStr();
                i = j - 1; continue; // interpolate a scalar variable in the replacement
            }
            if (s[i] == '@' && i + 1 < s.size() && (std::isalpha((unsigned char)s[i + 1]) || s[i + 1] == '_')) {
                size_t j = i + 1; std::string nm; while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '_')) nm += s[j++];
                // `@arr[expr]` — a non-empty subscript indexes one element (captures
                // are already bound, so the subscript may use $0/$<name>).
                if (j < s.size() && s[j] == '[') {
                    int depth = 0; size_t k = j;
                    for (; k < s.size(); k++) { if (s[k] == '[') depth++; else if (s[k] == ']' && --depth == 0) break; }
                    std::string subx = s.substr(j + 1, k - j - 1);
                    size_t ws = subx.find_first_not_of(" \t");
                    if (k < s.size() && ws != std::string::npos) { // non-empty subscript
                        long idx = 0; try { idx = evalString(subx).toInt(); } catch (...) {}
                        if (Value* v = tctx_.cur->find("@" + nm)) {
                            ValueList fl = v->flatten();
                            if (idx < 0) idx += (long)fl.size();
                            if (idx >= 0 && idx < (long)fl.size()) r += fl[idx].toStr();
                        }
                        i = k; continue;
                    }
                    if (k + 1 <= s.size()) j = k + 1; // empty `[]` zen slice → whole array
                }
                if (Value* v = tctx_.cur->find("@" + nm)) { // interpolate an array (space-joined)
                    ValueList fl = v->flatten();
                    for (size_t k = 0; k < fl.size(); k++) { if (k) r += ' '; r += fl[k].toStr(); }
                }
                i = j - 1; continue;
            }
            r += s[i];
        }
        return r;
    };
    auto replFor = [&](const RxMatch& mm) -> std::string {
        std::string orig = subj.substr(mm.from, mm.to - mm.from);
        std::string r;
        Value matchV = build(mm);
        auto bindCaps = [&]() {
            tctx_.cur->define("$/", matchV);
            if (matchV.arr) for (size_t k = 0; k < matchV.arr->size(); k++) tctx_.cur->define("$" + std::to_string(k), (*matchV.arr)[k]);
            for (auto& kv : *matchV.hash) tctx_.cur->define("$<" + kv.first + ">", kv.second);
        };
        if (replArg && replArg->t == VT::Code) {
            bindCaps();
            Value saved = tctx_.cur->vars.count("$_") ? tctx_.cur->vars["$_"] : Value::any();
            tctx_.cur->define("$_", matchV);
            r = callCallable(*replArg, ValueList{matchV}).toStr();
            tctx_.cur->vars["$_"] = saved;
        } else if (tmplRepl) {
            // s/// replacement TEMPLATE: `{ code }` evaluated per match, else $N/$<name> interpolation
            std::string rt = *tmplRepl;
            size_t a = rt.find_first_not_of(" \t\n"), b = rt.find_last_not_of(" \t\n");
            std::string t = a == std::string::npos ? "" : rt.substr(a, b - a + 1);
            bindCaps();
            if (t.size() >= 2 && t.front() == '{' && t.back() == '}') {
                // a code replacement re-parsed here can't see caller-local custom infixes
                // (`s[…] fromplus= …`); on failure keep the original so it can't abort.
                try { r = evalString(t.substr(1, t.size() - 2)).toStr(); } catch (...) { r = orig; }
            } else r = interp(*tmplRepl, matchV);
        } else if (replArg) {
            r = replArg->toStr();
        }
        if (samespace) {
            // replace each whitespace run in r with the corresponding run in the match
            // (done first so grapheme positions line up for :samecase / :samemark)
            std::vector<std::string> ws;
            for (size_t i = 0; i < orig.size(); ) {
                if (std::isspace((unsigned char)orig[i])) { std::string w; while (i < orig.size() && std::isspace((unsigned char)orig[i])) w += orig[i++]; ws.push_back(w); }
                else i++;
            }
            std::string a; size_t wi = 0;
            for (size_t i = 0; i < r.size(); ) {
                if (std::isspace((unsigned char)r[i])) { while (i < r.size() && std::isspace((unsigned char)r[i])) i++; a += wi < ws.size() ? ws[wi++] : std::string(" "); }
                else a += r[i++];
            }
            r = a;
        }
        if (ignoremark) r = applyCaseMark(orig, r, samecase, true); // :m/:mm transfer marks (and case if :ii)
        else if (samecase) r = applySamecase(orig, r);
        return r;
    };
    std::string out; long last = 0, occ = 0;
    std::vector<Value> selMatches;
    for (auto& mm : matches) {
        occ++;
        out += subj.substr(last, mm.from - last);
        if (sel.count(occ)) { out += replFor(mm); nsub++; selMatches.push_back(build(mm)); }
        else out += subj.substr(mm.from, mm.to - mm.from);
        last = mm.to;
    }
    out += subj.substr(last);
    // $/ / the s/// return value: no match → falsey Nil; :g/:nth → a List of Matches;
    // otherwise the single Match. (`.Str` = matched text, `+` of the :g List = count.)
    Value result;
    if (selMatches.empty()) result = Value::nil();
    else if (global || haveNth || haveX) { result = Value::list(selMatches); } // multi-match adverbs → List
    else result = selMatches.back();
    if (!literal) tctx_.cur->define("$/", result); // a literal (string) .subst leaves $/ untouched
    if (matchResult) *matchResult = result;
    return out;
}

Value Interpreter::grammarParse(ClassInfo* g, const std::string& input, bool subparse,
                                const std::string& startRule, Value actions) {
    bool haveActions = (actions.t == VT::Object || actions.t == VT::Type);
    ClassInfo* actCls = nullptr;
    if (actions.t == VT::Object && actions.obj) actCls = actions.obj->cls.get();
    else if (actions.t == VT::Type) { auto it = classes_.find(actions.s); if (it != classes_.end()) actCls = it->second.get(); }

    // run the actions method for `name` (if any) on a freshly built Match, setting .made
    auto runAction = [&](const std::string& name, Value& mv) {
        if (!haveActions || !actCls) return;
        Value* method = actCls->findMethod(name);
        if (!method) {
            // A `:sym«baz»` candidate is acted on by `method X:sym<baz>` — normalise the
            // guillemet sym to the canonical angle form and retry.
            auto g = name.find(":sym\xC2\xAB");
            if (g != std::string::npos) { auto e = name.find("\xC2\xBB", g + 6);
                if (e != std::string::npos) method = actCls->findMethod(name.substr(0, g) + ":sym<" + name.substr(g + 6, e - (g + 6)) + ">" + name.substr(e + 2)); }
        }
        if (!method) return;
        tctx_.makeTargets.push_back(&mv);
        try { invokeMethod(*method, actions, {mv}); }
        catch (RakuError& e) { if (std::getenv("RAKUPP_ACTTRACE")) std::cerr << "[ACT] " << name << " threw: " << e.message << "\n"; tctx_.makeTargets.pop_back(); throw; }
        catch (...) { tctx_.makeTargets.pop_back(); throw; }
        tctx_.makeTargets.pop_back();
    };

    // Gather every rule (walking the inheritance chain; child overrides parent)
    // into a backtrackable GrammarMatcher.
    GrammarMatcher gm;
    std::function<void(ClassInfo*)> collect = [&](ClassInfo* c) {
        if (!c) return;
        collect(c->parent.get());
        for (auto& r : c->rules) {
            GrammarMatcher::Rule rule;
            rule.pattern = r.second;
            rule.kind = c->ruleKind.count(r.first) ? c->ruleKind.at(r.first) : "token";
            auto pit = c->ruleParams.find(r.first);
            if (pit != c->ruleParams.end()) rule.params = pit->second;
            gm.rules[r.first] = std::move(rule);
        }
    };
    collect(g);

    // Register protoregex candidates: `element:<null>` / `element:sym<x>` is a
    // candidate of proto `element`; matching `<element>` tries them all (LTM).
    for (auto& r : gm.rules) {
        size_t c = r.first.find(":sym<");
        if (c == std::string::npos) c = r.first.find(":sym\xC2\xAB"); // :sym«…»
        if (c == std::string::npos) c = r.first.find(":<");
        if (c != std::string::npos && c > 0) gm.protos[r.first.substr(0, c)].push_back(r.first);
    }

    // Wire the match-time interpreter hooks. Embedded Raku (`<?{…}>` assertions,
    // `:my`/`{…}` side-effects, `$var` atoms, `** {…}` bounds) is parsed once (cached)
    // and evaluated against the live interpreter scope, with $/ bound to input[from..to].
    auto codeCache = std::make_shared<std::map<std::string, std::shared_ptr<Program>>>();
    auto parseCode = [this, codeCache](const std::string& code) -> std::shared_ptr<Program> {
        auto it = codeCache->find(code);
        if (it != codeCache->end()) return it->second;
        auto prog = std::make_shared<Program>();
        try { Lexer lx(code); Parser ps(lx.tokenize()); *prog = ps.parseProgram(); }
        catch (...) { (*codeCache)[code] = nullptr; return nullptr; }
        { std::unique_lock<std::mutex> kl(sharedMut_, std::defer_lock); if (parallelMode_) kl.lock(); keptPrograms_.push_back(prog); }
        (*codeCache)[code] = prog;
        return prog;
    };
    // Inline `{ make … }` blocks in a token run during matching; their made value is
    // recorded here by span and applied to the node in build().
    auto pendingMakes = std::make_shared<std::map<std::pair<long, long>, Value>>();
    // A block containing `make` is DEFERRED: it runs in build() with $/ = the fully-built
    // match (so `{ make $/.values[0].ast }` sees children's .made). A queue per span
    // disambiguates a parent and child that happen to share the same span (innermost-first).
    auto pendingMakeCode = std::make_shared<std::map<std::pair<long, long>, std::vector<std::string>>>();
    // Execute `code` with an overlay of the current rule params (as Str) and $/ (carrying
    // the named captures so far) temporarily bound; `:my`/assignments persist in tctx_.cur.
    using NamedMap = GrammarHooks::NamedMap; using ParamMap = GrammarHooks::ParamMap;
    auto runCode = [this, &input, parseCode, pendingMakes](const std::string& code, long from, long to,
                                             const NamedMap& named, const ParamMap& params) -> Value {
        auto prog = parseCode(code);
        if (!prog) return Value::any();
        // build $/ over [from..to] with the named sub-captures attached
        Value m = Value::matchVal(input.substr(from, to - from), from, to);
        for (auto& nm : named)
            m.hashRef()[nm.first] = Value::matchVal(input.substr(nm.second.first, nm.second.second - nm.second.first),
                                                  nm.second.first, nm.second.second);
        // save & overlay $/ + params; restore them after (but let :my vars persist)
        std::vector<std::pair<std::string, Value>> restore;
        auto overlay = [&](const std::string& name, const Value& v) {
            Value* slot = tctx_.cur->find(name);
            restore.push_back({name, slot ? *slot : Value::nil()});
            tctx_.cur->define(name, v);
        };
        bool hadSlash = tctx_.cur->find("$/") != nullptr; Value savedSlash = hadSlash ? *tctx_.cur->find("$/") : Value::nil();
        tctx_.cur->define("$/", m);
        for (auto& p : params) overlay(p.first, Value::str(p.second));
        Value makeTarget; tctx_.makeTargets.push_back(&makeTarget); // capture an inline `make`
        Value last = Value::any();
        try { for (auto& s : prog->stmts) last = exec(s.get()); } catch (...) {}
        tctx_.makeTargets.pop_back();
        if (makeTarget.pairVal) (*pendingMakes)[{from, to}] = *makeTarget.pairVal; // record the inline make
        for (auto it = restore.rbegin(); it != restore.rend(); ++it) tctx_.cur->define(it->first, it->second);
        if (Value* s2 = tctx_.cur->find("$/")) *s2 = savedSlash;
        return last;
    };
    gm.hooks.assertPass = [runCode, parseCode](const std::string& code, long from, long to, const NamedMap& nm, const ParamMap& pm) -> bool {
        if (!parseCode(code)) return true; // unparseable assertion → lenient pass
        return runCode(code, from, to, nm, pm).truthy();
    };
    gm.hooks.run = [runCode, pendingMakeCode](const std::string& code, long from, long to, const NamedMap& nm, const ParamMap& pm) {
        // a `make` block is deferred to build() (it may reference children's .made);
        // other blocks (`:my`, indentation assignments) run now for their side effects.
        if (code.find("make") != std::string::npos) (*pendingMakeCode)[{from, to}].push_back(code);
        else runCode(code, from, to, nm, pm);
    };
    gm.hooks.str = [runCode](const std::string& expr, const NamedMap& nm, const ParamMap& pm) -> std::string {
        // Fast path: a bare `$param` atom (e.g. `$indent`) is by far the most common
        // VarMatch in real grammars and is matched constantly. Resolve it straight from
        // the rule's param bindings, skipping parse/Match-build/overlay/exec entirely.
        auto it = pm.find(expr);
        if (it != pm.end()) return it->second;
        return runCode(expr, 0, 0, nm, pm).toStr();
    };
    gm.hooks.range = [runCode](const std::string& code, const NamedMap& nm, const ParamMap& pm) -> std::pair<long, long> {
        // `** { N..* }` / `..Inf` / `..∞` is an unbounded quantifier — detect it from the
        // source (a Whatever endpoint numifies to 0, which would wrongly mean "exactly N").
        bool unbounded = code.find("..*") != std::string::npos || code.find("..Inf") != std::string::npos ||
                         code.find("..\xE2\x88\x9E") != std::string::npos;
        Value v = runCode(code, 0, 0, nm, pm);
        if (v.t == VT::Range) {
            long lo = v.rFrom, hi = unbounded ? -1 : (v.rExTo ? v.rTo - 1 : v.rTo);
            if (v.rTo >= (long long)1e15) hi = -1;
            return {lo, hi};
        }
        long n = v.toInt(); return {n, n};
    };
    // Snapshot/restore the interpreter side-effects an LTM probe may touch. We roll back
    // `:my` vars (a probe branch may set e.g. $new-indent that the committed branch must
    // re-derive), but NOT the deferred-make queues: those are keyed by span and consumed
    // in build() strictly by the FINAL tree's spans, so entries left by a probed-but-
    // rejected branch are inert. Copying the whole (O(input)-sized) make queues on every
    // `|` — and `|` is hit O(input) times — was quadratic; skipping it keeps probes O(1).
    struct GState { std::unordered_map<std::string, Value> vars; };
    // Shared sentinel meaning "scope was empty at snapshot time" — restoring it just
    // clears whatever the probe added, with no per-probe allocation or map copy. Most
    // LTM `|` probes happen before any `:my` var exists, so this is the common path.
    auto emptySentinel = std::make_shared<int>(0);
    gm.hooks.saveState = [this, emptySentinel]() -> std::shared_ptr<void> {
        if (tctx_.cur->vars.empty()) return emptySentinel;
        auto s = std::make_shared<GState>();
        s->vars = tctx_.cur->vars;
        return s;
    };
    gm.hooks.restoreState = [this, emptySentinel](std::shared_ptr<void> v) {
        if (v == emptySentinel) { tctx_.cur->vars.clear(); return; }
        auto s = std::static_pointer_cast<GState>(v);
        tctx_.cur->vars = s->vars;
    };

    // Turn the recorded parse tree into Match values, running actions bottom-up
    // so `$<child>.made` is available to a parent's action.
    std::function<Value(const ParseNode&)> build = [&](const ParseNode& pn) -> Value {
        Value mv = Value::matchVal(input.substr(pn.from, pn.to - pn.from), pn.from, pn.to);
        for (auto& c : pn.caps) {
            if (c.first < 0) mv.arrRef().push_back(Value::nil());
            else mv.arrRef().push_back(Value::matchVal(input.substr(c.first, c.second - c.first), c.first, c.second));
        }
        for (auto& kv : pn.named)
            if (!pn.kids || !pn.kids->count(kv.first))
                mv.hashRef()[kv.first] = Value::matchVal(input.substr(kv.second.first, kv.second.second - kv.second.first), kv.second.first, kv.second.second);
        // a leaf with no rule name is a plain $<x>=[…] capture: just its span, no actions
        auto buildChild = [&](const ParseNode& child) -> Value {
            if (child.name.empty())
                return Value::matchVal(input.substr(child.from, child.to - child.from), child.from, child.to);
            return build(child);
        };
        if (pn.kids) for (auto& kv : *pn.kids) {
            // a name captured more than once ($<num> ... $<num>, or <item>+) collates into a list
            if (kv.second.size() == 1) mv.hashRef()[kv.first] = buildChild(kv.second[0]);
            else {
                Value arr = Value::array(); arr.isList = true;
                for (auto& child : kv.second) arr.arr->push_back(buildChild(child));
                mv.hashRef()[kv.first] = arr;
            }
        }
        // a deferred inline `{ make … }` runs now, with $/ = this fully-built match
        // (so it can read `$/.values[0].ast` etc.) and this node as the make target.
        // Pop the innermost queued make for this span (parent/child may share it).
        auto mc = pendingMakeCode->find({pn.from, pn.to});
        if (mc != pendingMakeCode->end() && !mc->second.empty()) {
            std::string code = mc->second.front();
            mc->second.erase(mc->second.begin());
            if (auto prog = parseCode(code)) {
                Value* slot = tctx_.cur->find("$/"); Value saved = slot ? *slot : Value::nil();
                tctx_.cur->define("$/", mv);
                tctx_.makeTargets.push_back(&mv);
                try { for (auto& s : prog->stmts) exec(s.get()); } catch (...) {}
                tctx_.makeTargets.pop_back();
                if (Value* s2 = tctx_.cur->find("$/")) *s2 = saved;
            }
        }
        auto pm = pendingMakes->find({pn.from, pn.to});
        if (pm != pendingMakes->end() && !mv.pairVal) mv.pairVal = std::make_shared<Value>(pm->second);
        runAction(pn.name, mv); // an action-class method (if any) can still override
        return mv;
    };

    // Match against a dedicated child scope so match-time `:my` vars land in a small,
    // isolated map — the LTM `|` snapshot (saveState) then copies O(:my vars) per probe,
    // not the whole enclosing scope (which for a module-level parse holds every sub/var).
    ParseNode tree; long endPos = -1;
    bool matched;
    {
        auto matchScope = std::make_shared<Env>();
        matchScope->parent = tctx_.cur;
        auto savedScope = tctx_.cur;
        tctx_.cur = matchScope;
        matched = gm.parse(input, startRule, subparse, tree, endPos);
        tctx_.cur = savedScope;
    }
    if (!matched) {
        tctx_.cur->define("$/", Value::nil()); return Value::nil();
    }
    Value mv = build(tree);
    tctx_.cur->define("$/", mv);
    return mv;
}

Value Interpreter::applyBinOp(const std::string& op, const Value& l, const Value& r) {
    // reverse metaop (`a R- b` == `b - a`) — so `[R-]`/`[R~]` reduce works like the
    // standalone `R-` binary does (evalBinary strips it; applyBinOp must too).
    if (op.size() > 1 && op[0] == 'R' && !std::isalnum((unsigned char)op[1]))
        return applyBinOp(op.substr(1), r, l);
    // short-circuit ops applied to already-evaluated VALUES ([//] reduce, sort &[||]):
    // no thunking here, just the selection semantics
    if (op == "//") return isDefined(l) ? l : r;
    if (op == "||" || op == "or") return l.truthy() ? l : r;
    if (op == "&&" || op == "and") return l.truthy() ? r : l;
    if (op == "andthen") return isDefined(l) ? r : l;
    if (op == "orelse") return isDefined(l) ? l : r;
    if (op == "xor" || op == "^^")
        return l.truthy() ? (r.truthy() ? Value::nil() : l) : r; // one true → it; none → last
    // zip/cross with an inner op (Z&& / Zand / X~) — resolve the inner via applyBinOp
    if (op.size() > 1 && (op[0] == 'Z' || op[0] == 'X')) {
        std::string sub = op.substr(1);
        ValueList a = l.flatten(), bb = r.flatten();
        Value out = Value::array(); out.isList = true;
        auto emit = [&](const Value& x, const Value& y) {
            if (sub == "=>") out.arr->push_back(Value::pair(x.toStr(), y));
            else out.arr->push_back(applyBinOp(sub, x, y));
        };
        if (op[0] == 'Z') { for (size_t i = 0; i < a.size() && i < bb.size(); i++) emit(a[i], bb[i]); }
        else { for (auto& x : a) for (auto& y : bb) emit(x, y); }
        return out;
    }
    if (op == "minmax") { // Range spanning both operands' extremes
        ValueList a = l.flatten(), bb = r.flatten();
        bool first = true; long long mn = 0, mx = 0;
        auto see = [&](const Value& v) {
            long long x = v.toInt();
            if (first) { mn = mx = x; first = false; }
            else { if (x < mn) mn = x; if (x > mx) mx = x; }
        };
        for (auto& v : a) see(v);
        for (auto& v : bb) see(v);
        return Value::range(mn, mx, false, false);
    }
    // hyper metaop over evaluated lists — also reachable via [>>+<<] reduce
    if (op.size() >= 5 && (op.compare(0, 2, ">>") == 0 || op.compare(0, 2, "<<") == 0) &&
        (op.compare(op.size() - 2, 2, ">>") == 0 || op.compare(op.size() - 2, 2, "<<") == 0)) {
        std::string inner = op.substr(2, op.size() - 4);
        ValueList a = l.flatten(), bb = r.flatten();
        Value out = Value::array(); out.isList = true;
        size_t n = a.size() > bb.size() ? a.size() : bb.size();
        if (!a.empty() && !bb.empty())
            for (size_t i = 0; i < n; i++) out.arr->push_back(applyBinOp(inner, a[i % a.size()], bb[i % bb.size()]));
        return out;
    }
    try { return applyArith(op, l, r); }
    catch (RakuError&) {
        if (Value* f = tctx_.cur->find("&infix:<" + op + ">")) return callCallable(*f, ValueList{l, r});
        throw;
    }
}

Value Interpreter::evalBinary(Binary* b) {
    const std::string& op = b->op;
    // Fast path for plain operators (`+ - * < == …`): skip the ~20 string compares
    // for the special-cased forms below and go straight to eval-both + applyArith.
    // The classification is computed once and cached on the node.
    if (b->simpleOp < 0) {
        static const std::set<std::string> special = {
            "~", "does", "but", "xx", "==>", "<==", "...", "...^", "~~", "!~~",
            "&&", "and", "||", "or", "andthen", "orelse", "//", "^^", "xor", "&", "|", "^",
            "=:=", "!=:="};
        bool rmeta = op.size() > 1 && op[0] == 'R' && !std::isalnum((unsigned char)op[1]);
        b->simpleOp = (rmeta || special.count(op)) ? 0 : 1;
    }
    if (b->simpleOp == 1) {
        Value l = eval(b->lhs.get());
        Value r = eval(b->rhs.get());
        // function composition `f ∘ g` / `f o g` → a callable computing f(g(...))
        if (op == "\xE2\x88\x98" || op == "o") {
            Value fV = l, gV = r;
            Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
            code.code->builtin = [fV, gV](Interpreter& I, ValueList& a) -> Value {
                return I.callCallable(fV, ValueList{ I.callCallable(gV, a) });
            };
            return code;
        }
        if (l.t == VT::Object || r.t == VT::Object)
            if (Value* f = tctx_.cur->find("&infix:<" + op + ">"))
                try { return callCallable(*f, ValueList{l, r}); } catch (RakuError&) {}
        // hyper metaop `>>op<<` — element-wise, resolving a user inner operator
        if (op.size() >= 5 && (op.compare(0, 2, ">>") == 0 || op.compare(0, 2, "<<") == 0) &&
            (op.compare(op.size() - 2, 2, ">>") == 0 || op.compare(op.size() - 2, 2, "<<") == 0)) {
            std::string inner = op.substr(2, op.size() - 4);
            ValueList a = l.flatten(), bb = r.flatten();
            Value out = Value::array(); out.isList = true;
            size_t n = a.size() > bb.size() ? a.size() : bb.size();
            if (!a.empty() && !bb.empty())
                for (size_t i = 0; i < n; i++) out.arr->push_back(applyBinOp(inner, a[i % a.size()], bb[i % bb.size()]));
            return out;
        }
        // zip/cross metaop `Zop`/`Xop` — resolve a user inner operator via applyBinOp
        if (op.size() > 1 && (op[0] == 'Z' || op[0] == 'X')) {
            std::string sub = op.substr(1);
            ValueList a = l.flatten(), bb = r.flatten();
            Value out = Value::array(); out.isList = true;
            auto emit = [&](const Value& x, const Value& y) {
                if (sub.empty()) out.arr->push_back(Value::array({x, y}));
                else if (sub == "=>") out.arr->push_back(Value::pair(x.toStr(), y));
                else out.arr->push_back(applyBinOp(sub, x, y));
            };
            if (op[0] == 'Z') { for (size_t i = 0; i < a.size() && i < bb.size(); i++) emit(a[i], bb[i]); }
            else { for (auto& x : a) for (auto& y : bb) emit(x, y); }
            return out;
        }
        return applyArith(op, l, r);
    }
    if (op == "=:=" || op == "!=:=") {
        // container identity: two variables are =:= only when they are the SAME
        // slot (`my ($x, $y)` are two containers even while both hold Any).
        // Non-variable operands fall back to type+value identity (`1 =:= 1`).
        bool same;
        if (b->lhs->kind == NK::VarExpr && b->rhs->kind == NK::VarExpr) {
            Value* lp = nullptr; Value* rp = nullptr;
            try { lp = lvalue(b->lhs.get()); } catch (RakuError&) {}
            try { rp = lvalue(b->rhs.get()); } catch (RakuError&) {}
            same = lp && rp && lp == rp;
        }
        else {
            Value l = eval(b->lhs.get()), r = eval(b->rhs.get());
            same = (l.t == r.t) && valueEq(l, r);
        }
        return Value::boolean(op[0] == '!' ? !same : same);
    }
    if (op.size() > 1 && op[0] == 'R' && !std::isalnum((unsigned char)op[1])) {
        // reverse metaoperator: `a R/ b` computes `b / a`
        Value l = eval(b->lhs.get()), r = eval(b->rhs.get());
        return applyArith(op.substr(1), r, l);
    }
    if (op == "~") {
        // string concat coerces via .Str; honour a user-defined `method Str`/`gist`
        Value l = eval(b->lhs.get()), r = eval(b->rhs.get());
        if (l.t == VT::Object || r.t == VT::Object) return Value::str(strOf(l) + strOf(r));
        return applyArith("~", l, r);
    }
    if (op == "does" || op == "but") {
        Value base = eval(b->lhs.get());
        Value rhs = eval(b->rhs.get());
        Value res = mixinValue(std::move(base), rhs, op == "but");
        // `does` mutates the container in place; for a boxed non-object base the
        // mixed value is a fresh object, so write it back to the LHS lvalue.
        if (op == "does" && res.t == VT::Object && res.obj && res.obj->hasBoxed) {
            NK k = b->lhs->kind;
            if (k == NK::VarExpr || k == NK::Index || k == NK::MethodCall)
                try { if (Value* lv = lvalue(b->lhs.get())) *lv = res; } catch (...) {}
        }
        return res;
    }
    if (op == "xx") {
        // list repetition THUNKS its left side: `EXPR xx N` re-evaluates EXPR once
        // per copy (so `rand xx 3` / `(…roll…) xx $N` yield independent results).
        long long n = eval(b->rhs.get()).toInt();
        Value a = Value::array(); a.isList = true;
        for (long long k = 0; k < n; k++) a.arr->push_back(eval(b->lhs.get()));
        return a;
    }
    if (op == "==>" || op == "<==") { // feed: source ==> f(args) ==> … ==> my @target
        Expr* srcE = op == "==>" ? b->lhs.get() : b->rhs.get();
        Expr* dstE = op == "==>" ? b->rhs.get() : b->lhs.get();
        Value src = eval(srcE);
        if (dstE->kind == NK::Call) { // append the fed value as the trailing argument
            auto* c = static_cast<Call*>(dstE);
            ValueList args = evalArgs(c->args);
            args.push_back(src);
            if (!c->name.empty()) {
                if (Value* f = tctx_.cur->find("&" + c->name)) return callCallable(*f, args);
                auto it = builtins_.find(c->name);
                if (it != builtins_.end()) return it->second(*this, args);
            }
            if (c->callee) return callCallable(eval(c->callee.get()), args);
            throw RakuError{Value::str("Undefined routine"), "Undefined routine '" + c->name + "'"};
        }
        // ==> my @target (or an existing container): store the fed value
        Value* lv = lvalue(dstE);
        char sig = (dstE->kind == NK::VarExpr && !static_cast<VarExpr*>(dstE)->name.empty()) ? static_cast<VarExpr*>(dstE)->name[0] : '$';
        *lv = sig == '@' ? coerceArray(src) : sig == '%' ? coerceHash(src) : src;
        return *lv;
    }
    if (op == "..." || op == "...^") { // sequence operator: seed [, closure] ... endpoint|*
        Value l = eval(b->lhs.get());
        Value r = eval(b->rhs.get());
        bool exclusive = (op == "...^");
        // A comma-list `a, b, c` is a list of seeds; take its direct elements only
        // (a SHALLOW split — a deep flatten would collapse an array-valued seed like
        // `[1]` to `1` and break an array sequence `[1], -> @b {…} … *`).
        ValueList seed;
        if (l.t == VT::Array) seed = *l.arr;
        else if (l.t == VT::Range) seed = l.flatten();
        else seed = ValueList{l};
        Value gen; bool hasGen = false; // a trailing closure is the generator
        if (!seed.empty() && seed.back().t == VT::Code) { gen = seed.back(); seed.pop_back(); hasGen = true; }
        // a Code endpoint (`… * > 100`) terminates the sequence at the first
        // element it accepts (included for `...`, dropped for `...^`)
        bool endCode = (r.t == VT::Code);
        bool infinite = (r.t == VT::Whatever) || (r.t == VT::Num && std::isinf(r.n));
        double endVal = (infinite || endCode) ? 0 : r.toNum();
        Value out = Value::array(); out.isList = true;
        for (auto& s : seed) out.arr->push_back(s);
        if (out.arr->empty()) return out;
        // String sequence: "a"..."e" climbs via strSucc, "E"..."A" descends via strPred.
        if (!hasGen && !infinite && r.t == VT::Str && seed.back().t == VT::Str) {
            std::string end = r.s, cur = seed.back().s;
            bool desc = cur > end;
            if (cur == end) { if (exclusive) out.arr->pop_back(); return out; }
            const size_t SCAP = 1000000;
            while (out.arr->size() < SCAP) {
                bool ok = true;
                std::string nxt = desc ? strPred(cur, ok) : strSucc(cur);
                if (!ok) break;
                if (!desc && (nxt.length() > end.length() || (nxt.length() == end.length() && nxt > end))) break;
                if (desc && (nxt.length() < end.length() || (nxt.length() == end.length() && nxt < end))) break;
                out.arr->push_back(Value::str(nxt));
                cur = nxt;
                if (cur == end) { if (exclusive) out.arr->pop_back(); break; }
            }
            return out;
        }
        bool allInt = true; for (auto& s : seed) if (s.t != VT::Int && s.t != VT::Bool) allInt = false;
        double step = 1; bool geometric = false; double ratio = 1;
        if (!hasGen) {
            if (seed.size() >= 3) {
                // constant difference => arithmetic; else constant ratio => geometric (1,2,4,8 → ×2)
                bool arith = true, geom = true;
                double d0 = seed[1].toNum() - seed[0].toNum();
                double r0 = seed[0].toNum() != 0 ? seed[1].toNum() / seed[0].toNum() : 0;
                for (size_t i = 1; i + 1 < seed.size(); i++) {
                    if (std::abs((seed[i + 1].toNum() - seed[i].toNum()) - d0) > 1e-9) arith = false;
                    if (seed[i].toNum() == 0 || std::abs(seed[i + 1].toNum() / seed[i].toNum() - r0) > 1e-9) geom = false;
                }
                if (arith) step = d0;
                else if (geom) { geometric = true; ratio = r0; }
                else step = seed.back().toNum() - seed[seed.size() - 2].toNum();
            } else if (seed.size() == 2) step = seed[1].toNum() - seed[0].toNum();
            else if (!infinite && !endCode && out.arr->back().toNum() > endVal) step = -1;
        }
        // Non-numeric seeds (Str / object with .succ) step via succ/pred when the
        // endpoint is Whatever or Code: 'a' ... * ; H.new ... *.y > 10
        bool succSeed = !hasGen && !seed.empty() && (infinite || endCode) &&
                        (seed.back().t == VT::Str || seed.back().t == VT::Object);
        bool succDesc = succSeed && seed.size() >= 2 &&
                        seed[seed.size() - 2].toStr() > seed.back().toStr();
        bool ascending = hasGen ? true : (geometric ? ratio >= 1 : step >= 0);
        long long arity = 1;
        if (hasGen && gen.code) arity = gen.code->whateverArity > 0 ? gen.code->whateverArity
                                       : (gen.code->params ? (long long)gen.code->params->size() : 1);
        // An infinite sequence (`… … *`) is LAZY: keep only the seed materialised and
        // attach a generator that computes one more element on demand.
        if (infinite) {
            auto st = std::make_shared<LazySeqState>();
            Interpreter* self = this;
            st->appendNext = [self, gen, hasGen, geometric, ratio, step, allInt, arity,
                              succSeed, succDesc](ValueList& cache) -> bool {
                if (cache.empty()) return false;
                double lastV = cache.back().toNum();
                Value next;
                if (hasGen) {
                    ValueList args; size_t n = cache.size();
                    for (long long k = arity; k >= 1; k--) { long long idx = (long long)n - k; args.push_back(idx >= 0 ? cache[idx] : Value::integer(0)); }
                    next = self->callCallable(gen, args);
                } else if (succSeed) { // 'a' ... * : step by succ/pred
                    const Value& lastE = cache.back();
                    if (lastE.t == VT::Str) {
                        bool ok = true;
                        std::string s = succDesc ? strPred(lastE.s, ok) : strSucc(lastE.s);
                        if (!ok) return false;
                        next = Value::str(s);
                    } else {
                        ValueList none;
                        next = self->methodCall(lastE, succDesc ? "pred" : "succ", none);
                    }
                } else if (geometric) {
                    double nv = lastV * ratio;
                    next = (allInt && ratio == std::floor(ratio)) ? Value::integer((long long)std::llround(nv)) : Value::number(nv);
                } else {
                    double nv = lastV + step;
                    next = allInt ? Value::integer((long long)nv) : Value::number(nv);
                }
                cache.push_back(next);
                return true;
            };
            out.ext = st;
            return out;
        }
        // Code endpoint: the first accepted element ends the sequence — check the
        // seeds themselves first (`1, 2, 4 ... * > 3` is (1 2 4))
        auto endAccepts = [&](const Value& v) -> bool {
            ValueList a{v}; return callCallable(r, a).truthy();
        };
        if (endCode)
            for (size_t k = 0; k < out.arr->size(); k++)
                if (endAccepts((*out.arr)[k])) {
                    out.arr->resize(exclusive ? k : k + 1);
                    return out;
                }
        const size_t CAP = 1000000;
        while (out.arr->size() < CAP) {
            double lastV = out.arr->back().toNum();
            if (!infinite && !endCode && (ascending ? lastV >= endVal : lastV <= endVal)) break; // reached endpoint
            Value next;
            if (hasGen) {
                ValueList args; size_t n = out.arr->size();
                for (long long k = arity; k >= 1; k--) { long long idx = (long long)n - k; args.push_back(idx >= 0 ? (*out.arr)[idx] : Value::integer(0)); }
                next = callCallable(gen, args);
            } else if (succSeed) { // H.new ... *.y > 10 : step by succ/pred
                const Value& lastE = out.arr->back();
                if (lastE.t == VT::Str) {
                    bool ok = true;
                    std::string s = succDesc ? strPred(lastE.s, ok) : strSucc(lastE.s);
                    if (!ok) break;
                    next = Value::str(s);
                } else {
                    ValueList none;
                    next = methodCall(lastE, succDesc ? "pred" : "succ", none);
                }
            } else if (geometric) {
                double nv = lastV * ratio;
                next = (allInt && ratio == std::floor(ratio)) ? Value::integer((long long)std::llround(nv)) : Value::number(nv);
            } else {
                double nv = lastV + step;
                next = allInt ? Value::integer((long long)nv) : Value::number(nv);
            }
            if (endCode) {
                if (endAccepts(next)) { if (!exclusive) out.arr->push_back(next); break; }
                out.arr->push_back(next);
                continue;
            }
            if (!infinite) { double nv = next.toNum(); if (ascending ? nv > endVal : nv < endVal) break; } // would overshoot
            out.arr->push_back(next);
            if (!infinite && next.toNum() == endVal) break; // hit endpoint exactly
        }
        if (exclusive && !infinite && !endCode && !out.arr->empty() && out.arr->back().toNum() == endVal) out.arr->pop_back();
        return out;
    }
    if (op == "~~" || op == "!~~") {
        // regex match: $str ~~ /pat/   /   $str ~~ s/pat/repl/
        if (b->rhs->kind == NK::RegexLit) {
            Value l = eval(b->lhs.get());
            Value m = regexMatch(l.toStr(), static_cast<RegexLit*>(b->rhs.get())->pattern);
            // `~~` yields the Match on success (Nil on failure); `!~~` yields a Bool
            if (op == "~~") return m.truthy() ? m : Value::nil();
            return Value::boolean(!m.truthy());
        }
        if (b->rhs->kind == NK::SubstLit) {
            auto* sub = static_cast<SubstLit*>(b->rhs.get());
            Value l = eval(b->lhs.get());
            if (isTrSubst(sub->pattern)) { // tr/from/to/ — transliteration, returns the count changed
                long long n; std::string out = translit(l.toStr(), sub->pattern.substr(1), sub->repl, n);
                if (Value* lv = lvalue(b->lhs.get())) *lv = Value::str(out);
                return Value::integer(n);
            }
            long nsub = 0; ValueList noArgs; Value mres;
            std::string out = substSelect(l.toStr(), sub->pattern, nullptr, noArgs, nsub, false, &sub->repl, &mres);
            if (sub->nonMut) return Value::str(out);        // S/// : return new string, leave lhs intact
            if (Value* lv = lvalue(b->lhs.get())) *lv = Value::str(out);
            return mres;                                     // s/// returns the Match / List of matches
        }
        // `X ~~ Y` topicalizes: $_ is bound to X while Y is evaluated (so `$x ~~ .so` works)
        Value lTopic = eval(b->lhs.get());
        Value savedTopic = tctx_.cur->vars.count("$_") ? tctx_.cur->vars["$_"] : Value::any();
        tctx_.cur->define("$_", lTopic);
        Value r;
        try { r = eval(b->rhs.get()); } catch (...) { tctx_.cur->vars["$_"] = savedTopic; throw; }
        tctx_.cur->vars["$_"] = savedTopic;
        // `$path.IO ~~ :e` (and :d/:f/:r/:w/:x/:s/:z/:l) — a filetest adverb: call
        // the matching method on the path and compare to the adverb's boolean.
        if (r.t == VT::Pair && lTopic.hashKind == "IO" && !r.s.empty()) {
            // a missing file makes the test False rather than propagating the throw
            bool actual = false;
            try { actual = boolify(methodCall(lTopic, r.s, {})); } catch (RakuError&) { actual = false; }
            bool want = r.pairVal ? boolify(*r.pairVal) : true;
            bool ok = (actual == want);
            return Value::boolean(op == "~~" ? ok : !ok);
        }
        if (isJunction(r)) {
            // autothread the smartmatch over the junction's eigenstates (each matched
            // with full ~~ semantics, so a junction of regexes / blocks works too)
            int t = 0, total = 0;
            for (auto& e : *r.arr) {
                total++;
                bool m;
                if (e.t == VT::Regex) m = regexMatch(lTopic.toStr(), e.s).truthy();
                else if (e.t == VT::Code) m = boolify(callCallable(e, ValueList{lTopic}));
                else m = applyArith("~~", lTopic, e).truthy();
                if (m) t++;
            }
            bool res = r.enumName == "any" ? t > 0 : r.enumName == "all" ? t == total
                     : r.enumName == "one" ? t == 1 : t == 0;
            return Value::boolean(op == "~~" ? res : !res);
        }
        if (r.t == VT::Regex) {
            Value m = regexMatch(lTopic.toStr(), r.s);
            if (op == "~~") return m.truthy() ? m : Value::nil();
            return Value::boolean(!m.truthy());
        }
        // Regex ~~ Hash / Regex ~~ Array : does the regex match any KEY / element
        if (b->lhs->kind == NK::RegexLit && (r.t == VT::Hash || r.t == VT::Array)) {
            const std::string& pat = static_cast<RegexLit*>(b->lhs.get())->pattern;
            bool res = false;
            if (r.t == VT::Hash && r.hash) {
                for (auto& kv : *r.hash) if (regexMatch(kv.first, pat).truthy()) { res = true; break; }
            } else if (r.arr) {
                for (auto& e : *r.arr) if (regexMatch(e.toStr(), pat).truthy()) { res = true; break; }
            }
            return Value::boolean(op == "~~" ? res : !res);
        }
        if (r.t == VT::Code) {
            // `$x ~~ *.method` / `$x ~~ { … }` / `$x ~~ &c` — call it with $x, match on truthiness
            Value m = callCallable(r, ValueList{lTopic});
            bool ok = boolify(m);
            return Value::boolean(op == "~~" ? ok : !ok);
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
        auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
        scope->define("$_", l);
        auto saved = tctx_.cur; tctx_.cur = scope;
        Value r; try { r = eval(b->rhs.get()); } catch (...) { tctx_.cur = saved; throw; }
        tctx_.cur = saved; return r;
    }
    if (op == "//") {
        Value l = eval(b->lhs.get());
        if (isDefined(l)) return l;
        return eval(b->rhs.get());
    }
    if (op == "^^" || op == "xor") {
        // `^^` has "find the one true value" semantics over the WHOLE chain
        // (list-associative): the single true operand, Nil if more than one is
        // true, the last operand if none is. Flatten `a ^^ b ^^ c` first.
        std::vector<Expr*> chain;
        Expr* cur = b;
        while (cur->kind == NK::Binary &&
               (static_cast<Binary*>(cur)->op == "^^" || static_cast<Binary*>(cur)->op == "xor")) {
            chain.push_back(static_cast<Binary*>(cur)->rhs.get());
            cur = static_cast<Binary*>(cur)->lhs.get();
        }
        chain.push_back(cur);
        std::reverse(chain.begin(), chain.end());
        Value found; bool haveTrue = false, haveAny = false; Value last;
        for (Expr* e : chain) {
            last = eval(e); haveAny = true;
            if (!boolify(last)) continue;
            if (haveTrue) return Value::nil();
            found = last; haveTrue = true;
        }
        (void)haveAny;
        return haveTrue ? found : last;
    }
    if (op == "&" || op == "|" || op == "^") {
        // junction constructors: operands are values, so `rx/a/ & rx/b/` builds a
        // junction of Regex objects (not two matches against $_).
        Value l = evalValueOf(b->lhs.get());
        Value r = evalValueOf(b->rhs.get());
        return applyArith(op, l, r);
    }
    Value l = eval(b->lhs.get());
    Value r = eval(b->rhs.get());
    // operator overloading: a built-in operator on a user object dispatches to a
    // user `sub infix:<op>` if one is in scope (falling back to the built-in when
    // no candidate matches the operands).
    if (l.t == VT::Object || r.t == VT::Object)
        if (Value* f = tctx_.cur->find("&infix:<" + op + ">"))
            try { return callCallable(*f, ValueList{l, r}); }
            catch (RakuError&) {}
    return applyArith(op, l, r);
}

Value Interpreter::mixinValue(Value base, const Value& rhs, bool copy) {
    // Collect the role(s) and attribute Pair(s) from the RHS (a single role type,
    // a list of them, or a `:name(value)` Pair mixing one attribute).
    std::vector<ClassInfo*> roleInfos;
    std::vector<std::string> roleNames;
    std::vector<Value> pairs;
    std::function<void(const Value&)> collect = [&](const Value& v) {
        if (v.t == VT::Type) {
            roleNames.push_back(v.s);
            auto it = classes_.find(v.s);
            if (it != classes_.end()) roleInfos.push_back(it->second.get());
        } else if (v.t == VT::Pair) {
            pairs.push_back(v);
        } else if (v.t == VT::Array && v.arr) {
            for (auto& e : *v.arr) collect(e);
        }
    };
    collect(rhs);

    std::shared_ptr<ObjectData> obj;
    if (base.t == VT::Object && base.obj) {
        obj = base.obj;
        if (copy) { // `but` works on a fresh copy; the original is untouched
            auto nd = std::make_shared<ObjectData>();
            nd->cls = obj->cls;
            nd->attrs = obj->attrs;
            nd->boxed = obj->boxed;
            nd->hasBoxed = obj->hasBoxed;
            obj = nd;
        }
    } else {
        // non-object base (`5 but Role`, `{} does R`): box the value so the mixed
        // object still coerces / dispatches to it. `does`/`but` are both copies here.
        obj = std::make_shared<ObjectData>();
        obj->boxed = base;
        obj->hasBoxed = true;
        auto bc = std::make_shared<ClassInfo>();
        bc->name = base.typeName();
        bc->nativeParent = base.typeName();
        obj->cls = bc;
    }
    // A new anonymous class derived from the current one, composing the role(s).
    auto nc = std::make_shared<ClassInfo>();
    nc->parent = obj->cls;
    std::string suffix;
    for (auto& rn : roleNames) suffix += (suffix.empty() ? "" : ",") + rn;
    nc->name = obj->cls->name + "+{" + suffix + "}";
    for (ClassInfo* role : roleInfos) {
        for (auto& kv : role->methods) nc->methods[kv.first] = kv.second;
        for (auto& sub : role->doneRoles) nc->doneRoles.insert(sub);
        // compose attrs and initialize each to its default, evaluated in the role's
        // own declaration scope (so `role { has $.x = $val }` captures $val).
        for (auto& a : role->attrs) {
            nc->attrs.push_back(a);
            if (obj->attrs.count(a.name)) continue;
            Value dv = Value::any();
            if (a.hasDefVal) dv = a.defVal;
            else if (a.def) {
                auto saved = tctx_.cur;
                if (role->declEnv) tctx_.cur = role->declEnv;
                try { dv = eval(const_cast<Expr*>(a.def)); } catch (...) { dv = Value::any(); }
                tctx_.cur = saved;
            }
            obj->attrs[a.name] = dv;
        }
    }
    for (auto& rn : roleNames) nc->doneRoles.insert(rn);
    // `but :name(value)` — mix one attribute with a public read accessor.
    for (auto& p : pairs) {
        ClassAttr ca; ca.name = p.s; ca.sigil = '$'; ca.pub = true;
        nc->attrs.push_back(ca);
        obj->attrs[p.s] = p.pairVal ? *p.pairVal : Value::any();
    }
    noteSymbolMutation("does/but mixin");
    classes_[nc->name] = nc;
    obj->cls = nc;
    Value out; out.t = VT::Object; out.obj = obj;
    return out;
}

Value Interpreter::evalUnary(Unary* u) {
    // control-flow in expression position: return/last/next/redo
    if (u->op == "return" || u->op == "return-rw") throw ReturnEx{u->operand ? eval(u->operand.get()) : Value::any()};
    if (u->op == "last") throw LastEx{};
    if (u->op == "next") throw NextEx{};
    if (u->op == "redo") throw RedoEx{};
    // reduction metaoperator [op] — and its triangular/scan form [\op]
    if (u->op.size() >= 3 && u->op.front() == '[' && u->op.back() == ']') {
        std::string op = u->op.substr(1, u->op.size() - 2);
        if (op == "=" && u->operand->kind == NK::ListExpr) {
            // [=] $a, $b, $c, 42 — right-to-left chain assignment (needs lvalues)
            auto* le = static_cast<ListExpr*>(u->operand.get());
            if (le->items.empty()) return Value::any();
            Value v = eval(le->items.back().get());
            for (size_t k = le->items.size() - 1; k-- > 0; )
                if (Value* lvp = lvalue(le->items[k].get())) *lvp = v;
            return v;
        }
        if ((op == "=:=" || op == "!=:=") && u->operand->kind == NK::ListExpr) {
            // container-identity chain over variables ([=:=] $x, $y, $x)
            auto* le = static_cast<ListExpr*>(u->operand.get());
            bool allVars = !le->items.empty();
            for (auto& it : le->items) if (it->kind != NK::VarExpr) allVars = false;
            if (allVars) {
                bool neg = op[0] == '!';
                Value* prev = nullptr;
                for (size_t k = 0; k < le->items.size(); k++) {
                    Value* p = nullptr;
                    try { p = lvalue(le->items[k].get()); } catch (RakuError&) {}
                    if (k > 0) {
                        bool same = prev && p && prev == p;
                        if (neg ? same : !same) return Value::boolean(false);
                    }
                    prev = p;
                }
                return Value::boolean(true);
            }
        }
        // Flatten like a slurpy arg list: @-vars/ranges/inner lists spread, but a
        // $-held container or an [..] literal stays ONE item ([===] $a, $a, [1,2]).
        ValueList items;
        auto pushFlat = [&](Value v, bool item) {
            if (!item && (v.t == VT::Array || v.t == VT::Range))
                for (auto& x : v.flatten()) items.push_back(x);
            else items.push_back(std::move(v));
        };
        if (u->operand->kind == NK::ListExpr) {
            auto* le = static_cast<ListExpr*>(u->operand.get());
            for (auto& ie : le->items) {
                bool item = (ie->kind == NK::VarExpr &&
                             !static_cast<VarExpr*>(ie.get())->name.empty() &&
                             static_cast<VarExpr*>(ie.get())->name[0] == '$') ||
                            ie->kind == NK::ArrayLit;
                pushFlat(eval(ie.get()), item);
            }
        }
        else pushFlat(eval(u->operand.get()), false);
        return applyReduce(op, items);
    }
    if (u->op == "ctx$" || u->op == "ctx@" || u->op == "ctx%") {
        Value v = eval(u->operand.get());
        if (u->op == "ctx@") {
            // `@<name>` (a single named capture in list context) is that match as a
            // 1-element list, not its positional sub-captures — so `@<x>».ast` works.
            if (v.t == VT::Match) { Value a = Value::array(); a.arr->push_back(v); a.isList = true; return a; }
            if (v.t == VT::Any || v.t == VT::Nil) { Value a = Value::array(); a.isList = true; return a; } // @<undefined> = ()
            // one-level list context: an array yields its top-level elements (nested
            // itemized arrays stay intact); a Range flattens; a scalar becomes (x,).
            if (v.t == VT::Array && v.arr) { Value a = Value::array(*v.arr); a.isList = true; return a; }
            if (v.t == VT::Range) return Value::array(v.flatten());
            Value a = Value::array(); a.arr->push_back(v); a.isList = true; return a;
        }
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
            tctx_.cur->define("$!", Value::nil());
            return r;
        } catch (RakuError& e) {
            tctx_.cur->define("$!", exceptionFor(e));
            return Value::nil();
        }
    }
    if (u->op == "quietly") { // suppress warn() output within the block
        quietDepth_++;
        try {
            Value r = u->operand->kind == NK::BlockExpr
                ? callCallable(makeClosure(static_cast<BlockExpr*>(u->operand.get())), {})
                : eval(u->operand.get());
            quietDepth_--;
            return r;
        }
        catch (...) { quietDepth_--; throw; }
    }
    if (u->op == "gather") {
        Unary* gu = u;
        Value blockClosure;
        if (u->operand->kind == NK::BlockExpr)
            blockClosure = makeClosure(static_cast<BlockExpr*>(u->operand.get()));
        // Run the gather block, collecting takes up to `limit` (0 = unlimited).
        // Returns true if the limit was hit (the block may have more to give — i.e.
        // it's infinite or larger than the limit).
        auto runGather = [this, gu, blockClosure](size_t limit, ValueList& out) -> bool {
            auto collector = std::make_shared<ValueList>();
            tctx_.gatherStack.push_back(collector);
            tctx_.gatherLimits.push_back(limit);
            bool hit = false;
            try {
                if (gu->operand->kind == NK::BlockExpr) callCallable(blockClosure, {});
                else eval(gu->operand.get());
            } catch (StopGatherEx&) { hit = true; }
              catch (...) { tctx_.gatherStack.pop_back(); tctx_.gatherLimits.pop_back(); throw; }
            tctx_.gatherStack.pop_back(); tctx_.gatherLimits.pop_back();
            out = std::move(*collector);
            return hit;
        };
        const size_t INITIAL = 10000;
        ValueList prefix;
        // finite gather (terminates within the cap): eager, exactly as before
        if (!runGather(INITIAL, prefix)) return Value::array(std::move(prefix));
        // hit the cap → treat as lazy: keep the prefix and extend on demand by
        // re-running the block with a larger cap (re-run because there are no
        // coroutines; fine for the usual pure generator `gather { loop { take … } }`).
        Value arr = Value::array(prefix); arr.isList = true;
        auto st = std::make_shared<LazySeqState>();
        st->appendNext = [this, runGather](ValueList& out) -> bool {
            ValueList grown;
            bool more = runGather(out.size() + 4096, grown);
            for (size_t i = out.size(); i < grown.size(); i++) out.push_back(grown[i]);
            return more;
        };
        arr.ext = st;
        return arr;
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
            if (lv->natBits) wrapNative(newv, lv->natBits, lv->natSigned); // native int wraparound
        }
        *lv = newv;
        return u->postfix ? oldv : newv;
    }
    Value v = eval(u->operand.get());
    // Whatever-currying for prefix ops: `~*`, `-*`, `+*`, `?*`, `!*` become a
    // WhateverCode (e.g. `.sort: ~*` sorts by stringification).
    if ((v.t == VT::Whatever || (v.t == VT::Code && v.code && v.code->isWhateverCode)) &&
        (u->op == "~" || u->op == "-" || u->op == "+" || u->op == "?" || u->op == "!" ||
         u->op == "so" || u->op == "not")) {
        Value inner = v; std::string op = u->op;
        Value code; code.t = VT::Code; code.code = std::make_shared<Callable>(); code.code->isWhateverCode = true;
        code.code->builtin = [inner, op](Interpreter& I, ValueList& a) -> Value {
            Value arg = a.empty() ? Value::any() : a[0];
            Value b = arg;
            if (inner.t == VT::Code && inner.code && inner.code->isWhateverCode) b = I.callCallable(inner, ValueList{arg});
            if (op == "~") return Value::str(b.toStr());
            if (op == "-") return b.t == VT::Int ? Value::integer(-b.toInt()) : Value::number(-b.toNum());
            if (op == "+") return b.isNumeric() ? b : Value::number(b.toNum());
            if (op == "?" || op == "so") return Value::boolean(b.truthy());
            return Value::boolean(!b.truthy()); // ! / not
        };
        return code;
    }
    // postfix:<i> — multiply by the imaginary unit: (3)i, (2i)i → -2
    if (u->op == "i" && u->postfix) return postfixI(std::move(v));
    // numeric prefix on an object uses its .Numeric (or .Bridge/.Int): `+$o`, `-$o`
    if ((u->op == "+" || u->op == "-") && v.t == VT::Object && v.obj && v.obj->cls) {
        for (const char* nm : {"Numeric", "Bridge", "Int"})
            if (Value* m = v.obj->cls->findMethod(nm)) {
                ValueList none; Value n = invokeMethod(*m, v, none);
                if (u->op == "-") return n.t == VT::Int ? Value::integer(-n.toInt()) : Value::number(-n.toNum());
                return n;
            }
    }
    // Numeric context of a list/array/hash/range is its element count —
    // except a Proc / Proc::Async, which numifies to its exit status (+$proc).
    if ((u->op == "+" || u->op == "-") &&
        (v.t == VT::Array || v.t == VT::Hash || v.t == VT::Range) &&
        !(v.t == VT::Hash && (v.hashKind == "Proc" || v.hashKind == "Proc::Async"))) {
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
    if (u->op == "+") {
        if (v.t == VT::Bool) return Value::integer(v.b ? 1 : 0); // +True == 1
        return v.isNumeric() ? v : (v.t == VT::Str ? numifyStr(v.s) : Value::number(v.toNum()));
    }
    if (u->op == "~") return Value::str(strOf(v)); // honour a user Str/gist / Exception .message
    if (u->op == "!") return Value::boolean(!boolify(v));
    if (u->op == "?") return Value::boolean(boolify(v));
    if (u->op == "^") return Value::range(0, v.toInt(), false, true);
    if (u->op == "|") return v; // slip: spread handled in evalArgs
    // user-defined prefix operator: `sub prefix:<§>($x) { … }`
    if (Value* f = tctx_.cur->find("&prefix:<" + u->op + ">"))
        return callCallable(*f, ValueList{v});
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
            // |@list slips positionally (Pair elements stay positional); |%hash slips as named args.
            if (v.t == VT::Array || v.t == VT::Range) { for (auto& x : v.flatten()) args.push_back(x); }
            else if (v.t == VT::Hash && v.hash) { for (auto& kv : *v.hash) { Value p = Value::pair(kv.first, kv.second); p.namedArg = true; args.push_back(std::move(p)); } }
            else args.push_back(v);
        } else {
            Value v = eval(a.get());
            // Only a syntactic pair (k=>v / :k(v), i.e. a NK::Pair expression) whose key
            // is a bare identifier is a NAMED argument; a Pair value from a variable/
            // call/list — or with a non-identifier key (`3 => 4`) — is positional.
            if (v.t == VT::Pair && a->kind == NK::Pair) {
                const std::string& k = static_cast<PairExpr*>(a.get())->key;
                bool ident = !k.empty() && (std::isalpha((unsigned char)k[0]) || k[0] == '_');
                for (size_t ci = 1; ident && ci < k.size(); ci++)
                    if (!std::isalnum((unsigned char)k[ci]) && k[ci] != '-' && k[ci] != '_' && k[ci] != '\'')
                        ident = false;
                if (ident) v.namedArg = true;
            }
            args.push_back(std::move(v));
        }
    }
    return args;
}

// Stringify honouring user-defined `method gist` / `method Str` (Raku: say/note use
// .gist; print/put/string interpolation use .Str, which itself falls back to .gist).
// The value stored into $! / $_ when an error is caught. A bare exception TYPE
// payload (`throw RakuError{Value::typeObj("X::Foo"), msg}`) would be an
// UNDEFINED type object — `$!.defined` must be True and `.message` must answer,
// so wrap it into a defined instance of that class (registered on the fly).
Value Interpreter::exceptionFor(const RakuError& e) {
    if (e.payload.t != VT::Type) return e.payload; // already a value/object (die $obj / die "msg")
    std::string tn = e.payload.s.empty() ? "X::AdHoc" : e.payload.s;
    std::shared_ptr<ClassInfo> ci;
    auto it = classes_.find(tn);
    if (it != classes_.end()) ci = it->second;
    else {
        ci = std::make_shared<ClassInfo>();
        ci->name = tn;
        ClassAttr a; a.name = "message"; a.sigil = '$'; a.pub = true;
        ci->attrs.push_back(a);
        classes_[tn] = ci;
    }
    auto od = std::make_shared<ObjectData>();
    od->cls = ci;
    od->attrs["message"] = Value::str(e.message);
    return Value::object(od);
}

std::string Interpreter::gistOf(const Value& v) {
    if (v.t == VT::Object && v.obj && v.obj->cls) {
        if (Value* m = v.obj->cls->findMethod("gist")) { ValueList none; return invokeMethod(*m, v, none).toStr(); }
        // exceptions gist to their message (`say $!` prints "boom", not X::AdHoc<obj>)
        if (v.obj->cls->name.rfind("X::", 0) == 0) {
            auto it = v.obj->attrs.find("message");
            if (it != v.obj->attrs.end()) return it->second.toStr();
        }
    }
    if (v.t == VT::Object && v.obj && v.obj->hasBoxed) return gistOf(v.obj->boxed);
    return v.gist();
}
std::string Interpreter::strOf(const Value& v) {
    if (v.t == VT::Object && v.obj && v.obj->cls) {
        for (const char* nm : {"Str", "gist", "Stringy"}) // ~$o uses .Stringy, print uses .Str
            if (Value* m = v.obj->cls->findMethod(nm)) { ValueList none; return invokeMethod(*m, v, none).toStr(); }
        // an Exception stringifies to its .message (Raku: Exception.Str is .message),
        // whether message is a method or a plain attribute.
        if (Value* m = v.obj->cls->findMethod("message")) { ValueList none; return invokeMethod(*m, v, none).toStr(); }
        auto mit = v.obj->attrs.find("message");
        if (mit != v.obj->attrs.end()) return strOf(mit->second);
        if (v.obj->hasBoxed) return strOf(v.obj->boxed);
    }
    return v.toStr();
}

Value Interpreter::evalCall(Call* c) {
    ValueList args = evalArgs(c->args);
    if (c->callee) {
        Value f = eval(c->callee.get());
        return callCallable(f, args, &c->args);
    }
    if (!c->name.empty()) {
        // `temp $x` / `temp @a` — snapshot the container now, restore it when the current
        // scope leaves (dynamic-scope save, like Perl's local). Modifications persist until then.
        if (c->name == "temp" && c->args.size() == 1 && !tctx_.cur->find("&temp")) {
            if (Value* lv = lvalue(c->args[0].get())) {
                Value snapshot = *lv; // deep-copy containers so later mutation doesn't touch the snapshot
                if (snapshot.t == VT::Array && snapshot.arr) snapshot.arr = std::make_shared<ValueList>(*snapshot.arr);
                else if (snapshot.t == VT::Hash && snapshot.hash) snapshot.hash = std::make_shared<std::map<std::string, Value>>(*snapshot.hash);
                tctx_.cur->tempRestores.push_back([lv, snapshot]() { *lv = snapshot; });
                return *lv;
            }
        }
        // undefine($x) resets its argument container to the type's undefined value
        if (c->name == "undefine" && !c->args.empty() && !tctx_.cur->find("&undefine")) {
            Expr* t = c->args[0].get();
            char sig = (t->kind == NK::VarExpr && !static_cast<VarExpr*>(t)->name.empty()) ? static_cast<VarExpr*>(t)->name[0] : '$';
            if (sig != '$' && sig != '@' && sig != '%')
                throw RakuError{Value::typeObj("X::Assignment::RO"), "Cannot undefine an immutable value"};
            if (Value* lv = lvalue(t)) *lv = (sig == '@') ? Value::array() : (sig == '%') ? Value::makeHash() : Value::any();
            return Value::any();
        }
        // atomic ops on an `atomicint` container. Under the GIL these are plain
        // read-modify-write (the lock already serialises them); they take the
        // container by reference, so operate on its lvalue.
        if (c->name.rfind("atomic-", 0) == 0 && !c->args.empty() && !tctx_.cur->find("&" + c->name)) {
            if (Value* lv = lvalue(c->args[0].get())) {
                long long cur = lv->toInt();
                auto argN = [&](size_t i) { return c->args.size() > i ? eval(c->args[i].get()).toInt() : 0; };
                if (c->name == "atomic-fetch")     return *lv;
                if (c->name == "atomic-fetch-inc") { *lv = Value::integer(cur + 1);        return Value::integer(cur); }
                if (c->name == "atomic-fetch-dec") { *lv = Value::integer(cur - 1);        return Value::integer(cur); }
                if (c->name == "atomic-inc-fetch") { *lv = Value::integer(cur + 1);        return *lv; }
                if (c->name == "atomic-dec-fetch") { *lv = Value::integer(cur - 1);        return *lv; }
                if (c->name == "atomic-fetch-add") { *lv = Value::integer(cur + argN(1));  return Value::integer(cur); }
                if (c->name == "atomic-fetch-sub") { *lv = Value::integer(cur - argN(1));  return Value::integer(cur); }
                if (c->name == "atomic-assign")    { Value v = c->args.size() > 1 ? eval(c->args[1].get()) : Value::any(); *lv = v; return v; }
            }
        }
        if (Value* f = tctx_.cur->find("&" + c->name)) return callCallable(*f, args, &c->args);
        auto it = builtins_.find(c->name);
        if (it != builtins_.end()) return it->second(*this, args);
        // enum-type coercion: `Color(1)` -> the enum value whose number is 1
        if (Value* v = tctx_.cur->find(c->name); v && !v->enumType.empty() && v->t == VT::Array && !args.empty()) {
            long long want = args[0].toInt();
            if (v->arr) for (auto& pr : *v->arr)
                if (pr.t == VT::Pair && pr.pairVal && pr.pairVal->toInt() == want) {
                    Value ev = Value::enumVal(pr.s, want); ev.enumType = v->enumType; return ev;
                }
            return Value::typeObj(v->enumType); // out of range -> the type object
        }
    }
    // operator-call form: infix:<+>(1,2) / postfix:<i>($x) / prefix:<[**]>(2,3,4)
    if (c->name.rfind("infix:<", 0) == 0 && c->name.back() == '>') {
        std::string op = c->name.substr(7, c->name.size() - 8);
        return args.size() >= 2 ? applyBinOp(op, args[0], args[1])
             : args.size() == 1 ? args[0] : Value::any();
    }
    if (c->name == "postfix:<i>" && !args.empty()) return postfixI(args[0]);
    if (c->name.rfind("prefix:<[", 0) == 0 && c->name.size() > 11 &&
        c->name.compare(c->name.size() - 2, 2, "]>") == 0) {
        std::string op = c->name.substr(9, c->name.size() - 11); // the reduce base op
        ValueList items; for (auto& a : args) for (auto& x : a.flatten()) items.push_back(x);
        return applyReduce(op, items);
    }
    // coercion-type functions: Str(x), Int(x), Num(x), Bool(x), … and the no-arg
    // defaults Str()=='' / Int()==0 / Num()==0e0 / Bool()==False.
    {
        static const std::set<std::string> coerce = {"Str", "Int", "Num", "Bool", "Numeric", "Real", "Rat"};
        if (coerce.count(c->name)) {
            if (args.empty())
                return c->name == "Str" ? Value::str("")
                     : c->name == "Bool" ? Value::boolean(false)
                     : c->name == "Num" ? Value::number(0) : Value::integer(0);
            Value a0 = args[0];
            if (c->name == "Str") return Value::str(strOf(a0));
            if (c->name == "Bool") return Value::boolean(boolify(a0));
            if (a0.t == VT::Object && a0.obj && a0.obj->cls) { // honour .Int/.Num/.Numeric
                if (Value* m = a0.obj->cls->findMethod(c->name)) { ValueList none; a0 = invokeMethod(*m, a0, none); }
            }
            if (a0.t == VT::Complex && (c->name == "Int" || c->name == "Num" ||
                                        c->name == "Real" || c->name == "Rat"))
                return methodCall(a0, c->name, {}); // $*TOLERANCE-aware imaginary-part check
            if (c->name == "Int") return Value::integer(a0.toInt());
            if (c->name == "Num" || c->name == "Real") return Value::number(a0.toNum());
            if (c->name == "Rat") return methodCall(a0, "Rat", {}); // CF approximation for Num
            return a0.isNumeric() ? a0 : Value::number(a0.toNum()); // Numeric
        }
    }
    throw RakuError{Value::str("Undefined routine &" + c->name),
                    "Undefined routine '" + c->name + "'"};
}

// [op] reduce over an item list — shared by the [op] unary, prefix:<[op]>(…)
// calls, and &prefix:<[op]> references. `op` may carry a leading '\' (scan form).
Value Interpreter::applyReduce(std::string op, ValueList& items) {
    bool scan = !op.empty() && op.front() == '\\'; // [\+] : running partial reductions
    if (scan) op = op.substr(1);
    // comparison reduces CHAIN pairwise ([<] 1,2,3 == 1<2 && 2<3); a leading
    // `!` negates each pairwise test ([!=:=] $x,$y,$x == $x !=:= $y && $y !=:= $x)
    static const std::set<std::string> chainOps = {
        "<", "<=", ">", ">=", "==", "!=", "eq", "ne", "lt", "le", "gt", "ge",
        "=:=", "===", "eqv", "before", "after", "~~"};
    bool neg = op.size() > 1 && op[0] == '!' && op != "!=" && op != "!==";
    std::string base = neg ? op.substr(1) : op;
    if (scan) { // yield (a, a op b, a op b op c, …)
        Value out = Value::array(); out.isList = true;
        if (items.empty()) return out;
        if (op == "**") { // right-assoc scan: suffix reductions — [\**] 1,2,3 → (3 8 1)
            Value acc = items.back();
            out.arr->push_back(acc);
            for (size_t k = items.size() - 1; k-- > 0; ) {
                acc = applyBinOp(op, items[k], acc);
                out.arr->push_back(acc);
            }
            return out;
        }
        if (op == "^^" || op == "xor") { // prefix one-xor: [\^^] 1,1,x → (1 Nil Nil)
            for (size_t k = 0; k < items.size(); k++) {
                ValueList pre(items.begin(), items.begin() + k + 1);
                out.arr->push_back(applyReduce(op, pre));
            }
            return out;
        }
        if (chainOps.count(base)) { // sticky chain: [\<] 1,3,2,4 → (True True False False)
            bool ok = true;
            out.arr->push_back(Value::boolean(true)); // a single element chains truthfully
            for (size_t k = 1; k < items.size(); k++) {
                bool c = applyBinOp(base, items[k - 1], items[k]).truthy();
                if (neg) c = !c;
                ok = ok && c;
                out.arr->push_back(Value::boolean(ok));
            }
            return out;
        }
        Value acc = items[0];
        out.arr->push_back(acc);
        for (size_t k = 1; k < items.size(); k++) { acc = applyBinOp(op, acc, items[k]); out.arr->push_back(acc); }
        return out;
    }
    if (op == "min" || op == "max") { // undefined items are skipped; empty → ±Inf
        ValueList def;
        for (auto& it : items) if (isDefined(it)) def.push_back(it);
        if (def.empty()) return Value::number(op == "min" ? INFINITY : -INFINITY);
        Value acc = def[0];
        for (size_t k = 1; k < def.size(); k++) acc = applyBinOp(op, acc, def[k]);
        return acc;
    }
    if (items.empty()) {
        // hyper form empty ([>>+<<]) falls back to the inner op's identity
        if (op.size() >= 5 && (op.compare(0, 2, ">>") == 0 || op.compare(0, 2, "<<") == 0) &&
            (op.compare(op.size() - 2, 2, ">>") == 0 || op.compare(op.size() - 2, 2, "<<") == 0))
            return applyReduce(op.substr(2, op.size() - 4), items);
        if (op == "+" || op == "-") return Value::integer(0);
        if (op == "*" || op == "/") return Value::integer(1);
        if (op == "~") return Value::str("");
        return Value::any();
    }
    if (chainOps.count(base)) {
        for (size_t k = 1; k < items.size(); k++) {
            bool ok = applyBinOp(base, items[k - 1], items[k]).truthy();
            if (neg) ok = !ok;
            if (!ok) return Value::boolean(false);
        }
        return Value::boolean(true);
    }
    if (op == ",") { Value out = Value::list(items); return out; } // [,] : the list itself
    if (op == "^^" || op == "xor") {
        // one-xor is list-aware: exactly one truthy item → that item;
        // none truthy → the last item; more than one → Nil
        const Value* found = nullptr;
        for (auto& it : items) {
            if (!it.truthy()) continue;
            if (found) return Value::nil();
            found = &it;
        }
        return found ? *found : items.back();
    }
    // right-associative reduces fold from the right: [**] 2,3,2 == 2**(3**2),
    // [=>] 1,2,3 == 1 => (2 => 3)
    if (op == "=>" || op == "**") {
        Value acc = items.back();
        for (size_t k = items.size() - 1; k-- > 0; ) {
            if (op == "=>") { Value p = Value::pair(items[k].toStr(), acc); acc = p; }
            else acc = applyBinOp(op, items[k], acc);
        }
        return acc;
    }
    Value acc = items[0];
    for (size_t k = 1; k < items.size(); k++) acc = applyBinOp(op, acc, items[k]);
    return acc;
}

double Interpreter::toleranceDyn() {
    Value* tp = nullptr;
    for (auto it = tctx_.dynStack.rbegin(); it != tctx_.dynStack.rend() && !tp; ++it)
        tp = (*it)->find("$*TOLERANCE");
    if (!tp && tctx_.cur) tp = tctx_.cur->find("$*TOLERANCE"); // `my $*TOLERANCE` is lexical
    return tp ? tp->toNum() : 1e-15;
}

// postfix:<i> — multiply by the imaginary unit; honours .Numeric/.Bridge on
// objects and type objects (`postfix:<i>(class :: does Numeric {…})`).
Value Interpreter::postfixI(Value v) {
    if (v.t == VT::Complex) return Value::complex(-v.im, v.n);
    if (!v.isNumeric()) {
        ClassInfo* ci = nullptr;
        if (v.t == VT::Object && v.obj) ci = v.obj->cls.get();
        else if (v.t == VT::Type) { auto it = classes_.find(v.s); if (it != classes_.end()) ci = it->second.get(); }
        if (ci)
            for (const char* nm : {"Numeric", "Bridge"})
                if (Value* m = ci->findMethod(nm)) { ValueList none; v = invokeMethod(*m, v, none); break; }
        if (v.t == VT::Complex) return Value::complex(-v.im, v.n);
    }
    return Value::complex(0.0, v.toNum());
}

Value Interpreter::evalIndex(Index* idx) {
    Value base = eval(idx->base.get());
    // parameterizing a type at runtime: array[$T] / Hash[$K] — yields Type[param]
    if (base.t == VT::Type && !idx->isHash && idx->index) {
        Value p = eval(idx->index.get());
        if (p.t == VT::Type) {
            Value ty = Value::typeObj(base.s);
            ty.ofType = base.ofType.empty() ? p.s : base.ofType + "," + p.s;
            return ty;
        }
    }
    // a native-container subclass / `but`/`does` mixin instance indexes through its box
    if (base.t == VT::Object && base.obj && base.obj->hasBoxed) base = base.obj->boxed;
    // subscripting an infinite range (…..Inf) — index its lazy @-array form so
    // nothing materialises the whole range.
    if (base.t == VT::Range && base.rTo >= 9000000000000000000LL && !idx->isHash)
        base = methodCall(base, "list", {});

    // A lazy list (infinite `… … *` / lazy `.map`): grow its prefix to cover the
    // requested index/slice before subscripting.
    if (base.t == VT::Array && base.ext && !idx->isHash) {
        Value iv = eval(idx->index.get());
        // an infinite lazy array has no end: `@a[*-1]` / `@a[*]` can't be indexed
        if (std::static_pointer_cast<LazySeqState>(base.ext)->infinite &&
            (iv.t == VT::Whatever || (iv.t == VT::Code && iv.code && iv.code->isWhateverCode)))
            throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot use a Whatever index on an infinite list"};
        long long maxi = -1;
        if (iv.t == VT::Int || iv.t == VT::Num || iv.t == VT::Bool) maxi = iv.toInt();
        else if (iv.t == VT::Range || iv.t == VT::Array) for (auto& e : iv.flatten()) if (e.t == VT::Int || e.t == VT::Num) maxi = std::max(maxi, e.toInt());
        if (maxi >= 0) materializeLazy(base, (size_t)maxi + 1);
    }

    // Whatever-currying for subscripts: `*.<key>` / `*<key>` / `*[i]` yield a
    // WhateverCode that indexes its argument (mirrors `*.method`).
    if (base.t == VT::Whatever || (base.t == VT::Code && base.code && base.code->isWhateverCode)) {
        bool isHash = idx->isHash;
        Value keyv = eval(idx->index.get());
        Value inner = base;
        Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
        code.code->isWhateverCode = true;
        code.code->builtin = [inner, isHash, keyv](Interpreter& I, ValueList& a) -> Value {
            Value arg = a.empty() ? Value::any() : a[0];
            Value b = arg;
            if (inner.t == VT::Code && inner.code && inner.code->isWhateverCode) b = I.callCallable(inner, ValueList{arg});
            if (isHash) {
                std::string k = keyv.toStr();
                if ((b.t == VT::Hash || b.t == VT::Match) && b.hash) { auto it = b.hash->find(k); return it != b.hash->end() ? it->second : Value::nil(); }
                return Value::nil();
            }
            long long n = keyv.toInt();
            if ((b.t == VT::Array || b.t == VT::Match) && b.arr) { if (n < 0) n += (long long)b.arr->size(); if (n >= 0 && n < (long long)b.arr->size()) return (*b.arr)[n]; }
            return Value::nil();
        };
        return code;
    }

    // Hash whatever-slice `%h{*}` (all top-level values) and hyperslice `%h{**}`
    // (all leaf values, descending nested hashes), with :k/:v/:kv/:p adverbs.
    if (idx->isHash && base.t == VT::Hash && base.hash && idx->index && idx->index->kind == NK::Whatever) {
        bool hyper = static_cast<const WhateverExpr*>(idx->index.get())->hyper;
        std::vector<std::pair<std::string, Value>> leaves;
        std::function<void(const Value&)> walk = [&](const Value& h) {
            if (h.t != VT::Hash || !h.hash) return;
            for (auto& kv : *h.hash) {
                if (hyper && kv.second.t == VT::Hash && kv.second.hash) walk(kv.second);
                else leaves.push_back({kv.first, kv.second});
            }
        };
        walk(base);
        const std::string& adv = idx->adverb;
        Value o = Value::array(); o.isList = true;
        for (auto& lv : leaves) {
            if (adv == "k") o.arr->push_back(Value::str(lv.first));
            else if (adv == "kv") { o.arr->push_back(Value::str(lv.first)); o.arr->push_back(lv.second); }
            else if (adv == "p") o.arr->push_back(Value::pair(lv.first, lv.second));
            else o.arr->push_back(lv.second); // :v or no adverb → the leaf values
        }
        return o;
    }

    // `@a[|| @dims]` / `%h{|| @keys}` — navigate nested dimensions from a runtime list
    // (each element is the index/key for one level).
    if (idx->index && idx->index->kind == NK::Unary && static_cast<const Unary*>(idx->index.get())->op == "dimslip") {
        Value dims = eval(static_cast<const Unary*>(idx->index.get())->operand.get());
        ValueList path = (dims.t == VT::Array && dims.arr) ? *dims.arr : (dims.t == VT::Range ? dims.flatten() : ValueList{dims});
        Value cur = base;
        for (auto& step : path) cur = rtIndexGet(cur, step, cur.t == VT::Hash);
        return cur;
    }

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
        // Adverbs may be stacked (`:exists:kv:$delete`) and each may be the
        // variable form ($name: active while the variable is true) or negated.
        bool wantExists = false, negExists = false, wantDelete = false;
        bool kvF = false, pF = false, kF = false, vF = false;
        {
            std::string rest = idx->adverb;
            while (!rest.empty()) {
                size_t c = rest.find(':');
                std::string part = c == std::string::npos ? rest : rest.substr(0, c);
                rest = c == std::string::npos ? "" : rest.substr(c + 1);
                bool neg = !part.empty() && part[0] == '!';
                if (neg) part = part.substr(1);
                if (!part.empty() && part[0] == '$') { // conditional: on iff the var is true
                    Value* vp = tctx_.cur->find(part);
                    bool on = vp && vp->truthy();
                    if (neg) on = !on;
                    if (!on) continue;
                    part = part.substr(1);
                    neg = false;
                }
                if (part == "exists") { wantExists = true; negExists = neg; }
                else if (part == "delete") wantDelete = true;
                else if (part == "kv") kvF = true;
                else if (part == "p") pF = true;
                else if (part == "k") kF = true;
                else if (part == "v") vF = true;
            }
        }
        if (wantExists || wantDelete || kvF || pF || kF || vF) {
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
        if (wantDelete && exists) {
            if (idx->isHash) base.hash->erase(key);
            else (*base.arr)[ai] = Value::any();
        }
        if (wantExists) {
            Value ex = Value::boolean(negExists ? !exists : exists);
            if (kvF) { Value o = Value::array({keyV, ex}); o.isList = true; return o; }
            if (pF) return Value::pair(keyV.toStr(), ex);
            return ex;
        }
        if (kF) return exists ? keyV : Value::array();
        if (vF) return exists ? val : Value::array();
        if (kvF) { Value o = exists ? Value::array({keyV, val}) : Value::array(); o.isList = true; return o; }
        if (pF) return exists ? Value::pair(keyV.toStr(), val) : Value::array();
        return exists ? val : Value::any(); // plain :delete (or all conditionals off after delete)
        }
    }

    if (idx->isHash) {
        // Associative indexing on an array-backed value: a Capture (`\(1, :i)`) is
        // stored as an Array of positionals + Pairs, and `c<i>` finds the named part.
        // On a plain Array with no such named element it's a type error (`$aref<0>`).
        if (base.t == VT::Array) {
            Value iv = eval(idx->index.get());
            std::string key = iv.toStr();
            bool capturish = false; // holds named parts (Pairs)? then it's Capture-like
            if (base.arr)
                for (auto& el : *base.arr)
                    if (el.t == VT::Pair) {
                        capturish = true;
                        if (el.s == key) return el.pairVal ? *el.pairVal : Value::any();
                    }
            if (capturish) return Value::any(); // Capture without that named part
            throw RakuError{Value::typeObj("X::AdHoc"), "Type Array does not support associative indexing"};
        }
        Value iv = eval(idx->index.get());
        auto lookup1 = [&](const std::string& key) -> Value {
            if (base.t == VT::Hash && base.hash) {
                auto it = base.hash->find(key);
                if (it != base.hash->end()) return it->second;
            }
            if (base.t == VT::Hash && !base.hashKind.empty()) // Set/Bag/Mix typed default
                return base.hashKind.find("Set") == 0 ? Value::boolean(false) : Value::integer(0);
            if (!base.ofType.empty()) return typedElemDefault(base); // Hash[Int] -> Int
            return Value::any();
        };
        // hash slice: %h{'a','b'} / %h<a b> — multiple keys yield a list of values
        if (iv.t == VT::Array || iv.t == VT::Range) {
            Value out = Value::array(); out.isList = true;
            for (auto& k : iv.flatten()) out.arr->push_back(lookup1(k.toStr()));
            return out;
        }
        return lookup1(iv.toStr());
    }
    // positional [0] on a non-Positional item returns the item itself: `$hashref[0] === $hashref`
    if (!idx->isHash && base.t == VT::Hash && base.hashKind.empty()) {
        Value iv = eval(idx->index.get());
        if (iv.t != VT::Array && iv.t != VT::Range && iv.toInt() == 0) return base;
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
            // `*` in an endpoint resolves to the list length: `@a[0 .. *-2]`, `@a[*-3 .. *-1]`.
            auto resolveWhat = [&](Value v) -> long long {
                if (v.t == VT::Code && v.code && v.code->isWhateverCode) return callCallable(v, ValueList{Value::integer(n)}).toInt();
                if (v.t == VT::Whatever || std::isinf(v.toNum())) return n - 1;
                return v.toInt();
            };
            long long from = resolveWhat(eval(re->from.get()));
            long long to = resolveWhat(eval(re->to.get()));
            if (re->exTo) to--;
            if (from < 0) from += n;
            isSlice = true;
            for (long long k = from; k <= to; k++) indices.push_back(k);
        } else {
            Value iv = eval(idx->index.get());
            // Whatever-star index: `@a[*-1]` (WhateverCode called with the length),
            // `@a[*]` (all elements).
            if (iv.t == VT::Code && iv.code && iv.code->isWhateverCode)
                iv = callCallable(iv, ValueList{Value::integer(n)});
            if (iv.t == VT::Whatever) {
                isSlice = true;
                for (long long k = 0; k < n; k++) indices.push_back(k);
            } else if (iv.t == VT::Range || iv.t == VT::Array) {
                isSlice = true;
                for (auto& e : iv.flatten()) indices.push_back(e.toInt());
            } else {
                long long i = iv.toInt();
                if (base.t == VT::Str) { if (i < 0) i += (long long)base.s.size(); return (i >= 0 && i < (long long)base.s.size()) ? Value::str(std::string(1, base.s[i])) : Value::any(); }
                if (i < 0) i += n;
                return (i >= 0 && i < n) ? src[i] : (base.ofType.empty() ? Value::any() : typedElemDefault(base));
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
        case NK::NumLit: { auto* nl = static_cast<NumLit*>(e);
            if (nl->imaginary) return Value::complex(0, nl->v);
            if (nl->isRat) return nl->bigNum.empty()
                ? Value::ratZ(BigInt(nl->ratNum), BigInt(nl->ratDen))                 // 3.14 is a Rat
                : Value::ratZ(BigInt::fromString(nl->bigNum), BigInt::fromString(nl->bigDen));
                // (ratZ: an explicit zero denominator — `<42/0>` — is preserved)
            return Value::number(nl->v); }
        case NK::StrLit: return Value::str(static_cast<StrLit*>(e)->v);
        case NK::RegexLit: {
            auto* rl = static_cast<RegexLit*>(e);
            // rx// is always the Regex object; bare /…/ and m// match against $_
            if (rl->isRx) return Value::regex(rl->pattern);
            Value topic; if (Value* p = tctx_.cur->find("$_")) topic = *p;
            return regexMatch(topic.toStr(), rl->pattern);
        }
        case NK::SubstLit: {
            auto* sl = static_cast<SubstLit*>(e);
            Value topic; if (Value* p = tctx_.cur->find("$_")) topic = *p;
            if (isTrSubst(sl->pattern)) { // tr/// against $_
                long long n; std::string out = translit(topic.toStr(), sl->pattern.substr(1), sl->repl, n);
                if (Value* p = tctx_.cur->find("$_")) *p = Value::str(out);
                return Value::integer(n);
            }
            long nsub = 0; ValueList noArgs; Value mres;
            bool topicUndef = topic.t == VT::Nil || topic.t == VT::Any || topic.t == VT::Type;
            std::string out = substSelect(topicUndef ? std::string() : topic.toStr(), sl->pattern, nullptr, noArgs, nsub, false, &sl->repl, &mres);
            if (sl->nonMut) return Value::str(out);        // S/// : return new string, leave $_ intact
            if (topic.readonly)                             // s/// on a readonly-bound $_ (a plain param) dies
                throw RakuError{Value::typeObj("X::Assignment::RO"), "Cannot modify a readonly value"};
            if (Value* p = tctx_.cur->find("$_")) *p = Value::str(out);
            return mres;                                    // s/// returns the Match / List of matches
        }
        case NK::BoolLit: return Value::boolean(static_cast<BoolLit*>(e)->v);
        case NK::InterpStr: return evalInterp(static_cast<InterpStr*>(e));
        case NK::VarExpr: {
            auto* ve = static_cast<VarExpr*>(e);
            char sigil = ve->name.empty() ? '$' : ve->name[0];
            if (ve->name == "$=finish") return Value::str(finishData_); // =finish data block
            if (ve->name == "$?LINE") return Value::integer(ve->line);
            if (ve->name == "$?FILE") return Value::str(srcFile_);
            // Built-in magic dynamic vars ($*OUT, $*CWD, …). A user binding
            // (`my $*OUT = …`) or a fresh declaration takes precedence, so only
            // fall back to the built-in default when the name is neither being
            // declared nor already bound in this scope / the caller chain.
            bool builtinDefault = !ve->declare;
            if (builtinDefault && ve->name.size() > 1 && ve->name[1] == '*') {
                if (tctx_.cur->find(ve->name)) builtinDefault = false;
                else for (auto it = tctx_.dynStack.rbegin(); it != tctx_.dynStack.rend(); ++it)
                    if ((*it)->find(ve->name)) { builtinDefault = false; break; }
            }
            if (builtinDefault) {
            if (ve->name == "$=pod" || ve->name == "@=pod") { Value a = Value::array(); *a.arr = podDom_; return a; }
            if (ve->name == "$*CWD") { char buf[4096]; Value p = Value::str(getcwd(buf, sizeof buf) ? buf : "."); p.hashKind = "IO"; return p; }
            if (ve->name == "$*RAKU" || ve->name == "$*PERL" || ve->name == "$?RAKU" || ve->name == "$?PERL") {
                Value r = Value::makeHash(); r.hashKind = "Raku"; return r;
            }
            if (ve->name == "$*PROGRAM") { Value p = Value::str(srcFile_); p.hashKind = "IO"; return p; } // running script, as IO::Path
            if (ve->name == "$*PROGRAM-NAME") return Value::str(srcFile_);
            if (ve->name == "$*EXECUTABLE" || ve->name == "$*EXECUTABLE-NAME") { Value p = Value::str(execPath_); p.hashKind = "IO"; return p; }
            if (ve->name == "$*OUT" || ve->name == "$*ERR" || ve->name == "$*IN") {
                Value h = Value::makeHash(); h.hashKind = "FileHandle";
                (*h.hash)["std"] = Value::str(ve->name == "$*ERR" ? "err" : ve->name == "$*IN" ? "in" : "out");
                return h;
            }
            if (ve->name == "$*DISTRO") { Value h = Value::makeHash(); h.hashKind = "Distro"; (*h.hash)["name"] = Value::str("macos"); return h; }
            if (ve->name == "$*KERNEL") { Value h = Value::makeHash(); h.hashKind = "Kernel"; (*h.hash)["name"] = Value::str("darwin"); return h; }
            if (ve->name == "$*VM")     { Value h = Value::makeHash(); h.hashKind = "VM";     (*h.hash)["name"] = Value::str("moar");   return h; }
            if (ve->name == "$*SPEC") return Value::typeObj("IO::Spec::Unix"); // POSIX platform
            if (ve->name == "$*THREAD") { Value h = Value::makeHash(); h.hashKind = "Thread"; (*h.hash)["initial"] = Value::boolean(threadDepth_ == 0); return h; }
            if (ve->name == "$*SCHEDULER") { Value s = Value::makeHash(); s.hashKind = "Scheduler"; (*s.hash)["name"] = Value::str("ThreadPoolScheduler"); return s; }
            if (ve->name == "$*PID") return Value::integer((long long)::getpid());
            if (ve->name == "$*TMPDIR") { const char* t = std::getenv("TMPDIR"); std::string d = (t && *t) ? t : "/tmp"; while (d.size() > 1 && d.back() == '/') d.pop_back(); Value p = Value::str(d); p.hashKind = "IO"; return p; }
            }
            if (ve->declare) {
                if (ve->declScope == "state" && tctx_.curStateEnv) { // persistent across calls
                    if (!tctx_.curStateEnv->vars.count(ve->name)) tctx_.curStateEnv->define(ve->name, typedDefault(ve->declType, sigil));
                    return tctx_.curStateEnv->vars[ve->name];
                }
                // `our &foo;` re-declaration: bind to the existing package (global) code
                // symbol — e.g. an `our sub` defined in a sibling block. Restricted to the
                // `&` sigil so scalar/array/hash `our` vars keep their assignable containers.
                if (ve->declScope == "our" && sigil == '&' && curPkgEnv_ && curPkgEnv_ != tctx_.cur)
                    if (Value* g = curPkgEnv_->find(ve->name)) { tctx_.cur->define(ve->name, *g); return *g; }
                // A bare untyped `my @a` re-evaluated in the SAME scope keeps its
                // container — declarations initialize per scope entry, not per
                // evaluation. So in `until $i.push-exactly(my @a, 3) =:= IterationEnd`,
                // @a accumulates across condition re-checks (a loop BODY re-entry is a
                // fresh scope, so body-level `my` still resets each iteration). A TYPED
                // declaration always redefines: EVALs run in the caller's scope, so
                // `my Ta $c` in one EVAL must not leave its type constraint on a later
                // `my Tc $c`.
                if (!ve->declType.empty() || !tctx_.cur->vars.count(ve->name))
                    tctx_.cur->define(ve->name, typedDefault(ve->declType, sigil));
                return tctx_.cur->vars[ve->name];
            }
            if (ve->name.size() > 2 && (ve->name[1] == '.' || ve->name[1] == '!')) {
                Value* selfp = tctx_.cur->find("self");
                if (selfp && selfp->t == VT::Object && selfp->obj) {
                    std::string an = ve->name.substr(2);
                    auto it = selfp->obj->attrs.find(an);
                    if (it != selfp->obj->attrs.end()) return it->second;
                    // `$.name` is `self.name` — call the method when it's not an attribute
                    if (ve->name[1] == '.') return methodCall(*selfp, an, {});
                }
                return defaultFor(sigil);
            }
            // dynamic variable ($*foo/@*foo/%*foo): search the caller chain, not the lexical closure
            if (ve->name.size() > 1 && ve->name[1] == '*') {
                for (auto it = tctx_.dynStack.rbegin(); it != tctx_.dynStack.rend(); ++it)
                    if (Value* dp = (*it)->find(ve->name)) return *dp;
            }
            Value* p = tctx_.cur->find(ve->name);
            if (p) {
                if (p->t == VT::Hash && p->hashKind == "Proxy" && p->hash) {
                    auto it = p->hash->find("FETCH");
                    if (it != p->hash->end()) return callCallable(it->second, { *p });
                }
                return *p;
            }
            // `&`-references that aren't user subs: built-in operators (&infix:<+>,
            // &prefix:<->) and named builtins (&say) → a Callable that applies them.
            if (ve->name.size() > 1 && ve->name[0] == '&') {
                std::string bare = ve->name.substr(1);
                if (bare.rfind("infix:<", 0) == 0 && bare.back() == '>') {
                    std::string op = bare.substr(7, bare.size() - 8);
                    Value code; code.t = VT::Code; code.code = std::make_shared<Callable>(); code.code->name = bare;
                    code.code->whateverArity = 2; // an infix takes two operands (so sort treats it as a comparator)
                    code.code->builtin = [op](Interpreter& I, ValueList& a) -> Value {
                        if (a.size() >= 2) return I.applyBinOp(op, a[0], a[1]);
                        if (a.size() == 1) { // single arg combines with the op's identity
                            if (op == "+" || op == "-") return I.applyBinOp(op, Value::integer(0), a[0]);
                            if (op == "*" || op == "/") return I.applyBinOp(op, Value::integer(1), a[0]);
                            if (op == "~") return I.applyBinOp(op, Value::str(""), a[0]);
                            return a[0];
                        }
                        return Value::any();
                    };
                    return code;
                }
                if (bare.rfind("prefix:<[", 0) == 0 && bare.size() > 11 &&
                    bare.compare(bare.size() - 2, 2, "]>") == 0) {
                    // &prefix:<[+]> — the reduce metaop as a Callable
                    std::string op = bare.substr(9, bare.size() - 11);
                    Value code; code.t = VT::Code; code.code = std::make_shared<Callable>(); code.code->name = bare;
                    code.code->builtin = [op](Interpreter& I, ValueList& a) -> Value {
                        ValueList items; for (auto& v : a) for (auto& x : v.flatten()) items.push_back(x);
                        return I.applyReduce(op, items);
                    };
                    return code;
                }
                auto bit = builtins_.find(bare);
                if (bit != builtins_.end()) {
                    Value code; code.t = VT::Code; code.code = std::make_shared<Callable>(); code.code->name = bare;
                    code.code->builtin = bit->second;
                    return code;
                }
            }
            // $0, $1, … are aliases for $/[N]; fall back to $/ when not directly bound
            if (ve->name.size() >= 2 && ve->name[0] == '$' && std::isdigit((unsigned char)ve->name[1])) {
                bool alldig = true;
                for (size_t k = 1; k < ve->name.size(); k++) if (!std::isdigit((unsigned char)ve->name[k])) alldig = false;
                if (alldig) if (Value* sl = tctx_.cur->find("$/"))
                    if (sl->arr) { long idx = std::stol(ve->name.substr(1)); if (idx < (long)sl->arr->size()) return (*sl->arr)[idx]; }
            }
            if (!isSpecialVar(ve->name))
                throw RakuError{Value::typeObj("X::Undeclared"),
                                "Variable '" + ve->name + "' is not declared"};
            return defaultFor(sigil);
        }
        case NK::SymbolicRef: {
            auto* sr = static_cast<SymbolicRef*>(e);
            std::string nm;
            if (sr->nameExpr) nm = eval(sr->nameExpr.get()).toStr();
            if (!sr->sigil.empty()) nm = sr->sigil + nm;
            if (nm.empty())
                throw RakuError{Value::typeObj("X::NoSuchSymbol"), "Cannot look up empty name"};
            char c0 = nm[0];
            if (c0 == '$' || c0 == '@' || c0 == '%' || c0 == '&') {
                // resolve exactly as if it were the variable/routine of that name
                VarExpr tmp(nm); tmp.line = e->line;
                try { return eval(&tmp); }
                catch (RakuError&) {
                    Value f = Value::makeHash(); f.hashKind = "Failure";
                    (*f.hash)["exception"] = Value::str("No such symbol '" + nm + "'");
                    return f; // soft failure: falsey / undefined
                }
            }
            // sigilless: constant, then type / builtin resolution (NameTerm rules)
            if (Value* p = tctx_.cur->find(nm)) return *p;
            // an unknown lowercase name is no type — X::NoSuchSymbol (`"::a".EVAL`)
            if (!classes_.count(nm) && !nm.empty() && std::islower((unsigned char)nm[0]))
                throw RakuError{Value::typeObj("X::NoSuchSymbol"), "No such symbol '" + nm + "'"};
            NameTerm tmp(nm); tmp.line = e->line;
            return eval(&tmp);
        }
        case NK::SelfTerm: {
            Value* p = tctx_.cur->find("self");
            return p ? *p : Value::any();
        }
        case NK::NameTerm: {
            auto* nt = static_cast<NameTerm*>(e);
            const std::string& n = nt->name;
            if (!nt->ofType.empty()) { // parameterized type: Array[Int], Hash[Int,Str]
                Value ty = Value::typeObj(n); ty.ofType = nt->ofType; return ty;
            }
            if (n == "next") throw NextEx{};
            if (n == "last") throw LastEx{};
            if (n == "redo") throw RedoEx{};
            if (n == "proceed") throw ProceedEx{};   // leave when, keep matching
            if (n == "succeed") throw BreakGivenEx{}; // exit the enclosing given
            if (n == "Nil") return Value::nil();
            if (n == "True" || n == "Bool::True") return Value::boolean(true);
            if (n == "False" || n == "Bool::False") return Value::boolean(false);
            if (n == "Inf") return Value::number(INFINITY);
            if (n == "NaN") return Value::number(NAN);
            if (n == "Order::Same" || n == "Same") return Value::enumVal("Same", 0);
            if (n == "Order::Less" || n == "Less") return Value::enumVal("Less", -1);
            if (n == "Order::More" || n == "More") return Value::enumVal("More", 1);
            // PromiseStatus <Planned Broken Kept> — real enum values so `$p.status`
            // both compares (=== Kept) and stringifies (~$s eq 'Kept') correctly.
            if (n == "PromiseStatus::Planned" || n == "Planned") return Value::enumVal("Planned", 0);
            if (n == "PromiseStatus::Broken"  || n == "Broken")  return Value::enumVal("Broken", 1);
            if (n == "PromiseStatus::Kept"    || n == "Kept")    return Value::enumVal("Kept", 2);
            if (n == "pi" || n == "π") return Value::number(M_PI);
            if (n == "e") return Value::number(M_E);
            if (n == "i") return Value::complex(0, 1); // imaginary unit
            if (n == "tau" || n == "τ") return Value::number(2 * M_PI);
            if (n == "now") { // Instant: high-resolution seconds since the epoch
                auto d = std::chrono::system_clock::now().time_since_epoch();
                return Value::number(std::chrono::duration<double>(d).count());
            }
            if (n == "time") return Value::integer((long long)::time(nullptr)); // POSIX seconds (Int)
            if (n == "rand") return Value::number(randDouble()); // random Num in [0, 1)
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
            if (Value* p = tctx_.cur->find(n)) return *p;
            if (Value* f = tctx_.cur->find("&" + n)) return callCallable(*f, {});
            auto it = builtins_.find(n);
            if (it != builtins_.end()) { ValueList none; return it->second(*this, none); }
            return Value::typeObj(n);
        }
        case NK::ListExpr: {
            auto* l = static_cast<ListExpr*>(e);
            ValueList items;
            for (auto& it : l->items) {
                // a |slip in a list literal splices its elements: Set, |@more, Bag
                if (it->kind == NK::Unary && static_cast<Unary*>(it.get())->op == "|") {
                    Value v = eval(static_cast<Unary*>(it.get())->operand.get());
                    if (v.t == VT::Array || v.t == VT::Range) { for (auto& x : v.flatten()) items.push_back(x); continue; }
                    items.push_back(v); continue;
                }
                items.push_back(eval(it.get()));
            }
            return listToArray(items);
        }
        case NK::ArrayLit: {
            auto* l = static_cast<ArrayLit*>(e);
            Value a = Value::array();
            a.isList = l->isList; // word-lists are Lists (flatten in list context)
            for (auto& it : l->items) {
                Value v = eval(it.get());
                // a bare @-variable (or a |slip) flattens into the array literal;
                // nested [...] literals stay as single items. A hyper result
                // (`@x».meth`) is itemized — it stays one element (so
                // `[Foo, @x».ast]` is a 2-element [class, list] tuple).
                bool isHyper = it->kind == NK::MethodCall && static_cast<MethodCall*>(it.get())->hyper;
                bool flatten = !isHyper &&
                               ((it->kind == NK::VarExpr && !static_cast<VarExpr*>(it.get())->name.empty() &&
                                 static_cast<VarExpr*>(it.get())->name[0] == '@') ||
                                (v.t == VT::Array && v.isList));
                if (flatten && v.t == VT::Array) { for (auto& x : *v.arr) a.arr->push_back(x); }
                else if (v.t == VT::Range && !v.rExFrom && v.rTo - v.rFrom < 1000000) { // finite Range flattens: [1..10]
                    for (auto& x : v.flatten()) a.arr->push_back(x);
                }
                else {
                    // a hyper result kept as one element is itemized, so it stays nested
                    // through later list contexts (`@(...)`, list-assignment) rather than re-spreading.
                    if (isHyper && v.t == VT::Array) v.isList = false;
                    a.arr->push_back(v);
                }
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
            // `.DEFINITE` as a literal identifier is a metamodel macro: it always
            // reports concreteness and never a user-declared DEFINITE method. The
            // quoted `."DEFINITE"()` form (methodExpr set) still dispatches normally.
            if (mc->method == "DEFINITE" && !mc->methodExpr && !mc->meta)
                return Value::boolean(isDefined(inv));
            if (mc->methodExpr) mc->method = eval(mc->methodExpr.get()).toStr(); // indirect ."$name"()
            ValueList args = evalArgs(mc->args);
            // `$x.&foo(...)` — call the sub `foo` (not a method) with the invocant prepended:
            // foo($x, ...). Used a lot for "method-ish" helpers, e.g. `@a.sort: { .&naturally }`.
            if (mc->method.size() > 1 && mc->method[0] == '&' && !mc->meta) {
                Value* f = tctx_.cur->find(mc->method);
                if (f && f->t == VT::Code) {
                    ValueList ca; ca.reserve(args.size() + 1);
                    ca.push_back(inv); for (auto& a : args) ca.push_back(a);
                    return callCallable(*f, ca);
                }
            }
            // Autovivification: `%h{k}.push(...)` on an undefined element creates an
            // Array in place (Raku semantics), so the mutation persists in the container.
            if ((inv.t == VT::Any || inv.t == VT::Nil || inv.t == VT::Type) &&
                mc->inv->kind == NK::Index &&
                (mc->method == "push" || mc->method == "append" ||
                 mc->method == "unshift" || mc->method == "prepend")) {
                try {
                    if (Value* slot = lvalue(mc->inv.get())) {
                        if (slot->t == VT::Any || slot->t == VT::Nil || slot->t == VT::Type)
                            *slot = Value::array();
                        inv = *slot; // shares the arr shared_ptr; the push writes through
                    }
                } catch (...) {}
            }
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
            // Autothread a junction argument: `$s.contains(all("a","b"))` becomes
            // all($s.contains("a"), $s.contains("b")) — a junction that collapses later.
            if (!mc->meta) for (size_t ai = 0; ai < args.size(); ai++) {
                if (isJunction(args[ai])) {
                    Value jr = Value::array(); jr.enumName = args[ai].enumName; jr.isList = true;
                    for (auto& e : *args[ai].arr) {
                        ValueList a2 = args; a2[ai] = e;
                        jr.arr->push_back(methodCall(inv, mc->method, a2));
                    }
                    return jr;
                }
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
                Value arr = Value::array(); arr.isList = true; // a string range is list-like (flattens)
                std::string cur = from.s, end = to.s;
                for (int g = 0; g < 1000000; g++) {
                    if (cur.length() > end.length()) break;
                    // a descending string range ("e".."a") is empty: succ only climbs, so
                    // once we're past `end` at equal length there is nothing to yield.
                    if (cur.length() == end.length() && cur > end) break;
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
            if (p->keyExpr) {
                Value kv = eval(p->keyExpr.get());
                Value pr = Value::pair(kv.toStr(), evalValueOf(p->value.get()));
                // a non-string key (number, object, match, array, hash, code) is preserved
                // so `.key` and `.raku` reflect its real type (e.g. `1 => 2`, not `"1" => 2`)
                if (kv.t == VT::Int || kv.t == VT::Num || kv.t == VT::Rat || kv.t == VT::Bool ||
                    kv.t == VT::Array || kv.t == VT::Hash || kv.t == VT::Object || kv.t == VT::Pair ||
                    kv.t == VT::Match || kv.t == VT::Code) pr.pairKey = std::make_shared<Value>(kv);
                return pr;
            }
            return Value::pair(p->key, evalValueOf(p->value.get())); // `:err(/pat/)` → Regex value
        }
        case NK::BlockExpr: return makeClosure(static_cast<BlockExpr*>(e));
        case NK::Whatever: { Value w = Value::whatever(); if (static_cast<const WhateverExpr*>(e)->hyper) w.b = true; return w; } // `**` marked via .b
        default:
            return Value::any();
    }
}

} // namespace rakupp
