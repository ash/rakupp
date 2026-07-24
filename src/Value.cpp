#include "Value.h"
#include "Interpreter.h" // RakuError (zero-denominator Rat Str-coercion throws)
#include "Unicode.h"     // uniGeneralCategory (magic-increment window over non-ASCII)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <limits>
#include <sstream>

// codepoint -> UTF-8 (for Str-Range element rendering)
static std::string cpToU8(uint32_t cp) {
    std::string o;
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    return o;
}

namespace rakupp {

// Recursion depth backstop for gist()/toStr() over nested containers. A
// self-referential array/hash (`@a[0] = @a`) would otherwise recurse until it
// exhausts memory; bail with an ellipsis once absurdly deep. (`.raku` has its own
// pointer-based cycle detection in Builtins.cpp; this is the render-side backstop.)
static thread_local int g_reprDepth = 0;
struct ReprDepthGuard {
    ReprDepthGuard() { ++g_reprDepth; }
    ~ReprDepthGuard() { --g_reprDepth; }
    bool tooDeep() const { return g_reprDepth > 512; }
};

static std::string dateGist(const std::map<std::string, Value>& h, bool isDate) {
    auto f = [&](const char* k) { auto it = h.find(k); return it != h.end() ? it->second.toInt() : 0; };
    char buf[48];
    const char* ys = f("year") > 9999 ? "+" : ""; // ISO 8601: years past 9999 carry a leading +
    if (isDate) std::snprintf(buf, sizeof buf, "%s%04lld-%02lld-%02lld", ys, f("year"), f("month"), f("day"));
    else {
        long long tz = f("timezone");
        char suf[12];
        if (tz == 0) std::snprintf(suf, sizeof suf, "Z");
        else std::snprintf(suf, sizeof suf, "%c%02lld:%02lld", tz < 0 ? '-' : '+',
                           (tz < 0 ? -tz : tz) / 3600, ((tz < 0 ? -tz : tz) % 3600) / 60);
        auto sit = h.find("second");
        double sd = sit != h.end() ? sit->second.toNum() : 0.0;
        if (sd != (double)(long long)sd)
            std::snprintf(buf, sizeof buf, "%s%04lld-%02lld-%02lldT%02lld:%02lld:%09.6f%s",
                          ys, f("year"), f("month"), f("day"), f("hour"), f("minute"), sd, suf);
        else
            std::snprintf(buf, sizeof buf, "%s%04lld-%02lld-%02lldT%02lld:%02lld:%02lld%s",
                          ys, f("year"), f("month"), f("day"), f("hour"), f("minute"), f("second"), suf);
    }
    return buf;
}

bool Value::truthy() const {
    switch (t) {
        case VT::Nil:
        case VT::Any:  return false;
        case VT::Bool: return b;
        case VT::Int:  return big ? !big->isZero() : i != 0;
        case VT::Num:  return n != 0.0;
        case VT::Complex: return n != 0.0 || im != 0.0;
        case VT::Rat:  return ratN && !ratN->isZero();
        case VT::Str:  return !s.empty(); // Raku: any non-empty string is true (incl. "0")
        case VT::Array:
            // a Junction collapses by its kind — since comparisons autothread
            // into PRESERVED junctions, every truthiness site must collapse
            if (arr && (enumName == "any" || enumName == "all" ||
                        enumName == "one" || enumName == "none")) {
                int t2 = 0, total = 0;
                for (auto& e : *arr) { total++; if (e.truthy()) t2++; }
                return enumName == "any" ? t2 > 0 : enumName == "all" ? t2 == total
                     : enumName == "one" ? t2 == 1 : t2 == 0;
            }
            return arr && !arr->empty();
        case VT::Hash:
            // A Proc / Proc::Async is true iff it exited successfully (exit code 0).
            if ((hashKind == "Proc" || hashKind == "Proc::Async") && hash) {
                auto it = hash->find("exitcode");
                return it == hash->end() || it->second.toInt() == 0;
            }
            if (hashKind == "Failure") return false; // a Failure boolifies False (soft failure)
            // A Promise boolifies True only once it is Kept/Broken (a Planned
            // promise is False) — IO::Socket::Async::SSL's handshake pump relies
            // on `elsif $!connected-promise` being false while still negotiating.
            if (hashKind == "Promise" && hash) {
                auto it = hash->find("status");
                if (it != hash->end()) return it->second.toStr() != "Planned";
            }
            return (hashKind == "Raku" || hashKind == "Compiler") // object-like: always defined/true
                            || (hash && !hash->empty());
        case VT::Range: return true;
        case VT::Code:  return true;
        case VT::Pair:  return true; // a Pair object is always true (one-key-hash semantics)
        case VT::Type:  return false;
        case VT::Whatever: return true;
        case VT::Object: return true;
        case VT::Regex: return true;
        case VT::Match: return true;
    }
    return false;
}

long long Value::toInt() const {
    switch (t) {
        case VT::Bool: return b ? 1 : 0;
        case VT::Int:  return big ? big->toLL() : i;
        case VT::Num:
            // Casting ±Inf/NaN to integer is UB (x86 gives LLONG_MIN for +Inf!) —
            // saturate instead, so `0..Inf` becomes 0..LLONG_MAX and is recognised
            // as an infinite range by the lazy-range handling.
            if (std::isnan(n)) return 0;
            if (n >= 9223372036854775807.0) return 9223372036854775807LL;
            if (n <= -9223372036854775808.0) return -9223372036854775807LL - 1;
            return (long long)n;
        case VT::Rat:  { if (!ratN || !ratD || ratD->isZero()) return 0; BigInt q, r; BigInt::divmod(*ratN, *ratD, q, r); return q.toLL(); }
        case VT::Str:  { try {
            // a Blob/Buf numifies to its ELEMENT COUNT (Rakudo: `8 * $msg` is
            // bits, `blob32 $M where $M == 16` counts words), never by parsing
            // its bytes as text
            if (hashKind == "Blob" || hashKind == "Buf") return blobElems();
            size_t pos = 0;
            long long v = std::stoll(s, &pos);
            // the string is a wider numeric literal ("3.14", "1.23E4", "1/3"):
            // numify the whole thing, then truncate — like .Int on the numeric
            if (pos < s.size() && (s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E' || s[pos] == '/')) {
                try {
                    double d = s[pos] == '/' ? (std::stod(s.substr(pos + 1)) == 0.0 ? 0.0
                                                : (double)v / std::stod(s.substr(pos + 1)))
                                             : std::stod(s);
                    if (std::isnan(d)) return 0;
                    if (d >= 9223372036854775807.0) return 9223372036854775807LL;
                    if (d <= -9223372036854775808.0) return -9223372036854775807LL - 1;
                    return (long long)d;
                } catch (...) {}
            }
            return v;
        } catch (...) { return 0; } }
        case VT::Match: { try { return std::stoll(s); } catch (...) { return 0; } } // matched text as a number
        case VT::Array: return arr ? (long long)arr->size() : 0;
        case VT::Hash:
            // A tr/// StrDistance numifies to the substitution count.
            if (hash && hashKind == "StrDistance") {
                auto it = hash->find("distance");
                if (it != hash->end()) return it->second.toInt();
            }
            // A Proc / Proc::Async numifies to its exit status (+$proc), like Rakudo.
            if (hash && (hashKind == "Proc" || hashKind == "Proc::Async")) {
                auto it = hash->find("exitcode");
                return it != hash->end() ? it->second.toInt() : 0;
            }
            // A Bag/Mix numifies to its .total (sum of counts), not its .elems.
            if (hash && (hashKind.rfind("Bag", 0) == 0 || hashKind.rfind("Mix", 0) == 0)) {
                double t = 0; for (auto& kv : *hash) t += kv.second.toNum();
                return (long long)t;
            }
            return hash ? (long long)hash->size() : 0;
        default: return 0;
    }
}

double Value::toNum() const {
    switch (t) {
        case VT::Bool: return b ? 1.0 : 0.0;
        case VT::Int:  return big ? big->toDouble() : (double)i;
        case VT::Num:  return n;
        case VT::Rat:
            if (ratN && ratD && ratD->isZero()) // zero-denominator Rat numifies to ±Inf / NaN
                return ratN->isZero() ? std::numeric_limits<double>::quiet_NaN()
                     : ratN->sign > 0 ? std::numeric_limits<double>::infinity()
                                      : -std::numeric_limits<double>::infinity();
            if (ratN && ratD) {
                // both parts exactly representable → one division, one rounding
                auto dblExact = [](const BigInt& b) {
                    return b.mag.size() <= 1 ||
                           (b.fitsLL() && std::llabs(b.toLL()) <= (1LL << 53));
                };
                if (!(dblExact(*ratN) && dblExact(*ratD))) {
                    // wide parts: separate BigInt→double conversions round TWICE
                    // and drift in the last bits (1e26-digit division printed
                    // …37 where Rakudo has …33). Long-divide to 19 significant
                    // decimal digits and let strtod do the single, correct
                    // rounding. Also covers FatRats too big for double (the
                    // old inf/inf special case).
                    std::string sn = ratN->abs().toString(), sd = ratD->abs().toString();
                    long long scale = 19 - ((long long)sn.size() - (long long)sd.size());
                    BigInt num = ratN->abs(), den = ratD->abs(), q, r;
                    if (scale > 0) num = num * BigInt(10).pow(scale);
                    else if (scale < 0) den = den * BigInt(10).pow(-scale);
                    BigInt::divmod(num, den, q, r);
                    std::string lit = q.toString() + "e" + std::to_string(-scale);
                    double d = std::strtod(lit.c_str(), nullptr);
                    return ratN->sign < 0 ? -d : d;
                }
                return ratN->toDouble() / ratD->toDouble();
            }
            return 0.0;
        case VT::Str:
            if (hashKind == "Blob" || hashKind == "Buf") return (double)blobElems(); // element count, like .Int
            { try { return std::stod(s); } catch (...) { return 0.0; } }
        case VT::Match: { try { return std::stod(s); } catch (...) { return 0.0; } }
        default: return (double)toInt();
    }
}

static std::string numToStr(double n) {
    if (std::isinf(n)) return n < 0 ? "-Inf" : "Inf";
    if (std::isnan(n)) return "NaN";
    if (n == 0.0 && std::signbit(n)) return "-0"; // negative zero keeps its sign (Rakudo)
    if (n == (long long)n && std::fabs(n) < 1e15) {
        return std::to_string((long long)n);
    }
    // shortest decimal that round-trips to the same double (matches Rakudo's Num.Str)
    for (int prec = 15; prec <= 17; prec++) {
        std::ostringstream os;
        os.precision(prec);
        os << n;
        if (std::strtod(os.str().c_str(), nullptr) == n) return os.str();
    }
    std::ostringstream os;
    os.precision(17);
    os << n;
    return os.str();
}

static std::string ratToStr(const BigInt& num, const BigInt& den) {
    if (den.isZero()) return "0";
    bool neg = (num.sign < 0) != (den.sign < 0);
    std::string sign = neg ? "-" : "";
    BigInt n = num.abs(), d = den.abs();
    { BigInt ip, rem; BigInt::divmod(n, d, ip, rem);
      if (rem.isZero()) return ip.isZero() ? "0" : sign + ip.toString(); } // integer-valued
    // Rakudo Rat.Str: fractional digits = max(6, #digits(denominator) + 1),
    // the value rounded to that many places, then trailing zeros trimmed.
    long long fracDigits = std::max<long long>(6, (long long)d.toString().size() + 1);
    BigInt scaled = n * BigInt(10).pow(fracDigits);
    BigInt q, r; BigInt::divmod(scaled, d, q, r);
    if ((r * BigInt(2) - d).sign >= 0) q = q + BigInt(1); // round half up
    std::string digits = q.toString();
    while ((long long)digits.size() <= fracDigits) digits = "0" + digits;
    std::string ipart = digits.substr(0, digits.size() - fracDigits);
    std::string fpart = digits.substr(digits.size() - fracDigits);
    while (!fpart.empty() && fpart.back() == '0') fpart.pop_back();
    std::string res = sign + ipart + (fpart.empty() ? "" : "." + fpart);
    return res == "-0" ? "0" : res;
}

std::string Value::toStr() const {
    if (!enumName.empty()) return enumName;
    if (isAllomorph()) return s; // the allomorph's source string ("0123", "1/3", …)
    switch (t) {
        case VT::Nil:
        case VT::Any:  return "";
        case VT::Bool: return b ? "True" : "False";
        case VT::Int:  return big ? big->toString() : std::to_string(i);
        case VT::Num:  return numToStr(n);
        case VT::Complex: {
            std::string r = numToStr(n);
            std::string i2 = numToStr(im);
            return r + (im < 0 || i2[0] == '-' ? "" : "+") + i2 + "i";
        }
        case VT::Rat:
            if (ratN && ratD && ratD->isZero()) // Rakudo dies on Str-coercing a zero-denominator Rat
                throw RakuError{Value::typeObj("X::Numeric::DivideByZero"),
                                "Attempt to divide by zero when coercing Rational to Str"};
            return (ratN && ratD) ? ratToStr(*ratN, *ratD) : "0";
        case VT::Str:  return s;
        case VT::Type: return "(" + s + ")";
        case VT::Pair: return s + "\t" + (pairVal ? pairVal->toStr() : "");
        case VT::Range: {
            // a finite Range stringifies to its elements (`put 1..5` → 1 2 3 4 5);
            // an infinite one keeps the endpoint form
            if (ofType == "Str") { // Str range: space-join the chars
                long long lo = rFrom + (rExFrom ? 1 : 0), hi = rTo - (rExTo ? 1 : 0);
                std::string out2;
                for (long long k2 = lo; k2 <= hi; k2++) { if (k2 != lo) out2 += " "; out2 += cpToU8((uint32_t)k2); }
                return out2;
            }
            if (rNum) { // fractional: space-join the stepped elements
                std::string out2; bool first = true;
                double lo = n + (rExFrom ? 1.0 : 0.0);
                for (double x = lo; rExTo ? x < im - 1e-9 : x <= im + 1e-9; x += 1.0) {
                    if (!first) out2 += " "; first = false; out2 += Value::number(x).toStr();
                }
                return out2;
            }
            if (rTo < 9000000000000000000LL && rFrom > -9000000000000000000LL) {
                long long lo = rFrom + (rExFrom ? 1 : 0), hi = rTo - (rExTo ? 1 : 0);
                std::string out2;
                for (long long k2 = lo; k2 <= hi; k2++) { if (k2 != lo) out2 += " "; out2 += std::to_string(k2); }
                return out2;
            }
            std::ostringstream os;
            os << rFrom << ".." << (rExTo ? "^" : "") << rTo;
            return os.str();
        }
        case VT::Array: {
            ReprDepthGuard g; if (g.tooDeep()) return "...";
            std::string out;
            if (arr) for (size_t k = 0; k < arr->size(); k++) {
                if (k) out += " ";
                out += (*arr)[k].toStr();
            }
            return out;
        }
        case VT::Hash: {
            if (hashKind == "Format" && hash && hash->count("fmt")) return hash->at("fmt").toStr();
            if (hashKind == "StrDistance" && hash && hash->count("after"))
                return hash->at("after").toStr(); // "$dist" is the resulting string
            if ((hashKind == "Date" || hashKind == "DateTime") && hash) return dateGist(*hash, hashKind == "Date");
            ReprDepthGuard g; if (g.tooDeep()) return "...";
            std::string out;
            if (hash) { bool first = true;
                for (auto& kv : *hash) {
                    if (!first) out += "\n"; first = false;
                    out += kv.first + "\t" + kv.second.toStr();
                }
            }
            return out;
        }
        case VT::Code: return "sub { ... }";
        case VT::Whatever: return "*";
        case VT::Object:
            if (obj && obj->hasBoxed) return obj->boxed.toStr(); // but/does mixin over a value
            if (obj && obj->cls && obj->cls->name.rfind("X::", 0) == 0) { // exceptions stringify to their message
                auto it = obj->attrs.find("message");
                if (it != obj->attrs.end() && it->second.t == VT::Str) return it->second.s;
            }
            return obj && obj->cls ? obj->cls->name + "<obj>" : "Object";
        case VT::Regex: return s;
        case VT::Match: return s;
    }
    return "";
}

std::string Value::gist() const {
    if (isAllomorph()) return s; // IntStr `<0123>`.gist is "0123"
    if (!enumName.empty()) {
        // a Junction gists with its eigenstates: any(1, 2, 3)
        if (t == VT::Array && arr &&
            (enumName == "any" || enumName == "all" || enumName == "one" || enumName == "none")) {
            ReprDepthGuard g; if (g.tooDeep()) return enumName + "(...)";
            std::string out = enumName + "(";
            for (size_t k = 0; k < arr->size(); k++) {
                if (k) out += ", ";
                out += (*arr)[k].gist();
            }
            return out + ")";
        }
        return enumName;
    }
    switch (t) {
        case VT::Nil:  return "Nil";
        case VT::Any:  return "(Any)";
        case VT::Type: return "(" + (ofType.empty() ? s : s + "[" + ofType + "]") + ")";
        case VT::Array: {
            ReprDepthGuard g; if (g.tooDeep()) return isList ? "(...)" : "[...]";
            std::string out = isList ? "(" : "[";
            if (arr) for (size_t k = 0; k < arr->size(); k++) {
                if (k) out += " ";
                out += (*arr)[k].gist();
            }
            return out + (isList ? ")" : "]");
        }
        case VT::Pair: return s + " => " + (pairVal ? pairVal->gist() : "");
        case VT::Str:
            if (hashKind == "Version") return "v" + s; // v1.2.3.gist is "v1.2.3" (.Str is "1.2.3")
            if (hashKind == "Buf" || hashKind == "Blob") { // Buf:0x<01 02 03> / Buf[uint8]:0x<…>
                std::string h = hashKind + (ofType.empty() ? "" : "[" + ofType + "]") + ":0x<";
                static const char* hx = "0123456789ABCDEF";
                for (size_t k = 0; k < s.size(); k++) { if (k) h += ' '; unsigned char b = s[k]; h += hx[b >> 4]; h += hx[b & 15]; }
                return h + ">";
            }
            return s;
        case VT::Range: { // gist keeps the endpoint form (Str expands the elements)
            const char* exF = rExFrom ? "^" : "";
            const char* exT = rExTo ? "^" : "";
            if (ofType == "Str")
                return "\"" + cpToU8((uint32_t)rFrom) + "\"" + exF + ".." + exT + "\"" + cpToU8((uint32_t)rTo) + "\"";
            if (rNum) return Value::number(n).toStr() + exF + ".." + exT + Value::number(im).toStr();
            std::ostringstream os;
            os << rFrom << exF << ".." << exT << rTo;
            return os.str();
        }
        case VT::Hash: {
            // plain Hash: {a => 1, b => 2}; Map: Map.new((a => 1)); Set/Bag/Mix
            // families: Kind(elems). Everything else (Date, Failure, …) keeps
            // its toStr form via the default below.
            if (hashKind == "Attribute" && hash) { // Str $!name
                std::string tn = hash->count("type") ? hash->at("type").s : "Mu";
                std::string nm = hash->count("name") ? hash->at("name").s : "";
                return (tn.empty() ? "Mu" : tn) + " " + nm;
            }
            if (hashKind == "Scalar" && hash) { // a .VAR container gists as its value
                auto it = hash->find("value");
                return it != hash->end() ? it->second.gist() : "(Any)";
            }
            if (hashKind.empty() || hashKind == "Map" || hashKind == "Stash") {
                ReprDepthGuard g; if (g.tooDeep()) return "{...}";
                std::string body; bool first = true;
                if (hash) for (auto& kv : *hash) {
                    if (!first) body += ", "; first = false;
                    body += kv.first + " => " + kv.second.gist();
                }
                if (hashKind == "Map") return "Map.new((" + body + "))";
                return "{" + body + "}";
            }
            if (hashKind == "Set" || hashKind == "SetHash" ||
                hashKind == "Bag" || hashKind == "BagHash" ||
                hashKind == "Mix" || hashKind == "MixHash") {
                ReprDepthGuard g; if (g.tooDeep()) return hashKind + "(...)";
                bool isSet = hashKind[0] == 'S', isMix = hashKind[0] == 'M';
                std::string body; bool first = true;
                if (hash) for (auto& kv : *hash) {
                    if (!first) body += " "; first = false;
                    if (isSet) body += kv.first;
                    else if (isMix) body += kv.first + " => " + kv.second.gist();
                    else { // Bag: elem(count), (1) omitted
                        body += kv.first;
                        if (!(kv.second.t == VT::Int && kv.second.toInt() == 1))
                            body += "(" + kv.second.gist() + ")";
                    }
                }
                return hashKind + "(" + body + ")";
            }
            return toStr();
        }
        case VT::Match: {
            // Rakudo's Match.gist: ｢matched｣, then one `key => value` line per
            // capture — a LIST capture (quantified subrule) contributes one line
            // per element under the same key — all ordered by match position,
            // children indented by depth. Multiline matched text stays raw.
            std::function<std::string(const Value&, int)> mg = [&mg](const Value& m, int d) -> std::string {
                std::string r = "\xEF\xBD\xA2" + m.s + "\xEF\xBD\xA3";
                std::vector<std::pair<std::string, const Value*>> caps;
                auto add = [&caps](const std::string& key, const Value& v) {
                    if (v.t == VT::Array && v.arr) { for (auto& e : *v.arr) caps.push_back({key, &e}); }
                    else caps.push_back({key, &v});
                };
                if (m.arr) for (size_t k = 0; k < m.arr->size(); k++) add(std::to_string(k), (*m.arr)[k]);
                if (m.hash) for (auto& kv : *m.hash) add(kv.first, kv.second);
                std::stable_sort(caps.begin(), caps.end(),
                                 [](auto& a, auto& b) { return a.second->rFrom < b.second->rFrom; });
                std::string pad(d + 1, ' ');
                for (auto& c : caps)
                    r += "\n" + pad + c.first + " => " +
                         (c.second->t == VT::Match ? mg(*c.second, d + 1) : c.second->gist());
                return r;
            };
            return mg(*this, 0);
        }
        case VT::Code:
            if (code && code->isWhateverCode) return "WhateverCode.new"; // say (* > 2)
            return toStr();
        default: return toStr();
    }
}

std::string Value::typeName() const {
    if (!enumType.empty()) return enumType; // enum value / enum type object -> its enum type
    if (isAllomorph()) return hashKind;     // IntStr / RatStr / NumStr / ComplexStr
    switch (t) {
        case VT::Nil:  return "Nil";
        case VT::Any:  return "Any";
        case VT::Bool: return "Bool";
        case VT::Int:  return "Int";
        case VT::Num:  return hashKind == "Duration" ? "Duration"
                            : hashKind == "Instant" ? "Instant" : "Num";
        case VT::Complex: return "Complex";
        case VT::Str:  return hashKind == "IO" ? (enumName.empty() ? "IO::Path" : "IO::Path::" + enumName) : hashKind == "Version" ? "Version" : hashKind == "Blob" ? "Blob" : hashKind == "Buf" ? "Buf" : hashKind == "IO::Special" ? "IO::Special" : "Str";
        case VT::Array:
            if (s == "Uni" || s == "NFC" || s == "NFD" || s == "NFKC" || s == "NFKD") return s;
            if (enumName == "any" || enumName == "all" || enumName == "one" || enumName == "none") return "Junction";
            if (hashKind == "Capture") return "Capture"; // \(…) literal
            return !isList ? "Array" : s == "Seq" ? "Seq" : s == "Slip" ? "Slip" : "List";
        case VT::Hash:  if (hashKind == "Pod" && hash && hash->count("podclass")) return hash->at("podclass").s;
                        return hashKind.empty() ? "Hash" : hashKind; // the TYPE name (gist is via toStr)
        case VT::Code:  return code && code->isWhateverCode ? "WhateverCode"
                             : code && code->isMethod ? "Method" : code && code->isBlock ? "Block" : "Sub";
        case VT::Rat:   return fatRat ? "FatRat" : "Rat";
        case VT::Range: return "Range";
        case VT::Pair:  return "Pair";
        case VT::Type:  return ofType.empty() ? s : s + "[" + ofType + "]";
        case VT::Whatever: return b ? "HyperWhatever" : "Whatever"; // `**` marks hyper via .b
        case VT::Object: return obj && obj->cls ? obj->cls->name : "Object";
        case VT::Regex: return "Regex";
        case VT::Match: return "Match";
    }
    return "Any";
}


long long Value::blobWordAt(long long idx) const {
    int w = blobElemSize();
    unsigned long long v = 0;
    size_t base = (size_t)idx * w;
    for (int k = 0; k < w; k++) v |= (unsigned long long)(unsigned char)s[base + k] << (8 * k);
    return (long long)v;
}

ValueList Value::blobList() const {
    ValueList out;
    long long n = blobElems();
    out.reserve((size_t)n);
    for (long long i = 0; i < n; i++) out.push_back(Value::integer(blobWordAt(i)));
    return out;
}

ValueList Value::flatten() const {
    ValueList out;
    if (t == VT::Array && arr) {
        for (auto& v : *arr) {
            if (v.t == VT::Array || v.t == VT::Range) {
                ValueList sub = v.flatten();
                out.insert(out.end(), sub.begin(), sub.end());
            } else {
                out.push_back(v);
            }
        }
    } else if (t == VT::Range && rNum) {
        // fractional range: step by 1 from `n`, stopping at `im` (exclusive bounds
        // drop an endpoint that lands exactly on it)
        double lo = n + (rExFrom ? 1.0 : 0.0), hi = im;
        for (double x = lo; rExTo ? x < hi - 1e-9 : x <= hi + 1e-9; x += 1.0)
            out.push_back(Value::number(x));
    } else if (t == VT::Range && ofType == "Str") {
        long long lo = rFrom + (rExFrom ? 1 : 0);
        long long hi = rTo - (rExTo ? 1 : 0);
        for (long long k = lo; k <= hi; k++) out.push_back(Value::str(cpToU8((uint32_t)k)));
    } else if (t == VT::Range) {
        long long lo = rFrom + (rExFrom ? 1 : 0);
        long long hi = rTo - (rExTo ? 1 : 0);
        // an infinite range (1..* / 1..Inf) yields a bounded prefix instead of
        // hanging — eager consumers that reach it index/scan a finite prefix
        if (rTo >= 9000000000000000000LL || rFrom <= -9000000000000000000LL) {
            if (hi > lo + 9999 || hi < lo) hi = lo + 9999;
        }
        for (long long k = lo; k <= hi; k++) out.push_back(Value::integer(k));
    } else {
        out.push_back(*this);
    }
    return out;
}

// The magic increment/decrement window (S03, Str.succ): the LAST alphanumeric
// run that is not immediately preceded by a '.', so trailing extension-like
// segments are skipped — "abc.txt"++ is "abd.txt", "foo.tar.gz"++ is
// "fop.tar.gz", "v1.2.3"++ is "v2.2.3". Returns false when nothing in the
// string is incrementable (".txt", "🐧.txt", "") — the string stays unchanged.
static bool succWindow(const std::string& s, int& lo, int& hi) {
    auto alnum = [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    };
    int i = (int)s.size() - 1;
    while (i >= 0) {
        while (i >= 0 && !alnum((unsigned char)s[i])) i--;   // skip a non-alnum tail
        if (i < 0) return false;
        int end = i;
        while (i >= 0 && alnum((unsigned char)s[i])) i--;    // the candidate run
        if (i < 0 || s[i] != '.') { lo = i + 1; hi = end + 1; return true; }
        i--; // run is a ".ext" segment — skip it and its dot, keep scanning left
    }
    return false;
}

// ---- non-ASCII magic increment (verified against Rakudo v2026.06) ----------
// Each incrementable range: [lo..hi] with an optional excluded codepoint
// (Greek uppercase has the U+03A2 hole; lowercase treats final sigma ς as not
// part of the alphabet), the wrap target on carry, and the codepoint prepended
// when the carry escapes the window (letters prepend their first letter,
// digit families their ONE, circled numbers ①).
struct SuccRange { uint32_t lo, hi, skip, wrap, prepend; };
static const SuccRange kSuccRanges[] = {
    {'0', '9', 0, '0', '1'},
    {'a', 'z', 0, 'a', 'a'},
    {'A', 'Z', 0, 'A', 'A'},
    {0x03B1, 0x03C9, 0x03C2, 0x03B1, 0x03B1}, // Greek α..ω, ς is not a member
    {0x0391, 0x03A9, 0x03A2, 0x0391, 0x0391}, // Greek Α..Ω, U+03A2 is unassigned
    {0x0430, 0x044F, 0, 0x0430, 0x0430},      // Cyrillic а..я
    {0x0410, 0x042F, 0, 0x0410, 0x0410},      // Cyrillic А..Я
    {0x05D0, 0x05EA, 0, 0x05D0, 0x05D0},      // Hebrew א..ת
    {0x0660, 0x0669, 0, 0x0660, 0x0661},      // Arabic-Indic digits
    {0x0966, 0x096F, 0, 0x0966, 0x0967},      // Devanagari digits
    {0x09E6, 0x09EF, 0, 0x09E6, 0x09E7},      // Bengali digits
    {0x0A66, 0x0A6F, 0, 0x0A66, 0x0A67},      // Gurmukhi digits
    {0x0AE6, 0x0AEF, 0, 0x0AE6, 0x0AE7},      // Gujarati digits
    {0x0B66, 0x0B6F, 0, 0x0B66, 0x0B67},      // Oriya digits
    {0x0BE6, 0x0BEF, 0, 0x0BE6, 0x0BE7},      // Tamil digits
    {0x0C66, 0x0C6F, 0, 0x0C66, 0x0C67},      // Telugu digits
    {0x0CE6, 0x0CEF, 0, 0x0CE6, 0x0CE7},      // Kannada digits
    {0x0D66, 0x0D6F, 0, 0x0D66, 0x0D67},      // Malayalam digits
    {0x0E50, 0x0E59, 0, 0x0E50, 0x0E51},      // Thai digits
    {0x0ED0, 0x0ED9, 0, 0x0ED0, 0x0ED1},      // Lao digits
    {0x0F20, 0x0F29, 0, 0x0F20, 0x0F21},      // Tibetan digits
    {0x1040, 0x1049, 0, 0x1040, 0x1041},      // Myanmar digits
    {0x17E0, 0x17E9, 0, 0x17E0, 0x17E1},      // Khmer digits
    {0xFF10, 0xFF19, 0, 0xFF10, 0xFF11},      // fullwidth digits
    {0x2460, 0x2473, 0, 0x2460, 0x2460},      // circled ①..⑳ (no zero: wraps to ①)
};
static const SuccRange* succRangeOf(uint32_t cp) {
    for (auto& r : kSuccRanges)
        if (cp >= r.lo && cp <= r.hi && cp != r.skip) return &r;
    return nullptr;
}
static std::vector<uint32_t> sxDecode(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        uint32_t cp; int n;
        if (c < 0x80)      { cp = c; n = 1; }
        else if (c < 0xE0) { cp = c & 0x1F; n = 2; }
        else if (c < 0xF0) { cp = c & 0x0F; n = 3; }
        else               { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
        out.push_back(cp); i += n;
    }
    return out;
}
static std::string sxEncode(const std::vector<uint32_t>& cps) {
    std::string o;
    for (uint32_t cp : cps) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800)   { o += (char)(0xC0 | (cp >> 6));  o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    return o;
}
static bool sxAlnum(uint32_t cp) {
    if (cp < 128) return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
    std::string gc = uniGeneralCategory(cp);
    return !gc.empty() && (gc[0] == 'L' || gc[0] == 'N');
}
// The codepoint window: final non-extension ALPHANUMERIC run, then its maximal
// suffix of range-member codepoints. An alnum that is in no range blocks:
// "ας"++ is unchanged (ς can't increment, and being a letter it can't be
// skipped like punctuation), while "ςα"++ is "ςβ".
static bool succWindowCp(const std::vector<uint32_t>& c, int& lo, int& hi) {
    int i = (int)c.size() - 1;
    while (i >= 0) {
        while (i >= 0 && !sxAlnum(c[i])) i--;
        if (i < 0) return false;
        int end = i;
        while (i >= 0 && sxAlnum(c[i])) i--;
        if (i >= 0 && c[i] == '.') { i--; continue; } // an extension segment
        if (!succRangeOf(c[end])) return false;       // blocked (e.g. trailing ς)
        int wlo = end;
        while (wlo - 1 > i && succRangeOf(c[wlo - 1])) wlo--;
        lo = wlo; hi = end + 1; return true;
    }
    return false;
}
static bool sxAscii(const std::string& s) {
    for (unsigned char c : s) if (c >= 0x80) return false;
    return true;
}

std::string strSucc(const std::string& s) {
    if (sxAscii(s)) { // fast byte path for the overwhelmingly common case
        int lo, hi;
        if (!succWindow(s, lo, hi)) return s; // nothing incrementable: unchanged (Rakudo)
        std::string r = s;
        int pos = hi - 1;
        for (; pos >= lo; --pos) {
            char c = r[pos];
            if (c >= '0' && c <= '9') { if (c != '9') { r[pos] = c + 1; return r; } r[pos] = '0'; }
            else if (c >= 'a' && c <= 'z') { if (c != 'z') { r[pos] = c + 1; return r; } r[pos] = 'a'; }
            else { if (c != 'Z') { r[pos] = c + 1; return r; } r[pos] = 'A'; }
        }
        // carried past the window start: grow the window with its first char's class
        char k = r[lo];
        char ins = (k == '0') ? '1' : (k == 'a') ? 'a' : (k == 'A') ? 'A' : '1';
        r.insert(lo, 1, ins);
        return r;
    }
    std::vector<uint32_t> c = sxDecode(s);
    int lo, hi;
    if (!succWindowCp(c, lo, hi)) return s;
    for (int pos = hi - 1; pos >= lo; --pos) {
        const SuccRange* r = succRangeOf(c[pos]);
        uint32_t nxt = c[pos] + 1;
        if (nxt == r->skip) nxt++;
        if (nxt <= r->hi) { c[pos] = nxt; return sxEncode(c); }
        c[pos] = r->wrap; // carry
    }
    c.insert(c.begin() + lo, succRangeOf(c[lo])->prepend);
    return sxEncode(c);
}

std::string strPred(const std::string& s, bool& ok) {
    ok = true;
    if (sxAscii(s)) {
        int lo, hi;
        if (!succWindow(s, lo, hi)) return s; // nothing decrementable: unchanged
        std::string r = s;
        int pos = hi - 1;
        for (; pos >= lo; --pos) {
            char c = r[pos];
            if (c >= '0' && c <= '9') { if (c != '0') { r[pos] = c - 1; return r; } r[pos] = '9'; }
            else if (c >= 'a' && c <= 'z') { if (c != 'a') { r[pos] = c - 1; return r; } r[pos] = 'z'; }
            else { if (c != 'A') { r[pos] = c - 1; return r; } r[pos] = 'Z'; }
        }
        ok = false; // borrowed past the window start: "Decrement out of range"
        return s;
    }
    std::vector<uint32_t> c = sxDecode(s);
    int lo, hi;
    if (!succWindowCp(c, lo, hi)) return s;
    for (int pos = hi - 1; pos >= lo; --pos) {
        const SuccRange* r = succRangeOf(c[pos]);
        if (c[pos] > r->lo) {
            uint32_t prv = c[pos] - 1;
            if (prv == r->skip) prv--;
            c[pos] = prv;
            return sxEncode(c);
        }
        c[pos] = r->hi == r->skip ? r->hi - 1 : r->hi; // borrow (skip can't sit at hi in our table)
    }
    ok = false; // borrowed past the window start
    return s;
}

bool valueEq(const Value& a, const Value& b) {
    if (a.isNumeric() && b.isNumeric())
        return a.toNum() == b.toNum();
    return a.toStr() == b.toStr();
}

int valueCmp(const Value& a, const Value& b) {
    if (a.isNumeric() && b.isNumeric()) {
        // Fast, exact path for native ints (the common case — e.g. sorting 50k
        // integers): compare the int64 fields directly, no double, no allocation.
        bool aInt = (a.t == VT::Int && !a.big) || a.t == VT::Bool;
        bool bInt = (b.t == VT::Int && !b.big) || b.t == VT::Bool;
        if (aInt && bInt) {
            long long x = a.t == VT::Bool ? (a.b ? 1 : 0) : a.i;
            long long y = b.t == VT::Bool ? (b.b ? 1 : 0) : b.i;
            return x < y ? -1 : x > y ? 1 : 0;
        }
        // Exact types where a value can exceed 2^53 (big Int / Rat) compare via
        // the <=> operator's BigInt cross-multiply — the double path would lose
        // precision (e.g. (2**53, 2**53+1).max picked the wrong element).
        auto exact = [](const Value& v) { return v.t == VT::Int || v.t == VT::Rat || v.t == VT::Bool; };
        if (exact(a) && exact(b)) return applyArith("<=>", a, b).toInt();
        double x = a.toNum(), y = b.toNum(); // Num/Complex: inherently binary float
        return x < y ? -1 : x > y ? 1 : 0;
    }
    // Pairs compare by key first, then value (Rakudo's Pair cmp semantics).
    // Compare the TYPED key (pairKey — an Int/Num/… preserved from `1 => 2`),
    // not the stringified `s`: otherwise `12 => …` sorts before `8 => …`.
    if (a.t == VT::Pair && b.t == VT::Pair) {
        Value ak = a.pairKey ? *a.pairKey : Value::str(a.s);
        Value bk = b.pairKey ? *b.pairKey : Value::str(b.s);
        int k = valueCmp(ak, bk);
        if (k != 0) return k;
        Value av = a.pairVal ? *a.pairVal : Value::any();
        Value bv = b.pairVal ? *b.pairVal : Value::any();
        return valueCmp(av, bv);
    }
    // Lists compare elementwise, shorter-is-less on a tie — so a sort key of
    // `{ -.value, .key }` orders by the first element, then the second.
    if (a.t == VT::Array && b.t == VT::Array && a.arr && b.arr && a.enumName.empty() && b.enumName.empty()) {
        size_t n = std::min(a.arr->size(), b.arr->size());
        for (size_t k = 0; k < n; k++) {
            int c = valueCmp((*a.arr)[k], (*b.arr)[k]);
            if (c != 0) return c;
        }
        return a.arr->size() < b.arr->size() ? -1 : a.arr->size() > b.arr->size() ? 1 : 0;
    }
    std::string x = a.toStr(), y = b.toStr();
    return x < y ? -1 : x > y ? 1 : 0;
}

} // namespace rakupp
