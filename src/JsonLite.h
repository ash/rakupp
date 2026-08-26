// JsonLite.h — the small JSON the protocol servers speak.
//
// A value, a writer and a depth-capped parser: exactly as much JSON as
// `rakupp --mcp` (JSON-RPC 2.0) and `rakupp --jupyter` (the Jupyter wire
// protocol) need, and no more. It lives in a header of its own because both
// speak JSON and neither may pull in a library for it — a third-party
// dependency in the binary is the one thing this project does not do.
//
// This is NOT the engine's JSON: `to-json`/`from-json` on the Raku side are
// Builtins.cpp's, over Value. Nothing here touches the interpreter.
#pragma once

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace rakupp::json {

struct Json {
    enum class T { Null, Bool, Int, Num, Str, Arr, Obj };
    T t = T::Null;
    bool b = false;
    long long i = 0;
    double n = 0;
    std::string s;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj; // insertion order kept

    static Json null() { return Json{}; }
    static Json boolean(bool v) { Json j; j.t = T::Bool; j.b = v; return j; }
    static Json integer(long long v) { Json j; j.t = T::Int; j.i = v; return j; }
    static Json number(double v) { Json j; j.t = T::Num; j.n = v; return j; }
    static Json str(std::string v) { Json j; j.t = T::Str; j.s = std::move(v); return j; }
    static Json array() { Json j; j.t = T::Arr; return j; }
    static Json object() { Json j; j.t = T::Obj; return j; }

    Json& set(const std::string& k, Json v) {
        obj.emplace_back(k, std::move(v));
        return *this;
    }
    const Json* find(const std::string& k) const {
        for (auto& kv : obj)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
    // The common "string field or fallback" read.
    std::string getStr(const std::string& k, const std::string& dflt = "") const {
        const Json* v = find(k);
        return v && v->t == T::Str ? v->s : dflt;
    }
};

// The shortest %g that strtod round-trips — so 3.88 prints as 3.88, not as
// seventeen digits of it.
inline std::string numToStr(double d) {
    char buf[40];
    for (int prec = 15; prec <= 17; prec++) {
        std::snprintf(buf, sizeof buf, "%.*g", prec, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    return buf;
}

inline void dumpStr(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char u[8];
                    std::snprintf(u, sizeof u, "\\u%04x", c);
                    out += u;
                }
                else { out += (char)c; }
        }
    }
    out += '"';
}

inline void dump(const Json& j, std::string& out) {
    switch (j.t) {
        case Json::T::Null: out += "null"; break;
        case Json::T::Bool: out += j.b ? "true" : "false"; break;
        case Json::T::Int:  out += std::to_string(j.i); break;
        case Json::T::Num:
            // JSON has no spelling for these; a string is the honest crossing,
            // matching how the bindings document odd values (they stringify).
            if (std::isnan(j.n)) { out += "\"NaN\""; }
            else if (std::isinf(j.n)) { out += j.n > 0 ? "\"Inf\"" : "\"-Inf\""; }
            else { out += numToStr(j.n); }
            break;
        case Json::T::Str: dumpStr(j.s, out); break;
        case Json::T::Arr: {
            out += '[';
            bool first = true;
            for (auto& v : j.arr) {
                if (!first) out += ',';
                first = false;
                dump(v, out);
            }
            out += ']';
            break;
        }
        case Json::T::Obj: {
            out += '{';
            bool first = true;
            for (auto& kv : j.obj) {
                if (!first) out += ',';
                first = false;
                dumpStr(kv.first, out);
                out += ':';
                dump(kv.second, out);
            }
            out += '}';
            break;
        }
    }
}

inline std::string dumps(const Json& j) {
    std::string out;
    dump(j, out);
    return out;
}

// Recursive descent over one line. Depth-capped: a hostile client must not be
// able to blow the native stack with ten thousand '['.
struct Parser {
    const char* p;
    const char* end;
    int depth = 0;
    bool ok = true;

    explicit Parser(const std::string& s) : p(s.data()), end(s.data() + s.size()) {}

    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++; }
    bool lit(const char* w, size_t n) {
        if ((size_t)(end - p) < n || std::strncmp(p, w, n) != 0) return false;
        p += n;
        return true;
    }
    void utf8(unsigned cp, std::string& s) {
        if (cp < 0x80) { s += (char)cp; }
        else if (cp < 0x800) {
            s += (char)(0xC0 | (cp >> 6));
            s += (char)(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000) {
            s += (char)(0xE0 | (cp >> 12));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        }
        else {
            s += (char)(0xF0 | (cp >> 18));
            s += (char)(0x80 | ((cp >> 12) & 0x3F));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& v) {
        if (end - p < 4) return false;
        v = 0;
        for (int k = 0; k < 4; k++) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& s) {
        if (p >= end || *p != '"') return false;
        p++;
        while (p < end && *p != '"') {
            unsigned char c = (unsigned char)*p;
            if (c == '\\') {
                p++;
                if (p >= end) return false;
                switch (*p++) {
                    case '"':  s += '"';  break;
                    case '\\': s += '\\'; break;
                    case '/':  s += '/';  break;
                    case 'n':  s += '\n'; break;
                    case 't':  s += '\t'; break;
                    case 'r':  s += '\r'; break;
                    case 'b':  s += '\b'; break;
                    case 'f':  s += '\f'; break;
                    case 'u': {
                        unsigned u;
                        if (!hex4(u)) return false;
                        if (u >= 0xD800 && u <= 0xDBFF && end - p >= 6 &&
                            p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            unsigned lo;
                            if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                            u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(u, s);
                        break;
                    }
                    default: return false;
                }
            }
            else if (c < 0x20) { return false; }
            else {
                s += (char)c;
                p++;
            }
        }
        if (p >= end) return false;
        p++; // closing quote
        return true;
    }
    bool value(Json& j) {
        if (++depth > 128) return false;
        struct Undepth { int& d; ~Undepth() { d--; } } u{depth};
        ws();
        if (p >= end) return false;
        char c = *p;
        if (c == '{') {
            p++;
            j = Json::object();
            ws();
            if (p < end && *p == '}') { p++; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (p >= end || *p++ != ':') return false;
                Json v;
                if (!value(v)) return false;
                j.obj.emplace_back(std::move(k), std::move(v));
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; return true; }
                return false;
            }
        }
        if (c == '[') {
            p++;
            j = Json::array();
            ws();
            if (p < end && *p == ']') { p++; return true; }
            for (;;) {
                Json v;
                if (!value(v)) return false;
                j.arr.push_back(std::move(v));
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; return true; }
                return false;
            }
        }
        if (c == '"') { j = Json::str(""); return string(j.s); }
        if (lit("true", 4))  { j = Json::boolean(true);  return true; }
        if (lit("false", 5)) { j = Json::boolean(false); return true; }
        if (lit("null", 4))  { j = Json::null();         return true; }
        // number
        const char* start = p;
        if (p < end && (*p == '-' || *p == '+')) p++;
        bool isNum = false, frac = false;
        while (p < end) {
            char d = *p;
            if (d >= '0' && d <= '9') { isNum = true; p++; }
            else if (d == '.' || d == 'e' || d == 'E' || d == '+' || d == '-') { frac = true; p++; }
            else { break; }
        }
        if (!isNum) return false;
        std::string tok(start, (size_t)(p - start));
        if (!frac) {
            errno = 0;
            char* rest = nullptr;
            long long v = std::strtoll(tok.c_str(), &rest, 10);
            if (errno == 0 && rest && *rest == '\0') { j = Json::integer(v); return true; }
        }
        j = Json::number(std::strtod(tok.c_str(), nullptr));
        return true;
    }
};

inline bool parse(const std::string& line, Json& out) {
    Parser ps(line);
    if (!ps.value(out)) return false;
    ps.ws();
    return ps.p == ps.end;
}
} // namespace rakupp::json
