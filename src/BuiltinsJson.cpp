// BuiltinsJson.cpp — the JSON codec's reader: whitespace and JSONC comments, strings, numbers, values; and JSON Lines
//
// One of the parts BuiltinsParts.h lists; what they share is declared there.
// The writer (jfEncode/jfEscape) and the two entry bodies stay in Builtins.cpp.
#include "BuiltinsParts.h"
#include <numeric>
#include <algorithm>
#include <cstring>

namespace rakupp {

void jsonSkipWs(std::string_view s, size_t& i, const JsonCfg& cfg) {
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
static bool jsonParseString(std::string_view s, size_t& i, std::string& out) {
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
bool jsonParseValue(std::string_view s, size_t& i, Value& out, JsonCfg cfg) {
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
                out = Value::number(cnum::stod(std::string(s.substr(st, i - st))));
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
                out.setRatParts(BigInt(v / g), BigInt(den / g));
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

// ---- JSON Lines: from-jsonl, to-jsonl (docs/dev/plans/JSONL-PLAN.md) ----------
//
// What JSON::Native::Raku's pair does — from-json for each line of
// `$source.lines`, to-json(:!pretty) ~ "\n" for each value — in one pass over
// the window: no Str per line, no Raku frame per line, the decode skipped
// where the bytes already are the text. Anything else goes to `uncovered`
// whole, and the Raku pair answers it exactly, line number and all.

// The bytes of a Blob that decodes as UTF-8, as they are: pure ASCII is
// already valid, BOM-free and NFC, which is everything .decode would do to it.
static bool jsonlAscii(std::string_view s) {
    unsigned char hi = 0;
    for (unsigned char c : s) hi |= c;   // one pass the compiler vectorizes
    return hi < 0x80;
}

// An IO::Path or IO::Handle argument is READ, in place: replaced by what
// `slurp` gives — its text, or with `bin` a path's bytes — so the parse, and
// any hand-over to the Raku fallback, see the contents and never the name.
// from-json("doc.json".IO) used to hand the path to the fallback, whose
// Str() parsed the name "doc.json" (rakupp issue 267). In place matters for a
// handle, which reads once: the fallback gets what was read, and the handle is
// left open for its owner. (The tags are DataCsv.cpp's: an IO::Path is a Str
// tagged "IO", an IO::Handle a Hash tagged "FileHandle".)
void jsonReadIoArg(Interpreter& I, Value& v, bool bin) {
    const bool path = v.t == VT::Str && v.hashKind == "IO";
    if (!path && !(v.t == VT::Hash && v.hashKind == "FileHandle")) return;
    ValueList sa; sa.push_back(v);
    if (bin && path) { Value p = Value::pair("bin", Value::boolean(true)); p.namedArg = true; sa.push_back(p); }
    v = I.callBuiltin("slurp", std::move(sa));
}

static thread_local std::string jsonlWhy;
Value jsonFromJsonlBody(Interpreter& I, ValueList& a, const JsonUncovered& uncovered) {
    const Value* src = nullptr;
    JsonCfg cfg;
    for (auto& x : a) {
        if (x.t == VT::Pair && x.namedArg) {
            if (x.s == "immutable") cfg.immutable = x.pairVal() && x.pairVal()->truthy();
            else return uncovered("no adverb of that name");
        }
        else if (!src) { jsonReadIoArg(I, x, true); src = &x; }   // a path or handle is READ
        else return uncovered("more than one positional argument");
    }
    if (!src || src->t != VT::Str) return uncovered("expected a Blob or a Str");
    const std::string& kind = src->hashKind;
    if (kind.empty() && !src->enumName().empty()) return uncovered("an enum's Str is the Raku pair's");
    Value decoded;
    std::string_view s = src->s.str();
    if (!kind.empty()) {
        if (kind != "Blob" && kind != "Buf" && kind != "blob8" && kind != "buf8" && kind != "utf8")
            return uncovered("expected a Blob or a Str");
        if (!jsonlAscii(s)) {
            decoded = I.methodCall(*src, "decode", ValueList{Value::str("utf-8")});
            s = decoded.s.str();
        }
    }
    Value out = Value::array();
    out.isList = cfg.immutable;
    ValueList& vals = *out.arr();
    vals.reserve(std::count(s.begin(), s.end(), '\n') + 1);
    const size_t n = s.size();
    size_t pos = 0, lineNo = 0;
    while (pos < n) {
        // the line ends where .lines ends it: "\n", "\r\n" or a lone "\r"
        const char* nl = (const char*)std::memchr(s.data() + pos, '\n', n - pos);
        size_t end = nl ? (size_t)(nl - s.data()) : n;
        size_t next = nl ? end + 1 : n;
        if (const char* cr = (const char*)std::memchr(s.data() + pos, '\r', end - pos)) {
            end = (size_t)(cr - s.data());
            next = end + 1 < n && s[end + 1] == '\n' ? end + 2 : end + 1;
        }
        lineNo++;
        std::string_view line = s.substr(pos, end - pos);
        pos = next;
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
        if (i == line.size()) continue;   // blank: spaces and tabs only
        Value v;
        const bool parsed = jsonParseValue(line, i, v, cfg);
        if (parsed) jsonSkipWs(line, i, cfg);
        if (!parsed || i != line.size()) {
            jsonlWhy = std::string(parsed ? "trailing content after the value" : "malformed JSON") +
                       " at line " + std::to_string(lineNo) + ", column " + std::to_string(i + 1);
            return uncovered(jsonlWhy.c_str());
        }
        vals.push_back(std::move(v));
    }
    return out;
}

Value jsonToJsonlBody(Interpreter& I, ValueList& a, const JsonUncovered& uncovered) {
    const Value* list = nullptr;
    bool sorted = false, enums = false;
    for (auto& x : a) {
        if (x.t == VT::Pair && x.namedArg) {
            const bool tv = x.pairVal() && x.pairVal()->truthy();
            if (x.s == "pretty") { if (tv) return uncovered(":pretty does not apply"); }
            else if (x.s == "enums-as-value") enums = tv;
            else if (x.s == "sorted-keys") {
                if (x.pairVal() && x.pairVal()->t == VT::Code) return uncovered("a key comparator");
                sorted = tv;
            }
            else return uncovered("no adverb of that name");
        }
        else if (!list) list = &x;
        else return uncovered("more than one positional argument");
    }
    // a lazy list is the Raku pair's to run out, as anything not a list is
    if (!list || list->t != VT::Array || list->ext()) return uncovered("expected a list");
    std::string text;
    if (!jsonWriteLines(I, *list->arr(), sorted, enums, text)) return uncovered("a value to-json does not write");
    return Value::str(std::move(text));
}

// ---- what `use JSON::Native` and `use Data::Native <json>` hand out ---------
//
// The codec as a routine whose fallback is JSON::Native::Raku — the pure-Raku
// half of JSON::Native — so a call the codec does not cover (an adverb it does
// not take, a type outside the ladder, a document it refuses) is answered
// exactly as JSON::Fast answers it, and nothing has to be installed for that.
// The fallback is loaded by the first call that needs it; a program whose
// calls the codec answers never reads it. (dataNativeUse, InterpreterModules.cpp.)
//
// It is the copy this rakupp BUNDLES, ahead of an installed or -I one
// (bundledFirst_; a compile embeds the same copy, collectModuleGraph). Those
// can only be older: a newer JSON::Native, installed or on -I, makes the
// engine step aside from the `use` altogether. And an older one lacks what
// newer codecs hand over — JSON::Native 0.1.0, installed for Rakudo, has no
// from-jsonl, and was what every from-jsonl fallback found.
static Value jsonRakuFallback(Interpreter& I, const std::string& name, ValueList& args) {
    const std::string qual = "&JSON::Native::Raku::" + name;
    Value* f = I.global_ ? I.global_->find(qual) : nullptr;
    if (!f || f->t != VT::Code) {
        struct G { std::string& s; ~G() { s.clear(); } } g{I.bundledFirst_};
        I.bundledFirst_ = "JSON::Native::Raku";
        I.loadModule("JSON::Native::Raku", {}, /*doImport=*/false, /*quiet=*/false, "", /*requireForm=*/true);
        f = I.global_ ? I.global_->find(qual) : nullptr;
    }
    if (!f || f->t != VT::Code)
        throw RakuError{Value::typeObj("X::AdHoc"),
                        std::string("JSON::Native: this call needs JSON::Native::Raku, which did not load")};
    Value fn = *f;
    return I.callCallable(fn, args, nullptr, false, /*arityCheck=*/true);   // the caller's call, checked as one
}
// Rakupp::Internals::JSON.to-json (and its Rakudo alias): the same pair.
Value jsonExactToJson(Interpreter& I, ValueList& a) {
    return jsonToJsonBody(I, a, [&](const char*) { return jsonRakuFallback(I, "to-json", a); });
}
// `name` is from-json, to-json, from-jsonl or to-jsonl. `defaults` are
// JSON::Fast's import options (`use JSON::Native <!pretty>`): a named argument
// the caller did not pass is added with its default.
Value jsonExactRoutine(const std::string& name, const std::map<std::string, bool>& defaults) {
    using Body = Value (*)(Interpreter&, ValueList&, const JsonUncovered&);
    const Body body = name == "from-json"  ? jsonFromJsonBody  : name == "to-json" ? jsonToJsonBody
                    : name == "from-jsonl" ? jsonFromJsonlBody : jsonToJsonlBody;
    Value w; w.t = VT::Code; w.setCode(makePayload<Callable>());
    w.code()->ciM().name = name;
    w.code()->ciM().builtin = [name, body, defaults](Interpreter& I, ValueList& args) -> Value {
        ValueList a = args;
        for (auto& d : defaults) {
            bool given = false;
            for (auto& x : a) if (x.t == VT::Pair && x.namedArg && x.s == d.first) { given = true; break; }
            if (!given) { Value p = Value::pair(d.first, Value::boolean(d.second)); p.namedArg = true; a.push_back(p); }
        }
        return body(I, a, [&](const char*) { return jsonRakuFallback(I, name, a); });
    };
    return w;
}

} // namespace rakupp
