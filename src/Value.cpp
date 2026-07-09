#include "Value.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace rakupp {

static std::string dateGist(const std::map<std::string, Value>& h, bool isDate) {
    auto f = [&](const char* k) { auto it = h.find(k); return it != h.end() ? it->second.toInt() : 0; };
    char buf[40];
    if (isDate) std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lld", f("year"), f("month"), f("day"));
    else std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld",
                       f("year"), f("month"), f("day"), f("hour"), f("minute"), f("second"));
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
        case VT::Num:  return (long long)n;
        case VT::Rat:  { if (!ratN || !ratD || ratD->isZero()) return 0; BigInt q, r; BigInt::divmod(*ratN, *ratD, q, r); return q.toLL(); }
        case VT::Str:  { try { return std::stoll(s); } catch (...) { return 0; } }
        case VT::Match: { try { return std::stoll(s); } catch (...) { return 0; } } // matched text as a number
        case VT::Array: return arr ? (long long)arr->size() : 0;
        case VT::Hash:
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
        case VT::Rat:  return (ratN && ratD && !ratD->isZero()) ? ratN->toDouble() / ratD->toDouble() : 0.0;
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
        case VT::Rat:  return (ratN && ratD) ? ratToStr(*ratN, *ratD) : "0";
        case VT::Str:  return s;
        case VT::Type: return "(" + s + ")";
        case VT::Pair: return s + "\t" + (pairVal ? pairVal->toStr() : "");
        case VT::Range: {
            std::ostringstream os;
            os << rFrom << ".." << (rExTo ? "^" : "") << rTo;
            return os.str();
        }
        case VT::Array: {
            std::string out;
            if (arr) for (size_t k = 0; k < arr->size(); k++) {
                if (k) out += " ";
                out += (*arr)[k].toStr();
            }
            return out;
        }
        case VT::Hash: {
            if (hashKind == "Format" && hash && hash->count("fmt")) return hash->at("fmt").toStr();
            if ((hashKind == "Date" || hashKind == "DateTime") && hash) return dateGist(*hash, hashKind == "Date");
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
            return obj && obj->cls ? obj->cls->name + "<obj>" : "Object";
        case VT::Regex: return s;
        case VT::Match: return s;
    }
    return "";
}

std::string Value::gist() const {
    if (!enumName.empty()) return enumName;
    switch (t) {
        case VT::Nil:  return "Nil";
        case VT::Any:  return "(Any)";
        case VT::Type: return "(" + (ofType.empty() ? s : s + "[" + ofType + "]") + ")";
        case VT::Array: {
            std::string out = isList ? "(" : "[";
            if (arr) for (size_t k = 0; k < arr->size(); k++) {
                if (k) out += " ";
                out += (*arr)[k].gist();
            }
            return out + (isList ? ")" : "]");
        }
        case VT::Pair: return s + " => " + (pairVal ? pairVal->gist() : "");
        case VT::Str:  return s;
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
        case VT::Num:  return "Num";
        case VT::Complex: return "Complex";
        case VT::Str:  return hashKind == "IO" ? "IO::Path" : hashKind == "Version" ? "Version" : hashKind == "Blob" ? "Blob" : "Str";
        case VT::Array:
            if (s == "Uni" || s == "NFC" || s == "NFD" || s == "NFKC" || s == "NFKD") return s;
            return (isList && s == "Seq") ? "Seq" : "Array";
        case VT::Hash:  if (hashKind == "Pod" && hash && hash->count("podclass")) return hash->at("podclass").s;
                        return (hashKind == "Date" || hashKind == "DateTime") && hash ? dateGist(*hash, hashKind == "Date")
                             : (hashKind.empty() ? "Hash" : hashKind);
        case VT::Code:  return "Sub";
        case VT::Rat:   return fatRat ? "FatRat" : "Rat";
        case VT::Range: return "Range";
        case VT::Pair:  return "Pair";
        case VT::Type:  return ofType.empty() ? s : s + "[" + ofType + "]";
        case VT::Whatever: return "Whatever";
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
        double x = a.toNum(), y = b.toNum();
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
    std::string x = a.toStr(), y = b.toStr();
    return x < y ? -1 : x > y ? 1 : 0;
}

} // namespace rakupp
