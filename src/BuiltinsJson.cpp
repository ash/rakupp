// BuiltinsJson.cpp — the JSON codec's reader: whitespace and JSONC comments, strings, numbers, values
//
// One of the parts BuiltinsParts.h lists; what they share is declared there.
// The writer (jfEncode/jfEscape) and the two entry bodies stay in Builtins.cpp.
#include "BuiltinsParts.h"
#include <numeric>

namespace rakupp {

void jsonSkipWs(const std::string& s, size_t& i, const JsonCfg& cfg) {
    for (;;) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
        if (!cfg.jsonc || i + 1 >= s.size() || s[i] != '/') return;
        // A comment that runs into the end of the text — `//` with no newline
        // after it, `/*` with no `*/` — leaves the position one PAST the end,
        // where every caller reads "no more input" and the document fails.
        // That is JSON::Fast's answer (its nom-comment overshoots there, so
        // after a document such a comment is additional content), and the
        // answer this codec has to give, since it stands in for it.
        if (s[i + 1] == '/') {
            i += 2;
            while (i < s.size() && s[i] != '\n') i++;
            if (i >= s.size()) { i = s.size() + 1; return; }
        }
        else if (s[i + 1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) i++;
            if (i + 1 >= s.size()) { i = s.size() + 1; return; }
            i += 2;
        }
        else return; // a lone '/' is the next token's parse error, not whitespace
    }
}
static bool jsonParseString(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    i++;
    out.clear();
    bool uEscaped = false, nonAscii = false;
    while (i < s.size() && s[i] != '"') {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20) return false; // raw control characters must arrive \u-escaped
        i++;
        if (c != '\\') { if (c >= 0x80) nonAscii = true; out += (char)c; continue; }
        if (i >= s.size()) return false;
        char e = s[i++];
        switch (e) {
            case 'n': out += '\n'; break;  case 't': out += '\t'; break;
            case 'r': out += '\r'; break;  case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;  case '/': out += '/';  break;
            case '"': out += '"';  break;  case '\\': out += '\\'; break;
            case 'u': {
                uEscaped = true;
                auto hex4 = [&](size_t p, unsigned& v) {
                    if (p + 4 > s.size()) return false;
                    v = 0;
                    for (int k = 0; k < 4; k++) {
                        char h = s[p + k]; v <<= 4;
                        if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                        else return false;
                    }
                    return true;
                };
                unsigned cp;
                if (!hex4(i, cp)) return false;
                i += 4;
                // a high surrogate followed by \u-escaped low surrogate is ONE
                // astral character (03-unicode.t round-trips flag emoji this way)
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() &&
                    s[i] == '\\' && s[i + 1] == 'u') {
                    unsigned lo;
                    if (hex4(i + 2, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                }
                if (cp >= 0xD800 && cp <= 0xDFFF) return false; // a LONE surrogate is not a character — die, like chr() would
                if (cp >= 0x80) nonAscii = true;
                out += cpToU8(cp);
                break;
            }
            default: return false; // JSON::Fast dies on unknown escapes; so do we
        }
    }
    if (i >= s.size()) return false;
    i++; // closing quote
    // Text assembled around an escape can put a combining mark after a
    // character it did not follow in the source ("a\u0301"), or spell one that
    // is not in composed form at all (U+1D15E, which NFC splits in two). Every
    // Str Rakudo — and so JSON::Fast — hands back is NFC, and a Str is not
    // normalised here on construction, so without this the two would look the
    // same and compare unequal. Only a string that had a \u escape AND holds
    // non-ASCII can need it: the text between escapes is the source's own,
    // and the short escapes stand for characters nothing composes with.
    if (uEscaped && nonAscii && uniNfcQuickCheckUtf8(out.data(), out.size()) != 1) {
        std::string norm;
        for (uint32_t cp : uniNormalize(utf8cp(out), 1)) norm += cpToU8(cp);
        out = std::move(norm);
    }
    return true;
}
bool jsonParseValue(const std::string& s, size_t& i, Value& out, JsonCfg cfg) {
    if (++cfg.depth > 20000) return false; // ~3 MB of C++ frames; no real document nests this deep
    jsonSkipWs(s, i, cfg);
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '"') { std::string str; if (!jsonParseString(s, i, str)) return false; out = Value::str(std::move(str)); return true; }
    if (c == '{') {
        i++; out = Value::makeHash();
        if (cfg.immutable) out.hashKind = "Map";
        jsonSkipWs(s, i, cfg);
        if (i < s.size() && s[i] == '}') { i++; return true; }
        for (;;) {
            jsonSkipWs(s, i, cfg);
            std::string key; if (!jsonParseString(s, i, key)) return false;
            jsonSkipWs(s, i, cfg);
            if (i >= s.size() || s[i] != ':') return false; i++;
            Value v; if (!jsonParseValue(s, i, v, cfg)) return false;
            (*out.hash())[key] = std::move(v);
            jsonSkipWs(s, i, cfg);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; return true; }
            return false;
        }
    }
    if (c == '[') {
        i++; out = Value::array();
        out.isList = cfg.immutable; // List under :immutable, Array otherwise — is-deeply tells them apart
        jsonSkipWs(s, i, cfg);
        if (i < s.size() && s[i] == ']') { i++; return true; }
        for (;;) {
            Value v; if (!jsonParseValue(s, i, v, cfg)) return false;
            out.arr()->push_back(std::move(v));
            jsonSkipWs(s, i, cfg);
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; return true; }
            return false;
        }
    }
    if (s.compare(i, 4, "true") == 0)  { i += 4; out = Value::boolean(true);  return true; }
    if (s.compare(i, 5, "false") == 0) { i += 5; out = Value::boolean(false); return true; }
    if (s.compare(i, 4, "null") == 0)  { i += 4; out = Value::any();           return true; }
    // number: scan the same loose token JSON::Fast does, then type it exactly
    // like Str.Numeric does — that is what the real module calls on the token.
    if (c != '-' && !ascii::isdigit((unsigned char)c)) return false; // JSON has no leading '+'
    size_t st = i;
    if (c == '-') i++;
    while (i < s.size() && (ascii::isdigit((unsigned char)s[i]) || s[i] == '.' ||
                            s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) i++;
    if (i == st) return false;
    // The shapes nearly every document is made of — a JSON integer, a plain
    // decimal, an exponent form — are typed here, by the same constructors
    // numifyStr reaches for them: an integer of up to 18 digits is the
    // Value::integer its stoll would make, a decimal of up to 18 digits the
    // Value::rat(digits, 10**k) its float arm builds, an exponent form the Num
    // of the same cnum::stod. numifyStr is the whole of Str.Numeric — radix
    // forms, `1/3`, `2*10**3`, Unicode digits, half a dozen std::regex matches
    // — and costs a JSON number 5-30x the extension's direct typing. Every
    // other token, and every longer one, still goes through it, so what a
    // token means is decided in one place.
    {
        const char* p = s.data() + st;
        const char* e = s.data() + i;
        const bool neg = *p == '-';
        if (neg) p++;
        const char* ip = p;
        while (p < e && ascii::isdigit((unsigned char)*p)) p++;
        const size_t intDigits = (size_t)(p - ip);
        size_t fracDigits = 0;
        bool frac = false, expo = false, ok = intDigits > 0;
        if (ok && p < e && *p == '.') {
            frac = true;
            const char* fp = ++p;
            while (p < e && ascii::isdigit((unsigned char)*p)) p++;
            fracDigits = (size_t)(p - fp);
            ok = fracDigits > 0;
        }
        if (ok && p < e && (*p == 'e' || *p == 'E')) {
            expo = true;
            if (++p < e && (*p == '+' || *p == '-')) p++;
            const char* xp = p;
            while (p < e && ascii::isdigit((unsigned char)*p)) p++;
            ok = p > xp;
        }
        if (ok && p == e) {
            if (expo) {
                out = Value::number(cnum::stod(s.substr(st, i - st)));
                return true;
            }
            if (intDigits + fracDigits <= 18) {
                long long v = 0;
                for (const char* q = ip; q < e; q++)
                    if (*q != '.') v = v * 10 + (*q - '0');
                if (neg) v = -v;
                if (!frac) { out = Value::integer(v); return true; }
                long long den = 1;
                for (size_t k = 0; k < fracDigits; k++) den *= 10;
                // reduced here, in machine words: what Value::rat would make
                // of (v, den) — a normalised Rat has one representation —
                // without its BigInt gcd and two BigInt divisions per number
                const long long g = std::gcd(v < 0 ? -v : v, den);
                out = Value();
                out.t = VT::Rat;
                ValueExt& x = out.xw();
                x.ratN = std::make_shared<BigInt>(BigInt(v / g));
                x.ratD = std::make_shared<BigInt>(BigInt(den / g));
                return true;
            }
            // longer than a machine word: numifyStr's own construction, step
            // for step — an integer through stoll or else BigInt, a decimal as
            // its digits over 10**k — without its regexes on the way there
            std::string digits;
            digits.reserve((size_t)(e - s.data() - st));
            if (neg) digits += '-';
            for (const char* q = ip; q < e; q++) if (*q != '.') digits += *q;
            if (!frac) {
                try { out = Value::integer(std::stoll(digits)); }
                catch (...) { out = Value::bigint(BigInt::fromString(digits)); }
                return true;
            }
            BigInt den(1);
            for (size_t k = 0; k < fracDigits; k++) den = den * BigInt(10);
            out = Value::rat(BigInt::fromString(digits), std::move(den));
            return true;
        }
    }
    out = numifyStr(s.substr(st, i - st));
    return out.t != VT::Any && out.t != VT::Nil; // "5." scans but does not numify — die like .Numeric
}

} // namespace rakupp
