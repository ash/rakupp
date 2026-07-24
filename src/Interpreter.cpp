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
#include <climits>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <thread>
#include <ctime>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#if !defined(_WIN32)
#include <dirent.h>   // Windows gets the FindFirstFile-based shim from Platform.h
#endif
#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#include <signal.h>      // stack_t (used by pthread_stackseg_np's out-param)
#include <pthread_np.h>  // pthread_stackseg_np (OpenBSD) / pthread_attr_get_np (Free/Net/DragonFly)
#endif

namespace rakupp {

// Thread-local RNG state: drand48's process-global state is not thread-safe, so
// under parallel execution each thread keeps its own erand48 seed.
// Does a CATCH block contain `when`/`default` clauses? If so, an exception that
// matches none of them is NOT handled and must rethrow; a CATCH with only plain
// statements is an unconditional handler.
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
        // 64-bit on purpose: unsigned long is 32 bits on LLP64 (Windows) and
        // wasm32, where a `>> 32` would be undefined.
        unsigned long long s = (unsigned long long)::time(nullptr) ^ ((unsigned long long)::getpid() << 16)
                             ^ (unsigned long long)std::hash<std::thread::id>{}(std::this_thread::get_id());
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
    if (sig == '&') return n[1] != '?';     // &foo code refs (builtins not in env);
                                             // &?ROUTINE/&?BLOCK exist only inside callables
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
    // eqv is type-aware: 42 eqv 42.0 is False (Int vs Num/Rat), unlike ==
    if (a.t != b.t) return false;
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
        case VT::Rat: // structural nude compare — .Str on a 0-denominator Rat throws
            return a.fatRat == b.fatRat &&
                   a.ratN && b.ratN && a.ratD && b.ratD &&
                   BigInt::cmp(*a.ratN, *b.ratN) == 0 && BigInt::cmp(*a.ratD, *b.ratD) == 0;
        default: return a.toStr() == b.toStr();
    }
}

// Numify a string with Raku-correct result type (Int vs Num); undefined if non-numeric.
// External linkage (declared in Interpreter.h) so Builtins.cpp's .Int/.Numeric can
// reuse the BigInt-aware parse instead of the lossy long-long toInt().
Value numifyStr(const std::string& in) {
    size_t a = in.find_first_not_of(" \t\n\r\f\v");
    if (a == std::string::npos) return Value::integer(0); // empty/whitespace -> 0
    size_t b = in.find_last_not_of(" \t\n\r\f\v");
    std::string s = in.substr(a, b - a + 1);
    // Unicode MINUS SIGN (U+2212) is accepted as an ASCII '-' in numeric strings
    for (size_t k = 0; (k = s.find("\xE2\x88\x92", k)) != std::string::npos; )
        s.replace(k, 3, "-");
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
    static const std::regex reRadix(R"(^[+-]?0[xobd][0-9a-fA-F_]+$)");
    static const std::regex reFloat(R"(^[+-]?(\d+(\.\d+)?|\.\d+)([eE][+-]?\d+)?$)");
    static const std::regex reRat(R"(^[+-]?\d+/\d+$)");
    // convert a single radix digit (0-9, a-z / A-Z) → value, or -1 if not a digit
    auto digitVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'z') return c - 'a' + 10;
        if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
        return -1;
    };
    // parse a radix number "d.d" (underscores already allowed) in the given base,
    // validating each digit < base. Returns a numeric Value or Value::any() on failure.
    auto parseRadix = [&](const std::string& raw, int base, bool neg) -> Value {
        // underscores are separators only BETWEEN two digits — reject edges/doubles
        for (size_t k = 0; k < raw.size(); k++)
            if (raw[k] == '_' && (k == 0 || k + 1 == raw.size() ||
                digitVal(raw[k-1]) < 0 || digitVal(raw[k+1]) < 0)) return Value::any();
        std::string digs; for (char c : raw) if (c != '_') digs += c;
        size_t dot = digs.find('.');
        std::string ip = dot == std::string::npos ? digs : digs.substr(0, dot);
        std::string fp = dot == std::string::npos ? "" : digs.substr(dot + 1);
        if (ip.empty() && fp.empty()) return Value::any();
        BigInt iv(0), b(base);
        for (char c : ip) { int d = digitVal(c); if (d < 0 || d >= base) return Value::any(); iv = iv * b + BigInt(d); }
        if (fp.empty()) { if (neg) iv = BigInt(0) - iv; return Value::bigint(iv); }
        BigInt num = iv, den(1);
        for (char c : fp) { int d = digitVal(c); if (d < 0 || d >= base) return Value::any(); num = num * b + BigInt(d); den = den * b; }
        if (neg) num = BigInt(0) - num;
        return Value::rat(num, den);
    };
    try {
        if (std::regex_match(s, reInt)) {
            try { return Value::integer(std::stoll(s)); }
            catch (...) { return Value::bigint(BigInt::fromString(s)); }
        }
        // :N<digits> radix notation, e.g. :10<42>, :2<11>, :36<aZ>, :16<c.8>
        {
            size_t off = (s[0] == '+' || s[0] == '-') ? 1 : 0;
            bool neg = s[0] == '-';
            if (off < s.size() && s[off] == ':') {
                size_t lt = s.find('<', off);
                if (lt != std::string::npos && s.back() == '>') {
                    std::string baseStr = s.substr(off + 1, lt - off - 1);
                    static const std::regex reDec(R"(^\d+$)");
                    if (std::regex_match(baseStr, reDec)) {
                        int base = std::atoi(baseStr.c_str());
                        if (base >= 2 && base <= 36)
                            return parseRadix(s.substr(lt + 1, s.size() - lt - 2), base, neg);
                    }
                }
                return Value::any();
            }
        }
        if (std::regex_match(s, reRadix)) {
            bool neg = s[0] == '-'; size_t off = (s[0] == '+' || s[0] == '-') ? 1 : 0;
            char pc = s[off + 1];
            int base = pc == 'x' ? 16 : pc == 'o' ? 8 : pc == 'd' ? 10 : 2;
            return parseRadix(s.substr(off + 2), base, neg);
        }
        if (std::regex_match(s, reRat)) {
            size_t sl = s.find('/');
            return Value::rat(BigInt::fromString(s.substr(0, sl)), BigInt::fromString(s.substr(sl + 1)));
        }
        if (std::regex_match(s, reFloat)) {
            // a plain decimal ("3.14") numifies to a Rat, like the literal would;
            // an exponent ("1.23E4") makes it a Num
            if (s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
                size_t dot = s.find('.');
                std::string frac = dot == std::string::npos ? "" : s.substr(dot + 1);
                if (!frac.empty()) {
                    std::string digits = (dot == std::string::npos ? s : s.substr(0, dot) + frac);
                    if (!digits.empty() && digits[0] == '+') digits.erase(0, 1);
                    BigInt den(1);
                    for (size_t k = 0; k < frac.size(); k++) den = den * BigInt(10);
                    return Value::rat(BigInt::fromString(digits), std::move(den));
                }
            }
            return Value::number(std::stod(s));
        }
        // Complex: "a+bi" / "a-bi" / "bi" (i may be written "\i"). Split at the
        // sign that separates real and imaginary parts (not a leading sign or an
        // exponent sign), numify each half recursively.
        {
            std::string t = s;
            size_t ip = t.rfind("\\i");
            if (ip == t.size() - 2 && ip != std::string::npos) t.erase(ip, 1); // "\i" -> "i"
            if (!t.empty() && (t.back() == 'i')) {
                std::string body = t.substr(0, t.size() - 1); // drop trailing i
                // find split sign: scan from the right for +/- not preceded by e/E
                size_t split = std::string::npos;
                for (size_t k = body.size(); k-- > 1; ) {
                    if ((body[k] == '+' || body[k] == '-') &&
                        body[k-1] != 'e' && body[k-1] != 'E') { split = k; break; }
                }
                std::string reStr, imStr;
                if (split == std::string::npos) { reStr = "0"; imStr = body.empty() ? "1" : body; }
                else { reStr = body.substr(0, split); imStr = body.substr(split);
                       if (imStr == "+" || imStr == "-") imStr += "1"; }
                Value rv = numifyStr(reStr), iv = numifyStr(imStr);
                if (rv.isNumeric() && iv.isNumeric())
                    return Value::complex(rv.toNum(), iv.toNum());
            }
        }
    } catch (...) {}
    return Value::any(); // not numeric -> undefined (so .defined is False)
}

static Value defaultFor(char sigil) {
    if (sigil == '@') return Value::array();
    if (sigil == '%') return Value::makeHash();
    return Value::any();
}
// Build a shaped array `my @a[2;3]`: a fixed-size row-major structure pre-filled
// with the element-type default, tagged with its dimensions for `.shape`.
Value makeShapedContainer(const std::vector<long long>& dims, const std::string& declType,
                          const ValueList* fill) {
    // native (lowercase) element types default to a concrete zero/empty; named
    // types default to the type object; untyped to Any.
    Value elemDef;
    if (declType.empty()) elemDef = Value::any();
    else if (declType == "str") elemDef = Value::str("");
    else if (declType.rfind("num", 0) == 0) elemDef = Value::number(0);
    else if (declType.rfind("int", 0) == 0 || declType.rfind("uint", 0) == 0 || declType == "byte")
        elemDef = Value::integer(0);
    else elemDef = Value::typeObj(declType);
    size_t idx = 0;
    std::function<Value(size_t)> mk = [&](size_t lvl) -> Value {
        Value a = Value::array(); a.ofType = declType;
        long long n = lvl < dims.size() ? dims[lvl] : 0;
        for (long long i = 0; i < n; i++) {
            if (lvl + 1 < dims.size()) a.arr->push_back(mk(lvl + 1));
            else a.arr->push_back(fill && idx < fill->size() ? (*fill)[idx++] : elemDef);
        }
        return a;
    };
    Value v = mk(0);
    v.shape = std::make_shared<std::vector<long long>>(dims);
    return v;
}
// Evaluate the dimension list of a shaped-array declaration (`my @a[2;3]` → {2,3}).
std::vector<long long> Interpreter::evalShapeDims(Expr* shape) {
    std::vector<long long> dims;
    if (shape && shape->kind == NK::ListExpr)
        for (auto& d : static_cast<ListExpr*>(shape)->items) dims.push_back(eval(d.get()).toInt());
    else if (shape) dims.push_back(eval(shape).toInt());
    return dims;
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
        // num32 rounds to float32 precision on assignment (num/num64 are already
        // double, so they need no truncation marker).
        if (type == "num32") { Value v = Value::number(0); v.natBits = 32; v.natFloat = true; return v; }
        if (type == "num" || type.rfind("num", 0) == 0) return Value::number(0);
        if (type == "int" || type.rfind("int", 0) == 0 || type.rfind("uint", 0) == 0) return Value::integer(0);
        if (type == "str") return Value::str("");
        if (std::isupper((unsigned char)type[0])) return Value::typeObj(type); // my Int $x -> (Int)
    }
    // typed containers: `my Int @a` -> Array[Int], `my int @a` (native) too
    if ((sigil == '@' || sigil == '%') && !type.empty() &&
        (std::isupper((unsigned char)type[0]) ||
         type.rfind("int", 0) == 0 || type.rfind("uint", 0) == 0 ||
         type.rfind("num", 0) == 0 || type == "str" || type == "byte")) {
        Value v = defaultFor(sigil);
        v.ofType = type;
        return v;
    }
    return defaultFor(sigil);
}
// Truncate an integer value to a native type's bit width (wraparound), keeping the tag.
static void wrapNative(Value& v, int bits, bool sign, bool isFloat = false) {
    if (bits <= 0) return;
    if (isFloat) { // native float container: num32 rounds to float32 precision
        if (bits == 32) { float f = (float)v.toNum(); v = Value::number((double)f); }
        else            v = Value::number(v.toNum());
        v.natBits = bits; v.natFloat = true;
        return;
    }
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
void collectPHExprPublic(const Expr* e, std::set<std::string>& out) { collectPHExpr(e, out); }

static void addIfPlaceholder(const std::string& name, std::set<std::string>& out) {
    if (name.size() > 2 && (name[1] == '^' || name[1] == ':')) out.insert(name); // $^a positional, $:n named
    else if (name == "@_") out.insert(name); // implicit slurpy — consumers filter
    else if (name.size() > 2 && name[1] == '!' &&
             (std::isalpha((unsigned char)name[2]) || name[2] == '_'))
        out.insert(name); // $!attr — attribute references; placeholder consumers filter these
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

std::vector<std::string> computePlaceholders(const std::vector<StmtPtr>& body) {
    std::set<std::string> ph;
    for (auto& s : body) collectPHStmt(s.get(), ph);
    std::vector<std::string> out;
    for (auto& n : ph) if (n[1] != '!' && n != "@_") out.push_back(n); // drop $!attr and @_ refs
    return out; // std::set is sorted
}

// the first placeholder-ish name (incl. @_) in a body that TAKES NO signature —
// for the X::Placeholder::Block diagnostic
std::string firstBlockPlaceholder(const std::vector<StmtPtr>& body) {
    std::set<std::string> ph;
    for (auto& s : body) collectPHStmt(s.get(), ph);
    for (auto& n : ph) if (n[1] == '^' || n[1] == ':') return n;
    if (ph.count("@_")) return "@_";
    return "";
}

// every `$!x`/`@!x`/`%!x` attribute reference in a body (for undeclared-attribute checks)
std::vector<std::string> collectAttrRefs(const std::vector<StmtPtr>& body) {
    std::set<std::string> ph;
    for (auto& s : body) collectPHStmt(s.get(), ph);
    std::vector<std::string> out;
    for (auto& n : ph) if (n[1] == '!') out.push_back(n);
    return out;
}

Value listToArray(const ValueList& items) {
    // Post-GLR: a comma list keeps every member as-is — nested Lists, Ranges,
    // and word lists stay single elements. Only Slips splice (`|x`, slip(),
    // .Slip — all tagged s=="Slip" by their producers).
    Value v = Value::array();
    for (auto& it : items) {
        if (it.t == VT::Array && it.arr && it.s == "Slip")
            v.arr->insert(v.arr->end(), it.arr->begin(), it.arr->end());
        else v.arr->push_back(it);
    }
    return v;
}

// Turn a raw argv into MAIN's argument list: --opt / --opt=val / --/opt become
// named args, everything else stays a positional string. Shared by the
// interpreter's auto-invoke and the native-codegen main().
// Rakudo-format usage text generated from &MAIN's signature(s): the `Usage:`
// header, one line per candidate, then an option list built from `#=` trailing
// declarator pod, aligned, with [default: X] for defaulted params.
std::string Interpreter::mainUsage() {
    Value* mainSub = tctx_.cur ? tctx_.cur->find("&MAIN") : nullptr;
    if (!mainSub && global_) mainSub = global_->find("&MAIN");
    if (!mainSub || mainSub->t != VT::Code || !mainSub->code) return "";
    ValueList cands;
    if (mainSub->code->isMultiDispatcher)
        for (auto& c : mainSub->code->candidates) cands.push_back(c);
    else cands.push_back(*mainSub);
    std::string out = "Usage:\n";
    struct Opt { std::string label, desc, def; };
    std::vector<Opt> opts;
    auto bare = [](const Param& p) {
        const std::string& n = p.namedKey.empty() ? p.name : p.namedKey;
        return (!n.empty() && (n[0] == '$' || n[0] == '@' || n[0] == '%' || n[0] == '&'))
             ? n.substr(1) : n;
    };
    for (auto& cand : cands) {
        if (!cand.code || !cand.code->params) continue;
        std::string line = "  " + (srcFile_.empty() ? std::string("<program>") : srcFile_);
        for (auto& p : *cand.code->params) {
            std::string label;
            if (p.litVal) { line += " " + eval(p.litVal.get()).toStr(); continue; }
            if (p.named) {
                label = "--" + bare(p);
                if (!(p.type.empty() || p.type == "Bool")) label += "=<" + p.type + ">";
                line += " [" + label + "]";
            }
            else if (p.slurpy) { label = "[<" + bare(p) + "> ...]"; line += " " + label; }
            else if (p.optional || p.defaultVal) { label = "[<" + bare(p) + ">]"; line += " " + label; }
            else { label = "<" + bare(p) + ">"; line += " " + label; }
            if (!p.pod.empty()) {
                std::string def;
                if (p.defaultVal) { try { def = eval(p.defaultVal.get()).gist(); } catch (...) {} }
                opts.push_back({label, p.pod, def});
            }
        }
        out += line + "\n";
    }
    if (!opts.empty()) {
        out += "  \n";
        size_t w = 0;
        for (auto& o : opts) w = std::max(w, o.label.size());
        for (auto& o : opts) {
            out += "    " + o.label + std::string(w - o.label.size() + 4, ' ') + o.desc;
            if (!o.def.empty()) out += " [default: " + o.def + "]";
            out += "\n";
        }
    }
    return out;
}

ValueList rtMainArgs(const std::vector<std::string>& argv) {
    ValueList margs;
    auto named = [](std::string key, Value v) { // --opt args bind to :$named params
        Value p = Value::pair(std::move(key), std::move(v));
        p.namedArg = true;
        return p;
    };
    auto allomorph = [](const std::string& str) { // numeric-looking argv binds Int/Num params too
        Value v = Value::str(str);
        if (str.empty()) return v;
        size_t k = (str[0] == '-' || str[0] == '+') ? 1 : 0;
        if (k >= str.size()) return v;
        bool digits = true, dot = false;
        for (size_t j = k; j < str.size(); j++) {
            if (str[j] == '.' && !dot) { dot = true; continue; }
            if (!std::isdigit((unsigned char)str[j])) { digits = false; break; }
        }
        if (digits) v.hashKind = "Allomorph";
        return v;
    };
    for (auto& a : argv) {
        if (a.rfind("--", 0) == 0 && a.size() > 2) {
            std::string rest = a.substr(2);
            if (rest.rfind("/", 0) == 0) margs.push_back(named(rest.substr(1), Value::boolean(false)));
            else {
                auto eq = rest.find('=');
                if (eq != std::string::npos) margs.push_back(named(rest.substr(0, eq), allomorph(rest.substr(eq + 1))));
                else margs.push_back(named(rest, Value::boolean(true)));
            }
        } else {
            margs.push_back(allomorph(a));
        }
    }
    return margs;
}

// gather { … } for native codegen: same probe-and-double laziness as the
// interpreter's gather — run the block collecting takes up to a cap; if the cap
// is hit the result is lazy and extends by re-running with a doubled cap.
Value Interpreter::rtGather(Value blockClosure) {
    auto runGather = [this, blockClosure](size_t limit, ValueList& out) -> bool {
        auto collector = std::make_shared<ValueList>();
        tctx_.gatherStack.push_back(collector);
        tctx_.gatherLimits.push_back(limit);
        bool hit = false;
        try { ValueList noargs; callCallable(blockClosure, noargs); }
        catch (StopGatherEx&) { hit = true; }
        catch (...) { tctx_.gatherStack.pop_back(); tctx_.gatherLimits.pop_back(); throw; }
        tctx_.gatherStack.pop_back(); tctx_.gatherLimits.pop_back();
        out = std::move(*collector);
        return hit;
    };
    const size_t INITIAL = 64;
    ValueList prefix;
    if (!runGather(INITIAL, prefix)) { // finite: eager, but still a Seq
        Value a = Value::array(std::move(prefix)); a.isList = true; a.s = "Seq"; return a;
    }
    Value arr = Value::array(prefix); arr.isList = true; arr.s = "Seq";
    auto st = std::make_shared<LazySeqState>();
    st->appendNext = [this, runGather](ValueList& out) -> bool {
        ValueList grown;
        bool more = runGather(out.size() + std::max<size_t>(64, out.size()), grown);
        for (size_t i = out.size(); i < grown.size(); i++) out.push_back(grown[i]);
        return more;
    };
    arr.ext = st;
    return arr;
}

Value Interpreter::seqOp(Value l, Value r, bool exclusive) {
    // The sequence operator, callable from both evalBinary and native codegen:
    // seed list [, generator closure] ... endpoint|*|Code.
    {
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
        Value out = Value::array(); out.isList = true; out.s = "Seq"; // (1...5).WHAT is (Seq)
        for (auto& s : seed) out.arr->push_back(s);
        if (out.arr->empty() && !hasGen) return out; // `{ } ... *` seeds from the closure itself
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
        // exact geometric ratio: Rat/Int seeds continue in Rat space (Rakudo keeps
        // 1, 1/2, 1/4 ... 0 as Rats; doubles would leak an e-suffix via .raku)
        Value ratioV; bool exactRatio = false;
        if (geometric) {
            bool seedsExact = true;
            for (auto& sv : seed) if (sv.t != VT::Int && sv.t != VT::Bool && sv.t != VT::Rat) seedsExact = false;
            if (seedsExact && seed.size() >= 2) {
                ratioV = applyArith("/", seed[1], seed[0]);
                exactRatio = (ratioV.t == VT::Rat || ratioV.t == VT::Int);
            }
        }
        bool ascending = hasGen ? true : (geometric ? ratio >= 1 : step >= 0);
        // How many trailing elements the generator consumes: a `* + *` WhateverCode
        // by its whateverArity, `{ $^a + $^b }` by its placeholder count (the
        // canonical `1, 1, { $^a + $^b } … *` fibonacci), `-> $a, $b {…}` by its
        // signature — and a topic block `{ $_ … }` or a bare block by 1 (an EMPTY
        // params vector must not read as arity 0, which fed the block nothing).
        long long arity = 1;
        if (hasGen && gen.code) {
            if (gen.code->whateverArity > 0)            arity = gen.code->whateverArity;
            else if (!gen.code->placeholders.empty())   arity = (long long)gen.code->placeholders.size();
            else if (gen.code->params && !gen.code->params->empty()) arity = (long long)gen.code->params->size();
        }
        // An infinite sequence (`… … *`) is LAZY — and so is a GENERATOR
        // sequence with a literal endpoint (`1, {-$_} ... 3`): it stops only on
        // an EXACT endpoint match, which may never come (Rakudo semantics),
        // so it must not materialise eagerly.
        if (infinite || (hasGen && !endCode)) {
            // seed already at the endpoint: the sequence is just the seed
            if (!infinite && !seed.empty() && seed.back().isNumeric() &&
                seed.back().toNum() == endVal) {
                if (exclusive) out.arr->pop_back();
                return out;
            }
            auto st = std::make_shared<LazySeqState>();
            Interpreter* self = this;
            bool boundedGen = !infinite;
            st->infinite = !boundedGen; // a literal-endpoint gen seq CAN drain (stops on match)
            st->appendNext = [self, gen, hasGen, geometric, ratio, step, allInt, arity,
                              succSeed, succDesc, ratioV, exactRatio,
                              boundedGen, endVal, exclusive](ValueList& cache) -> bool {
                if (cache.empty() && !hasGen) return false;
                if (boundedGen && !cache.empty() && cache.back().isNumeric() &&
                    cache.back().toNum() == endVal) return false; // endpoint reached
                if (boundedGen && cache.size() >= 1000000) return false; // runaway cap (endpoint never matched)
                double lastV = cache.empty() ? 0 : cache.back().toNum();
                Value next;
                if (hasGen) {
                    ValueList args; size_t n = cache.size();
                    for (long long k = arity; k >= 1; k--) { long long idx = (long long)n - k; args.push_back(idx >= 0 ? cache[idx] : Value::integer(0)); }
                    // `last` inside the generator terminates the sequence
                    try { next = self->callCallable(gen, args); }
                    catch (const LastEx&) { return false; }
                    if (boundedGen && exclusive && next.isNumeric() && next.toNum() == endVal)
                        return false; // ...^ drops the endpoint element
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
                    const Value& lastE = cache.back();
                    if (allInt && ratio == std::floor(ratio)) next = Value::integer((long long)std::llround(lastV * ratio));
                    else if (exactRatio && (lastE.t == VT::Int || lastE.t == VT::Rat || lastE.t == VT::Bool))
                        next = applyArith("*", lastE, ratioV); // stays Rat
                    else next = Value::number(lastV * ratio);
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
        bool dirKnown = !hasGen; // for a closure gen the travel direction is learned
        while (out.arr->size() < CAP) {
            double lastV = out.arr->empty() ? 0 : out.arr->back().toNum(); // empty seed: gen makes elem 1
            // Non-gen numeric sequences can pre-check the endpoint; a closure gen
            // must generate first (its direction — and thus overshoot test — is
            // only known once we see the next value).
            if (!hasGen && !infinite && !endCode && (ascending ? lastV >= endVal : lastV <= endVal)) break; // reached endpoint
            Value next;
            if (hasGen) {
                ValueList args; size_t n = out.arr->size();
                for (long long k = arity; k >= 1; k--) { long long idx = (long long)n - k; args.push_back(idx >= 0 ? (*out.arr)[idx] : Value::integer(0)); }
                // `last` inside the generator terminates the sequence
                try { next = callCallable(gen, args); }
                catch (const LastEx&) { break; }
                if (!dirKnown && !infinite && !endCode && next.isNumeric()) {
                    ascending = next.toNum() >= lastV; dirKnown = true;
                }
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
                const Value& lastE = out.arr->back();
                if (allInt && ratio == std::floor(ratio)) next = Value::integer((long long)std::llround(lastV * ratio));
                else if (exactRatio && (lastE.t == VT::Int || lastE.t == VT::Rat || lastE.t == VT::Bool))
                    next = applyArith("*", lastE, ratioV); // stays Rat
                else next = Value::number(lastV * ratio);
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
    return Value::array();
}

// `from .. to` for native codegen: a string range materialises via succ (like the
// interpreter's NK::Range eval); anything else is a numeric Range value.
Value rtRangeVal(const Value& from, const Value& to, bool exFrom, bool exTo) {
    if (from.t == VT::Str && to.t == VT::Str) {
        Value arr = Value::array(); arr.isList = true; // a string range is list-like (flattens)
        std::string cur = from.s, end = to.s;
        for (int g = 0; g < 1000000; g++) {
            if (cur.length() > end.length()) break;
            if (cur.length() == end.length() && cur > end) break;
            if (cur == end) { if (!exTo) arr.arr->push_back(Value::str(cur)); break; }
            arr.arr->push_back(Value::str(cur));
            cur = strSucc(cur);
        }
        return arr;
    }
    {
        Value r = Value::range(from.toInt(), to.toInt(), exFrom, exTo);
        if (to.t == VT::Int && to.big) r.big = to.big; // keep the big bound (pick/roll sample it)
        return r;
    }
}

// `@a[$i .. *]` for native codegen: the tail slice from index `from` to the end.
Value rtSliceFrom(const Value& base, long long from, bool exFrom) {
    Value out = Value::array(); out.isList = true;
    if (from < 0) return out;
    if (base.t == VT::Array && base.arr) {
        long long n = (long long)base.arr->size();
        for (long long i = from + (exFrom ? 1 : 0); i < n; i++) out.arr->push_back((*base.arr)[i]);
    }
    else if (base.t == VT::Str) { // "abc"[1..*] is rare but harmless to support: chars
        std::string ss = base.s;
        if (from + (exFrom ? 1 : 0) < (long long)ss.size()) return Value::str(ss.substr(from + (exFrom ? 1 : 0)));
    }
    return out;
}

// >>.method for native codegen: apply to each top-level element (structure-
// preserving, no deep flatten) — mirrors the interpreter's hyper method call.
Value rtHyperMethod(Interpreter& I, const Value& inv, const std::string& m, ValueList args) {
    Value out = Value::array();
    if (inv.t == VT::Array && inv.arr)
        for (auto& el : *inv.arr) out.arr->push_back(I.methodCall(el, m, args));
    else
        for (auto& el : inv.flatten()) out.arr->push_back(I.methodCall(el, m, args));
    out.isList = true;
    return out;
}

// |x for native codegen. In argument position (argPos): arrays/ranges spread
// positionally and a hash spreads as named args — mirroring evalArgs. In a list
// literal a hash stays one item — mirroring the ListExpr eval.
void rtSpreadArg(ValueList& as, const Value& v, bool argPos) {
    if (v.t == VT::Array || v.t == VT::Range) { for (auto& x : v.flatten()) as.push_back(x); return; }
    if (argPos && v.t == VT::Hash && v.hash) {
        for (auto& kv : *v.hash) { Value p = Value::pair(kv.first, kv.second); p.namedArg = true; as.push_back(std::move(p)); }
        return;
    }
    as.push_back(v);
}
// |x as a list-literal element: a pre-spread List that listToArray will splice.
Value rtSlipVal(const Value& v) {
    Value out = Value::array(); out.isList = true; out.s = "Slip"; // splices via listToArray
    rtSpreadArg(*out.arr, v, false);
    return out;
}

// A bareword term for native codegen — the interpreter's NameTerm tail: an
// env-bound value, a zero-arg &routine call, a zero-arg builtin, else a type object.
Value Interpreter::rtNameTerm(const std::string& n) {
    if (tctx_.cur) {
        if (Value* p = tctx_.cur->find(n)) return *p;
        if (Value* f = tctx_.cur->find("&" + n)) return callCallable(*f, {});
    }
    auto it = builtins_.find(n);
    if (it != builtins_.end()) { ValueList none; return it->second(*this, none); }
    // builtin enum members (mirror the NameTerm eval): without the numeric
    // payload a native `sort { $a < $b ?? Less !! More }` compares 0 vs 0
    if (n == "Order::Same" || n == "Same") return Value::enumVal("Same", 0);
    if (n == "Order::Less" || n == "Less") return Value::enumVal("Less", -1);
    if (n == "Order::More" || n == "More") return Value::enumVal("More", 1);
    if (n == "PromiseStatus::Planned" || n == "Planned") return Value::enumVal("Planned", 0);
    if (n == "PromiseStatus::Broken"  || n == "Broken")  return Value::enumVal("Broken", 1);
    if (n == "PromiseStatus::Kept"    || n == "Kept")    return Value::enumVal("Kept", 2);
    // Signal enum members (SIGINT, SIGTERM, …) — value is the OS signal number
    if (n.rfind("SIG", 0) == 0 || n.rfind("Signal::SIG", 0) == 0) {
        std::string bare = n.rfind("Signal::", 0) == 0 ? n.substr(8) : n;
        int num = signalNumberOfName(bare);
        if (num > 0) { Value v = Value::enumVal(bare, num); v.enumType = "Signal"; return v; }
    }
    return Value::typeObj(n);
}

// k => v at a call site (native codegen): a syntactic identifier-keyed pair is
// a NAMED argument — mirrors evalArgs.
Value rtNamedPair(const std::string& k, Value v) {
    Value p = Value::pair(k, std::move(v));
    p.namedArg = true;
    return p;
}
// The count of POSITIONAL args (named pairs excluded) — for multi-dispatch arity.
size_t rtPosCount(const ValueList& a, size_t from) {
    size_t n = 0;
    for (size_t i = from; i < a.size(); i++) if (!(a[i].t == VT::Pair && a[i].namedArg)) n++;
    return n;
}

// `use MODULE` for native codegen: mirror exec(UseStmt) — Test flag, language
// pragma, lib paths, real module loading into the runtime env.
void Interpreter::rtUse(const std::string& module, const std::string& arg) {
    if (module == "Test") { usedTest_ = true; return; }
    if (module.size() >= 2 && module[0] == 'v' && std::isdigit((unsigned char)module[1])) {
        if (module.find("6.c") != std::string::npos) langRev_ = 0;
        else if (module.find("6.d") != std::string::npos) langRev_ = 1;
        else langRev_ = 2;
        return;
    }
    if (module == "lib") {
        if (!arg.empty()) libPaths_.insert(libPaths_.begin(), arg);
        return;
    }
    if (!module.empty()) loadModule(module);
}

// |@a in VALUE position (a return value, an assignment RHS) for native codegen:
// a one-level splice marker — the consumer (map, list assignment) splices the
// top-level elements; itemized inner arrays stay whole. Mirrors the interp,
// where the value-position slip returns the array and the consumer flattens.
Value rtSlipShallow(const Value& v) {
    if (v.t == VT::Array && v.arr) { Value r = v; r.isList = true; r.s = "Slip"; return r; }
    if (v.t == VT::Range) { Value r = Value::array(v.flatten()); r.isList = true; r.s = "Slip"; return r; }
    Value out = Value::array(); out.isList = true; out.s = "Slip";
    if (v.t != VT::Nil) out.arr->push_back(v);
    return out;
}
// [ … ] composer items for native codegen — mirror the interpreter's ArrayLit
// splice rules (listToArray then splices anything tagged Slip one level):
// a List-valued member of a non-comma [..] splices; a comma-list member stays.
Value rtSpliceIfList(const Value& v) {
    if (v.t == VT::Array && v.arr && v.isList) { Value r = v; r.s = "Slip"; return r; }
    return v;
}
// the one-arg rule: `[<a b>».Str]` — a SINGLE list-valued, non-itemized item spreads
Value rtOneArgItem(const Value& v) {
    if (v.t == VT::Array && v.arr && v.isList && !v.itemized) { Value r = v; r.s = "Slip"; return r; }
    return v;
}
// a hyper result kept as one element is itemized — clear isList so later list
// contexts don't re-spread it (matches the interpreter's ArrayLit else-branch)
Value rtHyperItem(const Value& v) {
    if (v.t == VT::Array) { Value r = v; r.isList = false; return r; }
    return v;
}

// next/last/redo in EXPRESSION position for native codegen (`$x > 3 && last`):
// throw the interpreter's control-flow signals; native loop bodies catch them.
Value rtThrowNext(const std::string& label) { throw NextEx{label}; }
Value rtThrowLast(const std::string& label) { throw LastEx{label}; }
Value rtThrowRedo(const std::string& label) { throw RedoEx{label}; }

// { k => v, … } for native codegen — mirrors the interpreter's HashLit eval
// (arrays splice one level, then hash coercion).
Value rtHashLit(const ValueList& items) {
    Value arr = Value::array();
    for (auto& v : items) {
        if (v.t == VT::Array && v.arr) { for (auto& x : *v.arr) arr.arr->push_back(x); }
        else arr.arr->push_back(v);
    }
    return rtCoerceHash(arr);
}

// `@a = expr` for native codegen: list-assignment semantics. A List splices its
// elements (one level, via listToArray); an Array (itemized rows included) keeps
// its elements as they are; a Range expands; a lone scalar becomes a 1-elem array.
// A Hash (or quanthash) in list context spreads into its Pairs: a plain Hash
// gives `key => value`, a Set gives `elem => True`, a Bag/Mix `elem => weight`.
// `my @a = %h` / `my @s = $set` all go through this. (Iteration order follows
// the container; callers that need a stable order sort.)
static Value hashToPairs(const Value& v) {
    Value out = Value::array(); out.isList = true;
    if (!v.hash) return out;
    bool setty = v.hashKind == "Set" || v.hashKind == "SetHash";
    for (auto& kv : *v.hash) {
        Value p = Value::pair(kv.first, setty ? Value::boolean(true) : kv.second);
        p.pairKey = kv.second.pairKey; // Set/Bag/Mix: recover the element's original type
        out.arr->push_back(std::move(p));
    }
    return out;
}

Value rtArrayVal(const Value& v) {
    if (v.t == VT::Hash && v.hash) return hashToPairs(v);
    if (v.t == VT::Array && v.arr) {
        if (v.ext) return v; // a lazy seq stays lazy; indexing/consumers materialise on demand
        if (v.isList) { Value r = listToArray(*v.arr); r.isList = false; return r; }
        Value r = Value::array(*v.arr); // fresh buffer: `@a = @b` must not alias @b
        return r;
    }
    if (v.t == VT::Range) return Value::array(v.flatten());
    Value a = Value::array();
    if (v.t != VT::Nil) a.arr->push_back(v);
    return a;
}

static Value coerceArray(const Value& v) {
    if (v.t == VT::Hash && v.hash) return hashToPairs(v);
    // a Blob/Buf assigns to an @-array as its elements (`my uint32 @W = $M`
    // in Digest::SHA1 — 32-bit words for blob32)
    if (v.t == VT::Str && !v.itemized && (v.hashKind == "Blob" || v.hashKind == "Buf")) {
        Value r = Value::array(v.blobList()); r.isList = false; return r;
    }
    if (v.t == VT::Array) {
        if (v.itemized) { // an itemized Array is ONE element: `my @row = @m[0]` is [[...],]
            Value r = Value::array(); r.arr->push_back(v); return r;
        }
        if (v.ext) return v; // a lazy seq stays lazy (shared machinery; consumers materialise)
        // `@b = @a` / `@x is copy` copy the top-level buffer — a fresh Array that does
        // NOT alias the source (nested itemized arrays are containers, shared by value,
        // matching Rakudo). Mirrors rtArrayVal so the interpreter and native backends agree.
        Value r = Value::array(*v.arr); r.isList = false; return r;
    }
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
        } else if (items[i].t == VT::Hash && items[i].hash && items[i].hashKind.empty() &&
                   !items[i].itemized) {
            // a plain (non-itemized) Hash in the list MERGES its pairs
            // (`%( $<authority>.ast, path => … )` in Cro::Uri) — it is not a key.
            // An ITEMIZED $hashitem stays whole (S02 assigning-refs).
            for (auto& kv : *items[i].hash) (*h.hash)[kv.first] = kv.second;
        } else if (i + 1 < items.size()) {
            (*h.hash)[items[i].toStr()] = items[i + 1];
            i++;
        }
    }
    return h;
}

// Per-thread execution registers. One instance per real thread; the GIL still
// serialises who runs. See the declaration in Interpreter.h.
static Interpreter* g_cbInterp = nullptr; // NativeCall callback trampoline target
thread_local ExecContext Interpreter::tctx_;
// Per-thread call-stack state (step 3a — see header).
thread_local std::vector<Interpreter::RedispatchCtx> Interpreter::redispatchStack_;
thread_local std::vector<std::shared_ptr<ReactCtx>> Interpreter::reactStack_;
thread_local int Interpreter::threadDepth_ = 0;

// the live interpreter's class registry, for free-function smartmatch on user
// type objects (applyArith has no Interpreter&); the newest instance wins
static std::unordered_map<std::string, std::shared_ptr<ClassInfo>>* g_matchClasses = nullptr;
// subset check for free-function ~~ (`5 ~~ Five`): returns true and sets `out`
// when the RHS names a live subset; the newest interpreter instance wins
std::function<bool(const std::string&, const Value&, bool&)> g_subsetCheck;

void rtSetAliasView(const std::unordered_map<std::string, std::string>* a,
                    const std::unordered_map<std::string, std::shared_ptr<ClassInfo>>* c); // defined near typeMatchesArg

Interpreter::Interpreter() {
    g_cbInterp = this; // NativeCall callback trampolines dispatch through here
    g_matchClasses = &classes_;
    rtSetAliasView(&classAliases_, &classes_); // package-relative short names for the type matchers
    g_subsetCheck = [this](const std::string& name, const Value& v, bool& out) {
        if (!subsets_.count(name)) return false;
        out = subsetMatches(name, v);
        return true;
    };
    initInstant_ = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    defaultScheduler_ = Value::makeHash(); defaultScheduler_.hashKind = "Scheduler";
    (*defaultScheduler_.hash)["name"] = Value::str("ThreadPoolScheduler");
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
    // — plus the handful of typed exceptions roast constructs with .new.
    {
        auto reg = [&](const char* name, std::initializer_list<const char*> attrs) {
            auto ci = std::make_shared<ClassInfo>();
            ci->name = name;
            for (const char* an : attrs) { ClassAttr a; a.name = an; a.sigil = '$'; a.pub = true; ci->attrs.push_back(a); }
            classes_[name] = ci;
        };
        reg("X::AdHoc", {"message"});
        reg("X::Syntax::Reserved", {"reserved", "instead", "pos", "message"});
        reg("Exception", {"message"}); // base class: Exception.new is instantiable
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

// Set `$/` after a match/substitution. If an enclosing scope already has a $/,
// UPDATE that one — a match inside a desugared block (`(S/// with X)` becomes
// `do { with X { … } }`) must be visible to the caller, like Rakudo's
// per-routine $/ declaration.
void Interpreter::setMatchVar(Value v) {
    for (Env* e = tctx_.cur.get(); e; e = e->parent.get()) {
        auto it = e->vars.find("$/");
        if (it != e->vars.end()) { it->second = std::move(v); return; } // update the visible $/
        if (e->routineFrame || !e->parent) { e->vars["$/"] = std::move(v); return; } // scope to the routine (or program top)
    }
}

// Raku `my`/`state` declarations are visible throughout their enclosing block,
// even when the declaration is textually inside one branch of a ternary/if/nqp
// op and referenced from a sibling branch (JSON::Fast declares `my int $codepoint`
// in its `\u` branch and assigns it from the `\n` branch). Pre-declare such vars
// in the block env so a sibling reference resolves. We descend through EXPRESSION
// nodes only — never into a nested Block/BlockExpr/SubDecl body, which owns its
// own scope. Zero-cost for blocks with no expression-buried declarations.
void Interpreter::hoistExprDecls(const std::vector<StmtPtr>& stmts, Env* env) {
    // Narrow by design: only a plain `my` declared INSIDE a conditional branch
    // (a Ternary then/else, or an nqp::if/while/stmts arg) needs hoisting for a
    // SIBLING branch to see it. `state` is excluded (its persistence machinery
    // owns scoping), and a `my` in normal statement flow is left alone (hoisting
    // it would disturb loop/gather per-iteration freshness — that cost the
    // state/gather regressions). `cond` = we're inside such a branch.
    std::function<void(const Expr*, bool)> walkE = [&](const Expr* e, bool cond) {
        if (!e) return;
        switch (e->kind) {
            case NK::VarExpr: {
                auto* v = static_cast<const VarExpr*>(e);
                if (cond && v->declare && v->declScope == "my" &&
                    !v->name.empty() && !env->vars.count(v->name))
                    env->vars[v->name] = typedDefault(v->declType, v->name[0]);
                if (v->declDefault) walkE(v->declDefault.get(), cond);
                break;
            }
            case NK::Ternary: { auto* t = static_cast<const Ternary*>(e); walkE(t->cond.get(), cond); walkE(t->then.get(), true); walkE(t->els.get(), true); break; }
            case NK::NqpOp:   { for (auto& x : static_cast<const NqpOp*>(e)->args) walkE(x.get(), true); break; }
            case NK::Binary:  { auto* b = static_cast<const Binary*>(e); walkE(b->lhs.get(), cond); walkE(b->rhs.get(), cond); break; }
            case NK::Unary:   walkE(static_cast<const Unary*>(e)->operand.get(), cond); break;
            case NK::Assign:  { auto* a = static_cast<const Assign*>(e); walkE(a->target.get(), cond); walkE(a->value.get(), cond); break; }
            case NK::Call:    { auto* c = static_cast<const Call*>(e); if (c->callee) walkE(c->callee.get(), cond); for (auto& x : c->args) walkE(x.get(), cond); break; }
            case NK::MethodCall: { auto* m = static_cast<const MethodCall*>(e); walkE(m->inv.get(), cond); for (auto& x : m->args) walkE(x.get(), cond); break; }
            case NK::Index:   { auto* i = static_cast<const Index*>(e); walkE(i->base.get(), cond); walkE(i->index.get(), cond); break; }
            case NK::ListExpr:{ for (auto& x : static_cast<const ListExpr*>(e)->items) walkE(x.get(), cond); break; }
            case NK::ChainExpr: { for (auto& x : static_cast<const ChainExpr*>(e)->operands) walkE(x.get(), cond); break; }
            // Block / BlockExpr / lambda bodies own their scope — do NOT descend.
            default: break;
        }
    };
    for (auto& s : stmts)
        if (s->kind == NK::ExprStmt) walkE(static_cast<const ExprStmt*>(s.get())->e.get(), false);
}

bool Interpreter::hoistSubs(const std::vector<StmtPtr>& stmts) {
    // Named subs are visible across their whole enclosing scope regardless of
    // textual position, so register them before executing the statements.
    bool any = false;
    bool saved = hoistingSubs_; hoistingSubs_ = true;
    for (auto& s : stmts) {
        if (s->kind == NK::SubDecl) {
            auto* sd = static_cast<SubDecl*>(s.get());
            if (!sd->isMethod && !sd->name.empty()) { exec(s.get()); any = true; }
        }
    }
    hoistingSubs_ = saved;
    return any;
}

// Run a hoisted sub's user `is` traits at its textual position (the sub itself
// was registered at scope entry; the trait handler may use `my`s declared above).
void Interpreter::applySubTraits(SubDecl* sd) {
    if (sd->traits.empty()) return;
    Value* tm = tctx_.cur->find("&trait_mod:<is>");
    if (!tm || tm->t != VT::Code) return;
    Value* fn = tctx_.cur->find("&" + sd->name);
    if (!fn) return;
    for (auto& st : sd->traits) {
        Value arg = st.arg ? eval(st.arg.get()) : Value::boolean(true);
        Value p = Value::pair(st.name, arg); p.namedArg = true;
        ValueList ta; ta.push_back(*fn); ta.push_back(p);
        try { callCallable(*tm, ta); }
        catch (RakuError&) {} // no matching candidate: not this handler's trait
    }
}

// A named sub hoisted into a scope closes over that scope's Env and is stored back
// into it, forming a shared_ptr cycle (Env→Value→Callable→closure→Env, plus a
// second edge via stateEnv->parent) that neither side can free — the frame leaks.
// When the frame exits, break the back-edges of any Code that closes over THIS env
// and is referenced nowhere else (use_count 1 ⇒ it did not escape via return or
// assignment). A torn-down non-escaped frame has no surviving `state` to preserve.
void Interpreter::breakSelfClosures(Env* env) {
    // Wiring an on-demand supply block: its env OUTLIVES the frame (whenever
    // taps fire later, from I/O workers) — a nested `my sub`'s closure must
    // survive, so the frame-death heuristic is suspended.
    if (noCycleBreak_ > 0) return;
    for (auto& kv : env->vars) {
        Value& v = kv.second;
        if (v.t == VT::Code && v.code && v.code.use_count() == 1 &&
            v.code->closure.get() == env) {
            v.code->closure.reset();
            v.code->stateEnv.reset();
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
    c.tapStack    = std::move(tctx_.tapStack);
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
    tctx_.tapStack     = std::move(c.tapStack);
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
    // cap: preserves small relative delays (sleep-sort), bounded for the harness.
    // With workers outstanding the full duration matters (scheduler cues fire
    // during the sleep) — allow it, still bounded.
    double cap = (liveWorkers_.load() > 0 || cuedLoads_.load() > 0) ? 35.0 : 1.0;
    if (secs > cap) secs = cap;
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
thread_local Value t_threadSelf;
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
Value Interpreter::spawnPromise(Value code, Value threadVal) {
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
        reapFinishedWorkers();
        auto fin = std::make_shared<std::atomic<bool>>(false);
        auto spawnScope = tctx_.cur ? tctx_.cur : global_;
        workers_.push_back({BigStackThread([self, code, ps, fin, spawnScope, threadVal]() mutable {
            t_isWorker = true;
            if (threadVal.t == VT::Hash) t_threadSelf = threadVal;
            tctx_.cur = spawnScope;        // anchor: dynamics visible at the spawn point
            tctx_.dynStack.push_back(spawnScope.get());
            Value r; bool broke = false; Value cause;
            try {
                ValueList noargs;
                r = code.t == VT::Code ? self->callCallable(code, noargs) : code;
            }
            catch (const RakuError& e) { broke = true; cause = e.payload; ps->causeMsg = e.message; }
            catch (...) { broke = true; }
            std::vector<std::function<void()>> fire;
            {
                std::lock_guard<std::mutex> lk(ps->m);
                ps->result = r; ps->cause = cause; ps->broken = broke; ps->done = true;
                fire.swap(ps->thens);      // `.then`s registered before the worker settled
            }
            ps->cv.notify_all();
            for (auto& f : fire) f();
            self->liveWorkers_--;
            fin->store(true, std::memory_order_release);
        }), fin});
        return p;
    }
    liveWorkers_++;
    reapFinishedWorkers();                 // release stacks of already-finished workers
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_; // dynamics visible at the spawn point
    workers_.push_back({BigStackThread([self, code, ps, fin, spawnScope, threadVal]() mutable {
        t_isWorker = true;
        if (threadVal.t == VT::Hash) t_threadSelf = threadVal;
        self->gil_.lock();                 // acquire the GIL (main must have yielded)
        ExecContext wctx;                  // fresh, empty registers for this worker
        self->loadCtx(wctx);
        tctx_.cur = spawnScope;            // anchor: `start {…}` sees the spawner's dynamics
        tctx_.dynStack.push_back(spawnScope.get());
        Value r; bool broke = false; Value cause;
        try {
            ValueList noargs;
            r = code.t == VT::Code ? self->callCallable(code, noargs) : code;
        }
        catch (const RakuError& e) { broke = true; cause = e.payload; ps->causeMsg = e.message; }
        catch (...) { broke = true; }
        self->saveCtx(wctx);               // pull worker registers back out
        std::vector<std::function<void()>> fire;
        {
            std::lock_guard<std::mutex> lk(ps->m);
            ps->result = r; ps->cause = cause; ps->broken = broke; ps->done = true;
            fire.swap(ps->thens);          // `.then`s registered before the worker settled
        }
        for (auto& f : fire) f();          // run them now, still on the GIL — like keep/break
        self->liveWorkers_--;
        self->gilYieldNotify();            // release the GIL (waking any cooperative yielder)
        ps->cv.notify_all();
        fin->store(true, std::memory_order_release);
    }), fin});
    return p;
}

// $*SCHEDULER.cue(&code, :in/:at → delaySecs, :every, :times, :stop, :catch).
// A worker sleeps GIL-free, takes the GIL per run, and re-arms for :every.
Value Interpreter::cueJob(Value code, double delaySecs, double everySecs, long long times,
                          Value stopF, Value catchF) {
    engageGil();
    auto cs = std::make_shared<CueState>();
    Value cancellation = Value::makeHash(); cancellation.hashKind = "Cancellation";
    cancellation.ext = cs;
    cuedLoads_++;
    liveWorkers_++;
    reapFinishedWorkers();
    auto fin = std::make_shared<std::atomic<bool>>(false);
    auto spawnScope = tctx_.cur ? tctx_.cur : global_;
    Interpreter* self = this;
    workers_.push_back({BigStackThread([self, code, cs, fin, spawnScope, delaySecs, everySecs, times, stopF, catchF]() mutable {
        t_isWorker = true;
        auto clock0 = std::chrono::steady_clock::now();
        auto napUntil = [&](double secsFromStart) { // GIL not held here; drift-free deadline
            auto dl = clock0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::duration<double>(std::min(secsFromStart, 600.0)));
            std::this_thread::sleep_until(dl);
        };
        double firedAt = delaySecs;
        napUntil(delaySecs);
        self->gil_.lock();
        ExecContext wctx;
        self->loadCtx(wctx);
        tctx_.cur = spawnScope;
        tctx_.dynStack.push_back(spawnScope.get());
        long long ran = 0;
        for (;;) {
            if (cs->cancelled.load(std::memory_order_relaxed)) break;
            if (stopF.t == VT::Code) {
                bool stop = false;
                try { ValueList na; stop = self->callCallable(stopF, na).truthy(); } catch (...) {}
                if (stop) break;
            }
            try { ValueList na; self->callCallable(code, na); }
            catch (const RakuError& e) {
                if (catchF.t == VT::Code) {
                    try { ValueList ca{self->exceptionFor(e)}; self->callCallable(catchF, ca); } catch (...) {}
                }
            }
            catch (const WorkerAbortEx&) { break; } // shutdown: unwind the cue loop
            catch (...) {}
            if (self->workerAbort_.load(std::memory_order_relaxed)) break;
            ran++;
            // :times(N) without :every repeats back-to-back; :every repeats on the
            // interval; neither -> one-shot
            long long target = times > 0 ? times : (everySecs > 0 ? -1 : 1);
            if (target > 0 && ran >= target) break;
            if (everySecs > 0) {
                // park like workerYield: the live registers belong to whoever holds
                // the GIL — save ours, sleep unlocked, re-acquire, restore
                static thread_local ExecContext parked; // reused buffers: no per-tick allocs
                self->saveCtx(parked);
                self->gilYieldNotify();
                firedAt += everySecs;      // fixed cadence from the START, not from run end
                napUntil(firedAt);
                self->gil_.lock();
                self->loadCtx(parked);
            }
        }
        self->saveCtx(wctx);
        self->cuedLoads_--;
        self->liveWorkers_--;
        self->gilYieldNotify();
        fin->store(true, std::memory_order_release);
    }), fin});
    return cancellation;
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
    plk.unlock();          // drop ps->m BEFORE reacquiring the GIL (avoids ABBA with keep/break/second-awaiter)
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
        lk.unlock();       // drop ctx->m before reacquiring the GIL (avoids ABBA with emitters)
        gil_.lock();
        loadCtx(parked);
        lk.lock();         // re-check the loop condition under ctx->m
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
            std::vector<WorkerSlot> batch;
            { std::lock_guard<std::mutex> lk(sharedMut_); batch.swap(workers_); }
            if (batch.empty()) break;
            for (auto& t : batch) if (t.th.joinable()) t.th.join();
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
    std::vector<WorkerSlot> batch;
    { std::lock_guard<std::mutex> lk(sharedMut_); batch.swap(workers_); } // take ownership under the lock
    if (liveWorkers_.load() > 0) {
        for (auto& t : batch) t.th.detach(); // daemon: don't wait on it
        abandonedWorkers_ = true;
    } else {
        for (auto& t : batch) if (t.th.joinable()) t.th.join(); // reap the finished threads
    }
    gilHeld_ = false;
}

void Interpreter::flushOpenWriteHandles() {
    for (auto& h : openWriteHandles_) {
        if (!h) continue;
        auto cl = h->find("closed"); if (cl != h->end() && cl->second.truthy()) continue;
        auto fl = h->find("flushed"); if (fl != h->end() && fl->second.truthy()) continue;
        std::string mode = (*h)["mode"].toStr();
        std::ofstream out((*h)["path"].toStr(), mode == "a" ? std::ios::app : std::ios::trunc);
        if (out) out << (*h)["buffer"].toStr();
    }
    openWriteHandles_.clear();
}

// ---- mainline sink-context "Useless use" warnings ------------------------
// A conservative compile-time pass over the mainline (and blocks/loop bodies
// reached from it): only provably value-only statements warn. Anything with a
// call, assignment, or unknown node stays silent — over-warning would pollute
// stderr that tests compare exactly.
static bool sinkPure(Expr* e, std::string& spell, std::string& kindw) {
    switch (e->kind) {
        case NK::IntLit: {
            auto* n = static_cast<IntLit*>(e);
            spell = !n->raw.empty() ? n->raw : (!n->big.empty() ? n->big : std::to_string(n->v));
            kindw = "constant integer ";
            return true;
        }
        case NK::NumLit: {
            auto* n = static_cast<NumLit*>(e);
            spell = n->raw;
            if (spell.empty()) { std::ostringstream o; o << n->v; spell = o.str(); }
            kindw = "constant number ";
            return true;
        }
        case NK::StrLit:
            spell = "\"" + static_cast<StrLit*>(e)->v + "\"";
            kindw = "constant string ";
            return true;
        case NK::AllomorphLit: // a `<42>`/`<1.5>` word warns as its numeric literal
            return sinkPure(static_cast<AllomorphLit*>(e)->num.get(), spell, kindw);
        case NK::InterpStr: { // "foo" with nothing to interpolate is still constant
            std::string flat;
            for (auto& p : static_cast<InterpStr*>(e)->parts) {
                if (p->kind != NK::StrLit) return false;
                flat += static_cast<StrLit*>(p.get())->v;
            }
            spell = "\"" + flat + "\"";
            kindw = "constant string ";
            return true;
        }
        case NK::VarExpr: {
            auto* ve = static_cast<VarExpr*>(e);
            const std::string& n = ve->name;
            // a bare `$` term (parsed as an anonymous state var) is useless in
            // sink position even though it technically declares
            if (n.rfind("$anon--state--", 0) == 0) {
                spell = "unnamed $ variable"; kindw = "";
                return true;
            }
            if (ve->declare) return false; // `my $b;` declares, no useless use
            if (n.empty() || (n[0] != '$' && n[0] != '@' && n[0] != '%')) return false;
            if (n.size() == 1) { spell = "unnamed " + n + " variable"; kindw = ""; return true; }
            char tw = n[1]; // no twigilled/special vars: $*DYN may throw, $_ / $/ / $! are set by context
            if (!(std::isalpha((unsigned char)tw) && (unsigned char)tw < 0x80) && tw != '-') return false;
            spell = n; kindw = "";
            return true;
        }
        case NK::NameTerm: {
            const std::string& n = static_cast<NameTerm*>(e)->name;
            static const std::set<std::string> known = {
                "Inf", "NaN", "pi", "tau", "e", "\xCF\x80", "\xCF\x84",
                "Any", "Mu", "Cool", "Int", "Str", "Num", "Rat", "Complex"};
            if (!known.count(n) || !static_cast<NameTerm*>(e)->ofType.empty()) return false;
            spell = n; kindw = "";
            return true;
        }
        case NK::BoolLit:
            spell = static_cast<BoolLit*>(e)->v ? "True" : "False";
            kindw = "constant Bool ";
            return true;
        case NK::Unary: {
            auto* u = static_cast<Unary*>(e);
            if (u->postfix || !u->operand) return false;
            if (u->op != "-" && u->op != "+" && u->op != "?" && u->op != "~" && u->op != "!")
                return false;
            std::string in;
            if (!sinkPure(u->operand.get(), in, kindw)) return false;
            spell = u->op + in;
            return true;
        }
        case NK::Binary: {
            auto* b = static_cast<Binary*>(e);
            static const std::set<std::string> pureOps = {
                "+", "-", "*", "/", "%", "**", "~", "x", "div", "mod", "gcd", "lcm",
                "==", "!=", "<", "<=", ">", ">=", "eq", "ne", "lt", "le", "gt", "ge"};
            if (!pureOps.count(b->op)) return false;
            std::string l, r, kw2;
            if (!b->lhs || !b->rhs) return false;
            if (!sinkPure(b->lhs.get(), l, kindw) || !sinkPure(b->rhs.get(), r, kw2)) return false;
            spell = l + b->op + r; kindw = "";
            return true;
        }
        case NK::Pair: {
            auto* p = static_cast<PairExpr*>(e);
            if (!p->value) return false;
            if (p->keyExpr) { // "foo" => 42 — a constant quoted key is still pure
                std::string k, v, kw2, kw3;
                if (!sinkPure(p->keyExpr.get(), k, kw2) || kw2 != "constant string " ||
                    !sinkPure(p->value.get(), v, kw3)) return false;
                spell = k + " => " + v; kindw = "";
                return true;
            }
            if (p->key.empty()) return false;
            std::string v, kw2;
            if (!sinkPure(p->value.get(), v, kw2)) return false;
            spell = p->colonForm ? ":" + p->key + "(" + v + ")"
                                 : (p->quotedKey ? "\"" + p->key + "\"" : p->key) + " => " + v;
            kindw = "";
            return true;
        }
        case NK::ListExpr: { // only the EMPTY list as a unit; others warn per element
            auto* le = static_cast<ListExpr*>(e);
            if (!le->items.empty()) return false;
            spell = "()"; kindw = "";
            return true;
        }
        default: return false;
    }
}

static void sinkWarnStmt(Stmt* s, bool nilHint, std::vector<std::string>& out);

static void sinkWarnOne(Expr* e, int line, bool nilHint, std::vector<std::string>& out) {
    std::string sp, kw;
    if (!sinkPure(e, sp, kw)) return;
    out.push_back("Useless use of " + kw + sp + " in sink context" +
                  (nilHint ? " (use Nil instead to suppress this warning)" : "") +
                  " (line " + std::to_string(line) + ")");
}

static void sinkWarnExprTop(Expr* e, int line, bool nilHint, std::vector<std::string>& out) {
    if (e->kind == NK::BlockExpr) { // a bare block runs sunk: its statements sink too
        auto* be = static_cast<BlockExpr*>(e);
        if (!be->isSub && be->params.empty())
            for (auto& st : be->body) sinkWarnStmt(st.get(), nilHint, out);
        return;
    }
    if (e->kind == NK::ListExpr && !static_cast<ListExpr*>(e)->items.empty()) {
        for (auto& it : static_cast<ListExpr*>(e)->items) // sink distributes over commas
            sinkWarnExprTop(it.get(), line, nilHint, out);
        return;
    }
    if (e->kind == NK::ArrayLit && static_cast<ArrayLit*>(e)->isList) { // word list <a b c>
        for (auto& it : static_cast<ArrayLit*>(e)->items)
            sinkWarnOne(it.get(), line, nilHint, out);
        return;
    }
    sinkWarnOne(e, line, nilHint, out);
}

// find `gather` inside a mainline statement's expression (its body is sunk:
// `my @x = gather 43` must warn about the 43)
static void sinkScanGather(Expr* e, std::vector<std::string>& out) {
    if (!e) return;
    switch (e->kind) {
        case NK::Unary: {
            auto* u = static_cast<Unary*>(e);
            if (u->op == "gather" && u->operand) {
                if (u->operand->kind == NK::BlockExpr) {
                    for (auto& st : static_cast<BlockExpr*>(u->operand.get())->body)
                        sinkWarnStmt(st.get(), false, out);
                }
                else sinkWarnOne(u->operand.get(), u->line, false, out);
                return;
            }
            sinkScanGather(u->operand.get(), out);
            return;
        }
        case NK::Binary: {
            auto* b = static_cast<Binary*>(e);
            sinkScanGather(b->lhs.get(), out); sinkScanGather(b->rhs.get(), out);
            return;
        }
        case NK::Assign:
            sinkScanGather(static_cast<Assign*>(e)->value.get(), out);
            return;
        case NK::ListExpr:
            for (auto& it : static_cast<ListExpr*>(e)->items) sinkScanGather(it.get(), out);
            return;
        default: return;
    }
}

static void sinkWarnStmt(Stmt* s, bool nilHint, std::vector<std::string>& out) {
    if (!s) return;
    switch (s->kind) {
        case NK::ExprStmt: {
            auto* es = static_cast<ExprStmt*>(s);
            if (!es->e) return;
            int ln = s->line ? s->line : es->e->line;
            sinkWarnExprTop(es->e.get(), ln, nilHint, out);
            sinkScanGather(es->e.get(), out);
            return;
        }
        case NK::Block: {
            auto* b = static_cast<Block*>(s);
            if (b->isCatch || !b->phaser.empty()) return;
            for (auto& st : b->stmts) sinkWarnStmt(st.get(), nilHint, out);
            return;
        }
        case NK::WhileStmt: {
            auto* w = static_cast<WhileStmt*>(s);
            if (!w->asExpr && w->body)
                for (auto& st : w->body->stmts) sinkWarnStmt(st.get(), true, out);
            return;
        }
        case NK::ForStmt: {
            auto* f = static_cast<ForStmt*>(s);
            if (!f->asExpr && f->body)
                for (auto& st : f->body->stmts) sinkWarnStmt(st.get(), true, out);
            return;
        }
        case NK::RepeatStmt: {
            auto* r = static_cast<RepeatStmt*>(s);
            if (r->body) for (auto& st : r->body->stmts) sinkWarnStmt(st.get(), true, out);
            return;
        }
        case NK::LoopStmt: {
            auto* l = static_cast<LoopStmt*>(s);
            if (!l->asExpr && l->body)
                for (auto& st : l->body->stmts) sinkWarnStmt(st.get(), true, out);
            return;
        }
        case NK::GivenStmt: { // only the modifier form (`1.0 given 1,2`) is surely sunk
            auto* g = static_cast<GivenStmt*>(s);
            if (g->modifier && g->body)
                for (auto& st : g->body->stmts) sinkWarnStmt(st.get(), true, out);
            return;
        }
        case NK::VarDecl: {
            auto* d = static_cast<VarDecl*>(s);
            if (d->init) sinkScanGather(d->init.get(), out);
            return;
        }
        default: return;
    }
}

int Interpreter::run(Program& prog) {
    int code = 0;
    bool crashed = false;
    { // mainline sink warnings, printed before execution (Rakudo compile-time style)
        bool noWorries = false;
        for (auto& s : prog.stmts)
            if (s->kind == NK::UseStmt) {
                auto* us = static_cast<UseStmt*>(s.get());
                if (us->isNo && (us->module == "worries" || us->module == "warnings")) noWorries = true;
            }
        if (!noWorries) {
            std::vector<std::string> ws;
            for (auto& s : prog.stmts) sinkWarnStmt(s.get(), false, ws);
            for (auto& w : ws) std::cerr << w << "\n";
        }
    }
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
    auto runPhaser = [&](Block* b) {
        if (b->stmtForm) { execBlock(b, tctx_.cur); return; } // `INIT my $x = …` declares in the mainline scope
        auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur; execBlock(b, sc);
    };
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
                    if (!ve->name.empty() && !global_->vars.count(ve->name)) {
                        if (ve->declShape && ve->name[0] == '@')
                            global_->define(ve->name, makeShapedContainer(evalShapeDims(ve->declShape.get()), ve->declType));
                        else
                            global_->define(ve->name, typedDefault(ve->declType, ve->name[0]));
                    }
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
                !static_cast<SubDecl*>(s)->isMethod) { applySubTraits(static_cast<SubDecl*>(s)); continue; } // hoisted
            // a bare `my $x;` (no init) must not clobber a value a phaser already set
            std::string nm = topDecl(s, hasInit);
            if (!nm.empty() && !hasInit && global_->vars.count(nm)) {
                // the skipped declaration still owns its container metadata:
                // `my $port is default(8080);` initializes AND registers the default
                if (s->kind == NK::ExprStmt) {
                    Expr* e0 = static_cast<ExprStmt*>(s)->e.get();
                    if (e0 && e0->kind == NK::VarExpr) {
                        auto* ve0 = static_cast<VarExpr*>(e0);
                        if (ve0->declDefault) {
                            Value dv = eval(ve0->declDefault.get());
                            if (ve0->name[0] == '@' || ve0->name[0] == '%') {
                                // container stays empty; v is the ELEMENT default
                                Value c = ve0->name[0] == '@' ? Value::array() : Value::makeHash();
                                c.pairVal = std::make_shared<Value>(dv);
                                global_->vars[ve0->name] = c;
                            } else {
                                global_->varDefault[ve0->name] = dv;
                                global_->vars[ve0->name] = dv;
                            }
                        }
                        else if (ve0->name[0] == '$' && !ve0->declType.empty() &&
                                 std::isupper((unsigned char)ve0->declType[0]))
                            global_->varDefault[ve0->name] = Value::typeObj(ve0->declType);
                    }
                }
                continue;
            }
            exec(s, /*sink=*/true); // every top-level statement is in sink context (Rakudo)
        }
        // auto-invoke MAIN with command-line arguments, if defined
        if (Value* mainSub = tctx_.cur->find("&MAIN")) {
            ValueList margs = rtMainArgs(argv_);
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
            else if (mainSub->code && mainSub->code->params) // single MAIN: same bind check
                mainMatches = scoreCandidate(*mainSub, margs) >= 0;
            if (!mainMatches) {
                // a user-defined USAGE takes over (it prints to stdout, like Rakudo)
                Value* usage = tctx_.cur->find("&USAGE");
                if (!usage && global_) usage = global_->find("&USAGE");
                if (usage) {
                    try { callCallable(*usage, {}); } catch (ExitEx& e) { code = e.code; goto mainDone; }
                }
                else std::cerr << mainUsage();
                code = 2;
                mainDone: ;
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
            else {
                // a compile-time (X::Comp-style) exception carries filename+line
                // attrs — print it with Rakudo's ===SORRY!=== banner and location
                std::string cf, cl;
                if (e.payload.t == VT::Object && e.payload.obj) {
                    auto& at = e.payload.obj->attrs;
                    auto fi = at.find("filename"), li = at.find("line");
                    if (fi != at.end() && li != at.end()) { cf = fi->second.toStr(); cl = li->second.toStr(); }
                }
                if (!cf.empty())
                    std::cerr << "===SORRY!=== Error while compiling " << cf << "\n"
                              << e.message << "\nat " << cf << ":" << cl << "\n";
                else std::cerr << e.message << "\n";
            }
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
    flushOpenWriteHandles(); // write out any file handle the program forgot to .close
    drainWorkers(); // join any outstanding async workers before we tear down
    // Compilation-unit LEAVE/KEEP/UNDO phasers run (reverse source order) on the
    // way out — after the mainline, before END.
    for (auto it = leaveP.rbegin(); it != leaveP.rend(); ++it) {
        try { runPhaser(*it); } catch (ExitEx& e) { code = e.code; } catch (...) {}
    }
    runEnds(); // END phasers (reverse source order), after the mainline
    // Rakudo's Test module never fabricates a trailing plan (and does not warn):
    // a file that ran tests without `plan`/`done-testing` just ends its TAP.
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

// Build the %?RESOURCES hash for an installed distribution: read its dist meta
// (JSON) and map each `resources/<name>` file entry to an IO::Path pointing at
// the on-disk `<repo>/resources/<content-id>` copy. The dist `files` map is a
// flat string→string object, so a targeted scan for `"resources/…":"…"` pairs
// is enough (the top-level `resources` array holds bare strings, no colon).
Value Interpreter::buildResourceMap(const std::string& repo, const std::string& distId) {
    Value h = Value::makeHash(); h.hashKind = "";
    std::ifstream meta(repo + "/dist/" + distId);
    if (!meta) return h;
    std::ostringstream ss; ss << meta.rdbuf();
    const std::string j = ss.str();
    // scan for "resources/<key>" : "<value>"
    size_t p = 0;
    const std::string tag = "\"resources/";
    while ((p = j.find(tag, p)) != std::string::npos) {
        size_t ks = p + tag.size();
        size_t ke = j.find('"', ks);
        if (ke == std::string::npos) break;
        std::string key = j.substr(ks, ke - ks);
        size_t c = j.find(':', ke);
        if (c == std::string::npos) { p = ke + 1; continue; }
        size_t vs = j.find('"', c);
        if (vs == std::string::npos) break;
        size_t ve = j.find('"', vs + 1);
        if (ve == std::string::npos) break;
        std::string val = j.substr(vs + 1, ve - vs - 1);
        Value path = Value::str(repo + "/resources/" + val);
        path.hashKind = "IO"; // IO::Path — supports .slurp/.IO/.Str/.e
        (*h.hash)[key] = path;
        p = ve + 1;
    }
    return h;
}

void Interpreter::loadModule(const std::string& name, const std::vector<std::string>& importArgs, bool doImport) {
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
        // `is export` subs may sit inside a BRACED module/package body
        // (Cro::HTTP::Router exports `get`/`post`/… from `module … { }`) —
        // recurse so a builtin-colliding name like `get` still publishes
        std::function<void(const std::vector<StmtPtr>&)> scanExports =
            [&](const std::vector<StmtPtr>& stmts) {
            for (auto& st : stmts) {
                if (st->kind == NK::SubDecl) {
                    auto* sd = static_cast<SubDecl*>(st.get());
                    if (sd->isExport && !sd->name.empty()) exported.insert(sd->name);
                } else if (st->kind == NK::ClassDecl)
                    scanExports(static_cast<ClassDecl*>(st.get())->body);
            }
        };
        scanExports(prog->stmts);
        tctx_.cur = moduleEnv;
        auto savedPkg = curPkgEnv_; curPkgEnv_ = moduleEnv; // `our sub` in the module installs here, not main's global
        // A `unit module Foo;` sets pkgPrefix for the rest of the file so its
        // `our sub`s publish qualified (Foo::name); save/restore so it can't leak
        // into the importing program.
        auto savedModPrefix = tctx_.pkgPrefix; tctx_.pkgPrefix.clear();
        auto publish = [&] {
            for (auto& kv : moduleEnv->vars) {
                const std::string& k = kv.first;
                if (k == "&EXPORT") continue; // per-module export protocol sub — never republished
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
        loadingModuleDepth_++;
        bool savedDoImport = moduleDoImport_; moduleDoImport_ = doImport;
        try {
            hoistSubs(prog->stmts);
            for (auto& st : prog->stmts) {
                if (st->kind == NK::SubDecl && !static_cast<SubDecl*>(st.get())->name.empty() &&
                    !static_cast<SubDecl*>(st.get())->isMethod) {
                    auto* sd = static_cast<SubDecl*>(st.get());
                    applySubTraits(sd);
                    // A leading `unit module Foo;` has now set pkgPrefix, but this
                    // `our sub` was already defined by hoistSubs (bare) — publish it
                    // under its qualified name so external `Foo::name()` calls resolve.
                    if (sd->isOur && !tctx_.pkgPrefix.empty())
                        if (Value* c = tctx_.cur->find("&" + sd->name))
                            global_->define("&" + tctx_.pkgPrefix + sd->name, *c);
                    continue; // hoisted
                }
                exec(st.get());
            }
        }
        catch (RakuError& e) {
            loadingModuleDepth_--; moduleDoImport_ = savedDoImport;
            publish();
            tctx_.cur = saved; curPkgEnv_ = savedPkg; finishData_ = savedFinish; tctx_.pkgPrefix = savedModPrefix;
            std::cerr << "===WARNING=== Module " << name << " failed during load: " << e.message << "\n";
            return;
        }
        catch (...) { loadingModuleDepth_--; moduleDoImport_ = savedDoImport; tctx_.cur = saved; curPkgEnv_ = savedPkg; finishData_ = savedFinish; tctx_.pkgPrefix = savedModPrefix; throw; }
        loadingModuleDepth_--; moduleDoImport_ = savedDoImport;
        publish();
        tctx_.cur = saved; curPkgEnv_ = savedPkg; finishData_ = savedFinish; tctx_.pkgPrefix = savedModPrefix;
        // `sub EXPORT(*@_)` protocol: call it with the use-statement's <...>
        // args; its returned Map ('&name' => &code, ...) defines the imports
        // in the USING scope.
        {
            auto it = moduleEnv->vars.find("&EXPORT");
            if (doImport && it != moduleEnv->vars.end() && it->second.t == VT::Code) {
                ValueList eargs;
                for (auto& s : importArgs) eargs.push_back(Value::str(s));
                try {
                    Value res = callCallable(it->second, eargs);
                    if (res.t == VT::Hash && res.hash)
                        for (auto& kv : *res.hash) tctx_.cur->define(kv.first, kv.second);
                } catch (RakuError& e) {
                    std::cerr << "===WARNING=== Module " << name
                              << " EXPORT failed: " << e.message << "\n";
                }
            }
        }
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
        // Bind %?RESOURCES for this module (the short-index filename is its dist
        // id) so BEGIN-time `%?RESOURCES<x>.slurp` resolves. Pop after loading.
        resourceStack_.push_back(buildResourceMap(repo, entry));
        struct RGuard { std::vector<Value>& s; ~RGuard() { s.pop_back(); } } rg{resourceStack_};
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

Value Interpreter::evalString(const std::string& src, bool mainlinePH) {
    Lexer lexer(src);
    auto prog = std::make_shared<Program>();
    try {
        Parser parser(lexer.tokenize());
        parser.strictSep_ = true; // EVAL snippets get "two terms in a row" strictness
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
        if (e.exType == "X::Package::Stubbed") {
            // the space-joined `packages` names become a real list attribute
            std::string names;
            for (auto& kv : e.exAttrs) if (kv.first == "packages") names = kv.second;
            Value arr = Value::array(); arr.isList = true;
            size_t p = 0;
            while (p < names.size()) {
                size_t q = names.find(' ', p);
                arr.arr->push_back(Value::str(names.substr(p, q - p)));
                if (q == std::string::npos) break;
                p = q + 1;
            }
            throwTypedV("X::Package::Stubbed", {{"packages", arr}}, e.what());
        }
        if (e.exType == "X::Comp::Group") {
            // parse-level group diagnostic: the `sorrow` attr names the inner
            // exception type; rebuild it as a real object list in .sorrows
            std::string stype = "X::AdHoc";
            for (auto& kv : e.exAttrs) if (kv.first == "sorrow") stype = kv.second;
            Value arr = Value::array(); arr.isList = true;
            arr.arr->push_back(makeTypedEx(stype, {}, e.what()));
            throwTypedV("X::Comp::Group", {{"sorrows", arr}}, e.what());
        }
        if (!e.exType.empty()) throwTyped(e.exType, e.exAttrs, e.what()); // typed compile diagnostic
        throw RakuError{Value::typeObj("X::Syntax::Confused"), std::string("EVAL parse error: ") + e.what()};
    }
    // a placeholder in the EVAL mainline has no signature to attach to (only
    // for user-level EVAL: internal reparses, e.g. S{}=repl, must stay silent)
    if (mainlinePH)
        if (std::string ph = firstBlockPlaceholder(prog->stmts); !ph.empty())
            throwTyped("X::Placeholder::Mainline", {{"placeholder", ph}},
                       "Cannot use placeholder parameter " + ph + " in the mainline");
    { std::unique_lock<std::mutex> kl(sharedMut_, std::defer_lock); if (parallelMode_) kl.lock(); keptPrograms_.push_back(prog); } // keep AST alive for closures defined within
    Value last = Value::any();
    for (auto& s : prog->stmts) {
        // An END block in EVAL'd code runs at the END of the whole program (not here),
        // capturing the EVAL scope so it still sees this EVAL's lexicals.
        if (s->kind == NK::Block && static_cast<Block*>(s.get())->phaser == "END") {
            deferredEnds_.push_back({static_cast<Block*>(s.get()), tctx_.cur});
            continue;
        }
        // loop control with no loop in the EVAL is a CATCHABLE error, not a crash
        try { last = exec(s.get()); }
        catch (RedoEx&) { throw RakuError{Value::typeObj("X::ControlFlow"), "redo without a supporting loop construct"}; }
        catch (NextEx&) { throw RakuError{Value::typeObj("X::ControlFlow"), "next without a supporting loop construct"}; }
        catch (LastEx&) { throw RakuError{Value::typeObj("X::ControlFlow"), "last without a supporting loop construct"}; }
        catch (ReturnEx&) {
            // with an enclosing routine, `return` in the EVAL returns from IT;
            // top-level it is the spec'd control-flow error
            if (tctx_.curRoutineFrame != 0) throw;
            throw RakuError{Value::typeObj("X::ControlFlow::Return"), "Attempt to return outside of any Routine"};
        }
        // the cooperative flags must not leak out of a TOP-LEVEL EVAL — a phaser's
        // `return` inside an EVAL'd loop would silently unwind the whole program
        if (tctx_.returning && tctx_.curRoutineFrame == 0) {
            tctx_.returning = false;
            throw RakuError{Value::typeObj("X::ControlFlow::Return"), "Attempt to return outside of any Routine"};
        }
        if (tctx_.returning) return last; // enclosing routine consumes the flag
        if (tctx_.loopCtl) {
            int c = tctx_.loopCtl; tctx_.loopCtl = 0;
            throw RakuError{Value::typeObj("X::ControlFlow"),
                            std::string(c == 1 ? "next" : c == 2 ? "last" : "redo") +
                            " without a supporting loop construct"};
        }
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
        testNum_++; // advance the subtest-local counter (plan checks + skip-rest depend on it)
        std::string ind(4 * subtestDepth_, ' '); // one indent level per nesting depth
        std::cout << ind << (ok ? "ok " : "not ok ") << testNum_;
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
           p == "NEXT" || p == "LAST" || p == "QUIT" || p == "CLOSE";
}
void Interpreter::runNextPhasers(const std::vector<StmtPtr>& stmts, std::shared_ptr<Env>& scope) {
    // NEXT phasers run in REVERSE declaration order (like LEAVE)
    for (auto it = stmts.rbegin(); it != stmts.rend(); ++it) if ((*it)->kind == NK::Block) {
        auto* b = static_cast<Block*>(it->get());
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
    // A LEAVE/KEEP/UNDO phaser body runs to completion even though the block is
    // leaving via a cooperative return/next/last — those flags belong to the
    // OUTER control flow. Save and clear them around each phaser so execBlock's
    // "break on returning/loopCtl" doesn't truncate the phaser to one statement,
    // then restore so the outer transfer still happens.
    for (auto it = leaves.rbegin(); it != leaves.rend(); ++it) {
        bool savedRet = tctx_.returning; Value savedRV = tctx_.returnV;
        int savedLC = tctx_.loopCtl;
        tctx_.returning = false; tctx_.loopCtl = 0;
        auto sc = std::make_shared<Env>(); sc->parent = tctx_.cur;
        try { execBlock(*it, sc); } catch (...) {}
        tctx_.returning = savedRet; tctx_.returnV = std::move(savedRV); tctx_.loopCtl = savedLC;
    }
    // `temp`-saved containers are restored on scope exit (reverse order), after LEAVE blocks.
    if (tctx_.cur && !tctx_.cur->tempRestores.empty()) {
        for (auto it = tctx_.cur->tempRestores.rbegin(); it != tctx_.cur->tempRestores.rend(); ++it) (*it)();
        tctx_.cur->tempRestores.clear();
    }
}

Value Interpreter::execBlock(Block* b, std::shared_ptr<Env> scope, bool sink) {
    auto saved = tctx_.cur;
    tctx_.cur = std::move(scope);
    // A named sub hoisted into this block leaks the block env via a closure cycle
    // (see breakSelfClosures). Break it just before restoring tctx_.cur, while the
    // block env is still held by tctx_.cur (so the raw pointer is valid). Gated on
    // hasNestedSub → zero cost for the overwhelmingly common sub-free block.
    Env* blockEnv = tctx_.cur.get();
    bool hasNestedSub = false;
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
    hasNestedSub = hoistSubs(b->stmts);
    hoistExprDecls(b->stmts, blockEnv); // `my` buried in ternary/nqp branches → block scope
    runEnterPhasers(b->stmts);
    // Run the block's CATCH handler; returns true if `.resume` was called (so the
    // block should carry on after the throwing statement).
    // 0 = handled, 1 = .resume (carry on at the next statement), 2 = unmatched → rethrow
    auto runCatch = [&](RakuError& e) -> int {
        tctx_.cur->define("$_", exceptionFor(e));
        tctx_.cur->define("$!", exceptionFor(e));
        bool matched = false;
        try {
            struct G { int& d; G(int& x) : d(x) { d++; } ~G() { d--; } } g{catchDepth_};
            for (auto& s : catchBlk->stmts) exec(s.get());
        }
        catch (BreakGivenEx&) { matched = true; /* when/default matched */ }
        catch (ResumeEx&) { return 1; }
        // only a matching when/default HANDLES the exception — a CATCH body
        // without one runs (it can read $_/.message) but the exception rethrows
        // (Rakudo dies even under try in that shape)
        return matched ? 0 : 2;
    };
    try {
        for (size_t i = 0; i < b->stmts.size(); i++) {
            auto& s = b->stmts[i];
            if (s->kind == NK::Block && static_cast<Block*>(s.get())->isCatch) continue;
            if (isBlockPhaser(s.get())) continue; // ENTER/LEAVE handled at entry/exit
            if (s->kind == NK::SubDecl && !static_cast<SubDecl*>(s.get())->name.empty() &&
                !static_cast<SubDecl*>(s.get())->isMethod) { applySubTraits(static_cast<SubDecl*>(s.get())); continue; } // hoisted
            // sink every statement whose value is discarded: all but the block's
            // final statement, and even that one when the whole block is sink.
            if (catchBlk) {
                // per-statement, so `.resume` can continue at the next statement
                try { last = exec(s.get(), sink || i != lastIdx); }
                catch (RakuError& e) {
                    int r = runCatch(e);
                    if (r == 1) continue;                // .resume → next statement
                    runLeavePhasers(b->stmts);
                    if (tctx_.cur && !tctx_.cur->letRestores.empty()) {
                        for (auto it = tctx_.cur->letRestores.rbegin(); it != tctx_.cur->letRestores.rend(); ++it) (*it)();
                        tctx_.cur->letRestores.clear();
                    }
                    if (hasNestedSub) breakSelfClosures(blockEnv);
                    tctx_.cur = saved;
                    if (r == 2) throw;                   // R1: unmatched → rethrow
                    return Value::nil();                 // handled
                }
            } else {
                last = exec(s.get(), sink || i != lastIdx);
            }
            if (tctx_.returning || tctx_.loopCtl) break; // cooperative return/next/last unwinds native blocks
        }
    } catch (RakuError& e) {
        runLeavePhasers(b->stmts);
        // `let`-saved containers restore only on this UNSUCCESSFUL exit
        if (tctx_.cur && !tctx_.cur->letRestores.empty()) {
            for (auto it = tctx_.cur->letRestores.rbegin(); it != tctx_.cur->letRestores.rend(); ++it) (*it)();
            tctx_.cur->letRestores.clear();
        }
        if (hasNestedSub) breakSelfClosures(blockEnv);
        tctx_.cur = saved;
        throw;
    } catch (...) {
        runLeavePhasers(b->stmts);
        if (tctx_.cur && !tctx_.cur->letRestores.empty()) {
            for (auto it = tctx_.cur->letRestores.rbegin(); it != tctx_.cur->letRestores.rend(); ++it) (*it)();
            tctx_.cur->letRestores.clear();
        }
        if (hasNestedSub) breakSelfClosures(blockEnv);
        tctx_.cur = saved;
        throw;
    }
    runLeavePhasers(b->stmts);
    if (hasNestedSub) breakSelfClosures(blockEnv);
    tctx_.cur = saved;
    return last;
}

bool Interpreter::runLoopBody(Block* body, std::shared_ptr<Env> scope, const std::string& label,
                             bool isFirst, bool isLast, ValueList* collect,
                             const std::function<void()>& rebind) {
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
    // `return` inside a NEXT/LAST phaser WITH an enclosing routine returns from
    // it (`sub f { for 1,2 { LAST return $_ } }`); with NO routine it is
    // X::ControlFlow::Return (like Rakudo) — never the raw unwind that used to
    // reach std::terminate at the top level
    // Generic (not std::function) so wrapping a phaser runner is a plain inlined
    // call — a std::function here constructed + heap-allocated a closure over
    // (body, scope) on EVERY iteration, which cost ~25% on tight loops.
    auto noReturn = [&](auto&& fn) {
        try { fn(); }
        catch (ReturnEx&) {
            if (tctx_.curRoutineFrame != 0) throw; // the enclosing routine consumes it
            throw RakuError{Value::typeObj("X::ControlFlow::Return"),
                            "Attempt to return outside of any Routine"};
        }
        if (tctx_.returning && tctx_.curRoutineFrame == 0) {
            tctx_.returning = false;
            throw RakuError{Value::typeObj("X::ControlFlow::Return"),
                            "Attempt to return outside of any Routine"};
        }
    };
    auto runNextP = [&]() { noReturn([&]{ runNextPhasers(body->stmts, scope); }); };
    auto runLast = [&]() { if (isLast) noReturn([&]{ inScope(&Interpreter::runLastPhasers); }); }; // LAST {…}: once, after the last
    // this loop is now the innermost native loop for cooperative next/last/redo
    uint64_t savedLoopFrame = tctx_.curLoopFrame;
    tctx_.curLoopFrame = tctx_.frameTop;
    struct LoopGuard {
        ExecContext& t; uint64_t lf;
        ~LoopGuard() { t.curLoopFrame = lf; }
    } lguard{tctx_, savedLoopFrame};
    for (;;) {
        try { Value v = execBlock(body, scope, /*sink=*/collect == nullptr);
              if (tctx_.returning) { suppressLoopFirst_ = savedSF; return false; } // cooperative return: stop looping
              if (tctx_.loopCtl) { // cooperative next/last/redo from this loop's body
                  int ctl = tctx_.loopCtl; tctx_.loopCtl = 0;
                  if (ctl == 3) { if (rebind) rebind(); continue; } // redo: rerun the body (fresh `is copy` params)
                  if (ctl == 1) { try { runNextP(); } catch (LastEx&) { runLast(); suppressLoopFirst_ = savedSF; return false; } runLast(); suppressLoopFirst_ = savedSF; return true; }
                  runLast(); suppressLoopFirst_ = savedSF; return false; // last
              }
              if (collect) collect->push_back(v); try { runNextP(); } catch (LastEx&) { runLast(); suppressLoopFirst_ = savedSF; return false; } runLast(); suppressLoopFirst_ = savedSF; return true; }
        catch (LeaveEx&) { runLast(); suppressLoopFirst_ = savedSF; return true; } // leave: end this iteration, NO NEXT phasers
        catch (RedoEx& e) { if (!e.label.empty() && e.label != label) { suppressLoopFirst_ = savedSF; throw; } if (rebind) rebind(); continue; }
        catch (NextEx& e) { if (!e.label.empty() && e.label != label) { suppressLoopFirst_ = savedSF; throw; } try { runNextP(); } catch (LastEx&) { runLast(); suppressLoopFirst_ = savedSF; return false; } runLast(); suppressLoopFirst_ = savedSF; return true; }
        catch (BreakGivenEx&) { suppressLoopFirst_ = savedSF; return true; }
        catch (LastEx& e) { if (!e.label.empty() && e.label != label) { suppressLoopFirst_ = savedSF; throw; } runLast(); suppressLoopFirst_ = savedSF; return false; }
        catch (...) { suppressLoopFirst_ = savedSF; throw; }
    }
}

// Is `n` a built-in type name that a class may legitimately derive from?
// (Used to decide whether `class X is Y` with an unregistered Y is an error.)
bool isKnownTypeName(const std::string& n) {
    if (n.empty()) return false;
    if (n.rfind("X::", 0) == 0) return true;         // exception types
    if (n.rfind("Metamodel::", 0) == 0) return true; // HOWs
    if (n.rfind("IO::", 0) == 0) return true;         // IO::Path::Unix, etc.
    static const std::set<std::string> t = {
        "Mu", "Any", "Cool", "Junction", "Whatever", "WhateverCode", "Nil",
        "Int", "UInt", "Num", "Rat", "FatRat", "Complex", "Numeric", "Real", "Bool",
        "Str", "Stringy", "Uni", "Blob", "Buf", "Stringy",
        "blob8", "buf8", "blob16", "buf16", "blob32", "buf32", "blob64", "buf64",
        "utf8", "utf16", "utf32",
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
            // Rakudo sink semantics: a discarded FRESH object with a user-defined
            // `sink` method has it invoked (HTTP::Status registers each instance in
            // a table via `method sink { @codes[$!code] = self }`, created as bare
            // `HTTP::Status.new: …` statements). Restricted to a method-call result:
            // a value read back through a variable or an `is rw` routine is a
            // container, and MoarVM does not descend a container to sink its
            // contents (S04-statements/sink.t). The `VT::Object` test short-circuits
            // for the common non-object result, so only sunk objects pay the probe.
            if (sink && e->kind == NK::MethodCall &&
                r.t == VT::Object && r.obj && r.obj->cls &&
                r.obj->cls->findMethod("sink"))
                methodCall(r, "sink", ValueList{});
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
            // the `&name` form: a Callable running the regex against its argument
            // (unanchored), so `'port = 443' ~~ &pair` and &pair($str) work
            std::string pat = nr->pattern;
            Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
            code.code->name = nr->name;
            code.code->builtin = [pat](Interpreter& I, ValueList& a) -> Value {
                return a.empty() ? Value::nil() : I.regexMatch(a[0].toStr(), pat);
            };
            tctx_.cur->define("&" + nr->name, code);
            return Value::any();
        }
        case NK::SubsetDecl: {
            auto* sd = static_cast<SubsetDecl*>(s);
            if (!sd->name.empty()) subsets_[sd->name] = SubsetInfo{sd->baseType, sd->where.get()};
            return Value::any();
        }
        case NK::UseStmt: {
            auto* u = static_cast<UseStmt*>(s);
            if (u->isNo && u->module == "strict") { noStrict_ = true; return Value::any(); }
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
            else if (!u->module.empty()) loadModule(u->module, u->importArgs, !u->isNeed);
            return Value::any();
        }
        case NK::Block: {
            auto* b = static_cast<Block*>(s);
            // a statement-level `{}` with no statements is Rakudo's empty-hash
            // composer, not a block (EVAL('{}') is {}, not Any)
            if (b->stmts.empty() && b->phaser.empty() && !b->isCatch)
                return Value::makeHash();
            // statement-form phaser (`INIT my $x = …`): the declaration belongs
            // to the ENCLOSING scope — run without a child env
            if (b->stmtForm && !b->phaser.empty())
                return execBlock(b, tctx_.cur, sink);
            auto scope = std::make_shared<Env>();
            scope->parent = tctx_.cur;
            return execBlock(b, scope, sink); // a sunk bare block sinks its final value too
        }
        case NK::SubDecl: {
            auto* sd = static_cast<SubDecl*>(s);
            auto makeCand = [&](const std::vector<Param>* prms) {
                Value c; c.t = VT::Code; c.code = std::make_shared<Callable>();
                c.code->name = sd->name;
                c.code->pkg = tctx_.pkgPrefix.empty() ? "GLOBAL"
                            : tctx_.pkgPrefix.substr(0, tctx_.pkgPrefix.size() - 2); // strip trailing ::
                c.code->params = prms;
                c.code->body = &sd->body;
                c.code->closure = tctx_.cur;
                c.code->retType = sd->retType;
                c.code->pod = sd->pod;
                {   // `sub f { @_ }` — an @_/%_ reference implies a slurpy signature
                    std::set<std::string> ph2;
                    for (auto& s2 : sd->body) collectPHStmt(s2.get(), ph2);
                    c.code->usesArgs = ph2.count("@_") || ph2.count("%_");
                    c.code->hadSig = sd->hadSig;
                }
                // Only a PURE `{*}` proto (a bare redispatcher) is excluded from
                // dispatch; a proto with a real body (or an operator proto with no
                // multis) stays a callable candidate.
                c.code->isProto = sd->isProto && sd->body.size() == 1 &&
                    sd->body[0]->kind == NK::ExprStmt &&
                    static_cast<ExprStmt*>(sd->body[0].get())->e &&
                    static_cast<ExprStmt*>(sd->body[0].get())->e->kind == NK::Whatever;
                if (sd->isNative) { c.code->isNative = true; c.code->nativeLib = sd->nativeLib;
                                    c.code->nativeLibSub = sd->nativeLibSub;
                                    c.code->nativeSym = sd->nativeSym.empty() ? sd->name : sd->nativeSym; }
                for (auto& p : *prms) {
                    // a default makes no sense on a slurpy or a required param
                    if (p.defaultVal && p.slurpy)
                        throwTyped("X::Parameter::Default", {{"how", "slurpy"}, {"parameter", p.name}},
                            "Cannot put default on slurpy parameter " + p.name);
                    if (p.defaultVal && p.required)
                        throwTyped("X::Parameter::Default", {{"how", "required"}, {"parameter", p.name}},
                            "Cannot put default on required parameter " + p.name);
                }
                // a `--> 42` / `--> "foo"` / `--> Nil` constraint forbids
                // `return <value>` anywhere in the body (compile error in Rakudo)
                if (sd->retLiteralPresent || sd->retType == "Nil") {
                    std::string repr = "Nil";
                    // the literal was appended as the body's LAST statement (parseSub);
                    // read it back from there for the error message
                    const Expr* rl = nullptr;
                    if (sd->retLiteralPresent && !sd->body.empty() &&
                        sd->body.back()->kind == NK::ExprStmt)
                        rl = static_cast<const ExprStmt*>(sd->body.back().get())->e.get();
                    if (rl) {
                        if (rl->kind == NK::IntLit)
                            repr = std::to_string(static_cast<const IntLit*>(rl)->v);
                        else if (rl->kind == NK::StrLit)
                            repr = "\"" + static_cast<const StrLit*>(rl)->v + "\"";
                        else if (rl->kind == NK::NumLit)
                            repr = Value::number(static_cast<const NumLit*>(rl)->v).gist();
                        else if (rl->kind == NK::InterpStr) { // `"foo"` lexes as an InterpStr
                            std::string flat; bool constStr = true;
                            for (auto& p : static_cast<const InterpStr*>(rl)->parts) {
                                if (p->kind == NK::StrLit) flat += static_cast<const StrLit*>(p.get())->v;
                                else { constStr = false; break; }
                            }
                            if (constStr) repr = "\"" + flat + "\"";
                        }
                        else if (rl->kind == NK::BoolLit)
                            repr = static_cast<const BoolLit*>(rl)->v ? "True" : "False";
                        else if (rl->kind == NK::NameTerm)
                            repr = static_cast<const NameTerm*>(rl)->name; // `--> True`/`--> Nil`
                    }
                    std::function<bool(const Stmt*)> hasRetVal = [&](const Stmt* st) -> bool {
                        if (!st) return false;
                        if (st->kind == NK::ReturnStmt)
                            return static_cast<const ReturnStmt*>(st)->value != nullptr;
                        // `42.return` — the method form of returning a value is
                        // forbidden by a literal `-->` constraint too
                        if (st->kind == NK::ExprStmt) {
                            const Expr* e2 = static_cast<const ExprStmt*>(st)->e.get();
                            if (e2 && e2->kind == NK::MethodCall &&
                                static_cast<const MethodCall*>(e2)->method == "return")
                                return true;
                        }
                        if (st->kind == NK::Block) {
                            for (auto& b : static_cast<const Block*>(st)->stmts)
                                if (hasRetVal(b.get())) return true;
                        }
                        else if (st->kind == NK::IfStmt) {
                            auto* is = static_cast<const IfStmt*>(st);
                            for (auto& br : is->branches) if (hasRetVal(br.second.get())) return true;
                            if (hasRetVal(is->elseBlock.get())) return true;
                        }
                        else if (st->kind == NK::WhileStmt)
                            return hasRetVal(static_cast<const WhileStmt*>(st)->body.get());
                        else if (st->kind == NK::ForStmt)
                            return hasRetVal(static_cast<const ForStmt*>(st)->body.get());
                        return false;
                    };
                    for (auto& st : sd->body)
                        if (hasRetVal(st.get()))
                            throwTyped("X::Comp",
                                {{"payload", repr}},
                                "No return arguments allowed when return value " +
                                repr + " is already specified in the signature");
                }
                if (prms->empty()) {
                    c.code->placeholders = computePlaceholders(sd->body);
                    // an explicit signature — even `()` — forbids placeholders
                    if (sd->hadSig && !c.code->placeholders.empty())
                        throwTyped("X::Signature::Placeholder",
                            {{"placeholder", c.code->placeholders[0]},
                             {"line", std::to_string(sd->line > 0 ? sd->line : 1)}},
                            "Placeholder variable '" + c.code->placeholders[0] +
                            "' cannot override existing signature");
                }
                return c;
            };
            Value code = makeCand(&sd->params);
            std::vector<Value> altCands;
            for (auto& ap : sd->altParams) altCands.push_back(makeCand(&ap));
            // dispatch non-built-in `is` traits to a user trait_mod:<is> multi:
            // `sub foo() is traced {…}` calls trait_mod:<is>($foo, :traced).
            // Skipped while hoisting — traits run at the declaration's textual
            // position (see the hoist-skip sites), after earlier `my`s initialized.
            if (!sd->traits.empty() && !hoistingSubs_) {
                if (Value* tm = tctx_.cur->find("&trait_mod:<is>")) {
                    if (tm->t == VT::Code) for (auto& st : sd->traits) {
                        Value arg = st.arg ? eval(st.arg.get()) : Value::boolean(true);
                        Value p = Value::pair(st.name, arg); p.namedArg = true;
                        ValueList ta; ta.push_back(code); ta.push_back(p);
                        try { callCallable(*tm, ta); }
                        catch (RakuError&) {} // no matching candidate: not this handler's trait
                    }
                }
            }
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
                // and publish the fully-qualified name (Foo::Bar::name) so callers
                // outside the module can reach an unexported `our sub` — the only
                // way OpenSSL::Version::version_num etc. are invoked.
                if (sd->isOur && !tctx_.pkgPrefix.empty())
                    global_->define("&" + tctx_.pkgPrefix + sd->name, code);
            }
            if (sd->immediateCall) { // `sub f(...) {...}(args)` — call right away
                ValueList ia;
                for (auto& a : sd->immediateArgs) ia.push_back(eval(a.get()));
                return callCallable(code, ia);
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
                code.code->pod = md->pod;
                    code.code->pod = md->pod;
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
                // resolve package-relative short names: `augment class B` inside
                // `augment class A { … }` finds the nested A::B via prefix/alias
                auto existing = classes_.find(cd->name);
                if (existing == classes_.end() && !tctx_.pkgPrefix.empty())
                    existing = classes_.find(tctx_.pkgPrefix + cd->name);
                if (existing == classes_.end())
                    existing = classes_.find(resolveClassAlias(cd->name));
                if (existing != classes_.end()) {
                    // augment a user-declared type — merge into its ClassInfo
                    ClassInfo* ci = existing->second.get();
                    for (auto& md : cd->methods) addTo(ci->methods, md.get());
                    for (auto& a : cd->attrs) {
                        ClassAttr ca; ca.name = a.name; ca.sigil = a.sigil;
                        ca.pub = a.pub; ca.rw = a.rw; ca.def = a.def.get(); ca.type = a.type;
                        ca.containerIs = a.containerIs;
                        ci->attrs.push_back(ca);
                    }
                    for (auto& r : cd->rules) { ci->rules[r.name] = r.pattern; ci->ruleKind[r.name] = r.kind; ci->ruleOrder.push_back(r.name); }
                    noteSymbolMutation("augment (user type)");
                } else {
                    // augment a built-in type — park methods in the extension
                    // table; a name that is neither a user type nor a known
                    // built-in has nothing to augment
                    if (!isKnownTypeName(cd->name))
                        throwTyped("X::Augment::NoSuchType",
                                   {{"package-kind", "class"}, {"package", cd->name}},
                                   "You tried to augment class " + cd->name +
                                   ", but it does not exist");
                    for (auto& md : cd->methods) addTo(builtinExt_[cd->name], md.get());
                    noteSymbolMutation("augment (built-in type)");
                }
                { // nested decls, if any — under this package's prefix so an inner
                  // `augment class B` resolves the nested A::B
                    std::string savedPrefix = tctx_.pkgPrefix;
                    tctx_.pkgPrefix = cd->name + "::";
                    try { for (auto& st : cd->body) exec(st.get()); }
                    catch (...) { tctx_.pkgPrefix = savedPrefix; throw; }
                    tctx_.pkgPrefix = savedPrefix;
                }
                return Value::typeObj(cd->name);
            }
            if (cd->isPackage) {
                // file-scoped `unit module Foo;` (empty body): register the name and
                // set the package prefix so the rest of the file's `our sub`s / `our`
                // vars publish under qualified names (Foo::name). The prefix persists
                // to end-of-compunit; loadModule save/restores it so it can't leak.
                if (cd->body.empty()) {
                    if (!cd->name.empty()) {
                        tctx_.cur->define(cd->name, Value::typeObj(cd->name));
                        tctx_.pkgPrefix += cd->name + "::";
                        if (curPkgEnv_ == global_) curPkgEnv_ = tctx_.cur; // `our` installs here
                    }
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
                hoistSubs(cd->body); // forward refs: Cro::HTTP::Router calls router-plugin-register long before its definition
                // classes/roles register FIRST (Rakudo declares types at compile
                // time): `our $p = router-plugin-register('link')` at the top of
                // Cro::HTTP::Router news a PluginKey declared 1400 lines later
                for (auto& st : cd->body)
                    if (st->kind == NK::ClassDecl && !static_cast<ClassDecl*>(st.get())->isAugment)
                        exec(st.get());
                for (auto& st : cd->body)
                    if (!(st->kind == NK::ClassDecl && !static_cast<ClassDecl*>(st.get())->isAugment))
                        exec(st.get());
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
                // During a `use` load, `is export` subs of a braced module also
                // surface BARE to the importer (same-file code still sees only
                // the qualified names, like Rakudo).
                if (loadingModuleDepth_ > 0 && moduleDoImport_) {
                    std::function<void(const std::vector<StmtPtr>&)> surface =
                        [&](const std::vector<StmtPtr>& body) {
                        for (auto& st : body) {
                            if (st->kind == NK::SubDecl) {
                                auto* sd = static_cast<SubDecl*>(st.get());
                                if (sd->isExport && !sd->name.empty()) {
                                    auto it = pkgEnv->vars.find("&" + sd->name);
                                    if (it != pkgEnv->vars.end())
                                        tctx_.cur->define(it->first, it->second);
                                }
                            }
                        }
                    };
                    surface(cd->body);
                }
                tctx_.pkgPrefix = savedPrefix;
                return Value::any();
            }
            auto ci = std::make_shared<ClassInfo>();
            // anonymous `role {…}` / `class {…}` literals get a synthesized name so
            // they can be registered, mixed in (`does`/`but`), and introspected.
            // an unqualified class nested in a class/package body registers under
            // its QUALIFIED name (`class GenericActions` inside `class Cro::Uri`
            // is Cro::Uri::GenericActions, as in Rakudo); the tail alias keeps the
            // short name resolvable
            std::string clsName = cd->name.empty()
                ? "<anon|" + std::to_string(++anonTypeCounter_) + ">"
                : (!tctx_.pkgPrefix.empty() && cd->name.find("::") == std::string::npos
                    ? tctx_.pkgPrefix + cd->name : cd->name);
            ci->name = clsName;
            ci->pod = cd->pod;
            if (!cd->parent.empty()) {
                // a type may not inherit from / compose itself:  class A is A / role A does A
                if (cd->parent == cd->name && !cd->name.empty())
                    throwTyped(cd->isRole ? "X::InvalidType" : "X::Inheritance::SelfInherit",
                        {{"name", cd->name}},
                        std::string(cd->isRole ? "Role" : "Class") + " '" + cd->name + "' cannot inherit from / compose itself");
                auto it = classes_.find(cd->parent);
                if (it == classes_.end() && !tctx_.pkgPrefix.empty())
                    it = classes_.find(tctx_.pkgPrefix + cd->parent); // sibling nested type
                if (it == classes_.end()) it = classes_.find(resolveClassAlias(cd->parent));
                // `does X` where X is a class or a concrete core type: only
                // roles compose (built-in role names like Positional stay fine)
                if (cd->parentIsDoes) {
                    static const std::set<std::string> kConcreteTy = {
                        "Int", "Str", "Num", "Rat", "Bool", "Complex", "Array", "Hash"};
                    if ((it != classes_.end() && !it->second->isRole) ||
                        (it == classes_.end() && kConcreteTy.count(cd->parent)))
                        throwTypedV("X::Composition::NotComposable",
                            {{"target-name", Value::str(clsName)},
                             {"composer", Value::typeObj(cd->parent)}},
                            cd->parent + " is not composable, so " + clsName +
                            " cannot compose it");
                }
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
                if (it == classes_.end() && !tctx_.pkgPrefix.empty())
                    it = classes_.find(tctx_.pkgPrefix + pn);
                if (it == classes_.end()) it = classes_.find(resolveClassAlias(pn));
                if (it != classes_.end()) ci->extraParents.push_back(it->second);
                else if (!isKnownTypeName(pn))
                    throw RakuError{Value::typeObj("X::Inheritance::UnknownParent"),
                        "Class '" + cd->name + "' cannot inherit from '" + pn + "' because it is unknown"};
            }
            // -------- role composition helpers --------
            // a stub body is a bare `...` / `!!!` — in a role it declares a
            // requirement the composing class must fulfil
            auto sigKeyParams = [](const std::vector<Param>* ps) {
                std::string k;
                if (ps) for (auto& p : *ps) {
                    if (p.named || p.slurpy) continue;
                    k += (p.type.empty() ? "Any" : p.type) + ",";
                }
                return k;
            };
            auto codeIsStub = [](const Value& v) -> bool {
                if (v.t != VT::Code || !v.code) return false;
                if (v.code->isMultiDispatcher) {
                    if (v.code->candidates.empty()) return true;
                    for (auto& c : v.code->candidates) if (!(c.code && c.code->isStub)) return false;
                    return true;
                }
                return v.code->isStub;
            };
            auto cloneDispatcher = [](const Value& v) {
                Value nv = v;
                auto fresh = std::make_shared<Callable>();
                fresh->pkg = v.code->pkg; fresh->name = v.code->name;
                fresh->isMultiDispatcher = true; fresh->isProto = v.code->isProto;
                fresh->isMethod = v.code->isMethod;
                fresh->candidates = v.code->candidates;
                nv.code = fresh;
                return nv;
            };
            // every role this type composes (the first `does` lands as parent)
            std::vector<ClassInfo*> composedRoles;
            if (ci->parent && ci->parent->isRole) composedRoles.push_back(ci->parent.get());
            for (auto& p : ci->extraParents) if (p && p->isRole) composedRoles.push_back(p.get());
            for (auto& rn : cd->roles) {
                auto it = classes_.find(rn);
                if (it == classes_.end() && !tctx_.pkgPrefix.empty())
                    it = classes_.find(tctx_.pkgPrefix + rn);
                if (it == classes_.end()) it = classes_.find(resolveClassAlias(rn));
                if (it == classes_.end() && !tctx_.pkgPrefix.empty())
                    it = classes_.find(tctx_.pkgPrefix + rn); // `does Handler` where the role is a sibling nested type
                if (it == classes_.end()) it = classes_.find(resolveClassAlias(rn));
                if (it != classes_.end() && it->second->isRole) composedRoles.push_back(it->second.get());
            }
            // conflict detection: the same method (same signature for multis)
            // provided — as a real implementation — by two different roles must be
            // resolved by the class's own method (diamonds share the Callable, so
            // pointer identity exempts them)
            std::map<std::string, std::map<std::string, std::set<const Callable*>>> provided;
            std::map<std::string, std::set<std::string>> providerRoles;
            for (ClassInfo* role : composedRoles)
                for (auto& kv : role->methods) {
                    if (kv.second.t != VT::Code || !kv.second.code) continue;
                    if (kv.second.code->isMultiDispatcher) {
                        for (auto& c : kv.second.code->candidates)
                            if (c.code && !c.code->isStub) { provided[kv.first][sigKeyParams(c.code->params)].insert(c.code.get()); providerRoles[kv.first].insert(role->name); }
                    }
                    else if (!kv.second.code->isStub) { provided[kv.first][""].insert(kv.second.code.get()); providerRoles[kv.first].insert(role->name); }
                }
            std::set<std::string> conflicted;
            for (auto& kv : provided) for (auto& sk : kv.second) if (sk.second.size() > 1) conflicted.insert(kv.first);
            // compose additional `does Role`s: flatten their methods/attrs in (the
            // class's own methods, registered below, override by key). Multi
            // dispatchers are CLONED (never share the role's own dispatcher) and
            // merged per candidate; an implementation displaces a same-signature
            // stub, never the other way around.
            for (auto& rn : cd->roles) {
                auto it = classes_.find(rn);
                {   // only a role (or an unknown/built-in role name) composes;
                    // a class or concrete core type is X::Composition::NotComposable
                    static const std::set<std::string> kConcrete = {
                        "Int", "Str", "Num", "Rat", "Bool", "Complex", "Array", "Hash"};
                    bool bad = (it != classes_.end() && !it->second->isRole) ||
                               (it == classes_.end() && kConcrete.count(rn));
                    if (bad)
                        throwTypedV("X::Composition::NotComposable",
                            {{"target-name", Value::str(cd->name)},
                             {"composer", Value::typeObj(rn)}},
                            rn + " is not composable, so " + cd->name +
                            " cannot compose it");
                }
                ci->doneRoles.insert(rn); // record membership (for ~~ Role / .does), even if unknown
                if (it == classes_.end()) continue;
                for (auto& kv : it->second->methods) {
                    bool newDisp = kv.second.t == VT::Code && kv.second.code && kv.second.code->isMultiDispatcher;
                    auto ex = ci->methods.find(kv.first);
                    if (ex == ci->methods.end()) {
                        ci->methods[kv.first] = newDisp ? cloneDispatcher(kv.second) : kv.second;
                        continue;
                    }
                    Value& e = ex->second;
                    if (e.code == kv.second.code) continue; // identical method (diamond)
                    bool exDisp = e.t == VT::Code && e.code && e.code->isMultiDispatcher;
                    if (exDisp && newDisp) {
                        for (auto& c : kv.second.code->candidates) {
                            std::string sk = sigKeyParams(c.code ? c.code->params : nullptr);
                            bool cStub = c.code && c.code->isStub;
                            bool placed = false;
                            for (auto& e2 : e.code->candidates) {
                                if (sigKeyParams(e2.code ? e2.code->params : nullptr) != sk) continue;
                                bool eStub = e2.code && e2.code->isStub;
                                if (eStub && !cStub) e2 = c; // implementation replaces stub
                                placed = true; break;
                            }
                            if (!placed) e.code->candidates.push_back(c);
                        }
                        continue;
                    }
                    bool eStub = codeIsStub(e), nStub = codeIsStub(kv.second);
                    if (nStub) continue;                    // a stub never displaces
                    if (eStub) ci->methods[kv.first] = newDisp ? cloneDispatcher(kv.second) : kv.second;
                    // both real implementations: recorded in `conflicted` above
                }
                for (auto& a : it->second->attrs) {
                    bool dup = false;
                    for (auto& ex : ci->attrs)
                        if (ex.name == a.name && ex.sigil == a.sigil) {
                            // the same declaration arriving twice (diamond) is fine;
                            // two distinct same-name declarations conflict in a class
                            if (!(ex.declId && a.declId && ex.declId == a.declId) && !cd->isRole)
                                throw RakuError{Value::typeObj("X::Role::Attribute::Conflicts"),
                                    "Attribute '" + std::string(1, a.sigil) + "!" + a.name +
                                    "' conflicts in " + std::string(cd->isRole ? "role" : "class") +
                                    " '" + clsName + "' composition: declared in both '" + rn + "' and another role"};
                            dup = true; break;
                        }
                    if (!dup) ci->attrs.push_back(a);
                }
                for (auto& sub : it->second->doneRoles) ci->doneRoles.insert(sub); // role-of-role
            }
            // a role used as a parent (`class C does R` where R lands as parent) also counts
            if (ci->parent && ci->parent->isRole) ci->doneRoles.insert(ci->parent->name);
            for (auto& p : ci->extraParents) if (p && p->isRole) ci->doneRoles.insert(p->name);
            ci->isGrammar = cd->isGrammar;
            ci->isRole = cd->isRole;
            ci->repr = cd->repr;
            ci->ver = cd->ver; ci->auth = cd->auth; ci->api = cd->api;
            // the class/role BODY scope: body lexicals (`my $lex = ...`) live here,
            // and methods/attr-defaults close over it
            auto bodyEnv = std::make_shared<Env>();
            bodyEnv->parent = tctx_.cur;
            ci->declEnv = bodyEnv; // capture the declaration scope (attr-default closures)
            {   // a class body takes no arguments — placeholders are compile errors
                std::string ph = firstBlockPlaceholder(cd->body);
                if (!ph.empty())
                    throwTyped("X::Placeholder::Block", {{"placeholder", ph}},
                        "Placeholder variable '" + ph +
                        "' may not be used here because the surrounding block does not take a signature");
            }
            for (auto& r : cd->rules) { ci->rules[r.name] = r.pattern; ci->ruleKind[r.name] = r.kind; if (!r.params.empty()) ci->ruleParams[r.name] = r.params; ci->ruleOrder.push_back(r.name); }
            for (auto& a : cd->attrs) {
                // a placeholder in an attribute default has no block to bind to
                if (a.def) {
                    std::set<std::string> ph;
                    std::vector<StmtPtr> tmp; // reuse the stmt walker via a shim below
                    collectPHExprPublic(a.def.get(), ph);
                    for (auto& n : ph)
                        if (n[1] == '^' || n[1] == ':')
                            throwTyped("X::Placeholder::Attribute", {{"placeholder", n}},
                                "Placeholder variable '" + n + "' may not be used in an attribute default");
                }
                // a class may not redeclare an attribute a composed role declares
                if (!cd->isRole)
                    for (ClassInfo* role : composedRoles)
                        for (auto& ra : role->attrs)
                            if (ra.name == a.name && ra.sigil == a.sigil)
                                throw RakuError{Value::typeObj("X::Role::Attribute::Conflicts"),
                                    "Attribute '" + std::string(1, a.sigil) + "!" + a.name +
                                    "' conflicts in class '" + clsName +
                                    "' composition: also declared in role '" + role->name + "'"};
                ClassAttr ca; ca.name = a.name; ca.sigil = a.sigil; ca.pub = a.pub; ca.rw = a.rw; ca.type = a.type;
                ca.containerIs = a.containerIs;
                ca.handles = a.handles;
                ca.def = a.def.get();
                ca.declId = &a;
                ci->attrs.push_back(ca);
            }
            auto stmtIsStub = [](const std::vector<StmtPtr>& body) -> bool {
                if (body.size() != 1 || body[0]->kind != NK::ExprStmt) return false;
                Expr* e = static_cast<ExprStmt*>(body[0].get())->e.get();
                if (!e || e->kind != NK::Call) return false;
                auto* c = static_cast<Call*>(e);
                return (c->name == "..." || c->name == "!!!") && c->args.empty() && !c->callee;
            };
            std::set<const void*> ownParams; // this declaration's own method signatures
            for (auto& md : cd->methods) ownParams.insert(&md->params);
            for (auto& md : cd->methods) {
                Value code; code.t = VT::Code;
                code.code = std::make_shared<Callable>();
                code.code->name = md->name;
                code.code->params = &md->params;
                code.code->retType = md->retType;
                code.code->body = &md->body;
                code.code->closure = bodyEnv;
                code.code->isMethod = true; // invoked via .() binds the 1st arg as self
                code.code->isStub = stmtIsStub(md->body);
                // an undeclared `$!attr` reference in a method body is a compile
                // error in a CLASS (roles get their attrs from consumers)
                if (!cd->isRole)
                    for (auto& ar : collectAttrRefs(md->body)) {
                        std::string bare = ar.substr(2);
                        bool known = false;
                        for (ClassInfo* cc = ci.get(); cc && !known; cc = cc->parent.get())
                            for (auto& at : cc->attrs)
                                if (at.name == bare) { known = true; break; }
                        if (!known)
                            // filename+line mark it a compile-time (X::Comp) error —
                            // the top-level printer adds the ===SORRY!=== banner
                            throwTyped("X::Attribute::Undeclared",
                                {{"symbol", ar}, {"package-name", clsName},
                                 {"package-kind", cd->isGrammar ? "grammar" : "class"},
                                 {"what", "attribute"},
                                 {"line", std::to_string(md->line)},
                                 {"filename", srcFileAbs_.empty() ? srcFile_ : srcFileAbs_}},
                                "Attribute " + ar + " not declared in " +
                                (cd->isGrammar ? "grammar " : "class ") + clsName);
                    }
                if (md->params.empty()) code.code->placeholders = computePlaceholders(md->body);
                if (md->isMulti) {
                    auto it = ci->methods.find(md->name);
                    if (it != ci->methods.end() && it->second.code && it->second.code->isMultiDispatcher) {
                        // an own candidate overrides a same-signature candidate
                        // composed from a role (never another own candidate)
                        auto& cands = it->second.code->candidates;
                        std::string sk = sigKeyParams(code.code->params);
                        cands.erase(std::remove_if(cands.begin(), cands.end(), [&](const Value& c){
                            return c.code && !ownParams.count(c.code->params) &&
                                   sigKeyParams(c.code->params) == sk; }), cands.end());
                        cands.push_back(code);
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
            // aggregate role requirements (composed roles already carry the ones
            // they inherited from roles they compose, so this is transitive) and
            // record this role's own stub methods as requirements
            std::map<std::string, std::set<std::string>> reqFrom; // req name -> role names (for the message)
            for (ClassInfo* role : composedRoles) {
                for (const std::string& rq : role->requiredMethods) { ci->requiredMethods.insert(rq); reqFrom[rq].insert(role->name); }
                for (auto& kv : role->requiredMultiSigs) {
                    auto& dst = ci->requiredMultiSigs[kv.first];
                    for (auto& s : kv.second) if (std::find(dst.begin(), dst.end(), s) == dst.end()) dst.push_back(s);
                    reqFrom[kv.first].insert(role->name);
                }
            }
            if (cd->isRole)
                for (auto& md : cd->methods)
                    if (stmtIsStub(md->body)) {
                        ci->requiredMethods.insert(md->name);
                        if (md->isMulti) ci->requiredMultiSigs[md->name].push_back(sigKeyParams(&md->params));
                    }
            // role composition check: a non-role class that composes a role must
            // implement every method the role requires — via its own methods, a
            // composed/inherited implementation, a public attribute's accessor,
            // or an attribute `handles` delegation. Roles compose freely.
            if (!cd->isRole) {
                std::set<std::string> classOwn;
                for (auto& md : cd->methods) classOwn.insert(md->name);
                // conflicts first: two roles provided the same real implementation
                for (const std::string& cn : conflicted)
                    if (!classOwn.count(cn)) {
                        std::string rl; for (auto& r : providerRoles[cn]) { if (!rl.empty()) rl += ", "; rl += r; }
                        throw RakuError{Value::typeObj("X::Role::Unresolved::Method"),
                            "Method '" + cn + "' must be resolved by class " + clsName +
                            " because it exists in multiple roles (" + rl + ")"};
                    }
                // a real (non-stub) implementation anywhere in the effective type
                std::function<bool(ClassInfo*, const std::string&, const std::string*)> hasImpl =
                    [&](ClassInfo* c, const std::string& n, const std::string* sig) -> bool {
                    if (!c) return false;
                    auto mit = c->methods.find(n);
                    if (mit != c->methods.end() && mit->second.t == VT::Code && mit->second.code) {
                        if (mit->second.code->isMultiDispatcher) {
                            for (auto& cand : mit->second.code->candidates)
                                if (cand.code && !cand.code->isStub &&
                                    (!sig || sigKeyParams(cand.code->params) == *sig)) return true;
                        }
                        else if (!mit->second.code->isStub) return true; // a plain method covers any signature
                    }
                    if (hasImpl(c->parent.get(), n, sig)) return true;
                    for (auto& p : c->extraParents) if (p && hasImpl(p.get(), n, sig)) return true;
                    return false;
                };
                std::function<bool(ClassInfo*, const std::string&)> attrCovers =
                    [&](ClassInfo* c, const std::string& n) -> bool {
                    if (!c) return false;
                    for (auto& a : c->attrs) {
                        if (a.pub && a.name == n) return true; // accessor counts as the method
                        for (auto& h : a.handles) if (h == n) return true;
                    }
                    if (attrCovers(c->parent.get(), n)) return true;
                    for (auto& p : c->extraParents) if (p && attrCovers(p.get(), n)) return true;
                    return false;
                };
                for (const std::string& rq : ci->requiredMethods) {
                    bool ok;
                    auto sigsIt = ci->requiredMultiSigs.find(rq);
                    if (sigsIt != ci->requiredMultiSigs.end() && !sigsIt->second.empty()) {
                        ok = true;
                        for (auto& s : sigsIt->second) if (!hasImpl(ci.get(), rq, &s)) { ok = false; break; }
                    }
                    else ok = hasImpl(ci.get(), rq, nullptr) ||
                              classOwn.count(rq) || // an own stub is a deliberate promise
                              attrCovers(ci.get(), rq);
                    if (!ok && attrCovers(ci.get(), rq)) ok = true;
                    if (!ok) {
                        std::string rl; for (auto& r : reqFrom[rq]) { if (!rl.empty()) rl += ", "; rl += r; }
                        throw RakuError{Value::typeObj("X::Comp::AdHoc"),
                            "Method '" + rq + "' must be implemented by " + clsName +
                            " because it is required by roles: " + (rl.empty() ? "?" : rl) + "."};
                    }
                }
                // requirements verified: drop role-composed stubs so calls reach a
                // parent implementation / accessor instead of executing the stub
                // (a stub the class itself declares stays callable-and-dies)
                for (auto mit = ci->methods.begin(); mit != ci->methods.end(); ) {
                    Value& mv = mit->second; bool drop = false;
                    if (mv.t == VT::Code && mv.code) {
                        if (mv.code->isMultiDispatcher) {
                            auto& cs = mv.code->candidates;
                            cs.erase(std::remove_if(cs.begin(), cs.end(), [&](const Value& c){
                                return c.code && c.code->isStub && !ownParams.count(c.code->params); }), cs.end());
                            drop = cs.empty();
                        }
                        else if (mv.code->isStub && !ownParams.count(mv.code->params)) drop = true;
                    }
                    if (drop) mit = ci->methods.erase(mit); else ++mit;
                }
            }
            noteSymbolMutation("class/role/grammar declaration");
            classes_[clsName] = ci;
            // a qualified name also answers to its TAIL where no real class claims
            // it: `use URI::Path` inside `unit class URI` lets bare `Path` resolve
            // (Rakudo finds it in the URI:: package stash; we alias globally)
            if (size_t sep = clsName.rfind("::"); sep != std::string::npos) {
                std::string tail = clsName.substr(sep + 2);
                // never shadow a BUILT-IN type: `class X::Roast::Channel` must not
                // make bare `Channel` mean the exception class
                if (!tail.empty() && !isKnownTypeName(tail) &&
                    !classes_.count(tail) && !classAliases_.count(tail))
                    classAliases_[tail] = clsName;
            }
            if (!cd->name.empty()) tctx_.cur->define(cd->name, Value::typeObj(clsName));
            // run the body statements in the body scope: nested classes/enums and
            // static subs register; `my` lexicals land where the methods see them.
            // The package prefix covers the body so a nested `class GenericActions`
            // registers as Cro::Uri::GenericActions (Rakudo nesting semantics).
            {
                auto saved = tctx_.cur;
                std::string savedPrefix = tctx_.pkgPrefix;
                tctx_.pkgPrefix = clsName + "::";
                tctx_.cur = bodyEnv;
                hoistSubs(cd->body); // class-body subs visible to earlier body statements too
                try { for (auto& st : cd->body) exec(st.get()); }
                catch (...) { tctx_.cur = saved; tctx_.pkgPrefix = savedPrefix; throw; }
                tctx_.cur = saved;
                tctx_.pkgPrefix = savedPrefix;
            }
            // evaluate to the type object, so `my class Foo {…}` / anon `role {…}` work as expressions
            return Value::typeObj(clsName);
        }
        case NK::ReturnStmt: {
            auto* r = static_cast<ReturnStmt*>(s);
            Value v = r->value ? eval(r->value.get()) : Value::any();
            // cooperative return when no callable boundary sits between here and
            // the routine (native loops/blocks only) — else the exception path
            if (tctx_.curRoutineFrame != 0 && tctx_.frameTop == tctx_.curRoutineFrame) {
                tctx_.returning = true; tctx_.returnV = std::move(v);
                return Value::any();
            }
            throw ReturnEx{v};
        }
        case NK::LastStmt: {
            const std::string& t = static_cast<LastStmt*>(s)->target;
            if (t.empty() && tctx_.curLoopFrame != 0 && tctx_.frameTop == tctx_.curLoopFrame) {
                tctx_.loopCtl = 2; return Value::any(); // cooperative last
            }
            throw LastEx{t};
        }
        case NK::NextStmt: {
            const std::string& t = static_cast<NextStmt*>(s)->target;
            if (t.empty() && tctx_.curLoopFrame != 0 && tctx_.frameTop == tctx_.curLoopFrame) {
                tctx_.loopCtl = 1; return Value::any(); // cooperative next
            }
            throw NextEx{t};
        }
        case NK::RedoStmt: {
            const std::string& t = static_cast<RedoStmt*>(s)->target;
            if (t.empty() && tctx_.curLoopFrame != 0 && tctx_.frameTop == tctx_.curLoopFrame) {
                tctx_.loopCtl = 3; return Value::any(); // cooperative redo
            }
            throw RedoEx{t};
        }
        case NK::IfStmt: {
            auto* is = static_cast<IfStmt*>(s);
            for (size_t bi = 0; bi < is->branches.size(); bi++) {
                auto& br = is->branches[bi];
                Value cv = eval(br.first.get());
                bool c = boolify(cv);
                if (is->isUnless) c = !c;
                if (c) {
                    auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
                    std::string bv = bi < is->branchVars.size() ? is->branchVars[bi]
                                     : (bi == 0 ? is->thenVar : "");
                    if (!bv.empty() && bv[0] == '*') { // slurpy binder: one-element list
                        Value l = Value::array({cv}); l.isList = false;
                        scope->define(bv.substr(1), l);
                    }
                    else if (!bv.empty()) scope->define(bv, cv); // if/elsif EXPR -> $x
                    else { // a lone $^placeholder in the branch body receives the condition
                        auto ph = computePlaceholders(br.second->stmts);
                        if (ph.size() == 1) scope->define(ph[0], cv);
                    }
                    return execBlock(br.second.get(), scope);
                }
                if (is->isUnless) break; // unless has single branch
            }
            if (is->elseBlock) {
                auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
                if (!is->elseVar.empty() && !is->branches.empty()) { // else -> $x gets the last cond value
                    Value lv = eval(is->branches.back().first.get());
                    if (is->elseVar[0] == '*') { Value l = Value::array({lv}); scope->define(is->elseVar.substr(1), l); }
                    else scope->define(is->elseVar, lv);
                }
                return execBlock(is->elseBlock.get(), scope);
            }
            { Value e = Value::array(); e.isList = true; e.s = "Slip"; return e; } // if/unless not taken: Empty
        }
        case NK::WhileStmt: {
            auto* ws = static_cast<WhileStmt*>(s);
            ValueList collected; ValueList* col = ws->asExpr ? &collected : nullptr;
            bool firstIter = true;
            std::shared_ptr<Env> scope; // reused across iterations unless captured
            for (;;) {
                Value cv = eval(ws->cond.get());
                bool c = boolify(cv);
                if (ws->isUntil) c = !c;
                if (!c) break;
                if (!scope || scope.use_count() > 1) { scope = std::make_shared<Env>(); scope->parent = tctx_.cur; }
                else scope->vars.clear(); // reuse buckets, drop last iteration's bindings
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
            // for over .values/.kv/.pairs of a SetHash/BagHash/MixHash ALIASES the
            // weights: mutations through the loop variable write back into the
            // container, and a weight of 0 (BagHash also <0) removes the element
            if (!fs->asExpr && !fs->destructure && fs->params.empty() && !fs->rwVars &&
                fs->list && fs->list->kind == NK::MethodCall) {
                auto* mc = static_cast<MethodCall*>(fs->list.get());
                int vmode = mc->method == "values" ? 1 : mc->method == "kv" ? 2
                          : mc->method == "pairs" ? 3 : 0;
                if (vmode && mc->args.empty() && !mc->hyper && !mc->meta && !mc->methodExpr &&
                    (vmode == 2 ? fs->vars.size() == 2 : fs->vars.size() <= 1) &&
                    // only probe side-effect-free invocants: resolving the lvalue of a
                    // method chain ($fh.lines.kv) would evaluate it and consume state
                    (mc->inv->kind == NK::VarExpr || mc->inv->kind == NK::SelfTerm)) {
                    Value* bagp = nullptr;
                    try { bagp = lvalue(mc->inv.get()); } catch (RakuError&) {}
                    if (bagp && bagp->t == VT::Hash && bagp->hash &&
                        (bagp->hashKind == "SetHash" || bagp->hashKind == "BagHash" ||
                         bagp->hashKind == "MixHash")) {
                        auto h = bagp->hash;
                        const std::string kind = bagp->hashKind;
                        auto applyW = [&](const std::string& key, const Value& nv) {
                            if (kind == "SetHash") {
                                if (nv.truthy()) (*h)[key] = Value::boolean(true);
                                else h->erase(key);
                            }
                            else if (kind == "BagHash") {
                                long long n = nv.toInt();
                                if (n <= 0) h->erase(key); else (*h)[key] = Value::integer(n);
                            }
                            else { // MixHash: 0 removes, negative weights stay
                                if (nv.toNum() == 0.0) h->erase(key); else (*h)[key] = nv;
                            }
                        };
                        std::vector<std::string> keys;
                        for (auto& kv : *h) keys.push_back(kv.first);
                        const std::string tvar = fs->vars.empty() ? "$_" : fs->vars[0];
                        for (size_t i = 0; i < keys.size(); i++) {
                            auto it0 = h->find(keys[i]);
                            if (it0 == h->end()) continue; // removed by an earlier iteration
                            Value w = it0->second;
                            auto scope = std::make_shared<Env>();
                            scope->parent = tctx_.cur;
                            Value pv; // pairs mode: shared pairVal carries mutations back
                            if (vmode == 1) scope->define(tvar, w);
                            else if (vmode == 2) {
                                scope->define(fs->vars[0], Value::str(keys[i]));
                                scope->define(fs->vars[1], w);
                            }
                            else { pv = Value::pair(keys[i], w); scope->define(tvar, pv); }
                            bool cont = runLoopBody(fs->body.get(), scope, fs->label,
                                                    i == 0, i + 1 == keys.size(), col);
                            if (vmode == 3) applyW(keys[i], pv.pairVal ? *pv.pairVal : w);
                            else {
                                auto it = scope->vars.find(vmode == 2 ? fs->vars[1] : tvar);
                                applyW(keys[i], it != scope->vars.end() ? it->second : w);
                            }
                            if (!cont) break;
                        }
                        return forResult();
                    }
                }
            }
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
                    // an itemized value ($(@a)) or a $-scalar source is ONE topic
                    bool oneItem = lv.itemized ||
                        (fs->list->kind == NK::VarExpr && !static_cast<VarExpr*>(fs->list.get())->name.empty() &&
                         static_cast<VarExpr*>(fs->list.get())->name[0] == '$');
                    if (oneItem) items.push_back(lv);
                    else if (lv.t == VT::Array && lv.arr) items = *lv.arr;
                    else if (lv.t == VT::Range) items = lv.flatten();
                    else if (lv.t == VT::Hash && lv.hash &&
                             (lv.hashKind.empty() || lv.hashKind == "Map" ||
                              lv.hashKind == "Set" || lv.hashKind == "SetHash" ||
                              lv.hashKind == "Bag" || lv.hashKind == "BagHash" ||
                              lv.hashKind == "Mix" || lv.hashKind == "MixHash")) {
                        for (auto& kv : *lv.hash) items.push_back(Value::pair(kv.first, kv.second));
                    }
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
            // a user object doing the Iterator role iterates by the pull-one
            // protocol: drain it (until IterationEnd) and loop over the values
            if (listv.t == VT::Object && listv.obj && listv.obj->cls &&
                listv.obj->cls->findMethod("pull-one")) {
                Value* po = listv.obj->cls->findMethod("pull-one");
                Value acc = Value::array(); acc.isList = true;
                for (;;) {
                    ValueList none;
                    Value v = invokeMethod(*po, listv, none);
                    if (v.t == VT::Type && v.s == "IterationEnd") break;
                    acc.arr->push_back(v);
                }
                listv = acc;
            }
            // A `$`-sigil scalar source (or an explicitly itemized value) is a single item:
            // `for $scalar { }` runs once even when the scalar holds an Array/Range, because a
            // scalar container does not flatten in list context. `@a`, ranges, and lists still flatten.
            bool scalarItem = listv.itemized ||
                (fs->list->kind == NK::VarExpr && !static_cast<VarExpr*>(fs->list.get())->name.empty()
                 && static_cast<VarExpr*>(fs->list.get())->name[0] == '$');
            // A body using $^a/$^b placeholders is an arity-N block, exactly like
            // `-> $a, $b`: bind them (sorted, per placeholder rules) and batch.
            std::vector<std::string> phVars;
            if (fs->vars.empty() && !fs->destructure && fs->body && fs->body->kind == NK::Block) {
                for (auto& pn : computePlaceholders(static_cast<Block*>(fs->body.get())->stmts))
                    if (pn.size() > 2 && pn[1] == '^') phVars.push_back(pn);
            }
            const std::vector<std::string>& loopVars = phVars.empty() ? fs->vars : phVars;
            // Fast paths for the common single-topic loop: avoid materializing the
            // whole sequence up front (a Range of N ints or a copy of an N-elem array).
            if (!scalarItem && !fs->destructure && loopVars.size() <= 1 &&
                fs->params.empty()) { // a sub-signature (`-> $ (:$k)`) needs real binding
                const std::string var = loopVars.empty() ? "$_" : loopVars[0];
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
                if (listv.t == VT::Range && !listv.rNum && listv.ofType != "Str") {
                    long long lo = listv.rFrom + (listv.rExFrom ? 1 : 0);
                    long long hi = listv.rTo - (listv.rExTo ? 1 : 0);
                    for (long long k = lo; k <= hi; k++) {
                        freshScope();
                        scope->define(var, Value::integer(k));
                        auto rb = [&] { scope->define(var, Value::integer(k)); }; // redo re-copies
                        if (!runLoopBody(fs->body.get(), scope, fs->label, k == lo, k == hi, col, rb)) break;
                    }
                    return forResult();
                }
                if (listv.t == VT::Array && listv.arr) {
                    auto arr = listv.arr; // share, don't copy the elements
                    // `$_` is rw-aliased to the elements when the source is a mutable
                    // `@`-variable, so `for @a { $_ *= 10 }` writes back into @a.
                    // `<-> $i` / `-> $i is rw` params alias the same way.
                    bool rw = (fs->vars.empty() || fs->rwVars) && fs->list->kind == NK::VarExpr &&
                              !static_cast<VarExpr*>(fs->list.get())->name.empty() &&
                              static_cast<VarExpr*>(fs->list.get())->name[0] == '@';
                    for (size_t i = 0; i < arr->size(); i++) {
                        freshScope();
                        scope->define(var, (*arr)[i]);
                        auto rb = [&] { if (!rw) scope->define(var, (*arr)[i]); }; // redo re-copies (aliases keep writes)
                        bool cont = runLoopBody(fs->body.get(), scope, fs->label, i == 0, i + 1 == arr->size(), col, rb);
                        if (rw) { auto it = scope->vars.find(var); if (it != scope->vars.end()) (*arr)[i] = it->second; }
                        if (!cont) break;
                    }
                    return forResult();
                }
            }
            ValueList items;
            // a Blob/Buf iterates its BYTES (as Int) — `for $data -> $b1,$b2?,$b3?`
            // is how MIME::Base64 & friends read the buffer. Only an explicitly
            // itemized `$(…)` blob stays one element. (rakupp lacks assign-time
            // itemization, so `for $my-scalar-blob` iterates too — the common,
            // intuitive reading; the rare Rakudo one-item quirk is not modeled.)
            if (listv.t == VT::Str && !listv.itemized &&
                (listv.hashKind == "Blob" || listv.hashKind == "Buf")) {
                items = listv.blobList(); // elements (bytes, or LE words for blob16/32/64)
            }
            else if (scalarItem) items.push_back(listv); // a $-scalar / itemized source is one item
            else if (listv.t == VT::Array && listv.arr) items = *listv.arr; // one-level
            else if (listv.t == VT::Range) items = listv.flatten();
            else if (listv.t == VT::Hash && listv.hash &&
                     (listv.hashKind.empty() || listv.hashKind == "Map" ||
                      listv.hashKind == "Set" || listv.hashKind == "SetHash" ||
                      listv.hashKind == "Bag" || listv.hashKind == "BagHash" ||
                      listv.hashKind == "Mix" || listv.hashKind == "MixHash")) {
                // a bare hash/set/bag iterates its Pairs (elem => True / count)
                for (auto& kv : *listv.hash) items.push_back(Value::pair(kv.first, kv.second));
            }
            else items.push_back(listv);
            // `-> (:key($k), :value($v))` / nested sub-signatures: real signature
            // binding — each element is the single argument of the pointy signature.
            if (!fs->params.empty()) {
                for (size_t i = 0; i < items.size(); i++) {
                    auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
                    ValueList one{items[i]};
                    one[0].namedArg = false; // a loop topic is a VALUE, never a named arg
                    bindParams(fs->params, one, scope);
                    if (!runLoopBody(fs->body.get(), scope, fs->label, i == 0, i + 1 == items.size(), col)) break;
                }
                return forResult();
            }
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
            size_t nvars = loopVars.empty() ? 1 : loopVars.size();
            for (size_t i = 0; i < items.size(); i += nvars) {
                auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
                if (loopVars.empty()) {
                    scope->define("$_", items[i]);
                } else {
                    for (size_t k = 0; k < loopVars.size(); k++) {
                        scope->define(loopVars[k], (i + k < items.size()) ? items[i + k] : Value::any());
                    }
                }
                if (!runLoopBody(fs->body.get(), scope, fs->label, i == 0, i + nvars >= items.size(), col)) break;
            }
            return forResult();
        }
        case NK::GivenStmt: {
            auto* g = static_cast<GivenStmt*>(s);
            Value topic = eval(g->topic.get());
            // `with 'literal' { tr/// }` must die: a LITERAL topic is not a container
            // (`given my $x = …`, expression topics, and `-> $_ is copy` stay writable)
            if (g->topic && g->var.empty() &&
                (g->topic->kind == NK::StrLit || g->topic->kind == NK::InterpStr ||
                 g->topic->kind == NK::IntLit || g->topic->kind == NK::NumLit ||
                 g->topic->kind == NK::BoolLit))
                topic.readonly = true;
            // with/without definedness guard
            bool skip = (g->defGuard == 1 && !isDefined(topic)) || (g->defGuard == 2 && isDefined(topic));
            if (g->modifier) { // `EXPR with X`: no implicit block — a `my` in EXPR leaks out
                auto env = tctx_.cur;
                bool hadTopic = env->vars.count("$_");
                Value savedTopic = hadTopic ? env->vars["$_"] : Value::any();
                env->vars["$_"] = topic;
                // `{ $^x } without X` — a placeholder block receives the topic as its argument
                if (g->body) {
                    auto ph = computePlaceholders(g->body->stmts);
                    if (ph.size() == 1) env->vars[ph[0]] = topic;
                }
                Value r = Value::any();
                if (skip && !g->hasElse) { // `(42 without $def)` contributes NOTHING to a list
                    r = Value::array(); r.isList = true; r.s = "Slip";
                    if (hadTopic) env->vars["$_"] = savedTopic; else env->vars.erase("$_");
                    return r;
                }
                try {
                    if (skip) { if (g->hasElse) r = execBlock(g->elseBody.get(), env); }
                    else r = execBlock(g->body.get(), env);
                } catch (BreakGivenEx& e) { r = e.hasVal ? e.v : Value::any(); }
                catch (...) { if (hadTopic) env->vars["$_"] = savedTopic; else env->vars.erase("$_"); throw; }
                if (hadTopic) env->vars["$_"] = savedTopic; else env->vars.erase("$_");
                return r;
            }
            auto scope = std::make_shared<Env>(); scope->parent = tctx_.cur;
            scope->define("$_", topic);
            if (!g->var.empty()) scope->define(g->var, topic);
            // `do with (EXPR) { $^a … }` — a placeholder block receives the topic as
            // its argument (mirrors the statement-modifier form above; Base64 pads via
            // `do with (3 - ($c.key+1) % 3) { $^a == 3 ?? 0 !! $^a }`)
            if (g->body) {
                auto ph = computePlaceholders(g->body->stmts);
                if (ph.size() == 1) scope->define(ph[0], topic);
            }
            // `given`/`with` is an expression: it evaluates to the value of the matched
            // `when`/`default` block (delivered via BreakGivenEx), or, if nothing matches,
            // the value of the block's last statement.
            if (skip) {
                if (g->hasElse) {
                    if (!g->elseVar.empty()) scope->define(g->elseVar, topic); // else -> $pos { }
                    try { return execBlock(g->elseBody.get(), scope); }
                    catch (BreakGivenEx& e) { return e.hasVal ? e.v : Value::any(); }
                }
                Value e = Value::array(); e.isList = true; e.s = "Slip"; return e; // Empty
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
                std::shared_ptr<Env> scope; // reused across iterations unless captured
                for (;;) {
                    if (ls->cond && !boolify(eval(ls->cond.get()))) break;
                    if (!scope || scope.use_count() > 1) { scope = std::make_shared<Env>(); scope->parent = tctx_.cur; }
                    else scope->vars.clear(); // reuse buckets, drop last iteration's bindings
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
            std::shared_ptr<Env> scope; // reused across iterations unless captured
            for (;;) {
                if (!scope || scope.use_count() > 1) { scope = std::make_shared<Env>(); scope->parent = tctx_.cur; }
                else scope->vars.clear(); // reuse buckets, drop last iteration's bindings
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

// Throw a typed exception as a real OBJECT carrying attributes, so
// throws-like matchers (`symbol => '$!bar'`) can introspect it. The class is
// registered on first use with exactly the attributes passed.
Value Interpreter::makeTypedEx(const std::string& type,
                               std::vector<std::pair<std::string, Value>> attrs,
                               const std::string& message) {
    auto it = classes_.find(type);
    if (it == classes_.end()) {
        auto ci = std::make_shared<ClassInfo>();
        ci->name = type;
        for (auto& kv : attrs) { ClassAttr a; a.name = kv.first; a.sigil = '$'; a.pub = true; ci->attrs.push_back(a); }
        { ClassAttr a; a.name = "message"; a.sigil = '$'; a.pub = true; ci->attrs.push_back(a); }
        classes_[type] = ci;
        it = classes_.find(type);
    }
    Value ex; ex.t = VT::Object; ex.obj = std::make_shared<ObjectData>();
    ex.obj->cls = it->second;
    for (auto& kv : attrs) ex.obj->attrs[kv.first] = kv.second;
    ex.obj->attrs["message"] = Value::str(message);
    return ex;
}

void Interpreter::throwTypedV(const std::string& type,
                              std::vector<std::pair<std::string, Value>> attrs,
                              const std::string& message) {
    Value ex = makeTypedEx(type, std::move(attrs), message);
    throw RakuError{ex, message};
}

void Interpreter::throwTyped(const std::string& type,
                             std::vector<std::pair<std::string, std::string>> attrs,
                             const std::string& message) {
    std::vector<std::pair<std::string, Value>> va;
    va.reserve(attrs.size());
    for (auto& kv : attrs)
        // a `type` attribute naming a core type is the type OBJECT (so
        // throws-like `type => Array` smartmatches), not the string
        if (kv.first == "type" && isKnownTypeName(kv.second))
            va.emplace_back(kv.first, Value::typeObj(kv.second));
        else
            va.emplace_back(kv.first, Value::str(kv.second));
    throwTypedV(type, std::move(va), message);
}

// Build the effective symbol name of a (possibly multi-segment) symbolic ref:
// join `pkg` + the head + `::seg` parts, hoist a sigil on the LAST segment to
// the front (`OUR::('$x')` looks up `$OUR::x`), then rewrite pseudo-package
// heads (GLOBAL:: strips, OUR:: is the current package, MY::/UNIT::/OUTER::/
// CALLER::/SETTING::/CORE:: fall back to the lexical chain — approximations).
std::string Interpreter::symRefName(SymbolicRef* sr) {
    std::string nm;
    if (sr->nameExpr) nm = eval(sr->nameExpr.get()).toStr();
    for (auto& sg : sr->segs) {
        if (!nm.empty()) nm += "::";
        nm += eval(sg.get()).toStr();
    }
    if (!sr->pkg.empty()) nm = sr->pkg + "::" + nm;
    // a sigil written on the last path part names the variable: A::B::$x == $A::B::x
    size_t lastSep = nm.rfind("::");
    if (lastSep != std::string::npos && lastSep + 2 < nm.size() &&
        std::strchr("$@%&", nm[lastSep + 2]))
        nm = nm[lastSep + 2] + nm.substr(0, lastSep + 2) + nm.substr(lastSep + 3);
    if (!sr->sigil.empty() && (nm.empty() || !std::strchr("$@%&", nm[0])))
        nm = sr->sigil + nm;
    // pseudo-package heads
    std::string sig;
    if (!nm.empty() && std::strchr("$@%&", nm[0])) { sig = nm.substr(0, 1); nm = nm.substr(1); }
    for (;;) {
        if      (nm.rfind("GLOBAL::",  0) == 0) nm = nm.substr(8);
        else if (nm.rfind("OUR::",     0) == 0) nm = tctx_.pkgPrefix + nm.substr(5);
        else if (nm.rfind("MY::",      0) == 0) nm = nm.substr(4);
        else if (nm.rfind("UNIT::",    0) == 0) nm = nm.substr(6);
        else if (nm.rfind("OUTER::",   0) == 0) nm = nm.substr(7);
        else if (nm.rfind("CALLER::",  0) == 0) nm = nm.substr(8);
        else if (nm.rfind("SETTING::", 0) == 0) nm = nm.substr(9);
        else if (nm.rfind("CORE::",    0) == 0) nm = nm.substr(6);
        else break;
    }
    return sig + nm;
}

// A lone typed candidate rejects a mismatched argument (multi dispatch already
// type-selects; without this a single `sub f(Int $x)` bound anything). Type
// objects/undefined bind (no :D enforcement here) and junction kinds pass —
// they were autothreaded upstream; one reaching here is a matcher-style arg.
void Interpreter::typeCheckBind(const Param& p, const Value& v) {
    if (v.t == VT::Type || v.t == VT::Nil || v.t == VT::Any) return;
    if (v.t == VT::Array && (v.enumName == "any" || v.enumName == "all" ||
                             v.enumName == "one" || v.enumName == "none")) return;
    // a type name we can't resolve (a `::T` capture, an unimported module type)
    // cannot be enforced — bind freely like before. Native lowercase names are
    // resolvable even though isKnownTypeName (boxed names) doesn't list them.
    static const std::set<std::string> natNames = {
        "int", "int8", "int16", "int32", "int64", "uint", "uint8", "uint16",
        "uint32", "uint64", "byte", "num", "num32", "num64", "str", "atomicint"};
    if (!classes_.count(p.type) && !subsets_.count(p.type) &&
        !isKnownTypeName(p.type) && !natNames.count(p.type)) return;
    if (typeOrSubsetMatches(v, p.type)) return;
    std::string gist = v.t == VT::Str ? "\"" + v.s + "\"" : v.gist();
    if (gist.size() > 50) gist = gist.substr(0, 47) + "...";
    throw RakuError{Value::typeObj("X::TypeCheck::Binding"),
        "Type check failed in binding to parameter '" + p.name + "'; expected " +
        p.type + " but got " + v.typeName() + " (" + gist + ")"};
}

void Interpreter::bindParams(const std::vector<Param>& params, ValueList& args,
                             std::shared_ptr<Env>& env, bool methodCtx) {
    // Fast path: every parameter is a plain mandatory positional scalar and no
    // named arguments were passed — the overwhelmingly common signature. Bind
    // positionally, skipping the named-map / explicit-named-set / substr /
    // default-eval machinery below (all pure allocation for this case).
    {
        bool simple = true;
        for (auto& p : params)
            if (p.sigil != '$' || p.named || p.slurpy || p.optional || p.invocant ||
                p.isRw || p.isCopy || p.defaultVal || p.subSig || p.litVal ||
                p.whereExpr || p.defConstraint || p.coerce ||
                (p.name.size() > 2 && (p.name[1] == '!' || p.name[1] == '.'))) // attributive: writes through to self
                { simple = false; break; }
        if (simple)
            for (auto& a : args) if (isNamedArg(a)) { simple = false; break; }
        if (simple) {
            for (size_t i = 0; i < params.size(); i++) {
                if (i < args.size()) {
                    Value v = args[i];
                    if (!params[i].type.empty()) typeCheckBind(params[i], v);
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
    bool hasNamedSlurpy = false;
    for (auto& p : params) {
        // a `|c` capture (slurpy, '\\' sigil) absorbs unclaimed nameds like *%h does
        if (p.slurpy) { if (p.sigil == '%' || p.sigil == '\\') hasNamedSlurpy = true; continue; }
        if (!p.named) continue;
        if (!p.namedKey.empty()) explicitNamed.insert(p.namedKey);
        for (auto& ak : p.aliasKeys) explicitNamed.insert(ak); // nested alias layers
        if (p.namedKey.empty() || p.aliasBoth) {
            // strip sigil AND any twigil: `:$!bar` answers `bar => …`
            std::string key = p.name.size() > 2 && (p.name[1] == '!' || p.name[1] == '.')
                            ? p.name.substr(2) : (p.name.size() > 1 ? p.name.substr(1) : p.name);
            explicitNamed.insert(key);
        }
    }
    // A default value is evaluated in the param scope being built, so it can refer
    // to earlier parameters (`sub f($g, $a = $g/2)`).
    auto evalDefault = [&](Expr* e) -> Value {
        auto saved = tctx_.cur; tctx_.cur = env;
        Value v; try { v = eval(e); } catch (...) { tctx_.cur = saved; throw; }
        tctx_.cur = saved; return v;
    };
    size_t pi = 0;
    for (auto& p : params) {
        // An explicit invocant (`$self:` / `Type:D:`) binds to `self` (already in
        // the method env) and does NOT consume a positional argument — the dispatch
        // matched it. Consuming one here would shift every following parameter.
        if (p.invocant) {
            if (!p.name.empty())
                if (Value* sp = env->find("self")) env->define(p.name, *sp);
            continue;
        }
        std::string bareName = !p.namedKey.empty() ? p.namedKey
                             : (p.name.size() > 1 ? p.name.substr(1) : p.name);
        // attributive param `:$!attr` / `:$.attr` (BUILD/TWEAK style): the named
        // key is the attribute's bare name, and a bound value writes through to
        // the invocant's attribute
        bool attributive = p.named && p.name.size() > 2 &&
                           (p.name[1] == '!' || p.name[1] == '.');
        if (attributive && p.namedKey.empty()) bareName = p.name.substr(2);
        auto attrWrite = [&](const Value& v) {
            if (!attributive) return;
            if (Value* sp = env->find("self"))
                if (sp->t == VT::Object && sp->obj) {
                    Value av = v;
                    if (p.name[0] == '@') av = coerceArray(av);
                    else if (p.name[0] == '%') av = coerceHash(av);
                    sp->obj->attrs[p.name.substr(2)] = std::move(av);
                }
        };
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
                // a `|c` capture also carries the UNCLAIMED named args (as
                // namedArg pairs), so `samewith(|c)` re-passes them — Base64's
                // adverb multis peel one named per round and forward the rest
                if (p.sigil == '\\' && !p.name.empty())
                    for (auto& kv : named)
                        if (!explicitNamed.count(kv.first)) {
                            Value na = Value::pair(kv.first, kv.second); na.namedArg = true;
                            a.arr->push_back(std::move(na));
                        }
                env->define(p.name, a);
                // capture sub-signature `|c($x, :$y!)` — unpack the slurped
                // positionals AND the call's named args into the inner params
                if (p.subSig) {
                    ValueList inner = *a.arr;
                    for (auto& kv : named) {
                        Value na = Value::pair(kv.first, kv.second); na.namedArg = true;
                        inner.push_back(na);
                    }
                    bindParams(*p.subSig, inner, env);
                }
            }
            continue;
        }
        // destructure a value against p's sub-signature: positionals from the
        // elements, named inner params (`:key($k)`) from the value's accessors
        auto destructure = [&](const Param& sp, const Value& v) {
            // a hash sub-signature `%h (:$left, :$right, *%)` binds each `:$k` from the
            // hash's KEY (not a `.k` accessor), and `*%` slurps the remaining keys.
            if (v.t == VT::Hash && v.hash) {
                ValueList inner;
                for (auto& kv : *v.hash) { Value na = Value::pair(kv.first, kv.second); na.namedArg = true; inner.push_back(na); }
                bindParams(*sp.subSig, inner, env);
                return;
            }
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
            if (it == named.end() && p.aliasBoth && p.name.size() > 1)
                it = named.find(p.name.substr(1)); // :a(:$b) also answers b => …
            for (auto& ak : p.aliasKeys)          // :x(:y(:z($a))): y/z answer too
                if (it == named.end()) it = named.find(ak);
            if (it != named.end()) {
                // a bare `:j` (Bool True) cannot bind a %- or @-sigil named param
                if ((p.sigil == '%' || p.sigil == '@') && it->second.t == VT::Bool)
                    throw RakuError{Value::typeObj("X::TypeCheck::Binding"),
                        "Type check failed in binding to parameter '" + p.name + "'"};
                if (p.subSig) destructure(p, it->second); // :value((Str :key($d), …))
                if (!p.subSig && p.sigil == '$' && !p.type.empty() && !p.coerce)
                    typeCheckBind(p, it->second);
                if (!p.name.empty() || !p.subSig) env->define(p.name, it->second);
                attrWrite(it->second);
            }
            else if (p.defaultVal) {
                Value dv = evalDefault(p.defaultVal.get());
                env->define(p.name, dv);
                attrWrite(dv); // `:$!x = 42` with no arg still initializes the attr
            }
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
            // coercion type `Int() \x` / `Str(Cool) $s`: coerce the bound value via
            // its .Type method — which FAILS where the method does (Int on <1/0>)
            if (p.coerce && !p.type.empty() && v.typeName() != p.type)
                v = methodCall(v, p.type, ValueList{});
            else if (p.sigil == '$' && !p.invocant && !p.type.empty())
                typeCheckBind(p, v); // a lone typed candidate REJECTS a mismatch (like Rakudo)
            // a plain scalar param (no `is rw`/`is copy`) is readonly — mutating it (s///) dies
            if (p.sigil == '$' && !p.isRw && !p.isCopy && !p.invocant) v.readonly = true;
            env->define(p.name, v);
            // POSITIONAL attributive param `method set-body($!body)`: the bound
            // value writes through to the invocant's attribute (Cro's
            // MessageWithBody sets bodies this way)
            if (p.name.size() > 2 && (p.name[1] == '!' || p.name[1] == '.')) {
                if (Value* sp = env->find("self"))
                    if (sp->t == VT::Object && sp->obj) {
                        Value av = v;
                        if (p.name[0] == '@') av = coerceArray(av);
                        else if (p.name[0] == '%') av = coerceHash(av);
                        sp->obj->attrs[p.name.substr(2)] = std::move(av);
                    }
            }
        } else if (p.subSig) {
            bindParams(*p.subSig, positional, env); // no arg → bind inner to (), fills defaults
        } else if (p.defaultVal) {
            env->define(p.name, evalDefault(p.defaultVal.get()));
        } else {
            env->define(p.name, typedDefault(p.type, p.sigil));
        }
    }

    // unknown named arguments are an error unless a *%slurpy collects them.
    // Only enforced when the signature DECLARES named params — adverbed calls to
    // positional-only subs keep the historical tolerance (our signature model
    // still under-parses some named forms; see named-renaming.t for the case
    // this must catch: sub g(:a($b)) rejects g(b => 1)).
    if (!hasNamedSlurpy && !methodCtx && !explicitNamed.empty())
        for (auto& kv : named)
            if (!explicitNamed.count(kv.first))
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Unexpected named argument '" + kv.first + "' passed"};

    // enforce `where` constraints on the bound values (a single sub isn't dispatched,
    // so scoreCandidate never ran — `sub p(Int $n where * > 0)` must reject p(-1))
    for (auto& p : params) {
        if (!p.whereExpr || p.slurpy || p.name.empty()) continue;
        Value* bound = env->find(p.name);
        if (!bound) continue;
        Value val = *bound;
        auto wenv = std::make_shared<Env>(); wenv->parent = env;
        wenv->define("$_", val);
        auto saved = tctx_.cur; tctx_.cur = wenv;
        bool ok;
        try {
            Value cv = eval(p.whereExpr.get());
            // `where EXPR` is a smartmatch: a Code/WhateverCode is called with the
            // value; anything else (a type, a junction like `Any:U|Blob|Cool`) is
            // smartmatched — NOT just boolified
            if (cv.t == VT::Code && cv.code) ok = boolify(callCallable(cv, ValueList{val}));
            else ok = boolify(applyBinOp("~~", val, cv));
        } catch (...) { tctx_.cur = saved; throw; }
        tctx_.cur = saved;
        if (!ok)
            throw RakuError{Value::typeObj("X::TypeCheck::Binding"),
                "Constraint type check failed in binding to parameter '" + p.name + "'"};
    }
}

// Does an argument satisfy a parameter type-constraint name?
// Package-relative short-name view for the type matchers (free/static functions):
// set by the Interpreter so `has Path $.path` accepts a URI::Path object when
// `Path` is an alias. Null in the compiled-runtime path (no aliasing there).
static const std::unordered_map<std::string, std::string>* s_classAliases = nullptr;
static const std::unordered_map<std::string, std::shared_ptr<ClassInfo>>* s_classesForAlias = nullptr;
void rtSetAliasView(const std::unordered_map<std::string, std::string>* a,
                    const std::unordered_map<std::string, std::shared_ptr<ClassInfo>>* c) {
    s_classAliases = a; s_classesForAlias = c;
}
// resolve a type-constraint name through the alias table — only when no REAL
// class claims the short name (a later genuine `class Path` wins over the alias)
static const std::string& aliasType(const std::string& type) {
    if (!s_classAliases || (s_classesForAlias && s_classesForAlias->count(type))) return type;
    auto it = s_classAliases->find(type);
    return it != s_classAliases->end() ? it->second : type;
}

static bool typeMatchesArg(const Value& arg, const std::string& type) {
    if (type.empty() || type == "Any" || type == "Mu") return true;
    // an enum VALUE as a parameter type (`multi f(\b, int $i, LittleEndian)`)
    // matches exactly that value; the enum TYPE name matches any of its values
    if (!arg.enumName.empty() && (type == arg.enumName || type == arg.enumType ||
        (!arg.enumType.empty() && type == arg.enumType + "::" + arg.enumName))) return true;
    // command-line allomorphs: a numeric-looking argv string binds Int/Num/Rat
    // params as well as Str ones (Rakudo passes IntStr/RatStr to MAIN)
    if (arg.t == VT::Str && arg.hashKind == "Allomorph" &&
        (type == "Int" || type == "UInt" || type == "Num" || type == "Rat" ||
         type == "Numeric" || type == "Real"))
        return arg.s.find('.') == std::string::npos || (type != "Int" && type != "UInt");
    // an allomorph (IntStr/RatStr/NumStr) also binds Str/Stringy params and its own name
    if (arg.isAllomorph() && (type == "Str" || type == "str" || type == "Stringy" ||
                              type == "Cool" || type == arg.typeName()))
        return true;
    // a tagged built-in value (IO::Path, Version, Duration, Promise, …) matches
    // its own reported type — hashKind is empty for plain values, so this
    // costs one branch on the hot path
    if (!arg.hashKind.empty() && (type == arg.hashKind || type == arg.typeName())) return true;
    // native-typed params (`int $i`, `num $x`, `str $s`) take the boxed kind
    static const std::set<std::string> natIntTypes = {"int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64", "byte"};
    static const std::set<std::string> natNumTypes = {"num", "num32", "num64"};
    switch (arg.t) {
        case VT::Int:  return type == "Int" || type == "Cool" || type == "Numeric" || type == "Real" || type == "Rat" || natIntTypes.count(type) > 0;
        case VT::Num:  return type == "Num" || type == "Cool" || type == "Numeric" || type == "Real" || natNumTypes.count(type) > 0;
        case VT::Complex: return type == "Complex" || type == "Cool" || type == "Numeric";
        case VT::Rat:  return type == "Rat" || type == "Cool" || type == "Numeric" || type == "Real";
        case VT::Bool: return type == "Bool";
        case VT::Str:
            // a byte buffer is NOT Stringy: Blob/Buf bind only buffer-typed
            // params (`multi sha1(Str)` vs `multi sha1(blob8)` — Digest's
            // `samewith $str.encode` looped forever when Blob re-matched Str)
            if (arg.hashKind == "Blob" || arg.hashKind == "Buf" || arg.hashKind == "CArray") {
                static const std::set<std::string> bufTypes = {
                    "Blob", "Buf", "blob8", "buf8", "blob16", "buf16", "blob32", "buf32",
                    "blob64", "buf64", "utf8", "utf16", "utf32", "Positional"};
                return bufTypes.count(type) > 0;
            }
            return type == "Str" || type == "Cool" || type == "Stringy" || type == "str";
        case VT::Array: return type == "Array" || type == "List" || type == "Positional" || type == "Iterable" || (arg.isList && arg.s == "Seq" && type == "Seq");
        case VT::Hash:
            if (arg.hashKind == "FileHandle" && (type == "IO::Handle" || type == "IO" || type == "Handle")) return true;
            return type == "Hash" || type == "Map" || type == "Associative" || (arg.hashKind == type);
        case VT::Pair: return type == "Pair";
        case VT::Code: return type == "Code" || type == "Callable" || type == "Routine" || type == "Block" || type == "Sub";
        case VT::Regex: return type == "Regex";
        case VT::Match: return type == "Match";
        case VT::Range: return type == "Range" || type == "Iterable";
        case VT::Object: {
            for (ClassInfo* ci = arg.obj ? arg.obj->cls.get() : nullptr; ci; ci = ci->parent.get()) {
                if (ci->name == type || ci->nativeParent == type) return true;
                if (ci->doneRoles.count(type)) return true; // composed roles count as types
                for (auto& p : ci->extraParents) if (p && p->name == type) return true;
            }
            // package-relative short name: a `Path`-typed param accepts URI::Path
            const std::string& q = aliasType(type);
            if (&q != &type)
                for (ClassInfo* ci = arg.obj ? arg.obj->cls.get() : nullptr; ci; ci = ci->parent.get())
                    if (ci->name == q || ci->doneRoles.count(q)) return true;
            // a subclass of a built-in (`class F is DateTime`) matches the built-in
            if (arg.obj && arg.obj->hasBoxed) return typeMatchesArg(arg.obj->boxed, type);
            return false;
        }
        default: return true; // Nil/Any/Type/unknown subset/enum: lenient
    }
}

// Does value v satisfy subset `name` (base type chain + where constraint)?
bool Interpreter::subsetMatches(const std::string& name, const Value& v, int depth) {
    if (depth > 16) return false; // subset cycle backstop
    auto it = subsets_.find(name);
    if (it == subsets_.end()) return typeMatchesArg(v, name);
    const SubsetInfo& si = it->second;
    if (!si.base.empty() && !subsetMatches(si.base, v, depth + 1)) return false;
    if (si.where) {
        auto env = std::make_shared<Env>(); env->parent = tctx_.cur;
        env->define("$_", v);
        auto saved = tctx_.cur; tctx_.cur = env;
        bool ok = false;
        try {
            Value cv = eval(const_cast<Expr*>(si.where));
            // `where EXPR` is a smartmatch: a Code/WhateverCode is called with
            // the value; anything else (a type, a junction like
            // `Cro::Message | Cro::Connection`) is smartmatched — NOT boolified
            if (cv.t == VT::Code && cv.code) ok = boolify(callCallable(cv, ValueList{v}));
            else if (cv.t == VT::Regex) ok = boolify(regexMatch(v.toStr(), cv.s));
            else ok = boolify(applyBinOp("~~", v, cv));
        } catch (...) { tctx_.cur = saved; return false; }
        tctx_.cur = saved;
        return ok;
    }
    return true;
}

bool Interpreter::typeOrSubsetMatches(const Value& v, const std::string& type) {
    if (subsets_.count(type)) return subsetMatches(type, v);
    return typeMatchesArg(v, type);
}

int Interpreter::scoreCandidate(const Value& cand, const ValueList& args) {
    if (cand.t != VT::Code || !cand.code || !cand.code->params) return 0; // no signature: lowest specificity
    const auto& params = *cand.code->params;
    ValueList pos; for (auto& a : args) if (!isNamedArg(a)) pos.push_back(a);
    size_t required = 0, total = 0; bool slurpy = false;
    const Param* slurpyParam = nullptr;
    std::vector<const Param*> positional;
    for (auto& p : params) {
        if (p.named) continue;
        if (p.invocant) continue; // the invocant (`Foo:D:`) is matched by the dispatch, not a positional arg
        if (p.slurpy) { slurpy = true; if (!p.named) slurpyParam = &p; continue; }
        positional.push_back(&p);
        total++;
        if (!p.optional && !p.defaultVal) required++;
    }
    // a multi whose only params are placeholders (`multi sub f { $^a² }`):
    // its arity is the placeholder count
    if (positional.empty() && !slurpy && !cand.code->placeholders.empty()) {
        if (pos.size() != cand.code->placeholders.size()) return -1;
        return 1;
    }
    if (pos.size() < required) return -1;
    if (!slurpy && pos.size() > total) return -1;
    // a `where` on the slurpy is checked against the list it would bind
    // (`multi MAIN(*@ints where { .elems > 0 })` must lose to MAIN() bare)
    if (slurpyParam && slurpyParam->whereExpr) {
        Value lst = Value::array();
        for (size_t i = total; i < pos.size(); i++) lst.arr->push_back(pos[i]);
        auto env = std::make_shared<Env>(); env->parent = tctx_.cur;
        if (!slurpyParam->name.empty()) env->define(slurpyParam->name, lst);
        env->define("$_", lst);
        auto saved = tctx_.cur; tctx_.cur = env;
        bool ok = false;
        try {
            Value cv = eval(slurpyParam->whereExpr.get());
            if (cv.t == VT::Code && cv.code) cv = callCallable(cv, ValueList{lst});
            ok = boolify(cv);
        } catch (...) { tctx_.cur = saved; return -1; }
        tctx_.cur = saved;
        if (!ok) return -1;
    }
    int score = 0;
    // a capture with a sub-signature (`|c (Int $x)`) dispatches on the INNER
    // signature: score the remaining args (positionals it would slurp + all
    // nameds) against it recursively
    if (slurpyParam && slurpyParam->sigil == '\\' && slurpyParam->subSig) {
        auto tmp = std::make_shared<Callable>();
        tmp->params = slurpyParam->subSig.get();
        Value tv; tv.t = VT::Code; tv.code = tmp;
        ValueList rest;
        for (size_t i = total; i < pos.size(); i++) rest.push_back(pos[i]);
        for (auto& a : args) if (isNamedArg(a)) rest.push_back(a);
        int s = scoreCandidate(tv, rest);
        if (s < 0) return -1;
        score += 1 + s;
    }
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
        if (subsets_.count(p->type)) {
            if (!subsetMatches(p->type, pos[i])) return -1;
            score += 2; // a satisfied subset is very specific
        }
        else if (p->sigil == '&' && !p->type.empty() && p->type != "Callable") {
            // `Int &x` constrains the routine's RETURN type — not modeled; accept
            // any Code so dispatch proceeds (return-type dispatch logged as a gap)
            if (pos[i].t != VT::Code) return -1;
        }
        else if (p->sigil == '&') {
            // a bare `&task` param requires a Callable — a type object or plain
            // value must not bind (Log::Timeline's log($parent,&task) vs
            // log(&task,*%data) dispatch depends on this)
            if (pos[i].t != VT::Code) return -1;
        }
        else if (p->coerce && !p->type.empty()) {
            // coercion type `Str(Cool)`: any coercible argument matches — the
            // coercion itself happens at binding (append-header('CL', $int))
            score++;
        }
        else if ((p->sigil == '@' || p->sigil == '%') && !p->type.empty() &&
                 p->type != "Any" && p->type != "Mu" && p->type != "Positional" &&
                 p->type != "Associative" && p->type != "Iterable") {
            // `Int @a` / `Str %h`: the type parameterizes the CONTAINER — only a
            // matching typed container (my Int @a) dispatches, as in Rakudo
            const Value& av = pos[i];
            if (p->sigil == '@' ? av.t != VT::Array : av.t != VT::Hash) return -1;
            if (av.ofType != p->type) return -1;
            score += 2;
        }
        else if (!typeMatchesArg(pos[i], p->type)) return -1;
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
    // named params: a REQUIRED named (`:$test!`) disqualifies the candidate when
    // that named arg wasn't passed — `multi MAIN(:$test!)` must lose to the
    // default candidate on a bare invocation. A supplied match adds specificity,
    // and a `where` constraint participates (evaluated against the supplied
    // value, or the type object / default when absent).
    for (auto& p : params) {
        if (!p.named) continue;
        // every name this param answers to: the primary key, nested alias keys,
        // and the variable's own name for `:$x` / `:buf(:$bin)` (aliasBoth) forms
        std::vector<std::string> keys;
        if (!p.namedKey.empty()) keys.push_back(p.namedKey);
        for (auto& ak : p.aliasKeys) keys.push_back(ak);
        if (p.namedKey.empty() || p.aliasBoth)
            keys.push_back(p.name.size() > 1 ? p.name.substr(1) : p.name); // strip the sigil
        bool supplied = false; Value sval;
        for (auto& a : args) {
            if (!isNamedArg(a)) continue;
            for (auto& key : keys)
                if (a.s == key) { supplied = true; sval = a.pairVal ? *a.pairVal : Value::boolean(true); break; }
            if (supplied) break;
        }
        if (p.required && !supplied) return -1;
        if (supplied) {
            // a supplied named must TYPE-match its declared constraint, or the
            // candidate is out — `(Bool:D :$pad!)` does not bind `:pad('')`
            // (Base64's pad-rewrite chain relies on this to terminate)
            if (p.sigil == '$' && !p.type.empty() && !typeMatchesArg(sval, p.type)) return -1;
            if (p.defConstraint == 1 && !isDefined(sval)) return -1;
            if (p.defConstraint == 2 && isDefined(sval)) return -1;
            // Rakudo's candidate sort treats a candidate REQUIRING a named as narrower
            // than one that doesn't, ahead of positional-type comparison — Base64's
            // `:$pad!`/`:$uri!`/`:$str!` adverb multis (each `(Bool:D :$x!, |c)`) must
            // beat the positionally-typed `(Str:D $s, |c)` when their named is passed.
            // A large fixed boost approximates that lexicographic rule.
            score += p.required ? 16 : 2;
        }
        if (p.whereExpr) {
            Value v = supplied ? sval
                    : p.defaultVal ? eval(p.defaultVal.get())
                    : !p.type.empty() ? Value::typeObj(p.type) : Value::any();
            auto env = std::make_shared<Env>(); env->parent = tctx_.cur;
            if (!p.name.empty()) env->define(p.name, v);
            env->define("$_", v);
            auto saved = tctx_.cur; tctx_.cur = env;
            bool ok = false;
            try {
                Value cv = eval(p.whereExpr.get());
                if (cv.t == VT::Code && cv.code) cv = callCallable(cv, ValueList{v});
                ok = boolify(cv);
            } catch (...) { tctx_.cur = saved; return -1; }
            tctx_.cur = saved;
            if (!ok) return -1;
        }
    }
    // A slurpy candidate is the LEAST-specific tiebreaker: at equal specificity a
    // fixed-arity candidate wins (`multi f(){}` beats `multi f(*@a){}` on `f()`),
    // but a more-constrained slurpy still beats a plainer fixed one. Encode that as
    // the low bit so it only decides otherwise-equal scores; matches stay >= 0.
    return score * 2 + (slurpy ? 0 : 1);
}

// Per-thread stack accounting for the recursion guard. `t_stackTop` is a byte
// address near the top of this thread's stack (set once, lazily, from the first
// guarded frame); `t_stackLimit` is that thread's usable stack size. The main
// interpreter runs on a 1 GiB stack (Runtime.cpp) and workers on 256 MiB
// (BigStackThread) — a headroom check fits both, where a fixed frame count
// cannot. We stop with X::Recursion while ~2 MiB of stack remains, so the throw
// unwinds cleanly instead of the process taking SIGSEGV/SIGBUS (the latter
// wedging kill-proof under Rosetta).
thread_local char* t_stackTop = nullptr;
thread_local size_t t_stackLimit = 0;
static size_t currentThreadStackSize() {
#if defined(_WIN32)
    ULONG_PTR low = 0, high = 0;
    ::GetCurrentThreadStackLimits(&low, &high); // Win8+: [low, high) is the committed+reserved stack
    size_t sz = (size_t)(high - low);
    return sz ? sz : (size_t(8) << 20);
#elif defined(__APPLE__)
    size_t sz = pthread_get_stacksize_np(pthread_self());
    return sz ? sz : (size_t(8) << 20);
#elif defined(__OpenBSD__)
    // OpenBSD has no pthread_getattr_np; pthread_stackseg_np fills a stack_t
    // whose ss_size is the thread's stack size (glibc extension by another name).
    stack_t ss; size_t sz = 0;
    if (pthread_stackseg_np(pthread_self(), &ss) == 0) sz = ss.ss_size;
    return sz ? sz : (size_t(8) << 20);
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    // These BSDs spell it pthread_attr_get_np, and the attr must be init'd first.
    pthread_attr_t attr; void* base = nullptr; size_t sz = 0;
    pthread_attr_init(&attr);
    if (pthread_attr_get_np(pthread_self(), &attr) == 0)
        pthread_attr_getstack(&attr, &base, &sz);
    pthread_attr_destroy(&attr);
    return sz ? sz : (size_t(8) << 20);
#else
    pthread_attr_t attr; void* base = nullptr; size_t sz = 0;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        pthread_attr_getstack(&attr, &base, &sz);
        pthread_attr_destroy(&attr);
    }
    return sz ? sz : (size_t(8) << 20);
#endif
}
struct DepthGuard {
    int& d;
    explicit DepthGuard(int& dd) : d(dd) {
        ++d;
        char probe;
        if (!t_stackTop) { t_stackTop = &probe; t_stackLimit = currentThreadStackSize(); }
        // used stack grows downward from the recorded top
        size_t used = (size_t)(t_stackTop - &probe);
        // Stop with ~2 MiB to spare — but on a SMALL stack (a foreign thread,
        // or a native-compiled main before the linker flag existed) a fixed
        // reserve would fire immediately; scale it down to a quarter of the
        // stack so tiny threads still get useful depth before the throw.
        size_t reserve = size_t(2) << 20;
        if (t_stackLimit < reserve * 4) reserve = t_stackLimit / 4;
        if (used + reserve >= t_stackLimit || d > 100000) { // hard frame backstop too
            --d; throw RakuError{Value::str("X::Recursion"), "Too many levels of recursion"};
        }
    }
    ~DepthGuard() { --d; }
};

static bool isJunction(const Value& v); // defined below

// Does this expression contain a literal `*` (Whatever) term — walking only
// through composable expression forms, never into variables or calls? This is
// the syntactic test Whatever-currying keys on: `(* * 2).floor` composes,
// `my $d = * * 2; $d.floor` does not.
bool Interpreter::exprHasWhateverLit(const Expr* e) {
    if (!e) return false;
    switch (e->kind) {
        case NK::Whatever: return true;
        case NK::Binary: {
            auto* b = static_cast<const Binary*>(e);
            return exprHasWhateverLit(b->lhs.get()) || exprHasWhateverLit(b->rhs.get());
        }
        case NK::Unary: return exprHasWhateverLit(static_cast<const Unary*>(e)->operand.get());
        case NK::MethodCall: return exprHasWhateverLit(static_cast<const MethodCall*>(e)->inv.get());
        case NK::Index: return exprHasWhateverLit(static_cast<const Index*>(e)->base.get());
        case NK::ChainExpr: {
            auto* c = static_cast<const ChainExpr*>(e);
            for (auto& o : c->operands) if (exprHasWhateverLit(o.get())) return true;
            return false;
        }
        case NK::ListExpr: {
            auto* l = static_cast<const ListExpr*>(e);
            if (l->items.size() != 1) return false; // only parenthesized grouping composes
            return exprHasWhateverLit(l->items[0].get());
        }
        default: return false;
    }
}

bool Interpreter::boolify(const Value& v) {
    // a Regex in boolean context matches the current topic `$_`
    // (`?$rx` / `if $rx` == `$_ ~~ $rx`) — URI::Encode leans on this to test
    // each char against an unreserved-set pattern.
    if (v.t == VT::Regex) {
        if (Value* topic = tctx_.cur ? tctx_.cur->find("$_") : nullptr)
            return regexMatch(topic->toStr(), v.s).truthy();
        return false;
    }
    if (v.t == VT::Object && v.obj && v.obj->cls) {
        if (Value* b = v.obj->cls->findMethod("Bool"))
            return invokeMethod(*b, v, {}).truthy();
        if (Value* br = v.obj->cls->findMethod("Bridge")) // a Real bridges to its numeric value
            return invokeMethod(*br, v, {}).truthy();
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
    if (it == builtins_.end()) {
        // not a builtin: a module-loaded (or otherwise env-bound) routine?
        Value* p = tctx_.cur ? tctx_.cur->find("&" + name) : nullptr;
        if (!p && global_) { auto g = global_->vars.find("&" + name); if (g != global_->vars.end()) p = &g->second; }
        if (p && p->t == VT::Code) return callCallable(*p, std::move(args));
        throw RakuError{Value::nil(), "Undefined routine '" + name + "'"};
    }
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

// what a MISSING (or deleted) element reads as: `is default(v)` beats the type default
static Value arrayMissingDefault(const Value& base);

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
    if (name == "$?FILE") return Value::str(srcFileAbs_.empty() ? srcFile_ : srcFileAbs_);
    if (name == "$*PROGRAM") { Value p = Value::str(srcFile_); p.hashKind = "IO"; return p; }
    if (name == "$*PROGRAM-NAME") return Value::str(srcFile_);
    if (name == "$*USAGE") { std::string u = mainUsage(); if (!u.empty() && u.back() == '\n') u.pop_back(); return Value::str(u); }
    if (name == "$*EXECUTABLE" || name == "$*EXECUTABLE-NAME") { Value p = Value::str(execPath_); p.hashKind = "IO"; return p; }
    if (name == "$*OUT" || name == "$*ERR" || name == "$*IN") { Value h = Value::makeHash(); h.hashKind = "FileHandle"; (*h.hash)["std"] = Value::str(name == "$*ERR" ? "err" : name == "$*IN" ? "in" : "out"); return h; }
    if (name == "$*DISTRO") { Value h = Value::makeHash(); h.hashKind = "Distro"; (*h.hash)["name"] = Value::str("macos"); return h; }
    if (name == "$*KERNEL") { Value h = Value::makeHash(); h.hashKind = "Kernel"; (*h.hash)["name"] = Value::str("darwin"); return h; }
    if (name == "$*VM")     { Value h = Value::makeHash(); h.hashKind = "VM";     (*h.hash)["name"] = Value::str("moar");   return h; }
    if (name == "$*SPEC") return Value::typeObj("IO::Spec::Unix");
    if (name == "$*PID") return Value::integer((long long)::getpid());
    if (name == "$*TZ") return Value::integer(tzOffsetDyn());
    if (name == "$*INIT-INSTANT") { Value v = Value::number(initInstant_); v.hashKind = "Instant"; return v; }
    if (name == "$*THREAD") { if (t_threadSelf.t == VT::Hash) return t_threadSelf; Value h = Value::makeHash(); h.hashKind = "Thread"; (*h.hash)["initial"] = Value::boolean(threadDepth_ == 0); (*h.hash)["id"] = Value::integer(1); return h; }
    if (name == "$*SCHEDULER") {
        if (tctx_.cur) if (Value* p = tctx_.cur->find("$*SCHEDULER")) return *p; // user-assigned wins
        return defaultScheduler_; // shared .hash: attr writes (uncaught_handler) persist
    }
    if (name == "$*TMPDIR") { const char* t = std::getenv("TMPDIR"); std::string d = (t && *t) ? t : "/tmp"; while (d.size() > 1 && d.back() == '/') d.pop_back(); Value p = Value::str(d); p.hashKind = "IO"; return p; }
    // anything else: resolve from the live env chain (covers %*ENV, $*REPO,
    // program-declared dynamics, $!, $/ — used by native codegen)
    if (tctx_.cur) if (Value* p = tctx_.cur->find(name)) return *p;
    if (global_) { auto it = global_->vars.find(name); if (it != global_->vars.end()) return it->second; }
    return Value::any();
}

// @a[i]:exists / %h<k>:delete / :k / :v / :kv / :p for native codegen — the
// static subset of the interpreter's adverbed indexing (no $var conditionals).
Value rtIndexAdverb(Value& base, const Value& keyIn, bool isHash, const std::string& adverb) {
    bool wantExists = false, negExists = false, wantDelete = false;
    bool kvF = false, pF = false, kF = false, vF = false;
    {
        std::string rest = adverb;
        while (!rest.empty()) {
            size_t c = rest.find(':');
            std::string part = c == std::string::npos ? rest : rest.substr(0, c);
            rest = c == std::string::npos ? "" : rest.substr(c + 1);
            bool neg = !part.empty() && part[0] == '!';
            if (neg) part = part.substr(1);
            if (part == "exists") { wantExists = true; negExists = neg; }
            else if (part == "delete") wantDelete = true;
            else if (part == "kv") kvF = true;
            else if (part == "p") pF = true;
            else if (part == "k") kF = true;
            else if (part == "v") vF = true;
        }
    }
    bool exists = false;
    Value val;
    std::string key;
    long long ai = 0;
    if (isHash) {
        key = keyIn.toStr();
        if (base.t == VT::Hash && base.hash) {
            auto it = base.hash->find(key);
            if (it != base.hash->end()) { exists = true; val = it->second; }
        }
    } else {
        ai = keyIn.toInt();
        if (base.t == VT::Array && base.arr) {
            if (ai < 0) ai += (long long)base.arr->size();
            if (ai >= 0 && ai < (long long)base.arr->size()) {
                const Value& v = (*base.arr)[ai];
                exists = !(v.t == VT::Nil || v.t == VT::Any || v.t == VT::Type);
                val = v;
            }
        }
    }
    Value keyV = isHash ? Value::str(key) : Value::integer(ai);
    if (wantDelete && exists) {
        if (isHash) base.hash->erase(key);
        else {
            (*base.arr)[ai] = Value::any();
            if (ai == (long long)base.arr->size() - 1) { // a trailing delete SHORTENS the array
                base.arr->pop_back();
                while (!base.arr->empty() &&
                       (base.arr->back().t == VT::Nil || base.arr->back().t == VT::Any))
                    base.arr->pop_back();
            }
        }
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
    if (exists) return val;
    // missing (or just-deleted) element: the container's default, when it has one
    Value dv = arrayMissingDefault(base);
    return dv.t == VT::Nil ? Value::any() : dv;
}

// $obj.accessor as an assignable slot (native codegen) — same semantics as the
// interpreter's MethodCall lvalue: FileHandle slots are free-form; a public
// attribute must be `is rw`; anything else is not assignable.
Value& Interpreter::accessorRef(Value& base, const std::string& name) {
    if (base.t == VT::Hash && base.hashKind == "FileHandle") {
        if (!base.hash) base.hash = std::make_shared<std::map<std::string, Value>>();
        return (*base.hash)[name];
    }
    if (base.t == VT::Object && base.obj) {
        for (ClassInfo* ci = base.obj->cls.get(); ci; ci = ci->parent.get())
            for (auto& at : ci->attrs)
                // public @./%. attrs assign through the accessor without `is rw`
                if (at.name == name &&
                    !(at.pub && (at.rw || at.sigil == '@' || at.sigil == '%')))
                    throw RakuError{Value::typeObj("X::Assignment::RO"),
                        "Cannot modify an immutable '" + name + "'"};
        return base.obj->attrs[name];
    }
    throw RakuError{Value::str("Cannot assign"), "Target is not assignable"};
}

// Assignable slot for a dynamic/special variable (native codegen): the existing
// binding if one is visible, else a fresh one in the global env.
Value& Interpreter::dynVarRef(const std::string& name) {
    if (tctx_.cur) if (Value* p = tctx_.cur->find(name)) return *p;
    global_->define(name, Value::any());
    return global_->vars[name];
}

// Value-level indexing for native codegen (no AST). Read returns Nil when absent.
// The default value for a missing element of a (possibly typed) container:
// a typed container answers its element type object, else Nil.
static Value typedElemDefault(const Value& base) {
    if (base.ofType.empty()) return Value::nil();
    std::string first = base.ofType.substr(0, base.ofType.find(','));
    if (first.empty()) return Value::nil();
    if (std::isupper((unsigned char)first[0])) return Value::typeObj(first);
    // native element types are zero-initialized (my int @a — gaps read as 0)
    if (first == "num" || first == "num32" || first == "num64") return Value::number(0.0);
    if (first == "str") return Value::str("");
    if (first.compare(0, 3, "int") == 0 || first.compare(0, 4, "uint") == 0 || first == "byte")
        return Value::integer(0);
    return Value::nil();
}
static Value arrayMissingDefault(const Value& base) {
    if (base.pairVal) return *base.pairVal; // `is default(v)`
    return typedElemDefault(base);
}

Value rtIndexGet(const Value& base, const Value& key, bool isHash) {
    // a Range/list key is a slice: `@a[1..3]` / `@a[1,3]` / `%h<a b>`
    if (key.t == VT::Range || (key.t == VT::Array && key.arr)) {
        Value out = Value::array(); out.isList = true;
        for (auto& k : key.flatten()) out.arr->push_back(rtIndexGet(base, k, isHash));
        return out;
    }
    if (isHash) {
        if ((base.t == VT::Hash || base.t == VT::Match) && base.hash) { // Match: named captures
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
    if (base.t == VT::Array && base.arr && base.ext) { // lazy seq: force elements up to i
        long long i = key.toInt();
        if (i >= 0) {
            auto st = std::static_pointer_cast<LazySeqState>(base.ext);
            const size_t CAP = 1000000;
            if (st->appendNext)
                while ((long long)base.arr->size() <= i && base.arr->size() < CAP && st->appendNext(*base.arr)) {}
        }
    }
    if ((base.t == VT::Array || base.t == VT::Match) && base.arr) { // Match: positional captures
        long long i = key.toInt(), n = (long long)base.arr->size();
        if (i < 0) i += n;
        if (i >= 0 && i < n) {
            const Value& v = (*base.arr)[i];
            // a deleted slot (undefined hole) in a typed/defaulted array reads as the default
            if (base.t == VT::Array && (v.t == VT::Nil || v.t == VT::Any) &&
                (base.pairVal || !base.ofType.empty()))
                return arrayMissingDefault(base);
            return v;
        }
    }
    return arrayMissingDefault(base);
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
        // Bool is an Int-backed enum, so `True ~~ Int` / nqp::istype(True, Int)
        // hold (CBOR::Simple classifies Bool via nqp::istype($_, Numeric)).
        case VT::Bool:    return type == "Bool" || type == "Int" ||
                                 type == "Numeric" || type == "Real";
        case VT::Array:
            // a Seq is an Array tagged `.s == "Seq"`; a plain Array/List is NOT a
            // Seq (JSON::Fast's jsonify tests `istype($_, Seq)` before Positional
            // — a false match sends it into an infinite `jsonify(.cache)` loop)
            if (type == "Seq") return v.s == "Seq";
            return type == "Array" || type == "List" || type == "Positional" || type == "Iterable";
        case VT::Hash:    return type == "Hash" || type == "Associative" || type == "Map";
        case VT::Code:    return type == "Code" || type == "Callable" || type == "Routine" || type == "Block";
        case VT::Object: {
            for (ClassInfo* c = v.obj && v.obj->cls ? v.obj->cls.get() : nullptr; c; c = c->parent.get())
                if (c->name == type) return true;
            // package-relative short name: `has Path $.path` accepts URI::Path
            const std::string& q = aliasType(type);
            if (&q != &type)
                for (ClassInfo* c = v.obj && v.obj->cls ? v.obj->cls.get() : nullptr; c; c = c->parent.get())
                    if (c->name == q) return true;
            return false;
        }
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
        // list-building ops over nothing build nothing: [Z] () / [Z~] () / [X] () are ()
        if (op == "Z" || op == "X" || op == "," ||
            (op.size() > 1 && (op[0] == 'Z' || op[0] == 'X'))) {
            Value o = Value::array(); o.isList = true; return o;
        }
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
    if (i >= (long long)base.arr->size())
        base.arr->resize(i + 1, base.ofType.empty() ? Value::any() : typedElemDefault(base));
    return (*base.arr)[i];
}

Value Interpreter::callCallable(const Value& codeVal, ValueList args, const std::vector<ExprPtr>* rwArgs, bool ownFrame, bool arityCheck) {
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
                try { r = callCallable(wraps[level], as, rwArgs, /*ownFrame=*/true); }
                catch (...) { redispatchStack_.pop_back(); throw; }
                redispatchStack_.pop_back();
                return r;
            };
        return runLevel((int)wraps.size() - 1, std::move(args));
    }
    return callCallableRaw(codeVal, std::move(args), rwArgs, ownFrame, arityCheck);
}

static bool ncIsFloatType(const std::string& t) {
    return t == "num" || t == "num32" || t == "num64" || t == "Num" ||
           t == "Rat" || t == "Real" || t == "double" || t == "Numeric";
}

// Over-wide fixed signatures for the FFI call. On the SysV-AMD64 and AArch64
// ABIs the integer and float register banks are independent and a callee simply
// ignores the argument registers it doesn't declare, so ONE 8-int + 8-float
// signature dispatches every call whose arguments fit in registers — including
// MIXED int/float, which the old per-arity dispatch rejected. Unused slots are
// padded with zero; args reorder into their bank in declaration order, which is
// exactly how the callee reads them.
typedef long   (*NcFnI)(long,long,long,long,long,long,long,long,
                        double,double,double,double,double,double,double,double);
typedef double (*NcFnD)(long,long,long,long,long,long,long,long,
                        double,double,double,double,double,double,double,double);
// native scalar type → byte width and signedness (for is-rw copy-back, cglobal,
// Pointer.deref). 0 ⇒ not a plain native scalar.
static int ncScalarWidth(const std::string& t, bool& sign, bool& isFloat) {
    isFloat = false; sign = true;
    if (t == "num32") { isFloat = true; return 4; }
    if (t == "num" || t == "num64" || t == "Num") { isFloat = true; return 8; }
    if (t == "int8"  || t == "char")   { return 1; }
    if (t == "uint8" || t == "byte")   { sign = false; return 1; }
    if (t == "int16")  return 2;
    if (t == "uint16") { sign = false; return 2; }
    if (t == "int32" || t == "long32") return 4;
    if (t == "uint32") { sign = false; return 4; }
    if (t == "int" || t == "int64" || t == "long" || t == "longlong" || t == "ssize_t" || t == "Int") return 8;
    if (t == "uint" || t == "uint64" || t == "ulong" || t == "ulonglong" || t == "size_t" || t == "bool" || t == "Bool") { sign = false; return 8; }
    return 0;
}

// element type inside `Pointer[T]` / `CArray[T]` ("" for a bare Pointer)
static std::string ncElemType(const std::string& t) {
    size_t b = t.find('[');
    return b == std::string::npos ? "" : t.substr(b + 1, t.size() - b - 2);
}
// A live Pointer holds { addr, of } — deref reads native memory of type `of`.
Value Interpreter::ncMakePointer(const std::string& type, void* p) {
    Value v = Value::makeHash(); v.hashKind = "Pointer";
    (*v.hash)["addr"] = Value::integer((long long)(intptr_t)p);
    (*v.hash)["of"]   = Value::str(ncElemType(type));
    return v;
}
// A live CArray[T] holds { addr, of } — element access reads native memory.
Value Interpreter::ncMakeLiveCArray(const std::string& type, void* p) {
    Value v = Value::makeHash(); v.hashKind = "CArray";
    (*v.hash)["addr"] = Value::integer((long long)(intptr_t)p);
    std::string of = ncElemType(type);
    (*v.hash)["of"]   = Value::str(of.empty() ? "int64" : of);
    return v;
}
// Read one native element at addr[index] as the given native type.
Value Interpreter::ncReadElem(long long addr, const std::string& ofType, long long index) {
    if (!addr) return Value::any();
    bool sgn, isFlt; int w = ncScalarWidth(ofType, sgn, isFlt);
    if (w == 0) w = 8; // pointer-sized default (Pointer[T], opaque)
    const char* base = (const char*)(intptr_t)addr + index * w;
    if (isFlt) {
        if (w == 4) { float x; std::memcpy(&x, base, 4); return Value::number((double)x); }
        double x; std::memcpy(&x, base, 8); return Value::number(x);
    }
    long long val = 0;
    switch (w) {
        case 1: { if (sgn) { int8_t x; std::memcpy(&x, base, 1); val = x; } else { uint8_t x; std::memcpy(&x, base, 1); val = x; } break; }
        case 2: { if (sgn) { int16_t x; std::memcpy(&x, base, 2); val = x; } else { uint16_t x; std::memcpy(&x, base, 2); val = x; } break; }
        case 4: { if (sgn) { int32_t x; std::memcpy(&x, base, 4); val = x; } else { uint32_t x; std::memcpy(&x, base, 4); val = x; } break; }
        default:{ std::memcpy(&val, base, 8); break; }
    }
    return Value::integer(val);
}
// Write one native scalar `val` at addr[index] of type ofType.
void Interpreter::ncWriteElem(long long addr, const std::string& ofType, long long index, const Value& val) {
    if (!addr) return;
    bool sgn, isFlt; int w = ncScalarWidth(ofType, sgn, isFlt); if (w == 0) w = 8;
    char* base = (char*)(intptr_t)addr + index * w;
    if (isFlt) {
        if (w == 4) { float x = (float)val.toNum(); std::memcpy(base, &x, 4); }
        else        { double x = val.toNum();       std::memcpy(base, &x, 8); }
        return;
    }
    long long v = val.t == VT::Object || val.t == VT::Hash ? Interpreter::ncRawAddr(val)
                : val.t == VT::Str ? (long long)(intptr_t)val.s.c_str() : val.toInt();
    switch (w) {
        case 1: { int8_t  x = (int8_t)v;  std::memcpy(base, &x, 1); break; }
        case 2: { int16_t x = (int16_t)v; std::memcpy(base, &x, 2); break; }
        case 4: { int32_t x = (int32_t)v; std::memcpy(base, &x, 4); break; }
        default:{ std::memcpy(base, &v, 8); break; }
    }
}
// C struct layout for a `repr('CStruct')` class: byte offset of `field` (with its
// type), plus the total padded struct size. Natural alignment (align == size,
// capped at 8); non-scalar fields (Str/Pointer/CArray/CStruct) are pointer-sized.
long long Interpreter::ncFieldOffset(ClassInfo* ci, const std::string& field, std::string& type) {
    long long off = 0;
    for (auto& a : ci->attrs) {
        bool sgn, isF; int w = ncScalarWidth(a.type, sgn, isF); if (w == 0) w = 8;
        off = (off + w - 1) / w * w;
        std::string an = a.name; if (!an.empty() && (an[0]=='$'||an[0]=='@'||an[0]=='%')) an = an.substr(1);
        if (!an.empty() && (an[0]=='!'||an[0]=='.')) an = an.substr(1);
        if (an == field) { type = a.type.empty() ? "int64" : a.type; return off; }
        off += w;
    }
    return -1;
}
long long Interpreter::ncStructSize(ClassInfo* ci) {
    long long off = 0, maxA = 1;
    for (auto& a : ci->attrs) {
        bool sgn, isF; int w = ncScalarWidth(a.type, sgn, isF); if (w == 0) w = 8;
        if (w > maxA) maxA = w;
        off = (off + w - 1) / w * w; off += w;
    }
    return off ? (off + maxA - 1) / maxA * maxA : 0;
}

// Extract a raw native address from any native-pointer value (0 if none).
long long Interpreter::ncRawAddr(const Value& v) {
    if (v.t == VT::Object && v.obj && v.obj->attrs.count("__native_ptr")) return v.obj->attrs.at("__native_ptr").toInt();
    if (v.t == VT::Hash && v.hash) { auto it = v.hash->find("addr"); if (it != v.hash->end()) return it->second.toInt(); }
    if (v.t == VT::Int) return v.i;
    return 0;
}

// ---- NativeCall callbacks --------------------------------------------------
// A Raku Callable passed to a native function is marshalled to a C function
// pointer via a fixed pool of trampolines. The C library calls the trampoline
// (with C-ABI integer/pointer args), which routes back into the interpreter.
// Synchronous callbacks (qsort/bsearch, invoked during the native call while the
// GIL is held) work; async ones (stored by C, fired later from another thread)
// are not supported. Pointer/int args reach the callback as Int addresses.
// (g_cbInterp is defined up near the other file-scope interpreter state so the
// constructor can set it.)
static std::vector<Value> g_cbSlots;   // slot → Raku Callable (never shrinks)

long Interpreter::runCallback(int slot, long a0, long a1, long a2, long a3, long a4, long a5) {
    if (slot < 0 || slot >= (int)g_cbSlots.size()) return 0;
    Value cb = g_cbSlots[slot];
    if (cb.t != VT::Code) return 0;
    long raw[6] = {a0, a1, a2, a3, a4, a5};
    size_t arity = (cb.code && cb.code->params) ? cb.code->params->size() : 2;
    if (arity > 6) arity = 6;
    ValueList as;
    for (size_t i = 0; i < arity; i++) {
        std::string pt = (cb.code && cb.code->params && i < cb.code->params->size()) ? (*cb.code->params)[i].type : "";
        if (pt == "Pointer" || pt.rfind("Pointer[", 0) == 0) as.push_back(ncMakePointer(pt, (void*)(intptr_t)raw[i]));
        else if (pt == "num" || pt == "num64" || pt == "num32") { double d; std::memcpy(&d, &raw[i], 8); as.push_back(Value::number(d)); }
        else as.push_back(Value::integer(raw[i]));
    }
    Value r;
    try { r = callCallable(cb, as); } catch (...) { return 0; }
    return (long)r.toInt();
}

template<int N> static long cbTramp(long a, long b, long c, long d, long e, long f) {
    return g_cbInterp ? g_cbInterp->runCallback(N, a, b, c, d, e, f) : 0;
}
template<int... Is> static void cbFill(void** t, std::integer_sequence<int, Is...>) {
    ((t[Is] = (void*)&cbTramp<Is>), ...);
}
static void* g_cbTable[64];
static bool g_cbTableInit = [] { cbFill(g_cbTable, std::make_integer_sequence<int, 64>{}); return true; }();

// NativeCall (`is native`): resolve the C symbol via dlsym and call it. Arguments
// and the return are classified by their *declared* type — integer/pointer
// (`Str`→`char*`, the `int*`/`uint*`/`size_t`/`bool`/`Pointer`/CArray/CStruct
// family → 64-bit) or floating-point (`num`/`num32`/`num64`). Mixed int+float
// args, `is rw` out-params (marshalled as `T*` with copy-back), and CStruct/
// CPointer/CArray/Pointer returns are handled. Not supported: by-value C structs,
// callbacks, and calls needing more than 8 integer or 8 float register args.
Value Interpreter::callNative(Callable& c, ValueList& args, const std::vector<ExprPtr>* rwArgs) {
    void* handle = RTLD_DEFAULT;
    // `is native(&sub)`: call the sub now to get the library path (a full path or
    // bare name). Its result is cached on the Callable so we resolve it once.
    std::string lib = c.nativeLib;
    if (lib.empty() && !c.nativeLibSub.empty()) {
        if (Value* f = tctx_.cur->find("&" + c.nativeLibSub)) {
            ValueList none; Value r = callCallable(*f, none);
            lib = r.toStr();
        }
    }
    if (!lib.empty()) {
        // try the name as-is, then platform-decorated forms
        const std::string& l = lib;
        std::string dlerr;
        for (const std::string& cand : {l, "lib" + l + ".dylib", "lib" + l + ".so", l + ".dylib", l + ".so"}) {
            if ((handle = dlopen(cand.c_str(), RTLD_LAZY | RTLD_GLOBAL))) break;
            if (const char* e = dlerror()) { // keep the first failure, but an arch mismatch wins (it's the useful one)
                std::string es = e;
                if (dlerr.empty() || es.find("architecture") != std::string::npos) dlerr = es;
            }
        }
        if (!handle) throw RakuError{Value::typeObj("X::Libc"),
            "Cannot load native library '" + lib + "'" + (dlerr.empty() ? "" : ": " + dlerr)};
    }
    void* sym = dlsym(handle, c.nativeSym.c_str());
    if (!sym) {
        // Some libraries expose a renamed symbol behind a compat macro the header
        // resolves at C compile time but that isn't a real exported symbol (so a
        // module's `is native` on the old name can't dlsym it). Fall back to the
        // known aliases — e.g. OpenSSL 3.x's SSL_get_peer_certificate is now only
        // SSL_get1_peer_certificate.
        static const std::map<std::string, std::string> aliases = {
            {"SSL_get_peer_certificate", "SSL_get1_peer_certificate"},
        };
        auto it = aliases.find(c.nativeSym);
        if (it != aliases.end()) sym = dlsym(handle, it->second.c_str());
    }
    if (!sym) throw RakuError{Value::typeObj("X::AdHoc"), "Cannot find native symbol '" + c.nativeSym + "'"};

    const std::vector<Param>* prm = c.params;
    std::vector<std::string> keep; keep.reserve(args.size()); // keep Str buffers alive across the call
    std::vector<long>   g;  // integer/pointer args, in declaration order
    std::vector<double> f;  // float args, in declaration order
    // is-rw out-params: backing slots (stable addresses via deque) + copy-back list
    std::deque<long>   rwI;
    std::deque<double> rwD;
    struct RwBack { size_t arg; const long* i; const double* d; };
    std::vector<RwBack> rwbacks;
    struct CABack { size_t arg; size_t keep; }; // byte-backed CArray → copy bytes back
    std::vector<CABack> cabacks;

    for (size_t i = 0; i < args.size(); i++) {
        Value& v = args[i];
        const Param* p = (prm && i < prm->size()) ? &(*prm)[i] : nullptr;
        std::string pt = p ? p->type : "";
        bool sgn, isFlt; int w = ncScalarWidth(pt, sgn, isFlt);
        bool fp = isFlt || (pt.empty() && (v.t == VT::Num || v.t == VT::Rat));
        if (p && p->isRw && w) { // `is rw` scalar → pass a pointer to a backing slot
            if (isFlt) { rwD.push_back(v.toNum()); g.push_back((long)(intptr_t)&rwD.back()); rwbacks.push_back({i, nullptr, &rwD.back()}); }
            else       { rwI.push_back(v.toInt()); g.push_back((long)(intptr_t)&rwI.back()); rwbacks.push_back({i, &rwI.back(), nullptr}); }
        }
        else if (v.t == VT::Str && v.hashKind == "CArray") { keep.push_back(v.s); g.push_back((long)(intptr_t)keep.back().data()); cabacks.push_back({i, keep.size() - 1}); }
        else if (v.t == VT::Str && (v.hashKind == "Buf" || v.hashKind == "Blob")) {
            // a Buf/blob8 is a mutable native buffer: pass its bytes and copy back
            // after the call (BIO_read/SSL_read/recv fill it in place).
            keep.push_back(v.s); g.push_back((long)(intptr_t)keep.back().data());
            if (v.hashKind == "Buf") cabacks.push_back({i, keep.size() - 1});
        }
        else if (v.t == VT::Str || (v.t == VT::Hash && v.hashKind == "IO")) { keep.push_back(v.toStr()); g.push_back((long)(intptr_t)keep.back().c_str()); }
        else if (v.t == VT::Object && v.obj && v.obj->attrs.count("__native_ptr")) g.push_back((long)v.obj->attrs["__native_ptr"].toInt());
        else if (v.t == VT::Hash && (v.hashKind == "Pointer" || v.hashKind == "CArray") && v.hash->count("addr"))
            g.push_back((long)(*v.hash)["addr"].toInt()); // live Pointer / CArray handle
        else if (v.t == VT::Code) { // Raku callback → C function pointer (trampoline)
            int slot = -1;
            for (size_t s = 0; s < g_cbSlots.size(); s++) if (g_cbSlots[s].code == v.code) { slot = (int)s; break; }
            if (slot < 0 && g_cbSlots.size() < 64) { slot = (int)g_cbSlots.size(); g_cbSlots.push_back(v); }
            g.push_back(slot >= 0 ? (long)(intptr_t)g_cbTable[slot] : 0);
        }
        else if (fp) f.push_back(v.toNum());
        else         g.push_back(v.toInt());
    }
    if (g.size() > 8 || f.size() > 8)
        throw RakuError{Value::typeObj("X::NYI"), "NativeCall: too many register arguments (max 8 integer + 8 float)"};

    long G[8] = {0}; for (size_t k = 0; k < g.size(); k++) G[k] = g[k];
    double D[8] = {0}; for (size_t k = 0; k < f.size(); k++) D[k] = f[k];

    const std::string& rt = c.retType;
    bool retFP = ncIsFloatType(rt);
    long ri = 0; double rd = 0;
    if (retFP) rd = ((NcFnD)sym)(G[0],G[1],G[2],G[3],G[4],G[5],G[6],G[7], D[0],D[1],D[2],D[3],D[4],D[5],D[6],D[7]);
    else       ri = ((NcFnI)sym)(G[0],G[1],G[2],G[3],G[4],G[5],G[6],G[7], D[0],D[1],D[2],D[3],D[4],D[5],D[6],D[7]);

    // is-rw copy-back: write each out-param's slot to the caller's lvalue
    for (auto& rb : rwbacks) {
        if (!rwArgs || rb.arg >= rwArgs->size()) continue;
        Value nv = rb.i ? Value::integer(*rb.i) : Value::number(*rb.d);
        try {
            if (Value* lv = lvalue((*rwArgs)[rb.arg].get())) {
                int nb = lv->natBits; bool ns = lv->natSigned;
                *lv = nv;
                if (nb) wrapNative(*lv, nb, ns);
            }
        } catch (RakuError&) {}
    }
    // CArray copy-back: a native function may mutate the array in place (qsort,
    // fill buffers), so write the possibly-changed bytes back to the caller.
    for (auto& cb : cabacks) {
        if (!rwArgs || cb.arg >= rwArgs->size()) continue;
        try { if (Value* lv = lvalue((*rwArgs)[cb.arg].get()))
                  if (lv->t == VT::Str && (lv->hashKind == "CArray" || lv->hashKind == "Buf")) lv->s = keep[cb.keep];
        } catch (RakuError&) {}
    }

    if (rt.empty() || rt == "void" || rt == "Nil") return Value::nil();
    if (rt == "Str") return Value::str(ri ? std::string((const char*)(intptr_t)ri) : "");
    if (retFP) return Value::number(rd);
    if (rt == "bool" || rt == "Bool") return Value::boolean(ri != 0);
    // Pointer / CArray return: box the raw pointer in a live-pointer value whose
    // element/deref access reads native memory (see ncMakeLivePointer/CArray).
    if (rt == "Pointer" || rt.rfind("Pointer[", 0) == 0)
        return ncMakePointer(rt, (void*)(intptr_t)ri);
    if (rt == "CArray" || rt.rfind("CArray[", 0) == 0)
        return ncMakeLiveCArray(rt, (void*)(intptr_t)ri);
    // CStruct/CPointer return: box the pointer as an object of the return class so
    // it satisfies the type check and round-trips into later native calls.
    if (!rt.empty() && (std::isupper((unsigned char)rt[0]) || rt.find("::") != std::string::npos)) {
        std::shared_ptr<ClassInfo> ci;
        auto it = classes_.find(rt);
        if (it != classes_.end()) ci = it->second;
        else for (auto& kv : classes_) { // match a qualified name ending in ::rt
            const std::string& nm = kv.first;
            if (nm.size() > rt.size() + 2 && nm.compare(nm.size() - rt.size() - 2, rt.size() + 2, "::" + rt) == 0) { ci = kv.second; break; }
        }
        if (ci) {
            Value o; o.t = VT::Object; o.obj = std::make_shared<ObjectData>();
            o.obj->cls = ci; o.obj->attrs["__native_ptr"] = Value::integer(ri);
            return o;
        }
    }
    // Integer return: the value comes back in a 64-bit register but a narrower
    // return type (int32/uint16/…) only fills the low bits — truncate and
    // sign/zero-extend to the DECLARED width, or a returned int32 -1 reads back as
    // 0xFFFFFFFF (4294967295) and `$rc < 0` never holds (BIO_read's would-block).
    {
        bool rsgn, rflt; int rw = ncScalarWidth(rt, rsgn, rflt);
        if (rw > 0 && rw < 8 && !rflt) {
            unsigned long long mask = (1ULL << (rw * 8)) - 1;
            unsigned long long u = (unsigned long long)ri & mask;
            if (rsgn && (u & (1ULL << (rw * 8 - 1)))) ri = (long)(long long)(u | ~mask);
            else ri = (long)u;
        }
    }
    return Value::integer(ri);
}

// cglobal(lib, symbol, Type): resolve a C global variable's address via dlsym and
// read its current value per the declared type (Pointer types return a live
// Pointer; scalar types return the value).
Value Interpreter::cglobal(const std::string& lib, const std::string& sym, const std::string& type) {
    void* handle = RTLD_DEFAULT;
    if (!lib.empty()) {
        std::string dlerr;
        for (const std::string& cand : {lib, "lib" + lib + ".dylib", "lib" + lib + ".so", lib + ".dylib", lib + ".so"}) {
            if ((handle = dlopen(cand.c_str(), RTLD_LAZY | RTLD_GLOBAL))) break;
            if (const char* e = dlerror()) { // keep the first failure, but an arch mismatch wins (it's the useful one)
                std::string es = e;
                if (dlerr.empty() || es.find("architecture") != std::string::npos) dlerr = es;
            }
        }
        if (!handle) throw RakuError{Value::typeObj("X::Libc"),
            "Cannot load native library '" + lib + "'" + (dlerr.empty() ? "" : ": " + dlerr)};
    }
    void* addr = dlsym(handle, sym.c_str());
    if (!addr) throw RakuError{Value::typeObj("X::AdHoc"), "Cannot find native symbol '" + sym + "'"};
    if (type == "Pointer" || type.rfind("Pointer[", 0) == 0) {
        void* p = *(void**)addr; // the global holds a pointer
        return ncMakePointer(type, p);
    }
    return ncReadElem((long long)(intptr_t)addr, type, 0);
}

// run `let` restorations for the current env — only on UNSUCCESSFUL exits
static void runLetRestoresOf(const std::shared_ptr<Env>& e) {
    if (!e || e->letRestores.empty()) return;
    for (auto it = e->letRestores.rbegin(); it != e->letRestores.rend(); ++it) (*it)();
    e->letRestores.clear();
}

Value Interpreter::callCallableRaw(const Value& codeVal, ValueList args, const std::vector<ExprPtr>* rwArgs, bool ownFrame, bool arityCheck) {
    // callsame/nextsame are ROUTINE-scoped: a routine activation sees only
    // redispatch frames pushed for it (ownFrame) or by its own body. Blocks
    // inherit the enclosing routine's floor.
    struct FloorGuard {
        ExecContext& t; size_t saved; bool active;
        ~FloorGuard() { if (active) t.redispatchFloor = saved; }
    } floorG{tctx_, tctx_.redispatchFloor, false};
    if (codeVal.t == VT::Code && codeVal.code && !codeVal.code->isBlock) {
        floorG.active = true;
        tctx_.redispatchFloor = redispatchStack_.size() - (ownFrame && !redispatchStack_.empty() ? 1 : 0);
    }
    Value* topicWB = topicWriteback_; topicWriteback_ = nullptr; // one-shot, consumed here
    const std::vector<Value*>* rwSlots = pendingRwSlots_; pendingRwSlots_ = nullptr; // one-shot too
    bool noAT = noAutothread_; noAutothread_ = false; // one-shot too (Junction.THREAD)
    int lpc = loopPhaserCtl_; loopPhaserCtl_ = 0; // one-shot: loop-phaser control from an iterating driver (map)
    bool implicitTopic_local = false;
    // A Format template (q:o/…/) is callable: it applies as sprintf over the args.
    if (codeVal.t == VT::Code && codeVal.code && codeVal.code->isNative) return callNative(*codeVal.code, args, rwArgs);
    if (codeVal.t == VT::Hash && codeVal.hashKind == "Format") {
        std::string fmt = codeVal.hash && codeVal.hash->count("fmt") ? (*codeVal.hash)["fmt"].toStr() : "";
        return Value::str(doSprintf(fmt, args));
    }
    if (isJunction(codeVal)) { // a junction of callables autothreads the invocation
        Value out = Value::array(); out.enumName = codeVal.enumName; out.isList = true;
        out.arr = std::make_shared<ValueList>();
        for (auto& e : *codeVal.arr) { ValueList a2 = args; out.arr->push_back(callCallable(e, a2)); }
        return out;
    }
    // A Junction ARGUMENT autothreads the call — recursively, so nested
    // junctions thread down to their leaves — unless the parameter's type
    // accepts a Junction (Mu / Junction). `(-> Any $x { @e.push: $x }).($j)`
    // visits every leaf eigenstate (S03-junctions/associative.t).
    if (!noAT && codeVal.t == VT::Code && codeVal.code && !codeVal.code->builtin &&
        codeVal.code->params && !codeVal.code->isMultiDispatcher) {
        const auto& ps = *codeVal.code->params;
        for (size_t ai = 0; ai < args.size(); ai++) {
            if (!isJunction(args[ai])) continue;
            const Param* p = nullptr; size_t seen = 0;
            for (auto& pp : ps) {
                if (pp.named) continue;
                if (pp.slurpy) { p = &pp; break; }
                if (seen == ai) { p = &pp; break; }
                seen++;
            }
            if (p && (p->type == "Mu" || p->type == "Junction" || p->slurpy)) continue;
            Value out = Value::array(); out.enumName = args[ai].enumName;
            for (auto& e : *args[ai].arr) {
                ValueList a2 = args; a2[ai] = e;
                out.arr->push_back(callCallable(codeVal, a2, rwArgs));
            }
            return out;
        }
    }
    if (codeVal.t != VT::Code || !codeVal.code) {
        // an object (or type object) with a CALL-ME method is invokable: $obj(…)
        if (codeVal.t == VT::Object && codeVal.obj && codeVal.obj->cls) {
            if (Value* cm = codeVal.obj->cls->findMethod("CALL-ME"))
                return invokeMethod(*cm, codeVal, std::move(args));
        }
        if (codeVal.t == VT::Type) {
            auto cit = classes_.find(codeVal.s);
            if (cit != classes_.end())
                if (Value* cm = cit->second->findMethod("CALL-ME"))
                    return invokeMethod(*cm, codeVal, std::move(args));
        }
        throw RakuError{Value::str("Not callable"), "Cannot invoke non-Callable value of type " + codeVal.typeName()};
    }
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
                if (cand.code && cand.code->isProto) continue; // the proto defines the group; it is not a candidate
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
                // no candidate takes the Junction itself — autothread over it
                // (recursively, so `mstest(1&2 | 3)` threads down to the leaves)
                for (size_t ai = 0; ai < as.size(); ai++) {
                    if (!isJunction(as[ai])) continue;
                    Value out = Value::array(); out.enumName = as[ai].enumName;
                    for (auto& e : *as[ai].arr) {
                        ValueList a2 = as; a2[ai] = e;
                        out.arr->push_back(callCallable(codeVal, a2, rwArgs));
                    }
                    return out;
                }
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
            try { r = callCallable(*best, as, rwArgs, /*ownFrame=*/true); }
            catch (...) { redispatchStack_.pop_back(); throw; }
            redispatchStack_.pop_back();
            return r;
        };
        return dispatch(args);
    }
    // `&infix:<=>` / `&infix:<+=>` / `&infix:<~=>` … as a first-class Callable: an
    // assignment operator needs an l-value first operand, which only the call-site
    // AST (rwArgs) can supply — the builtin lambda only sees values.
    if (c.builtin && rwArgs && !rwArgs->empty() && !args.empty() &&
        c.name.rfind("infix:<", 0) == 0 && c.name.back() == '>') {
        std::string op = c.name.substr(7, c.name.size() - 8);
        bool isAssign = op == "=" ||
            (op.size() >= 2 && op.back() == '=' && op != "==" && op != "!=" &&
             op != "<=" && op != ">=" && op != "=:=" && op != "!==" && op != ".=");
        if (isAssign) {
            if (Value* lv = lvalue((*rwArgs)[0].get())) {
                Value rhs = args.size() > 1 ? args[1] : Value::any();
                *lv = (op == "=") ? rhs : applyBinOp(op.substr(0, op.size() - 1), *lv, rhs);
                return *lv;
            }
        }
    }
    if (c.builtin) return c.builtin(*this, args);

    // Implicit @_ is a flattening slurpy (*@_): list-flavored args (Seq/List
    // tuples, e.g. from X/Z) spread one level; itemized [..] arrays stay nested.
    auto slurpyArgs = [](const ValueList& as) {
        ValueList out;
        for (auto& a : as) {
            if (a.t == VT::Array && a.isList && a.arr)
                out.insert(out.end(), a.arr->begin(), a.arr->end());
            else out.push_back(a);
        }
        return out;
    };

    auto env = std::make_shared<Env>();
    // Break the leak cycle a nested named sub would form (see breakSelfClosures).
    // The guard fires only when hoistSubs (below) reported a nested sub, and
    // destructs before `env` does, on every exit path. `env` is a live local, so
    // the reference stays valid — no extra shared_ptr copy on the hot path.
    bool hasNestedSub = false;
    struct CycleBreaker {
        Interpreter* self; std::shared_ptr<Env>& env; bool& active;
        ~CycleBreaker() { if (active && env) self->breakSelfClosures(env.get()); }
    } cycleBreaker{this, env, hasNestedSub};
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
    // Arity enforcement for NAMED plain user subs (Rakudo rejects these at
    // compile time; we reject at call time with the same message shape).
    // Blocks, methods, multis/protos and builtins keep their lax binding.
    if (arityCheck && c.hadSig &&
        !c.name.empty() && !c.isMethod && !c.isProto && !c.isMultiDispatcher &&
        !c.isWhateverCode && !c.isNative && !c.builtin && !c.hasPrimed &&
        c.placeholders.empty() && !c.usesArgs) { // `sub f { @_ }` slurps implicitly
        bool unbounded = false;
        int maxPos = 0, reqPos = 0;
        if (c.params)
            for (auto& p : *c.params) {
                if (p.invocant || p.named) continue;
                if (p.slurpy || p.sigil == '|' || p.sigil == '\\') { unbounded = true; continue; }
                // an @-positional historically absorbs capture-interpolated
                // argument lists (foo(|$c) with sub foo(@arr) — roast-blessed),
                // and the interpolation is invisible after flattening: treat it
                // as unbounded for the too-many test (still required below)
                if (p.sigil == '@') unbounded = true;
                maxPos++;
                if (!p.optional && !p.defaultVal && !p.litVal) reqPos++;
            }
        // Pairs are ambiguous at this level (quoted-key pairs bind
        // positionally; capture-flattened named args LOSE the namedArg bit),
        // so the too-many test counts only non-Pair positionals and the
        // too-few test credits every pair — reject only what can never fit.
        int posStrict = 0, pairish = 0;
        for (auto& a : args) { if (a.t == VT::Pair) pairish++; else posStrict++; }
        if (posStrict + pairish < reqPos || (!unbounded && posStrict > maxPos)) {
            std::string prof, sigt;
            for (auto& a : args) {
                if (isNamedArg(a)) continue;
                if (!prof.empty()) prof += ", ";
                prof += a.typeName();
            }
            if (c.params)
                for (auto& p : *c.params) {
                    if (p.invocant) continue;
                    if (!sigt.empty()) sigt += ", ";
                    sigt += (p.slurpy ? "*" : "") + std::string(p.named ? ":" : "") +
                            p.name + (p.optional && !p.named ? "?" : "");
                }
            throw RakuError{Value::typeObj("X::Signature::ArityMismatch"),
                "Calling " + c.name + "(" + prof + ") will never work with "
                "declared signature (" + sigt + ")"};
        }
    }
    if (c.params && !c.params->empty()) {
        bindParams(*c.params, args, env, c.isMethod);
        if (rwArgs) setupRwLinks(c.params, env, rwArgs); // rw/raw params write through
        if (rwSlots) setupRwSlots(c.params, env, rwSlots); // hyper element slots
    } else if (!c.placeholders.empty()) {
        implicitTopic_local = false;
        // $:name placeholders bind from :name(…) pair args; only when they are
        // present do Pair args stop binding positionally (a block mapping over
        // %h.pairs still receives each Pair as $^p)
        bool namedPh = false;
        for (auto& pn : c.placeholders) if (pn.size() > 2 && pn[1] == ':') { namedPh = true; break; }
        size_t pos = 0;
        for (size_t k = 0; k < c.placeholders.size(); k++) {
            const std::string& pn = c.placeholders[k];
            Value v = Value::any();
            if (namedPh && pn.size() > 2 && pn[1] == ':') {
                std::string key = pn.substr(2);
                for (auto& a : args) if (a.t == VT::Pair && a.s == key && a.pairVal) { v = *a.pairVal; break; }
            } else if (namedPh) {
                while (pos < args.size() && args[pos].t == VT::Pair) pos++;
                if (pos < args.size()) v = args[pos++];
            } else {
                if (k < args.size()) v = args[k];
            }
            env->define(pn, v);
            // $^foo / $:foo is also visible as $foo within the block
            if (pn.size() > 2 && (pn[1] == '^' || pn[1] == ':')) env->define(std::string(1, pn[0]) + pn.substr(2), v);
        }
        env->define("@_", Value::array(slurpyArgs(args)));
        implicitTopic_local = false; // placeholders bound: no implicit topic
    } else {
        implicitTopic_local = true;
        // implicit $_ / @_ — NB: no implicit $a/$b (Perl-5 style sort vars are
        // invalid Raku; defining them here shadowed legitimate outer $a/$b in
        // every paramless block: `map {$_+$a}, @k` doubled elements)
        // a zero-arg BLOCK call inherits the topic lexically (Rakudo's implicit
        // block signature is `$_? is raw = OUTER::<$_>`): define NO local $_, so
        // reads AND writes fall through the closure chain to the outer $_ —
        // `for <a> { lives-ok { $_.foo } }` sees the loop topic inside the inner
        // block, at zero per-call cost. Routines still get a fresh $_.
        if (!(args.empty() && c.isBlock))
            env->define("$_", args.empty() ? Value::any() : args[0]);
        // a bare block invoked with no args (a gather/do wrapper) leaves @_
        // unbound, so an inner for-body's @_ can synthesize from its own topic
        if (!(c.isBlock && args.empty()))
            env->define("@_", Value::array(slurpyArgs(args)));
    }
    auto saved = tctx_.cur;
    Env* savedState = tctx_.curStateEnv;
    tctx_.cur = env;
    tctx_.curStateEnv = c.stateEnv.get();
    tctx_.dynStack.push_back(saved ? saved.get() : global_.get()); // caller's scope, for dynamic $*var lookup
    // restore() puts the caller's scope back; called on every exit path. (ENTER
    // phasers now run inside the try, and the CATCH body is wrapped, so a throw
    // from either restores instead of leaking dynStack / bleeding scope.)
    // &?BLOCK / &?ROUTINE resolve lazily via these frame pointers (no per-call defines)
    struct MagicalGuard {
        ExecContext& t; const Value* savedB; const Value* savedR;
        MagicalGuard(ExecContext& tc, const Value* cv, bool routine)
            : t(tc), savedB(tc.curBlockVal), savedR(tc.curRoutineVal) {
            t.curBlockVal = cv;
            if (routine) t.curRoutineVal = cv;
        }
        ~MagicalGuard() { t.curBlockVal = savedB; t.curRoutineVal = savedR; }
    } magicalGuard{tctx_, &codeVal, !c.isBlock};
    auto restore = [&] {
        // a mutated implicit $_ flows back to the caller's element (grep/map aliasing)
        if (topicWB && implicitTopic_local) {
            auto it = env->vars.find("$_");
            if (it != env->vars.end()) *topicWB = it->second;
        }
        tctx_.cur = saved; tctx_.curStateEnv = savedState; tctx_.dynStack.pop_back();
    };
    // cooperative-return frame bookkeeping: every callable bumps frameTop;
    // a ROUTINE (not a bare block) is a return boundary
    ++tctx_.frameTop;
    uint64_t savedFrameTop = tctx_.frameTop, savedRoutineFrame = tctx_.curRoutineFrame;
    bool isRoutine = !c.isBlock;
    if (isRoutine) { tctx_.curRoutineFrame = tctx_.frameTop; env->routineFrame = true; } // $/ scopes here
    struct FrameGuard {
        ExecContext& t; uint64_t ft, rf;
        ~FrameGuard() { t.frameTop = ft - 1; t.curRoutineFrame = rf; }
    } fguard{tctx_, savedFrameTop, savedRoutineFrame};
    Value last = Value::any();
    if (c.body) hasNestedSub = hoistSubs(*c.body); // nested named subs are visible throughout the body
    if (c.body) hoistExprDecls(*c.body, tctx_.cur.get()); // `my` buried in ternary/nqp branches → routine scope
    // an inline CATCH {} anywhere in the body handles exceptions from the whole block
    Block* catchBlk = nullptr;
    if (c.body) for (auto& s : *c.body)
        if (s->kind == NK::Block && static_cast<Block*>(s.get())->isCatch) catchBlk = static_cast<Block*>(s.get());
    try {
        // Loop-phaser control from an iterating driver (.map over a block with
        // FIRST/NEXT/LAST): FIRST fires only on the first iteration (and must not
        // be re-run as an ENTER-alike), NEXT after each body, LAST after the final
        // one — all in THIS invocation's env, so block params ($c) are visible.
        if (c.body && lpc) {
            bool savedSF = suppressLoopFirst_; suppressLoopFirst_ = true;
            runEnterPhasers(*c.body); // ENTER only; FIRST suppressed
            suppressLoopFirst_ = savedSF;
            if (lpc & 1) runFirstPhasers(*c.body);
        }
        else if (c.body) runEnterPhasers(*c.body);
        if (c.body) {
            size_t nst = c.body->size();
            // The statement whose value becomes the routine result — trailing
            // phasers / CATCH blocks don't count; everything before it runs in
            // sink context, so a discarded object with a `sink` method has it
            // called (Rakudo semantics). One reverse scan, ~free for short bodies.
            size_t lastReal = nst;
            for (size_t k = nst; k-- > 0; ) {
                auto* s = (*c.body)[k].get();
                if (isBlockPhaser(s)) continue;
                if (s->kind == NK::Block && static_cast<Block*>(s)->isCatch) continue;
                lastReal = k; break;
            }
            for (size_t i = 0; i < nst; i++) {
                auto* s = (*c.body)[i].get();
                if (isBlockPhaser(s)) continue;
                if (s->kind == NK::Block && static_cast<Block*>(s)->isCatch) continue;
                // Tail-position `return X` yields exactly X as the call result — evaluate
                // it directly instead of throwing+unwinding a ReturnEx (the hot path for
                // the many one-line accessor/action methods a grammar parse calls).
                // ROUTINES only: in a bare Block, `return` belongs to the enclosing
                // routine (or is an error without one) — it must take the throw path.
                if (i + 1 == nst && s->kind == NK::ReturnStmt && isRoutine) {
                    auto* r = static_cast<ReturnStmt*>(s);
                    last = r->value ? eval(r->value.get()) : Value::any();
                } else
                    last = exec(s, i != lastReal); // non-final statements sink
                if (tctx_.returning) { // cooperative return reached this routine
                    if (isRoutine) { tctx_.returning = false; last = std::move(tctx_.returnV); }
                    break; // a bare block propagates the flag to its routine
                }
            }
            if (lpc && !tctx_.returning) { // driver-run loop phasers, in this invocation's env
                if (lpc & 4) runNextPhasers(*c.body, env);
                if (lpc & 2) runLastPhasers(*c.body);
            }
        }
    } catch (ReturnEx& r) {
        if (c.body) runLeavePhasers(*c.body);
        restore();
        // `return` returns from the innermost enclosing ROUTINE, not from a bare
        // Block: a block (e.g. `-> $x, $y {…}` passed to reduce/map) lets it fly
        // through — aborting the C++ HOF loop — to the caller's routine frame.
        // With no enclosing routine at all it is the spec'd control-flow error.
        if (!isRoutine) {
            if (savedRoutineFrame == 0)
                throw RakuError{Value::typeObj("X::ControlFlow::Return"),
                                "Attempt to return outside of any Routine"};
            throw;
        }
        copyOutRw(c.params, env, rwArgs, false);
        return c.retType.empty() ? std::move(r.v) : checkRetType(c, std::move(r.v));
    } catch (BreakGivenEx& b) { // `when` matched in this block: its value is the block's result
        if (c.body) runLeavePhasers(*c.body);
        restore();
        copyOutRw(c.params, env, rwArgs, false);
        return b.hasVal ? b.v : last;
    } catch (RakuError& e) {
        if (catchBlk) {
            tctx_.cur->define("$_", exceptionFor(e));
            tctx_.cur->define("$!", exceptionFor(e));
            bool matched = false;
            try {
                struct G { int& d; G(int& x) : d(x) { d++; } ~G() { d--; } } g{catchDepth_};
                for (auto& s : catchBlk->stmts) exec(s.get());
            } catch (BreakGivenEx&) { matched = true; /* a when/default matched */ }
            catch (ResumeEx&) { matched = true; /* .resume: absorbed; body can't re-enter, treat as handled */ }
            catch (...) { if (c.body) runLeavePhasers(*c.body); restore(); throw; } // die/rethrow from CATCH
            // Only a matching when/default handles the exception (R1): a CATCH
            // whose clauses matched none — or with no clauses at all — rethrows.
            if (!matched) {
                if (c.body) runLeavePhasers(*c.body);
                runLetRestoresOf(tctx_.cur);
                tctx_.cur = saved; tctx_.curStateEnv = savedState; tctx_.dynStack.pop_back();
                throw;
            }
            if (c.body) runLeavePhasers(*c.body);
            restore();
            copyOutRw(c.params, env, rwArgs, false);
            // A `return` inside the CATCH returns from this routine (R2).
            if (isRoutine && tctx_.returning) { tctx_.returning = false; return std::move(tctx_.returnV); }
            return Value::nil();
        }
        if (c.body) runLeavePhasers(*c.body);
        runLetRestoresOf(tctx_.cur); // unsuccessful exit
        tctx_.cur = saved; tctx_.curStateEnv = savedState; tctx_.dynStack.pop_back();
        throw;
    } catch (LeaveEx& le) {
        if (c.body) runLeavePhasers(*c.body);
        tctx_.cur = saved; tctx_.curStateEnv = savedState; tctx_.dynStack.pop_back();
        return le.hasVal ? le.v : Value::nil(); // `leave` exits this block with its value
    } catch (...) {
        if (c.body) runLeavePhasers(*c.body);
        runLetRestoresOf(tctx_.cur); // unsuccessful exit
        tctx_.cur = saved; tctx_.curStateEnv = savedState; tctx_.dynStack.pop_back();
        throw;
    }
    if (c.body) runLeavePhasers(*c.body);
    tctx_.cur = saved; tctx_.curStateEnv = savedState; tctx_.dynStack.pop_back();
    // a mutated implicit $_ flows back to the caller's element (grep/map aliasing)
    if (topicWB && implicitTopic_local) {
        auto it = env->vars.find("$_");
        if (it != env->vars.end()) *topicWB = it->second;
    }
    copyOutRw(c.params, env, rwArgs, false);
    return c.retType.empty() ? std::move(last) : checkRetType(c, std::move(last));
}

// Enforce a routine's declared nominal return type on its result value.
Value Interpreter::checkRetType(const Callable& c, Value v) {
    if (c.retType.empty()) return v;
    static const std::set<std::string> kRet = {
        "Int", "Num", "Rat", "Complex", "Str", "Bool", "Junction"};
    // a return type that names NOTHING known is an undeclared symbol
    // (checked before the definedness bail: it fires even for empty bodies)
    if (!kRet.count(c.retType) && !classes_.count(c.retType) &&
        !subsets_.count(c.retType) && !isKnownTypeName(c.retType))
        throwTyped("X::Undeclared",
                   {{"what", "Type"}, {"symbol", c.retType}},
                   "Type '" + c.retType + "' is not declared");
    if (!isDefined(v)) return v;
    if (kRet.count(c.retType) && !rtTypeMatch(v, c.retType) &&
        !(c.retType == "Int" && v.t == VT::Bool))
        throwTypedV("X::TypeCheck::Return",
                    {{"got", v}, {"expected", Value::typeObj(c.retType)}},
                    "Type check failed for return value; expected " + c.retType +
                    " but got " + v.typeName() + " (" + v.gist() + ")");
    return v;
}

// Copy `is rw` parameter final values back to the caller's argument lvalues.
void Interpreter::copyOutRw(const std::vector<Param>* params, std::shared_ptr<Env>& env,
                            const std::vector<ExprPtr>* rwArgs, bool /*methodCtx*/) {
    if (!params || !rwArgs) return;
    // `is rw` params write back explicitly; a sigilless capture param (`\a`) binds
    // the caller's container, so an assignment through it must write back too.
    // Assignments already went through IMMEDIATELY via rwWriteThrough; this is the
    // backstop for mutations that bypass it (s///, .=, ++ on odd paths). A param
    // whose value matches rwSynced (the bound/last-pushed value) is SKIPPED —
    // copying it back late would re-apply a stale value over the callee's own
    // edits, and lvalue() on an untouched arg can autovivify a missing path.
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
                auto sy = env->rwSynced.find(p.name);
                bool unchanged = sy != env->rwSynced.end() && valueEqv(it->second, sy->second);
                if (!unchanged)
                    try { if (Value* lv = lvalue((*rwArgs)[pi].get())) *lv = it->second; } catch (...) {}
            }
        }
        pi++;
    }
}

// Record write-through links for rw/raw params at bind time (mirrors copyOutRw's
// positional indexing). Called while tctx_.cur is still the CALLER's scope.
void Interpreter::setupRwLinks(const std::vector<Param>* params, std::shared_ptr<Env>& env,
                               const std::vector<ExprPtr>* rwArgs) {
    if (!params || !rwArgs) return;
    size_t pi = 0;
    for (auto& p : *params) {
        if (p.named) continue;
        if (p.slurpy) break;
        if ((p.isRw || p.sigil == '\\') && pi < rwArgs->size()) {
            env->rwLinks[p.name] = { (*rwArgs)[pi].get(), tctx_.cur };
            auto it = env->vars.find(p.name);
            env->rwSynced[p.name] = it != env->vars.end() ? it->second : Value::any();
            anyRwLinks_ = true;
        }
        pi++;
    }
}

// Bind hyper element slots: like setupRwLinks but the caller supplies container
// slots DIRECTLY (positional, aligned with the params); a null slot means the
// argument was immutable — assigning that param dies.
void Interpreter::setupRwSlots(const std::vector<Param>* params, std::shared_ptr<Env>& env,
                               const std::vector<Value*>* slots) {
    if (!params || !slots) return;
    size_t pi = 0;
    for (auto& p : *params) {
        if (p.named) continue;
        if (p.slurpy) break;
        if ((p.isRw || p.sigil == '\\') && pi < slots->size()) {
            if (Value* s = (*slots)[pi]) {
                env->rwDirect[p.name] = s;
                auto it = env->vars.find(p.name);
                env->rwSynced[p.name] = it != env->vars.end() ? it->second : Value::any();
            }
            else env->rwDead.insert(p.name);
            anyRwLinks_ = true;
        }
        pi++;
    }
}

// After a mutation through a variable target, push the new value through the
// caller's argument expression if the variable is a linked rw/raw param.
void Interpreter::rwWriteThrough(Expr* target) {
    if (!target) return;
    std::string name;
    if (target->kind == NK::VarExpr) name = static_cast<VarExpr*>(target)->name;
    else if (target->kind == NK::NameTerm) name = static_cast<NameTerm*>(target)->name;
    else return;
    Env* e = tctx_.cur.get();
    while (e && !e->vars.count(name)) e = e->parent.get();
    if (!e) return;
    if (e->rwDead.count(name))
        throw RakuError{Value::typeObj("X::Assignment::RO"),
                        "Cannot modify an immutable value"};
    auto dit = e->rwDirect.find(name);
    if (dit != e->rwDirect.end()) {
        Value v = e->vars[name];
        *dit->second = v;
        e->rwSynced[name] = v;
        return;
    }
    if (e->rwLinks.empty()) return;
    auto it = e->rwLinks.find(name);
    if (it == e->rwLinks.end()) return;
    Value v = e->vars[name];
    auto savedCur = tctx_.cur;
    tctx_.cur = it->second.second; // the caller's scope, where the arg expr lives
    try { if (Value* lv = lvalue(it->second.first)) *lv = v; } catch (...) {}
    tctx_.cur = savedCur;
    e->rwSynced[name] = v;
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
    try { r = invokeMethod(*um, self, args, rwArgs, /*ownFrame=*/true); }
    catch (...) { redispatchStack_.pop_back(); throw; }
    redispatchStack_.pop_back();
    return r;
}

Value Interpreter::invokeMethod(const Value& codeVal, const Value& self, ValueList args, const std::vector<ExprPtr>* rwArgs, bool ownFrame) {
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
                if (cand.code && cand.code->isProto) continue; // the proto defines the group; it is not a candidate
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
                // no candidate takes the Junction itself — autothread over it
                for (size_t ai = 0; ai < as.size(); ai++) {
                    if (!isJunction(as[ai])) continue;
                    Value out = Value::array(); out.enumName = as[ai].enumName;
                    for (auto& e : *as[ai].arr) {
                        ValueList a2 = as; a2[ai] = e;
                        out.arr->push_back(invokeMethod(dispatcherVal, selfCopy, a2, rwArgs));
                    }
                    return out;
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
            try { r = invokeMethod(*best, selfCopy, as, rwArgs, /*ownFrame=*/true); }
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
    // `state` vars in a method body (or a for-body executed inline within it)
    // live in the method's own persistent env, spliced into the lookup chain —
    // exactly like callClosure. Without this, curStateEnv stays the CALLER's:
    // writes land in a foreign frame's stateEnv that the method's chain never
    // sees (Cro.compose's `++state $split` → "Variable '$split' is not
    // declared" when called from inside Cro::HTTP::Server.new).
    std::call_once(c.stateInit, [&] {
        c.stateEnv = std::make_shared<Env>();
        c.stateEnv->parent = c.closure ? c.closure : global_;
    });
    env->parent = c.stateEnv;
    env->define("self", self);
    if (c.params && !c.params->empty()) {
        bindParams(*c.params, args, env, /*methodCtx=*/true);
        if (rwArgs) setupRwLinks(c.params, env, rwArgs); // rw/raw params write through
    }
    else if (!c.placeholders.empty()) {
        for (size_t k = 0; k < c.placeholders.size(); k++) {
            const std::string& pn = c.placeholders[k];
            Value v = Value::any();
            if (pn.size() > 2 && pn[1] == ':') { // $:name — from :name(…) pair args
                std::string key = pn.substr(2);
                for (auto& a : args) if (a.t == VT::Pair && a.s == key && a.pairVal) { v = *a.pairVal; break; }
            } else if (k < args.size()) v = args[k];
            env->define(pn, v);
            if (pn.size() > 2 && (pn[1] == '^' || pn[1] == ':')) env->define(std::string(1, pn[0]) + pn.substr(2), v);
        }
        env->define("@_", Value::array(args));
    } else env->define("@_", Value::array(args));
    auto saved = tctx_.cur;
    tctx_.cur = env;
    // state declarations in this method's body must write to ITS stateEnv (the
    // one spliced into env->parent above), not the caller's; RAII restores on
    // every exit path
    Env* savedStateEnv = tctx_.curStateEnv;
    tctx_.curStateEnv = c.stateEnv.get();
    struct StateGuard { ExecContext& t; Env* s; ~StateGuard() { t.curStateEnv = s; } } stG{tctx_, savedStateEnv};
    // a method frame is a dynamic-scope boundary like a sub frame: push the
    // CALLER's env so `$*CRO-ROUTE-SET`-style dynamics set in a calling sub stay
    // visible through method calls (Cro's route -> definition-complete -> plugin)
    tctx_.dynStack.push_back(saved ? saved.get() : global_.get());
    struct DynGuard { ExecContext& t; ~DynGuard() { t.dynStack.pop_back(); } } dynG{tctx_};
    // callsame/nextsame scope: this activation sees only frames pushed FOR it
    // (ownFrame) — never the caller's (a frameless bottom method must not
    // re-enter its caller's chain; that was an infinite nextsame loop)
    size_t savedFloor = tctx_.redispatchFloor;
    tctx_.redispatchFloor = redispatchStack_.size() - (ownFrame && !redispatchStack_.empty() ? 1 : 0);
    struct FloorGuard2 { ExecContext& t; size_t s; ~FloorGuard2() { t.redispatchFloor = s; } } floorG2{tctx_, savedFloor};
    // A method is a routine boundary for cooperative return, exactly like a sub:
    // establish a frame so a `return` inside a loop/native block in the body unwinds
    // to here instead of leaking the `returning` flag past the loop (mirrors callCallable).
    ++tctx_.frameTop;
    uint64_t savedFrameTop = tctx_.frameTop, savedRoutineFrame = tctx_.curRoutineFrame;
    tctx_.curRoutineFrame = tctx_.frameTop;
    struct FrameGuard {
        ExecContext& t; uint64_t ft, rf;
        ~FrameGuard() { t.frameTop = ft - 1; t.curRoutineFrame = rf; }
    } fguard{tctx_, savedFrameTop, savedRoutineFrame};
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
                if (tctx_.returning) { // cooperative return reached this method boundary
                    tctx_.returning = false; last = std::move(tctx_.returnV);
                    break;
                }
            }
        }
    } catch (ReturnEx& r) { tctx_.cur = saved; copyOutRw(c.params, env, rwArgs, true); return r.v; }
    catch (BreakGivenEx& b) {
        // a matched `when` in the method body: the routine is its topicalizer,
        // so it exits the method with the when-block's value (mirrors callCallable)
        tctx_.cur = saved; copyOutRw(c.params, env, rwArgs, true);
        return b.hasVal ? b.v : last;
    }
    catch (...) { tctx_.cur = saved; throw; }
    tctx_.cur = saved;
    copyOutRw(c.params, env, rwArgs, true);
    return last;
}

Value Interpreter::evalInterp(InterpStr* s) {
    std::string out;
    for (auto& p : s->parts) out += strOf(eval(p.get())); // honour user `method Str`/`gist`
    return Value::str(nfcNormalize(out)); // NFG: combining marks compose across part boundaries
}

Value* Interpreter::lvalue(Expr* e) {
    if (e->kind == NK::VarExpr) {
        auto* ve = static_cast<VarExpr*>(e);
        char sigil = ve->name.empty() ? '$' : ve->name[0];
        // process-immutable dynamics; a user `my $*PID` shadow stays assignable
        if (!ve->declare && (ve->name == "$*PID" || ve->name == "$*EXECUTABLE" ||
                             ve->name == "$*EXECUTABLE-NAME") && !tctx_.cur->find(ve->name))
            throw RakuError{Value::typeObj("X::Assignment::RO"),
                            "Cannot modify an immutable value (" + ve->name + ")"};

        if (ve->declare) {
            if (ve->declScope == "state" && tctx_.curStateEnv) { // persistent across calls
                if (!tctx_.curStateEnv->vars.count(ve->name)) tctx_.curStateEnv->define(ve->name, typedDefault(ve->declType, sigil));
                return &tctx_.curStateEnv->vars[ve->name];
            }
            Value init = typedDefault(ve->declType, sigil);
            if (ve->declShape && sigil == '@') // shaped array `my @a[2;3]`
                init = makeShapedContainer(evalShapeDims(ve->declShape.get()), ve->declType);
            if (!ve->containerIs.empty() && sigil == '%') { // my %h is Set — an empty Setty
                init = makeBaggy({}, ve->containerIs);
                init.ofType = ve->containerOf; // `is Bag[Int]` keys on Int
            }
            if (ve->declDefault) { // `is default(v)`: initial AND reset value
                Value dv = eval(ve->declDefault.get());
                if (sigil == '@' || sigil == '%') // container stays empty; v is the ELEMENT default
                    init.pairVal = std::make_shared<Value>(dv);
                else { init = dv; tctx_.cur->varDefault[ve->name] = dv; }
            }
            else if (sigil == '$' && !ve->declType.empty() && std::isupper((unsigned char)ve->declType[0]))
                tctx_.cur->varDefault[ve->name] = Value::typeObj(ve->declType); // `$x = Nil` resets to (Type)
            tctx_.cur->define(ve->name, std::move(init));
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
        // &?BLOCK / &?ROUTINE: not real slots — nothing assignable here
        // (reads go through the VarExpr eval path)
        // inside a method, a bare `@something` may be the twigil-less attribute
        // `has @something` — resolve it against the invocant's declared attrs
        if (ve->name.size() > 1) {
            if (Value* selfp = tctx_.cur->find("self")) {
                if (selfp->t == VT::Object && selfp->obj && selfp->obj->cls) {
                    std::string an = ve->name.substr(1);
                    for (auto& at : selfp->obj->cls->attrs)
                        if (at.name == an && at.sigil == ve->name[0])
                            return &selfp->obj->attrs[an];
                }
            }
        }
        if (!isSpecialVar(ve->name) && !noStrict_)
            throw RakuError{Value::typeObj("X::Undeclared"),
                            "Variable '" + ve->name + "' is not declared"};
        tctx_.cur->define(ve->name, defaultFor(sigil));
        return &tctx_.cur->vars[ve->name];
    }
    if (e->kind == NK::SymbolicRef) {
        auto* sr = static_cast<SymbolicRef*>(e);
        std::string nm = symRefName(sr);
        if (nm.empty())
            throw RakuError{Value::typeObj("X::NoSuchSymbol"), "Cannot look up empty name"};
        VarExpr tmp(nm); tmp.line = e->line;
        return lvalue(&tmp);
    }
    if (e->kind == NK::Assign) {
        // `(my $a = …)<key> = v` — a parenthesized assignment is an lvalue via its target
        auto* a = static_cast<Assign*>(e);
        evalAssign(a);
        if (a->target->kind == NK::VarExpr) {
            // answer the just-assigned slot directly: recursing into a declaring
            // VarExpr would re-declare it and wipe the value
            auto* ve = static_cast<VarExpr*>(a->target.get());
            if (Value* p = tctx_.cur->find(ve->name)) return p;
        }
        return lvalue(a->target.get());
    }
    if (e->kind == NK::Index) {
        auto* idx = static_cast<Index*>(e);
        Value* base;
        if (idx->base->kind == NK::Call) {
            // `foo()[$i] = v` — index into a call's result; the returned Value
            // shares its arr/hash with the real container, so writes stick
            static thread_local Value callBaseHold;
            callBaseHold = eval(idx->base.get());
            base = &callBaseHold;
        }
        else base = lvalue(idx->base.get());
        // assignment to an adverbed multidim subscript (`%h{a;b;c}:!exists = v`)
        // has no postcircumfix candidate — it dies
        if (idx->multiDim && !idx->adverb.empty())
            throw RakuError{Value::typeObj("X::Adverb"),
                "Cannot assign to an adverbed multidim subscript"};
        if (idx->multiDim && base) { // @a[0;1] = v / %h{a;b;c} = v — walk/autoviv nested containers
            auto* dims = static_cast<ListExpr*>(idx->index.get());
            Value* node = base;
            bool hashy = idx->isHash;
            auto shape = base->shape; // fixed-dimension array bounds (if any)
            size_t d = 0;
            for (auto& de : dims->items) {
                if (hashy || node->t == VT::Hash) {
                    if (node->t != VT::Hash) *node = Value::makeHash();
                    std::string key = eval(de.get()).toStr();
                    node = &(*node->hash)[key];
                    continue;
                }
                if (node->t != VT::Array) *node = Value::array();
                Value kv = eval(de.get());
                if (kv.t == VT::Code && kv.code && kv.code->isWhateverCode) // @a[*-1;…] = v
                    kv = callCallable(kv, ValueList{Value::integer((long long)node->arr->size())});
                long long i = kv.toInt();
                if (i < 0) i += (long long)node->arr->size();
                // a shaped array's dimensions are FIXED — an out-of-range index dies
                // rather than growing the array (`my @a[2;2]; @a[1;2] = 5` throws)
                if (shape && d < shape->size() && (i < 0 || i >= (*shape)[d]))
                    throw RakuError{Value::typeObj("X::OutOfRange"),
                        "Index " + std::to_string(kv.toInt()) + " out of range for dimension " +
                        std::to_string(d) + " (0.." + std::to_string((*shape)[d] - 1) + ")"};
                if (i < 0) i = 0;
                while ((long long)node->arr->size() <= i)
                    node->arr->push_back(node->ofType.empty() ? Value::any() : typedElemDefault(*node));
                node = &(*node->arr)[i];
                d++;
            }
            return node;
        }
        if (idx->isHash) {
            // autovivifying an undefined Set/Bag/Mix through a subscript dies too
            if (base->t == VT::Type &&
                (base->s == "Set" || base->s == "Bag" || base->s == "Mix"))
                throw RakuError{Value::typeObj("X::Assignment::RO"),
                    "Cannot modify an immutable " + base->s};
            // Set/Bag/Mix are immutable — element assignment dies (the *Hash variants mutate)
            if (base->t == VT::Hash &&
                (base->hashKind == "Set" || base->hashKind == "Bag" || base->hashKind == "Mix"))
                throw RakuError{Value::typeObj("X::Assignment::RO"),
                    "Cannot modify an immutable " + base->hashKind + " (" + base->gist() + ")"};
            if (base->t != VT::Hash) *base = Value::makeHash();
            std::string key = eval(idx->index.get()).toStr();
            return &(*base->hash)[key];
        } else {
            // a List ((1,3,5) held in a scalar) is immutable — element assignment dies
            if (base->t == VT::Array && base->isList && base->s != "Seq" && base->enumName.empty())
                throw RakuError{Value::typeObj("X::Assignment::RO"),
                    "Cannot modify an immutable List (" + base->gist() + ")"};
            if (base->t != VT::Array) *base = Value::array();
            // `@a[*-1] = v` / `@a[*-1]++`: a WhateverCode index resolves against the
            // current length (like the read path), not eagerly to 0.
            Value keyV = eval(idx->index.get());
            if (keyV.t == VT::Code && keyV.code && keyV.code->isWhateverCode)
                keyV = callCallable(keyV, ValueList{Value::integer((long long)base->arr->size())});
            long long i = keyV.toInt();
            if (i < 0) i += (long long)base->arr->size();
            if (i < 0) i = 0;
            while ((long long)base->arr->size() <= i)
                base->arr->push_back(base->ofType.empty() ? Value::any() : typedElemDefault(*base));
            return &(*base->arr)[i];
        }
    }
    // method-call lvalue: $obj.accessor = value  (rw accessors)
    if (e->kind == NK::MethodCall) {
        auto* mc = static_cast<MethodCall*>(e);
        // Pair.value is a writable container ($p.value = 5, .value-- in loops);
        // pairVal is shared between pair copies so mutation is visible everywhere
        if (mc->method == "value" && mc->args.empty() && !mc->meta && !mc->hyper) {
            Value* base = nullptr;
            try { base = lvalue(mc->inv.get()); } catch (RakuError&) {}
            if (base && base->t == VT::Pair && base->pairVal) return base->pairVal.get();
        }
        // container-access methods as l-values: `%h.AT-KEY("k") = v`,
        // `@a.AT-POS(i) = v`, and multidim `@a.AT-POS(i, j) = v`
        if ((mc->method == "AT-KEY" || mc->method == "AT-POS") && !mc->args.empty() && !mc->meta) {
            Value* base = lvalue(mc->inv.get());
            if (mc->method == "AT-KEY") {
                if (base->t != VT::Hash) *base = Value::makeHash();
                return &(*base->hash)[eval(mc->args[0].get()).toStr()];
            }
            Value* cur = base;
            for (auto& a : mc->args) {
                if (cur->t != VT::Array) *cur = Value::array();
                long long i = eval(a.get()).toInt();
                if (i < 0) i += (long long)cur->arr->size();
                if (i < 0) i = 0;
                while ((long long)cur->arr->size() <= i) cur->arr->push_back(Value::any());
                cur = &(*cur->arr)[i];
            }
            return cur;
        }
        // dynamic handle attribute: `$*OUT.out-buffer = 0` — the dynamic var has no
        // container (lvalue would throw); hold the evaluated handle so the slot
        // pointer stays valid across the assignment
        if (mc->inv->kind == NK::VarExpr &&
            static_cast<VarExpr*>(mc->inv.get())->name.compare(0, 2, "$*") == 0) {
            Value hv = eval(mc->inv.get());
            if (hv.t == VT::Hash && (hv.hashKind == "FileHandle" || hv.hashKind == "Scheduler") && hv.hash) {
                static thread_local Value dynHandleHold;
                dynHandleHold = hv; // shares .hash with the persistent handle
                return &(*dynHandleHold.hash)[mc->method];
            }
        }
        Value* base = lvalue(mc->inv.get());
        if (base->t == VT::Hash && (base->hashKind == "FileHandle" || base->hashKind == "Scheduler")) {
            if (!base->hash) base->hash = std::make_shared<std::map<std::string, Value>>();
            return &(*base->hash)[mc->method];
        }
        if (base->t == VT::Object && base->obj) {
            // Assigning to `$obj.attr` is only allowed through a PUBLIC `is rw`
            // accessor. If the name matches any other attribute — a private
            // `$!x`, or a public read-only `$.x` — the value is read-only, so
            // reject it rather than writing straight into the (private) slot.
            // Exceptions kept writable: `$obj!attr` (explicit private-access
            // syntax — self/trusts writes), and a name matching NO attribute
            // (a plain method call; `is rw`/`return-rw` lvalue methods rely on it).
            if (!mc->bang)
                for (ClassInfo* ci = base->obj->cls.get(); ci; ci = ci->parent.get())
                    for (auto& at : ci->attrs)
                        // a public @./%. attr is assignable through its accessor even
                        // without `is rw` (list-assign replaces the container's
                        // contents — Cro: `.body-parsers = @!body-parsers`); only
                        // $-attrs need the rw trait
                        if (at.name == mc->method &&
                            !(at.pub && (at.rw || at.sigil == '@' || at.sigil == '%')))
                            throw RakuError{Value::typeObj("X::Assignment::RO"),
                                "Cannot modify an immutable '" + mc->method + "'"};
            return &base->obj->attrs[mc->method];
        }
    }
    // `$(expr)` as an assignment target: the itemization wrapper is transparent
    // for the container — assign into the inner slot
    if (e->kind == NK::Unary && static_cast<Unary*>(e)->op == "ctx$")
        return lvalue(static_cast<Unary*>(e)->operand.get());
    // `self{$k} = v` / `self[$i] = v` inside a method mutates the invocant
    if (e->kind == NK::SelfTerm) {
        if (Value* p = tctx_.cur->find("self")) return p;
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
    // `my @a is List` makes the container immutable — reassigning it throws (the
    // declaration's own initialiser, declare=true, still runs; only later `@a = …` dies)
    if (a->op == "=" && a->target->kind == NK::VarExpr) {
        auto* ve = static_cast<VarExpr*>(a->target.get());
        if (!ve->declare) {
            Value* cur = tctx_.cur->find(ve->name);
            if (cur && cur->readonly && cur->t == VT::Array)
                throw RakuError{Value::typeObj("X::Assignment::RO"),
                                "Cannot modify an immutable List"};
        }
    }
    Value r = evalAssignInner(a, sink);
    if (a->target->kind == NK::VarExpr) {
        auto* ve = static_cast<VarExpr*>(a->target.get());
        if (ve->declare && ve->containerIs == "List")
            if (Value* cur = tctx_.cur->find(ve->name)) cur->readonly = true;
        // `our $x = v` anywhere publishes the initialized value to the package
        // (GLOBAL) stash — `$OUR::x` / `$GLOBAL::x` find it from other scopes
        if (ve->declare && ve->declScope == "our" && ve->name.size() > 1) {
            if (Value* p = tctx_.cur->find(ve->name)) {
                std::string qual = ve->name.substr(0, 1) + tctx_.pkgPrefix + ve->name.substr(1);
                noteSymbolMutation("our-declaration publish");
                global_->define(qual, *p);
            }
        }
    }
    // one hook covers every assignment form (=, op=, ||= …): if the target is a
    // linked rw/raw param, push the new value through to the caller immediately
    if (anyRwLinks_) rwWriteThrough(a->target.get());
    return r;
}

Value Interpreter::evalAssignInner(Assign* a, bool sink) {

    // NativeCall CStruct field write: `$s.field = v` writes native memory at the
    // field's offset (there is no Value container to hand back as an lvalue).
    if (a->op == "=" && a->target->kind == NK::MethodCall) {
        auto* mc = static_cast<MethodCall*>(a->target.get());
        if (mc->args.empty() && !mc->meta && !mc->hyper && mc->inv->kind == NK::VarExpr) {
            Value inv = eval(mc->inv.get());
            if (inv.t == VT::Object && inv.obj && inv.obj->cls &&
                (inv.obj->cls->repr == "CStruct" || inv.obj->cls->repr == "CPPStruct") &&
                inv.obj->attrs.count("__native_ptr") && !inv.obj->cls->findMethod(mc->method)) {
                std::string type; long long off = ncFieldOffset(inv.obj->cls.get(), mc->method, type);
                if (off >= 0) {
                    Value rhs = eval(a->value.get());
                    ncWriteElem(inv.obj->attrs["__native_ptr"].toInt() + off, type, 0, rhs);
                    return sink ? Value::any() : rhs;
                }
            }
        }
    }

    // named destructuring: `my (:@positional, :@named) := %h` — each element
    // binds the RHS hash's value under its bare name (Cro::HTTP::Router
    // classifies signature params this way)
    if ((a->op == "=" || a->op == ":=") && a->target->kind == NK::ListExpr) {
        auto* lst0 = static_cast<ListExpr*>(a->target.get());
        bool anyNamed = false;
        for (auto& it : lst0->items)
            if (it->kind == NK::VarExpr && static_cast<VarExpr*>(it.get())->namedBind) { anyNamed = true; break; }
        if (anyNamed) {
            Value rhs = eval(a->value.get());
            for (auto& it : lst0->items) {
                if (it->kind != NK::VarExpr) continue;
                auto* ve = static_cast<VarExpr*>(it.get());
                std::string bare = ve->name.size() > 1 ? ve->name.substr(1) : ve->name;
                Value v = Value::any();
                if (rhs.t == VT::Hash && rhs.hash) {
                    auto hit = rhs.hash->find(bare);
                    if (hit != rhs.hash->end()) v = hit->second;
                }
                Value* lv = lvalue(it.get());
                if (ve->name[0] == '@') *lv = coerceArray(v);
                else if (ve->name[0] == '%') *lv = coerceHash(v);
                else *lv = v;
            }
            return sink ? Value::any() : rhs;
        }
    }
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
            size_t vi = 0; // value cursor (a slurpy @/% target consumes the rest)
            for (size_t i = 0; i < L->items.size(); i++) {
                Expr* tgt = L->items[i].get();
                if (tgt->kind == NK::Whatever) { vi++; continue; } // `(*, $a) = …` skips a value
                if (tgt->kind == NK::VarExpr) {
                    const std::string& nm = static_cast<VarExpr*>(tgt)->name;
                    if (nm.size() >= 1 && (nm[0] == '@' || nm[0] == '%')) {
                        // an @/% target slurps every remaining value; later targets get Any
                        Value rest = Value::array();
                        for (size_t j = vi; j < vals.size(); j++) rest.arr->push_back(vals[j]);
                        vi = vals.size();
                        Value* lv = lvalue(tgt);
                        if (nm[0] == '%') { rest.isList = true; *lv = coerceHash(rest); }
                        else *lv = rest;
                        continue;
                    }
                }
                Value v = (vi < vals.size()) ? vals[vi] : Value::any();
                vi++;
                if (tgt->kind == NK::ListExpr) bind(static_cast<ListExpr*>(tgt), v);
                else { Value* lv = lvalue(tgt); *lv = v; }
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

    // `$buf.subbuf-rw(from, len) = $repl` / `subbuf-rw($buf, from, len) = $repl`
    // — splice the replacement bytes over [from, from+len) in place
    if (a->op == "=") {
        Expr* invE = nullptr; std::vector<ExprPtr>* sbArgs = nullptr; size_t argOfs = 0;
        if (a->target->kind == NK::MethodCall &&
            static_cast<MethodCall*>(a->target.get())->method == "subbuf-rw") {
            auto* mc = static_cast<MethodCall*>(a->target.get());
            invE = mc->inv.get(); sbArgs = &mc->args;
        }
        else if (a->target->kind == NK::Call &&
                 static_cast<Call*>(a->target.get())->name == "subbuf-rw" &&
                 !static_cast<Call*>(a->target.get())->args.empty()) {
            auto* c = static_cast<Call*>(a->target.get());
            invE = c->args[0].get(); sbArgs = &c->args; argOfs = 1;
        }
        if (invE) {
            Value* bp = lvalue(invE);
            if (bp && bp->t == VT::Str && (bp->hashKind == "Buf" || bp->hashKind == "Blob")) {
                long long n = (long long)bp->s.size();
                long long from = sbArgs->size() > argOfs ? eval((*sbArgs)[argOfs].get()).toInt() : 0;
                long long len  = sbArgs->size() > argOfs + 1 ? eval((*sbArgs)[argOfs + 1].get()).toInt() : n - from;
                if (from < 0) from += n;
                if (from < 0) from = 0; if (from > n) from = n;
                if (len < 0) len = 0; if (from + len > n) len = n - from;
                Value rhs = eval(a->value.get());
                std::string repl = rhs.t == VT::Str ? rhs.s : std::string();
                if (rhs.t != VT::Str) for (auto& b : rhs.flatten()) repl += (char)(unsigned char)b.toInt();
                bp->s = bp->s.substr(0, (size_t)from) + repl + bp->s.substr((size_t)(from + len));
                return sink ? Value::any() : rhs;
            }
            if (bp && bp->t == VT::Array && bp->arr) {
                long long n = (long long)bp->arr->size();
                long long from = sbArgs->size() > argOfs ? eval((*sbArgs)[argOfs].get()).toInt() : 0;
                long long len  = sbArgs->size() > argOfs + 1 ? eval((*sbArgs)[argOfs + 1].get()).toInt() : n - from;
                if (from < 0) from += n;
                if (from < 0) from = 0; if (from > n) from = n;
                if (len < 0) len = 0; if (from + len > n) len = n - from;
                Value rhs = eval(a->value.get());
                ValueList repl = (rhs.t == VT::Array && rhs.arr) ? *rhs.arr : rhs.flatten();
                bp->arr->erase(bp->arr->begin() + from, bp->arr->begin() + from + len);
                bp->arr->insert(bp->arr->begin() + from, repl.begin(), repl.end());
                return sink ? Value::any() : rhs;
            }
        }
    }
    if (a->op == "=" &&
        (a->target->kind == NK::IntLit || a->target->kind == NK::NumLit ||
         a->target->kind == NK::StrLit))
        throwTyped("X::Assignment::RO", {},
                   "Cannot modify an immutable value");
    if (a->op == ":=") {
        // Perl-6-level bind diagnostics (roast S32-exceptions/misc2.t)
        if (a->target->kind == NK::Call || a->target->kind == NK::MethodCall)
            throwTyped("X::Bind", {{"target", "a call"}},
                       "Cannot use bind operator with this left-hand side");
        if (a->target->kind == NK::NameTerm)
            throwTyped("X::Bind",
                       {{"target", static_cast<NameTerm*>(a->target.get())->name}},
                       "Cannot bind to '" +
                       static_cast<NameTerm*>(a->target.get())->name + "'");
        if (a->target->kind == NK::VarExpr) {
            auto* tv = static_cast<VarExpr*>(a->target.get());
            static const std::set<std::string> natTy = {
                "int", "int8", "int16", "int32", "int64", "uint", "uint8",
                "uint16", "uint32", "uint64", "num", "num32", "num64", "str", "byte"};
            if (tv->declare && natTy.count(tv->declType))
                throwTyped("X::Bind::NativeType", {{"name", tv->name}},
                           "Cannot bind to natively typed variable '" + tv->name +
                           "'; native types are not containers");
            // `my Str $x := 3` — typed bind of a LITERAL value (literals only:
            // an expression RHS must not be evaluated twice)
            static const std::set<std::string> kBindChecked = {
                "Int", "Num", "Rat", "Complex", "Str", "Bool"};
            if (tv->declare && kBindChecked.count(tv->declType) &&
                (a->value->kind == NK::IntLit || a->value->kind == NK::NumLit ||
                 a->value->kind == NK::StrLit || a->value->kind == NK::BoolLit)) {
                Value bv = eval(a->value.get());
                if (!rtTypeMatch(bv, tv->declType) &&
                    !(tv->declType == "Int" && bv.t == VT::Bool))
                    throwTypedV("X::TypeCheck::Binding",
                        {{"got", bv}, {"expected", Value::typeObj(tv->declType)}},
                        "Type check failed in binding; expected " + tv->declType +
                        " but got " + bv.typeName() + " (" + bv.gist() + ")");
            }
        }
        if (a->target->kind == NK::Index) {
            auto* ix = static_cast<Index*>(a->target.get());
            if (!ix->index)
                throwTypedV("X::Bind::ZenSlice",
                            {{"type", Value::typeObj(ix->isHash ? "Hash" : "Array")}},
                            "Cannot bind to a zen slice");
        }
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
        // multidim slice assignment: @a[*;0;*] = v1,v2,… / %h{*;"b";"c"} = v —
        // expand the star/list dims into concrete per-branch tuples, then
        // distribute the flattened RHS across them in order
        if (a->op == "=" && a->target->kind == NK::Index &&
            static_cast<Index*>(a->target.get())->multiDim &&
            static_cast<Index*>(a->target.get())->adverb.empty()) {
            auto* ix = static_cast<Index*>(a->target.get());
            auto* dims = static_cast<ListExpr*>(ix->index.get());
            ValueList keys; bool anyMulti = false;
            for (auto& de : dims->items) {
                if (de->kind == NK::Whatever) { anyMulti = true; keys.push_back(Value::whatever()); continue; }
                Value k = eval(de.get());
                if (k.t == VT::Array || k.t == VT::Range || k.t == VT::Whatever) anyMulti = true;
                keys.push_back(k);
            }
            if (anyMulti) {
                Value* root = lvalue(ix->base.get());
                std::vector<ValueList> tuples;
                std::function<void(const Value&, size_t, ValueList&)> expand =
                    [&](const Value& node, size_t d, ValueList& pref) {
                    if (d == keys.size()) { tuples.push_back(pref); return; }
                    auto emit1 = [&](Value kk) {
                        if (kk.t == VT::Code && kk.code)
                            kk = (node.t == VT::Array && node.arr)
                               ? callCallable(kk, ValueList{Value::integer((long long)node.arr->size())})
                               : callCallable(kk, ValueList{node});
                        Value child = Value::any();
                        if (node.t == VT::Array && node.arr) {
                            long long i = kk.toInt(), n = (long long)node.arr->size();
                            if (i < 0) { i += n; kk = Value::integer(i); }
                            if (i >= 0 && i < n) child = (*node.arr)[i];
                        } else if (node.t == VT::Hash && node.hash) {
                            auto it = node.hash->find(kk.toStr());
                            if (it != node.hash->end()) child = it->second;
                        }
                        pref.push_back(kk);
                        expand(child, d + 1, pref);
                        pref.pop_back();
                    };
                    const Value& k = keys[d];
                    if (k.t == VT::Whatever) {
                        if (node.t == VT::Array && node.arr)
                            for (long long i = 0; i < (long long)node.arr->size(); i++) emit1(Value::integer(i));
                        else if (node.t == VT::Hash && node.hash)
                            for (auto& kv2 : *node.hash) emit1(Value::str(kv2.first));
                        return;
                    }
                    if (k.t == VT::Array || k.t == VT::Range) {
                        for (auto& e2 : k.flatten()) emit1(e2);
                        return;
                    }
                    emit1(k);
                };
                ValueList pref;
                expand(*root, 0, pref);
                Value rhs = eval(a->value.get());
                ValueList vs = (rhs.t == VT::Array || rhs.t == VT::Range) ? rhs.flatten() : ValueList{rhs};
                size_t vi = 0;
                for (auto& tup : tuples) {
                    Value* node = root;
                    for (size_t d2 = 0; d2 < tup.size(); d2++) {
                        if (node->t == VT::Hash || (ix->isHash && d2 + 1 == tup.size() && node->t != VT::Array)) {
                            if (node->t != VT::Hash) *node = Value::makeHash();
                            node = &(*node->hash)[tup[d2].toStr()];
                        } else {
                            if (node->t != VT::Array) *node = Value::array();
                            long long i = tup[d2].toInt();
                            if (i < 0) i += (long long)node->arr->size();
                            if (i < 0) i = 0;
                            while ((long long)node->arr->size() <= i) node->arr->push_back(Value::any());
                            node = &(*node->arr)[i];
                        }
                    }
                    *node = vi < vs.size() ? vs[vi] : Value::any();
                    vi++;
                }
                return sink ? Value::any() : rhs;
            }
        }
        // slice assignment: %h{K1,K2,…} = v1,v2,… / @a[I1,I2,…] = … distributes the
        // (flattened) RHS across the keys. A slice subscript is a syntactic list, a
        // Range (^$n), or an @-var — a scalar subscript keeps the ordinary path.
        if (a->op == "=" && a->target->kind == NK::Index) {
            auto* ix = static_cast<Index*>(a->target.get());
            bool sliceForm = ix->index && !ix->multiDim &&
                (ix->index->kind == NK::ListExpr || ix->index->kind == NK::Range ||
                 ix->index->kind == NK::ArrayLit || // angle-word slice `%h<a b> = …`
                 (ix->index->kind == NK::VarExpr && !static_cast<VarExpr*>(ix->index.get())->name.empty() &&
                  static_cast<VarExpr*>(ix->index.get())->name[0] == '@') ||
                 (ix->index->kind == NK::Unary && static_cast<Unary*>(ix->index.get())->op == "^"));
            if (sliceForm) {
                Value keys = eval(ix->index.get());
                if (keys.t == VT::Array || keys.t == VT::Range) {
                    ValueList ks = keys.flatten();
                    Value rv = evalValueOf(a->value.get());
                    ValueList vs;
                    if (rv.t == VT::Array || rv.t == VT::Range) vs = rv.flatten();
                    else vs.push_back(rv);
                    Value* bp;
                    Value callHold; // `foo()[$b,] = …` — the result shares arr/hash with the container
                    if (ix->base->kind == NK::Call) { callHold = eval(ix->base.get()); bp = &callHold; }
                    else bp = lvalue(ix->base.get());
                    if (bp) {
                        if (bp->t == VT::Any || bp->t == VT::Nil)
                            *bp = ix->isHash ? Value::makeHash() : Value::array();
                        Value out = Value::array(); out.isList = true;
                        for (size_t i = 0; i < ks.size(); i++) {
                            Value v = i < vs.size() ? vs[i] : Value::any();
                            if (ix->isHash && bp->t == VT::Hash && bp->hash) (*bp->hash)[ks[i].toStr()] = v;
                            else if (bp->t == VT::Array && bp->arr) {
                                long long j = ks[i].toInt();
                                if (j >= 0) {
                                    if ((long long)bp->arr->size() <= j) bp->arr->resize(j + 1, Value::any());
                                    (*bp->arr)[j] = v;
                                }
                            }
                            out.arr->push_back(v);
                        }
                        return sink ? Value::any() : out;
                    }
                }
            }
        }
        // `@a := item, item, …` binds the list AS-IS — a Range/List element stays
        // one element (ListExpr eval would flatten it, which is `=` semantics).
        // `my constant @m = Nil, <a b>, …` binds too: constants are := in disguise.
        if ((a->op == ":=" ||
             (a->op == "=" && a->target->kind == NK::VarExpr &&
              static_cast<VarExpr*>(a->target.get())->declare &&
              static_cast<VarExpr*>(a->target.get())->declScope == "constant")) &&
            a->target->kind == NK::VarExpr &&
            !static_cast<VarExpr*>(a->target.get())->name.empty() &&
            static_cast<VarExpr*>(a->target.get())->name[0] == '@' &&
            a->value->kind == NK::ListExpr) {
            auto* le = static_cast<ListExpr*>(a->value.get());
            Value b = Value::array();
            for (auto& it : le->items) {
                if (it->kind == NK::Unary && static_cast<Unary*>(it.get())->op == "|") {
                    Value v = eval(static_cast<Unary*>(it.get())->operand.get());
                    if ((v.t == VT::Array || v.t == VT::Range)) { for (auto& x : v.flatten()) b.arr->push_back(x); continue; }
                }
                b.arr->push_back(eval(it.get()));
            }
            Value* lv = lvalue(a->target.get());
            *lv = b;
            return sink ? Value::any() : *lv;
        }
        // `$y := $x` ALIASES the container: reads and writes on $y reach $x's slot.
        // Implemented as a Proxy over the source's owning Env (Env value slots are
        // node-stable; the shared_ptr keeps the Env alive for escaped closures).
        if (a->op == ":=" && a->target->kind == NK::VarExpr && a->value->kind == NK::VarExpr) {
            auto* tv = static_cast<VarExpr*>(a->target.get());
            auto* sv = static_cast<VarExpr*>(a->value.get());
            if (tv->name.size() > 1 && tv->name[0] == '$' && sv->name.size() > 1 && sv->name[0] == '$' &&
                (std::isalpha((unsigned char)sv->name[1]) || sv->name[1] == '_') &&
                (std::isalpha((unsigned char)tv->name[1]) || tv->name[1] == '_')) {
                std::shared_ptr<Env> owner;
                for (std::shared_ptr<Env> en = tctx_.cur; en; en = en->parent)
                    if (en->vars.count(sv->name)) { owner = en; break; }
                if (owner) {
                    std::string src = sv->name;
                    Value proxy = Value::makeHash(); proxy.hashKind = "Proxy";
                    Value fetch; fetch.t = VT::Code; fetch.code = std::make_shared<Callable>();
                    fetch.code->builtin = [owner, src](Interpreter& I, ValueList&) -> Value {
                        auto it = owner->vars.find(src);
                        if (it == owner->vars.end()) return Value::any();
                        // a bound-to-bound chain: deref one more Proxy level
                        if (it->second.t == VT::Hash && it->second.hashKind == "Proxy" && it->second.hash) {
                            auto f2 = it->second.hash->find("FETCH");
                            if (f2 != it->second.hash->end()) { ValueList none; return I.callCallable(f2->second, none); }
                        }
                        return it->second;
                    };
                    Value store; store.t = VT::Code; store.code = std::make_shared<Callable>();
                    store.code->builtin = [owner, src](Interpreter& I, ValueList& sa) -> Value {
                        Value nv = sa.empty() ? Value::any() : sa[0];
                        auto it = owner->vars.find(src);
                        if (it != owner->vars.end() && it->second.t == VT::Hash &&
                            it->second.hashKind == "Proxy" && it->second.hash) {
                            auto s2 = it->second.hash->find("STORE");
                            if (s2 != it->second.hash->end()) { ValueList one{nv}; return I.callCallable(s2->second, one); }
                        }
                        owner->vars[src] = nv;
                        return nv;
                    };
                    (*proxy.hash)["FETCH"] = fetch;
                    (*proxy.hash)["STORE"] = store;
                    Value* blv = lvalue(a->target.get());
                    *blv = proxy;
                    return sink ? Value::any() : eval(a->value.get());
                }
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
        int nb = lv->natBits; bool ns = lv->natSigned; bool nf = lv->natFloat; // native-int container: preserve width & wrap
        if (sigil == '@') {
            std::string keepType = lv->ofType; // the container keeps its element type
            // Shaped array assignment (`my @a[2;2] = …`).
            if (a->op == "=" && lv->shape && !lv->shape->empty()) {
                auto shp = lv->shape;
                if (shp->size() == 1) { // 1-dim: flat row fill, reject overflow
                    long long cap = (*shp)[0];
                    ValueList flat; for (auto& x : rhs.flatten()) flat.push_back(x);
                    if ((long long)flat.size() > cap)
                        throw RakuError{Value::typeObj("X::OutOfRange"),
                            "Cannot assign " + std::to_string(flat.size()) +
                            " elements to a shaped array of " + std::to_string(cap)};
                    *lv = makeShapedContainer(*shp, keepType, &flat);
                    return rhs;
                }
                // multi-dim: the RHS must MATCH the shape. A shaped source must have
                // an identical shape; a nested list may not have MORE than dims[d]
                // elements at any level (a flat list is rejected), but a shortfall is
                // fine — missing slots keep the element default.
                if (rhs.shape && *rhs.shape != *shp)
                    throw RakuError{Value::typeObj("X::Assignment::ArrayShapeMismatch"),
                        "Cannot assign an array of a different shape"};
                Value built = makeShapedContainer(*shp, keepType); // all defaults
                std::function<void(Value&, const Value&, size_t)> overlay =
                  [&](Value& dst, const Value& src, size_t d) {
                    if (d == shp->size()) { dst = src; return; } // leaf
                    if (!(src.t == VT::Array && src.arr))
                        throw RakuError{Value::typeObj("X::Assignment::ToShaped"),
                            "Assignment to a shaped array needs a matching nested structure"};
                    if ((long long)src.arr->size() > (*shp)[d])
                        throw RakuError{Value::typeObj("X::Assignment::ArrayShapeMismatch"),
                            "Too many elements for dimension " + std::to_string(d)};
                    for (size_t i = 0; i < src.arr->size(); i++)
                        overlay((*dst.arr)[i], (*src.arr)[i], d + 1);
                };
                overlay(built, rhs, 0);
                *lv = built;
                return rhs;
            }
            // `=` assignment flattens iterables into the array; `:=` BINDS — the
            // list's items stay what they are (`my @t := ^10, (1,2), [3]` is 3
            // elements: a Range, a List, an Array — not their union).
            if (a->op == ":=" && rhs.t == VT::Array) { Value b = rhs; b.isList = false; *lv = b; }
            else if (a->op == "=" && a->value && a->value->kind == NK::VarExpr &&
                     !static_cast<VarExpr*>(a->value.get())->name.empty() &&
                     static_cast<VarExpr*>(a->value.get())->name[0] == '$' &&
                     rhs.t == VT::Range) {
                // a $-container ITEMIZES: `my @r = $r` is ONE element (the
                // Range), unlike the flattening `my @r = 1..5` (Array/List
                // itemization is wider surgery; Range is the roast-tested bit)
                Value one = Value::array();
                one.arr->push_back(rhs);
                *lv = one;
            }
            else {
                Value nv = coerceArray(rhs);
                // `=` REFILLS the same container (Raku identity): anything bound
                // to @a — a `-> $x` capture, `:=` alias, closure — tracks the change
                if (lv->t == VT::Array && lv->arr && nv.arr && lv->arr != nv.arr) {
                    *lv->arr = *nv.arr;
                    nv.arr = lv->arr;
                }
                *lv = nv;
            }
            if (!keepType.empty() && lv->ofType.empty()) lv->ofType = keepType;
        }
        else if (sigil == '%') {
            static const std::set<std::string> setty = {
                "Set", "SetHash", "Bag", "BagHash", "Mix", "MixHash"};
            std::string keepType = lv->ofType; // typed container: `my Int %h` keeps Int
            if (lv->t == VT::Hash && setty.count(lv->hashKind)) { // my %h is Set = 1,2,3
                // Set/Bag/Mix are immutable — only the initial (empty) fill assigns
                if (lv->hash && !lv->hash->empty() &&
                    (lv->hashKind == "Set" || lv->hashKind == "Bag" || lv->hashKind == "Mix"))
                    throw RakuError{Value::typeObj("X::Assignment::RO"),
                        "Cannot modify an immutable " + lv->hashKind + " (" + lv->gist() + ")"};
                std::string keyT = lv->ofType;
                Value nh = makeBaggy(rhs.flatten(), lv->hashKind);
                if (!keyT.empty() && nh.hash) // parameterized: keys must match `is Bag[Int]`
                    for (auto& kv : *nh.hash) {
                        Value orig = kv.second.pairKey ? *kv.second.pairKey : Value::str(kv.first);
                        if (!typeOrSubsetMatches(orig, keyT))
                            throw RakuError{Value::typeObj("X::TypeCheck::Binding"),
                                "Type check failed for " + lv->hashKind + " key; expected " +
                                keyT + " but got " + orig.gist()};
                    }
                *lv = nh;
                lv->ofType = keyT;
            }
            else {
                Value nv = coerceHash(rhs);
                if (lv->t == VT::Hash && lv->hash && nv.hash && lv->hash != nv.hash) {
                    *lv->hash = *nv.hash; // refill in place, keep container identity
                    nv.hash = lv->hash;
                }
                *lv = nv;
            }
            if (!keepType.empty() && lv->ofType.empty()) lv->ofType = keepType;
        }
        else if (rhs.t == VT::Nil && a->op == "=" && a->target->kind == NK::VarExpr) {
            // assigning Nil restores the container's default (is default / (Type) / Any)
            const std::string& nm = static_cast<VarExpr*>(a->target.get())->name;
            Value dv = Value::any();
            for (Env* en = tctx_.cur.get(); en; en = en->parent.get()) {
                auto di = en->varDefault.find(nm);
                if (di != en->varDefault.end()) { dv = di->second; break; }
                if (en->vars.count(nm)) break; // owner scope reached, no declared default
            }
            *lv = dv;
        }
        else {
            // typed container: enforce the constraint on assignment for the core
            // nominal types (`my Int $i = $str` throws X::TypeCheck::Assignment).
            // UNDEFINED values are checked too: the matching type object (`= Int`)
            // is fine, but a supertype's — `= Any`, the classic un-checked
            // `prompt` result at EOF — fails like Rakudo. Nil takes the reset
            // branch above; a Failure soaks into any container.
            if (a->op == "=" && a->target->kind == NK::VarExpr &&
                !(rhs.t == VT::Hash && rhs.hashKind == "Failure")) {
                static const std::set<std::string> kChecked = {
                    "Int", "Num", "Rat", "Complex", "Str", "Bool",
                };
                auto undefOk = [&](const std::string& want) {
                    // an undefined value carries its TYPE; it must still be the
                    // declared type or a subtype (Int matches Int; Any does not)
                    std::string tn = rhs.t == VT::Type ? rhs.s : rhs.typeName();
                    if (tn == want) return true;
                    if (want == "Int" && tn == "IntStr") return true;
                    if (want == "Num" && tn == "NumStr") return true;
                    if (want == "Rat" && (tn == "RatStr" || tn == "FatRat")) return true;
                    if (want == "Str" && (tn == "IntStr" || tn == "NumStr" ||
                                          tn == "RatStr" || tn == "ComplexStr")) return true;
                    return false;
                };
                const std::string& nm = static_cast<VarExpr*>(a->target.get())->name;
                for (Env* en = tctx_.cur.get(); en; en = en->parent.get()) {
                    auto di = en->varDefault.find(nm);
                    if (di != en->varDefault.end()) {
                        if (di->second.t == VT::Type && kChecked.count(di->second.s) &&
                            (isDefined(rhs) ? !rtTypeMatch(rhs, di->second.s)
                                            : !undefOk(di->second.s)) &&
                            !(di->second.s == "Int" && rhs.t == VT::Bool)) // Bool is an Int subtype
                            throwTypedV("X::TypeCheck::Assignment",
                                {{"got", rhs},
                                 {"expected", Value::typeObj(di->second.s)},
                                 {"symbol", Value::str(nm)}},
                                "Type check failed in assignment to " + nm +
                                "; expected " + di->second.s + " but got " + rhs.typeName() +
                                (isDefined(rhs) ? " (" + rhs.gist() + ")"
                                                : " " + rhs.gist())); // undef gist has its own parens
                        break;
                    }
                    if (en->vars.count(nm)) break;
                }
            }
            *lv = rhs;
        }
        if (nb) wrapNative(*lv, nb, ns, nf);
        return sink ? Value::any() : *lv;
    }

    // compound assignment
    // bracketed metaop assignment `A [op]= B`: A = A op B. Unlike plain `Rop=`
    // (which reverses roles INCLUDING the target), the bracketed form keeps the
    // LEFT target — `%vars{$k} [R//]= %*ENV{$k}` writes %vars (LibraryMake).
    if (a->op.size() > 3 && a->op[0] == '[' && a->op.back() == '=') {
        std::string base = a->op.substr(1, a->op.find(']') - 1);
        Value* lv = lvalue(a->target.get());
        Value r = eval(a->value.get());
        *lv = applyBinOp(base, *lv, r);
        return sink ? Value::any() : *lv;
    }
    if (a->op.size() > 2 && a->op[0] == 'R' && a->op.back() == '=' &&
        !std::isalnum((unsigned char)a->op[1])) {
        // `A Rop= B` is `B op= A` — the R meta reverses roles including the target
        std::string base = a->op.substr(1, a->op.size() - 2);
        Value l = eval(a->target.get());
        Value* rv = lvalue(a->value.get());
        *rv = applyArith(base, *rv, l);
        return sink ? Value::any() : *rv;
    }
    {   // short-circuit family: the RHS is thunked, and the target only needs to
        // be assignable when the assignment happens (`1 or= $x++` neither dies
        // nor runs $x++ — the truthy literal short-circuits first)
        std::string b = a->op.substr(0, a->op.size() - 1);
        bool scOr  = b == "||" || b == "or";
        bool scAnd = b == "&&" || b == "and";
        bool scDef = b == "//" || b == "orelse";                 // keep when defined
        bool scAt  = b == "andthen", scNat = b == "notandthen";  // definedness-based
        if (scOr || scAnd || scDef || scAt || scNat) {
            Value* lv = nullptr;
            try { lv = lvalue(a->target.get()); } catch (RakuError&) {}
            Value cur = lv ? *lv : eval(a->target.get());
            bool keep = scOr ? cur.truthy() : scAnd ? !cur.truthy()
                      : scAt ? !isDefined(cur) : isDefined(cur);
            if (keep) return sink ? Value::any() : cur;
            if (!lv) throw RakuError{Value::str("Cannot assign"), "Target is not assignable"};
            Value rhs = eval(a->value.get());
            int nb = lv->natBits; bool ns = lv->natSigned; bool nf = lv->natFloat;
            *lv = rhs;
            if (nb) wrapNative(*lv, nb, ns, nf);
            return sink ? Value::any() : *lv;
        }
    }
    Value* lv = lvalue(a->target.get());
    Value rhs = eval(a->value.get());
    // a Proxy-bound target (`$a := $x`) routes OP= through FETCH/STORE so the
    // update reaches the underlying container instead of clobbering the Proxy
    if (lv->t == VT::Hash && lv->hashKind == "Proxy" && lv->hash) {
        auto fit = lv->hash->find("FETCH"), sit = lv->hash->find("STORE");
        if (fit != lv->hash->end() && sit != lv->hash->end()) {
            std::string bop = a->op.substr(0, a->op.size() - 1);
            Value cur = callCallable(fit->second, {});
            Value nv = (bop == "^^" || bop == "xor")
                ? (cur.truthy() ? (rhs.truthy() ? Value::nil() : cur) : rhs)
                : applyBinOp(bop, cur, rhs);
            callCallable(sit->second, { nv });
            return sink ? Value::any() : nv;
        }
    }
    int nb = lv->natBits; bool ns = lv->natSigned; bool nf = lv->natFloat;
    std::string binop = a->op.substr(0, a->op.size() - 1); // strip '='
    if (binop == "^^" || binop == "xor") { // one-true xor keeps the true side (else Nil)
        *lv = lv->truthy() ? (rhs.truthy() ? Value::nil() : *lv) : rhs;
        return sink ? Value::any() : *lv;
    }
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
    // string each step. Appending PURE-ASCII text can never change the NFC form of
    // the existing buffer (ASCII is a base, never a combining mark, and is already
    // NFC), so append in place — O(1) amortised, keeping string-building O(n). Only
    // a non-ASCII right-hand side needs a re-normalisation across the join.
    if (!overloaded && binop == "~" && lv->t == VT::Str && rhs.t == VT::Str) {
        bool asciiRhs = true;
        for (unsigned char c : rhs.s) if (c >= 0x80) { asciiRhs = false; break; }
        if (asciiRhs) lv->s += rhs.s;
        else lv->s = nfcNormalize(lv->s + rhs.s);
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
    if (nb) wrapNative(*lv, nb, ns, nf);
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
// Set ops that return a Bool (membership/subset/equality). Over a junction
// operand these COLLAPSE per the junction kind (like `==`/`eq`); the Set-valued
// producers ((|)/(&)/(-)/…) instead build a junction of Sets.
static bool isSetPredicateStr(const std::string& o) {
    static const std::set<std::string> ops = {
        "(elem)", "∈", "(!elem)", "∉", "(cont)", "∋", "(!cont)", "∌",
        "(<=)", "⊆", "(<)", "⊂", "(>=)", "⊇", "(>)", "⊃", "(==)", "(!=)", "(<>)",
    };
    return ops.count(o) > 0;
}

// how "heavy" a set-op operand is: 0 = Setty (or coercible), 1 = Baggy, 2 = Mixy
static int settyTier(const Value& v) {
    if (v.t == VT::Hash) {
        if (v.hashKind.find("Mix") == 0) return 2;
        if (v.hashKind.find("Bag") == 0) return 1;
    }
    return 0;
}
static bool lazySetOperand(const Value& v) {
    if (v.t == VT::Range && v.rTo >= 9000000000000000000LL) return true;
    if (v.t == VT::Array && v.ext)
        return std::static_pointer_cast<LazySeqState>(v.ext)->infinite;
    return false;
}
// An unhandled Failure operand detonates when a set operator uses it.
static void setOpCheckFailure(const Value& v) {
    if (v.t == VT::Hash && v.hashKind == "Failure") {
        std::string msg = "Failure";
        if (v.hash) { auto it = v.hash->find("message"); if (it != v.hash->end()) msg = it->second.toStr(); }
        throw RakuError{Value::typeObj("X::AdHoc"), msg};
    }
}
// Coerce one operand to key => weight at the JOINT tier. A plain Hash coerces
// per tier: truthy-filtered membership at Set tier, numeric counts at Bag/Mix
// tier ({a => 42, b => 0} is set <a>, but bag (a => 42)). Mix tier keeps
// negative and fractional weights; Set/Bag drop non-positive ones.
static std::map<std::string, double> setWeights(const Value& v, int tier) {
    std::map<std::string, double> m;
    if (v.t == VT::Hash && v.hash) {
        bool isSetK = v.hashKind.find("Set") == 0;
        bool countK = settyTier(v) >= 1;
        for (auto& kv : *v.hash) {
            double w;
            if (isSetK) w = 1;
            else if (countK) w = kv.second.toNum();
            else if (tier == 0) { if (!kv.second.truthy()) continue; w = 1; }
            else w = kv.second.toNum();
            if (tier == 2 ? w == 0 : w <= 0) continue;
            m[kv.first] += w;
        }
    } else if (v.t == VT::Array || v.t == VT::Range) {
        for (auto& x : v.flatten()) {
            if (x.t == VT::Pair) {
                // a pair in an uncoerced list carries its value as a weight; at
                // Set tier it collapses to presence (falsy pairs drop out), so
                // (:42a,:0b) is {a}, not a weighted map that would skew (^)
                double w = x.pairVal ? x.pairVal->toNum() : 0;
                if (tier == 0) { if (!(x.pairVal && x.pairVal->truthy())) continue; w = 1; }
                if (tier == 2 ? w == 0 : w <= 0) continue;
                m[x.s] += w;
            }
            else if (x.t == VT::Type || x.t == VT::Any) m[x.gist()] += 1; // type objects ARE elements, keyed by gist
            else m[x.toStr()] += 1;
        }
    } else if (v.t == VT::Pair) {
        double w = v.pairVal ? v.pairVal->toNum() : 0;
        if (tier == 0) { if (v.pairVal && v.pairVal->truthy()) m[v.s] = 1; }
        else if (!(tier == 2 ? w == 0 : w <= 0)) m[v.s] = w;
    } else if (v.t == VT::Type || v.t == VT::Any) {
        m[v.gist()] = 1; // (Set) (&) (Set) — the type object is a one-element set
    } else if (v.t != VT::Nil) {
        m[v.toStr()] = 1;
    }
    return m;
}
// wrap a weight map as the tier's IMMUTABLE type (Set / Bag / Mix)
static Value setWrap(const std::map<std::string, double>& res, int tier) {
    Value h = Value::makeHash();
    h.hashKind = tier == 2 ? "Mix" : tier == 1 ? "Bag" : "Set";
    for (auto& kv : res) {
        if (tier == 2 ? kv.second == 0 : kv.second <= 0) continue;
        if (tier == 0) (*h.hash)[kv.first] = Value::boolean(true);
        else if (kv.second == (double)(long long)kv.second)
            (*h.hash)[kv.first] = Value::integer((long long)kv.second);
        else (*h.hash)[kv.first] = Value::number(kv.second);
    }
    return h;
}
static int setOpMinTier(const std::string& op) {
    return (op == "(+)" || op == "\xE2\x8A\x8E" || op == "(.)" || op == "\xE2\x8A\x8D") ? 1 : 0;
}
// single-operand form: `(|) $x` coerces (union with nothing IS the coercion)
static Value setCoerceOne(const std::string& op, const Value& v) {
    int tier = std::max(settyTier(v), setOpMinTier(op));
    return setWrap(setWeights(v, tier), tier);
}

static Value setOp(const std::string& op, const Value& l, const Value& r) {
    setOpCheckFailure(l); setOpCheckFailure(r);
    // membership against a RANGE is an arithmetic bounds check — no
    // materialization, so 0..10**42 (and open-ended ranges) work
    auto rangeHas = [](const Value& rng, const Value& x) -> bool {
        if (rng.ofType == "Str") { // Str range: string ordering between endpoints
            const std::string v = x.toStr();
            const std::string lo = cpToU8((uint32_t)rng.rFrom), hi = cpToU8((uint32_t)rng.rTo);
            return (rng.rExFrom ? v > lo : v >= lo) &&
                   (rng.rExTo ? v < hi : v <= hi);
        }
        double v = x.toNum();
        double lo = (double)rng.rFrom + (rng.rExFrom ? 1 : 0);
        if (rng.rTo >= 9000000000000000000LL) return v >= lo; // huge/unbounded top
        double hi = (double)rng.rTo - (rng.rExTo ? 1 : 0);
        return v >= lo && v <= hi;
    };
    if (op == "(elem)" || op == "∈" || op == "(!elem)" || op == "∉") {
        bool neg = (op == "(!elem)" || op == "∉");
        if (r.t == VT::Range) return Value::boolean(neg ? !rangeHas(r, l) : rangeHas(r, l));
        if (lazySetOperand(r)) throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot " + op + " a lazy list"};
        auto b = setWeights(r, settyTier(r)); std::string k = l.toStr();
        bool in = b.count(k) && b[k] != 0;
        return Value::boolean(neg ? !in : in);
    }
    if (op == "(cont)" || op == "∋" || op == "(!cont)" || op == "∌") {
        bool neg = (op == "(!cont)" || op == "∌");
        if (l.t == VT::Range) return Value::boolean(neg ? !rangeHas(l, r) : rangeHas(l, r));
        if (lazySetOperand(l)) throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot " + op + " a lazy list"};
        auto a = setWeights(l, settyTier(l)); std::string k = r.toStr();
        bool in = a.count(k) && a[k] != 0;
        return Value::boolean(neg ? !in : in);
    }
    if (lazySetOperand(l) || lazySetOperand(r))
        throw RakuError{Value::typeObj("X::Cannot::Lazy"),
                        "Cannot " + op + " a lazy list"};
    // joint tier: Mixy > Baggy > Setty; (+) and (.) are Baggy at minimum
    int tier = std::max({settyTier(l), settyTier(r), setOpMinTier(op)});
    auto a = setWeights(l, tier), b = setWeights(r, tier);
    auto at = [](std::map<std::string, double>& m, const std::string& k) { return m.count(k) ? m[k] : 0.0; };
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
    std::map<std::string, double> res;
    if (op == "(|)" || op == "∪") { res = a; for (auto& kv : b) res[kv.first] = std::max(at(res, kv.first), kv.second); }
    else if (op == "(&)" || op == "∩") { for (auto& kv : a) if (b.count(kv.first)) res[kv.first] = std::min(kv.second, b[kv.first]); }
    else if (op == "(-)" || op == "∖") {
        if (tier == 2) { res = a; for (auto& kv : b) res[kv.first] = at(res, kv.first) - kv.second; } // Mix keeps negatives
        else for (auto& kv : a) { double d = kv.second - at(b, kv.first); if (d > 0) res[kv.first] = d; }
    }
    else if (op == "(^)" || op == "⊖") {
        // symmetric difference is the WEIGHT difference at every tier:
        // Bag(a b(2)) (^) Bag(a b) is bag(b) — for Sets it degenerates to
        // keys-in-exactly-one (|1-1| = 0 drops shared keys)
        for (auto& kv : a) { double d = kv.second - at(b, kv.first); res[kv.first] = d < 0 ? -d : d; }
        for (auto& kv : b) if (!a.count(kv.first)) res[kv.first] = kv.second;
    }
    else if (op == "(+)" || op == "⊎") { res = a; for (auto& kv : b) res[kv.first] += kv.second; }
    else if (op == "(.)" || op == "⊍") { for (auto& kv : a) if (b.count(kv.first)) res[kv.first] = kv.second * b[kv.first]; }
    return setWrap(res, tier);
}

// Multi-arg symmetric difference. Rakudo's (^)/⊖ is a genuine list operator, not
// a left fold: for each key the result weight is (largest − second-largest) over
// the operands' weights, where an operand lacking the key contributes 0. This
// reduces to |a−b| for two operands but diverges from a pairwise fold for three
// or more (e.g. Bag(a×42) ⊖ Bag(a×7) ⊖ Bag(a×43) is a×1, not a×8).
static Value setSymDiffN(const ValueList& operands) {
    for (auto& o : operands) {
        setOpCheckFailure(o);
        if (lazySetOperand(o))
            throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot (^) a lazy list"};
    }
    int tier = setOpMinTier("(^)");
    for (auto& o : operands) tier = std::max(tier, settyTier(o));
    std::vector<std::map<std::string, double>> ws;
    ws.reserve(operands.size());
    std::set<std::string> keys;
    for (auto& o : operands) { ws.push_back(setWeights(o, tier)); for (auto& kv : ws.back()) keys.insert(kv.first); }
    std::map<std::string, double> res;
    const double NINF = -std::numeric_limits<double>::infinity();
    for (auto& k : keys) {
        // exactly one slot per operand: its weight for k, or 0 if it lacks k
        double t1 = NINF, t2 = NINF;
        for (auto& m : ws) {
            auto it = m.find(k);
            double w = it != m.end() ? it->second : 0.0;
            if (w > t1) { t2 = t1; t1 = w; }
            else if (w > t2) { t2 = w; }
        }
        res[k] = t1 - t2;
    }
    return setWrap(res, tier);
}

static bool isJunction(const Value& v) {
    return v.t == VT::Array && (v.enumName == "any" || v.enumName == "all" || v.enumName == "one" || v.enumName == "none");
}

// +&/+|/+^ past int64: infinite two's complement over base-2^32 limbs.
// Negatives get sign-extension headroom so the top limb is pure sign, which
// also makes the result's sign readable off its top bit.
static BigInt bigBitwise(const BigInt& a, const BigInt& b, char which) {
    const BigInt two32(4294967296LL);
    auto rawLimbs = [&](const BigInt& x) {
        std::vector<uint32_t> limbs;
        BigInt cur = x.abs(), q, r;
        while (!cur.isZero()) { BigInt::divmod(cur, two32, q, r); limbs.push_back((uint32_t)r.toLL()); cur = q; }
        return limbs;
    };
    std::vector<uint32_t> la = rawLimbs(a), lb = rawLimbs(b);
    size_t n = std::max(la.size(), lb.size()) + 1;
    auto twos = [&](std::vector<uint32_t>& v, int sign) {
        v.resize(n, 0);
        if (sign < 0) { uint64_t carry = 1; for (auto& L : v) { uint64_t t = (uint64_t)(uint32_t)~L + carry; L = (uint32_t)t; carry = t >> 32; } }
    };
    twos(la, a.sign); twos(lb, b.sign);
    std::vector<uint32_t> res(n);
    for (size_t i = 0; i < n; i++)
        res[i] = which == '&' ? (la[i] & lb[i]) : which == '|' ? (la[i] | lb[i]) : (la[i] ^ lb[i]);
    bool neg = (res[n - 1] & 0x80000000u) != 0;
    if (neg) { uint64_t carry = 1; for (auto& L : res) { uint64_t t = (uint64_t)(uint32_t)~L + carry; L = (uint32_t)t; carry = t >> 32; } }
    BigInt out(0);
    for (size_t i = n; i-- > 0;) out = out * two32 + BigInt((long long)res[i]);
    if (neg && !out.isZero()) out.sign = -1;
    return out;
}

// List-context expansion of an operand for a list-infix op (Z/X/hyper/minmax):
// a Blob/Buf yields its ELEMENTS; everything else flattens as usual. (Distinct
// from flatten(), which keeps a Blob whole — matching Rakudo's `flat`/`reduce`.)
static ValueList listCtx(const Value& v) {
    if (v.t == VT::Str && !v.itemized && (v.hashKind == "Blob" || v.hashKind == "Buf"))
        return v.blobList();
    return v.flatten();
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
            case '+': if (c1 == '\0' && !rakupp::add_ovf(a, b, &z)) return Value::integer(z); break;
            case '-': if (c1 == '\0' && !rakupp::sub_ovf(a, b, &z)) return Value::integer(z); break;
            case '*': if (c1 == '\0' && !rakupp::mul_ovf(a, b, &z)) return Value::integer(z); break;
            case '<': if (c1 == '\0') return Value::boolean(a < b);
                      if (c1 == '=') return Value::boolean(a <= b); break;
            case '>': if (c1 == '\0') return Value::boolean(a > b);
                      if (c1 == '=') return Value::boolean(a >= b); break;
            case '=': if (c1 == '=') return Value::boolean(a == b); break;
            case '!': if (c1 == '=') return Value::boolean(a != b); break;
            case '%': if (c1 == '\0' && b != 0) { if (b == -1) return Value::integer(0); long long m = a % b; if (m && ((m < 0) != (b < 0))) m += b; return Value::integer(m); } break;
        }
    }
    // Mixed float fast path: one side Num, the other any simple numeric — the
    // result is what the generic double path below computes anyway, dispatched
    // by first char instead of walking the whole op chain. (Zero-denominator
    // Rats and bignums fall through so their special semantics stay intact.)
    if ((l.t == VT::Num || r.t == VT::Num) && !op.empty() && op.size() <= 2) {
        auto simpleNum = [](const Value& v, double& out) -> bool {
            switch (v.t) {
                case VT::Num:  out = v.n; return true;
                case VT::Int:  if (v.big) return false; out = (double)v.i; return true;
                case VT::Bool: out = v.b ? 1.0 : 0.0; return true;
                case VT::Rat:  if (!v.ratN || !v.ratD || v.ratD->isZero()) return false;
                               out = v.ratN->toDouble() / v.ratD->toDouble(); return true;
                default: return false;
            }
        };
        double a, b;
        if (simpleNum(l, a) && simpleNum(r, b)) {
            char c0 = op[0], c1 = op.size() > 1 ? op[1] : '\0';
            switch (c0) {
                case '+': if (c1 == '\0') return Value::number(a + b); break;
                case '-': if (c1 == '\0') return Value::number(a - b); break;
                case '*': if (c1 == '\0') return Value::number(a * b);
                          if (c1 == '*') return Value::number(std::pow(a, b)); break;
                case '/': if (c1 == '\0') return Value::number(a / b); break;
                case '<': if (c1 == '\0') return Value::boolean(a < b);
                          if (c1 == '=') return Value::boolean(a <= b); break;
                case '>': if (c1 == '\0') return Value::boolean(a > b);
                          if (c1 == '=') return Value::boolean(a >= b); break;
                case '=': if (c1 == '=') return Value::boolean(a == b); break;
                case '!': if (c1 == '=') return Value::boolean(a != b); break;
            }
        }
    }
    // Str/Str fast path: comparisons and concat on two PLAIN strings (no
    // Version/IO/Buf hashKind tag, no enum identity) dispatch by char and
    // compare/concat `s` directly — exactly what the op-chain tail does for
    // them (a plain Str's toStr() is its `s`). Tagged values (Version
    // part-compare, enum stringification), junctions (VT::Array), mixins
    // (VT::Object) and Whatever-currying all miss this guard and keep their
    // full-chain semantics.
    if (l.t == VT::Str && r.t == VT::Str && !op.empty() && op.size() <= 2 &&
        l.hashKind.empty() && r.hashKind.empty() && l.enumName.empty() && r.enumName.empty()) {
        char c0 = op[0], c1 = op.size() > 1 ? op[1] : '\0';
        switch (c0) {
            case '~': if (c1 == '\0') return Value::str(nfcNormalize(l.s + r.s)); break;
            case 'e': if (c1 == 'q') return Value::boolean(l.s == r.s); break;
            case 'n': if (c1 == 'e') return Value::boolean(l.s != r.s); break;
            case 'l': if (c1 == 't') return Value::boolean(l.s <  r.s);
                      if (c1 == 'e') return Value::boolean(l.s <= r.s); break;
            case 'g': if (c1 == 't') return Value::boolean(l.s >  r.s);
                      if (c1 == 'e') return Value::boolean(l.s >= r.s); break;
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
    // negated ops (`!%%`, `!eq`, `!before`, …): apply the base op, negate the Bool.
    // A curried base (`* !%% 3` -> WhateverCode) stays curried, negation wrapped in.
    if (op.size() > 1 && op[0] == '!' && op != "!=" && op != "!==" && op != "!===" && op != "!~~" &&
        !isSetOpStr(op)) {
        Value base = applyArith(op.substr(1), l, r);
        if (base.t == VT::Code && base.code && base.code->isWhateverCode) {
            Value wrap; wrap.t = VT::Code; wrap.code = std::make_shared<Callable>();
            wrap.code->isWhateverCode = true;
            wrap.code->whateverArity = base.code->whateverArity;
            Value inner = base;
            wrap.code->builtin = [inner](Interpreter& I, ValueList& a) -> Value {
                return Value::boolean(!I.callCallable(inner, a).truthy());
            };
            return wrap;
        }
        return Value::boolean(!base.truthy());
    }
    // set ops (∈ ∉ ∋ …) with a Whatever operand curry into a WhateverCode
    // (`.grep: * ∉ @seen`) instead of computing eagerly — fall through below.
    {
        auto wish = [](const Value& v) { return v.t == VT::Whatever || (v.t == VT::Code && v.code && v.code->isWhateverCode); };
        // A junction operand autothreads (below) BEFORE the set is computed:
        // `all([1,2,3],[2,3,1]) (==) 1..3` tests each eigenstate, then collapses.
        if (isSetOpStr(op) && !wish(l) && !wish(r) && !isJunction(l) && !isJunction(r))
            return setOp(op, l, r);
    }
    // hyper binary metaop  >>OP>>  : element-wise apply OP over the two lists
    if (op.size() >= 5 && (op.substr(0, 2) == ">>" || op.substr(0, 2) == "<<") &&
        (op.substr(op.size() - 2) == ">>" || op.substr(op.size() - 2) == "<<")) {
        std::string inner = op.substr(2, op.size() - 4);
        // hyper compound assignment: `@a <<+=>> 2019` applies the base op
        // elementwise and MUTATES the left array (Value.arr is shared storage,
        // so writing through it reaches the caller's container)
        static const std::set<std::string> eqTailCmp = {"==", "!=", "<=", ">=", "===", "=:="};
        if (inner.size() >= 2 && inner.back() == '=' && !eqTailCmp.count(inner) &&
            l.t == VT::Array && l.arr) {
            std::string base = inner.substr(0, inner.size() - 1);
            ValueList b = listCtx(r);
            if (!b.empty())
                for (size_t i = 0; i < l.arr->size(); i++)
                    (*l.arr)[i] = applyArith(base, (*l.arr)[i], b[i % b.size()]);
            return l;
        }
        ValueList a = listCtx(l), b = listCtx(r);
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
    // autothreading over a junction operand — but Whatever-currying WINS:
    // `* eq 'a'|'b'` is a WhateverCode over the junction, not a collapsed Bool
    auto whateverish = [](const Value& v) {
        return v.t == VT::Whatever || (v.t == VT::Code && v.code && v.code->isWhateverCode);
    };
    if ((isJunction(l) || isJunction(r)) && !whateverish(l) && !whateverish(r)) {
        // when BOTH sides are junctions, all/none is the OUTER (tighter) thread:
        // ('a'|'b') eq ($x & $y) threads the & first, then the | inside it
        bool bothJ = isJunction(l) && isJunction(r);
        auto rank = [](const Value& v) { return v.enumName == "all" || v.enumName == "none" ? 1 : 0; };
        bool pickL = !bothJ ? isJunction(l) : (rank(l) >= rank(r));
        const Value& j = pickL ? l : r;
        bool jleft = pickL;
        // smartmatch AGAINST a junction (RHS) keeps ACCEPTS' collapsing Bool
        // semantics (`5 ~~ 3|5|7` is True); every other op — comparisons
        // included — autothreads into a PRESERVED junction of results, which
        // only boolean context collapses: (5 == 3|5|7).gist is
        // 'any(False, True, False)' (S03-junctions/misc.t)
        if ((op == "~~" || op == "!~~") && !jleft) {
            int t = 0, total = 0;
            for (auto& e : *j.arr) { total++; if (applyArith(op, l, e).truthy()) t++; }
            bool res = j.enumName == "any" ? t > 0 : j.enumName == "all" ? t == total : j.enumName == "one" ? t == 1 : t == 0;
            return Value::boolean(res);
        }
        Value out = Value::array(); out.enumName = j.enumName;
        // A negated comparison flips the junction kind (De Morgan): `X != any(…)`
        // is `!(X == any(…))` = `all(X != …)`. Without this, `0 != 0|1|2` collapses
        // True instead of False (0 matches one eigenstate) — this broke
        // OpenSSL's `while ($err != 0|WANT_READ|WANT_WRITE)` drain loop.
        static const std::set<std::string> negEq = {"!=", "ne", "!==", "!eqv", "!=:="};
        if (negEq.count(op)) {
            if (out.enumName == "any") out.enumName = "all";
            else if (out.enumName == "all") out.enumName = "any";
        }
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
        ValueList a = listCtx(l), b = listCtx(r);
        Value out = Value::array(); out.isList = true;
        for (size_t i = 0; i < a.size() && i < b.size(); i++) {
            if (sub.empty()) { Value t = Value::array({a[i], b[i]}); t.isList = true; out.arr->push_back(t); }
            else if (sub == "=>") out.arr->push_back(Value::pair(a[i].toStr(), b[i]));
            else out.arr->push_back(applyArith(sub, a[i], b[i]));
        }
        return out;
    }
    if (op == "X" || (op.size() > 1 && op[0] == 'X')) { // cross; X<op> applies op
        std::string sub = op.substr(1);
        ValueList a = listCtx(l), b = listCtx(r);
        Value out = Value::array(); out.isList = true;
        for (auto& x : a) for (auto& y : b) {
            if (sub.empty()) { Value t = Value::array({x, y}); t.isList = true; out.arr->push_back(t); }
            else if (sub == "=>") out.arr->push_back(Value::pair(x.toStr(), y));
            else out.arr->push_back(applyArith(sub, x, y));
        }
        return out;
    }
    if (op == "minmax") { // list infix: a Range spanning both operands' extremes
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
    // Whatever-currying: `* + 1`, `*.elems == 2`, `2 * *`, etc. yield a WhateverCode.
    // Smartmatch curries on the LEFT — `* ~~ /rx/` and `* !~~ @x` are the matcher
    // idioms `.grep`/`.first` rely on — but a bare Whatever on the RIGHT stays a
    // value: `$x ~~ *` matches anything (the `when *` default), it does not curry.
    auto isWhateverish = [](const Value& v) {
        return v.t == VT::Whatever || (v.t == VT::Code && v.code && v.code->isWhateverCode);
    };
    // `*.abs ~~ Code` does NOT curry: an already-composed WhateverCode on the
    // left of a smartmatch is a VALUE (a bare `*` on the left still curries)
    bool skipCurry = (op == "~~" || op == "!~~") &&
                     l.t == VT::Code && l.code && l.code->isWhateverCode &&
                     r.t != VT::Whatever;
    if (!skipCurry && (isWhateverish(l) || isWhateverish(r))) {
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

    // Range ± n shifts both endpoints, preserving exclusivity: ^9+1 is 1..9,
    // (1..5)+1 is 2..6. n + Range commutes.
    if (l.t == VT::Range && (r.t == VT::Int || r.t == VT::Bool) && (op == "+" || op == "-")) {
        long long d = op == "+" ? r.toInt() : -r.toInt();
        Value out = Value::range(l.rFrom + d, l.rTo + d, l.rExFrom, l.rExTo);
        return out;
    }
    // Range * n / Range / n scale both endpoints (n * Range commutes);
    // a non-integer result becomes a fractional range
    if (l.t == VT::Range && !l.rNum && l.ofType.empty() &&
        (r.t == VT::Int || r.t == VT::Num || r.t == VT::Rat) &&
        (op == "*" || op == "/")) {
        double f = r.toNum();
        if (f != 0 || op == "*") {
            double lo = l.rFrom * (op == "*" ? f : 1.0 / f);
            double hi = l.rTo * (op == "*" ? f : 1.0 / f);
            if (lo == (long long)lo && hi == (long long)hi && r.t == VT::Int)
                return Value::range((long long)lo, (long long)hi, l.rExFrom, l.rExTo);
            Value out = Value::range((long long)lo, (long long)hi, l.rExFrom, l.rExTo);
            out.rNum = true; out.n = lo; out.im = hi;
            return out;
        }
    }
    if (r.t == VT::Range && !r.rNum && r.ofType.empty() && l.t == VT::Int && op == "*") {
        long long f = l.toInt();
        return Value::range(r.rFrom * f, r.rTo * f, r.rExFrom, r.rExTo);
    }
    if (r.t == VT::Range && (l.t == VT::Int || l.t == VT::Bool) && op == "+") {
        long long d = l.toInt();
        Value out = Value::range(r.rFrom + d, r.rTo + d, r.rExFrom, r.rExTo);
        return out;
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
        if (op == "**") {
            // 0 ** 0 == 1+0i by spec (RT #128785; std::pow gives NaN+NaNi via polar log)
            if (b == std::complex<double>(0.0, 0.0)) return mk({1.0, 0.0});
            return mk(std::pow(a, b));
        }
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
                } else if (std::isalpha(c) || c >= 0x80) { // ASCII or Unicode letters (α, β, …) are alpha parts
                    size_t j = i; while (j < s.size() && (std::isalpha((unsigned char)s[j]) || (unsigned char)s[j] >= 0x80)) j++;
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
            bool aP = k < pa.size(), bP = k < pb.size();
            if (aP && pa[k].second == "*") continue;
            if (bP && pb[k].second == "*") continue;
            if (aP && bP) {
                auto& a = pa[k]; auto& b = pb[k];
                if (a.first && b.first) // both numeric: by length then lexicographically
                    c = a.second.size() != b.second.size() ? (a.second.size() < b.second.size() ? -1 : 1)
                      : (a.second < b.second ? -1 : a.second > b.second ? 1 : 0);
                else if (!a.first && !b.first) // both alpha: string compare
                    c = a.second < b.second ? -1 : a.second > b.second ? 1 : 0;
                else c = a.first ? -1 : 1; // a numeric part sorts BEFORE an alpha part (3 < "a")
            }
            // one side ran out: an EXTRA non-zero numeric part makes that side greater
            // (1.2.1.1 > 1.2.1) but a trailing ZERO is insignificant (1.2.1a1.0 == 1.2.1a1);
            // an EXTRA alpha part makes it LESS (1.2.1γ < 1.2.1).
            else if (aP) c = pa[k].first ? (pa[k].second == "0" ? 0 : 1) : -1;
            else         c = pb[k].first ? (pb[k].second == "0" ? 0 : -1) : 1;
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
        // Small-Rat fast path: when every component fits well under 2^62, do the
        // exact arithmetic in native __int128 (products stay in range) and reduce
        // with a native gcd — orders of magnitude cheaper than the BigInt path.
        // Rakudo-spec spill applies here too: denominator past uint64 → Num.
        auto smallParts = [](const Value& v, long long& n, long long& d) -> bool {
            const long long LIM = 1LL << 62;
            if (v.t == VT::Rat) {
                if (!v.ratN->fitsLL() || !v.ratD->fitsLL()) return false;
                n = v.ratN->toLL(); d = v.ratD->toLL();
            } else if (v.t == VT::Int) {
                if (v.big) { if (!v.big->fitsLL()) return false; n = v.big->toLL(); }
                else n = v.i;
                d = 1;
            } else { n = v.b ? 1 : 0; d = 1; } // Bool
            return n > -LIM && n < LIM && d < LIM;
        };
#if RAKUPP_HAS_INT128
        if (anyRat && !fat && (op == "+" || op == "-" || op == "*" || op == "/")) {
            long long n1, d1, n2, d2;
            if (smallParts(l, n1, d1) && smallParts(r, n2, d2) &&
                d1 != 0 && d2 != 0 && !(op == "/" && n2 == 0)) { // zero-den Rats take the slow path
                __int128 n, d;
                if (op == "*") { n = (__int128)n1 * n2; d = (__int128)d1 * d2; }
                else if (op == "/") { n = (__int128)n1 * d2; d = (__int128)d1 * n2; }
                else { d = (__int128)d1 * d2;
                       n = op == "+" ? (__int128)n1 * d2 + (__int128)n2 * d1
                                     : (__int128)n1 * d2 - (__int128)n2 * d1; }
                if (d < 0) { n = -n; d = -d; }
                // binary gcd — shifts and subtracts instead of ~15 __int128 divisions
                auto ctz = [](unsigned __int128 x) -> int {
                    unsigned long long lo = (unsigned long long)x;
                    return lo ? __builtin_ctzll(lo) : 64 + __builtin_ctzll((unsigned long long)(x >> 64));
                };
                unsigned __int128 a = (unsigned __int128)(n < 0 ? -n : n), b = (unsigned __int128)d;
                unsigned __int128 g;
                if (a && b) {
                    int sh = ctz(a | b);
                    a >>= ctz(a);
                    while (b) {
                        b >>= ctz(b);
                        if (a > b) { unsigned __int128 t = a; a = b; b = t; }
                        b -= a;
                    }
                    g = a << sh;
                } else g = a | b;
                if (g > 1) { n /= (__int128)g; d /= (__int128)g; }
                if (d > (__int128)0xFFFFFFFFFFFFFFFFULL) // uint64 denominator cap
                    return Value::number((double)n / (double)d);
                if (n >= (__int128)LLONG_MIN && n <= (__int128)LLONG_MAX && d <= (__int128)LLONG_MAX) {
                    // already reduced with d > 0 — build directly, no second gcd
                    Value v; v.t = VT::Rat;
                    v.ratN = std::make_shared<BigInt>((long long)n);
                    v.ratD = std::make_shared<BigInt>((long long)d);
                    return v;
                }
                // (reduced numerator wider than 64 bits: fall through to BigInt)
            }
        }
#endif // RAKUPP_HAS_INT128 (small-Rat fast path; MSVC falls through to BigInt)
        if (op == "+" || op == "-" || op == "*") {
            if (smallInt) {
                long long a = l.toInt(), b = r.toInt(), res;
                if (op == "+" && !rakupp::add_ovf(a, b, &res)) return Value::integer(res);
                if (op == "-" && !rakupp::sub_ovf(a, b, &res)) return Value::integer(res);
                if (op == "*" && !rakupp::mul_ovf(a, b, &res)) return Value::integer(res);
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
            if (anyRat && (op == "%" || op == "%%")) {
                // Rat modulo stays exact: a % b = a - b * floor(a/b)  (10.3 % 3 == 1.3)
                BigInt an = getN(l), ad = getD(l), bn = getN(r), bd = getD(r);
                if (bn.isZero()) {
                    if (op == "%%") throw RakuError{Value::typeObj("X::Numeric::DivideByZero"),
                        "Attempt to divide " + l.toStr() + " by zero using infix:<%%>"};
                    return Value::typeObj("Failure");
                }
                BigInt N = an * bd, D = ad * bn;
                if (D.sign < 0) { N = -N; D = -D; }
                BigInt q, rm; BigInt::divmod(N, D, q, rm);
                if (!rm.isZero() && N.sign < 0) q = q - BigInt(1); // floor, not truncate
                BigInt rn = an * bd - bn * ad * q;
                if (op == "%%") return Value::boolean(rn.isZero());
                return mkRat(std::move(rn), ad * bd);
            }
            if (smallInt && op != "div") { // native fast path for small ints (div stays on BigInt for identical rounding)
                long long a = l.toInt(), b = r.toInt();
                if (b == 0) {
                    if (op == "%%") throw RakuError{Value::typeObj("X::Numeric::DivideByZero"),
                        "Attempt to divide " + l.toStr() + " by zero using infix:<%%>"};
                    return Value::typeObj("Failure");
                }
                if (b == -1) return op == "%%" ? Value::boolean(true) : Value::integer(0); // a % -1 == 0 (avoids LLONG_MIN%-1 UB)
                long long rem = a % b;
                if (op == "%%") return Value::boolean(rem == 0); // divisibility is sign-independent
                if (rem != 0 && ((rem < 0) != (b < 0))) rem += b; // sign follows divisor (matches BigInt path)
                return Value::integer(rem); // % / mod
            }
            BigInt a = l.toBig(), b = r.toBig();
            if (b.isZero()) {
                if (op == "%%") throw RakuError{Value::typeObj("X::Numeric::DivideByZero"),
                    "Attempt to divide " + l.toStr() + " by zero using infix:<%%>"};
                return Value::typeObj("Failure");
            }
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
#if RAKUPP_HAS_INT128
            else if (long long cn1, cd1, cn2, cd2;
                     anyRat && smallParts(l, cn1, cd1) && smallParts(r, cn2, cd2)) {
                // native cross-multiply — no BigInt temporaries for small Rats
                __int128 a = (__int128)cn1 * cd2, b = (__int128)cn2 * cd1;
                c = a < b ? -1 : a > b ? 1 : 0;
            }
#endif
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
        if (l.t == VT::Num || r.t == VT::Num) { // floating modulo: a - b * floor(a/b)
            double a = l.toNum(), b = r.toNum();
            if (b == 0.0) return Value::number(std::numeric_limits<double>::quiet_NaN());
            return Value::number(a - b * std::floor(a / b));
        }
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
    if (op == "~") {
        // Uni ~ Uni concatenates the CODEPOINT buffers (no normalization —
        // that's what .NFC on the result is for); nfc-concat.t's whole plan
        auto uniish = [](const Value& v) {
            return v.t == VT::Array && v.arr &&
                   (v.s == "Uni" || v.s == "NFC" || v.s == "NFD" || v.s == "NFKC" || v.s == "NFKD");
        };
        if (uniish(l) && uniish(r)) {
            Value out = Value::array(); out.s = "Uni";
            for (auto& c : *l.arr) out.arr->push_back(c);
            for (auto& c : *r.arr) out.arr->push_back(c);
            return out;
        }
        // an undefined operand stringifies to "" (Rakudo warns; `Any ~ $x` is $x)
        auto undef = [](const Value& v) { return v.t == VT::Any || v.t == VT::Nil || v.t == VT::Type; };
        return Value::str(nfcNormalize((undef(l) ? std::string() : l.toStr()) +
                                       (undef(r) ? std::string() : r.toStr())));
    }
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
    if (op == "+&" || op == "+|" || op == "+^") {
        if (l.big || r.big) {
            BigInt res = bigBitwise(l.big ? *l.big : BigInt(l.toInt()),
                                    r.big ? *r.big : BigInt(r.toInt()), op[1]);
            return res.fitsLL() ? Value::integer(res.toLL()) : Value::bigint(res);
        }
        long long a = l.toInt(), b = r.toInt();
        return Value::integer(op[1] == '&' ? (a & b) : op[1] == '|' ? (a | b) : (a ^ b));
    }
    if (op == "+<") { // escalate to BigInt when the result would overflow long long
        long long sh = r.toInt();
        if (sh < 0) return Value::integer(0);
        if (!l.big && sh < 62 && std::llabs(l.toInt()) < (1LL << (62 - sh)))
            return Value::integer(l.toInt() << sh);
        BigInt lb = l.big ? *l.big : BigInt(l.toInt());
        BigInt res = lb * BigInt(2).pow(sh);
        return res.fitsLL() ? Value::integer(res.toLL()) : Value::bigint(res);
    }
    if (op == "+>") {
        long long sh = r.toInt();
        if (sh < 0) return Value::integer(0);
        if (!l.big) return Value::integer(sh >= 63 ? (l.toInt() < 0 ? -1 : 0) : (l.toInt() >> sh));
        BigInt q, rem;
        BigInt::divmod(*l.big, BigInt(2).pow(sh), q, rem);
        if (l.big->sign < 0 && !rem.isZero()) q = q - BigInt(1); // arithmetic shift = floor
        return q.fitsLL() ? Value::integer(q.toLL()) : Value::bigint(q);
    }
    // boolean bitwise (return Bool)
    if (op == "?&") return Value::boolean(l.truthy() && r.truthy());
    if (op == "?|") return Value::boolean(l.truthy() || r.truthy());
    if (op == "?^") return Value::boolean(l.truthy() != r.truthy());
    // string bitwise — per-character on codepoints; the longer string's tail is
    // kept for | and ^, dropped for &
    if (op == "~&" || op == "~|" || op == "~^") {
        std::string a = l.toStr(), b = r.toStr(), out;
        // Blob/Buf extend rightwards for all three; plain Str ~& truncates to the shorter
        bool bufish = (l.t == VT::Str && !l.hashKind.empty()) || (r.t == VT::Str && !r.hashKind.empty());
        size_t n = (op == "~&" && !bufish) ? std::min(a.size(), b.size()) : std::max(a.size(), b.size());
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
    if (op == "<=>") {
        // numeric comparison: a non-numeric Str operand cannot coerce
        for (const Value* s : {&l, &r})
            if (s->t == VT::Str && !numifyStr(s->s).isNumeric())
                throw RakuError{Value::typeObj("X::Str::Numeric"),
                    "Cannot convert string to number: base-10 number must "
                    "begin with valid digits or '.' in '" + s->s + "'"};
        double a = l.toNum(), b = r.toNum();
        return orderVal(a < b ? -1 : a > b ? 1 : 0);
    }
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
        else if (l.t == VT::Rat) // structural nude compare — .Str on a 0-denominator Rat throws
            same = l.fatRat == r.fatRat &&
                   l.ratN && r.ratN && l.ratD && r.ratD &&
                   BigInt::cmp(*l.ratN, *r.ratN) == 0 && BigInt::cmp(*l.ratD, *r.ratD) == 0;
        else same = (l.toStr() == r.toStr()); // value types (Int/Str/Num/Rat/...)
        return Value::boolean(op == "===" ? same : !same); // !== and !=== both negate identity
    }
    if (op == "%%") { long long b = r.toInt(); return Value::boolean(b != 0 && l.toInt() % b == 0); }
    if (op == "=:=") return Value::boolean(l.t == r.t && valueEq(l, r));
    if (op == "~~" || op == "!~~") {
        bool res;
        // Whatever on the RHS matches anything (Whatever.ACCEPTS is always True):
        // `when *`, `$x ~~ *`. (~~ never curries — see kNoCurry above.)
        if (r.t == VT::Whatever) return Value::boolean(op == "~~");
        if (!r.enumType.empty() && r.t == VT::Array) {
            // $val ~~ EnumType : the enum type object is a tagged pair-list
            res = (!l.enumType.empty() && l.enumType == r.enumType) || l.typeName() == r.enumType;
            return Value::boolean(op == "~~" ? res : !res);
        }
        if (r.t == VT::Range) {
            if (l.t == VT::Range) {
                // Range ~~ Range: containment — every element of l is in r
                double llo = l.rNum ? l.n : (double)l.rFrom;
                double lhi = l.rNum ? l.im : (double)l.rTo;
                double rlo = r.rNum ? r.n : (double)r.rFrom;
                double rhi = r.rNum ? r.im : (double)r.rTo;
                bool loOK = r.rExFrom ? (llo > rlo || (l.rExFrom && llo >= rlo))
                                      : (llo >= rlo);
                bool hiOK = r.rExTo ? (lhi < rhi || (l.rExTo && lhi <= rhi))
                                    : (lhi <= rhi);
                res = loOK && hiOK;
            }
            else if (r.ofType == "Str") {
                // Str range: string ordering between the endpoints ("b" ~~ "a".."c")
                const std::string v = l.toStr();
                const std::string lo = cpToU8((uint32_t)r.rFrom), hi = cpToU8((uint32_t)r.rTo);
                res = (r.rExFrom ? v > lo : v >= lo) &&
                      (r.rExTo ? v < hi : v <= hi);
            }
            else if (l.t == VT::Rat) { // exact endpoint compare: 4.99…(45 digits) ~~ 0..^5
                res = applyArith(r.rExFrom ? ">" : ">=", l, Value::integer(r.rFrom)).truthy() &&
                      applyArith(r.rExTo ? "<" : "<=", l, Value::integer(r.rTo)).truthy();
            } else {
                double v = l.toNum();
                double lo = r.rFrom, hi = r.rTo;
                res = v >= lo && (r.rExTo ? v < hi : v <= hi);
            }
        } else if (r.t == VT::Type) {
            // a subset name on the RHS: base-chain + where-clause check
            // (`5 ~~ Five`, `$conn ~~ ConnectionOrMessage` in Cro)
            if (g_subsetCheck) {
                bool sres = false;
                if (g_subsetCheck(r.s, l, sres))
                    return Value::boolean(op == "~~" ? sres : !sres);
            }
            res = (l.typeName() == r.s) || r.s == "Any" || r.s == "Mu" ||
                  (l.t == VT::Code && (r.s == "Code" || r.s == "Callable" ||
                   (r.s == "WhateverCode" && l.code && l.code->isWhateverCode))) ||
                  (r.s == "Numeric" && l.isNumeric()) || (r.s == "Cool") ||
                  (l.t == VT::Bool && (r.s == "Int" || r.s == "Real")) || // Bool is an Int-backed enum
                  (r.s == "Exception" && l.typeName().rfind("X::", 0) == 0) || // every X::* isa Exception
                  (l.t == VT::Hash && l.hashKind == "FileHandle" && (r.s == "IO::Handle" || r.s == "IO"));
            // an allomorph (IntStr/RatStr/NumStr) satisfies BOTH its numeric type and Str
            if (!res && l.isAllomorph())
                res = typeMatchesArg(l, r.s) || r.s == "Str" || r.s == "Stringy";
            // an object matches its class ancestry incl. a built-in parent (`is DateTime`)
            if (!res && l.t == VT::Object) res = typeMatchesArg(l, r.s);
            // TYPE ~~ TYPE: consult the type ancestry (Array ~~ Positional,
            // array[int] ~~ Positional[int], Mix ~~ Associative, …)
            if (!res && l.t == VT::Type) {
                std::string ln = l.s;
                size_t br = ln.find('['); if (br != std::string::npos) ln = ln.substr(0, br);
                static const std::map<std::string, std::set<std::string>> typeDoes = {
                    {"array", {"array", "Array", "List", "Positional", "Iterable", "Cool"}},
                    {"Array", {"Array", "List", "Positional", "Iterable", "Cool"}},
                    {"List",  {"List", "Positional", "Iterable", "Cool"}},
                    {"Seq",   {"Seq", "List", "Positional", "Iterable", "Cool"}},
                    {"Slip",  {"Slip", "List", "Positional", "Iterable", "Cool"}},
                    {"Hash",  {"Hash", "Map", "Associative", "Cool"}},
                    {"Map",   {"Map", "Associative", "Cool"}},
                    {"Set",   {"Set", "Setty", "QuantHash", "Associative"}},
                    {"SetHash", {"SetHash", "Setty", "QuantHash", "Associative"}},
                    {"Bag",   {"Bag", "Baggy", "QuantHash", "Associative"}},
                    {"BagHash", {"BagHash", "Baggy", "QuantHash", "Associative"}},
                    {"Mix",   {"Mix", "Mixy", "Baggy", "QuantHash", "Associative"}},
                    {"MixHash", {"MixHash", "Mixy", "Baggy", "QuantHash", "Associative"}},
                };
                std::string rn = r.s;
                auto td = typeDoes.find(ln);
                bool baseOk = (td != typeDoes.end() && td->second.count(rn));
                if (!baseOk) { // numeric/string tower (Int -> Real -> Numeric -> Cool -> Any -> Mu)
                    static const std::map<std::string, std::vector<std::string>> tower = {
                        {"Int", {"Real", "Numeric", "Cool"}}, {"Num", {"Real", "Numeric", "Cool"}},
                        {"Rat", {"Real", "Numeric", "Cool"}}, {"Complex", {"Numeric", "Cool"}},
                        {"Str", {"Stringy", "Cool"}}, {"Bool", {"Cool"}},
                    };
                    auto tw = tower.find(ln);
                    if (tw != tower.end()) for (auto& anc : tw->second) if (anc == rn) { baseOk = true; break; }
                }
                if (baseOk && (r.ofType.empty() || r.ofType == l.ofType)) res = true;
                // a user type object matches its own ancestry: parent classes,
                // composed roles, and roles/parents anywhere up the chain
                if (!res && g_matchClasses) {
                    auto itc = g_matchClasses->find(ln);
                    if (itc != g_matchClasses->end())
                        for (ClassInfo* c = itc->second.get(); c && !res; c = c->parent.get()) {
                            if (c->name == rn || c->doneRoles.count(rn)) { res = true; break; }
                            for (auto& p : c->extraParents)
                                if (p && (p->name == rn || p->doneRoles.count(rn))) { res = true; break; }
                        }
                }
            }
            // role / container types (Positional, Associative, …) that a value does
            if (!res) {
                if ((r.s == "Positional" || r.s == "Iterable") && l.t == VT::Array) res = true;
                else if (r.s == "Iterable" && l.t == VT::Range) res = true;
                else if ((r.s == "Associative" || r.s == "Map") && l.t == VT::Hash) res = true;
                else if (r.s == "Callable" && l.t == VT::Code) res = true;
                else if (r.s == "Stringy" && l.t == VT::Str) res = true;
                else if (r.s == "Real" && l.isNumeric() && l.t != VT::Complex) res = true;
                // Buf does Blob; the sized views (buf8/blob8/utf8…) share our
                // byte representation, so a Buf answers any buf* type
                else if (l.t == VT::Str && (l.hashKind == "Buf" || l.hashKind == "Blob")) {
                    if (r.s == "Blob") res = true;
                    else if (l.hashKind == "Buf" && r.s.rfind("buf", 0) == 0) res = true;
                    else if (l.hashKind == "Blob" && (r.s.rfind("blob", 0) == 0 || r.s.rfind("utf", 0) == 0)) res = true;
                }
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
        } else if (r.t == VT::Hash &&
                   (r.hashKind.rfind("Set", 0) == 0 || r.hashKind.rfind("Bag", 0) == 0 ||
                    r.hashKind.rfind("Mix", 0) == 0)) {
            // Setty/Baggy ACCEPTS: coerce the topic to the invocant's kind and compare
            // contents — keys for Set (counts collapse), keys+counts for Bag/Mix.
            // (`1 ~~ set(1,2)` is False — membership is (elem), not smartmatch.)
            std::string root = r.hashKind.substr(0, 3);
            std::map<std::string, double> want, got;
            auto feed = [&](const Value& v, std::map<std::string, double>& m) {
                if (v.t == VT::Hash && v.hash &&
                    (v.hashKind.rfind("Set", 0) == 0 || v.hashKind.rfind("Bag", 0) == 0 ||
                     v.hashKind.rfind("Mix", 0) == 0)) {
                    bool vSet = v.hashKind.rfind("Set", 0) == 0;
                    for (auto& kv : *v.hash) m[kv.first] += vSet ? 1.0 : kv.second.toNum();
                }
                else if (v.t == VT::Array || v.t == VT::Range) { for (auto& e : v.flatten()) m[e.toStr()] += 1.0; }
                else m[v.toStr()] += 1.0;
            };
            feed(r, want); feed(l, got);
            res = want.size() == got.size();
            if (res) for (auto& kv : want) {
                auto it = got.find(kv.first);
                if (it == got.end() || (root != "Set" && it->second != kv.second)) { res = false; break; }
            }
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
            // NaN ~~ NaN is True even though NaN == NaN is False ('NaN' ~~ NaN coerces
            // the string to NaN); ACCEPTS special-cases NaN.
            if (!res && r.t == VT::Num && std::isnan(r.n) && std::isnan(l.toNum())) res = true;
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

// Interpolate @array variables into a regex as a first-match alternation of the
// elements' literal (quotemeta'd) text, LONGEST-FIRST — `/@alpha/` matches any
// element, as in Rakudo (LTM over literal alternatives == longest-first || here).
// Base64 decodes via `$str.comb(/@alpha/)`. Left untouched: `@<name>` list
// captures, escaped `\@`, '…' literal spans, and unknown/empty arrays.
std::string Interpreter::rxInterpArrays(const std::string& pat) {
    if (pat.find('@') == std::string::npos || !tctx_.cur) return pat;
    std::string out;
    bool inSq = false; // inside '…': a literal span — no interpolation
    for (size_t i = 0; i < pat.size(); i++) {
        if (pat[i] == '\\' && i + 1 < pat.size()) { out += pat[i]; out += pat[i + 1]; i++; continue; }
        if (pat[i] == '\'') { inSq = !inSq; out += pat[i]; continue; }
        if (inSq) { out += pat[i]; continue; }
        if (pat[i] == '@' && i + 1 < pat.size() &&
            (std::isalpha((unsigned char)pat[i + 1]) || pat[i + 1] == '_')) {
            size_t j = i + 1;
            while (j < pat.size() && (std::isalnum((unsigned char)pat[j]) || pat[j] == '_' ||
                   (pat[j] == '-' && j + 1 < pat.size() && std::isalpha((unsigned char)pat[j + 1])))) j++;
            Value* v = tctx_.cur->find("@" + pat.substr(i + 1, j - i - 1));
            if (v && v->t == VT::Array && v->arr && !v->arr->empty()) {
                std::vector<std::string> els;
                for (auto& e : *v->arr) els.push_back(e.toStr());
                std::stable_sort(els.begin(), els.end(),
                    [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
                out += "[ ";
                for (size_t k = 0; k < els.size(); k++) {
                    if (k) out += " || ";
                    out += quoteMetaRx(els[k]);
                }
                out += " ]";
                i = j - 1; continue;
            }
        }
        out += pat[i];
    }
    return out;
}

Value Interpreter::regexMatch(const std::string& subject, const std::string& pattern,
                              const Value* rxVal) {
    // wired mode: an anonymous `regex {…}` value — its code blocks and
    // assertions execute for real, in a per-match child of the scope the
    // regex closed over (`:my`/`$cap` persist across blocks within a match)
    bool wired = rxVal && rxVal->t == VT::Regex && !rxVal->hashKind.empty() && rxVal->ext;
    std::shared_ptr<Env> savedCurWired;
    if (wired) {
        savedCurWired = tctx_.cur;
        auto matchEnv = std::make_shared<Env>();
        matchEnv->parent = std::static_pointer_cast<Env>(rxVal->ext);
        tctx_.cur = matchEnv;
    }
    struct WiredGuard {
        Interpreter* I; std::shared_ptr<Env> saved; bool active;
        ~WiredGuard() { if (active) I->tctx_.cur = std::move(saved); }
    } wiredGuard{this, savedCurWired, wired};
    std::string pat = pattern;
    bool global = false;
    for (const char* adv : {":g ", ":global "}) // :g / :global adverb
        { size_t gp = pat.find(adv); if (gp != std::string::npos) { global = true; pat.erase(gp, strlen(adv)); } }
    bool exhaustive = false;
    for (const char* adv : {":exhaustive ", ":ex "}) // :ex / :exhaustive adverb
        { size_t gp = pat.find(adv); if (gp != std::string::npos) { exhaustive = true; pat.erase(gp, strlen(adv)); } }
    // counted adverbs — m:nth(N)/, m:nth(*)/, m:nth(2,3):global/, ordinals m:3rd/
    // (sloppy suffixes like :7st accepted, as in Rakudo)
    bool haveNth = false, nthStar = false; long long nthOfs = 0;
    std::vector<long long> nthList;
    {
        auto nthArg = [&](std::string a) {
            size_t b = a.find_first_not_of(" \t"), e2 = a.find_last_not_of(" \t");
            a = (b == std::string::npos) ? "" : a.substr(b, e2 - b + 1);
            if (a == "*") { nthStar = true; return; }
            if (a.rfind("*-", 0) == 0) { nthStar = true; nthOfs = std::strtoll(a.c_str() + 2, nullptr, 10); return; }
            Value v; try { v = evalString(a); } catch (...) {}
            double d = v.toNum();
            if (!(d >= 1) || std::isnan(d) || std::isinf(d))
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Attempt to retrieve match with :nth(" + a + "), but :nth must be a positive whole number"};
            nthList.push_back((long long)d);
        };
        size_t np;
        while ((np = pat.find(":nth(")) != std::string::npos) {
            size_t j = np + 5; int depth = 1; std::string arg;
            while (j < pat.size() && depth > 0) {
                char c = pat[j];
                if (c == '(') depth++;
                else if (c == ')') { if (--depth == 0) break; }
                arg += c; j++;
            }
            haveNth = true;
            size_t start = 0, comma;
            while ((comma = arg.find(',', start)) != std::string::npos) { nthArg(arg.substr(start, comma - start)); start = comma + 1; }
            nthArg(arg.substr(start));
            pat.erase(np, j + 1 - np);
        }
        // ordinal form: `:` digits + two-letter suffix
        for (size_t i = 0; i + 3 < pat.size(); i++) {
            if (pat[i] != ':' || !std::isdigit((unsigned char)pat[i + 1])) continue;
            if (i > 0 && (std::isalnum((unsigned char)pat[i - 1]) || pat[i - 1] == ':')) continue;
            size_t d = i + 1;
            while (d < pat.size() && std::isdigit((unsigned char)pat[d])) d++;
            if (d + 2 > pat.size()) break;
            std::string suf = pat.substr(d, 2);
            if (suf != "st" && suf != "nd" && suf != "rd" && suf != "th") continue;
            if (d + 2 < pat.size() && std::isalnum((unsigned char)pat[d + 2])) continue;
            long long n = std::strtoll(pat.c_str() + i + 1, nullptr, 10);
            if (n < 1)
                throw RakuError{Value::typeObj("X::AdHoc"),
                    "Attempt to retrieve match with :nth(" + std::to_string(n) + "), but :nth must be a positive whole number"};
            haveNth = true; nthList.push_back(n);
            pat.erase(i, d + 2 - i);
            i--; // rescan from the same spot
        }
    }
    // Interpolate $scalar variables into the pattern as their literal (quotemeta'd)
    // value — `/$x/` matches the contents of $x. Leaves $0.. backrefs, $<name>,
    // special vars, escaped \$, and the end-anchor $ untouched.
    // (wired mode: no textual interpolation — $vars inside code blocks belong
    // to the code, and pattern atoms resolve at match time via the str hook)
    if (!wired && pat.find('$') != std::string::npos && tctx_.cur) {
        std::string out;
        bool inSq = false; // inside '…': a literal span — $vars do NOT interpolate there
        for (size_t i = 0; i < pat.size(); i++) {
            if (pat[i] == '\\' && i + 1 < pat.size()) { out += pat[i]; out += pat[i + 1]; i++; continue; }
            if (pat[i] == '\'') { inSq = !inSq; out += pat[i]; continue; }
            if (inSq) { out += pat[i]; continue; }
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
    if (!wired) pat = rxInterpArrays(pat); // `/@alpha/` — array elements as longest-first alternation
    // flavor flags for anonymous declarators: token = ratchet, rule = ratchet+sigspace
    std::string reFlags = wired ? (rxVal->hashKind == "token" ? "r"
                                 : rxVal->hashKind == "rule" ? "sr" : "") : "";
    Regex re(pat, reFlags);
    if (!re.obsolete().empty())
        throw RakuError{Value::typeObj("X::Obsolete"),
            "Unsupported use of " + re.obsolete() + "; this Perl 5 metacharacter is gone in Raku"};
    // resolve <NAME> subrules against lexical `my regex/token NAME {…}`; unknown names stay
    // lenient (zero-width) so existing patterns with unhandled assertions don't start failing.
    // A lexical name also SHADOWS a same-named built-in subrule (my regex ident {…} beats <ident>).
    std::set<std::string> lexNames;
    for (auto& kv : namedRegex_) lexNames.insert(kv.first);
    SubResolver resolver;
    resolver = [&](const std::string& name, const std::string& subj, long pos, RxMatch& out) -> bool {
        if (name == "ws") { long p = pos; while (p < (long)subj.size() && std::isspace((unsigned char)subj[p])) p++; out.from = pos; out.to = p; out.matched = true; return true; }
        auto it = namedRegex_.find(name);
        if (it == namedRegex_.end()) { out.from = pos; out.to = pos; out.matched = true; return true; }
        const std::string& kind = namedRegexKind_[name];
        std::string flags = kind == "rule" ? "sr"       // rule:  sigspace + ratchet
                          : kind == "token" ? "r"        // token: ratchet (no backtracking)
                          : "";                          // regex: backtracking, no sigspace
        Regex sub(it->second, flags);
        return sub.matchAt(subj, pos, out, resolver, &lexNames);
    };
    auto build = [&](const RxMatch& m) {
        Value v = Value::matchVal(subject.substr(m.from, m.to - m.from), m.from, m.to);
        v.ext = std::make_shared<std::string>(subject); // the original, for .prematch/.postmatch/.orig
        for (size_t ci = 0; ci < m.caps.size(); ci++) {
            if (m.listCaps.count((int)ci)) { // `(…)+`/`(…)*`/`(…)**n` → $ci is an Array of every occurrence
                Value lst = Value::array();
                auto it = m.capReps.find((int)ci);
                if (it != m.capReps.end())
                    for (auto& o : it->second) lst.arrRef().push_back(Value::matchVal(subject.substr(o.first, o.second - o.first), o.first, o.second));
                v.arrRef().push_back(lst);
                continue;
            }
            auto& c = m.caps[ci];
            if (c.first < 0) v.arrRef().push_back(Value::nil());
            else v.arrRef().push_back(Value::matchVal(subject.substr(c.first, c.second - c.first), c.first, c.second));
        }
        for (auto& kv : m.named)
            if (!m.children.count(kv.first))
                v.hashRef()[kv.first] = Value::matchVal(subject.substr(kv.second.first, kv.second.second - kv.second.first), kv.second.first, kv.second.second);
        for (auto& kv : m.children) {
            // `%<name>=…` — each occurrence's matched text is a Hash KEY (value undefined)
            if (m.hashNames && m.hashNames->count(kv.first)) {
                Value h = Value::makeHash();
                for (auto& c : kv.second) h.hashRef()[subject.substr(c.from, c.to - c.from)] = Value::any();
                v.hashRef()[kv.first] = h;
                continue;
            }
            // a capture repeated under a quantifier collates into a list of Matches —
            // and a quantified name is a list even with a single occurrence
            bool asList = kv.second.size() > 1
                       || (m.listNames && m.listNames->count(kv.first));
            if (!asList) {
                auto& c = kv.second[0];
                v.hashRef()[kv.first] = Value::matchVal(subject.substr(c.from, c.to - c.from), c.from, c.to);
            } else {
                Value arr = Value::array(); arr.isList = true;
                for (auto& c : kv.second) arr.arr->push_back(Value::matchVal(subject.substr(c.from, c.to - c.from), c.from, c.to));
                v.hashRef()[kv.first] = arr;
            }
        }
        // a quantified capture that matched zero times is an empty list, not absent
        if (m.listNames) for (auto& nm : *m.listNames)
            if (!m.children.count(nm) && !v.hashRef().count(nm)) {
                Value arr = Value::array(); arr.isList = true;
                v.hashRef()[nm] = arr;
            }
        return v;
    };
    if (haveNth) { // m:nth(...)/ — enumerate all matches, keep the selected ones
        std::vector<RxMatch> all;
        long pos = 0;
        while (re.ok() && pos <= (long)subject.size()) {
            RxMatch m;
            if (!re.search(subject, pos, m, resolver, &lexNames)) break;
            all.push_back(m);
            pos = (m.to > m.from) ? m.to : m.to + 1;
        }
        long long total = (long long)all.size();
        if (nthStar) nthList.push_back(total - nthOfs);
        std::sort(nthList.begin(), nthList.end());
        std::vector<Value> picked;
        for (long long n : nthList)
            if (n >= 1 && n <= total) picked.push_back(build(all[n - 1]));
        if (picked.empty()) { setMatchVar(Value::nil()); return Value::nil(); }
        if (nthList.size() == 1 && !global) { setMatchVar(picked[0]); return picked[0]; }
        Value list = Value::array(); list.isList = true;
        for (auto& p : picked) list.arr->push_back(p);
        setMatchVar(list);
        return list;
    }
    if (exhaustive) { // m:ex// — a List of every match at every position and length
        Value list = Value::array(); list.isList = true;
        if (re.ok())
            for (auto& m : re.searchExhaustive(subject, resolver, &lexNames)) list.arr->push_back(build(m));
        setMatchVar(list);
        return list;
    }
    if (global) { // m:g// — a List of every match
        Value list = Value::array(); list.isList = true;
        long pos = 0;
        while (re.ok() && pos <= (long)subject.size()) {
            RxMatch m;
            if (!re.search(subject, pos, m, resolver, &lexNames)) break;
            list.arr->push_back(build(m));
            pos = (m.to > m.from) ? m.to : m.to + 1; // advance past zero-width matches
        }
        setMatchVar(list);
        return list;
    }
    RxMatch m;
    Value mv;
    std::shared_ptr<Value> inlineMade; // `{ make … }` inside a plain regex
    GrammarHooks rmHooks;
    bool wantHooks = false;
    // engage ONLY for make-blocks: running arbitrary {…} side effects during
    // backtracking/LTM measurement broke subst.t and longest-alternative.t
    if (pat.find("make") != std::string::npos && pat.find('{') != std::string::npos) {
        rmHooks.run = [this, &inlineMade](const std::string& code, long, long,
                                          const GrammarHooks::NamedMap&, const GrammarHooks::ParamMap&) {
            if (code.find("make") == std::string::npos) return; // other blocks stay inert
            Value tgt; tctx_.makeTargets.push_back(&tgt);
            try { evalString(code); } catch (...) {}
            tctx_.makeTargets.pop_back();
            if (tgt.pairVal) inlineMade = tgt.pairVal;
        };
        wantHooks = true;
    }
    // `\w**{$n}` / `**{ 1..3 }` — a runtime-bounded quantifier in a plain `~~`
    // regex needs the range hook too (without it the bounds default to 0..* and
    // the quantifier matches greedily). Mirror the grammar range hook.
    if (pat.find("**") != std::string::npos && pat.find('{') != std::string::npos) {
        rmHooks.range = [this](const std::string& code, const GrammarHooks::NamedMap&,
                               const GrammarHooks::ParamMap&) -> std::pair<long, long> {
            bool unbounded = code.find("..*") != std::string::npos || code.find("..Inf") != std::string::npos ||
                             code.find("..\xE2\x88\x9E") != std::string::npos;
            Value v = evalString(code);
            if (v.t == VT::Range) {
                long lo = v.rFrom, hi = unbounded ? -1 : (v.rExTo ? v.rTo - 1 : v.rTo);
                if (v.rTo >= (long long)1e15) hi = -1;
                return {lo, hi};
            }
            long n = v.toInt(); return {n, n};
        };
        wantHooks = true;
    }
    if (wired) {
        // full hooks: every block runs (make-blocks capture the ast), assertions
        // evaluate for real, all in the wired match scope
        rmHooks.run = [this, &inlineMade](const std::string& code, long, long,
                                          const GrammarHooks::NamedMap&, const GrammarHooks::ParamMap&) {
            if (code.find("make") != std::string::npos) {
                Value tgt; tctx_.makeTargets.push_back(&tgt);
                try { evalString(code); } catch (...) {}
                tctx_.makeTargets.pop_back();
                if (tgt.pairVal) inlineMade = tgt.pairVal;
            }
            else { try { evalString(code); } catch (...) {} }
        };
        rmHooks.assertPass = [this](const std::string& code, long, long,
                                    const GrammarHooks::NamedMap&, const GrammarHooks::ParamMap&) -> bool {
            try { return evalString(code).truthy(); } catch (...) { return false; }
        };
        if (!rmHooks.range) rmHooks.range = [this](const std::string& code, const GrammarHooks::NamedMap&,
                                                   const GrammarHooks::ParamMap&) -> std::pair<long, long> {
            Value v; try { v = evalString(code); } catch (...) {}
            if (v.t == VT::Range) return {(long)v.rFrom, (long)(v.rExTo ? v.rTo - 1 : v.rTo)};
            long n = v.toInt(); return {n, n};
        };
        wantHooks = true;
    }
    if (wantHooks) re.runHooks = &rmHooks;
    if (re.ok() && re.search(subject, 0, m, resolver, &lexNames)) mv = build(m);
    else mv = Value::nil();
    if (mv.t == VT::Match && inlineMade) mv.pairVal = inlineMade; // $/.ast
    setMatchVar(mv);
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
            bool asList = kv.second.size() > 1
                       || (mm.listNames && mm.listNames->count(kv.first));
            if (!asList) {
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
            if (s[i] == '\\' && i + 1 < s.size()) {
                // the replacement side is qq-ish: decode the standard escapes
                // (s/$/\n/ appends a NEWLINE, not the letter n)
                char c = s[i + 1]; i++;
                switch (c) {
                    case 'n': r += '\n'; break;
                    case 't': r += '\t'; break;
                    case 'r': r += '\r'; break;
                    case '0': r += '\0'; break;
                    case 'e': r += '\x1b'; break;
                    case 'a': r += '\a'; break;
                    case 'f': r += '\f'; break;
                    case 'b': r += '\b'; break;
                    default:  r += c;    break;
                }
                continue;
            }
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
        setMatchVar(mv);
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
    setMatchVar(last);
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

// `target ~~ s/pat/repl/` as one runtime call — mirrors the evalBinary SubstLit
// branch so native codegen can compile substitutions instead of bailing to bundle.
Value Interpreter::substApply(Value* target, const std::string& pattern, const std::string& repl, bool nonMut) {
    Value subj = target ? *target : Value::str("");
    if (isTrSubst(pattern)) { // tr/from/to/ — transliteration, returns the count changed
        long long n; std::string out = translit(subj.toStr(), pattern.substr(1), repl, n);
        if (target && !nonMut) *target = Value::str(out);
        return Value::integer(n);
    }
    long nsub = 0; ValueList noArgs; Value mres;
    std::string out = substSelect(subj.toStr(), pattern, nullptr, noArgs, nsub, false, &repl, &mres);
    if (nonMut) return Value::str(out); // S/// : return the new string, leave the target intact
    if (target) *target = Value::str(out);
    return mres;                        // s/// returns the Match / List of matches
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
        realPat = rxInterpArrays(realPat); // `/@alpha/` — element alternation (comb path)
    }
    // `\w**{$n}` / `**{1..3}` runtime-bounded quantifier: wire the range hook so
    // the bounds are evaluated at match time (without it they default to 0..*).
    GrammarHooks ssHooks;
    bool needRangeHook = !literal && realPat.find("**") != std::string::npos && realPat.find('{') != std::string::npos;
    if (needRangeHook) {
        ssHooks.range = [this](const std::string& code, const GrammarHooks::NamedMap&,
                               const GrammarHooks::ParamMap&) -> std::pair<long, long> {
            bool unbounded = code.find("..*") != std::string::npos || code.find("..Inf") != std::string::npos ||
                             code.find("..\xE2\x88\x9E") != std::string::npos;
            Value v = evalString(code);
            if (v.t == VT::Range) {
                long lo = v.rFrom, hi = unbounded ? -1 : (v.rExTo ? v.rTo - 1 : v.rTo);
                if (v.rTo >= (long long)1e15) hi = -1;
                return {lo, hi};
            }
            long n = v.toInt(); return {n, n};
        };
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
        if (needRangeHook) re.runHooks = &ssHooks;
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
        if (needRangeHook) re.runHooks = &ssHooks;
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
        for (size_t ci = 0; ci < mm.caps.size(); ci++) {
            if (mm.listCaps.count((int)ci)) { // repeated capture → $ci is an Array
                Value lst = Value::array();
                auto it = mm.capReps.find((int)ci);
                if (it != mm.capReps.end())
                    for (auto& o : it->second) lst.arrRef().push_back(Value::matchVal(subj.substr(o.first, o.second - o.first), o.first, o.second));
                v.arrRef().push_back(lst);
                continue;
            }
            auto& c = mm.caps[ci];
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
            if (s[i] == '\\' && i + 1 < s.size()) {
                // the replacement side is qq-ish: decode the standard escapes
                // (s/$/\n/ appends a NEWLINE, not the letter n)
                char c = s[i + 1]; i++;
                switch (c) {
                    case 'n': r += '\n'; break;
                    case 't': r += '\t'; break;
                    case 'r': r += '\r'; break;
                    case '0': r += '\0'; break;
                    case 'e': r += '\x1b'; break;
                    case 'a': r += '\a'; break;
                    case 'f': r += '\f'; break;
                    case 'b': r += '\b'; break;
                    default:  r += c;    break;
                }
                continue;
            }
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
            setMatchVar(matchV);
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
    // $/ / the s/// return value: no match → falsey Nil; :g/:nth-list → a List of
    // Matches; a SINGLE ordinal (:2nd, :nth(2)) yields the one Match even with :g.
    // (`.Str` = matched text, `+` of the :g List = count.)
    bool nthScalar = haveNth && !(nthVal.t == VT::Array || nthVal.t == VT::Range);
    Value result;
    if (selMatches.empty()) result = Value::nil();
    else if (nthScalar && selMatches.size() == 1) result = selMatches.back();
    else if (global || haveNth || haveX) { result = Value::list(selMatches); } // multi-match adverbs → List
    else result = selMatches.back();
    if (!literal) setMatchVar(result); // a literal (string) .subst leaves $/ untouched
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
    // Rule names in DECLARATION order (base grammar first, then derived), so proto
    // candidates keep source order — the final LTM tie-break (S05: earliest-declared
    // wins on an equal-length, equal-specificity match). A std::map would give
    // alphabetical order and silently mis-rank ties.
    std::vector<std::string> declOrder;
    std::set<std::string> declSeen;
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
        for (auto& nm : c->ruleOrder) if (declSeen.insert(nm).second) declOrder.push_back(nm);
    };
    collect(g);
    // Safety net: any rule not seen via ruleOrder still gets ranked (append in map order).
    for (auto& r : gm.rules) if (declSeen.insert(r.first).second) declOrder.push_back(r.first);

    // Register protoregex candidates: `element:<null>` / `element:sym<x>` is a
    // candidate of proto `element`; matching `<element>` tries them all (LTM).
    for (auto& nm : declOrder) {
        size_t c = nm.find(":sym<");
        if (c == std::string::npos) c = nm.find(":sym\xC2\xAB"); // :sym«…»
        if (c == std::string::npos) c = nm.find(":<");
        if (c != std::string::npos && c > 0) gm.protos[nm.substr(0, c)].push_back(nm);
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
        setMatchVar(m);
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
        for (size_t ci = 0; ci < pn.caps.size(); ci++) {
            // a positional capture under a repetition quantifier is an ARRAY of
            // every occurrence (`(...)+` → @$0), as in Rakudo — Cro::Uri's pchars
            // action concatenates `@$0` chunks to rebuild a path segment
            if (pn.listCaps && pn.listCaps->count((int)ci)) {
                Value lst = Value::array(); lst.isList = true;
                if (pn.capReps) {
                    auto it = pn.capReps->find((int)ci);
                    if (it != pn.capReps->end())
                        for (auto& o : it->second)
                            lst.arr->push_back(Value::matchVal(input.substr(o.first, o.second - o.first), o.first, o.second));
                }
                mv.arrRef().push_back(std::move(lst));
                continue;
            }
            auto& c = pn.caps[ci];
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
            // a name captured more than once ($<num> ... $<num>) collates into a list —
            // and a name under a quantifier (<item>+) is a list even with one occurrence
            bool asList = kv.second.size() > 1
                       || (pn.listNames && pn.listNames->count(kv.first));
            if (!asList) mv.hashRef()[kv.first] = buildChild(kv.second[0]);
            else {
                Value arr = Value::array(); arr.isList = true;
                for (auto& child : kv.second) arr.arr->push_back(buildChild(child));
                mv.hashRef()[kv.first] = arr;
            }
        }
        // a quantified capture that matched zero times is an empty list, not absent
        if (pn.listNames) for (auto& nm : *pn.listNames)
            if ((!pn.kids || !pn.kids->count(nm)) && !mv.hashRef().count(nm)) {
                Value arr = Value::array(); arr.isList = true;
                mv.hashRef()[nm] = arr;
            }
        // a deferred inline `{ make … }` runs now, with $/ = this fully-built match
        // (so it can read `$/.values[0].ast` etc.) and this node as the make target.
        // Pop the innermost queued make for this span — but only when THIS rule's
        // body actually contains the block: a parent and its only child share the
        // span (`token TOP { <number> {make …} }`), and the make is the parent's.
        auto mc = pendingMakeCode->find({pn.from, pn.to});
        if (std::getenv("RAKUPP_DEBUG_MAKE"))
            fprintf(stderr, "[make] node=%s span=%ld..%ld queued=%s\n", pn.name.c_str(), pn.from, pn.to,
                    mc != pendingMakeCode->end() && !mc->second.empty() ? mc->second.front().c_str() : "(none)");
        if (mc != pendingMakeCode->end() && !mc->second.empty()) {
            const std::string* rulePat = g ? g->findRule(
                pn.actualRule.empty() ? pn.name : pn.actualRule) : nullptr;
            if (rulePat && rulePat->find(mc->second.front()) == std::string::npos) {
                // a proto's tree node keeps the PROTO name while the make block
                // lives in the winning `name:sym<…>` candidate — accept it if
                // any candidate's pattern contains the code
                bool inCandidate = false;
                std::string prefix = pn.name + ":";
                for (const ClassInfo* ci = g; ci && !inCandidate; ci = ci->parent.get())
                    for (auto& rk : ci->rules)
                        if (rk.first.rfind(prefix, 0) == 0 &&
                            rk.second.find(mc->second.front()) != std::string::npos) { inCandidate = true; break; }
                if (!inCandidate) mc = pendingMakeCode->end();
            }
        }
        if (mc != pendingMakeCode->end() && !mc->second.empty()) {
            std::string code = mc->second.front();
            mc->second.erase(mc->second.begin());
            if (auto prog = parseCode(code)) {
                Value* slot = tctx_.cur->find("$/"); Value saved = slot ? *slot : Value::nil();
                setMatchVar(mv);
                tctx_.makeTargets.push_back(&mv);
                try { for (auto& s : prog->stmts) exec(s.get()); } catch (...) {}
                tctx_.makeTargets.pop_back();
                if (Value* s2 = tctx_.cur->find("$/")) *s2 = saved;
            }
        }
        auto pm = pendingMakes->find({pn.from, pn.to});
        if (pm != pendingMakes->end() && !mv.pairVal) mv.pairVal = std::make_shared<Value>(pm->second);
        // an action-class method (if any) can still override; a proto node
        // dispatches to the winning candidate's method (`x:sym<y>`), falling
        // back to a method named after the proto itself
        if (!pn.actualRule.empty() && pn.actualRule != pn.name) runAction(pn.actualRule, mv);
        runAction(pn.name, mv);
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
        if (!matched) {
            tctx_.cur = savedScope;
            setMatchVar(Value::nil()); return Value::nil();
        }
        // build with the match scope still current: a deferred `{ make … }`
        // may read the rule's `:my` vars (Cro's route matcher makes `$cap`)
        Value mv;
        try { mv = build(tree); }
        catch (...) { tctx_.cur = savedScope; throw; }
        tctx_.cur = savedScope;
        setMatchVar(mv);
        return mv;
    }
}

// Instant/Duration algebra: Instant−Instant→Duration, Instant±Duration→Instant,
// Duration±x→Duration; everything else drops to plain numbers (like Rakudo's *).
static void tagTemporal(const std::string& op, const Value& l, const Value& r, Value& res) {
    if (!(op == "+" || op == "-") || !res.isNumeric() || !res.hashKind.empty()) return;
    bool li = l.hashKind == "Instant", ri = r.hashKind == "Instant";
    bool ld = l.hashKind == "Duration", rd = r.hashKind == "Duration";
    if (!(li || ri || ld || rd)) return;
    if (op == "-") res.hashKind = (li && ri) ? "Duration" : li ? "Instant" : "Duration";
    else res.hashKind = (li || ri) ? "Instant" : "Duration";
}

// Shared hyper-operator core for every spelling (`>>op<<`, `»op«`, and the
// bracketed `>>[&op]<<` form). A `>>` on the left / `<<` on the right marks
// that side STRICT — it dictates the shape; a dwimmy side is CYCLED to the
// strict length (truncating when longer), but a strict SCALAR facing a list
// dies, as does a dwimmy side that is known-infinite in a position where no
// finite side dictates the length. Hash keysets follow Rakudo: both strict →
// union, one strict → that side's keys, both dwimmy → intersection.
Value Interpreter::hyperCore(Value& l, Value& r, bool strictL, bool strictR,
        const std::function<Value(const Value&, const Value&, Value*, Value*)>& apply,
        Value* lroot, Value* rroot, bool wantSlots) {
    // element-level distribution: a Pair keeps its key and ops its value; a
    // nested array/hash element recurses with the same marker rules — so
    // ([1,2],[3,[4,5]]) »+« ([6,7],[8,[9,10]]) distributes deeply, and a hash
    // sitting in an array ops its VALUES against the other side's element.
    std::function<Value(const Value&, const Value&, Value*, Value*)> deepApply =
        [&](const Value& x, const Value& y, Value* xs, Value* ys) -> Value {
        bool xP = x.t == VT::Pair, yP = y.t == VT::Pair;
        if (xP || yP) {
            const std::string& key = xP ? x.s : y.s;
            const Value& xv = xP && x.pairVal ? *x.pairVal : x;
            const Value& yv = yP && y.pairVal ? *y.pairVal : y;
            return Value::pair(key, deepApply(xv, yv, nullptr, nullptr));
        }
        bool xC = (x.t == VT::Array && x.arr) || x.t == VT::Range || (x.t == VT::Hash && x.hash);
        bool yC = (y.t == VT::Array && y.arr) || y.t == VT::Range || (y.t == VT::Hash && y.hash);
        if (xC || yC) {
            Value x2 = x, y2 = y;
            Value res = hyperCore(x2, y2, strictL, strictR, apply);
            // an itemized [..] operand yields an itemized (mutable) Array result
            if (res.t == VT::Array && ((x.t == VT::Array && !x.isList) ||
                                       (y.t == VT::Array && !y.isList)))
                res.isList = false;
            return res;
        }
        return apply(x, y, xs, ys);
    };
    auto dwimDie = [&]() -> RakuError {
        return RakuError{Value::typeObj("X::HyperOp::NonDWIM"),
            "Lists on either side of non-dwimmy hyperop are not of the same length"};
    };
    static const std::set<std::string> settyKinds = {
        "Set", "SetHash", "Bag", "BagHash", "Mix", "MixHash"};
    // rebuild a per-key result with the source hash's kind (SetHash/BagHash/…
    // re-coerce their counts exactly like `%h is Kind = pairs` does)
    auto hashOut = [&](const std::vector<std::pair<std::string, Value>>& pairs,
                       const Value& src) -> Value {
        if (src.t == VT::Hash && settyKinds.count(src.hashKind)) {
            ValueList pl;
            for (auto& kv : pairs) pl.push_back(Value::pair(kv.first, kv.second));
            return makeBaggy(pl, src.hashKind);
        }
        Value out = Value::makeHash();
        out.hashKind = src.t == VT::Hash ? src.hashKind : "";
        for (auto& kv : pairs) (*out.hash)[kv.first] = kv.second;
        return out;
    };
    bool lHash = l.t == VT::Hash && l.hash;
    bool rHash = r.t == VT::Hash && r.hash;
    if (lHash && rHash) {
        std::vector<std::pair<std::string, Value>> pairs;
        auto lval = [&](const std::string& k) { auto it = l.hash->find(k); return it != l.hash->end() ? it->second : Value::any(); };
        auto rval = [&](const std::string& k) { auto it = r.hash->find(k); return it != r.hash->end() ? it->second : Value::any(); };
        auto lslot = [&](const std::string& k) -> Value* { auto it = l.hash->find(k); return it != l.hash->end() ? &it->second : nullptr; };
        auto rslot = [&](const std::string& k) -> Value* { auto it = r.hash->find(k); return it != r.hash->end() ? &it->second : nullptr; };
        auto applyK = [&](const std::string& k) {
            pairs.push_back({k, deepApply(lval(k), rval(k), lslot(k), rslot(k))});
        };
        if (strictL && strictR) { // union — a strict side may not be shrunk
            for (auto& kv : *l.hash) applyK(kv.first);
            for (auto& kv : *r.hash) if (!l.hash->count(kv.first)) applyK(kv.first);
        }
        else if (strictL) for (auto& kv : *l.hash) applyK(kv.first);
        else if (strictR) for (auto& kv : *r.hash) applyK(kv.first);
        else { // both dwimmy → intersection
            for (auto& kv : *l.hash) if (r.hash->count(kv.first)) applyK(kv.first);
        }
        return hashOut(pairs, l);
    }
    if (lHash || rHash) {
        // hash ⨯ scalar / scalar ⨯ hash: the scalar side must be dwimmy
        // (it's "extended" over every key); a strict scalar side dies.
        Value& h = lHash ? l : r;
        if (lHash ? strictR : strictL) throw dwimDie();
        std::vector<std::pair<std::string, Value>> pairs;
        for (auto& kv : *h.hash)
            pairs.push_back({kv.first, lHash ? deepApply(kv.second, r, &kv.second, rroot)
                                             : deepApply(l, kv.second, lroot, &kv.second)});
        return hashOut(pairs, h);
    }
    bool lIter = l.t == VT::Array || l.t == VT::Range;
    bool rIter = r.t == VT::Array || r.t == VT::Range;
    auto isInf = [](const Value& v) {
        if (v.t == VT::Range && v.rTo >= 9000000000000000000LL) return true;
        if (v.t == VT::Array && v.ext)
            return std::static_pointer_cast<LazySeqState>(v.ext)->infinite;
        return false;
    };
    bool lInf = isInf(l), rInf = isInf(r);
    if (lInf || rInf) {
        // an infinite side is fine only where a finite STRICT side dictates n
        bool ok = (lInf && !rInf && !strictL && strictR && rIter) ||
                  (rInf && !lInf && strictL && !strictR && lIter);
        if (!ok) throw RakuError{Value::typeObj("X::HyperOp::Infinite"),
            std::string("Lists on ") + (lInf && rInf ? "both sides" : lInf ? "left side" : "right side") +
            " of hyperop are known to be infinite"};
    }
    // ONE level of elements — nested arrays stay whole so deepApply distributes
    // into them (flatten() would destroy the shape)
    ValueList la, ra;
    if (!lInf) la = l.t == VT::Array && l.arr ? *l.arr : lIter ? l.flatten() : ValueList{l};
    if (!rInf) ra = r.t == VT::Array && r.arr ? *r.arr : rIter ? r.flatten() : ValueList{r};
    size_t n;
    if (lInf) n = ra.size();
    else if (rInf) n = la.size();
    else if (strictL && strictR) {
        if (la.size() != ra.size()) throw dwimDie();
        n = la.size();
    }
    else if (strictL) { if (!lIter && rIter) throw dwimDie(); n = la.size(); }
    else if (strictR) { if (!rIter && lIter) throw dwimDie(); n = ra.size(); }
    else n = std::max(la.size(), ra.size());
    // materialize the first n elements of an infinite side (cycle unit / count-up)
    auto fill = [&](const Value& v, ValueList& out) {
        if (v.t == VT::Range) {
            long long lo = v.rFrom + (v.rExFrom ? 1 : 0);
            for (size_t i = out.size(); i < n; i++) out.push_back(Value::integer(lo + (long long)i));
        }
        else if (v.arr) {
            out = *v.arr;
            auto st = std::static_pointer_cast<LazySeqState>(v.ext);
            while (out.size() < n && st->appendNext(out)) {}
        }
    };
    if (lInf) fill(l, la);
    if (rInf) fill(r, ra);
    // element slots line up only when flatten() mirrored the raw array
    bool lSlot = wantSlots && l.t == VT::Array && l.arr && l.arr->size() == la.size();
    bool rSlot = wantSlots && r.t == VT::Array && r.arr && r.arr->size() == ra.size();
    Value out = Value::array(); out.isList = true;
    for (size_t i = 0; i < n && !la.empty() && !ra.empty(); i++) {
        const Value& x = la[i % la.size()];
        const Value& y = ra[i % ra.size()];
        Value* xs = lSlot ? &(*l.arr)[i % la.size()] : (!lIter ? lroot : nullptr);
        Value* ys = rSlot ? &(*r.arr)[i % ra.size()] : (!rIter ? rroot : nullptr);
        out.arr->push_back(deepApply(x, y, xs, ys));
    }
    return (!lIter && !rIter && out.arr->size() == 1) ? (*out.arr)[0] : out;
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
    if (op == "=>") return Value::pair(l.toStr(), r); // `.key <<=>>> .value` — hyper over the pair op
    // zip/cross with an inner op (Z&& / Zand / X~) — resolve the inner via applyBinOp
    if (op.size() > 1 && (op[0] == 'Z' || op[0] == 'X')) {
        std::string sub = op.substr(1);
        // one-level elements: sublists stay whole ((1,0) X (a,b),(c,d) is 2x2, not 2x4);
        // a Blob/Buf spreads to its elements (`$H Z+ $M` in Digest::SHA1)
        auto oneLevel = [](const Value& v) -> ValueList {
            if (v.t == VT::Array && v.arr) return *v.arr;
            if (v.t == VT::Range) return v.flatten();
            if (v.t == VT::Str && !v.itemized && (v.hashKind == "Blob" || v.hashKind == "Buf"))
                return v.blobList();
            return ValueList{v};
        };
        ValueList a = oneLevel(l), bb = oneLevel(r);
        Value out = Value::array(); out.isList = true;
        auto emit = [&](const Value& x, const Value& y) {
            if (sub == "=>") out.arr->push_back(Value::pair(x.toStr(), y));
            else if (sub == ",") { // Z, / X, — tuples
                Value t = Value::array(); t.isList = true;
                t.arr->push_back(x); t.arr->push_back(y);
                out.arr->push_back(t);
            }
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
        // hyper compound assignment: `@a <<+=>> 2019` applies the base op
        // elementwise and MUTATES the left array (Value.arr is shared storage,
        // so writing through it reaches the caller's container)
        static const std::set<std::string> eqTailCmp = {"==", "!=", "<=", ">=", "===", "=:="};
        if (inner.size() >= 2 && inner.back() == '=' && !eqTailCmp.count(inner) &&
            l.t == VT::Array && l.arr) {
            std::string base = inner.substr(0, inner.size() - 1);
            ValueList b = listCtx(r);
            if (!b.empty())
                for (size_t i = 0; i < l.arr->size(); i++)
                    (*l.arr)[i] = applyBinOp(base, (*l.arr)[i], b[i % b.size()]);
            return l;
        }
        bool strictL = op.compare(0, 2, ">>") == 0;
        bool strictR = op.compare(op.size() - 2, 2, "<<") == 0;
        Value ll = l, rr = r;
        return hyperCore(ll, rr, strictL, strictR,
            [&](const Value& x, const Value& y, Value*, Value*) { return applyBinOp(inner, x, y); });
    }
    try { return applyArith(op, l, r); }
    catch (RakuError&) {
        if (Value* f = tctx_.cur->find("&infix:<" + op + ">")) return callCallable(*f, ValueList{l, r});
        throw;
    }
}

// Real-role bridge: a user object that defines .Bridge (or .Numeric) numifies
// through it, so numeric operators work on `class F does Real` instances.
static Value bridgeReal(Interpreter& I, const Value& v) {
    if (v.t == VT::Object && v.obj && v.obj->cls) {
        if (Value* br = v.obj->cls->findMethod("Bridge"))  { ValueList none; return I.invokeMethod(*br, v, none); }
        if (Value* nu = v.obj->cls->findMethod("Numeric")) { ValueList none; return I.invokeMethod(*nu, v, none); }
    }
    return v;
}
static bool isNumOp(const std::string& op) {
    static const std::set<std::string> ops = {
        "+", "-", "*", "/", "%", "**", "div", "mod", "gcd", "lcm", "%%",
        "<", "<=", ">", ">=", "==", "!=", "<=>", "cmp", "before", "after"};
    return ops.count(op) > 0;
}

Value Interpreter::evalBinary(Binary* b) {
    const std::string& op = b->op;
    // Fast path for plain operators (`+ - * < == …`): skip the ~20 string compares
    // for the special-cased forms below and go straight to eval-both + applyArith.
    // The classification is computed once and cached on the node.
    if (b->simpleOp < 0) {
        static const std::set<std::string> special = {
            "~", "does", "but", "xx", "==>", "<==", "...", "...^", "~~", "!~~",
            "&&", "and", "||", "or", "andthen", "orelse", "notandthen", "//", "^^", "xor", "&", "|", "^",
            "=:=", "!=:=", "ff", "fff",
            "Z", "X"}; // plain Z/X: chained forms are ONE n-ary list-infix
        bool rmeta = op.size() > 1 && op[0] == 'R' && !std::isalnum((unsigned char)op[1]);
        b->simpleOp = (rmeta || special.count(op)) ? 0 : 1;
    }
    if (b->simpleOp == 1) {
        Value l = eval(b->lhs.get());
        Value r = eval(b->rhs.get());
        // DateTime/Date arithmetic & comparison work on the absolute instant (posix),
        // not the hash's numeric coercion (which would be 0).
        {
            bool ldt = l.hashKind == "DateTime" || l.hashKind == "Date";
            bool rdt = r.hashKind == "DateTime" || r.hashKind == "Date";
            if (ldt || rdt) {
                // leap-second boundaries in POSIX seconds (end of each historical leap day)
                static const long long* leapPx = [] {
                    static long long v[27]; static const long long ymd[] = {
                        19720630, 19721231, 19731231, 19741231, 19751231, 19761231, 19771231,
                        19781231, 19791231, 19810630, 19820630, 19830630, 19850630, 19871231,
                        19891231, 19901231, 19920630, 19930630, 19940630, 19951231, 19970630,
                        19981231, 20051231, 20081231, 20120630, 20150630, 20161231};
                    for (int i = 0; i < 27; i++)
                        v[i] = (civilToDays(ymd[i] / 10000, ymd[i] / 100 % 100, ymd[i] % 100) + 1) * 86400;
                    return v;
                }();
                // Rakudo does DateTime algebra in Instant/TAI, which counts leap seconds.
                // dtSec = TAI seconds (posix + fractional + leap seconds elapsed).
                auto dtSec = [](const Value& v) -> double {
                    if (v.hashKind != "DateTime" && v.hashKind != "Date") return v.toNum();
                    double p = v.hash && v.hash->count("posix") ? (*v.hash)["posix"].toNum() : 0.0;
                    if (v.hash && v.hash->count("second")) { double s = (*v.hash)["second"].toNum(); p += s - std::floor(s); }
                    long long ip = (long long)std::floor(p);
                    for (int i = 0; i < 27; i++) if (leapPx[i] <= ip) p += 1.0;
                    return p;
                };
                // TAI back to POSIX (the i-th leap's TAI boundary is leapPx[i] + i)
                auto taiToPosix = [](double tai) -> double {
                    long long it = (long long)std::floor(tai); double off = 0;
                    for (int i = 0; i < 27; i++) if (leapPx[i] + i <= it) off += 1.0;
                    return tai - off;
                };
                auto mkDT = [&](double taiVal, const Value& tzFrom) -> Value {
                    long long tz = tzFrom.hash && tzFrom.hash->count("timezone") ? (*tzFrom.hash)["timezone"].toInt() : 0;
                    Value res = methodCall(Value::typeObj("DateTime"), "new", ValueList{Value::number(taiToPosix(taiVal))});
                    if (tz) res = methodCall(res, "in-timezone", ValueList{Value::integer(tz)});
                    return res;
                };
                if (ldt && rdt && (op == "<" || op == ">" || op == "<=" || op == ">=" ||
                                   op == "==" || op == "!=" || op == "<=>")) {
                    double a = dtSec(l), c = dtSec(r);
                    if (op == "<=>") return Value::integer(a < c ? -1 : a > c ? 1 : 0);
                    bool res = op == "<" ? a < c : op == ">" ? a > c : op == "<=" ? a <= c
                             : op == ">=" ? a >= c : op == "==" ? a == c : a != c;
                    return Value::boolean(res);
                }
                if (op == "-" && l.hashKind == "DateTime" && r.hashKind == "DateTime") {
                    Value d = Value::number(dtSec(l) - dtSec(r)); d.hashKind = "Duration"; return d;
                }
                if ((op == "+" || op == "-") && l.hashKind == "DateTime" &&
                    (r.t == VT::Int || r.t == VT::Num || r.t == VT::Rat || r.hashKind == "Duration"))
                    return mkDT(dtSec(l) + (op == "-" ? -r.toNum() : r.toNum()), l);
                if (op == "+" && r.hashKind == "DateTime" &&
                    (l.t == VT::Int || l.t == VT::Num || l.t == VT::Rat || l.hashKind == "Duration"))
                    return mkDT(dtSec(r) + l.toNum(), r);
            }
        }
        if ((op == "+" || op == "-") &&
            (!l.hashKind.empty() || !r.hashKind.empty())) { // Instant/Duration algebra
            Value res = applyArith(op, l, r);
            tagTemporal(op, l, r, res);
            return res;
        }
        // function composition `f ∘ g` / `f o g` → a callable computing f(g(...))
        if (op == "\xE2\x88\x98" || op == "o") {
            Value fV = l, gV = r;
            Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
            code.code->builtin = [fV, gV](Interpreter& I, ValueList& a) -> Value {
                return I.callCallable(fV, ValueList{ I.callCallable(gV, a) });
            };
            return code;
        }
        if (l.t == VT::Object || r.t == VT::Object || !l.enumType.empty() || !r.enumType.empty())
            if (Value* f = tctx_.cur->find("&infix:<" + op + ">"))
                try { return callCallable(*f, ValueList{l, r}); } catch (RakuError&) {}
        // numeric operators on a .Bridge/.Numeric object work through the bridge
        if ((l.t == VT::Object || r.t == VT::Object) && isNumOp(op)) {
            Value l2 = bridgeReal(*this, l), r2 = bridgeReal(*this, r);
            if (l2.t != VT::Object && r2.t != VT::Object) {
                Value res = applyArith(op, l2, r2);
                tagTemporal(op, l2, r2, res);
                return res;
            }
        }
        // hyper metaop `>>op<<` — element-wise, resolving a user inner operator
        if (op.size() >= 5 && (op.compare(0, 2, ">>") == 0 || op.compare(0, 2, "<<") == 0) &&
            (op.compare(op.size() - 2, 2, ">>") == 0 || op.compare(op.size() - 2, 2, "<<") == 0)) {
            // `* <<+>> @b` / `@a >>+>> *` — a Whatever(Code) operand curries the metaop
            auto whr = [](const Value& v) { return v.t == VT::Whatever || (v.t == VT::Code && v.code && v.code->isWhateverCode); };
            if (whr(l) || whr(r)) {
                Value lc = l, rc = r; std::string opc = op;
                Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
                code.code->isWhateverCode = true;
                code.code->whateverArity = (whr(l) ? 1 : 0) + (whr(r) ? 1 : 0);
                code.code->builtin = [lc, rc, opc](Interpreter& I, ValueList& a) -> Value {
                    size_t ai = 0;
                    auto resolve = [&](const Value& v) -> Value {
                        if (v.t == VT::Whatever) return ai < a.size() ? a[ai++] : Value::any();
                        if (v.t == VT::Code && v.code && v.code->isWhateverCode)
                            return I.callCallable(v, ValueList{ai < a.size() ? a[ai++] : Value::any()});
                        return v;
                    };
                    Value nl = resolve(lc), nr = resolve(rc);
                    return I.applyBinOp(opc, nl, nr);
                };
                return code;
            }
            std::string inner = op.substr(2, op.size() - 4);
            // hyper compound assignment: `@a <<+=>> 2019` applies the base op
            // elementwise and MUTATES the left array (Value.arr is shared
            // storage, so writing through it reaches the caller's container)
            static const std::set<std::string> eqTailCmp = {"==", "!=", "<=", ">=", "===", "=:="};
            if (inner.size() >= 2 && inner.back() == '=' && !eqTailCmp.count(inner) &&
                l.t == VT::Array && l.arr) {
                std::string base = inner.substr(0, inner.size() - 1);
                ValueList b = r.flatten();
                if (!b.empty())
                    for (size_t i = 0; i < l.arr->size(); i++)
                        (*l.arr)[i] = applyBinOp(base, (*l.arr)[i], b[i % b.size()]);
                return l;
            }
            bool strictL = op.compare(0, 2, ">>") == 0;
            bool strictR = op.compare(op.size() - 2, 2, "<<") == 0;
            Value ll = l, rr = r;
            return hyperCore(ll, rr, strictL, strictR,
                [&](const Value& x, const Value& y, Value*, Value*) { return applyBinOp(inner, x, y); });
        }
        // zip/cross metaop `Zop`/`Xop` — resolve a user inner operator via applyBinOp
        if (op.size() > 1 && (op[0] == 'Z' || op[0] == 'X')) {
            std::string sub = op.substr(1);
            auto oneLevel = [](const Value& v) -> ValueList {
                if (v.t == VT::Array && v.arr) return *v.arr;
                if (v.t == VT::Range) return v.flatten();
                if (v.t == VT::Str && !v.itemized && (v.hashKind == "Blob" || v.hashKind == "Buf"))
                    return v.blobList(); // `$H Z+ $M` in Digest::SHA1
                return ValueList{v};
            };
            ValueList a = oneLevel(l), bb = oneLevel(r);
            Value out = Value::array(); out.isList = true;
            auto emit = [&](const Value& x, const Value& y) {
                if (sub.empty() || sub == ",") { // Z / Z, — tuples
                    Value t = Value::array({x, y}); t.isList = true;
                    out.arr->push_back(t);
                }
                else if (sub == "=>") out.arr->push_back(Value::pair(x.toStr(), y));
                else out.arr->push_back(applyBinOp(sub, x, y));
            };
            if (op[0] == 'Z') { for (size_t i = 0; i < a.size() && i < bb.size(); i++) emit(a[i], bb[i]); }
            else { for (auto& x : a) for (auto& y : bb) emit(x, y); }
            return out;
        }
        Value res = applyArith(op, l, r);
        tagTemporal(op, l, r, res);
        return res;
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
        // reverse metaoperator: `a R/ b` computes `b / a` — applyBinOp (not
        // applyArith) so the short-circuit family works too (`R//` in LibraryMake)
        Value l = eval(b->lhs.get()), r = eval(b->rhs.get());
        return applyBinOp(op.substr(1), r, l);
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
        Value rv = eval(b->rhs.get());
        if (rv.t == VT::Whatever || (rv.t == VT::Num && std::isinf(rv.n))) {
            // `EXPR xx *` — an endlessly repeating lazy list (one eval as the unit)
            Value a = Value::array(); a.isList = true;
            a.arr->push_back(eval(b->lhs.get()));
            auto st = std::make_shared<LazySeqState>(); st->infinite = true;
            Value unit = (*a.arr)[0];
            st->appendNext = [unit](ValueList& cache) -> bool { cache.push_back(unit); return true; };
            a.ext = st;
            return a;
        }
        long long n = rv.toInt();
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
        return seqOp(std::move(l), std::move(r), op == "...^");
    }
    if (op == "~~" || op == "!~~") {
        // regex match: $str ~~ /pat/   /   $str ~~ s/pat/repl/
        if (b->rhs->kind == NK::RegexLit) {
            Value l = eval(b->lhs.get());
            std::string pat = static_cast<RegexLit*>(b->rhs.get())->pattern;
            // `* ~~ /rx/` / `* !~~ /rx/` curry into a matcher WhateverCode (the
            // `.grep: * !~~ /1/` idiom) instead of matching the Whatever eagerly.
            if (l.t == VT::Whatever || (l.t == VT::Code && l.code && l.code->isWhateverCode)) {
                bool neg = (op == "!~~");
                Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
                code.code->isWhateverCode = true; code.code->whateverArity = 1;
                code.code->builtin = [pat, neg](Interpreter& I, ValueList& a) -> Value {
                    Value m = I.regexMatch((a.empty() ? Value::any() : a[0]).toStr(), pat);
                    if (neg) return Value::boolean(!m.truthy());
                    return m.truthy() ? m : Value::nil();
                };
                return code;
            }
            // a junction LHS autothreads: (any("4","5") ~~ /4/) is a junction of
            // per-eigenstate results — 'any(｢4｣, Nil)' (S03-junctions/misc.t)
            if (l.t == VT::Array && l.arr &&
                (l.enumName == "any" || l.enumName == "all" ||
                 l.enumName == "one" || l.enumName == "none")) {
                Value out = Value::array(); out.enumName = l.enumName;
                for (auto& e : *l.arr) {
                    Value m = regexMatch(e.toStr(), pat);
                    if (op == "~~") out.arr->push_back(m.truthy() ? m : Value::nil());
                    else out.arr->push_back(Value::boolean(!m.truthy()));
                }
                return out;
            }
            Value m = regexMatch(l.toStr(), pat);
            // `~~` yields the Match on success (Nil on failure); `!~~` yields a Bool
            if (op == "~~") return m.truthy() ? m : Value::nil();
            return Value::boolean(!m.truthy());
        }
        if (b->rhs->kind == NK::SubstLit) {
            auto* sub = static_cast<SubstLit*>(b->rhs.get());
            Value l = eval(b->lhs.get());
            if (isTrSubst(sub->pattern)) { // tr/from/to/ — transliteration, returns a StrDistance
                long long n; std::string before = l.toStr();
                std::string out = translit(before, sub->pattern.substr(1), sub->repl, n);
                if (Value* lv = lvalue(b->lhs.get())) *lv = Value::str(out);
                Value sd = Value::makeHash(); sd.hashKind = "StrDistance";
                (*sd.hash)["before"] = Value::str(before);
                (*sd.hash)["after"] = Value::str(out);
                (*sd.hash)["distance"] = Value::integer(n);
                return sd;
            }
            long nsub = 0; ValueList noArgs; Value mres;
            std::string out = substSelect(l.toStr(), sub->pattern, nullptr, noArgs, nsub, false, &sub->repl, &mres);
            if (sub->nonMut) return Value::str(out);        // S/// : return new string, leave lhs intact
            if (Value* lv = lvalue(b->lhs.get())) *lv = Value::str(out);
            return mres;                                     // s/// returns the Match / List of matches
        }
        // `X ~~ Y` topicalizes: $_ is bound to X while Y is evaluated (so `$x ~~ .so` works)
        Value lTopic = eval(b->lhs.get());
        // $_ may live in an OUTER scope (a when-block's topic while we evaluate an
        // if-condition): restore must then ERASE our local shadow, not leave a
        // stray `$_ = Any` that hides the outer topic for the rest of the scope
        bool hadLocalTopic = tctx_.cur->vars.count("$_") > 0;
        Value savedTopic = hadLocalTopic ? tctx_.cur->vars["$_"] : Value::any();
        tctx_.cur->define("$_", lTopic);
        auto restoreTopic = [&] {
            if (hadLocalTopic) tctx_.cur->vars["$_"] = savedTopic;
            else tctx_.cur->vars.erase("$_");
        };
        Value r;
        try { r = eval(b->rhs.get()); } catch (...) { restoreTopic(); throw; }
        restoreTopic();
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
            Value m = regexMatch(lTopic.toStr(), r.s, &r);
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
            // a regex Callable (my regex pair {…}) yields its MATCH, like Rakudo
            if (op == "~~" && m.t == VT::Match) return m;
            bool ok = boolify(m);
            return Value::boolean(op == "~~" ? ok : !ok);
        }
        return applyArith(op, lTopic, r); // generic smartmatch on the already-evaluated operands
    }
    if (op == "ff" || op == "fff") {
        // flip-flop: stateful per site. Off: test LHS; a hit turns it on (and for
        // `ff` the RHS is tested on the SAME element, so a one-element range works).
        // On: test RHS; a hit turns it off — the closing element still returns True.
        bool& on = ffState_[b];
        auto test = [&](Expr* side) -> bool {
            Value v = eval(side);
            if (v.t == VT::Bool) return v.truthy();
            if (v.t == VT::Whatever) return op == "ff" ? false : false; // `ff *`: never end
            Value* tp = tctx_.cur->find("$_");
            Value topic = tp ? *tp : Value::any();
            if (v.t == VT::Regex) return regexMatch(topic.toStr(), v.s).truthy();
            return applyArith("~~", topic, v).truthy();
        };
        if (!on) {
            if (!test(b->lhs.get())) return Value::boolean(false);
            on = true;
            if (op == "ff" && test(b->rhs.get())) on = false;
            return Value::boolean(true);
        }
        if (test(b->rhs.get())) on = false;
        return Value::boolean(true);
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
    if (op == "andthen" || op == "orelse" || op == "notandthen") {
        Value l = eval(b->lhs.get());
        bool def = isDefined(l);
        bool run = op == "andthen" ? def : !def; // orelse/notandthen fire on undefined
        if (!run) { // skip the RHS: orelse/andthen yield the LHS, notandthen yields Empty
            if (op == "notandthen") { Value e = Value::array(); e.isList = true; return e; }
            return l;
        }
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
    if ((op == "Z" || op == "X") && b->lhs->kind == NK::Binary &&
        static_cast<Binary*>(b->lhs.get())->op == op) {
        // `@a Z @b Z @c` is ONE list-infix chain producing 3-tuples — a pairwise
        // fold would zip tuples-with-a-list and come out mangled. Same for X.
        std::vector<Expr*> chain;
        Expr* cur = b;
        while (cur->kind == NK::Binary && static_cast<Binary*>(cur)->op == op) {
            chain.push_back(static_cast<Binary*>(cur)->rhs.get());
            cur = static_cast<Binary*>(cur)->lhs.get();
        }
        chain.push_back(cur);
        std::reverse(chain.begin(), chain.end());
        ValueList items;
        for (Expr* e : chain) items.push_back(eval(e));
        return applyReduce(op, items);
    }
    Value l = eval(b->lhs.get());
    Value r = eval(b->rhs.get());
    // operator overloading: a built-in operator on a user object dispatches to a
    // user `sub infix:<op>` if one is in scope (falling back to the built-in when
    // no candidate matches the operands).
    if (l.t == VT::Object || r.t == VT::Object || !l.enumType.empty() || !r.enumType.empty())
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
    std::vector<Value> pairs, valueMixins;
    std::function<void(const Value&)> collect = [&](const Value& v) {
        if (v.t == VT::Type) {
            roleNames.push_back(v.s);
            auto it = classes_.find(v.s);
            if (it != classes_.end()) roleInfos.push_back(it->second.get());
        } else if (v.t == VT::Pair) {
            pairs.push_back(v);
        } else if (v.t == VT::Array && v.arr) {
            for (auto& e : *v.arr) collect(e);
        } else {
            // `X but VALUE` mixes in a method named after VALUE's type that
            // returns it: 5 but "t" stringifies as "t"; 0 but True is true.
            valueMixins.push_back(v);
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
    // `but VALUE` — a constant method named after the value's type (Str/Bool/Int/…).
    for (auto& vm : valueMixins) {
        Value method = Value::closure([vm](ValueList&) { return vm; });
        method.code->isMethod = true;
        nc->methods[vm.typeName()] = method;
    }
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

// hyper prefix `-«(…)` / `--«%h`: apply the op per element, descending into
// nested arrays and hash values (keys kept); ++/-- mutate the elements in
// place through the shared containers and yield the new values (prefix).
Value Interpreter::hyperUnary(const std::string& op, Value v) {
    std::function<Value(Value&)> deep = [&](Value& x) -> Value {
        if (x.t == VT::Array && x.arr) {
            Value out = Value::array(); out.isList = x.isList;
            for (auto& e : *x.arr) out.arr->push_back(deep(e));
            return out;
        }
        if (x.t == VT::Range) {
            Value out = Value::array(); out.isList = true;
            for (auto& e : x.flatten()) out.arr->push_back(deep(e));
            return out;
        }
        if (x.t == VT::Hash && x.hash) {
            Value out = Value::makeHash(); out.hashKind = x.hashKind;
            for (auto& kv : *x.hash) (*out.hash)[kv.first] = deep(kv.second);
            return out;
        }
        if (op == "++" || op == "--") {
            Value nv = applyBinOp(op == "++" ? "+" : "-", x, Value::integer(1));
            x = nv;
            return nv;
        }
        if (op == "-") return applyBinOp("-", Value::integer(0), x);
        if (op == "+") {
            if (x.isAllomorph()) { // +IntStr strips to the pure numeric side
                Value nx = x; nx.hashKind.clear(); nx.s.clear(); return nx;
            }
            return applyBinOp("+", Value::integer(0), x);
        }
        if (op == "!") return Value::boolean(!boolify(x));
        if (op == "?") return Value::boolean(boolify(x));
        return Value::str(x.toStr()); // ~
    };
    return deep(v);
}

// hyper postfix `@a»++` / `%h»!` / `(2,3)»i`: descends nested arrays, keeps
// hash keys; ++/-- mutate in place and yield the OLD values (postfix).
Value Interpreter::hyperPostfixApply(const std::string& op, Value v) {
    Value* userPost = tctx_.cur->find("&postfix:<" + op + ">");
    std::function<Value(Value&)> deep = [&](Value& x) -> Value {
        if (x.t == VT::Array && x.arr) {
            Value out = Value::array(); out.isList = x.isList;
            for (auto& e : *x.arr) out.arr->push_back(deep(e));
            return out;
        }
        if (x.t == VT::Range) {
            Value out = Value::array(); out.isList = true;
            for (auto& e : x.flatten()) out.arr->push_back(deep(e));
            return out;
        }
        if (x.t == VT::Hash && x.hash) {
            Value out = Value::makeHash(); out.hashKind = x.hashKind;
            for (auto& kv : *x.hash) (*out.hash)[kv.first] = deep(kv.second);
            return out;
        }
        if (op == "++" || op == "--") {
            Value old = x;
            x = applyBinOp(op == "++" ? "+" : "-", x, Value::integer(1));
            return old;
        }
        if (userPost) return callCallable(*userPost, ValueList{x});
        if (op == "i") return postfixI(x);
        throw RakuError{Value::typeObj("X::AdHoc"),
                        "No postfix:<" + op + "> operator for hyper"};
    };
    return deep(v);
}

Value Interpreter::evalUnary(Unary* u) {
    // hyper prefix `-«(…)` / `--«%h`: apply the op per element, descending into
    // nested arrays and hash values (keys kept); ++/-- mutate the elements in
    // place through the shared containers and yield the new values (prefix).
    if (u->op.rfind("hyper:", 0) == 0)
        return hyperUnary(u->op.substr(6), eval(u->operand.get()));
    // control-flow in expression position: return/last/next/redo
    if (u->op == "return" || u->op == "return-rw") {
        Value v = u->operand ? eval(u->operand.get()) : Value::any();
        if (tctx_.curRoutineFrame != 0 && tctx_.frameTop == tctx_.curRoutineFrame) {
            tctx_.returning = true; tctx_.returnV = std::move(v); // cooperative return
            return Value::any();
        }
        throw ReturnEx{v};
    }
    if (u->op == "last" || u->op == "next" || u->op == "redo") {
        if (tctx_.curLoopFrame != 0 && tctx_.frameTop == tctx_.curLoopFrame) {
            tctx_.loopCtl = u->op == "next" ? 1 : u->op == "last" ? 2 : 3; // cooperative
            return Value::any();
        }
        if (u->op == "last") throw LastEx{};
        if (u->op == "next") throw NextEx{};
        throw RedoEx{};
    }
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
        // short-circuit reduces THUNK their operands: `[&&] 0, ++$x` decides at
        // the 0 and must never run the increment; scan forms freeze the partials
        // once decided ([\&&] 1,0,++$x is (1 0 0) with $x untouched)
        {
            bool scScan = !op.empty() && op[0] == '\\';
            std::string scBase = scScan ? op.substr(1) : op;
            bool scAnd = scBase == "&&" || scBase == "and";
            bool scOr  = scBase == "||" || scBase == "or";
            bool scDef = scBase == "//" || scBase == "orelse";
            bool scAt  = scBase == "andthen", scNat = scBase == "notandthen";
            bool scXor = scBase == "^^" || scBase == "xor";
            if ((scAnd || scOr || scDef || scAt || scNat || scXor) &&
                u->operand->kind == NK::ListExpr &&
                !static_cast<ListExpr*>(u->operand.get())->items.empty()) {
                auto* le = static_cast<ListExpr*>(u->operand.get());
                auto emptySlip = [] { Value es = Value::array(); es.isList = true; es.s = "Slip"; return es; };
                Value out = Value::array(); out.isList = true;   // scan partials
                Value acc;
                bool stopped = false;    // decided: later operands never run
                bool emptyTail = false;  // andthen/notandthen: whatever follows is Empty
                bool sawTail = false;    // an operand position exists past the decision
                size_t truthies = 0; Value oneTruthy;            // xor bookkeeping
                auto step = [&](const Value& elem) { // returns false once decided
                    if (scXor) {
                        if (elem.truthy()) { truthies++; oneTruthy = elem; }
                        if (truthies >= 2) { acc = Value::nil(); return false; }
                        acc = truthies == 1 ? oneTruthy : elem;  // running list-xor
                        return true;
                    }
                    acc = elem; // these all fold to the newest operand as-is
                    if (scAnd) return elem.truthy();
                    if (scOr)  return !elem.truthy();
                    if (scDef) return !isDefined(elem);
                    // andthen keeps going while defined, notandthen while undefined;
                    // the dead operand itself is the fold value, Empty starts AFTER it
                    bool dead = scAt ? !isDefined(elem) : isDefined(elem);
                    if (dead) { emptyTail = true; return false; }
                    return true;
                };
                for (auto& ie : le->items) {
                    if (stopped) { // frozen: push the settled partial, skip the thunk
                        sawTail = true;
                        if (scScan && !emptyTail) out.arr->push_back(acc);
                        continue;
                    }
                    Value v = eval(ie.get());
                    bool whole = (ie->kind == NK::VarExpr &&
                                  !static_cast<VarExpr*>(ie.get())->name.empty() &&
                                  static_cast<VarExpr*>(ie.get())->name[0] == '$') ||
                                 ie->kind == NK::ArrayLit;
                    ValueList elems;
                    if (!whole && v.t == VT::Range) for (auto& x : v.flatten()) elems.push_back(x);
                    else if (!whole && v.t == VT::Array && v.arr && !v.itemized)
                        for (auto& x : *v.arr) elems.push_back(x);
                    else elems.push_back(std::move(v));
                    for (auto& e : elems) {
                        if (stopped) {
                            sawTail = true;
                            if (scScan && !emptyTail) out.arr->push_back(acc);
                            continue;
                        }
                        if (!step(e)) stopped = true;
                        if (scScan) out.arr->push_back(acc); // the decider's own partial
                    }
                }
                if (scScan) return out;
                return (emptyTail && sawTail) ? emptySlip() : acc;
            }
        }
        // Flatten like a slurpy arg list: @-vars/ranges/inner lists spread, but a
        // $-held container or an [..] literal stays ONE item ([===] $a, $a, [1,2]).
        ValueList items;
        std::function<void(const Value&)> spread = [&](const Value& v) {
            // deep-spread plain lists, but an itemized element ([1,2,3] row of a
            // matrix) stays ONE item — so [Z] @matrix zips the rows
            if (v.t == VT::Range) { for (auto& x : v.flatten()) items.push_back(x); return; }
            if (v.t == VT::Array && v.arr && !v.itemized) { for (auto& x : *v.arr) spread(x); return; }
            items.push_back(v);
        };
        auto pushFlat = [&](Value v, bool item) {
            if (!item && (v.t == VT::Array || v.t == VT::Range))
                spread(v);
            else items.push_back(std::move(v));
        };
        // list-infix reduces ([Z]/[X] and their metaop forms) keep each operand
        // list whole: [Z] @a, @b zips the two arrays, [Z] @matrix zips the rows
        bool listInfix = !op.empty() && (op[0] == 'Z' || op[0] == 'X') &&
                         !isSetOpStr(op) && op != "X"; // "X" alone is still the cross op
        if (op == "Z" || op == "X") listInfix = true;
        if (u->operand->kind == NK::ListExpr) {
            auto* le = static_cast<ListExpr*>(u->operand.get());
            for (auto& ie : le->items) {
                bool item = listInfix ||
                            (ie->kind == NK::VarExpr &&
                             !static_cast<VarExpr*>(ie.get())->name.empty() &&
                             static_cast<VarExpr*>(ie.get())->name[0] == '$') ||
                            ie->kind == NK::ArrayLit;
                pushFlat(eval(ie.get()), item);
            }
        }
        else if (listInfix) { // a single @rows operand spreads ONE level
            Value v = eval(u->operand.get());
            if (v.t == VT::Array && v.arr) for (auto& x : *v.arr) items.push_back(x);
            else if (v.t == VT::Range) for (auto& x : v.flatten()) items.push_back(x);
            else items.push_back(std::move(v));
        }
        else pushFlat(eval(u->operand.get()), false);
        return applyReduce(op, items);
    }
    if (u->op == "siglit") { // :( … ) — a first-class Signature literal
        Value c = eval(u->operand.get()); // the params-only closure
        ValueList none;
        return methodCall(c, "signature", none);
    }
    if (u->op == "capture") { // \(…): a Capture — one item, assoc-indexable on its named parts
        Value v = eval(u->operand.get());
        if (v.t != VT::Array) { // \(:named) / \(42): a single part is still a Capture
            Value one = Value::array();
            if (v.t != VT::Nil) one.arr->push_back(std::move(v));
            v = std::move(one);
        }
        v.hashKind = "Capture"; v.itemized = true; v.isList = false;
        return v;
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
            // `@$blob` lists a Blob/Buf's elements (`flat @$msg, 0x80, …` in Digest)
            if (v.t == VT::Str && (v.hashKind == "Blob" || v.hashKind == "Buf")) {
                Value a = Value::array(v.blobList()); a.isList = true; return a;
            }
            Value a = Value::array(); a.arr->push_back(v); a.isList = true; return a;
        }
        if (u->op == "ctx%") return v.t == VT::Hash ? v : coerceHash(v); // %(...) hash composer
        if (v.t == VT::Array) v.itemized = true; // $[...] / $(...): array becomes one non-flattening item
        return v; // item context
    }
    if (u->op == "do") {
        if (u->operand->kind == NK::BlockExpr) {
            auto* be = static_cast<BlockExpr*>(u->operand.get());
            // a `do { }` block takes no arguments, so a placeholder inside it is
            // a compile error (X::Placeholder::Block), like Rakudo
            if (be->params.empty()) {
                std::string ph = firstBlockPlaceholder(be->body);
                if (!ph.empty())
                    throwTyped("X::Placeholder::Block", {{"placeholder", ph}},
                        "Placeholder variable '" + ph +
                        "' may not be used here because the surrounding block does not take a signature");
            }
            return callCallable(makeClosure(be), {});
        }
        return eval(u->operand.get());
    }
    if (u->op == "stmtseq") {
        // `$(stmt; stmt; expr)` — statements run in the CURRENT scope (a temp/let
        // inside must register on the enclosing block, not a nested one); the
        // value is the last statement's value
        Value r;
        if (u->operand->kind == NK::BlockExpr)
            for (auto& s : static_cast<BlockExpr*>(u->operand.get())->body)
                r = exec(s.get(), false);
        return r;
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
    if (u->op == "once") {
        // run once per enclosing-routine INSTANCE (a fresh closure clone
        // re-runs it); later hits return the cached first value
        Env* se = tctx_.curStateEnv;
        std::string key = "!once!" + std::to_string((uintptr_t)u);
        if (se) { auto it = se->vars.find(key); if (it != se->vars.end()) return it->second; }
        Value r = u->operand->kind == NK::BlockExpr
            ? callCallable(makeClosure(static_cast<BlockExpr*>(u->operand.get())), {})
            : eval(u->operand.get());
        if (se) se->vars[key] = r;
        return r;
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
        // A small first probe: an expensive generator (a prime sieve, say) must not
        // pay for thousands of takes when the consumer wants @g[^20]. Finite
        // whole-list consumers force the rest via materializeLazy (Builtins).
        const size_t INITIAL = 64;
        ValueList prefix;
        // finite gather (terminates within the cap): eager, exactly as before
        if (!runGather(INITIAL, prefix)) { // finite: eager, but a Seq (gists with parens)
            Value a = Value::array(std::move(prefix)); a.isList = true; a.s = "Seq"; return a;
        }
        // hit the cap → treat as lazy: keep the prefix and extend on demand by
        // re-running the block with a larger cap (re-run because there are no
        // coroutines; fine for the usual pure generator `gather { loop { take … } }`).
        // Growth DOUBLES so the O(n²) re-run cost stays amortised-linear in takes.
        Value arr = Value::array(prefix); arr.isList = true; arr.s = "Seq";
        auto st = std::make_shared<LazySeqState>();
        st->appendNext = [this, runGather](ValueList& out) -> bool {
            ValueList grown;
            bool more = runGather(out.size() + std::max<size_t>(64, out.size()), grown);
            for (size_t i = out.size(); i < grown.size(); i++) out.push_back(grown[i]);
            return more;
        };
        arr.ext = st;
        return arr;
    }
    if (u->op == "++" || u->op == "--") {
        Value* lv = lvalue(u->operand.get());
        // a Proxy-bound alias (`$a := $x`) reads via FETCH and writes via STORE,
        // so ++/-- reach the underlying container instead of clobbering the Proxy
        if (lv->t == VT::Hash && lv->hashKind == "Proxy" && lv->hash) {
            auto fit = lv->hash->find("FETCH"), sit = lv->hash->find("STORE");
            if (fit != lv->hash->end() && sit != lv->hash->end()) {
                Value oldp = callCallable(fit->second, {});
                Value newp;
                if (oldp.t == VT::Str) {
                    if (u->op == "++") newp = Value::str(strSucc(oldp.s));
                    else { bool ok; std::string r = strPred(oldp.s, ok); newp = ok ? Value::str(r) : Value::typeObj("Failure"); }
                }
                else if (oldp.t == VT::Bool) newp = Value::boolean(u->op == "++");
                else newp = applyArith(u->op == "++" ? "+" : "-", oldp, Value::integer(1));
                callCallable(sit->second, { newp });
                return u->postfix ? oldp : newp;
            }
        }
        Value oldv = *lv;
        Value newv;
        // A Str always increments/decrements as a string in Rakudo — even
        // numeric-looking ones ("42"++ is Str "43", "10"-- keeps width: "09",
        // "-5"++ is "-6"). strSucc/strPred pick the magic window themselves
        // and leave strings with nothing incrementable unchanged.
        bool strMagic = lv->t == VT::Str;
        // ++/-- IS .succ/.pred: Bool saturates (True++ stays True, --$b is
        // False), and an object whose class defines succ/pred dispatches there
        // (autoincrement.t's Incrementor). Everything else goes numeric.
        auto classHas = [&](const char* m) -> bool {
            if (lv->t != VT::Object || !lv->obj || !lv->obj->cls) return false;
            for (ClassInfo* c = lv->obj->cls.get(); c; c = c->parent ? c->parent.get() : nullptr) {
                if (c->methods.count(m)) return true;
                for (auto& ep : c->extraParents) if (ep && ep->methods.count(m)) return true;
            }
            return false;
        };
        if (lv->t == VT::Bool || (lv->t == VT::Type && lv->s == "Bool")) {
            newv = Value::boolean(u->op == "++");
            // postfix on an undefined Bool returns False (the S03 "postfix on
            // undefined returns the type's zero" rule), not the type object
            if (oldv.t == VT::Type) oldv = Value::boolean(false);
        } else if (strMagic && u->op == "++") {
            newv = Value::str(strSucc(lv->s));
        } else if (strMagic && u->op == "--") {
            bool ok; std::string r = strPred(lv->s, ok);
            newv = ok ? Value::str(r) : Value::typeObj("Failure");
        } else if (classHas(u->op == "++" ? "succ" : "pred")) {
            newv = methodCall(*lv, u->op == "++" ? "succ" : "pred", {});
        } else {
            newv = applyArith(u->op == "++" ? "+" : "-", *lv, Value::integer(1));
            if (lv->natBits) wrapNative(newv, lv->natBits, lv->natSigned, lv->natFloat); // native int wraparound
        }
        *lv = newv;
        if (anyRwLinks_) rwWriteThrough(u->operand.get()); // ++/-- on a linked rw param
        // BagHash/SetHash/MixHash element ++/--: a weight reaching 0 (or below)
        // removes the key entirely (`%h<a>--` deletes a 1-weighted item)
        if (u->operand->kind == NK::Index && newv.toNum() <= 0.0) {
            auto* ix = static_cast<Index*>(u->operand.get());
            if (ix->isHash) {
                Value* base = nullptr;
                try { base = lvalue(ix->base.get()); } catch (RakuError&) {}
                if (base && base->t == VT::Hash && base->hash &&
                    (base->hashKind == "BagHash" || base->hashKind == "SetHash" ||
                     base->hashKind == "MixHash")) {
                    bool drop = base->hashKind == "MixHash" ? newv.toNum() == 0.0
                                                            : true;
                    if (drop) base->hash->erase(eval(ix->index.get()).toStr());
                }
            }
        }
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
            if (op == "+") {
                if (b.isAllomorph()) { // +IntStr strips to the pure numeric side
                    Value nb = b; nb.hashKind.clear(); nb.s.clear(); return nb;
                }
                return b.isNumeric() ? b : Value::number(b.toNum());
            }
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
        !(v.t == VT::Hash && (v.hashKind == "Proc" || v.hashKind == "Proc::Async" ||
                              v.hashKind == "StrDistance"))) {
        // a Bag/Mix numifies to its .total (sum of counts/weights), possibly fractional
        if (v.t == VT::Hash && v.hash &&
            (v.hashKind.rfind("Bag", 0) == 0 || v.hashKind.rfind("Mix", 0) == 0)) {
            double t = 0; bool allInt = true;
            for (auto& kv : *v.hash) { t += kv.second.toNum(); if (kv.second.t != VT::Int && kv.second.t != VT::Bool) allInt = false; }
            if (u->op == "-") t = -t;
            return allInt ? Value::integer((long long)t) : Value::number(t);
        }
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
        if (v.t == VT::Rat) { Value r = Value::rat(-(*v.ratN), *v.ratD); r.fatRat = v.fatRat; return r; }
        if (v.t == VT::Str || v.t == VT::Match) {
            Value n = numifyStr(v.t == VT::Str ? v.s : strOf(v));
            if (n.t == VT::Int && !n.big) return Value::integer(-n.toInt());
            return n.t==VT::Rat ? Value::rat(-(*n.ratN),*n.ratD) : Value::number(-n.toNum());
        }
        return Value::number(-v.toNum());
    }
    if (u->op == "+") {
        if (v.t == VT::Bool) return Value::integer(v.b ? 1 : 0); // +True == 1
        if (v.isAllomorph()) { // +IntStr strips to the pure numeric side
            Value nv = v; nv.hashKind.clear(); nv.s.clear(); return nv;
        }
        if (v.t == VT::Match) return numifyStr(strOf(v)); // +$0 of digits is an Int, like +Str
        return v.isNumeric() ? v : (v.t == VT::Str ? numifyStr(v.s) : Value::number(v.toNum()));
    }
    if (u->op == "~") return Value::str(strOf(v)); // honour a user Str/gist / Exception .message
    if (u->op == "!") return Value::boolean(!boolify(v));
    if (u->op == "?") return Value::boolean(boolify(v));
    if (u->op == "+^") return Value::integer(~v.toInt()); // bitwise NOT
    if (u->op == "?^") return Value::boolean(!boolify(v));
    if (u->op == "~^") { // string bitwise NOT (byte-wise complement)
        std::string r = v.toStr();
        for (auto& ch : r) ch = (char)~(unsigned char)ch;
        return Value::str(r);
    }
    if (u->op == "^") {
        Value r = Value::range(0, v.toInt(), false, true);
        if (v.t == VT::Int && v.big) r.big = v.big; // keep the big bound (pick/roll sample it)
        return r;
    }
    if (u->op == "|") { // slip: spread handled in evalArgs; anywhere else the
        // value IS a Slip — mark it so list consumers (map, list literals) splice it.
        if (v.t == VT::Array && v.arr) {
            Value out = Value::array(); *out.arr = *v.arr;
            out.isList = true; out.s = "Slip";
            return out;
        }
        if (v.t == VT::Range) { Value out = Value::array(v.flatten()); out.isList = true; out.s = "Slip"; return out; }
        return v;
    }
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
            // |@list slips positionally ONE level (post-GLR: nested lists stay
            // whole elements — |(<a b>, <c d>) is two List arguments, not four
            // strings); |%hash slips as named args.
            if (v.t == VT::Array && v.arr) { for (auto& x : *v.arr) args.push_back(x); }
            else if (v.t == VT::Range) { for (auto& x : v.flatten()) args.push_back(x); }
            else if (v.t == VT::Hash && v.hash) { for (auto& kv : *v.hash) { Value p = Value::pair(kv.first, kv.second); p.namedArg = true; args.push_back(std::move(p)); } }
            else args.push_back(v);
        } else {
            Value v = eval(a.get());
            // Only a syntactic pair (k=>v / :k(v), i.e. a NK::Pair expression) whose key
            // is a bare identifier is a NAMED argument; a Pair value from a variable/
            // call/list — or with a non-identifier key (`3 => 4`) — is positional.
            if (v.t == VT::Pair && a->kind == NK::Pair && !static_cast<PairExpr*>(a.get())->quotedKey) {
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
    // A Str payload NAMING an exception type ("X::Recursion", "X::Multi::NoMatch")
    // builds a real exception object like a type payload would — otherwise the
    // caught value is a bare Str and `.message` in CATCH dies, masking the error.
    bool strTypeName = e.payload.t == VT::Str && e.payload.s.rfind("X::", 0) == 0;
    if (e.payload.t != VT::Type && !strTypeName) return e.payload; // die $obj / die "msg"
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
    // a DateTime/Date carrying a :formatter gists through .Str (which runs it) —
    // `say DateTime.now(formatter => …)` shows the formatted form
    if (v.t == VT::Hash && (v.hashKind == "DateTime" || v.hashKind == "Date") &&
        v.hash && v.hash->count("formatter"))
        return methodCall(v, "Str", {}).toStr();
    if (v.t == VT::Object && v.obj && v.obj->cls) {
        if (Value* m = v.obj->cls->findMethod("gist")) { ValueList none; return invokeMethod(*m, v, none).toStr(); }
        // exceptions gist to their message (`say $!` prints "boom", not X::AdHoc<obj>)
        if (v.obj->cls->name.rfind("X::", 0) == 0) {
            auto it = v.obj->attrs.find("message");
            if (it != v.obj->attrs.end()) return it->second.toStr();
        }
    }
    // a `but VALUE` mixin boxes the base and adds a method named after VALUE's type
    // (`42 but 'x'` → a Str method): its .gist is that mixed string, not the box's.
    if (v.t == VT::Object && v.obj && v.obj->hasBoxed && v.obj->cls)
        if (Value* m = v.obj->cls->findMethod("Str")) { ValueList none; return invokeMethod(*m, v, none).toStr(); }
    if (v.t == VT::Object && v.obj && v.obj->hasBoxed) return gistOf(v.obj->boxed);
    if (v.t == VT::Object && v.obj && v.obj->cls) {
        // Rakudo's default gist: Class.new(pubattr => repr, ...) — public attrs
        // in declaration order, base-class attrs first.
        std::vector<const ClassAttr*> pub;
        std::function<void(ClassInfo*)> collect = [&](ClassInfo* c) {
            if (!c) return;
            collect(c->parent.get());
            for (auto& p : c->extraParents) collect(p.get());
            for (auto& a : c->attrs) if (a.pub) pub.push_back(&a);
        };
        collect(v.obj->cls.get());
        std::function<std::string(const Value&)> repr = [&](const Value& av) -> std::string {
            if (av.t == VT::Str && av.hashKind.empty()) {
                std::string o = "\"";
                for (char ch : av.s) { if (ch == '"' || ch == '\\' || ch == '$' || ch == '@' || ch == '{') o += '\\'; o += ch; }
                return o + "\"";
            }
            if (av.t == VT::Type) return av.ofType.empty() ? av.s : av.s + "[" + av.ofType + "]";
            if (av.t == VT::Any) return "Any";
            if (av.t == VT::Nil) return "Nil";
            if (av.t == VT::Object) return gistOf(av);
            return av.gist();
        };
        std::string o = v.obj->cls->name + ".new";
        if (!pub.empty()) {
            o += "(";
            bool first = true;
            for (auto* a : pub) {
                if (!first) o += ", "; first = false;
                auto it = v.obj->attrs.find(a->name);
                Value av = it != v.obj->attrs.end() ? it->second : Value::any();
                if (av.t == VT::Any && !a->type.empty()) av = Value::typeObj(a->type); // unset typed attr shows its type
                o += a->name + " => " + repr(av);
            }
            o += ")";
        }
        return o;
    }
    return v.gist();
}
std::string Interpreter::strOf(const Value& v) {
    // a DateTime/Date carrying a :formatter stringifies through .Str (which runs it)
    if (v.t == VT::Hash && (v.hashKind == "DateTime" || v.hashKind == "Date") &&
        v.hash && v.hash->count("formatter"))
        return methodCall(v, "Str", {}).toStr();
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

// hyper markers in operator NAMES (&infix:<»+«>, prefix:<-«>) arrive as raw
// UTF-8 — the lexer only normalizes op tokens — so map them to ASCII >>/<<.
static std::string normHyperMarkers(std::string s) {
    for (size_t p; (p = s.find("\xC2\xBB")) != std::string::npos; ) s.replace(p, 2, ">>");
    for (size_t p; (p = s.find("\xC2\xAB")) != std::string::npos; ) s.replace(p, 2, "<<");
    return s;
}

// `temp $x` / `let $x` — snapshot the container now; temp restores when the
// scope leaves, let only when it leaves UNSUCCESSFULLY. Called BEFORE the
// generic argument pre-evaluation: `temp $a = 23` must snapshot $a first.
Value Interpreter::evalTempLet(Call* c) {
    // let = temp that only restores when the scope exits UNSUCCESSFULLY
    auto& restores = c->name == "let" ? tctx_.cur->letRestores
                                      : tctx_.cur->tempRestores;
    auto snap = [](Value v) { // detach container storage so later mutation misses the snapshot
        if (v.t == VT::Array && v.arr) v.arr = std::make_shared<ValueList>(*v.arr);
        else if (v.t == VT::Hash && v.hash) v.hash = std::make_shared<std::map<std::string, Value>>(*v.hash);
        return v;
    };
    // A VarExpr target restores THROUGH its owning Env by name — a raw
    // Value* into Env.vars (an unordered_map) dangles if the map rehashes
    // before the scope leaves (same reason the Index cases go by key).
    auto pushVarRestore = [&](Expr* tg) -> bool {
        if (tg->kind != NK::VarExpr) return false;
        std::string nm = static_cast<VarExpr*>(tg)->name;
        std::shared_ptr<Env> se = tctx_.cur;
        while (se && !se->vars.count(nm)) se = se->parent;
        if (!se) return false;
        Value snapshot = snap(se->vars[nm]);
        restores.push_back([se, nm, snapshot]() {
            se->vars[nm] = snapshot;
        });
        return true;
    };
    // `temp $a = 23`: the arg is an ASSIGN — snapshot the target BEFORE
    // the assignment stores the new value (else we "restore" the new one)
    if (c->args[0]->kind == NK::Assign) {
        auto* as = static_cast<Assign*>(c->args[0].get());
        if (!pushVarRestore(as->target.get())) {
            if (Value* lv = lvalue(as->target.get())) {
                Value snapshot = snap(*lv);
                restores.push_back([lv, snapshot]() { *lv = snapshot; });
            }
        }
        return eval(c->args[0].get());
    }
    // `temp @a[$i]` / `temp %h<k>`: restore THROUGH the container by key, not
    // through a raw element pointer — the element storage can reallocate (a
    // later push) before the scope leaves, dangling a captured Value*.
    Expr* tgt = c->args[0].get();
    if (tgt->kind == NK::Index) {
        auto* ix = static_cast<Index*>(tgt);
        Value base = eval(ix->base.get());
        Value key = eval(ix->index.get());
        if (base.t == VT::Array && base.arr && !ix->isHash) {
            auto arr = base.arr; long long i = key.toInt();
            if (i < 0) i += (long long)arr->size();
            if (i >= 0 && i < (long long)arr->size()) {
                Value snapshot = snap((*arr)[i]);
                restores.push_back([arr, i, snapshot]() {
                    if (i < (long long)arr->size()) (*arr)[i] = snapshot;
                });
                return (*arr)[i];
            }
        }
        else if (base.t == VT::Hash && base.hash) {
            auto h = base.hash; std::string k = key.toStr();
            Value snapshot = h->count(k) ? snap((*h)[k]) : Value::any();
            bool existed = h->count(k) > 0;
            restores.push_back([h, k, snapshot, existed]() {
                if (existed) (*h)[k] = snapshot; else h->erase(k);
            });
            return h->count(k) ? (*h)[k] : Value::any();
        }
    }
    if (pushVarRestore(tgt)) { // plain `temp $x` / `temp @a`: restore by env+name
        if (Value* lv = lvalue(tgt)) return *lv;
        return Value::any();
    }
    if (Value* lv = lvalue(tgt)) { // non-var lvalue (attribute etc.)
        Value snapshot = snap(*lv);
        restores.push_back([lv, snapshot]() { *lv = snapshot; });
        return *lv;
    }
    return Value::any();
}

Value Interpreter::evalCall(Call* c) {
    // temp/let take their argument by EXPRESSION — the generic args pre-eval
    // would run a `temp $a = 23` assignment before the snapshot is taken
    if ((c->name == "temp" || c->name == "let") && c->args.size() == 1 &&
        !tctx_.cur->find("&" + c->name))
        return evalTempLet(c);
    ValueList args = evalArgs(c->args);
    // Whatever-curry over USER-DEFINED infixes: `* quack 5` / `5 quack *` /
    // `* quack *` build a WhateverCode, exactly like built-in binaries
    if (c->name.rfind("infix:<", 0) == 0 && args.size() == 2) {
        auto isW = [](const Value& v) {
            return v.t == VT::Whatever || (v.t == VT::Code && v.code && v.code->isWhateverCode);
        };
        if (isW(args[0]) || isW(args[1])) {
            Value* fp = tctx_.cur->find("&" + c->name);
            if (fp) {
                Value f = *fp;
                Value a0 = args[0], a1 = args[1];
                auto wArity = [&](const Value& v) -> long long {
                    if (v.t == VT::Whatever) return 1;
                    if (v.t == VT::Code && v.code && v.code->isWhateverCode)
                        return v.code->whateverArity > 0 ? v.code->whateverArity : 1;
                    return 0;
                };
                Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
                code.code->isWhateverCode = true;
                code.code->whateverArity = wArity(a0) + wArity(a1);
                code.code->builtin = [f, a0, a1](Interpreter& I, ValueList& as) -> Value {
                    size_t k = 0;
                    auto feed = [&](const Value& side) -> Value {
                        if (side.t == VT::Whatever) return k < as.size() ? as[k++] : Value::any();
                        if (side.t == VT::Code && side.code && side.code->isWhateverCode) {
                            long long n = side.code->whateverArity > 0 ? side.code->whateverArity : 1;
                            ValueList sub;
                            for (long long j = 0; j < n && k < as.size(); j++) sub.push_back(as[k++]);
                            return I.callCallable(side, std::move(sub));
                        }
                        return side;
                    };
                    Value x = feed(a0), y = feed(a1);
                    return I.callCallable(f, ValueList{x, y});
                };
                return code;
            }
        }
    }
    if (c->callee) {
        Value f = eval(c->callee.get());
        // `.&(*.tc)` on a Whatever chain COMPOSES (curry continues) instead of
        // applying the callee to the code object
        if (f.t == VT::Code && f.code && f.code->isWhateverCode &&
            args.size() == 1 && args[0].t == VT::Code && args[0].code && args[0].code->isWhateverCode) {
            Value inner = args[0], outer = f;
            Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
            code.code->isWhateverCode = true;
            code.code->whateverArity = inner.code->whateverArity > 0 ? inner.code->whateverArity : 1;
            code.code->builtin = [inner, outer](Interpreter& I, ValueList& as) -> Value {
                Value mid = I.callCallable(inner, as);
                return I.callCallable(outer, ValueList{mid});
            };
            return code;
        }
        return callCallable(f, args, &c->args, /*ownFrame=*/false, /*arityCheck=*/true);
    }
    if (!c->name.empty()) {
        // bare `::` — the current-scope stash: a Hash of every visible symbol
        // (innermost binding wins), for `::.keys` / `::{'$var'}` introspection
        if (c->name == "__stash__") {
            Value h = Value::makeHash(); h.hashKind = "Stash";
            for (Env* en = tctx_.cur.get(); en; en = en->parent.get())
                for (auto& kv : en->vars)
                    if (!h.hash->count(kv.first)) (*h.hash)[kv.first] = kv.second;
            return h;
        }
        // cas $x, {code} — atomic read-modify-write; cas($x, $expected, $new) —
        // conditional swap. Returns the value seen. Serialized by a mutex so it
        // stays atomic in parallel (no-GIL) mode too.
        if (c->name == "cas" && c->args.size() >= 2 && !tctx_.cur->find("&cas")) {
            Value* lv = nullptr;
            try { lv = lvalue(c->args[0].get()); } catch (RakuError&) {}
            if (lv) {
                static std::mutex casM;
                std::lock_guard<std::mutex> lk(casM);
                Value seen = *lv;
                if (args.size() >= 3) {
                    if (applyArith("eqv", seen, args[1]).truthy()) *lv = args[2];
                    return seen;
                }
                if (args[1].t == VT::Code) *lv = callCallable(args[1], ValueList{seen});
                return *lv; // the code form returns the value it stored (Rakudo behavior)
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
        if (Value* f = tctx_.cur->find("&" + c->name)) return callCallable(*f, args, &c->args, /*ownFrame=*/false, /*arityCheck=*/true);
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
    // `@a»++` / `%h»--` / `@a»!` (user postfix) / `(2,3)»i` — hyper postfix:
    // descends nested arrays, keeps hash keys; ++/-- mutate the elements in
    // place (through the shared containers) and yield the OLD values (postfix).
    if (c->name.rfind("hyper-postfix:<", 0) == 0 && c->name.back() == '>' && !args.empty())
        return hyperPostfixApply(c->name.substr(15, c->name.size() - 16), args[0]);
    // `$a >>[&op]<< $b` (parser-desugared, markers in the name): apply a callable
    // element-wise. A `>>` on the left / `<<` on the right marks that side STRICT
    // — it dictates the shape; a dwimmy side may only be EXTENDED (cycled), never
    // truncated, so a longer dwimmy side dies like Rakudo's non-DWIM mismatch.
    // An assign-metaop callable (&[+=]) applies the base op and stores the result
    // back through the lhs expression. Two scalars yield a scalar.
    if (c->name.rfind("hyper-with", 0) == 0 && args.size() >= 2) {
        bool strictL = c->name.size() < 13 || c->name.compare(11, 2, ">>") == 0;
        bool strictR = c->name.size() < 15 || c->name.compare(13, 2, "<<") == 0;
        Value with; ValueList pos;
        for (auto& a : args) {
            if (a.t == VT::Pair && a.s == "with" && a.pairVal) with = *a.pairVal;
            else pos.push_back(a);
        }
        Value l = pos.size() > 0 ? pos[0] : Value::any();
        Value r = pos.size() > 1 ? pos[1] : Value::any();
        std::string base; // non-empty: &[OP=] — apply OP, then assign through the lhs
        if (with.t == VT::Code && with.code && with.code->name.rfind("infix:<", 0) == 0 &&
            with.code->name.size() > 9) {
            std::string op = with.code->name.substr(7, with.code->name.size() - 8);
            if (op.size() >= 2 && op.back() == '=' && op != "==" && op != "<=" &&
                op != ">=" && op != "!=" && op != "<=>")
                base = op.substr(0, op.size() - 1);
        }
        // a USER callable may mutate its raw/rw params (sub csta(\a,\b) { a = "foo" })
        // — pass the caller's container slots so the write goes through (a null
        // slot = immutable operand, so the assignment inside dies like Rakudo)
        bool userCode = base.empty() && with.t == VT::Code && with.code && !with.code->builtin;
        auto apply = [&](const Value& x, const Value& y, Value* xs = nullptr, Value* ys = nullptr) {
            if (!base.empty()) return applyBinOp(base, x, y);
            if (userCode) {
                std::vector<Value*> slots{xs, ys};
                pendingRwSlots_ = &slots;
                Value r = callCallable(with, ValueList{x, y});
                pendingRwSlots_ = nullptr; // in case no frame consumed it
                return r;
            }
            return callCallable(with, ValueList{x, y});
        };
        // scalar operands write through the lhs/rhs EXPRESSION's slot (if any)
        Value* lroot = nullptr; Value* rroot = nullptr;
        if (userCode && !c->args.empty()) { try { lroot = lvalue(c->args[0].get()); } catch (...) {} }
        if (userCode && c->args.size() > 1) { try { rroot = lvalue(c->args[1].get()); } catch (...) {} }
        Value result = hyperCore(l, r, strictL, strictR, apply, lroot, rroot, userCode);
        if (!base.empty()) {
            // metaop assign stores through the lhs EXPRESSION; a non-lvalue
            // (`3 <<+=<< %a`) dies like Rakudo's immutable-value assignment
            Value* lv = nullptr;
            if (!c->args.empty()) { try { lv = lvalue(c->args[0].get()); } catch (...) {} }
            if (!lv) throw RakuError{Value::typeObj("X::Assignment::RO"),
                                     "Cannot modify an immutable value"};
            *lv = result;
        }
        return result;
    }
    // operator-call form: infix:<+>(1,2) / postfix:<i>($x) / prefix:<[**]>(2,3,4)
    if (c->name.rfind("infix:<", 0) == 0 && c->name.back() == '>') {
        std::string op = c->name.substr(7, c->name.size() - 8);
        if (op.size() > 2 && op.front() == '<' && op.back() == '>')
            op = op.substr(1, op.size() - 2); // infix:<<∈>> — double-angle form
        op = normHyperMarkers(op); // infix:<»+«>(…) — the hyper spelling as ASCII
        // `infix:<=>($x, v)` / `infix:<+=>($x, v)` / … — an assignment operator in
        // call form assigns (or metaop-assigns) through its l-value first operand.
        bool isAssign = op == "=" ||
            (op.size() >= 2 && op.back() == '=' && op != "==" && op != "!=" &&
             op != "<=" && op != ">=" && op != "=:=" && op != "!==" && op != ".=");
        if (isAssign && c->args.size() >= 2) {
            if (Value* lv = lvalue(c->args[0].get())) {
                *lv = (op == "=") ? args[1] : applyBinOp(op.substr(0, op.size() - 1), *lv, args[1]);
                return *lv;
            }
        }
        if (isSetOpStr(op) && args.size() < 2 && !isSetPredicateStr(op)) {
            // set-op identities: `(|)()` is the empty Set/Bag, one arg COERCES
            if (args.empty()) return setWrap({}, setOpMinTier(op));
            return setCoerceOne(op, args[0]);
        }
        if (args.size() >= 2) { // n-ary: left-fold — (|)(a,b,c) is ((a (|) b) (|) c)
            if (op == "(^)" || op == "\xE2\x8A\x96") return setSymDiffN(args); // ⊖ is variadic, not a fold
            Value acc = args[0];
            for (size_t k = 1; k < args.size(); k++) acc = applyBinOp(op, acc, args[k]);
            return acc;
        }
        return args.size() == 1 ? args[0] : Value::any();
    }
    if (c->name == "postfix:<i>" && !args.empty()) return postfixI(args[0]);
    // hyper spellings in call form: prefix:<-«>(…) / postfix:<»i>(…)
    if (c->name.rfind("prefix:<", 0) == 0 && c->name.back() == '>' && !args.empty()) {
        std::string op = normHyperMarkers(c->name.substr(8, c->name.size() - 9));
        if (op.size() > 2 && op.compare(op.size() - 2, 2, "<<") == 0)
            return hyperUnary(op.substr(0, op.size() - 2), args[0]);
    }
    if (c->name.rfind("postfix:<", 0) == 0 && c->name.back() == '>' && !args.empty()) {
        std::string op = normHyperMarkers(c->name.substr(9, c->name.size() - 10));
        if (op.size() > 2 && op.compare(0, 2, ">>") == 0)
            return hyperPostfixApply(op.substr(2), args[0]);
    }
    if (c->name.rfind("prefix:<[", 0) == 0 && c->name.size() > 11 &&
        c->name.compare(c->name.size() - 2, 2, "]>") == 0) {
        std::string op = c->name.substr(9, c->name.size() - 11); // the reduce base op
        ValueList items; for (auto& a : args) for (auto& x : a.flatten()) items.push_back(x);
        return applyReduce(op, items);
    }
    // coercion-type functions: Str(x), Int(x), Num(x), Bool(x), … and the no-arg
    // defaults Str()=='' / Int()==0 / Num()==0e0 / Bool()==False.
    {
        // container constructors called as functions: Array(...)/Set(...)/Bag(...)/Mix(...)
        // (the bareword without a call stays the type object)
        if (c->name == "Slip" && !args.empty()) { // Slip(...) coercer call
            Value sl = Value::array(); sl.isList = true; sl.s = "Slip";
            for (auto& v : args) {
                if (v.t == VT::Array && v.arr) for (auto& x : *v.arr) sl.arr->push_back(x);
                else if (v.t == VT::Range) for (auto& x : v.flatten()) sl.arr->push_back(x);
                else sl.arr->push_back(v);
            }
            return sl;
        }
        if ((c->name == "Array" || c->name == "List" || c->name == "Set" ||
             c->name == "Bag" || c->name == "Mix" || c->name == "SetHash" ||
             c->name == "BagHash" || c->name == "MixHash") && !args.empty()) {
            if (c->name == "Array" || c->name == "List") {
                Value out = Value::array();
                out.isList = (c->name == "List");
                for (auto& v : args) {
                    if ((v.t == VT::Array || v.t == VT::Range) && !v.itemized)
                        for (auto& x : v.flatten()) out.arr->push_back(x);
                    else out.arr->push_back(v);
                }
                return out;
            }
            ValueList flat;
            for (auto& v : args) {
                if (v.t == VT::Array || v.t == VT::Range) for (auto& x : v.flatten()) flat.push_back(x);
                else flat.push_back(v);
            }
            return methodCall(Value::list(flat), c->name, ValueList{});
        }
        // `DateTime($instant)` / `Date($str)` coercion routines == .new
        if ((c->name == "DateTime" || c->name == "Date") && !args.empty())
            return methodCall(Value::typeObj(c->name), "new", args);
        // `Uni(97)` / `NFC(...)` etc. coercion routines build a Uni from codepoints
        if ((c->name == "Uni" || c->name == "NFC" || c->name == "NFD" ||
             c->name == "NFKC" || c->name == "NFKD"))
            return methodCall(Value::typeObj(c->name), "new", args);
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
            if (c->name == "Int" && a0.t == VT::Rat) return methodCall(a0, "Int", {}); // 0-denominator fails X::Numeric::DivideByZero
            if (c->name == "Int") return Value::integer(a0.toInt());
            if (c->name == "Num" || c->name == "Real") return Value::number(a0.toNum());
            if (c->name == "Rat") return methodCall(a0, "Rat", {}); // CF approximation for Num
            return a0.isNumeric() ? a0 : Value::number(a0.toNum()); // Numeric
        }
    }
    // type-object coercion call: Any(x) / Mu(x) / Cool(x) is the value itself
    if ((c->name == "Any" || c->name == "Mu" || c->name == "Cool") && c->args.size() == 1)
        return eval(c->args[0].get());
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
        if (op == ",") { // [\,] : growing prefixes — ((1) (1 2) (1 2 3))
            for (size_t k = 0; k < items.size(); k++) {
                Value pre = Value::array(); pre.isList = true;
                pre.arr->assign(items.begin(), items.begin() + k + 1);
                out.arr->push_back(pre);
            }
            return out;
        }
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
        if (op == "(^)" || op == "\xE2\x8A\x96") return setWrap({}, setOpMinTier(op)); // [⊖] () is set()
        // list-building ops over nothing build nothing: [Z] () / [Z~] () / [X] () are ()
        if (op == "Z" || op == "X" || op == "," ||
            (op.size() > 1 && (op[0] == 'Z' || op[0] == 'X'))) {
            Value o = Value::array(); o.isList = true; return o;
        }
        if (chainOps.count(base)) return Value::boolean(true); // [<] () is vacuously True
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
    if (op == "Z") { // n-ary zip: [Z] rows == transpose (a fold would nest tuples)
        std::vector<ValueList> rows;
        for (auto& it : items) {
            if (it.t == VT::Array && it.arr) rows.push_back(*it.arr);
            else if (it.t == VT::Range) rows.push_back(it.flatten());
            else if (it.t == VT::Str && (it.hashKind == "Blob" || it.hashKind == "Buf")) rows.push_back(it.blobList());
            else rows.push_back(ValueList{it});
        }
        Value out = Value::array(); out.isList = true;
        if (!rows.empty()) {
            size_t n = rows[0].size();
            for (auto& r : rows) n = std::min(n, r.size());
            for (size_t i = 0; i < n; i++) {
                Value t = Value::array(); t.isList = true;
                for (auto& r : rows) t.arr->push_back(r[i]);
                out.arr->push_back(t);
            }
        }
        return out;
    }
    if (op == "X") { // n-ary cross: [X] 1..2, 3..4, 5..6 yields 3-tuples
        std::vector<ValueList> rows;
        for (auto& it : items) {
            if (it.t == VT::Array && it.arr) rows.push_back(*it.arr);
            else if (it.t == VT::Range) rows.push_back(it.flatten());
            else if (it.t == VT::Str && (it.hashKind == "Blob" || it.hashKind == "Buf")) rows.push_back(it.blobList());
            else rows.push_back(ValueList{it});
        }
        Value out = Value::array(); out.isList = true;
        bool any = !rows.empty();
        for (auto& r : rows) if (r.empty()) any = false;
        if (any) {
            std::vector<size_t> idx(rows.size(), 0);
            for (;;) {
                Value t = Value::array(); t.isList = true;
                for (size_t k = 0; k < rows.size(); k++) t.arr->push_back(rows[k][idx[k]]);
                out.arr->push_back(t);
                size_t k = rows.size();
                while (k > 0 && ++idx[k - 1] == rows[k - 1].size()) idx[--k] = 0;
                if (k == 0) break;
            }
        }
        return out;
    }
    // [(^)] / [⊖] : symmetric difference is a genuine list op (max − 2nd-max per
    // key), not the left fold the general reducer below would compute
    if (op == "(^)" || op == "\xE2\x8A\x96") return setSymDiffN(items);
    Value acc = items[0];
    for (size_t k = 1; k < items.size(); k++) acc = applyBinOp(op, acc, items[k]);
    return acc;
}

// $*TZ: a user-assigned dynamic wins; otherwise the system UTC offset
long long Interpreter::tzOffsetDyn() {
    Value* tp = nullptr;
    for (auto it = tctx_.dynStack.rbegin(); it != tctx_.dynStack.rend() && !tp; ++it)
        if (*it) tp = (*it)->find("$*TZ");
    if (!tp && tctx_.cur) tp = tctx_.cur->find("$*TZ");
    if (tp) return tp->toInt();
    // portable UTC offset: local time minus UTC of the same instant
    time_t t = ::time(nullptr);
    struct tm lt, gt;
#if defined(_WIN32)
    localtime_s(&lt, &t); gmtime_s(&gt, &t);
#else
    localtime_r(&t, &lt); gmtime_r(&t, &gt);
#endif
    long long off = (lt.tm_hour - gt.tm_hour) * 3600LL + (lt.tm_min - gt.tm_min) * 60LL;
    int dd = lt.tm_yday - gt.tm_yday;
    if (dd == 1 || dd < -1) off += 86400;      // local is a day ahead (incl. year wrap)
    else if (dd == -1 || dd > 1) off -= 86400; // local is a day behind
    return off;
}

double Interpreter::toleranceDyn() {
    Value* tp = nullptr;
    for (auto it = tctx_.dynStack.rbegin(); it != tctx_.dynStack.rend() && !tp; ++it)
        if (*it) tp = (*it)->find("$*TOLERANCE");
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
    // multidim slice @a[X;Y(;Z)] / %h{X;Y;Z}: walk level by level; Whatever
    // selects all elements at its level; scalar/list/range dims select those.
    // (Shared with the adverb block below: `:$off` adverbs still read plainly.)
    std::function<Value(const Value&)> multiDimRead;
    if (idx->multiDim) {
        multiDimRead = [&](const Value& baseV) -> Value {
            auto* dims = static_cast<ListExpr*>(idx->index.get());
            Value out = Value::array(); out.isList = true;
            bool anyMulti = false; // all-scalar dims yield the lone element, not a 1-list
            std::function<void(const Value&, size_t)> walk = [&](const Value& node, size_t d) {
                if (d == dims->items.size()) { out.arr->push_back(node); return; }
                Expr* de = dims->items[d].get();
                if (de->kind == NK::Whatever) {
                    anyMulti = true;
                    if (node.t == VT::Array && node.arr) for (auto& el : *node.arr) walk(el, d + 1);
                    else if (node.t == VT::Range) for (auto& el : node.flatten()) walk(el, d + 1);
                    else if (node.t == VT::Hash && node.hash) for (auto& kv : *node.hash) walk(kv.second, d + 1);
                    return;
                }
                Value dv = eval(de);
                if (dv.t == VT::Whatever) { // a $var holding * — same as a literal star dim
                    anyMulti = true;
                    if (node.t == VT::Array && node.arr) for (auto& el : *node.arr) walk(el, d + 1);
                    else if (node.t == VT::Range) for (auto& el : node.flatten()) walk(el, d + 1);
                    else if (node.t == VT::Hash && node.hash) for (auto& kv : *node.hash) walk(kv.second, d + 1);
                    return;
                }
                // a Callable dim resolves against this level (arrays: called with elems)
                if (dv.t == VT::Code && dv.code) {
                    if (node.t == VT::Array && node.arr)
                        dv = callCallable(dv, ValueList{Value::integer((long long)node.arr->size())});
                    else dv = callCallable(dv, ValueList{node});
                }
                ValueList keys;
                if (dv.t == VT::Range || (dv.t == VT::Array && dv.arr)) { keys = dv.flatten(); anyMulti = true; }
                else keys.push_back(dv);
                for (auto& k : keys) {
                    if (node.t == VT::Array && node.arr) {
                        long long i = k.toInt(), n = (long long)node.arr->size();
                        if (i < 0) i += n;
                        walk(i >= 0 && i < n ? (*node.arr)[i] : Value::any(), d + 1);
                    }
                    else if (node.t == VT::Hash && node.hash) {
                        auto it = node.hash->find(k.toStr());
                        walk(it != node.hash->end() ? it->second : Value::any(), d + 1);
                    }
                    else walk(Value::any(), d + 1);
                }
            };
            walk(baseV, 0);
            if (!anyMulti && out.arr->size() == 1) return (*out.arr)[0]; // @a[1;0] is a scalar
            return out;
        };
        if (idx->adverb.empty()) return multiDimRead(eval(idx->base.get()));
    }
    // (Shaped declarations `my @a[5]` now carry their dimensions on the declarator
    // itself — `declShape` — so they never reach here as an Index node. A bare
    // `(my @)[i]` is therefore a genuine subscript on the freshly-declared array.)
    // Fast path: `@plainvar[intexpr]` — the overwhelmingly common array read.
    // Grab the array's shared_ptr (one refcount, safe across the index eval)
    // instead of copying the whole ~300-byte Value three times through eval().
    if (!idx->isHash && idx->adverb.empty() && idx->index &&
        idx->base->kind == NK::VarExpr) {
        auto* ve = static_cast<VarExpr*>(idx->base.get());
        if (!ve->declare && ve->name.size() > 1 && ve->name[0] == '@' &&
            (std::isalpha((unsigned char)ve->name[1]) || ve->name[1] == '_')) {
            if (Value* bp = tctx_.cur->find(ve->name)) {
                if (bp->t == VT::Array && bp->arr && !bp->ext && bp->ofType.empty() &&
                    !bp->pairVal && bp->hashKind.empty()) { // `is default` arrays take the slow path
                    auto arr = bp->arr; // keeps elements alive if the index expr mutates the var
                    Value iv = eval(idx->index.get());
                    if (iv.t == VT::Int && !iv.big) {
                        long long ix = iv.i;
                        if (ix < 0) { // negative is out of range (no Python wraparound) → Failure
                            long long sz = (long long)arr->size();
                            Value f = Value::makeHash(); f.hashKind = "Failure";
                            (*f.hash)["exception"] = Value::typeObj("X::OutOfRange");
                            (*f.hash)["message"] = Value::str("Index out of range. Is: " + std::to_string(ix) +
                                                              ", should be in 0.." + std::to_string(sz > 0 ? sz - 1 : 0));
                            return f;
                        }
                        if (ix < (long long)arr->size()) {
                            Value el = (*arr)[ix];
                            // an element is a scalar container: a nested Array stays
                            // ONE item downstream (`my @row = @m[0]` is [[...],])
                            if (el.t == VT::Array) el.itemized = true;
                            return el;
                        }
                        return Value::any();
                    }
                    // non-Int subscript (range/whatever/list): fall through with the
                    // base already at hand — re-enter the general machinery below
                }
            }
        }
    }
    Value base = eval(idx->base.get());
    // Indexing an unhandled Failure propagates it (`@a[-1][0]` keeps the Failure
    // from the out-of-range outer index rather than reading through it).
    if (base.t == VT::Hash && base.hashKind == "Failure") return base;
    // NativeCall CArray / Pointer element read (`$carray[$i]`): byte-backed
    // (CArray.new/allocate stores packed bytes) or a live native pointer.
    if (!idx->isHash && idx->index && !idx->multiDim) {
        if (base.t == VT::Str && base.hashKind == "CArray") {
            long long i = eval(idx->index.get()).toInt();
            std::string et = base.enumName.empty() ? "int64" : base.enumName;
            bool sgn, isFlt; int w = ncScalarWidth(et, sgn, isFlt); if (!w) w = 8;
            if (i < 0 || (i + 1) * w > (long long)base.s.size()) return Value::any();
            return ncReadElem((long long)(intptr_t)base.s.data(), et, i);
        }
        if (base.t == VT::Hash && (base.hashKind == "CArray" || base.hashKind == "Pointer") && base.hash->count("addr")) {
            long long i = eval(idx->index.get()).toInt();
            std::string of = base.hash->count("of") ? (*base.hash)["of"].toStr() : "int64";
            return ncReadElem((*base.hash)["addr"].toInt(), of, i);
        }
    }
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
    // (all leaf values, descending nested hashes). An ADVERBED `{*}` falls
    // through to the full adverb machinery below (negation, :k($flag), :exists,
    // :delete); only the plain form and the hyperslice take this fast path.
    if (idx->isHash && base.t == VT::Hash && base.hash && idx->index && idx->index->kind == NK::Whatever &&
        (idx->adverb.empty() || static_cast<const WhateverExpr*>(idx->index.get())->hyper)) {
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
        bool presenceNeg = false; // :k/:v/:kv/:p negative polarity (:!k / :k(False))
        std::vector<std::string> unknownAdv; // `:zorp` / `:zip:zop` — not a subscript adverb
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
                bool hasArg = false, argOn = false;
                size_t qm = part.find('?'); // `:exists($dont)` — variable-valued adverb
                if (qm != std::string::npos) {
                    Value* vp = tctx_.cur->find(part.substr(qm + 1));
                    argOn = vp && vp->truthy(); hasArg = true;
                    std::string nm = part.substr(0, qm);
                    if (nm == "exists") { if (!argOn) neg = !neg; }    // :exists(False) tests NON-existence
                    else if (nm == "delete") { if (!argOn) continue; }  // :delete(False) doesn't delete
                    part = nm;
                }
                // :k/:v/:kv/:p report a MISSING element (undefined value) only under
                // NEGATIVE polarity — `:!k` or `:k(False)`; existing always reports.
                bool posPol = hasArg ? (neg ? !argOn : argOn) : !neg;
                if (part == "exists") { wantExists = true; negExists = neg; }
                else if (part == "delete") wantDelete = true;
                else if (part == "kv") { kvF = true; presenceNeg = !posPol; }
                else if (part == "p")  { pF = true;  presenceNeg = !posPol; }
                else if (part == "k")  { kF = true;  presenceNeg = !posPol; }
                else if (part == "v")  { vF = true;  presenceNeg = !posPol; }
                else if (!part.empty()) unknownAdv.push_back(part); // :zorp / :zip / :zop
            }
        }
        // an unrecognised subscript adverb is an error (Rakudo: no postcircumfix
        // candidate → X::Adverb / dispatch failure): `@a[1]:zorp`, `%h<a>:kv:p:zip:zop`
        if (!unknownAdv.empty()) {
            std::string list;
            for (auto& u : unknownAdv) { if (!list.empty()) list += " "; list += u; }
            throw RakuError{Value::typeObj("X::Adverb"),
                "Unexpected adverbs passed to subscript: " + list};
        }
        if (idx->multiDim && !(wantExists || wantDelete || kvF || pF || kF || vF))
            return multiDimRead(base); // all adverbs conditionally off → plain multidim read
        if (wantExists || wantDelete || kvF || pF || kF || vF) {
        // On a multidim subscript, `:!exists:delete` has no matching postcircumfix
        // candidate in Rakudo and dies; a plain/chained subscript accepts it
        // (the :exists half reports negated existence, :delete removes).
        if ((idx->semicolonSub || idx->multiDim) && wantExists && negExists && wantDelete)
            throw RakuError{Value::typeObj("X::Adverb"),
                "Unexpected adverbs passed to subscript: combination of :!exists and :delete"};
        // the presentation adverbs (k/v/kv/p) are mutually exclusive; :exists
        // combines with :kv/:p/:delete but not :v
        if ((int)kF + (int)vF + (int)kvF + (int)pF > 1 || (wantExists && vF))
            throw RakuError{Value::typeObj("X::Adverb"),
                "Unexpected adverbs passed to subscript"};
        // ── adverbed multidim: navigate to the leaf's parent, apply the adverb set;
        // :kv/:p/:k report the WHOLE key tuple (%h{a;b;c}:kv is ((a,b,c), v)).
        if (idx->multiDim) {
            auto* dims = static_cast<ListExpr*>(idx->index.get());
            ValueList keys; bool anyMulti = false;
            for (auto& de : dims->items) {
                if (de->kind == NK::Whatever) { anyMulti = true; keys.push_back(Value::whatever()); continue; }
                Value k = eval(de.get());
                if (k.t == VT::Array || k.t == VT::Range || k.t == VT::Whatever) anyMulti = true;
                keys.push_back(k);
            }
            // a :delete'd (or :kv/:p/:v-reported) Array value decontainerizes to a List
            // ((314,) not [314]) — the 6.e multislice result shape
            auto decontList = [](const Value& v) {
                if (v.t == VT::Array && v.arr && !v.isList) { Value c = v; c.isList = true; c.itemized = false; return c; }
                return v;
            };
            if (!anyMulti && !keys.empty()) {
                auto resolveKey = [&](Value k, const Value& node) {
                    if (k.t == VT::Code && k.code) {
                        if (node.t == VT::Array && node.arr)
                            return callCallable(k, ValueList{Value::integer((long long)node.arr->size())});
                        return callCallable(k, ValueList{node});
                    }
                    return k;
                };
                Value cur = base; bool navOk = true;
                for (size_t d = 0; d + 1 < keys.size() && navOk; d++) {
                    keys[d] = resolveKey(keys[d], cur);
                    if (cur.t == VT::Hash && cur.hash) {
                        auto it = cur.hash->find(keys[d].toStr());
                        if (it == cur.hash->end()) { navOk = false; break; }
                        cur = it->second;
                    } else if (cur.t == VT::Array && cur.arr) {
                        long long i = keys[d].toInt(), n = (long long)cur.arr->size();
                        if (i < 0) i += n;
                        if (i < 0 || i >= n) { navOk = false; break; }
                        cur = (*cur.arr)[i];
                    } else navOk = false;
                }
                keys.back() = resolveKey(keys.back(), cur);
                bool exists = false; Value val;
                if (navOk && cur.t == VT::Hash && cur.hash) {
                    auto it = cur.hash->find(keys.back().toStr());
                    if (it != cur.hash->end()) { exists = true; val = it->second; }
                    if (wantDelete && exists) cur.hash->erase(keys.back().toStr());
                } else if (navOk && cur.t == VT::Array && cur.arr) {
                    long long i = keys.back().toInt(), n = (long long)cur.arr->size();
                    if (i < 0) i += n;
                    if (i >= 0 && i < n && isDefined((*cur.arr)[i])) { exists = true; val = (*cur.arr)[i]; }
                    if (wantDelete && exists) {
                        (*cur.arr)[i] = Value::any();
                        while (!cur.arr->empty() && !isDefined(cur.arr->back())) cur.arr->pop_back(); // trailing holes shrink
                    }
                }
                Value keyTuple = Value::array(keys); keyTuple.isList = true;
                auto tuplePair = [&](const Value& v2) {
                    Value p = Value::pair(keyTuple.toStr(), v2);
                    p.pairKey = std::make_shared<Value>(keyTuple);
                    return p;
                };
                auto emptyList = []() { Value e = Value::array(); e.isList = true; return e; };
                if (wantExists) {
                    Value ex = Value::boolean(negExists ? !exists : exists);
                    if (kvF) { if (!exists) return emptyList();
                               Value o = Value::array({keyTuple, ex}); o.isList = true; return o; }
                    if (pF)  return exists ? tuplePair(ex) : Value::nil();
                    return ex;
                }
                if (kF)  return exists ? keyTuple : Value::nil();
                if (kvF) { if (!exists) return emptyList();
                           Value o = Value::array({keyTuple, decontList(val)}); o.isList = true; return o; }
                if (pF)  return exists ? tuplePair(decontList(val)) : Value::nil();
                return exists ? decontList(val) : Value::nil(); // plain :delete / :v → the value or Nil
            }
            if (anyMulti) {
                // slice dims (Whatever / list / range per level) with adverbs:
                // expand into concrete per-branch index tuples, then apply the
                // adverb set per tuple, assembling one flat result list
                std::vector<ValueList> tuples;
                std::function<void(const Value&, size_t, ValueList&)> expand =
                    [&](const Value& node, size_t d, ValueList& pref) {
                    if (d == keys.size()) { tuples.push_back(pref); return; }
                    auto emit1 = [&](Value kk) {
                        if (kk.t == VT::Code && kk.code) // *-1 against this branch's size
                            kk = (node.t == VT::Array && node.arr)
                               ? callCallable(kk, ValueList{Value::integer((long long)node.arr->size())})
                               : callCallable(kk, ValueList{node});
                        Value child = Value::any();
                        if (node.t == VT::Array && node.arr) {
                            long long i = kk.toInt(), n = (long long)node.arr->size();
                            if (i < 0) { i += n; kk = Value::integer(i); }
                            if (i >= 0 && i < n) child = (*node.arr)[i];
                        } else if (node.t == VT::Hash && node.hash) {
                            auto it = node.hash->find(kk.toStr());
                            if (it != node.hash->end()) child = it->second;
                        }
                        pref.push_back(kk);
                        expand(child, d + 1, pref);
                        pref.pop_back();
                    };
                    const Value& k = keys[d];
                    if (k.t == VT::Whatever) {
                        if (node.t == VT::Array && node.arr)
                            for (long long i = 0; i < (long long)node.arr->size(); i++) emit1(Value::integer(i));
                        else if (node.t == VT::Hash && node.hash)
                            for (auto& kv2 : *node.hash) emit1(Value::str(kv2.first));
                        return;
                    }
                    if (k.t == VT::Array || k.t == VT::Range) {
                        for (auto& e2 : k.flatten()) emit1(e2);
                        return;
                    }
                    emit1(k);
                };
                ValueList pref;
                expand(base, 0, pref);
                Value out = Value::array(); out.isList = true;
                for (auto& tup : tuples) {
                    Value cur = base; bool navOk = true;
                    for (size_t d2 = 0; d2 + 1 < tup.size() && navOk; d2++) {
                        if (cur.t == VT::Hash && cur.hash) {
                            auto it = cur.hash->find(tup[d2].toStr());
                            if (it == cur.hash->end()) { navOk = false; break; }
                            cur = it->second;
                        } else if (cur.t == VT::Array && cur.arr) {
                            long long i = tup[d2].toInt(), n = (long long)cur.arr->size();
                            if (i < 0) i += n;
                            if (i < 0 || i >= n) { navOk = false; break; }
                            cur = (*cur.arr)[i];
                        } else navOk = false;
                    }
                    bool exists = false; Value val;
                    if (navOk && cur.t == VT::Hash && cur.hash) {
                        auto it = cur.hash->find(tup.back().toStr());
                        if (it != cur.hash->end()) { exists = true; val = it->second; }
                        if (wantDelete && exists) cur.hash->erase(tup.back().toStr());
                    } else if (navOk && cur.t == VT::Array && cur.arr) {
                        long long i = tup.back().toInt(), n = (long long)cur.arr->size();
                        if (i < 0) i += n;
                        if (i >= 0 && i < n && isDefined((*cur.arr)[i])) { exists = true; val = (*cur.arr)[i]; }
                        if (wantDelete && exists) {
                            (*cur.arr)[i] = Value::any();
                            while (!cur.arr->empty() && !isDefined(cur.arr->back())) cur.arr->pop_back();
                        }
                    }
                    Value keyTuple = Value::array(tup); keyTuple.isList = true;
                    auto tuplePair = [&](const Value& v2) {
                        Value p = Value::pair(keyTuple.toStr(), v2);
                        p.pairKey = std::make_shared<Value>(keyTuple);
                        return p;
                    };
                    if (wantExists) {
                        Value ex = Value::boolean(negExists ? !exists : exists);
                        // :kv/:p pair with :exists only under positive presence —
                        // or ALWAYS under negated presentation (`:!kv`)
                        if (kvF)      { if (exists || presenceNeg) { out.arr->push_back(keyTuple); out.arr->push_back(ex); } }
                        else if (pF)  { if (exists || presenceNeg) out.arr->push_back(tuplePair(ex)); }
                        else out.arr->push_back(ex);
                    }
                    else if (kF)  { if (exists) out.arr->push_back(keyTuple); }
                    else if (kvF) { if (exists) { out.arr->push_back(keyTuple); out.arr->push_back(decontList(val)); } }
                    else if (pF)  { if (exists) out.arr->push_back(tuplePair(decontList(val))); }
                    else if (vF)  { if (exists) out.arr->push_back(decontList(val)); }
                    else { // plain slice, possibly with :delete
                        if (exists) out.arr->push_back(wantDelete ? decontList(val) : val);
                        else out.arr->push_back(wantDelete ? Value::nil() : Value::any());
                    }
                }
                return out;
            }
        }
        Value iv = eval(idx->index.get());
        // `@a[*]` / `%h{*}` — and the zen slice `@a[]`, which parses to `[*]` —
        // with an adverb select EVERY element.
        bool allElems = iv.t == VT::Whatever;
        bool slice = allElems || iv.t == VT::Array || iv.t == VT::Range;
        ValueList sliceKeys;
        if (allElems) {
            if (idx->isHash && base.t == VT::Hash && base.hash)
                for (auto& e2 : *base.hash) sliceKeys.push_back(Value::str(e2.first));
            else if (base.t == VT::Array && base.arr)
                for (long long i = 0; i < (long long)base.arr->size(); i++)
                    sliceKeys.push_back(Value::integer(i));
        }
        else if (slice) for (auto& k : iv.flatten()) sliceKeys.push_back(k);
        else sliceKeys.push_back(iv);
        bool lazySlice = slice && iv.b && (iv.t == VT::Range || iv.t == VT::Array); // @a[lazy …]:adv
        struct Hit { Value keyV; Value val; bool exists; };
        std::vector<Hit> hits;
        for (auto& kv : sliceKeys) {
            bool exists = false; Value val; Value keyV;
            if (idx->isHash) {
                std::string key = kv.toStr(); keyV = Value::str(key);
                if (base.t == VT::Hash && base.hash) {
                    auto it = base.hash->find(key);
                    if (it != base.hash->end()) { exists = true; val = it->second; }
                }
            } else {
                // `*-1` / `*` in an adverbed slice (`@a[*-1, *-2]:v`) resolve against length
                long long asz = (base.t == VT::Array && base.arr) ? (long long)base.arr->size() : 0;
                Value kres = kv;
                if (kres.t == VT::Code && kres.code && kres.code->isWhateverCode)
                    kres = callCallable(kres, ValueList{Value::integer(asz)});
                long long ai = (kres.t == VT::Whatever || std::isinf(kres.toNum())) ? asz - 1 : kres.toInt();
                bool inBounds = false;
                if (base.t == VT::Array && base.arr) {
                    // a negative index is out of range (no from-the-end wraparound)
                    if (ai >= 0 && ai < (long long)base.arr->size()) { inBounds = true; exists = isDefined((*base.arr)[ai]); val = (*base.arr)[ai]; }
                }
                if (lazySlice && !inBounds) break; // lazy slice stops at the first hole
                keyV = Value::integer(ai);
            }
            hits.push_back({keyV, val, exists});
        }
        // Set/Bag/Mix are immutable — :delete dies (SetHash/BagHash/MixHash mutate)
        if (wantDelete && base.t == VT::Hash &&
            (base.hashKind == "Set" || base.hashKind == "Bag" || base.hashKind == "Mix"))
            throw RakuError{Value::typeObj("X::Immutable"),
                "Cannot modify an immutable " + base.hashKind + " (" + base.gist() + ")"};
        if (wantDelete) for (auto& h : hits) if (h.exists) {
            if (idx->isHash) base.hash->erase(h.keyV.toStr());
            else { long long ai = h.keyV.toInt();
                   if (base.arr && ai >= 0 && ai < (long long)base.arr->size()) (*base.arr)[ai] = Value::any(); }
        }
        // trailing holes shrink the array after deletes (plain subscripts only —
        // a multidim form that fell through indexes NESTED arrays, not this one)
        if (wantDelete && !idx->isHash && !idx->multiDim && !idx->semicolonSub &&
            base.t == VT::Array && base.arr)
            while (!base.arr->empty() && !isDefined(base.arr->back())) base.arr->pop_back();
        // `:p` keeps the real key type — an array index is an Int (`1 => "b"`),
        // not the stringified key a plain Pair would carry.
        auto mkPair = [](const Value& kk, const Value& vv) {
            Value p = Value::pair(kk.toStr(), vv);
            if (kk.t != VT::Str) p.pairKey = std::make_shared<Value>(kk);
            return p;
        };
        // a missing element's reported value is the container's typed default —
        // the TYPE OBJECT (`Str` for `my Str @s`, `Any` untyped), so it compares
        // and stringifies like Rakudo's ("B", Any), not as a blank undefined
        auto missLeaf = [&]() -> Value {
            if (idx->isHash) {
                if (base.t == VT::Hash && !base.ofType.empty()) return typedElemDefault(base);
                return Value::typeObj("Any");
            }
            Value dv = arrayMissingDefault(base);
            return dv.t == VT::Nil || dv.t == VT::Any ? Value::typeObj("Any") : dv;
        };
        if (!slice) {
            auto& h = hits[0];
            if (wantExists) {
                Value ex = Value::boolean(negExists ? !h.exists : h.exists);
                auto emptyE = []() { Value e = Value::array(); e.isList = true; return e; };
                if (kvF) { if (!(h.exists || presenceNeg)) return emptyE();
                           Value o = Value::array({h.keyV, ex}); o.isList = true; return o; }
                if (pF) return (h.exists || presenceNeg) ? mkPair(h.keyV, ex) : emptyE();
                return ex;
            }
            auto emptyL = []() { Value e = Value::array(); e.isList = true; return e; }; // () not []
            Value mval = missLeaf(); // a missing element's (undefined, typed) value
            if (kF)  return (h.exists || presenceNeg) ? h.keyV : emptyL();
            if (vF)  return h.exists ? h.val : (presenceNeg ? mval : emptyL());
            if (kvF) { if (!(h.exists || presenceNeg)) return emptyL();
                       Value o = Value::array({h.keyV, h.exists ? h.val : mval}); o.isList = true; return o; }
            if (pF)  return (h.exists || presenceNeg)
                         ? mkPair(h.keyV, h.exists ? h.val : mval) : emptyL();
            if (h.exists) return h.val; // plain :delete (or all conditionals off after delete)
            Value dv = arrayMissingDefault(base); // `is default(v)` / typed element default
            return dv.t == VT::Nil ? Value::any() : dv;
        }
        // slice + adverb: :exists is per-key; the others filter to existing keys
        Value out = Value::array(); out.isList = true;
        for (auto& h : hits) {
            if (wantExists) {
                Value ex = Value::boolean(negExists ? !h.exists : h.exists);
                if (kvF)      { if (h.exists || presenceNeg) { out.arr->push_back(h.keyV); out.arr->push_back(ex); } }
                else if (pF)  { if (h.exists || presenceNeg) out.arr->push_back(mkPair(h.keyV, ex)); }
                else out.arr->push_back(ex);
                continue;
            }
            if (!h.exists && !presenceNeg) continue; // missing kept only under :!k / :k(False)
            Value v = h.exists ? h.val : missLeaf();
            if (kvF) { out.arr->push_back(h.keyV); out.arr->push_back(v); }
            else if (pF) out.arr->push_back(mkPair(h.keyV, v));
            else if (kF) out.arr->push_back(h.keyV);
            else out.arr->push_back(v); // :v or plain :delete slice
        }
        return out;
        }
    }

    if (idx->isHash) {
        // Associative indexing on an array-backed value: a Capture (`\(1, :i)`) is
        // stored as an Array of positionals + Pairs, and `c<i>` finds the named part.
        // On a plain Array with no such named element it's a type error (`$aref<0>`).
        if (base.t == VT::Array) {
            Value iv = eval(idx->index.get());
            std::string key = iv.toStr();
            bool capturish = base.hashKind == "Capture"; // \(…) — even with no named parts
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
            if (base.t == VT::Pair) // a Pair is associative on its own key
                return key == base.s && base.pairVal ? *base.pairVal : Value::any();
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
        // a Blob/Buf subscript works on ELEMENTS: `$b[*-1]` / `$b[0..*-2]` need
        // the element count as the resolved length (bytes, or words for blob32)
        if (base.t == VT::Str && (base.hashKind == "Blob" || base.hashKind == "Buf"))
            n = base.blobElems();
        std::vector<long long> indices;
        bool isSlice = false;
        bool lazyIdx = false; // `@a[lazy 3..5]` truncates at the array end (no defaulting)
        bool junctionIdx = false; // `@a[any(1,2,3)]` threads: missing indices drop, not default
        // `*` / `*-1` resolve against the list length — for Range endpoints AND for
        // every element of a list subscript (`@a[*-1, *-2]`, `@a[@whatever-list]`).
        auto resolveWhat = [&](Value v) -> long long {
            if (v.t == VT::Code && v.code && v.code->isWhateverCode) return callCallable(v, ValueList{Value::integer(n)}).toInt();
            if (v.t == VT::Whatever || std::isinf(v.toNum())) return n - 1;
            return v.toInt();
        };
        if (idx->index->kind == NK::Range) {
            auto* re = static_cast<RangeExpr*>(idx->index.get());
            // `*` in an endpoint resolves to the list length: `@a[0 .. *-2]`, `@a[*-3 .. *-1]`.
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
            bool wasWhatever = false;
            if (iv.t == VT::Code && iv.code && iv.code->isWhateverCode) {
                iv = callCallable(iv, ValueList{Value::integer(n)});
                wasWhatever = true;
            }
            if (iv.t == VT::Whatever) {
                isSlice = true;
                for (long long k = 0; k < n; k++) indices.push_back(k);
            } else if (iv.t == VT::Range || iv.t == VT::Array) {
                isSlice = true;
                lazyIdx = iv.b; // `lazy` marker set by the lazy() builtin
                junctionIdx = isJunction(iv); // a junction index autothreads (no defaulting)
                for (auto& e : iv.flatten()) indices.push_back(resolveWhat(e)); // @a[*-1, *-2]
            } else {
                long long i = iv.toInt();
                if (base.t == VT::Str) {
                    if (base.hashKind == "Blob" || base.hashKind == "Buf") { // element view
                        long long bn = base.blobElems();
                        if (i < 0) i += bn;
                        return (i >= 0 && i < bn)
                             ? Value::integer(base.blobWordAt(i)) : Value::any();
                    }
                    // a Str is a one-item list: "ab"[0] is "ab"
                    if (i == 0) return base;
                    throw RakuError{Value::typeObj("X::OutOfRange"),
                        "Index out of range. Is: " + std::to_string(i) + ", should be in 0..0"};
                }
                // any other SCALAR (Pair, Int, …) is a one-item list too:
                // $b.grabpairs[0] indexes the single returned Pair
                if (base.t == VT::Pair || base.t == VT::Int || base.t == VT::Num ||
                    base.t == VT::Rat || base.t == VT::Bool || base.t == VT::Complex) {
                    if (i == 0 || i == -1) return base;
                    return Value::nil();
                }
                // A negative index is OUT OF RANGE in Raku (there is no Python-style
                // from-the-end wraparound — that is what `@a[*-1]` is for). Both a
                // literal `@a[-1]` and a `*-N` that resolves below 0 yield a Failure.
                (void)wasWhatever;
                if (i < 0) {
                    Value f = Value::makeHash(); f.hashKind = "Failure";
                    (*f.hash)["exception"] = Value::typeObj("X::OutOfRange");
                    (*f.hash)["message"] = Value::str("Index out of range. Is: " + std::to_string(i) +
                                                      ", should be in 0.." + std::to_string(n > 0 ? n - 1 : 0));
                    return f;
                }
                if (i >= 0 && i < n) {
                    // a hole (deleted slot) in a defaulted/typed array reads as the default
                    if (base.t == VT::Array && (src[i].t == VT::Nil || src[i].t == VT::Any) &&
                        (base.pairVal || !base.ofType.empty()))
                        return arrayMissingDefault(base);
                    return src[i];
                }
                if (base.pairVal) return *base.pairVal; // `is default(v)`
                if (!base.ofType.empty()) return typedElemDefault(base);
                // A List/Seq/Range indexed out of range yields Nil (List.AT-POS);
                // only a mutable Array yields the element type default (Any).
                return (base.t == VT::Range || base.isList) ? Value::nil() : Value::any();
            }
        }
        if (isSlice) {
            // Blob/Buf slices index the BYTES ($b[1..2] / $b[0,2] / $b[^2]),
            // not the one-item-list view a plain Str gets
            if (base.t == VT::Str && (base.hashKind == "Blob" || base.hashKind == "Buf")) {
                Value out = Value::array(); out.isList = true;
                long long bn = base.blobElems();
                for (long long k : indices) {
                    if (k < 0) k += bn;
                    if (k >= 0 && k < bn) out.arr->push_back(Value::integer(base.blobWordAt(k)));
                }
                return out;
            }
            Value out = Value::array(); out.isList = true;
            // a missing slice index reports the element default: a mutable Array
            // yields its typed default (Any / Str / `is default`), a List/Range Nil
            auto missElem = [&]() -> Value {
                if (base.pairVal) return *base.pairVal;
                if (!base.ofType.empty()) return typedElemDefault(base);
                return (base.t == VT::Range || base.isList) ? Value::nil() : Value::any();
            };
            for (long long k : indices) {
                if (k < 0) k += n;
                if (k >= 0 && k < n) out.arr->push_back(src[k]);
                else if (lazyIdx) break; // lazy slice: stop at the first hole, don't default
                else if (junctionIdx) continue; // junction index: a missing element threads to nothing
                else out.arr->push_back(missElem());
            }
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
    } else if (base.t == VT::Pair || base.t == VT::Int || base.t == VT::Num ||
               base.t == VT::Rat || base.t == VT::Bool || base.t == VT::Complex) {
        // a scalar is a one-item list: $b.grabpairs[0] indexes the single Pair
        if (i == 0 || i == -1) return base;
        return Value::nil();
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
            if (nl->isRat) {
                // build once, then share the immutable BigInt parts on every eval
                // (ratZ: an explicit zero denominator — `<42/0>` — is preserved)
                if (!nl->cacheN) {
                    Value first = nl->bigNum.empty()
                        ? Value::ratZ(BigInt(nl->ratNum), BigInt(nl->ratDen))         // 3.14 is a Rat
                        : Value::ratZ(BigInt::fromString(nl->bigNum), BigInt::fromString(nl->bigDen));
                    nl->cacheD = first.ratD; // set D first: cacheN is the "ready" flag
                    nl->cacheN = first.ratN;
                    return first;
                }
                Value v; v.t = VT::Rat;
                v.ratN = std::static_pointer_cast<BigInt>(nl->cacheN);
                v.ratD = std::static_pointer_cast<BigInt>(nl->cacheD);
                return v;
            }
            return Value::number(nl->v); }
        case NK::StrLit: { auto* sl = static_cast<StrLit*>(e); // NFC-normalize once (Raku's NFG storage)
            if (!sl->nfcDone) { sl->v = nfcNormalize(sl->v); sl->nfcDone = true; }
            return Value::str(sl->v); }
        case NK::AllomorphLit: {
            auto* al = static_cast<AllomorphLit*>(e);
            Value v = eval(al->num.get());
            v.hashKind = v.t == VT::Int ? "IntStr" : v.t == VT::Rat ? "RatStr"
                       : v.t == VT::Complex ? "ComplexStr" : "NumStr";
            v.s = al->str; // the source spelling, so it stringifies back to itself
            return v;
        }
        case NK::RegexLit: {
            auto* rl = static_cast<RegexLit*>(e);
            // an anonymous `regex {…}`/`token {…}`/`rule {…}` term: a Regex
            // value that runs its code blocks in the scope it closed over
            if (!rl->declKind.empty()) {
                Value v = Value::regex(rl->pattern);
                v.hashKind = rl->declKind;
                v.ext = std::static_pointer_cast<void>(tctx_.cur);
                return v;
            }
            // rx// is always the Regex object; bare /…/ and m// match against $_
            if (rl->isRx) return Value::regex(rl->pattern);
            Value topic; if (Value* p = tctx_.cur->find("$_")) topic = *p;
            return regexMatch(topic.toStr(), rl->pattern);
        }
        case NK::SubstLit: {
            auto* sl = static_cast<SubstLit*>(e);
            Value topic; if (Value* p = tctx_.cur->find("$_")) topic = *p;
            if (isTrSubst(sl->pattern)) { // tr/// against $_
                if (topic.readonly)
                    throw RakuError{Value::typeObj("X::Assignment::RO"), "Cannot modify a readonly value"};
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
            // Fast path: a plain lexical ($x/@a/%h/&f — second char is a letter
            // or underscore, so every twigilled/special name is excluded) that is
            // FOUND in scope returns immediately, skipping the special-name
            // string compares below. Misses and Proxies take the full path.
            if (!ve->declare && ve->name.size() > 1 &&
                (std::isalpha((unsigned char)ve->name[1]) || ve->name[1] == '_')) {
                if (Value* p = tctx_.cur->find(ve->name)) {
                    if (!(p->t == VT::Hash && p->hashKind == "Proxy")) return *p;
                }
            }
            if (ve->name == "$=finish") return Value::str(finishData_); // =finish data block
            if (ve->name == "%?RESOURCES") // dist resource files of the loading module
                return resourceStack_.empty() ? Value::makeHash() : resourceStack_.back();
            if (ve->name == "$?LINE") return Value::integer(ve->line);
            if (ve->name == "$?FILE") return Value::str(srcFileAbs_.empty() ? srcFile_ : srcFileAbs_);
            // Built-in magic dynamic vars ($*OUT, $*CWD, …). A user binding
            // (`my $*OUT = …`) or a fresh declaration takes precedence, so only
            // fall back to the built-in default when the name is neither being
            // declared nor already bound in this scope / the caller chain.
            bool builtinDefault = !ve->declare;
            if (builtinDefault && ve->name.size() > 1 && ve->name[1] == '*') {
                if (tctx_.cur->find(ve->name)) builtinDefault = false;
                else for (auto it = tctx_.dynStack.rbegin(); it != tctx_.dynStack.rend(); ++it)
                    if (*it && (*it)->find(ve->name)) { builtinDefault = false; break; }
            }
            if (builtinDefault) {
            if (ve->name == "$=pod" || ve->name == "@=pod") { Value a = Value::array(); *a.arr = podDom_; return a; }
            if (ve->name == "$*CWD") { char buf[4096]; Value p = Value::str(getcwd(buf, sizeof buf) ? buf : "."); p.hashKind = "IO"; return p; }
            if (ve->name == "$*RAKU" || ve->name == "$*PERL" || ve->name == "$?RAKU" || ve->name == "$?PERL") {
                Value r = Value::makeHash(); r.hashKind = "Raku"; return r;
            }
            if (ve->name == "$*PROGRAM") { Value p = Value::str(srcFile_); p.hashKind = "IO"; return p; } // running script, as IO::Path
            if (ve->name == "$*PROGRAM-NAME") return Value::str(srcFile_);
            if (ve->name == "$*USAGE") { std::string u = mainUsage(); if (!u.empty() && u.back() == '\n') u.pop_back(); return Value::str(u); }
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
            if (ve->name == "$*THREAD") { if (t_threadSelf.t == VT::Hash) return t_threadSelf; Value h = Value::makeHash(); h.hashKind = "Thread"; (*h.hash)["initial"] = Value::boolean(threadDepth_ == 0); (*h.hash)["id"] = Value::integer(1); return h; }
            if (ve->name == "$*SCHEDULER") {
                if (tctx_.cur) if (Value* p = tctx_.cur->find("$*SCHEDULER")) return *p; // user-assigned wins
                return defaultScheduler_;
            }
            if (ve->name == "$*PID") return Value::integer((long long)::getpid());
            if (ve->name == "$*TZ") return Value::integer(tzOffsetDyn());
            if (ve->name == "$*INIT-INSTANT") { Value v = Value::number(initInstant_); v.hashKind = "Instant"; return v; }
            if (ve->name == "&?BLOCK" && tctx_.curBlockVal) return *tctx_.curBlockVal;
            if (ve->name == "&?ROUTINE" && tctx_.curRoutineVal) return *tctx_.curRoutineVal;
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
                if (ve->declDefault) { // `is default(v)`: initial AND reset value
                    Value dv = eval(ve->declDefault.get());
                    if (sigil == '@' || sigil == '%') { // container stays empty; v is the ELEMENT default
                        Value c = sigil == '@' ? Value::array() : Value::makeHash();
                        c.pairVal = std::make_shared<Value>(dv);
                        tctx_.cur->define(ve->name, c);
                        return tctx_.cur->vars[ve->name];
                    }
                    tctx_.cur->varDefault[ve->name] = dv;
                    tctx_.cur->define(ve->name, dv);
                    return tctx_.cur->vars[ve->name];
                }
                if (ve->declShape && sigil == '@') { // shaped array `my @a[2;3]`
                    tctx_.cur->define(ve->name, makeShapedContainer(evalShapeDims(ve->declShape.get()), ve->declType));
                    return tctx_.cur->vars[ve->name];
                }
                if (!ve->declType.empty() || !tctx_.cur->vars.count(ve->name)) {
                    if (sigil == '$' && !ve->declType.empty() && std::isupper((unsigned char)ve->declType[0]))
                        tctx_.cur->varDefault[ve->name] = Value::typeObj(ve->declType); // `$x = Nil` resets to (Type)
                    tctx_.cur->define(ve->name, typedDefault(ve->declType, sigil));
                }
                return tctx_.cur->vars[ve->name];
            }
            if (ve->name.size() > 2 && (ve->name[1] == '.' || ve->name[1] == '!')) {
                Value* selfp = tctx_.cur->find("self");
                if (!selfp)
                    throwTyped("X::Syntax::NoSelf", {{"variable", ve->name}},
                               "Variable " + ve->name +
                               " used where no 'self' is available");
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
                    if (*it) if (Value* dp = (*it)->find(ve->name)) return *dp;
            }
            Value* p = tctx_.cur->find(ve->name);
            if (p) {
                if (p->t == VT::Hash && p->hashKind == "Proxy" && p->hash) {
                    auto it = p->hash->find("FETCH");
                    if (it != p->hash->end()) return callCallable(it->second, { *p });
                }
                return *p;
            }
            if (ve->name == "$!") return Value::nil(); // no error yet: $! is Nil
            // `&`-references that aren't user subs: built-in operators (&infix:<+>,
            // &prefix:<->) and named builtins (&say) → a Callable that applies them.
            if (ve->name.size() > 1 && ve->name[0] == '&') {
                std::string bare = ve->name.substr(1);
                if (bare.rfind("infix:<", 0) == 0 && bare.back() == '>') {
                    std::string op = bare.substr(7, bare.size() - 8);
                    if (op.size() > 2 && op.front() == '<' && op.back() == '>')
                        op = op.substr(1, op.size() - 2); // &infix:<<∈>> — double-angle form
                    op = normHyperMarkers(op); // &infix:<»+«> — hyper spelling
                    Value code; code.t = VT::Code; code.code = std::make_shared<Callable>(); code.code->name = bare;
                    code.code->whateverArity = 2; // an infix takes two operands (so sort treats it as a comparator)
                    code.code->builtin = [op](Interpreter& I, ValueList& a) -> Value {
                        if (isSetOpStr(op) && a.size() < 2 && !isSetPredicateStr(op)) {
                            if (a.empty()) return setWrap({}, setOpMinTier(op));
                            return setCoerceOne(op, a[0]); // one arg coerces
                        }
                        if (a.size() >= 2) { // n-ary: left-fold like the reduce metaop
                            if (op == "(^)" || op == "\xE2\x8A\x96") return setSymDiffN(a); // ⊖ is variadic
                            Value acc = a[0];
                            for (size_t k = 1; k < a.size(); k++) acc = I.applyBinOp(op, acc, a[k]);
                            return acc;
                        }
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
                // &prefix:<-«> / &postfix:<»i> — hyper op spellings as Callables
                if (bare.rfind("prefix:<", 0) == 0 && bare.back() == '>') {
                    std::string op = normHyperMarkers(bare.substr(8, bare.size() - 9));
                    if (op.size() > 2 && op.compare(op.size() - 2, 2, "<<") == 0) {
                        std::string hop = op.substr(0, op.size() - 2);
                        Value code; code.t = VT::Code; code.code = std::make_shared<Callable>(); code.code->name = bare;
                        code.code->builtin = [hop](Interpreter& I, ValueList& a) -> Value {
                            return a.empty() ? Value::any() : I.hyperUnary(hop, a[0]);
                        };
                        return code;
                    }
                }
                if (bare.rfind("postfix:<", 0) == 0 && bare.back() == '>') {
                    std::string op = normHyperMarkers(bare.substr(9, bare.size() - 10));
                    if (op.size() > 2 && op.compare(0, 2, ">>") == 0) {
                        std::string hop = op.substr(2);
                        Value code; code.t = VT::Code; code.code = std::make_shared<Callable>(); code.code->name = bare;
                        code.code->builtin = [hop](Interpreter& I, ValueList& a) -> Value {
                            return a.empty() ? Value::any() : I.hyperPostfixApply(hop, a[0]);
                        };
                        return code;
                    }
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
            // @_ in a block that was never CALLED (a for-loop body, a given block):
            // it is the implicit *@_ slurpy of the block, so synthesize it from the
            // topic — the one "argument" the loop passed. List-flavored topics
            // (e.g. X/Z tuples) spread: `for @a X @b { f(|@_) }` sees (a, b).
            if (ve->name == "@_") {
                Value out = Value::array();
                if (Value* topic = tctx_.cur->find("$_")) {
                    if (topic->t == VT::Array && topic->isList && topic->arr) *out.arr = *topic->arr;
                    else if (topic->t != VT::Any && topic->t != VT::Nil) out.arr->push_back(*topic);
                }
                return out;
            }
            // static pseudo-package-qualified form: $GLOBAL::x / $OUR::x / $MY::x
            if (ve->name.size() > 1) {
                std::string head = ve->name.substr(1);
                std::string rest;
                if      (head.rfind("GLOBAL::", 0) == 0) rest = head.substr(8);
                else if (head.rfind("OUR::",    0) == 0) rest = tctx_.pkgPrefix + head.substr(5);
                else if (head.rfind("MY::",     0) == 0) rest = head.substr(4);
                if (!rest.empty()) {
                    VarExpr tmp(ve->name.substr(0, 1) + rest); tmp.line = e->line;
                    return eval(&tmp);
                }
            }
            if (!isSpecialVar(ve->name) && !noStrict_)
                throwTyped("X::Undeclared", {{"symbol", ve->name}},
                           "Variable '" + ve->name + "' is not declared");
            return defaultFor(sigil);
        }
        case NK::SymbolicRef: {
            auto* sr = static_cast<SymbolicRef*>(e);
            std::string nm = symRefName(sr);
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
            if (!p)
                throwTyped("X::Syntax::Self::WithoutObject", {},
                           "'self' used where no object is available");
            return *p;
        }
        case NK::NameTerm: {
            if (static_cast<NameTerm*>(e)->name == "Empty" && !classes_.count("Empty")) { // the Empty term: an empty Slip (a user `class Empty` shadows it)
                Value es = Value::array(); es.isList = true; es.s = "Slip"; return es;
            }
            auto* nt = static_cast<NameTerm*>(e);
            const std::string& n = nt->name;
            if (!nt->ofType.empty()) { // parameterized type: Array[Int], Hash[Int,Str]
                Value ty = Value::typeObj(n); ty.ofType = nt->ofType; return ty;
            }
            if (n == "next" || n == "last" || n == "redo") {
                if (tctx_.curLoopFrame != 0 && tctx_.frameTop == tctx_.curLoopFrame) {
                    tctx_.loopCtl = n == "next" ? 1 : n == "last" ? 2 : 3; // cooperative
                    return Value::any();
                }
                if (n == "next") throw NextEx{};
                if (n == "last") throw LastEx{};
                throw RedoEx{};
            }
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
            if (n.rfind("SIG", 0) == 0 || n.rfind("Signal::SIG", 0) == 0) {
                std::string bare = n.rfind("Signal::", 0) == 0 ? n.substr(8) : n;
                int num = signalNumberOfName(bare);
                if (num > 0) { Value v = Value::enumVal(bare, num); v.enumType = "Signal"; return v; }
            }
            // enum Endian <NativeEndian LittleEndian BigEndian> (byte order of Blob reads/writes)
            if (n == "Endian::NativeEndian" || n == "NativeEndian" ||
                n == "Endian::LittleEndian" || n == "LittleEndian" ||
                n == "Endian::BigEndian"    || n == "BigEndian") {
                std::string key = n.rfind("Endian::", 0) == 0 ? n.substr(8) : n;
                Value ev = Value::enumVal(key, key == "NativeEndian" ? 0 : key == "LittleEndian" ? 1 : 2);
                ev.enumType = "Endian"; return ev;
            }
            if (n == "pi" || n == "π") return Value::number(M_PI);
            if (n == "e") return Value::number(M_E);
            if (n == "i") return Value::complex(0, 1); // imaginary unit
            if (n == "tau" || n == "τ") return Value::number(2 * M_PI);
            if (n == "now") { // Instant: high-resolution seconds since the epoch
                auto d = std::chrono::system_clock::now().time_since_epoch();
                Value v = Value::number(std::chrono::duration<double>(d).count());
                v.hashKind = "Instant";
                return v;
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
            // package-relative short name: bare `Path` answers `URI::Path` when no
            // class/var/builtin claims it (see classAliases_)
            return Value::typeObj(resolveClassAlias(n));
        }
        case NK::ListExpr: {
            auto* l = static_cast<ListExpr*>(e);
            ValueList items;
            for (auto& it : l->items) {
                // a |slip in a list literal splices its elements: Set, |@more, Bag
                if (it->kind == NK::Unary && static_cast<Unary*>(it.get())->op == "|") {
                    Value v = eval(static_cast<Unary*>(it.get())->operand.get());
                    if (v.t == VT::Array || v.t == VT::Range) { for (auto& x : v.flatten()) items.push_back(x); continue; }
                    // |%hash (or a Hash-valued expr like `|$<authority>.ast`)
                    // slips its PAIRS — Cro builds `%parts = scheme => …, |$<hier-part>.ast`
                    if (v.t == VT::Hash && v.hash &&
                        (v.hashKind.empty() || v.hashKind == "Map")) {
                        for (auto& kv : *v.hash) {
                            Value p = Value::pair(kv.first, kv.second);
                            p.pairKey = kv.second.pairKey;
                            items.push_back(std::move(p));
                        }
                        continue;
                    }
                    items.push_back(v); continue;
                }
                {
                    Value v = evalValueOf(it.get()); // a bare /pat/ list element stays a Regex object
                    // a $-scalar element is ITEMIZED: `%h = ($hashitem,)` must not
                    // merge the hash's pairs (it is one item, not a pair source)
                    if (v.t == VT::Hash && it->kind == NK::VarExpr &&
                        !static_cast<VarExpr*>(it.get())->name.empty() &&
                        static_cast<VarExpr*>(it.get())->name[0] == '$')
                        v.itemized = true;
                    items.push_back(std::move(v));
                }
            }
            Value lout = listToArray(items);
            lout.isList = true; // a comma list — parenned or bare — is a List, .WHAT (List)
            return lout;
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
                // one-arg rule: `[<a b>».Str]` (a SINGLE list-valued item) spreads —
                // even a hyper result; with several members the comma rules apply.
                bool oneArgSpread = l->items.size() == 1 && v.t == VT::Array && v.isList && !v.itemized;
                bool flatten = oneArgSpread ||
                               (!isHyper &&
                               ((it->kind == NK::VarExpr && !static_cast<VarExpr*>(it.get())->name.empty() &&
                                 static_cast<VarExpr*>(it.get())->name[0] == '@') ||
                                (v.t == VT::Array && v.isList && !l->fromCommaList)));
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
                // `{ a => 1, %more }` — a hash operand spreads its pairs into the literal
                // (but NOT a typed/kind hash — Set/Bag/Map keep their identity as a value)
                else if (v.t == VT::Hash && v.hash && v.hashKind.empty()) { for (auto& kv : *v.hash) items.arr->push_back(Value::pair(kv.first, kv.second)); }
                else items.arr->push_back(v);
            }
            return coerceHash(items);
        }
        case NK::Assign: return evalAssign(static_cast<Assign*>(e));
        case NK::Binary: return evalBinary(static_cast<Binary*>(e));
        case NK::ChainExpr: {
            auto* ch = static_cast<ChainExpr*>(e);
            // a Whatever operand curries the whole chain (`60 < * < 70`, and also
            // `0 <= *.all <= $n` where the slot is a `*.method` WhateverCode) — check
            // the AST up front so short-circuiting still guards the normal path
            std::vector<bool> isW; isW.reserve(ch->operands.size());
            bool anyWhatever = false;
            for (auto& o : ch->operands) { bool w = exprHasWhateverLit(o.get()); isW.push_back(w); anyWhatever = anyWhatever || w; }
            // a smartmatch chain with a COMPOSED WhateverCode on the left is a
            // value comparison (`*.abs ~~ Code` is True); only a bare `*`
            // operand keeps the chain currying (`* ~~ /rx/` for grep/first)
            if (anyWhatever && ch->ops.size() == 1 &&
                (ch->ops[0] == "~~" || ch->ops[0] == "!~~") &&
                ch->operands[0]->kind != NK::Whatever &&
                !exprHasWhateverLit(ch->operands[1].get()))
                anyWhatever = false;
            if (anyWhatever) {
                ValueList ops;
                for (auto& o : ch->operands) ops.push_back(eval(o.get()));
                Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
                code.code->isWhateverCode = true;
                long long stars = 0; for (bool w : isW) if (w) stars++;
                code.code->whateverArity = stars;
                std::vector<std::string> cops = ch->ops;
                code.code->builtin = [cops, ops, isW](Interpreter& I, ValueList& a) -> Value {
                    size_t ai = 0;
                    ValueList filled;
                    for (size_t k = 0; k < ops.size(); k++) {
                        if (!isW[k]) { filled.push_back(ops[k]); continue; }
                        Value arg = ai < a.size() ? a[ai++] : Value::any();
                        // a bare `*` fills with the arg; a `*.method` slot is itself a
                        // WhateverCode and must be applied to the arg
                        if (ops[k].t == VT::Code && ops[k].code && ops[k].code->isWhateverCode)
                            filled.push_back(I.callCallable(ops[k], ValueList{arg}));
                        else filled.push_back(arg);
                    }
                    for (size_t k = 0; k < cops.size(); k++)
                        if (!I.applyBinOp(cops[k], filled[k], filled[k + 1]).truthy()) return Value::boolean(false);
                    return Value::boolean(true);
                };
                return code;
            }
            Value prev = eval(ch->operands[0].get());
            for (size_t k = 0; k < ch->ops.size(); k++) {
                Value next = eval(ch->operands[k + 1].get());
                // a .Bridge/.Numeric object compares through its bridged value
                Value pc = prev, nc = next;
                if ((pc.t == VT::Object || nc.t == VT::Object) && isNumOp(ch->ops[k])) {
                    pc = bridgeReal(*this, pc); nc = bridgeReal(*this, nc);
                }
                if (!applyArith(ch->ops[k], pc, nc).truthy()) return Value::boolean(false);
                prev = next;
            }
            return Value::boolean(true);
        }
        case NK::Unary: return evalUnary(static_cast<Unary*>(e));
        case NK::Call: return evalCall(static_cast<Call*>(e));
        case NK::Index: return evalIndex(static_cast<Index*>(e));
        case NK::MethodCall: {
            auto* mc = static_cast<MethodCall*>(e);
            if (mc->bang && !tctx_.cur->find("self")) // private call outside any method body
                throw RakuError{Value::typeObj("X::Method::NotFound"),
                    "Private method call to '" + mc->method + "' outside the defining class"};
            // `/re/.method` operates on the Regex object; only bare /…/ in term
            // position matches $_ (so the invocant must not auto-match here)
            Value inv = (mc->inv && mc->inv->kind == NK::RegexLit)
                ? Value::regex(static_cast<RegexLit*>(mc->inv.get())->pattern)
                : eval(mc->inv.get());
            if (mc->methodExpr) { // indirect ."$name"() / .$var (Callable or name)
                Value mv = eval(mc->methodExpr.get());
                if (mv.t == VT::Code) { // a method object / callable: invoke with the invocant
                    ValueList ca; ca.push_back(inv);
                    for (auto& a : mc->args) ca.push_back(eval(a.get()));
                    if (mc->hyper) { // ».$var maps it
                        Value out = Value::array(); out.isList = true;
                        if (inv.t == VT::Array && inv.arr)
                            for (auto& el : *inv.arr) out.arr->push_back(callCallable(mv, ValueList{el}));
                        return out;
                    }
                    return callCallable(mv, ca);
                }
                mc->method = mv.toStr(); // resolved here so write- routing below sees it
            }
            // $/.make(v) attaches the ast to the MATCH ITSELF (not a copy)
            if (inv.t == VT::Match && !mc->meta && mc->method == "make") {
                ValueList margs = evalArgs(mc->args);
                Value v = margs.empty() ? Value::any() : margs[0];
                if (Value* lv = lvalue(mc->inv.get())) lv->pairVal = std::make_shared<Value>(v);
                // inside an action method $/ is a COPY of the tree node being
                // built — write through to the real node (the active make
                // target) so the parent action's $<child>.made sees it; the
                // span guard keeps $<child>.make(x) off the parent's slot
                if (!tctx_.makeTargets.empty()) {
                    Value* t = tctx_.makeTargets.back();
                    if (t && t->t == VT::Match && t->rFrom == inv.rFrom &&
                        t->rTo == inv.rTo && t->s == inv.s)
                        t->pairVal = std::make_shared<Value>(v);
                }
                return v;
            }
            // Buf.append/.push/.prepend/.unshift/.pop/.shift mutate the byte string
            // through the invocant's container
            if (inv.t == VT::Str && inv.hashKind == "Buf" && !mc->meta &&
                (mc->method == "append" || mc->method == "push" ||
                 mc->method == "prepend" || mc->method == "unshift" ||
                 mc->method == "pop" || mc->method == "shift" ||
                 mc->method == "reallocate")) {
                if (mc->method == "reallocate") { // grow (zero-fill) or truncate in place
                    ValueList wargs = evalArgs(mc->args);
                    size_t n = wargs.empty() ? 0 : (size_t)wargs[0].toInt();
                    if (Value* lv = lvalue(mc->inv.get())) { lv->s.resize(n, '\0'); return *lv; }
                    Value out = inv; out.s.resize(n, '\0'); return out;
                }
                if (mc->method == "pop" || mc->method == "shift") {
                    Value* lv = nullptr; try { lv = lvalue(mc->inv.get()); } catch (RakuError&) {}
                    Value* tgt = lv ? lv : nullptr;
                    std::string& s = tgt ? tgt->s : inv.s;
                    if (s.empty())
                        throw RakuError{Value::typeObj("X::Cannot::Empty"),
                            "Cannot " + mc->method + " from an empty " + inv.hashKind};
                    unsigned char byte;
                    if (mc->method == "pop") { byte = (unsigned char)s.back(); s.pop_back(); }
                    else { byte = (unsigned char)s.front(); s.erase(0, 1); }
                    return Value::integer(byte);
                }
                ValueList wargs = evalArgs(mc->args);
                std::string add;
                std::function<void(const Value&)> collect = [&](const Value& v) {
                    if ((v.t == VT::Array || v.t == VT::Range) && !(v.t == VT::Array && !v.arr)) { for (auto& e : v.flatten()) collect(e); }
                    else if (v.t == VT::Str && (v.hashKind == "Blob" || v.hashKind == "Buf")) add += v.s;
                    else add += (char)(unsigned char)(v.toInt() & 0xFF);
                };
                for (auto& a : wargs) collect(a);
                bool front = mc->method == "prepend" || mc->method == "unshift";
                if (Value* lv = lvalue(mc->inv.get())) {
                    if (front) lv->s = add + lv->s; else lv->s += add;
                    return *lv;
                }
                Value out = inv;
                if (front) out.s = add + out.s; else out.s += add;
                return out;
            }
            // Buf writes mutate the invocant's container in place
            if (inv.t == VT::Str && (inv.hashKind == "Buf" || inv.hashKind == "Blob") &&
                !mc->meta && mc->method.rfind("write-", 0) == 0) {
                ValueList wargs = evalArgs(mc->args);
                Value* lv = nullptr;                       // a temporary invocant
                try { lv = lvalue(mc->inv.get()); }        // (`buf8.new.write-…`) has no
                catch (RakuError&) {}                      // lvalue — mutate the copy below
                if (lv) {
                    if (lv->hashKind == "Blob")
                        throw RakuError{Value::typeObj("X::Buf::RO"), "Cannot write to an immutable Blob"};
                    return bufBitOp(*lv, mc->method, wargs);
                }
                return bufBitOp(inv, mc->method, wargs); // temporary: mutate the copy
            }
            // a write- on the buf8/buf16/… TYPE object autocreates a zero buffer of
            // the needed size; blob types stay immutable
            if (inv.t == VT::Type && !mc->meta && mc->method.rfind("write-", 0) == 0 &&
                (inv.s.rfind("buf", 0) == 0 || inv.s.rfind("blob", 0) == 0 || inv.s.rfind("utf", 0) == 0) &&
                inv.s.size() > 3 && std::isdigit((unsigned char)inv.s.back())) {
                if (inv.s.rfind("buf", 0) != 0)
                    throw RakuError{Value::typeObj("X::Buf::RO"), "Cannot write to an immutable Blob"};
                ValueList wargs = evalArgs(mc->args);
                Value fresh = Value::str(""); fresh.hashKind = "Buf";
                return bufBitOp(fresh, mc->method, wargs);
            }
            // mutators autovivify an undefined container: `my $x; $x.push(1)` → [1],
            // `%h<k>.push(v)` fills the slot. Rakudo: Any.push vivifies an Array.
            if ((inv.t == VT::Any || inv.t == VT::Nil) && !mc->meta && !mc->methodExpr &&
                (mc->method == "push" || mc->method == "append" ||
                 mc->method == "unshift" || mc->method == "prepend")) {
                if (Value* lv = lvalue(mc->inv.get())) {
                    if (lv->t == VT::Any || lv->t == VT::Nil) *lv = Value::array();
                    inv = *lv;
                }
                else inv = Value::array(); // no container: still act on a fresh Array
            }
            // `.DEFINITE` as a literal identifier is a metamodel macro: it always
            // reports concreteness and never a user-declared DEFINITE method. The
            // quoted `."DEFINITE"()` form (methodExpr set) still dispatches normally.
            if (mc->method == "DEFINITE" && !mc->methodExpr && !mc->meta)
                return Value::boolean(isDefined(inv));
            // `.VAR` on a $-variable: a Scalar container record answering
            // .^name (Scalar), .name ($x), .default; other methods hit the value.
            if (mc->method == "VAR" && !mc->methodExpr && !mc->meta &&
                mc->inv->kind == NK::VarExpr) {
                auto* ivar = static_cast<VarExpr*>(mc->inv.get());
                if (ivar->name.size() > 1 && ivar->name[0] == '$' &&
                    (std::isalpha((unsigned char)ivar->name[1]) || ivar->name[1] == '_' ||
                     ivar->name[1] == '*')) { // $*dynamic vars answer .VAR.dynamic
                    Value sc = Value::makeHash(); sc.hashKind = "Scalar";
                    (*sc.hash)["name"] = Value::str(ivar->name);
                    Value dv = Value::any();
                    for (Env* en = tctx_.cur.get(); en; en = en->parent.get()) {
                        auto di = en->varDefault.find(ivar->name);
                        if (di != en->varDefault.end()) { dv = di->second; break; }
                        if (en->vars.count(ivar->name)) break;
                    }
                    (*sc.hash)["default"] = dv;
                    (*sc.hash)["value"] = inv;
                    return sc;
                }
            }
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
                (mc->inv->kind == NK::Index || mc->inv->kind == NK::VarExpr) &&
                (mc->method == "push" || mc->method == "append" ||
                 mc->method == "unshift" || mc->method == "prepend" ||
                 mc->method == "ASSIGN-KEY" || mc->method == "BIND-KEY")) {
                bool hashy = mc->method == "ASSIGN-KEY" || mc->method == "BIND-KEY";
                try {
                    if (Value* slot = lvalue(mc->inv.get())) {
                        if (slot->t == VT::Any || slot->t == VT::Nil || slot->t == VT::Type)
                            *slot = hashy ? Value::makeHash() : Value::array();
                        inv = *slot; // shares the arr/hash shared_ptr; the write goes through
                    }
                } catch (...) {}
            }
            // Whatever-currying: `*.method(...)` yields a WhateverCode. The decision
            // is SYNTACTIC, like Rakudo's: only an invocant expression containing a
            // literal `*` composes — `my $d = * * 2; $d.^name` calls the method on
            // the stored WhateverCode instead. On a BARE `*` even metamethods curry
            // (`.map(*.^name)`); on a composed WhateverCode the macros answer directly.
            static const std::set<std::string> kMetaMacros = {"WHAT", "WHO", "HOW", "WHICH", "VAR", "WHY"};
            if (((inv.t == VT::Whatever &&
                  (mc->meta || !kMetaMacros.count(mc->method))) ||
                 (inv.t == VT::Code && inv.code && inv.code->isWhateverCode &&
                  !mc->meta && !kMetaMacros.count(mc->method))) &&
                exprHasWhateverLit(mc->inv.get())) {
                Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
                code.code->isWhateverCode = true;
                Value self = inv; ValueList margs = args;
                std::string method = mc->meta ? "^" + mc->method : mc->method; // *.^name keeps its meta form
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
                // a hash invocant keeps its keys: %h».abs maps the VALUES
                if (inv.t == VT::Hash && inv.hash && inv.hashKind.empty()) {
                    Value hout = Value::makeHash();
                    for (auto& kv : *inv.hash)
                        (*hout.hash)[kv.first] = methodCall(kv.second, mc->method, args);
                    return hout;
                }
                Value out = Value::array();
                if (inv.t == VT::Array && inv.arr)
                    for (auto& el : *inv.arr) out.arr->push_back(methodCall(el, mc->method, args));
                else
                    for (auto& el : inv.flatten()) out.arr->push_back(methodCall(el, mc->method, args));
                out.isList = true;
                if (mc->mutate) {
                    // `($a, $b)>>.=meth` writes each result back to that element's own
                    // container; a plain `@a>>.=meth` writes the whole list back to @a.
                    if (mc->inv->kind == NK::ListExpr) {
                        auto* le = static_cast<ListExpr*>(mc->inv.get());
                        for (size_t k = 0; k < le->items.size() && k < out.arr->size(); k++)
                            if (Value* elv = lvalue(le->items[k].get())) *elv = (*out.arr)[k];
                    }
                    else if (Value* lv = lvalue(mc->inv.get())) { out.isList = false; *lv = out; }
                }
                return out;
            }
            // Autothread a junction argument: `$s.contains(all("a","b"))` becomes
            // all($s.contains("a"), $s.contains("b")) — a junction that collapses later.
            // MATCHER positions are exempt: a junction to grep/first is a smartmatch
            // target (`@a.grep(any(@b))` filters, it does not autothread).
            static const std::set<std::string> junctionMatcherMethods = {
                "grep", "first", "classify", "categorize", "index-of", "split", "comb", "match", "subst"};
            if (!mc->meta && !junctionMatcherMethods.count(mc->method))
                for (size_t ai = 0; ai < args.size(); ai++) {
                if (isJunction(args[ai])) {
                    Value jr = Value::array(); jr.enumName = args[ai].enumName; jr.isList = true;
                    for (auto& e : *args[ai].arr) {
                        ValueList a2 = args; a2[ai] = e;
                        jr.arr->push_back(methodCall(inv, mc->method, a2));
                    }
                    return jr;
                }
            }
            // `.?meth` — Nil when the invocant has no such method. There is no
            // unified can() over the builtin surface, so dispatch and convert
            // only the NotFound raised for THIS method on THIS invocant; a
            // NotFound thrown deeper inside a real method still propagates.
            if (mc->maybe) {
                try {
                    Value res = methodCall(inv, mc->meta ? "^" + mc->method : mc->method, args, &mc->args);
                    if (mc->mutate) { if (Value* lv = lvalue(mc->inv.get())) *lv = res; }
                    return res;
                }
                catch (RakuError& err) {
                    const Value& p = err.payload;
                    bool notFound = (p.t == VT::Type && p.s == "X::Method::NotFound") ||
                                    (p.t == VT::Object && p.obj && p.obj->cls &&
                                     p.obj->cls->name == "X::Method::NotFound");
                    if (notFound) {
                        std::string mname, tname;
                        if (p.t == VT::Object && p.obj) {
                            auto mit = p.obj->attrs.find("method");
                            auto tit = p.obj->attrs.find("typename");
                            if (mit != p.obj->attrs.end()) mname = mit->second.toStr();
                            if (tit != p.obj->attrs.end()) tname = tit->second.toStr();
                        }
                        if ((mname.empty() || mname == mc->method) &&
                            (tname.empty() || tname == inv.typeName()))
                            return Value::nil();
                    }
                    throw;
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
        case NK::NqpOp: return evalNqpOp(static_cast<NqpOp*>(e));
        case NK::Range: {
            auto* r = static_cast<RangeExpr*>(e);
            Value from = eval(r->from.get());
            Value to = eval(r->to.get());
            // a WhateverCODE endpoint curries the whole range: `1..*-1` is a
            // WhateverCode (bare `1..*` stays an infinite Range)
            {
                auto isWC = [](const Value& v) {
                    return v.t == VT::Code && v.code && v.code->isWhateverCode;
                };
                if (isWC(from) || isWC(to)) {
                    Value f2 = from, t2 = to;
                    bool exF = r->exFrom, exT = r->exTo;
                    Value code; code.t = VT::Code; code.code = std::make_shared<Callable>();
                    code.code->isWhateverCode = true;
                    long long ar = (isWC(from) ? std::max(1LL, from.code->whateverArity) : 0)
                                 + (isWC(to)   ? std::max(1LL, to.code->whateverArity)   : 0);
                    code.code->whateverArity = ar ? ar : 1;
                    code.code->builtin = [f2, t2, exF, exT, isWC](Interpreter& I, ValueList& as) -> Value {
                        size_t k = 0;
                        auto feed = [&](const Value& side) -> Value {
                            if (!isWC(side)) return side;
                            long long n = std::max(1LL, side.code->whateverArity);
                            ValueList sub;
                            for (long long j = 0; j < n && k < as.size(); j++) sub.push_back(as[k++]);
                            return I.callCallable(side, std::move(sub));
                        };
                        Value lo = feed(f2), hi = feed(t2);
                        Value rr = Value::range(lo.toInt(), hi.toInt(), exF, exT);
                        return rr;
                    };
                    return code;
                }
            }
            // a Date..Date range enumerates days eagerly (numeric Range can't
            // hold Date endpoints; a century is only ~36k elements)
            if (from.t == VT::Hash && from.hashKind == "Date" && from.hash &&
                to.t == VT::Hash && to.hashKind == "Date" && to.hash) {
                auto fld = [](const Value& v, const char* k) -> long long {
                    auto it = v.hash->find(k); return it != v.hash->end() ? it->second.toInt() : 1;
                };
                long long lo = civilToDays(fld(from, "year"), fld(from, "month"), fld(from, "day")) + (r->exFrom ? 1 : 0);
                long long hi = civilToDays(fld(to, "year"), fld(to, "month"), fld(to, "day")) - (r->exTo ? 1 : 0);
                Value arr = Value::array(); arr.isList = true;
                for (long long d = lo; d <= hi; d++) arr.arr->push_back(makeDate(d));
                return arr;
            }
            if (from.t == VT::Str && to.t == VT::Str) {
                // single-CHARACTER endpoints walk CODEPOINTS: chr(0)..chr(0x7F)
                // is the 128 ASCII chars ('<'..'F' includes the symbols between)
                {
                    auto cpLen = [](const std::string& s) -> long long {
                        long long n = 0;
                        for (unsigned char ch : s) if ((ch & 0xC0) != 0x80) n++;
                        return n;
                    };
                    if (cpLen(from.s) == 1 && cpLen(to.s) == 1) {
                        auto firstCp = [](const std::string& s) -> uint32_t {
                            unsigned char c0 = s[0];
                            if (c0 < 0x80) return c0;
                            int len = (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xE ? 3 : 4;
                            uint32_t cp = c0 & (0xFF >> (len + 1));
                            for (int k = 1; k < len && k < (int)s.size(); k++) cp = (cp << 6) | (s[k] & 0x3F);
                            return cp;
                        };
                        // a real Str Range VALUE: codepoints live in rFrom/rTo
                        // (so elems/iteration arithmetic works), the endpoint
                        // STRINGS in s/enumName, ofType tags it (raku/gist,
                        // smartmatch, and flatten() render chars from it)
                        Value rr = Value::range(firstCp(from.s), firstCp(to.s),
                                                r->exFrom, r->exTo);
                        rr.ofType = "Str"; // endpoint text derives from the codepoints
                        return rr;
                    }
                }
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
            // `1..*` / `*..5`: a Whatever endpoint is unbounded (the LLONG extreme
            // marks an infinite range, same as 1..Inf)
            if (from.t == VT::Whatever && to.t == VT::Whatever)
                return Value::range(-9223372036854775807LL - 1, 9223372036854775807LL, false, false);
            if (to.t == VT::Whatever)
                return Value::range(from.toInt(), 9223372036854775807LL, r->exFrom, false);
            if (from.t == VT::Whatever)
                return Value::range(-9223372036854775807LL - 1, to.toInt(), false, r->exTo);
            // Fractional numeric range: at least one endpoint is a non-integer
            // Num/Rat. Keep the real endpoints (elements step by 1 from `from`).
            {
                bool fFrac = (from.t == VT::Num || from.t == VT::Rat) &&
                             from.toNum() != std::floor(from.toNum());
                bool tFrac = (to.t == VT::Num || to.t == VT::Rat) &&
                             to.toNum() != std::floor(to.toNum());
                if ((fFrac || tFrac) && from.isNumeric() && to.isNumeric() &&
                    std::isfinite(from.toNum()) && std::isfinite(to.toNum())) {
                    Value rr = Value::range((long long)std::floor(from.toNum()),
                                            (long long)std::floor(to.toNum()), r->exFrom, r->exTo);
                    rr.rNum = true; rr.n = from.toNum(); rr.im = to.toNum();
                    return rr;
                }
            }
            {
                Value rr = Value::range(from.toInt(), to.toInt(), r->exFrom, r->exTo);
                if (to.t == VT::Int && to.big) rr.big = to.big; // keep the big bound (pick/roll sample it)
                return rr;
            }
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
                    kv.t == VT::Match || kv.t == VT::Code ||
                    kv.t == VT::Range) pr.pairKey = std::make_shared<Value>(kv);
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
