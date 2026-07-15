#include "Value.h"
#include "Interpreter.h" // RakuError (zero-denominator Rat Str-coercion throws)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>

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
    char buf[40];
    if (isDate) std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lld", f("year"), f("month"), f("day"));
    else {
        long long tz = f("timezone");
        char suf[12];
        if (tz == 0) std::snprintf(suf, sizeof suf, "Z");
        else std::snprintf(suf, sizeof suf, "%c%02lld:%02lld", tz < 0 ? '-' : '+',
                           (tz < 0 ? -tz : tz) / 3600, ((tz < 0 ? -tz : tz) % 3600) / 60);
        auto sit = h.find("second");
        double sd = sit != h.end() ? sit->second.toNum() : 0.0;
        if (sd != (double)(long long)sd)
            std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lldT%02lld:%02lld:%09.6f%s",
                          f("year"), f("month"), f("day"), f("hour"), f("minute"), sd, suf);
        else
            std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld%s",
                          f("year"), f("month"), f("day"), f("hour"), f("minute"), f("second"), suf);
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
        case VT::Array: return arr && !arr->empty();
        case VT::Hash:
            // A Proc / Proc::Async is true iff it exited successfully (exit code 0).
            if ((hashKind == "Proc" || hashKind == "Proc::Async") && hash) {
                auto it = hash->find("exitcode");
                return it == hash->end() || it->second.toInt() == 0;
            }
            if (hashKind == "Failure") return false; // a Failure boolifies False (soft failure)
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
                double dn = ratN->toDouble(), dd = ratD->toDouble();
                if (std::isinf(dn) || std::isinf(dd)) {
                    // both sides overflow a double (FatRat with huge parts): divide
                    // the leading digits and scale by the ten-power difference —
                    // inf/inf would be NaN, and n/inf would wrongly give 0.
                    std::string sn = ratN->abs().toString(), sd = ratD->abs().toString();
                    double mn = std::stod(sn.substr(0, 17)), md = std::stod(sd.substr(0, 17));
                    double mag = ((double)sn.size() - (double)std::min<size_t>(sn.size(), 17)) -
                                 ((double)sd.size() - (double)std::min<size_t>(sd.size(), 17));
                    double r = (mn / md) * std::pow(10.0, mag);
                    return ratN->sign < 0 ? -r : r;
                }
                return dn / dd;
            }
            return 0.0;
        case VT::Str:  { try { return std::stod(s); } catch (...) { return 0.0; } }
        case VT::Match: { try { return std::stod(s); } catch (...) { return 0.0; } }
        default: return (double)toInt();
    }
}

static std::string numToStr(double n) {
    if (std::isinf(n)) return n < 0 ? "-Inf" : "Inf";
    if (std::isnan(n)) return "NaN";
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
        case VT::Str:  return s;
        case VT::Range: { // gist keeps the endpoint form (Str expands the elements)
            std::ostringstream os;
            os << rFrom << ".." << (rExTo ? "^" : "") << rTo;
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
            // ｢matched｣ followed by one indented line per capture (positional then named)
            std::string r = "\xEF\xBD\xA2" + s + "\xEF\xBD\xA3";
            auto indentChild = [](const std::string& g) {
                std::string out; for (char ch : g) { out += ch; if (ch == '\n') out += ' '; } return out;
            };
            if (arr) for (size_t k = 0; k < arr->size(); k++)
                r += "\n " + std::to_string(k) + " => " + indentChild((*arr)[k].gist());
            if (hash) {
                std::vector<const std::pair<const std::string, Value>*> items;
                for (auto& kv : *hash) items.push_back(&kv);
                std::sort(items.begin(), items.end(), [](auto* a, auto* b) { return a->second.rFrom < b->second.rFrom; });
                for (auto* kv : items)
                    r += "\n " + kv->first + " => " + indentChild(kv->second.gist());
            }
            return r;
        }
        default: return toStr();
    }
}

std::string Value::typeName() const {
    if (!enumType.empty()) return enumType; // enum value / enum type object -> its enum type
    switch (t) {
        case VT::Nil:  return "Nil";
        case VT::Any:  return "Any";
        case VT::Bool: return "Bool";
        case VT::Int:  return "Int";
        case VT::Num:  return hashKind == "Duration" ? "Duration"
                            : hashKind == "Instant" ? "Instant" : "Num";
        case VT::Complex: return "Complex";
        case VT::Str:  return hashKind == "IO" ? "IO::Path" : hashKind == "Version" ? "Version" : hashKind == "Blob" ? "Blob" : hashKind == "Buf" ? "Buf" : hashKind == "IO::Special" ? "IO::Special" : "Str";
        case VT::Array:
            if (s == "Uni" || s == "NFC" || s == "NFD" || s == "NFKC" || s == "NFKD") return s;
            if (enumName == "any" || enumName == "all" || enumName == "one" || enumName == "none") return "Junction";
            return !isList ? "Array" : s == "Seq" ? "Seq" : s == "Slip" ? "Slip" : "List";
        case VT::Hash:  if (hashKind == "Pod" && hash && hash->count("podclass")) return hash->at("podclass").s;
                        return (hashKind == "Date" || hashKind == "DateTime") && hash ? dateGist(*hash, hashKind == "Date")
                             : (hashKind.empty() ? "Hash" : hashKind);
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

std::string strSucc(const std::string& s) {
    if (s.empty()) return "1";
    std::string r = s;
    int pos = (int)r.size() - 1;
    for (; pos >= 0; --pos) {
        char c = r[pos];
        if (c >= '0' && c <= '9') { if (c != '9') { r[pos] = c + 1; return r; } r[pos] = '0'; }
        else if (c >= 'a' && c <= 'z') { if (c != 'z') { r[pos] = c + 1; return r; } r[pos] = 'a'; }
        else if (c >= 'A' && c <= 'Z') { if (c != 'Z') { r[pos] = c + 1; return r; } r[pos] = 'A'; }
        else break; // non-alnum stops the carry
    }
    char k = r[pos + 1];
    char ins = (k == '0') ? '1' : (k == 'a') ? 'a' : (k == 'A') ? 'A' : '1';
    r.insert(pos + 1, 1, ins);
    return r;
}

std::string strPred(const std::string& s, bool& ok) {
    ok = true;
    std::string r = s;
    int pos = (int)r.size() - 1;
    for (; pos >= 0; --pos) {
        char c = r[pos];
        if (c >= '0' && c <= '9') { if (c != '0') { r[pos] = c - 1; return r; } r[pos] = '9'; }
        else if (c >= 'a' && c <= 'z') { if (c != 'a') { r[pos] = c - 1; return r; } r[pos] = 'z'; }
        else if (c >= 'A' && c <= 'Z') { if (c != 'A') { r[pos] = c - 1; return r; } r[pos] = 'Z'; }
        else break;
    }
    ok = false; // borrowed past the start
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
    if (a.t == VT::Pair && b.t == VT::Pair) {
        int k = valueCmp(Value::str(a.s), Value::str(b.s));
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
