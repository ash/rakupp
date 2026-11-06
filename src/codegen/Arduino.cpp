// The Arduino backend (ARDUINO-PLAN.md, phase A0 with A1's constants): a
// Raku program to one Arduino sketch. Unlike Codegen.cpp, nothing here may
// fall back on a runtime — a board has none — so every construct either
// becomes plain C++ or is refused, with its line, before anything is built.
//
// The subset, and what each piece becomes:
//   - native scalars (`int`, `int8`…`uint64`, `num`, `num32`) and `Bool`:
//     C++ scalars of that width. Integer arithmetic is done in 64 bits with
//     Raku's native wrap-around and stored by truncation, as the interpreter
//     stores into a native of that size; `div` and `%` floor, as Raku's do.
//   - `constant`s: the values the compile-time pass computed while building
//     (CompileTime.h). A scalar is folded into each use; a list of numbers
//     is a `const` table, in flash (PROGMEM) on AVR.
//   - shaped native arrays (`my uint8 @buf[16]`): C arrays, bounds-checked.
//   - subs with native signatures: C++ functions; `--> T` gives the result.
//   - say / print / note of literals, integers and Bools: Serial writes.
//   - the `Arduino` module's routines and constants: the core's own names.
// A runtime error (a zero divisor, an index out of range, `die`) prints its
// message and line on the serial port and halts the board.
#include "Arduino.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace rakupp {
namespace {

// A native C type of the subset. None: not one (an untyped or boxed type).
enum class CT : unsigned char { None, I8, I16, I32, I64, U8, U16, U32, U64, F32, F64, Bool };

CT ctOf(const std::string& t) {
    if (t == "int" || t == "int64") return CT::I64;
    if (t == "int32") return CT::I32;
    if (t == "int16") return CT::I16;
    if (t == "int8") return CT::I8;
    if (t == "uint" || t == "uint64") return CT::U64;
    if (t == "uint32") return CT::U32;
    if (t == "uint16") return CT::U16;
    if (t == "uint8" || t == "byte") return CT::U8;
    if (t == "num" || t == "num64") return CT::F64;
    if (t == "num32") return CT::F32;
    if (t == "Bool") return CT::Bool;
    return CT::None;
}
const char* cType(CT t) {
    switch (t) {
        case CT::I8: return "int8_t";     case CT::I16: return "int16_t";
        case CT::I32: return "int32_t";   case CT::I64: return "int64_t";
        case CT::U8: return "uint8_t";    case CT::U16: return "uint16_t";
        case CT::U32: return "uint32_t";  case CT::U64: return "uint64_t";
        case CT::F32: return "float";     case CT::F64: return "double";
        case CT::Bool: return "bool";     default: return "void";
    }
}
bool isIntT(CT t) { return t >= CT::I8 && t <= CT::U64; }
bool isNumT(CT t) { return t == CT::F32 || t == CT::F64; }

// What an emitted expression is, in C terms: an int64_t value, a double, a
// bool, or a string literal (which only a print can take).
enum class K : unsigned char { Int, Num, Bool, Str };
struct Ex {
    std::string c;
    K k = K::Int;
    bool pure = false;   // no side effects: safe to write twice (a chained comparison)
};

struct Var {
    CT t = CT::None;
    std::string c;
    bool array = false;
    long long n = 0;     // an array's length
    bool ro = false;     // a parameter or a loop variable: read-only, as in Raku
};
struct Const {
    K k = K::Int;        // a scalar's kind
    bool table = false;
    long long i = 0;
    double d = 0;
    bool b = false;
    std::string s;
    CT et = CT::None;    // a table's element type
    long long n = 0;
    std::string c;       // a table's C name
};
struct SubSig {
    std::vector<CT> pt;
    CT rt = CT::None;
    std::string c;
};

// The core's routines `use Arduino` names: argument C types, result kind.
struct Api { const char* name; std::vector<const char*> args; bool result; };
const std::vector<Api>& apis() {
    static const std::vector<Api> a = {
        {"pinMode", {"uint8_t", "uint8_t"}, false},
        {"digitalWrite", {"uint8_t", "uint8_t"}, false},
        {"digitalRead", {"uint8_t"}, true},
        {"analogRead", {"uint8_t"}, true},
        {"analogWrite", {"uint8_t", "int"}, false},
        {"delay", {"unsigned long"}, false},
        {"delayMicroseconds", {"unsigned int"}, false},
        {"millis", {}, true},
        {"micros", {}, true},
    };
    return a;
}
const Api* apiOf(const std::string& n) {
    for (auto& a : apis()) if (n == a.name) return &a;
    return nullptr;
}
bool isBoardConst(const std::string& n) {
    static const std::set<std::string> s = {"HIGH", "LOW", "INPUT", "OUTPUT", "INPUT_PULLUP", "LED_BUILTIN",
                                            "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7"};
    return s.count(n) > 0;
}
// wl_status_t of the WiFiS3 library, which `use Arduino::WiFi` names
bool isWiFiConst(const std::string& n) {
    static const std::set<std::string> s = {"WL_NO_MODULE", "WL_IDLE_STATUS", "WL_NO_SSID_AVAIL", "WL_CONNECTED",
                                            "WL_CONNECT_FAILED", "WL_CONNECTION_LOST", "WL_DISCONNECTED"};
    return s.count(n) > 0;
}
bool isNamed(const ExprPtr& e, const char* name) {
    return e && e->kind == NK::NameTerm && static_cast<NameTerm*>(e.get())->name == name;
}

// A Raku name as a C identifier: the sigil off, `-` and anything outside
// [A-Za-z0-9_] spelled out, so `blink-count` and `blink_count` stay apart.
std::string mangle(const std::string& name) {
    std::string out;
    size_t i = (!name.empty() && (name[0] == '$' || name[0] == '@' || name[0] == '%' || name[0] == '&')) ? 1 : 0;
    for (; i < name.size(); i++) {
        unsigned char ch = (unsigned char)name[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) out += (char)ch;
        else if (ch == '_') out += "_u";
        else if (ch == '-') out += "_d";
        else { char b[8]; snprintf(b, sizeof b, "_x%02X", ch); out += b; }
    }
    return out;
}

// A C string literal; octal escapes, so a following digit never extends one.
std::string cStr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += (char)ch; }
        else if (ch == '\n') o += "\\n";
        else if (ch == '\t') o += "\\t";
        else if (ch == '\r') o += "\\r";
        else if (ch < 0x20 || ch >= 0x7f) { char b[8]; snprintf(b, sizeof b, "\\%03o", ch); o += b; }
        else o += (char)ch;
    }
    return o + "\"";
}
std::string cInt(long long v) {
    if (v == (-9223372036854775807LL - 1)) return "((int64_t)(-9223372036854775807LL - 1))";
    return "((int64_t)" + std::to_string(v) + "LL)";
}
std::string cNum(double d) {
    if (std::isnan(d)) return "((double)NAN)";
    if (std::isinf(d)) return d > 0 ? "((double)INFINITY)" : "(-(double)INFINITY)";
    char b[40];
    for (int p = 1; p <= 17; p++) {
        snprintf(b, sizeof b, "%.*g", p, d);
        if (strtod(b, nullptr) == d) break;
    }
    std::string s = b;
    if (s.find_first_of(".eE") == std::string::npos) s += ".0";
    return "((double)" + s + ")";
}

// The helpers a sketch may carry, as pieces: each defines the names in
// `names`, and a sketch gets the pieces its own code names, then the ones
// those name, in this order, so each comes after what it calls (runtimeFor
// below) — a sketch that calls none, such as blink, is setup() and loop()
// alone. RK_FN is `static inline` and, for GCC and Clang, `unused`: a piece's
// overloads the program does not call go without a warning for each.
// Written for C++11: the Due core's gcc 4.8 stops there.
// The functions live in a namespace because the Arduino IDE writes a
// prototype for each file-scope function of a .ino above the first one, and
// drops a template's `template<...>` when it does: `static T rk_flash(const
// T* p);` does not compile. Functions in a namespace it leaves alone.
struct Piece { const char* names; bool macro; const char* text; };
const Piece kPieces[] = {
{"RK_FN", true, R"RK(#if defined(__GNUC__)
#define RK_FN static inline __attribute__((unused))
#else
#define RK_FN static inline
#endif
)RK"},
{"RK_ERR", true, R"RK(#ifdef RAKUPP_ARDUINO_SIM
#define RK_ERR rk_sim::err
#else
#define RK_ERR Serial
#endif
)RK"},
{"RK_FLASH", true, R"RK(#if defined(__AVR__)
#define RK_FLASH PROGMEM
#else
#define RK_FLASH
#endif
)RK"},
{"rk_halt", false, R"RK(#ifdef RAKUPP_ARDUINO_SIM
#define rk_halt(code) rk_sim::halt(code)
#else
RK_FN void rk_halt(int) { Serial.flush(); for (;;) delay(1000); }
#endif
)RK"},
{"rk_flash", false, R"RK(#if defined(__AVR__)
template<typename T> RK_FN T rk_flash(const T* p) { T v; memcpy_P(&v, p, sizeof(T)); return v; }
#else
template<typename T> RK_FN T rk_flash(const T* p) { return *p; }
#endif
)RK"},
{"rk_print_int", false, R"RK(template<typename S> RK_FN void rk_print_int(S& s, int64_t v) {
    char b[21]; char* p = b + 20; *p = 0;
    uint64_t u = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
    do { *--p = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) *--p = '-';
    s.print(p);
}
)RK"},
{"rk_die_at", false, R"RK(RK_FN void rk_die_at(int line) {
    RK_ERR.print(F("\n  at line ")); rk_print_int(RK_ERR, line); RK_ERR.print(F("\n"));
    rk_halt(1);
}
)RK"},
{"rk_die_div", false, R"RK(RK_FN void rk_die_div(int line) { RK_ERR.print(F("Attempt to divide by zero")); rk_die_at(line); }
)RK"},
{"rk_oob", false, R"RK(RK_FN void rk_oob(int64_t i, int64_t n, int line) {
    RK_ERR.print(F("Index out of range. Is: ")); rk_print_int(RK_ERR, i);
    RK_ERR.print(F(", should be in 0..")); rk_print_int(RK_ERR, n - 1); rk_die_at(line);
}
)RK"},
{"rk_add", false, R"RK(RK_FN int64_t rk_add(int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); }
)RK"},
{"rk_sub", false, R"RK(RK_FN int64_t rk_sub(int64_t a, int64_t b) { return (int64_t)((uint64_t)a - (uint64_t)b); }
)RK"},
{"rk_mul", false, R"RK(RK_FN int64_t rk_mul(int64_t a, int64_t b) { return (int64_t)((uint64_t)a * (uint64_t)b); }
)RK"},
{"rk_neg", false, R"RK(RK_FN int64_t rk_neg(int64_t a) { return (int64_t)(0 - (uint64_t)a); }
)RK"},
{"rk_abs", false, R"RK(RK_FN int64_t rk_abs(int64_t a) { return a < 0 ? rk_neg(a) : a; }
)RK"},
{"rk_div", false, R"RK(RK_FN int64_t rk_div(int64_t a, int64_t b, int line) {
    if (!b) rk_die_div(line);
    if (b == -1) return rk_neg(a);
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}
)RK"},
{"rk_mod", false, R"RK(RK_FN int64_t rk_mod(int64_t a, int64_t b, int line) {
    if (!b) rk_die_div(line);
    if (b == -1) return 0;
    int64_t m = a % b;
    if (m && ((m < 0) != (b < 0))) m += b;
    return m;
}
)RK"},
{"rk_pow", false, R"RK(RK_FN int64_t rk_pow(int64_t a, int64_t b, int line) {
    if (b < 0) { RK_ERR.print(F("Negative exponent in an integer power")); rk_die_at(line); }
    uint64_t r = 1, x = (uint64_t)a;
    while (b) { if (b & 1) r *= x; x *= x; b >>= 1; }
    return (int64_t)r;
}
)RK"},
{"rk_shl rk_shr", false, R"RK(RK_FN int64_t rk_shr(int64_t a, int64_t n);
RK_FN int64_t rk_shl(int64_t a, int64_t n) { return n < 0 ? rk_shr(a, -n) : n >= 64 ? 0 : (int64_t)((uint64_t)a << n); }
RK_FN int64_t rk_shr(int64_t a, int64_t n) { return n < 0 ? rk_shl(a, -n) : n >= 64 ? (a < 0 ? -1 : 0) : (a >> n); }
)RK"},
{"rk_to_int", false, R"RK(RK_FN int64_t rk_to_int(double d, int line) {
    if (d != d || d - d != 0) { RK_ERR.print(F("Cannot convert NaN or Inf to an integer")); rk_die_at(line); }
    return (int64_t)d;
}
)RK"},
{"rk_min", false, R"RK(template<typename T> RK_FN T rk_min(T a, T b) { return b < a ? b : a; }
)RK"},
{"rk_max", false, R"RK(template<typename T> RK_FN T rk_max(T a, T b) { return a < b ? b : a; }
)RK"},
{"rk_preinc", false, R"RK(template<typename T> RK_FN T rk_preinc(T& x) { x = (T)((uint64_t)(int64_t)x + 1u); return x; }
RK_FN double rk_preinc(double& x) { return x += 1; }
RK_FN float rk_preinc(float& x) { return x += 1; }
)RK"},
{"rk_predec", false, R"RK(template<typename T> RK_FN T rk_predec(T& x) { x = (T)((uint64_t)(int64_t)x - 1u); return x; }
RK_FN double rk_predec(double& x) { return x -= 1; }
RK_FN float rk_predec(float& x) { return x -= 1; }
)RK"},
{"rk_postinc", false, R"RK(template<typename T> RK_FN T rk_postinc(T& x) { T o = x; rk_preinc(x); return o; }
RK_FN double rk_postinc(double& x) { double o = x; x += 1; return o; }
RK_FN float rk_postinc(float& x) { float o = x; x += 1; return o; }
)RK"},
{"rk_postdec", false, R"RK(template<typename T> RK_FN T rk_postdec(T& x) { T o = x; rk_predec(x); return o; }
RK_FN double rk_postdec(double& x) { double o = x; x -= 1; return o; }
RK_FN float rk_postdec(float& x) { float o = x; x -= 1; return o; }
)RK"},
{"rk_at", false, R"RK(template<typename T, size_t N> RK_FN T& rk_at(T (&a)[N], int64_t i, int line) {
    if (i < 0 || i >= (int64_t)N) rk_oob(i, (int64_t)N, line);
    return a[i];
}
)RK"},
{"rk_tab", false, R"RK(template<typename T, size_t N> RK_FN T rk_tab(const T (&a)[N], int64_t i, int line) {
    if (i < 0 || i >= (int64_t)N) rk_oob(i, (int64_t)N, line);
    return rk_flash(&a[i]);
}
)RK"},
};

// The runtime for a sketch whose code is `code`: the pieces it names, and
// the pieces those name, until none is new. Empty when it names none.
std::string runtimeFor(const std::string& code) {
    const size_t n = sizeof kPieces / sizeof kPieces[0];
    std::map<std::string, size_t> owner;
    for (size_t i = 0; i < n; i++) {
        std::istringstream names(kPieces[i].names);
        for (std::string w; names >> w;) owner[w] = i;
    }
    std::vector<bool> need(n);
    std::vector<size_t> todo;
    auto scan = [&](const char* s) {
        for (const char* p = s; *p;) {
            if (!isalnum((unsigned char)*p) && *p != '_') { p++; continue; }
            const char* b = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            auto f = owner.find(std::string(b, p));   // a number such as 500LL is no piece's name
            if (f != owner.end() && !need[f->second]) { need[f->second] = true; todo.push_back(f->second); }
        }
    };
    scan(code.c_str());
    while (!todo.empty()) { size_t i = todo.back(); todo.pop_back(); scan(kPieces[i].text); }
    std::string macros, fns;
    for (size_t i = 0; i < n; i++) if (need[i]) (kPieces[i].macro ? macros : fns) += kPieces[i].text;
    if (macros.empty() && fns.empty()) return "";
    std::string rt = "// ---- the Raku++ board runtime: the helpers this program calls, no allocation, no exceptions ----\n" + macros;
    if (!fns.empty()) rt += "namespace rk_rt {\n" + fns + "} // namespace rk_rt\nusing namespace rk_rt;\n";
    return rt + "// ---- the program ----\n";
}

struct LoopCtx { bool arduino; };   // arduino: the top-level loop, whose body is loop()

class Emitter {
public:
    Emitter(Program& p, const ArduinoOptions& o) : prog_(p), opt_(o) {}
    std::string run();

private:
    Program& prog_;
    const ArduinoOptions& opt_;
    std::ostringstream globals_, protos_, funcs_, begin_, setup_, loop_;
    std::vector<std::map<std::string, Var>> scopes_;
    std::map<std::string, Const> consts_;
    std::map<std::string, SubSig> subs_;
    std::set<std::string> cnames_;          // C names taken at file scope
    std::vector<LoopCtx> loops_;
    bool useArduino_ = false, prints_ = false, serialBegun_ = false;
    bool useMatrix_ = false, matrixUsed_ = false;   // `use Arduino::LEDMatrix`, and a call that needs the library
    bool useWiFi_ = false, wifiUsed_ = false;       // `use Arduino::WiFi`, and a name that needs the library
    bool inSub_ = false;
    CT subRt_ = CT::None;
    std::string subName_;
    int tmp_ = 0;
    int curLine_ = 0;    // the statement being emitted: the line for a node that carries none

    [[noreturn]] void refuse(const Node* n, const std::string& what) {
        CodegenError e;
        e.line = n && n->line ? n->line : curLine_;
        e.msg = "line " + std::to_string(e.line) + ": " + what;
        throw e;
    }
    static std::string ind(int n) { return std::string(n * 4, ' '); }
    std::string lineOf(const Node* n) const { return std::to_string(n && n->line ? n->line : curLine_); }
    std::string uniqueGlobal(const std::string& base) {
        std::string c = base;
        for (int k = 2; cnames_.count(c); k++) c = base + "_" + std::to_string(k);
        cnames_.insert(c);
        return c;
    }
    std::string uniqueLocal(const std::string& base) {
        std::string c = base;
        for (int k = 2;; k++) {
            bool clash = cnames_.count(c) > 0;
            for (auto& sc : scopes_) for (auto& kv : sc) if (kv.second.c == c) clash = true;
            if (!clash) return c;
            c = base + "_" + std::to_string(k);
        }
    }
    const Var* findVar(const std::string& n) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto f = it->find(n);
            if (f != it->end()) return &f->second;
        }
        return nullptr;
    }
    const Const* findConst(const std::string& n) const {
        auto f = consts_.find(n);
        return f == consts_.end() ? nullptr : &f->second;
    }

    // ---- conversions ------------------------------------------------------
    std::string asInt(const Ex& e, const Node* n) {
        if (e.k == K::Int) return e.c;
        if (e.k == K::Bool) return "((int64_t)(" + e.c + "))";
        if (e.k == K::Num) refuse(n, "a num where an integer is needed — say which integer with .Int, .floor or .round");
        refuse(n, "a string where a number is needed — the board has strings only as literals to print");
    }
    std::string asNum(const Ex& e, const Node* n) {
        if (e.k == K::Num) return e.c;
        if (e.k == K::Int || e.k == K::Bool) return "((double)(" + e.c + "))";
        refuse(n, "a string where a number is needed — the board has strings only as literals to print");
    }
    std::string asBool(const Ex& e, const Node* n) {
        if (e.k == K::Bool) return e.c;
        if (e.k == K::Int) return "((" + e.c + ") != 0)";
        if (e.k == K::Num) return "((" + e.c + ") != 0.0)";
        refuse(n, "a string used as a condition — the board has strings only as literals to print");
    }
    std::string store(const Ex& e, CT t, const Node* n) {
        if (isIntT(t)) return "(" + std::string(cType(t)) + ")(" + asInt(e, n) + ")";
        if (isNumT(t)) return "(" + std::string(cType(t)) + ")(" + asNum(e, n) + ")";
        if (t == CT::Bool) {
            if (e.k != K::Bool) refuse(n, "a value that is not a Bool stored into a Bool — compare it (`!= 0`) or use `so`");
            return e.c;
        }
        refuse(n, "a store into a variable of no native type");
    }
    Ex readVar(const Var& v) {
        if (isIntT(v.t)) return {"((int64_t)" + v.c + ")", K::Int, true};
        if (isNumT(v.t)) return {"((double)" + v.c + ")", K::Num, true};
        return {v.c, K::Bool, true};
    }
    Ex constEx(const Const& c, const Node* n) {
        if (c.table) refuse(n, "a constant list used as a value — index it, or take .elems");
        if (c.k == K::Int) return {cInt(c.i), K::Int, true};
        if (c.k == K::Num) return {cNum(c.d), K::Num, true};
        if (c.k == K::Bool) return {c.b ? "true" : "false", K::Bool, true};
        return {cStr(c.s), K::Str, true};
    }

    // ---- compile-time values ----------------------------------------------
    // A constant's initializer, as the compile-time pass left it: a literal,
    // a list of literals, or an expression of other constants.
    bool literalNumber(Expr* e, Const& out) {
        if (!e) return false;
        if (e->kind == NK::IntLit) {
            auto* l = static_cast<IntLit*>(e);
            if (!l->big.empty()) return false;
            out.k = K::Int; out.i = l->v; return true;
        }
        if (e->kind == NK::NumLit) {
            auto* l = static_cast<NumLit*>(e);
            if (l->imaginary) return false;
            out.k = K::Num; out.d = l->isRat && l->bigNum.empty() ? (double)l->ratNum / (double)l->ratDen : l->v;
            return true;
        }
        if (e->kind == NK::Unary) {
            auto* u = static_cast<Unary*>(e);
            if (u->op == "-" && !u->postfix && literalNumber(u->operand.get(), out)) {
                if (out.k == K::Int) out.i = (long long)(0 - (unsigned long long)out.i); else out.d = -out.d;
                return true;
            }
        }
        if (e->kind == NK::BoolLit) { out.k = K::Bool; out.b = static_cast<BoolLit*>(e)->v; return true; }
        if (e->kind == NK::NameTerm) {
            const std::string& n = static_cast<NameTerm*>(e)->name;
            if (n == "True" || n == "Bool::True") { out.k = K::Bool; out.b = true; return true; }
            if (n == "False" || n == "Bool::False") { out.k = K::Bool; out.b = false; return true; }
            if (const Const* c = findConst(n)) if (!c->table && c->k != K::Str) { out = *c; return true; }
        }
        if (e->kind == NK::VarExpr) {
            if (const Const* c = findConst(static_cast<VarExpr*>(e)->name)) if (!c->table && c->k != K::Str) { out = *c; return true; }
        }
        return false;
    }
    void declareConst(VarExpr* v, Expr* init, const Node* at) {
        if (inSub_ || !scopes_.empty() && scopes_.size() > 1)
            refuse(at, "a constant inside a block — the build computes the program's top-level constants only");
        Const c;
        const std::string& name = v->name;
        if (!init) refuse(at, "a constant with no value");
        if (init->kind == NK::StrLit) { c.k = K::Str; c.s = static_cast<StrLit*>(init)->v; consts_[name] = c; return; }
        if (init->kind == NK::InterpStr) {
            auto* is = static_cast<InterpStr*>(init);
            std::string s;
            for (auto& p : is->parts) {
                if (!p || p->kind != NK::StrLit) refuse(at, "a constant string the build could not compute");
                s += static_cast<StrLit*>(p.get())->v;
            }
            c.k = K::Str; c.s = s; consts_[name] = c; return;
        }
        if (literalNumber(init, c)) { consts_[name] = c; return; }
        std::vector<Expr*> items;
        if (init->kind == NK::ListExpr) for (auto& it : static_cast<ListExpr*>(init)->items) items.push_back(it.get());
        else if (init->kind == NK::ArrayLit) for (auto& it : static_cast<ArrayLit*>(init)->items) items.push_back(it.get());
        else refuse(at, "the constant " + name + " — its value is not a number, a string or a list of numbers "
                        "once the build has computed it");
        if (items.empty()) refuse(at, "the constant " + name + " is an empty list — a board table needs elements");
        std::vector<Const> vals;
        bool anyNum = false;
        for (Expr* it : items) {
            Const one;
            if (!literalNumber(it, one) || one.k == K::Bool) refuse(at, "the constant " + name + " — a list whose elements are not all numbers");
            anyNum = anyNum || one.k == K::Num;
            vals.push_back(one);
        }
        c.table = true;
        c.n = (long long)vals.size();
        if (anyNum) c.et = CT::F64;
        else {
            long long lo = vals[0].i, hi = vals[0].i;
            for (auto& x : vals) { lo = std::min(lo, x.i); hi = std::max(hi, x.i); }
            c.et = lo >= 0 && hi <= 255 ? CT::U8 : lo >= -128 && hi <= 127 ? CT::I8
                 : lo >= 0 && hi <= 65535 ? CT::U16 : lo >= -32768 && hi <= 32767 ? CT::I16
                 : lo >= 0 && hi <= 4294967295LL ? CT::U32 : lo >= -2147483648LL && hi <= 2147483647LL ? CT::I32 : CT::I64;
        }
        c.c = uniqueGlobal("t_" + mangle(name));
        globals_ << "static const " << cType(c.et) << " " << c.c << "[" << c.n << "] RK_FLASH = {";
        for (size_t i = 0; i < vals.size(); i++) {
            if (i) globals_ << ",";
            globals_ << (i % 12 == 0 ? "\n    " : " ");
            if (anyNum) globals_ << cNum(vals[i].k == K::Num ? vals[i].d : (double)vals[i].i);
            else globals_ << vals[i].i << (c.et == CT::I64 || c.et == CT::U32 ? "LL" : "");
        }
        globals_ << "\n};\n";
        consts_[name] = c;
    }

    // ---- expressions ------------------------------------------------------
    Ex expr(Expr* e) {
        if (!e) refuse(nullptr, "an empty expression");
        switch (e->kind) {
            case NK::IntLit: {
                auto* l = static_cast<IntLit*>(e);
                if (!l->big.empty()) refuse(e, "the integer " + l->big + " — it does not fit in 64 bits");
                return {cInt(l->v), K::Int, true};
            }
            case NK::NumLit: {
                Const c;
                if (!literalNumber(e, c)) refuse(e, "a complex number");
                return {cNum(c.d), K::Num, true};
            }
            case NK::BoolLit: return {static_cast<BoolLit*>(e)->v ? "true" : "false", K::Bool, true};
            case NK::StrLit: return {cStr(static_cast<StrLit*>(e)->v), K::Str, true};
            case NK::InterpStr: {
                auto* is = static_cast<InterpStr*>(e);
                if (is->parts.size() == 1 && is->parts[0]->kind == NK::StrLit) return expr(is->parts[0].get());
                refuse(e, "an interpolated string used as a value — the board can print one, not keep one");
            }
            case NK::NameTerm: return nameTerm(static_cast<NameTerm*>(e));
            case NK::VarExpr: {
                auto* v = static_cast<VarExpr*>(e);
                if (v->declare) refuse(e, "a declaration inside an expression — declare " + v->name + " on its own statement");
                if (const Var* var = findVar(v->name)) {
                    if (var->array) refuse(e, "the array " + v->name + " used as a value — index it, or take .elems");
                    return readVar(*var);
                }
                if (const Const* c = findConst(v->name)) return constEx(*c, e);
                refuse(e, "the variable " + v->name + ", which is not declared with a native type");
            }
            case NK::Binary: return binary(static_cast<Binary*>(e));
            case NK::Unary: return unary(static_cast<Unary*>(e));
            case NK::ChainExpr: {
                auto* ch = static_cast<ChainExpr*>(e);
                std::vector<Ex> ops;
                for (auto& o : ch->operands) ops.push_back(expr(o.get()));
                for (size_t i = 1; i + 1 < ops.size(); i++)
                    if (!ops[i].pure) refuse(e, "a chained comparison whose middle term has side effects");
                std::string c;
                for (size_t i = 0; i < ch->ops.size(); i++) {
                    if (i) c += " && ";
                    c += compare(ch->ops[i], ops[i], ops[i + 1], e);
                }
                return {"(" + c + ")", K::Bool};
            }
            case NK::Ternary: {
                auto* t = static_cast<Ternary*>(e);
                std::string c = asBool(expr(t->cond.get()), t->cond.get());
                Ex a = expr(t->then.get()), b = expr(t->els.get());
                if (a.k == K::Bool && b.k == K::Bool) return {"(" + c + " ? " + a.c + " : " + b.c + ")", K::Bool};
                if (a.k == K::Num || b.k == K::Num) return {"(" + c + " ? " + asNum(a, t) + " : " + asNum(b, t) + ")", K::Num};
                return {"(" + c + " ? " + asInt(a, t) + " : " + asInt(b, t) + ")", K::Int};
            }
            case NK::Assign: return assign(static_cast<Assign*>(e));
            case NK::Index: return index(static_cast<Index*>(e));
            case NK::Call: return call(static_cast<Call*>(e), true);
            case NK::MethodCall: return method(static_cast<MethodCall*>(e), true);
            case NK::ListExpr: {
                auto* l = static_cast<ListExpr*>(e);
                if (l->items.size() == 1) return expr(l->items[0].get());
                refuse(e, "a list — the board has lists only as constants and shaped native arrays");
            }
            case NK::ArrayLit: case NK::HashLit: refuse(e, "an array or hash literal — the board has lists only as constants and shaped native arrays");
            case NK::BlockExpr: refuse(e, "a block or closure used as a value");
            case NK::RegexLit: case NK::SubstLit: refuse(e, "a regex — they run while building, not on the board");
            case NK::Range: refuse(e, "a range used as a value — a range can only drive a `for` loop on the board");
            default: refuse(e, "an expression the board cannot evaluate");
        }
    }

    Ex nameTerm(NameTerm* n) {
        const std::string& s = n->name;
        if (s == "True" || s == "Bool::True") return {"true", K::Bool, true};
        if (s == "False" || s == "Bool::False") return {"false", K::Bool, true};
        if (const Const* c = findConst(s)) return constEx(*c, n);
        if (isBoardConst(s)) {
            if (!useArduino_) refuse(n, s + " is not declared — did you mean to `use Arduino`?");
            return {"((int64_t)" + s + ")", K::Int, true};
        }
        if (s == "pi" || s == "π") return {cNum(3.141592653589793), K::Num, true};
        if (s == "tau" || s == "τ") return {cNum(6.283185307179586), K::Num, true};
        if (s == "e") return {cNum(2.718281828459045), K::Num, true};
        if (isWiFiConst(s)) {
            if (!useWiFi_) refuse(n, s + " is not declared — did you mean to `use Arduino::WiFi`?");
            wifiUsed_ = true;
            return {"((int64_t)" + s + ")", K::Int, true};
        }
        if (s == "Serial" || s == "LEDMatrix" || s == "WiFi") refuse(n, s + " used as a value — call one of its methods");
        refuse(n, "the name " + s + ", which the board does not know");
    }

    std::string compare(const std::string& op, const Ex& a, const Ex& b, const Node* n) {
        static const std::map<std::string, std::string> cops = {
            {"==", "=="}, {"!=", "!="}, {"<", "<"}, {"<=", "<="}, {">", ">"}, {">=", ">="}};
        auto f = cops.find(op);
        if (f == cops.end()) refuse(n, "the comparison `" + op + "` — the board compares numbers (== != < <= > >=)");
        if (a.k == K::Str || b.k == K::Str) refuse(n, "a string comparison — the board has strings only as literals to print");
        if (a.k == K::Num || b.k == K::Num) return "(" + asNum(a, n) + " " + f->second + " " + asNum(b, n) + ")";
        return "(" + asInt(a, n) + " " + f->second + " " + asInt(b, n) + ")";
    }

    Ex arith(const std::string& op, const Ex& a, const Ex& b, const Node* n) {
        const std::string L = lineOf(n);
        bool num = a.k == K::Num || b.k == K::Num;
        if (a.k == K::Str || b.k == K::Str) refuse(n, "arithmetic on a string");
        if (op == "+" || op == "-" || op == "*") {
            if (num) return {"(" + asNum(a, n) + " " + op + " " + asNum(b, n) + ")", K::Num};
            const char* h = op == "+" ? "rk_add" : op == "-" ? "rk_sub" : "rk_mul";
            return {std::string(h) + "(" + asInt(a, n) + ", " + asInt(b, n) + ")", K::Int};
        }
        if (op == "/") {
            if (!num) refuse(n, "`/` of two integers, which is a Rat in Raku and the board has none — use `div`, or make one side a num");
            return {"(" + asNum(a, n) + " / " + asNum(b, n) + ")", K::Num};
        }
        if (op == "**") {
            if (num) return {"pow(" + asNum(a, n) + ", " + asNum(b, n) + ")", K::Num};
            return {"rk_pow(" + asInt(a, n) + ", " + asInt(b, n) + ", " + L + ")", K::Int};
        }
        if (op == "div") return {"rk_div(" + asInt(a, n) + ", " + asInt(b, n) + ", " + L + ")", K::Int};
        if (op == "%" || op == "mod") {
            if (num) refuse(n, "`%` of a num — the board takes `%` of integers");
            return {"rk_mod(" + asInt(a, n) + ", " + asInt(b, n) + ", " + L + ")", K::Int};
        }
        if (op == "%%") return {"(rk_mod(" + asInt(a, n) + ", " + asInt(b, n) + ", " + L + ") == 0)", K::Bool};
        if (op == "+&") return {"(" + asInt(a, n) + " & " + asInt(b, n) + ")", K::Int};
        if (op == "+|") return {"(" + asInt(a, n) + " | " + asInt(b, n) + ")", K::Int};
        if (op == "+^") return {"(" + asInt(a, n) + " ^ " + asInt(b, n) + ")", K::Int};
        if (op == "+<") return {"rk_shl(" + asInt(a, n) + ", " + asInt(b, n) + ")", K::Int};
        if (op == "+>") return {"rk_shr(" + asInt(a, n) + ", " + asInt(b, n) + ")", K::Int};
        if (op == "min" || op == "max") {
            const char* h = op == "min" ? "rk_min" : "rk_max";
            if (num) return {std::string(h) + "<double>(" + asNum(a, n) + ", " + asNum(b, n) + ")", K::Num};
            return {std::string(h) + "<int64_t>(" + asInt(a, n) + ", " + asInt(b, n) + ")", K::Int};
        }
        refuse(n, "the operator `" + op + "`, which the board does not have");
    }

    Ex binary(Binary* b) {
        const std::string& op = b->op;
        if (op == "&&" || op == "||" || op == "and" || op == "or") {
            Ex l = expr(b->lhs.get()), r = expr(b->rhs.get());
            bool isAnd = op == "&&" || op == "and";
            if (l.k == K::Bool && r.k == K::Bool) return {"(" + l.c + (isAnd ? " && " : " || ") + r.c + ")", K::Bool};
            // Raku answers an operand, not a Bool: `$a || 5` is $a when $a is true
            if (l.k == K::Str || r.k == K::Str) refuse(b, "`" + op + "` of a string");
            bool num = l.k == K::Num || r.k == K::Num;
            std::string T = num ? "double" : "int64_t";
            std::string lc = num ? asNum(l, b) : asInt(l, b), rc = num ? asNum(r, b) : asInt(r, b);
            std::string t = "rk_t" + std::to_string(++tmp_);
            return {"([&]() -> " + T + " { " + T + " " + t + " = " + lc + "; return " + t + (isAnd ? " ? " + rc + " : " + t : " ? " + t + " : " + rc) + "; }())",
                    num ? K::Num : K::Int};
        }
        if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=")
            return {compare(op, expr(b->lhs.get()), expr(b->rhs.get()), b), K::Bool};
        if (op == "~" || op == "x" || op == "xx" || op == "eq" || op == "ne" || op == "lt" || op == "gt" || op == "le" || op == "ge")
            refuse(b, "the string operator `" + op + "` — the board has strings only as literals to print");
        if (op == "<=>" || op == "cmp" || op == "leg") refuse(b, "`" + op + "`, which answers an Order the board does not have");
        return arith(op, expr(b->lhs.get()), expr(b->rhs.get()), b);
    }

    Ex unary(Unary* u) {
        const std::string& op = u->op;
        if (op == "++" || op == "--") {
            CT t;
            std::string lv = lvalue(u->operand.get(), t);
            if (t == CT::Bool) refuse(u, "`" + op + "` of a Bool");
            std::string h = std::string(u->postfix ? "rk_post" : "rk_pre") + (op == "++" ? "inc" : "dec");
            return {h + "(" + lv + ")", isNumT(t) ? K::Num : K::Int};
        }
        if (u->postfix) refuse(u, "the postfix operator `" + op + "`");
        Ex a = expr(u->operand.get());
        if (op == "-") {
            if (a.k == K::Num) return {"(-" + a.c + ")", K::Num};
            return {"rk_neg(" + asInt(a, u) + ")", K::Int};
        }
        if (op == "+") { if (a.k == K::Num) return a; return {asInt(a, u), K::Int, a.pure}; }
        if (op == "!" || op == "not") return {"(!" + asBool(a, u) + ")", K::Bool};
        if (op == "?" || op == "so") return {asBool(a, u), K::Bool};
        if (op == "+^") return {"(~" + asInt(a, u) + ")", K::Int};
        if (op == "^") refuse(u, "a range used as a value — a range can only drive a `for` loop on the board");
        if (op == "~") refuse(u, "`~`, which makes a string — the board has strings only as literals to print");
        refuse(u, "the prefix operator `" + op + "`, which the board does not have");
    }

    // An assignable place and its C type: a native scalar, or an element of
    // a shaped native array (a bounds-checked reference).
    std::string lvalue(Expr* e, CT& t) {
        if (e->kind == NK::VarExpr) {
            auto* v = static_cast<VarExpr*>(e);
            if (const Var* var = findVar(v->name)) {
                if (var->array) refuse(e, "an assignment to the whole array " + v->name + " — assign its elements");
                if (var->ro) refuse(e, "an assignment to " + v->name + ", which is read-only (a parameter or a loop variable; `is copy` makes a parameter writable)");
                t = var->t;
                return var->c;
            }
            if (findConst(v->name)) refuse(e, "an assignment to the constant " + v->name);
            refuse(e, "the variable " + v->name + ", which is not declared with a native type");
        }
        if (e->kind == NK::Index) {
            auto* ix = static_cast<Index*>(e);
            if (ix->isHash || ix->multiDim || !ix->adverb.empty() || ix->base->kind != NK::VarExpr)
                refuse(e, "an element of something other than a shaped native array");
            auto* bv = static_cast<VarExpr*>(ix->base.get());
            const Var* var = findVar(bv->name);
            if (!var || !var->array) {
                if (findConst(bv->name)) refuse(e, "an assignment into the constant " + bv->name);
                refuse(e, "an element of " + bv->name + ", which is not a shaped native array");
            }
            t = var->t;
            return "rk_at(" + var->c + ", " + asInt(expr(ix->index.get()), ix) + ", " + lineOf(e) + ")";
        }
        refuse(e, "an assignment to something that is not a native variable or an array element");
    }

    Ex assign(Assign* a) {
        if (a->target->kind == NK::VarExpr && static_cast<VarExpr*>(a->target.get())->declare)
            refuse(a, "a declaration inside an expression — declare it on its own statement");
        CT t;
        std::string lv = lvalue(a->target.get(), t);
        K k = isNumT(t) ? K::Num : t == CT::Bool ? K::Bool : K::Int;
        if (a->op == "=") return {"(" + lv + " = " + store(expr(a->value.get()), t, a) + ")", k};
        if (a->op == ":=") refuse(a, "binding (`:=`) — assign with `=`");
        std::string op = a->op.substr(0, a->op.size() - 1);
        if (op.empty() || op == "||" || op == "&&" || op == "//" || op == "~") refuse(a, "the assignment `" + a->op + "`, which the board does not have");
        Ex rhs = expr(a->value.get());
        if (a->target->kind == NK::VarExpr) {
            Ex cur = readVar(Var{t, lv});
            Ex val = arith(op, cur, rhs, a);
            return {"(" + lv + " = " + store(val, t, a) + ")", k};
        }
        // an element: one evaluation of its subscript, through a reference
        std::string r = "rk_r" + std::to_string(++tmp_);
        Ex cur = readVar(Var{t, r});
        Ex val = arith(op, cur, rhs, a);
        return {"([&]() -> " + std::string(cType(t)) + " { " + cType(t) + "& " + r + " = " + lv + "; " + r + " = " + store(val, t, a) + "; return " + r + "; }())", k};
    }

    Ex index(Index* ix) {
        if (ix->isHash) refuse(ix, "a hash subscript — the board has no hashes");
        if (ix->multiDim || !ix->adverb.empty() || ix->base->kind != NK::VarExpr)
            refuse(ix, "a subscript of something other than a constant list or a shaped native array");
        auto* bv = static_cast<VarExpr*>(ix->base.get());
        std::string L = lineOf(ix);
        if (const Var* var = findVar(bv->name)) {
            if (!var->array) refuse(ix, "a subscript of the scalar " + bv->name);
            Ex i = expr(ix->index.get());
            std::string c = "rk_at(" + var->c + ", " + asInt(i, ix) + ", " + L + ")";
            return isNumT(var->t) ? Ex{"((double)" + c + ")", K::Num} : var->t == CT::Bool ? Ex{c, K::Bool} : Ex{"((int64_t)" + c + ")", K::Int};
        }
        if (const Const* c = findConst(bv->name)) {
            if (!c->table) refuse(ix, "a subscript of the constant " + bv->name + ", which is not a list");
            Ex i = expr(ix->index.get());
            std::string cc = "rk_tab(" + c->c + ", " + asInt(i, ix) + ", " + L + ")";
            return isNumT(c->et) ? Ex{"((double)" + cc + ")", K::Num} : Ex{"((int64_t)" + cc + ")", K::Int};
        }
        refuse(ix, "a subscript of " + bv->name + ", which is neither a constant list nor a shaped native array");
    }

    // ---- calls --------------------------------------------------------------
    std::vector<Expr*> argsOf(std::vector<ExprPtr>& args) {
        std::vector<Expr*> out;
        for (auto& a : args) {
            if (a && a->kind == NK::ListExpr && !static_cast<ListExpr*>(a.get())->parenned)
                for (auto& it : static_cast<ListExpr*>(a.get())->items) out.push_back(it.get());
            else out.push_back(a.get());
        }
        return out;
    }
    // print each argument; a string literal goes out of flash
    void emitPrints(std::ostream& o, int in, const std::string& stream, const std::vector<Expr*>& args, const Node* at) {
        for (Expr* a : args) {
            if (a->kind == NK::InterpStr) {
                std::vector<Expr*> parts;
                for (auto& p : static_cast<InterpStr*>(a)->parts) parts.push_back(p.get());
                emitPrints(o, in, stream, parts, at);
                continue;
            }
            if (a->kind == NK::MethodCall && isNamed(static_cast<MethodCall*>(a)->inv, "WiFi") &&
                static_cast<MethodCall*>(a)->method == "localIP" && argsOf(static_cast<MethodCall*>(a)->args).empty()) {
                o << ind(in) << stream << ".print(" << wifiCall(static_cast<MethodCall*>(a), {}, true).c << ");\n";
                continue;
            }
            Ex e = expr(a);
            if (e.k == K::Str) { if (e.c != "\"\"") o << ind(in) << stream << ".print(F(" << e.c << "));\n"; }
            else if (e.k == K::Int) o << ind(in) << "rk_print_int(" << stream << ", " << e.c << ");\n";
            else if (e.k == K::Bool) o << ind(in) << stream << ".print(" << e.c << " ? F(\"True\") : F(\"False\"));\n";
            else refuse(a, "printing a num — the board cannot write a num the way Raku does yet; print an integer (.round, .Int) instead");
        }
        (void)at;
    }

    // A call. `value`: its result is used, so it must have one.
    Ex call(Call* c, bool value) {
        if (c->callee) refuse(c, "a call through a code value");
        const std::string& n = c->name;
        auto args = argsOf(c->args);
        auto arity = [&](size_t want) {
            if (args.size() != want) refuse(c, n + " takes " + std::to_string(want) + " argument" + (want == 1 ? "" : "s") + ", not " + std::to_string(args.size()));
        };
        auto f = subs_.find(n);
        if (f != subs_.end()) {
            const SubSig& s = f->second;
            arity(s.pt.size());
            if (value && s.rt == CT::None) refuse(c, "the result of " + n + ", which declares none — give it one, as in `sub " + n + "(…) --> int16`");
            std::string a;
            for (size_t i = 0; i < args.size(); i++) a += (i ? ", " : "") + store(expr(args[i]), s.pt[i], args[i]);
            std::string cc = s.c + "(" + a + ")";
            if (s.rt == CT::None) return {cc, K::Int};
            return readVar(Var{s.rt, cc});
        }
        if (const Api* api = apiOf(n)) {
            if (!useArduino_) refuse(c, n + " is not declared — did you mean to `use Arduino`?");
            arity(api->args.size());
            if (value && !api->result) refuse(c, "the result of " + n + ", which answers nothing");
            std::string a;
            for (size_t i = 0; i < args.size(); i++) a += std::string(i ? ", " : "") + "(" + api->args[i] + ")(" + asInt(expr(args[i]), args[i]) + ")";
            if (api->result) return {"((int64_t)" + n + "(" + a + "))", K::Int};
            return {n + "(" + a + ")", K::Int};
        }
        if (n == "abs") { arity(1); Ex a = expr(args[0]); return a.k == K::Num ? Ex{"fabs(" + a.c + ")", K::Num} : Ex{"rk_abs(" + asInt(a, c) + ")", K::Int}; }
        if (n == "sqrt" || n == "sin" || n == "cos" || n == "tan" || n == "exp" || n == "log" || n == "atan") {
            arity(1); return {n + "(" + asNum(expr(args[0]), c) + ")", K::Num};
        }
        if (n == "atan2") { arity(2); return {"atan2(" + asNum(expr(args[0]), c) + ", " + asNum(expr(args[1]), c) + ")", K::Num}; }
        if (n == "floor" || n == "ceiling" || n == "round") { arity(1); return toInt(n, expr(args[0]), c); }
        if (n == "min" || n == "max") { arity(2); return arith(n, expr(args[0]), expr(args[1]), c); }
        if (n == "so" || n == "not") { arity(1); Ex a = expr(args[0]); return {n == "so" ? asBool(a, c) : "(!" + asBool(a, c) + ")", K::Bool}; }
        if (n == "say" || n == "print" || n == "note" || n == "die" || n == "exit")
            refuse(c, n + " used as a value");
        refuse(c, "a call to " + n + ", which is not a sub of this program, a routine of `use Arduino`, or a board builtin"
                  " — a module's routines run while building (in a constant or BEGIN), not on the board");
    }

    Ex toInt(const std::string& how, const Ex& a, const Node* n) {
        if (a.k != K::Num) return {asInt(a, n), K::Int};
        std::string L = lineOf(n);
        if (how == "Int" || how == "truncate") return {"rk_to_int(" + a.c + ", " + L + ")", K::Int};
        if (how == "floor") return {"rk_to_int(floor(" + a.c + "), " + L + ")", K::Int};
        if (how == "ceiling") return {"rk_to_int(ceil(" + a.c + "), " + L + ")", K::Int};
        return {"rk_to_int(floor(" + a.c + " + 0.5), " + L + ")", K::Int};   // round: half up, as Raku's .round
    }

    Ex method(MethodCall* m, bool value) {
        if (m->methodExpr || m->maybe || m->allMode || m->bang || m->mutate || m->hyper || m->meta || !m->methodQual.empty())
            refuse(m, "a method call of a form the board does not have");
        const std::string& n = m->method;
        auto args = argsOf(m->args);
        if (isNamed(m->inv, "WiFi")) return wifiCall(m, args, false);
        if (m->inv && m->inv->kind == NK::NameTerm &&
            (static_cast<NameTerm*>(m->inv.get())->name == "Serial" || static_cast<NameTerm*>(m->inv.get())->name == "LEDMatrix"))
            refuse(m, static_cast<NameTerm*>(m->inv.get())->name + "." + n + " used as a value — it answers nothing");
        if (n == "elems" || n == "end") {
            if (!args.empty() || !m->inv || m->inv->kind != NK::VarExpr) refuse(m, "." + n + " of something other than a constant list or a shaped native array");
            const std::string& vn = static_cast<VarExpr*>(m->inv.get())->name;
            long long len = -1;
            if (const Var* v = findVar(vn)) { if (v->array) len = v->n; }
            else if (const Const* c = findConst(vn)) { if (c->table) len = c->n; }
            if (len < 0) refuse(m, "." + n + " of " + vn + ", which is neither a constant list nor a shaped native array");
            return {cInt(n == "elems" ? len : len - 1), K::Int, true};
        }
        if (!args.empty()) refuse(m, "the method ." + n + " with arguments");
        Ex a = expr(m->inv.get());
        if (n == "Int" || n == "truncate" || n == "floor" || n == "ceiling" || n == "round") return toInt(n, a, m);
        if (n == "Num") return {asNum(a, m), K::Num};
        if (n == "abs") return a.k == K::Num ? Ex{"fabs(" + a.c + ")", K::Num} : Ex{"rk_abs(" + asInt(a, m) + ")", K::Int};
        if (n == "Bool" || n == "so") return {asBool(a, m), K::Bool};
        if (n == "not") return {"(!" + asBool(a, m) + ")", K::Bool};
        if (n == "sqrt" || n == "sin" || n == "cos" || n == "tan" || n == "exp" || n == "log") return {n + "(" + asNum(a, m) + ")", K::Num};
        (void)value;
        refuse(m, "the method ." + n + ", which the board does not have");
    }

    // ---- statements ---------------------------------------------------------
    void block(const std::vector<StmtPtr>& stmts, std::ostream& o, int in) {
        scopes_.emplace_back();
        for (auto& s : stmts) stmt(s.get(), o, in);
        scopes_.pop_back();
    }

    std::string cond(Expr* e) { return asBool(expr(e), e); }

    // `my uint8 $x = 0`, `state int $n`, `my int16 @buf[8]`, `constant LED = 9`
    bool declaration(Expr* e, std::ostream& o, int in, bool topLevel) {
        VarExpr* v = nullptr;
        Expr* init = nullptr;
        if (e->kind == NK::VarExpr && static_cast<VarExpr*>(e)->declare) v = static_cast<VarExpr*>(e);
        else if (e->kind == NK::Assign) {
            auto* a = static_cast<Assign*>(e);
            if (a->target->kind == NK::VarExpr && static_cast<VarExpr*>(a->target.get())->declare) {
                v = static_cast<VarExpr*>(a->target.get());
                if (a->op != "=") refuse(e, "a declaration bound with `" + a->op + "` — initialize it with `=`");
                init = a->value.get();
            }
        }
        if (!v) return false;
        const VarDeclInfo& d = v->d();
        if (d.declScope == "constant") { declareConst(v, init, e); return true; }
        if (d.declScope != "my" && d.declScope != "state") refuse(e, "a `" + d.declScope + "` variable — the board has `my`, `state` and `constant`");
        char sig = v->name.empty() ? '$' : v->name[0];
        if (sig == '%') refuse(e, "the hash " + v->name + " — the board has no hashes");
        if (v->name.size() < 2 || !(nameLetterAt(v->name, 1) || v->name[1] == '_')) refuse(e, "the variable " + v->name + " — a twigil or special variable");
        CT t = ctOf(d.declType);
        if (t == CT::None)
            refuse(e, d.declType.empty()
                ? "the untyped variable " + v->name + " — give it a native type, as in `my int16 " + v->name + "`"
                : "the variable " + v->name + " of type " + d.declType + " — the board has native types (int, int8…uint64, num, num32) and Bool");
        bool global = topLevel && loops_.empty();
        bool isState = d.declScope == "state";
        Var var;
        var.t = t;
        std::string base = (sig == '@' ? "a_" : "v_") + mangle(v->name);
        var.c = global ? uniqueGlobal(base) : uniqueLocal(base);
        if (sig == '@') {
            if (!d.declShape) refuse(e, "the array " + v->name + " has no shape — the board's arrays have a fixed size: `my " + d.declType + " " + v->name + "[16]`");
            Const sz;
            Expr* shape = d.declShape.get();
            if (shape->kind == NK::ListExpr && static_cast<ListExpr*>(shape)->items.size() == 1) shape = static_cast<ListExpr*>(shape)->items[0].get();
            if (!literalNumber(shape, sz) || sz.k != K::Int || sz.i <= 0) refuse(e, "the shape of " + v->name + " — it must be one positive integer known while building");
            if (t == CT::Bool) refuse(e, "an array of Bool — use uint8");
            var.array = true;
            var.n = sz.i;
            std::string decl = std::string(cType(t)) + " " + var.c + "[" + std::to_string(var.n) + "]";
            std::vector<std::string> initVals;
            if (init) {
                std::vector<Expr*> items;
                if (init->kind == NK::ListExpr) for (auto& it : static_cast<ListExpr*>(init)->items) items.push_back(it.get());
                else items.push_back(init);
                if ((long long)items.size() > var.n) refuse(e, "more initial values than " + v->name + " has elements");
                for (Expr* it : items) {
                    Const one;
                    if (!literalNumber(it, one) || one.k == K::Bool) refuse(it, "an array initializer that is not a list of numbers");
                    Ex ex = one.k == K::Int ? Ex{cInt(one.i), K::Int} : Ex{cNum(one.d), K::Num};
                    initVals.push_back(store(ex, t, it));
                }
            }
            std::string initC = "{";
            for (size_t i = 0; i < initVals.size(); i++) initC += (i ? ", " : "") + initVals[i];
            initC += "}";
            if (global) globals_ << "static " << decl << " = " << initC << ";\n";
            else o << ind(in) << (isState ? "static " : "") << decl << " = " << initC << ";\n";
            scopes_.back()[v->name] = var;
            return true;
        }
        if (sig != '$') refuse(e, "the variable " + v->name);
        std::string initC = init ? store(expr(init), t, e) : std::string(isNumT(t) ? "0.0" : t == CT::Bool ? "false" : "0");
        if (global) {
            globals_ << "static " << cType(t) << " " << var.c << ";\n";
            if (init) o << ind(in) << var.c << " = " << initC << ";\n";
        } else {
            o << ind(in) << (isState ? "static " : "") << cType(t) << " " << var.c << " = " << initC << ";\n";
        }
        scopes_.back()[v->name] = var;
        return true;
    }

    void stmt(Stmt* s, std::ostream& o, int in, bool topLevel = false) {
        if (!s) return;
        if (s->line) curLine_ = s->line;
        if (!s->label.empty()) refuse(s, "the label " + s->label + " — the board's loops are unlabelled");
        switch (s->kind) {
            case NK::EmptyStmt: return;
            case NK::ExprStmt: exprStmt(static_cast<ExprStmt*>(s)->e.get(), o, in, topLevel); return;
            case NK::IfStmt: {
                auto* i = static_cast<IfStmt*>(s);
                if (!i->thenVar.empty() || !i->elseVar.empty() || !i->elseParams.empty()) refuse(s, "a pointy `if` (`-> $x`)");
                for (auto& bv : i->branchVars) if (!bv.empty()) refuse(s, "a pointy `elsif` (`-> $x`)");
                for (size_t b = 0; b < i->branches.size(); b++) {
                    std::string c = cond(i->branches[b].first.get());
                    if (b == 0 && i->isUnless) c = "!" + c;
                    o << ind(in) << (b ? "} else if (" : "if (") << c << ") {\n";
                    block(i->branches[b].second->stmts, o, in + 1);
                }
                if (i->elseBlock) { o << ind(in) << "} else {\n"; block(i->elseBlock->stmts, o, in + 1); }
                o << ind(in) << "}\n";
                return;
            }
            case NK::WhileStmt: {
                auto* w = static_cast<WhileStmt*>(s);
                if (!w->var.empty() || !w->params.empty()) refuse(s, "a pointy `while` (`-> $x`)");
                std::string c = cond(w->cond.get());
                o << ind(in) << "while (" << (w->isUntil ? "!" : "") << c << ") {\n";
                loops_.push_back({false});
                block(w->body->stmts, o, in + 1);
                loops_.pop_back();
                o << ind(in) << "}\n";
                return;
            }
            case NK::RepeatStmt: {
                auto* r = static_cast<RepeatStmt*>(s);
                if (!r->var.empty()) refuse(s, "a pointy `repeat` (`-> $x`)");
                o << ind(in) << "do {\n";
                loops_.push_back({false});
                block(r->body->stmts, o, in + 1);
                loops_.pop_back();
                o << ind(in) << "} while (" << (r->isUntil ? "!" : "") << cond(r->cond.get()) << ");\n";
                return;
            }
            case NK::LoopStmt: {
                auto* l = static_cast<LoopStmt*>(s);
                scopes_.emplace_back();
                o << ind(in) << "{\n";
                if (l->init) exprStmt(l->init.get(), o, in + 1, false);
                std::string c = l->cond ? cond(l->cond.get()) : "true";
                std::string inc = l->incr ? expr(l->incr.get()).c : "";
                o << ind(in + 1) << "for (; " << c << "; " << inc << ") {\n";
                loops_.push_back({false});
                block(l->body->stmts, o, in + 2);
                loops_.pop_back();
                o << ind(in + 1) << "}\n" << ind(in) << "}\n";
                scopes_.pop_back();
                return;
            }
            case NK::ForStmt: forStmt(static_cast<ForStmt*>(s), o, in); return;
            case NK::Block: {
                auto* b = static_cast<Block*>(s);
                if (!b->phaser.empty() || b->isCatch) refuse(s, "the " + (b->isCatch ? std::string("CATCH") : b->phaser) + " block — the board has no phasers");
                o << ind(in) << "{\n";
                block(b->stmts, o, in + 1);
                o << ind(in) << "}\n";
                return;
            }
            case NK::ReturnStmt: {
                auto* r = static_cast<ReturnStmt*>(s);
                if (!inSub_) refuse(s, "`return` outside a sub");
                if (subRt_ == CT::None) {
                    if (r->value) refuse(s, "`return` with a value from " + subName_ + ", which declares no result — give it one, as in `--> int16`");
                    o << ind(in) << "return;\n";
                } else {
                    if (!r->value) refuse(s, "`return` with no value from " + subName_ + ", which returns " + cType(subRt_));
                    o << ind(in) << "return " << store(expr(r->value.get()), subRt_, s) << ";\n";
                }
                return;
            }
            case NK::LastStmt: case NK::NextStmt: {
                bool last = s->kind == NK::LastStmt;
                const std::string& target = last ? static_cast<LastStmt*>(s)->target : static_cast<NextStmt*>(s)->target;
                if (!target.empty()) refuse(s, "a labelled `" + std::string(last ? "last" : "next") + "`");
                if (loops_.empty()) refuse(s, std::string(last ? "`last`" : "`next`") + " outside a loop");
                if (loops_.back().arduino) o << ind(in) << (last ? "rk_halt(0);\n" : "return;\n");
                else o << ind(in) << (last ? "break;\n" : "continue;\n");
                return;
            }
            case NK::UseStmt: {
                // Any other module is for the build: a constant or BEGIN may call
                // it, and a call from the board's code is refused where it is made.
                auto* u = static_cast<UseStmt*>(s);
                if (u->isRequire) refuse(s, "`require` — the board loads nothing while it runs");
                if (u->module == "Arduino" && !u->isNo) useArduino_ = true;
                if (u->module == "Arduino::LEDMatrix" && !u->isNo) useMatrix_ = true;
                if (u->module == "Arduino::WiFi" && !u->isNo) useWiFi_ = true;
                return;
            }
            case NK::SubDecl: refuse(s, "a sub declared inside a block — declare it at the top level");
            case NK::ClassDecl: refuse(s, "a class — the board has native scalars, shaped arrays and constants");
            case NK::EnumDecl: refuse(s, "an enum — write its values as constants");
            case NK::GivenStmt: case NK::WhenStmt: refuse(s, "given/when — write it as if/elsif");
            case NK::VarDecl: refuse(s, "a list declaration — declare one variable per statement");
            default: refuse(s, "a statement the board cannot run");
        }
    }

    void exprStmt(Expr* e, std::ostream& o, int in, bool topLevel) {
        if (!e) return;
        if (declaration(e, o, in, topLevel)) return;
        if (e->kind == NK::Call) {
            auto* c = static_cast<Call*>(e);
            const std::string& n = c->name;
            if (!c->callee && !subs_.count(n)) {
                if (n == "say" || n == "print" || n == "note") {
                    auto args = argsOf(c->args);
                    if (args.empty()) refuse(e, n + " with nothing to print");
                    std::string stream = n == "note" ? "RK_ERR" : "Serial";
                    prints_ = true;   // on a board, note's RK_ERR is the serial port too
                    emitPrints(o, in, stream, args, e);
                    if (n != "print") o << ind(in) << stream << ".print(F(\"\\n\"));\n";
                    return;
                }
                if (n == "die") {
                    prints_ = true;
                    auto args = argsOf(c->args);
                    emitPrints(o, in, "RK_ERR", args.empty() ? std::vector<Expr*>{} : args, e);
                    if (args.empty()) o << ind(in) << "RK_ERR.print(F(\"Died\"));\n";
                    o << ind(in) << "rk_die_at(" << lineOf(e) << ");\n";
                    return;
                }
                if (n == "exit") {
                    auto args = argsOf(c->args);
                    if (args.size() > 1) refuse(e, "exit takes one argument");
                    o << ind(in) << "rk_halt((int)(" << (args.empty() ? std::string("0") : asInt(expr(args[0]), e)) << "));\n";
                    return;
                }
            }
            o << ind(in) << call(c, false).c << ";\n";
            return;
        }
        if (e->kind == NK::MethodCall) {
            auto* m = static_cast<MethodCall*>(e);
            if (m->inv && m->inv->kind == NK::NameTerm && static_cast<NameTerm*>(m->inv.get())->name == "Serial") {
                if (!useArduino_) refuse(e, "Serial is not declared — did you mean to `use Arduino`?");
                auto args = argsOf(m->args);
                if (m->method == "begin") {
                    if (args.size() != 1) refuse(e, "Serial.begin takes the baud rate");
                    o << ind(in) << "Serial.begin((unsigned long)(" << asInt(expr(args[0]), e) << "));\n";
                    serialBegun_ = true;
                    return;
                }
                if (m->method == "print" || m->method == "println") {
                    if (args.empty() && m->method == "print") refuse(e, "Serial.print with nothing to print");
                    prints_ = true;
                    emitPrints(o, in, "Serial", args, e);
                    if (m->method == "println") o << ind(in) << "Serial.print(F(\"\\r\\n\"));\n";
                    return;
                }
                refuse(e, "Serial." + m->method + " — the board's Serial has begin, print and println");
            }
            if (m->inv && m->inv->kind == NK::NameTerm && static_cast<NameTerm*>(m->inv.get())->name == "LEDMatrix") {
                matrixCall(m, o, in);
                return;
            }
            o << ind(in) << "(void)" << method(m, false).c << ";\n";
            return;
        }
        Ex x = expr(e);
        o << ind(in) << (e->kind == NK::Assign || e->kind == NK::Unary ? "" : "(void)") << x.c << ";\n";
    }

    // The Uno R4 WiFi's matrix (`use Arduino::LEDMatrix`): one global
    // ArduinoLEDMatrix of the core's Arduino_LED_Matrix library. A frame is
    // copied into a local array of three words, so a table of any element
    // type, or a shaped array the board computes as it runs, can be shown.
    void matrixCall(MethodCall* m, std::ostream& o, int in) {
        if (!useMatrix_) refuse(m, "LEDMatrix is not declared — did you mean to `use Arduino::LEDMatrix`?");
        auto args = argsOf(m->args);
        const std::string& n = m->method;
        matrixUsed_ = true;
        if (n == "begin" && args.empty()) { o << ind(in) << "rk_matrix.begin();\n"; return; }
        if (n == "clear" && args.empty()) { o << ind(in) << "{ uint32_t rk_z[3] = {0, 0, 0}; rk_matrix.loadFrame(rk_z); }\n"; return; }
        if (n == "show" && args.size() == 1 && args[0]->kind == NK::VarExpr) {
            const std::string& vn = static_cast<VarExpr*>(args[0])->name;
            std::string L = lineOf(m), w[3];
            if (const Var* v = findVar(vn); v && v->array && v->n == 3 && isIntT(v->t)) {
                for (int i = 0; i < 3; i++) w[i] = "(uint32_t)rk_at(" + v->c + ", " + std::to_string(i) + ", " + L + ")";
            } else if (const Const* c = findConst(vn); c && c->table && c->n == 3 && isIntT(c->et)) {
                for (int i = 0; i < 3; i++) w[i] = "(uint32_t)rk_tab(" + c->c + ", " + std::to_string(i) + ", " + L + ")";
            } else {
                refuse(m, "LEDMatrix.show of " + vn + " — a frame is a constant list or a shaped integer array of three "
                          "32-bit words; make one with `frame`");
            }
            o << ind(in) << "{ uint32_t rk_f[3] = {" << w[0] << ", " << w[1] << ", " << w[2] << "}; rk_matrix.loadFrame(rk_f); }\n";
            return;
        }
        refuse(m, "LEDMatrix." + n + " — the matrix has begin, show(@frame) and clear");
    }

    // The Uno R4 WiFi's network (`use Arduino::WiFi`): the global `WiFi` of
    // the core's WiFiS3 library. The network's name and password are strings
    // the build knows — literals or constants — as the board has no others;
    // the address it answers can be printed (`printed`), not kept.
    Ex wifiCall(MethodCall* m, const std::vector<Expr*>& args, bool printed) {
        if (!useWiFi_) refuse(m, "WiFi is not declared — did you mean to `use Arduino::WiFi`?");
        const std::string& n = m->method;
        wifiUsed_ = true;
        if (n == "begin" && (args.size() == 1 || args.size() == 2)) {
            std::string a;
            for (Expr* x : args) {
                Ex e = expr(x);
                if (e.k != K::Str)
                    refuse(x, "WiFi.begin of something other than a string the build knows — the network's name and "
                              "password are constants, as in `constant SSID = %*ENV<WIFI_SSID>`");
                a += (a.empty() ? "" : ", ") + e.c;
            }
            return {"((int64_t)WiFi.begin(" + a + "))", K::Int};
        }
        if ((n == "status" || n == "disconnect") && args.empty()) return {"((int64_t)WiFi." + n + "())", K::Int};
        if (n == "localIP" && args.empty()) {
            if (!printed) refuse(m, "WiFi.localIP used as a value — the board can print the address (`say WiFi.localIP`), not keep it");
            return {"WiFi.localIP()", K::Str};   // an IPAddress, which the core's print takes
        }
        refuse(m, "WiFi." + n + " — the board's WiFi has begin(SSID, PASS), status, localIP and disconnect");
    }

    void forStmt(ForStmt* f, std::ostream& o, int in) {
        if (f->destructure || f->rwVars || f->hyper || f->vars.size() > 1 || f->params.size() > 1)
            refuse(f, "a `for` loop of a form the board does not have — one loop variable over a range, a constant list or a shaped array");
        std::string name = !f->vars.empty() ? f->vars[0] : !f->params.empty() ? f->params[0].name : "$_";
        CT vt = CT::I64;
        if (!f->params.empty() && !f->params[0].type.empty() && f->params[0].type != "Int") {
            vt = ctOf(f->params[0].type);
            if (!isIntT(vt) && !isNumT(vt)) refuse(f, "the loop variable's type " + f->params[0].type);
        }
        std::string L = lineOf(f);
        Expr* list = f->list.get();
        std::string from, to;
        bool exTo = false;
        if (list->kind == NK::Unary && static_cast<Unary*>(list)->op == "^") {
            from = cInt(0);
            to = asInt(expr(static_cast<Unary*>(list)->operand.get()), list);
            exTo = true;
        } else if (list->kind == NK::Range) {
            auto* r = static_cast<RangeExpr*>(list);
            from = asInt(expr(r->from.get()), list);
            if (r->exFrom) from = "rk_add(" + from + ", " + cInt(1) + ")";
            to = asInt(expr(r->to.get()), list);
            exTo = r->exTo;
        } else if (list->kind == NK::VarExpr) {
            const std::string& vn = static_cast<VarExpr*>(list)->name;
            const Var* av = findVar(vn);
            const Const* tc = findConst(vn);
            if (!(av && av->array) && !(tc && tc->table)) refuse(f, "a `for` over " + vn + ", which is neither a constant list nor a shaped native array");
            std::string i = "rk_i" + std::to_string(++tmp_);
            long long n = av ? av->n : tc->n;
            CT et = av ? av->t : tc->et;
            o << ind(in) << "for (int64_t " << i << " = 0; " << i << " < " << n << "; " << i << "++) {\n";
            scopes_.emplace_back();
            Var lv;
            lv.t = isNumT(et) ? CT::F64 : et == CT::Bool ? CT::Bool : CT::I64;
            lv.c = uniqueLocal("v_" + mangle(name));
            std::string elem = av ? "rk_at(" + av->c + ", " + i + ", " + L + ")" : "rk_tab(" + tc->c + ", " + i + ", " + L + ")";
            o << ind(in + 1) << "const " << cType(lv.t) << " " << lv.c << " = " << elem << "; (void)" << lv.c << ";\n";
            lv.ro = true;
            scopes_.back()[name] = lv;
            loops_.push_back({false});
            block(f->body->stmts, o, in + 1);
            loops_.pop_back();
            scopes_.pop_back();
            o << ind(in) << "}\n";
            return;
        } else {
            refuse(f, "a `for` over a list the board does not have — loop over a range (^N, A..B), a constant list or a shaped array");
        }
        std::string hi = "rk_hi" + std::to_string(++tmp_);
        o << ind(in) << "{\n" << ind(in + 1) << "const int64_t " << hi << " = " << to << ";\n";
        scopes_.emplace_back();
        Var lv;
        lv.t = vt;
        lv.c = uniqueLocal("v_" + mangle(name));
        o << ind(in + 1) << "for (int64_t " << lv.c << "_i = " << from << "; " << lv.c << "_i " << (exTo ? "<" : "<=") << " " << hi << "; " << lv.c << "_i++) {\n";
        o << ind(in + 2) << "const " << cType(vt) << " " << lv.c << " = (" << cType(vt) << ")" << lv.c << "_i; (void)" << lv.c << ";\n";
        lv.ro = true;
        scopes_.back()[name] = lv;
        loops_.push_back({false});
        block(f->body->stmts, o, in + 2);
        loops_.pop_back();
        scopes_.pop_back();
        o << ind(in + 1) << "}\n" << ind(in) << "}\n";
    }

    // ---- subs ---------------------------------------------------------------
    void signature(SubDecl* d) {
        if (d->isMulti || d->isProto || d->isMethod || d->isSubmethod || d->isPrivate || d->nameExpr || d->immediateCall || d->name.empty())
            refuse(d, "the sub " + d->name + " — the board has plain named subs (no multi, proto or method)");
        if (!d->altParams.empty() || !d->traits.empty() || d->retRw || d->retLiteralPresent)
            refuse(d, "the sub " + d->name + " — a trait or literal result the board does not have");
        SubSig s;
        for (auto& p : d->params) {
            if (p.sigil != '$' || p.named || p.slurpy || p.optional || p.invocant || p.defaultVal || p.litVal || p.whereExpr || p.hadWhere ||
                p.subSig || p.codeSig || p.isRw || p.isRaw || p.coerce || p.typeCapture || !p.userTraits.empty() || p.name.size() < 2)
                refuse(d, "the parameter " + (p.name.empty() ? std::string("(anonymous)") : p.name) + " of " + d->name + " — the board takes positional native scalars");
            CT t = ctOf(p.type);
            if (t == CT::None)
                refuse(d, p.type.empty() ? "the untyped parameter " + p.name + " of " + d->name + " — give it a native type, as in `int16 " + p.name + "`"
                                         : "the parameter " + p.name + " of " + d->name + " has type " + p.type + " — the board takes native types and Bool");
            s.pt.push_back(t);
        }
        if (!d->retType.empty()) {
            s.rt = ctOf(d->retType);
            if (s.rt == CT::None) refuse(d, d->name + " returns " + d->retType + " — the board's subs return a native type or Bool");
        }
        s.c = uniqueGlobal("u_" + mangle(d->name));
        if (subs_.count(d->name)) refuse(d, "a second sub named " + d->name);
        subs_[d->name] = s;
        std::string ps;
        for (size_t i = 0; i < d->params.size(); i++) ps += (i ? ", " : "") + std::string(cType(s.pt[i])) + " v_" + mangle(d->params[i].name);
        protos_ << "static " << cType(s.rt) << " " << s.c << "(" << (ps.empty() ? "void" : ps) << ");\n";
    }

    // The value of the last statement is a sub's result, as in Raku.
    void implicitReturn(Stmt* s, std::ostream& o, int in) {
        if (s->kind == NK::ExprStmt) {
            Expr* e = static_cast<ExprStmt*>(s)->e.get();
            if (e && !(e->kind == NK::Call && (static_cast<Call*>(e)->name == "die" || static_cast<Call*>(e)->name == "exit")) &&
                !(e->kind == NK::VarExpr && static_cast<VarExpr*>(e)->declare) &&
                !(e->kind == NK::Assign && static_cast<Assign*>(e)->target->kind == NK::VarExpr && static_cast<VarExpr*>(static_cast<Assign*>(e)->target.get())->declare)) {
                o << ind(in) << "return " << store(expr(e), subRt_, s) << ";\n";
                return;
            }
        }
        if (s->kind == NK::IfStmt) {
            auto* i = static_cast<IfStmt*>(s);
            if (i->elseBlock && i->thenVar.empty() && i->elseVar.empty()) {
                for (size_t b = 0; b < i->branches.size(); b++) {
                    std::string c = cond(i->branches[b].first.get());
                    if (b == 0 && i->isUnless) c = "!" + c;
                    o << ind(in) << (b ? "} else if (" : "if (") << c << ") {\n";
                    tailBlock(i->branches[b].second->stmts, o, in + 1);
                }
                o << ind(in) << "} else {\n";
                tailBlock(i->elseBlock->stmts, o, in + 1);
                o << ind(in) << "}\n";
                return;
            }
        }
        stmt(s, o, in);
    }
    void tailBlock(const std::vector<StmtPtr>& stmts, std::ostream& o, int in) {
        scopes_.emplace_back();
        for (size_t k = 0; k < stmts.size(); k++) {
            if (k + 1 == stmts.size()) implicitReturn(stmts[k].get(), o, in);
            else stmt(stmts[k].get(), o, in);
        }
        scopes_.pop_back();
    }

    void subBody(SubDecl* d) {
        const SubSig& s = subs_[d->name];
        std::ostringstream body;
        inSub_ = true;
        subRt_ = s.rt;
        subName_ = d->name;
        auto savedLoops = loops_;
        loops_.clear();
        scopes_.emplace_back();
        std::string ps;
        for (size_t i = 0; i < d->params.size(); i++) {
            Var v;
            v.t = s.pt[i];
            v.c = "v_" + mangle(d->params[i].name);
            v.ro = !d->params[i].isCopy;
            scopes_.back()[d->params[i].name] = v;
            ps += (i ? ", " : "") + std::string(cType(v.t)) + " " + v.c;
        }
        if (s.rt == CT::None) block(d->body, body, 1);
        else {
            tailBlock(d->body, body, 1);
            body << "    RK_ERR.print(F(\"" << d->name << " ended without returning a value\"));\n"
                 << "    rk_die_at(" << lineOf(d) << ");\n"
                 << "    return 0;\n";
        }
        scopes_.pop_back();
        loops_ = savedLoops;
        inSub_ = false;
        subRt_ = CT::None;
        funcs_ << "static " << cType(s.rt) << " " << s.c << "(" << (ps.empty() ? "void" : ps) << ") {\n" << body.str() << "}\n\n";
    }

    // the top-level `loop { }`: `loop` parses as `while True`
    static bool isForever(Stmt* s) {
        if (s->kind != NK::WhileStmt) return false;
        auto* w = static_cast<WhileStmt*>(s);
        if (w->isUntil || !w->var.empty() || !w->params.empty() || w->modifier || !w->cond) return false;
        if (w->cond->kind == NK::BoolLit) return static_cast<BoolLit*>(w->cond.get())->v;
        return w->cond->kind == NK::NameTerm && static_cast<NameTerm*>(w->cond.get())->name == "True";
    }

    // What the compile-time pass left of the top-level BEGIN blocks: plain
    // assignments of computed values, which become the variables' first values.
    void beginBlock(Block* b) {
        for (auto& st : b->stmts) {
            if (!st || st->kind != NK::ExprStmt) refuse(b, "a BEGIN block whose effect the build could not compute — the board cannot run it");
            Expr* e = static_cast<ExprStmt*>(st.get())->e.get();
            if (!e || e->kind != NK::Assign || static_cast<Assign*>(e)->op != "=")
                refuse(b, "a BEGIN block whose effect the build could not compute — the board cannot run it");
            auto* a = static_cast<Assign*>(e);
            if (a->target->kind != NK::VarExpr) refuse(b, "a BEGIN block that leaves something other than a variable");
            const std::string& vn = static_cast<VarExpr*>(a->target.get())->name;
            const Var* v = findVar(vn);
            if (!v) refuse(b, "a BEGIN block that sets " + vn + ", which is not a native variable of the program");
            if (v->array) {
                std::vector<Expr*> items;
                if (a->value->kind == NK::ListExpr) for (auto& it : static_cast<ListExpr*>(a->value.get())->items) items.push_back(it.get());
                else if (a->value->kind == NK::ArrayLit) for (auto& it : static_cast<ArrayLit*>(a->value.get())->items) items.push_back(it.get());
                else refuse(b, "a BEGIN value for " + vn + " that is not a list of numbers");
                if ((long long)items.size() > v->n) refuse(b, "a BEGIN value with more elements than " + vn + " has");
                for (size_t i = 0; i < items.size(); i++)
                    begin_ << "    " << v->c << "[" << i << "] = " << store(expr(items[i]), v->t, b) << ";\n";
            } else {
                begin_ << "    " << v->c << " = " << store(expr(a->value.get()), v->t, b) << ";\n";
            }
        }
    }
};

std::string Emitter::run() {
    scopes_.emplace_back();
    // signatures first: a sub may be called before its declaration
    for (auto& s : prog_.stmts)
        if (s && s->kind == NK::SubDecl) signature(static_cast<SubDecl*>(s.get()));
    bool sawLoop = false;
    for (auto& sp : prog_.stmts) {
        Stmt* s = sp.get();
        if (!s) continue;
        if (s->kind == NK::SubDecl) { subBody(static_cast<SubDecl*>(s)); continue; }
        if (s->kind == NK::Block && static_cast<Block*>(s)->phaser == "BEGIN") { beginBlock(static_cast<Block*>(s)); continue; }
        if (s->kind == NK::EmptyStmt) continue;
        if (sawLoop) refuse(s, "a statement after the top-level `loop` — it never runs, because the loop never ends");
        if (isForever(s)) {
            sawLoop = true;
            loops_.push_back({true});
            block(static_cast<WhileStmt*>(s)->body->stmts, loop_, 1);
            loops_.pop_back();
            continue;
        }
        stmt(s, setup_, 1, true);
    }
    std::ostringstream out;
    out << "// Generated by `rakupp --target=arduino` (Raku++ " << opt_.version << ") from " << opt_.srcName << ".\n"
        << "// An Arduino sketch: open it in the Arduino IDE, or build it with arduino-cli.\n"
        << "// `rakupp --target=arduino --sim` builds it for this computer instead, on a simulated board.\n"
        << "#include <Arduino.h>\n#include <stdint.h>\n#include <string.h>\n#include <math.h>\n";
    if (matrixUsed_) out << "#include \"Arduino_LED_Matrix.h\"   // the Uno R4 WiFi's matrix, from its core\n";
    if (wifiUsed_) out << "#include <WiFiS3.h>   // the Uno R4 WiFi's network, from its core\n";
    std::ostringstream body;   // the program; the runtime above it is what it calls
    if (matrixUsed_) body << "\nstatic ArduinoLEDMatrix rk_matrix;\n";
    if (!globals_.str().empty()) body << "\n" << globals_.str();
    if (!protos_.str().empty()) body << "\n" << protos_.str();
    body << "\nvoid setup() {\n";
    if (prints_ && !serialBegun_) body << "    Serial.begin(9600);\n";
    body << begin_.str() << setup_.str() << "}\n\nvoid loop() {\n";
    if (sawLoop) body << loop_.str();
    else body << "    rk_halt(0);   // the program has no top-level loop: it ran once, in setup()\n";
    body << "}\n";
    if (!funcs_.str().empty()) body << "\n" << funcs_.str();
    std::string rt = runtimeFor(body.str());
    if (!rt.empty()) out << "\n" << rt;
    out << body.str();
    return out.str();
}

} // namespace

std::string transpileToArduino(Program& prog, const ArduinoOptions& opt) {
    Emitter em(prog, opt);
    return em.run();
}

} // namespace rakupp
