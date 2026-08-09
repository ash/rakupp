// Rakupp::JSON — a JSON reader/writer implemented in C++, exposed as a module.
//
// Why this exists, and why it is NOT a faster JSON::Fast. rakupp interprets
// JSON::Fast's Raku source at roughly 440 ms for a 278 KB document where
// Rakudo's JIT runs the same source in 36 ms, and closing that is an
// interpreter problem, not a JSON problem (docs/dev/experiments/IR-EXPERIMENT.md
// measured why). The temptation was to make `use JSON::Fast` silently resolve
// `from-json` here — which would have "won" the benchmark by not running the
// benchmark's code, and quietly forked someone else's module semantics.
//
// So this is its own module under its own name. `Rakupp::` says what it is: a
// program that uses it does not run on Rakudo, and the name says so at the point
// of use rather than in a footnote. JSON::Fast keeps working exactly as before,
// unhooked and uninterrupted.
//
// Measured against the same corpus (tools/bench/diagnose/d800.json, 278 KB):
// ~2.9 ms here against Rakudo's JSON::Fast at 36 ms and rakupp's interpreted
// JSON::Fast at ~440 ms. Most of that came from BigInt's 64-bit fast paths, not
// from this file — before those, this same parser needed 12 ms and 95% of it was
// building Rats.
//
// Semantics are Raku's, not C's: an integer literal is an Int (arbitrary
// precision), a decimal is a Rat like `.Numeric` gives, and an exponent form is
// a Num. Objects come back as Hash and arrays as Array, or Map and List under
// `:immutable`.
#include "Interpreter.h"
#include "Value.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace rakupp {

namespace {

[[noreturn]] void jsonDie(const std::string& msg, size_t pos) {
    throw RakuError{Value::typeObj("X::AdHoc"),
                    "Rakupp::JSON: " + msg + " at position " + std::to_string(pos)};
}

struct JsonParser {
    const char* p;
    const char* begin;
    const char* end;
    bool immutable;

    size_t at() const { return (size_t)(p - begin); }

    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    void need(char c, const char* what) {
        if (p >= end || *p != c) jsonDie(std::string("expected ") + what, at());
        ++p;
    }

    // A JSON string body. `pos` is on the opening quote.
    std::string str() {
        if (p >= end || *p != '"') jsonDie("expected a string", at());
        ++p;
        std::string out;
        const char* run = p;
        for (;;) {
            if (p >= end) jsonDie("unterminated string", at());
            unsigned char c = (unsigned char)*p;
            if (c == '"') break;
            if (c == '\\') {
                out.append(run, (size_t)(p - run));
                ++p;
                if (p >= end) jsonDie("unterminated escape", at());
                char e = *p++;
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u':  out += uEscape(); break;
                    default:
                        jsonDie(std::string("unknown escape \\") + e, at());
                }
                run = p;
            } else if (c < 0x20) {
                // RFC 8259: raw control characters are not allowed in a string.
                jsonDie("unescaped control character in string", at());
            } else {
                ++p;
            }
        }
        out.append(run, (size_t)(p - run));
        ++p; // closing quote
        return out;
    }

    unsigned hex4() {
        if (end - p < 4) jsonDie("truncated \\u escape", at());
        unsigned v = 0;
        for (int i = 0; i < 4; i++) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else jsonDie("bad hex digit in \\u escape", at());
        }
        return v;
    }

    // Returns UTF-8 for one \uXXXX, pairing surrogates as JSON requires.
    std::string uEscape() {
        unsigned cp = hex4();
        if (cp >= 0xD800 && cp < 0xDC00) { // high surrogate: a low one must follow
            if (end - p >= 2 && p[0] == '\\' && p[1] == 'u') {
                const char* save = p;
                p += 2;
                unsigned lo = hex4();
                if (lo >= 0xDC00 && lo < 0xE000)
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                else { p = save; jsonDie("high surrogate not followed by a low one", at()); }
            } else jsonDie("high surrogate not followed by a low one", at());
        } else if (cp >= 0xDC00 && cp < 0xE000) {
            jsonDie("stray low surrogate", at());
        }
        std::string o;
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) {
            o += (char)(0xC0 | (cp >> 6));
            o += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            o += (char)(0xE0 | (cp >> 12));
            o += (char)(0x80 | ((cp >> 6) & 0x3F));
            o += (char)(0x80 | (cp & 0x3F));
        } else {
            o += (char)(0xF0 | (cp >> 18));
            o += (char)(0x80 | ((cp >> 12) & 0x3F));
            o += (char)(0x80 | ((cp >> 6) & 0x3F));
            o += (char)(0x80 | (cp & 0x3F));
        }
        return o;
    }

    // Raku numerics, not C ones: `1` is an Int of arbitrary precision, `1.5` is
    // a Rat (which is what .Numeric gives a decimal literal, and why `0.1 + 0.2
    // == 0.3` holds in Raku), and an exponent form is a Num.
    Value number() {
        const char* s = p;
        if (p < end && (*p == '-' || *p == '+')) ++p;
        bool any = false;
        while (p < end && *p >= '0' && *p <= '9') { ++p; any = true; }
        bool frac = false, expo = false;
        if (p < end && *p == '.') {
            ++p; frac = true;
            bool d = false;
            while (p < end && *p >= '0' && *p <= '9') { ++p; d = true; }
            if (!d) jsonDie("digits expected after the decimal point", at());
        }
        if (p < end && (*p == 'e' || *p == 'E')) {
            ++p; expo = true;
            if (p < end && (*p == '-' || *p == '+')) ++p;
            bool d = false;
            while (p < end && *p >= '0' && *p <= '9') { ++p; d = true; }
            if (!d) jsonDie("digits expected in the exponent", at());
        }
        if (!any) jsonDie("expected a number", at());
        std::string tok(s, (size_t)(p - s));
        if (expo) return Value::number(strtod(tok.c_str(), nullptr));
        if (!frac) {
            // Arbitrary precision: a 30-digit literal is an Int in Raku, so the
            // token goes through BigInt rather than strtoll.
            BigInt b = BigInt::fromString(tok);
            return Value::bigint(b);
        }
        size_t dot = tok.find('.');
        std::string digits = tok.substr(0, dot) + tok.substr(dot + 1);
        long long scale = (long long)(tok.size() - dot - 1);
        BigInt den(1);
        for (long long k = 0; k < scale; k++) den = den * BigInt(10);
        return Value::rat(BigInt::fromString(digits), den);
    }

    Value thing(int depth) {
        // A bound on nesting, so a hostile document gets an exception instead of
        // the C++ stack. JSON has no legitimate use for depths near this.
        if (depth > 512) jsonDie("nesting too deep", at());
        ws();
        if (p >= end) jsonDie("unexpected end of input", at());
        switch (*p) {
            case '{': {
                ++p;
                Value h = Value::makeHash();
                if (immutable) h.hashKind = "Map";
                ws();
                if (p < end && *p == '}') { ++p; return h; }
                for (;;) {
                    ws();
                    std::string k = str();
                    ws();
                    need(':', "':' after an object key");
                    (*h.hash)[k] = thing(depth + 1);
                    ws();
                    if (p < end && *p == ',') { ++p; continue; }
                    need('}', "',' or '}' in an object");
                    return h;
                }
            }
            case '[': {
                ++p;
                Value a = Value::array();
                if (immutable) a.isList = true;
                ws();
                if (p < end && *p == ']') { ++p; return a; }
                for (;;) {
                    a.arr->push_back(thing(depth + 1));
                    ws();
                    if (p < end && *p == ',') { ++p; continue; }
                    need(']', "',' or ']' in an array");
                    return a;
                }
            }
            case '"': return Value::str(str());
            case 't':
                if (end - p >= 4 && !memcmp(p, "true", 4)) { p += 4; return Value::boolean(true); }
                jsonDie("expected 'true'", at());
            case 'f':
                if (end - p >= 5 && !memcmp(p, "false", 5)) { p += 5; return Value::boolean(false); }
                jsonDie("expected 'false'", at());
            case 'n':
                if (end - p >= 4 && !memcmp(p, "null", 4)) { p += 4; return Value::any(); }
                jsonDie("expected 'null'", at());
            default: return number();
        }
    }
};

// ---- writing -------------------------------------------------------------

void jsonEscape(const std::string& s, std::string& out) {
    out += '"';
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    static const char* hexd = "0123456789abcdef";
                    out += "\\u00";
                    out += hexd[(c >> 4) & 0xF];
                    out += hexd[c & 0xF];
                } else out += (char)c; // UTF-8 passes through unescaped
        }
    }
    out += '"';
}

void writeValue(const Value& v, std::string& out, bool pretty, int spacing, int indent);

void writeIndent(std::string& out, bool pretty, int spacing, int depth) {
    if (!pretty) return;
    out += '\n';
    out.append((size_t)(spacing * depth), ' ');
}

void writeValue(const Value& v, std::string& out, bool pretty, int spacing, int indent) {
    switch (v.t) {
        case VT::Nil:
        case VT::Any:
        case VT::Type:  out += "null"; return;
        case VT::Bool:  out += v.b ? "true" : "false"; return;
        case VT::Int:
        case VT::Rat:
        case VT::Num: {
            // A Rat writes as a decimal and a Num as its Raku form; both round-trip
            // through the reader above.
            std::string n = v.toStr();
            // Raku renders these as words; JSON has no spelling for them.
            if (n == "Inf" || n == "-Inf" || n == "NaN") { out += "null"; return; }
            out += n;
            return;
        }
        case VT::Array: {
            if (!v.arr || v.arr->empty()) { out += "[]"; return; }
            out += '[';
            bool first = true;
            for (auto& e : *v.arr) {
                if (!first) out += ',';
                first = false;
                writeIndent(out, pretty, spacing, indent + 1);
                writeValue(e, out, pretty, spacing, indent + 1);
            }
            writeIndent(out, pretty, spacing, indent);
            out += ']';
            return;
        }
        case VT::Hash: {
            if (!v.hash || v.hash->empty()) { out += "{}"; return; }
            out += '{';
            bool first = true;
            // Keys come out sorted because the storage is ordered — so our output
            // is deterministic, which is a property worth having in a serializer
            // (diffable fixtures, reproducible builds) and one Rakudo's hash order
            // cannot offer.
            for (auto& kv : *v.hash) {
                if (!first) out += ',';
                first = false;
                writeIndent(out, pretty, spacing, indent + 1);
                jsonEscape(kv.first, out);
                out += pretty ? ": " : ":";
                writeValue(kv.second, out, pretty, spacing, indent + 1);
            }
            writeIndent(out, pretty, spacing, indent);
            out += '}';
            return;
        }
        case VT::Pair: {
            out += '{';
            writeIndent(out, pretty, spacing, indent + 1);
            jsonEscape(v.s, out);
            out += pretty ? ": " : ":";
            writeValue(v.pairVal ? *v.pairVal : Value::any(), out, pretty, spacing, indent + 1);
            writeIndent(out, pretty, spacing, indent);
            out += '}';
            return;
        }
        default: jsonEscape(v.toStr(), out); return; // Str and anything stringy
    }
}

Value namedArg(ValueList& a, const char* key, Value dflt) {
    for (auto& v : a)
        if (v.t == VT::Pair && v.namedArg && v.s == key)
            return v.pairVal ? *v.pairVal : Value::boolean(true);
    return dflt;
}

} // namespace

// `use Rakupp::JSON` installs these into the using scope, the same way a
// module's exports land there — so they are opt-in by name and cannot shadow
// JSON::Fast's `from-json`, which is a module sub and wins over anything here.
void installRakuppJson(Env* env) {
    Value fromJson = Value::closure([](ValueList& a) -> Value {
        if (a.empty()) return Value::any();
        std::string text = a[0].toStr();
        bool immutable = rtIsDefined(namedArg(a, "immutable", Value::boolean(false)))
                             ? namedArg(a, "immutable", Value::boolean(false)).truthy()
                             : false;
        JsonParser ps{text.data(), text.data(), text.data() + text.size(), immutable};
        Value out = ps.thing(0);
        ps.ws();
        if (ps.p != ps.end) jsonDie("trailing content after the document", ps.at());
        return out;
    });
    Value toJson = Value::closure([](ValueList& a) -> Value {
        if (a.empty()) return Value::str("null");
        bool pretty = namedArg(a, "pretty", Value::boolean(true)).truthy();
        long long spacing = namedArg(a, "spacing", Value::integer(2)).toInt();
        std::string out;
        writeValue(a[0], out, pretty, (int)spacing, 0);
        return Value::str(out);
    });
    env->define("&from-json", fromJson);
    env->define("&to-json", toJson);
}

// The RAKUPP_NATIVE_JSON=1 path, used over `use JSON::Fast`. It replaces
// `from-json` ONLY. `to-json` stays the module's own, because a serializer's
// output is a contract — spacing, key order, how a Rat or a Version renders —
// and silently swapping one accelerator in for another is how a round-trip test
// starts failing for reasons nobody can find. Parsing is where the time is;
// this is the part that is safe to substitute.
void installRakuppJsonOver(Env* env) {
    Env tmp;
    installRakuppJson(&tmp);
    if (Value* f = tmp.find("&from-json")) env->define("&from-json", *f);
}

} // namespace rakupp
