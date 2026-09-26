#include "CNumeric.h"
#include "AsciiCtype.h"
#include "Pod.h"
#include "Interpreter.h"  // numifyStr — the literal numeric ladder
#include <cctype>
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include "BigInt.h"
#include "Unicode.h"
#include <map>
#include <set>
#include <functional>
#include <algorithm>

namespace rakupp {

static std::string ltrim(const std::string& s) {
    size_t i = 0; while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    return s.substr(i);
}
static std::string strip(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && ascii::isspace((unsigned char)s[a])) a++;
    while (b > a && ascii::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}
// collapse runs of whitespace to single spaces, trimmed
static std::string collapseWs(const std::string& s) {
    std::string out; bool sp = false;
    for (char c : s) {
        if (ascii::isspace((unsigned char)c)) { sp = true; }
        else { if (sp && !out.empty()) out += ' '; sp = false; out += c; }
    }
    return out;
}
static std::string firstWord(const std::string& s) {
    size_t e = s.find_first_of(" \t");
    return e == std::string::npos ? s : s.substr(0, e);
}
static int indentOf(const std::string& s) {
    int n = 0; for (char c : s) { if (c == ' ' || c == '\t') n++; else break; } return n;
}

static Value mkPod(const std::string& cls) {
    Value v = Value::makeHash(); v.hashKind = "Pod";
    (*v.hash())["podclass"] = Value::str(cls);
    (*v.hash())["contents"] = Value::array();
    return v;
}
// ---------- formatting codes: B<bold> C<code> L<text|url> Z<comment> … ----------
//
// Inside paragraph text an uppercase letter directly before `<` opens a
// formatting code, closed by the matching `>`; `<<…>>` (any run) or `«…»`
// delimit text that itself holds angle brackets, and every code but the
// literal ones (C, V) nests. Rakudo makes each a Pod::FormattingCode with
// `.type`, `.contents` and `.meta` (the part after `|` in L<>, D<>, X<>, M<>,
// P<>), and the paragraph's contents alternate Str and code. Before this a
// paragraph was ONE Str, so Pod::Load's `.contents[0].contents[0].type` found
// "No such method 'type' for invocant of type 'Str'" — and every Pod::To::*
// renderer walks the same nodes.
static void parseFormatting(const std::string& s, ValueList& out);

static Value mkFmt(char type, const std::string& inner) {
    Value f = mkPod("Pod::FormattingCode");
    (*f.hash())["type"] = Value::str(std::string(1, type));
    Value meta = Value::array();
    std::string body = inner;
    if (type == 'L' || type == 'D' || type == 'X' || type == 'M' || type == 'P') {
        size_t bar = inner.find('|');
        if (bar != std::string::npos) {
            body = inner.substr(0, bar);
            meta.arr()->push_back(Value::str(inner.substr(bar + 1)));
        }
    }
    Value c = Value::array();
    if (type == 'C' || type == 'V') { if (!body.empty()) c.arr()->push_back(Value::str(body)); }
    else if (type == 'E') {
        // E<…> is an ENTITY: `;`-separated codepoints (decimal, 0x/0o/0b),
        // Unicode character names, or HTML5 entity names — each the character
        // it names, the way it was written kept in .meta
        static const std::map<std::string, const char*> kHtml5 = {
            {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"},
            {"nbsp", "\xC2\xA0"}, {"copy", "\xC2\xA9"}, {"reg", "\xC2\xAE"},
            {"deg", "\xC2\xB0"}, {"plusmn", "\xC2\xB1"}, {"sup1", "\xC2\xB9"},
            {"sup2", "\xC2\xB2"}, {"sup3", "\xC2\xB3"}, {"micro", "\xC2\xB5"},
            {"para", "\xC2\xB6"}, {"middot", "\xC2\xB7"}, {"laquo", "\xC2\xAB"},
            {"raquo", "\xC2\xBB"}, {"frac14", "\xC2\xBC"}, {"frac12", "\xC2\xBD"},
            {"frac34", "\xC2\xBE"}, {"times", "\xC3\x97"}, {"divide", "\xC3\xB7"},
            {"sect", "\xC2\xA7"}, {"euro", "\xE2\x82\xAC"}, {"hellip", "\xE2\x80\xA6"},
            {"mdash", "\xE2\x80\x94"}, {"ndash", "\xE2\x80\x93"}, {"Assign", "\xE2\x89\x94"},
            {"larr", "\xE2\x86\x90"}, {"rarr", "\xE2\x86\x92"}, {"uarr", "\xE2\x86\x91"},
            {"darr", "\xE2\x86\x93"}, {"ne", "\xE2\x89\xA0"}, {"le", "\xE2\x89\xA4"},
            {"ge", "\xE2\x89\xA5"}, {"infin", "\xE2\x88\x9E"}, {"trade", "\xE2\x84\xA2"}};
        auto utf8 = [](uint32_t cp) {
            std::string o;
            if (cp < 0x80) o += (char)cp;
            else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
            else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
            return o;
        };
        std::string out;
        std::stringstream ss(body);
        for (std::string ent; std::getline(ss, ent, ';'); ) {
            size_t a = ent.find_first_not_of(" \t"), b = ent.find_last_not_of(" \t");
            if (a == std::string::npos) continue;
            ent = ent.substr(a, b - a + 1);
            int base = 10; size_t off = 0;
            if (ent.size() > 2 && ent[0] == '0' && (ent[1] == 'x' || ent[1] == 'o' || ent[1] == 'b')) {
                base = ent[1] == 'x' ? 16 : ent[1] == 'o' ? 8 : 2; off = 2;
            }
            char* endp = nullptr;
            long v = std::strtol(ent.c_str() + off, &endp, base);
            if (endp && *endp == '\0' && endp != ent.c_str() + off) { out += utf8((uint32_t)v); continue; }
            auto h = kHtml5.find(ent);
            if (h != kHtml5.end()) { out += h->second; continue; }
            int32_t cp = uniCharByName(ent);
            if (cp >= 0) { out += utf8((uint32_t)cp); continue; }
            out += ent;   // unknown: as written
        }
        c.arr()->push_back(Value::str(out));
        meta.arr()->push_back(Value::str(body));
    }
    else parseFormatting(body, *c.arr());
    (*f.hash())["contents"] = c;
    (*f.hash())["meta"] = meta;
    return f;
}

// The opener a code letter at `k` is followed by: a run of `<` (its length in
// nOpen) or `«` (guil). Answers the index just past it, or npos when the
// letter is ordinary text.
static size_t fmtOpener(const std::string& s, size_t k, size_t& nOpen, bool& guil) {
    nOpen = 0; guil = false;
    if (k + 1 >= s.size() || s[k] < 'A' || s[k] > 'Z') return std::string::npos;
    size_t j = k + 1;
    if (s.compare(j, 2, "\xC2\xAB") == 0) { guil = true; return j + 2; }
    while (j < s.size() && s[j] == '<') { nOpen++; j++; }
    return nOpen ? j : std::string::npos;
}

// The closer of a code whose body starts at `k`: a run of nOpen `>` (or `»`).
// A NESTED code met on the way is skipped whole by ITS OWN delimiter, so
// `B<bold I<nested>>` closes I at the first `>` and B at the second, and a
// `>` inside `C<<a>b>>` is text. Answers the closer's index, or npos.
static size_t fmtClose(const std::string& s, size_t k, size_t nOpen, bool guil) {
    const size_t n = s.size();
    const std::string closer = guil ? "\xC2\xBB" : std::string(nOpen, '>');
    while (k < n) {
        size_t m; bool g;
        size_t body = fmtOpener(s, k, m, g);
        if (body != std::string::npos) {
            size_t e = fmtClose(s, body, m, g);
            if (e != std::string::npos) { k = e + (g ? 2 : m); continue; }
            // an unclosed nested opener is text — read on from its letter
        }
        if (s.compare(k, closer.size(), closer) == 0) return k;
        k++;
    }
    return std::string::npos;
}

static void parseFormatting(const std::string& s, ValueList& out) {
    std::string text;
    auto flush = [&]() { if (!text.empty()) { out.push_back(Value::str(text)); text.clear(); } };
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        size_t nOpen; bool guil;
        size_t body = fmtOpener(s, i, nOpen, guil);
        if (body != std::string::npos) {
            size_t end = fmtClose(s, body, nOpen, guil);
            if (end != std::string::npos) {
                flush();
                out.push_back(mkFmt(s[i], s.substr(body, end - body)));
                i = end + (guil ? 2 : nOpen);
                continue;
            }
            // no closer: it was ordinary text after all
        }
        text += s[i]; i++;
    }
    flush();
}

static Value mkPara(const std::string& text) {
    Value p = mkPod("Pod::Block::Para");
    Value pc = Value::array();
    parseFormatting(collapseWs(text), *pc.arr());
    (*p.hash())["contents"] = pc;
    return p;
}

// ---------- block configuration: `:key<a b>` `:key(42)` `:!key` `:key{a=>1}` ----------

// one scalar config atom: 'str' / "str" / Q[str] / True / False / number / bare word
static Value cfgScalar(std::string t) {
    size_t a = t.find_first_not_of(" \t\n");
    if (a == std::string::npos) return Value::str("");
    size_t b = t.find_last_not_of(" \t\n");
    t = t.substr(a, b - a + 1);
    if (t.size() >= 2 && (t[0] == '\'' || t[0] == '"') && t.back() == t[0]) {
        std::string o;
        for (size_t i = 1; i + 1 < t.size(); i++) {
            if (t[i] == '\\' && i + 2 < t.size()) { o += t[++i]; continue; }
            o += t[i];
        }
        return Value::str(o);
    }
    if (t.size() >= 3 && t.compare(0, 2, "Q[") == 0 && t.back() == ']')
        return Value::str(t.substr(2, t.size() - 3));
    if (t == "True")  return Value::boolean(true);
    if (t == "False") return Value::boolean(false);
    {   // integer (with optional sign; big ones go to BigInt), else general number
        bool allDigits = !t.empty();
        for (size_t i = (t[0] == '+' || t[0] == '-') ? 1 : 0; i < t.size(); i++)
            if (!ascii::isdigit((unsigned char)t[i])) { allDigits = false; break; }
        if (t.size() == 1 && (t[0] == '+' || t[0] == '-')) allDigits = false;
        if (allDigits) {
            errno = 0;
            char* end = nullptr;
            long long iv = std::strtoll(t.c_str(), &end, 10);
            if (errno != ERANGE) return Value::integer(iv);
            return Value::bigint(BigInt::fromString(t[0] == '+' ? t.substr(1) : t));
        }
        // Through the same numeric ladder a LITERAL takes, so a Pod config value
        // keeps the type its spelling asks for: `:k(2.3)` is a Rat and `:k(1e4)`
        // a Num. strtod made everything a Num, which is what Rakudo does too —
        // S26-documentation/09-configuration.t asserts Rat and Rakudo marks its
        // own failure `todo '2.3 and -2.3 are Rats, not Nums'`.
        Value n = numifyStr(t);
        if (n.t == VT::Int || n.t == VT::Rat || n.t == VT::Num || n.t == VT::Complex) return n;
    }
    return Value::str(t);
}

// split on `sep` at depth 0, respecting quotes (with backslash escapes) and ()[]{} nesting
static void cfgSplitTop(const std::string& s, char sep, std::vector<std::string>& out) {
    std::string cur; char q = 0; int depth = 0;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (q) {
            if (c == '\\' && i + 1 < s.size()) { cur += c; cur += s[++i]; continue; }
            if (c == q) q = 0;
            cur += c; continue;
        }
        if (c == '\'' || c == '"') { q = c; cur += c; continue; }
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') depth--;
        if (c == sep && depth == 0) { out.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    if (!strip(cur).empty()) out.push_back(cur);
}

// position of a top-level `=>` (outside quotes/brackets), or npos
static size_t cfgFindArrow(const std::string& s) {
    char q = 0; int depth = 0;
    for (size_t i = 0; i + 1 < s.size(); i++) {
        char c = s[i];
        if (q) { if (c == '\\') { i++; continue; } if (c == q) q = 0; continue; }
        if (c == '\'' || c == '"') { q = c; continue; }
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') depth--;
        else if (c == '=' && s[i + 1] == '>' && depth == 0) return i;
    }
    return std::string::npos;
}

static std::string cfgKey(std::string t) {
    Value v = cfgScalar(std::move(t));
    return v.toStr();
}

// the bracketed value of one pair: <words>, (list), [list], {pairs}
static Value cfgValue(char open, const std::string& body) {
    if (open == '<') { // whitespace-separated words; a single word is a plain Str
        std::vector<std::string> words; std::string w;
        for (char c : body) {
            if (ascii::isspace((unsigned char)c)) { if (!w.empty()) { words.push_back(w); w.clear(); } }
            else w += c;
        }
        if (!w.empty()) words.push_back(w);
        if (words.size() == 1) return Value::str(words[0]);
        Value lst = Value::array(); lst.isList = true;
        for (auto& s : words) lst.arr()->push_back(Value::str(s));
        return lst;
    }
    if (open == '{') { // hash: `k => v` pairs; a bare element is the KEY of the next element
        Value h = Value::makeHash();
        std::vector<std::string> pieces; cfgSplitTop(body, ',', pieces);
        std::string pendingKey; bool havePending = false;
        for (auto& p : pieces) {
            size_t ar = cfgFindArrow(p);
            if (ar != std::string::npos) {
                (*h.hash())[cfgKey(p.substr(0, ar))] = cfgScalar(p.substr(ar + 2));
                continue;
            }
            if (havePending) { (*h.hash())[pendingKey] = cfgScalar(p); havePending = false; }
            else { pendingKey = cfgKey(p); havePending = true; }
        }
        return h;
    }
    // ( … ) / [ … ]: a comma list; one element collapses to the element itself
    std::vector<std::string> pieces; cfgSplitTop(body, ',', pieces);
    if (pieces.size() == 1) return cfgScalar(pieces[0]);
    Value lst = Value::array(); lst.isList = true;
    for (auto& p : pieces) lst.arr()->push_back(cfgScalar(p));
    return lst;
}

// parse every `:pair` in a directive's remainder into a config hash
static Value podParseConfig(const std::string& s) {
    Value h = Value::makeHash();
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] != ':') i++;
        if (i >= s.size()) break;
        i++;
        bool neg = i < s.size() && s[i] == '!';
        if (neg) i++;
        // digit-prefix form `:034foo` = foo => 34
        std::string digits;
        while (!neg && i < s.size() && ascii::isdigit((unsigned char)s[i])) digits += s[i++];
        std::string key;
        while (i < s.size() && (ascii::isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '_'))
            key += s[i++];
        if (key.empty()) continue;
        Value val = digits.empty() ? Value::boolean(!neg)
                                   : Value::integer(std::strtoll(digits.c_str(), nullptr, 10));
        if (!neg && i < s.size() && (s[i] == '<' || s[i] == '(' || s[i] == '[' || s[i] == '{')) {
            char open = s[i];
            char close = open == '<' ? '>' : open == '(' ? ')' : open == '[' ? ']' : '}';
            int depth = 1; size_t j = i + 1; char q = 0; std::string body;
            while (j < s.size() && depth) {
                char c = s[j];
                if (q) {
                    if (c == '\\' && j + 1 < s.size()) { body += c; body += s[++j]; j++; continue; }
                    if (c == q) q = 0;
                    body += c; j++; continue;
                }
                if (c == '\'' || c == '"') { q = c; body += c; j++; continue; }
                if (c == open) depth++;
                else if (c == close) { depth--; if (!depth) { j++; break; } }
                body += c; j++;
            }
            i = j;
            val = cfgValue(open, body);
        }
        (*h.hash())[key] = val;
    }
    return h;
}

// `=   :more<config>` continuation of the previous directive's config line
static bool cfgContLine(const std::string& ln, std::string& content) {
    size_t p = ln.find_first_not_of(" \t");
    if (p == std::string::npos || ln[p] != '=') return false;
    if (p + 1 >= ln.size() || !ascii::isspace((unsigned char)ln[p + 1])) return false;
    content = strip(ln.substr(p + 1));
    return !content.empty() && content[0] == ':';
}

// remainder of a directive line after the block name, plus `=`-continuation lines
static std::string cfgTextAfterName(const std::string& rest, const std::string& name,
                                    const std::vector<std::string>& lines, size_t& i) {
    std::string cfg = rest.size() > name.size() ? rest.substr(name.size()) : std::string();
    std::string cont;
    while (i < lines.size() && cfgContLine(lines[i], cont)) { cfg += " " + cont; i++; }
    return cfg;
}

// A `=word rest` directive line (after left-trim). Returns the keyword + remainder.
static bool matchDirective(const std::string& line, std::string& kw, std::string& rest) {
    std::string t = ltrim(line);
    if (t.empty() || t[0] != '=') return false;
    size_t i = 1; std::string w;
    // a block name is an identifier: `-` / `'` join two word parts (`=SEE-ALSO`)
    while (i < t.size() && (ascii::isalnum((unsigned char)t[i]) || t[i] == '_' ||
                            ((t[i] == '-' || t[i] == '\'') && !w.empty() && i + 1 < t.size() &&
                             ascii::isalpha((unsigned char)t[i + 1]))))
        w += t[i++];
    if (w.empty()) return false;
    kw = w;
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) i++;
    rest = t.substr(i);
    return true;
}

// Collect a paragraph verbatim (raw lines, joined with newlines, no whitespace
// collapse). `trailingNL` adds a closing newline (comment blocks keep one).
static std::string collectVerbatim(const std::vector<std::string>& lines, size_t& i, bool trailingNL) {
    std::vector<std::string> body; std::string k2, r2;
    while (i < lines.size() && !strip(lines[i]).empty() && !matchDirective(lines[i], k2, r2)) { body.push_back(lines[i]); i++; }
    std::string text; for (size_t k = 0; k < body.size(); k++) { if (k) text += "\n"; text += body[k]; }
    if (trailingNL && !text.empty()) text += "\n";
    return text;
}

// Collect the text of a paragraph: consecutive non-blank, non-directive lines.
static std::string collectPara(const std::vector<std::string>& lines, size_t& i) {
    std::string para; std::string kw, rest;
    while (i < lines.size() && !strip(lines[i]).empty() && !matchDirective(lines[i], kw, rest)) {
        if (!para.empty()) para += " ";
        para += strip(lines[i]);
        i++;
    }
    return para;
}

// A Pod::Defn from its lines: the first is the TERM, the rest its definition,
// one Para per blank-separated paragraph. Neither is parsed for formatting
// codes (Rakudo keeps `B<term>` as written in both).
static Value mkDefn(std::vector<std::string> lines, Value config) {
    Value d = mkPod("Pod::Defn");
    while (!lines.empty() && strip(lines.front()).empty()) lines.erase(lines.begin());
    std::string term = lines.empty() ? std::string() : strip(lines.front());
    if (!lines.empty()) lines.erase(lines.begin());
    // `=defn # term` — the hash is the :numbered alias
    if (term.size() > 1 && term[0] == '#' && (term[1] == ' ' || term[1] == '\t')) {
        term = strip(term.substr(1));
        if (config.t != VT::Hash) config = Value::makeHash();
        (*config.hash())["numbered"] = Value::boolean(true);
    }
    (*d.hash())["term"] = Value::str(term);
    Value cc = Value::array();
    std::string para;
    auto flush = [&]() {
        if (para.empty()) return;
        Value p = mkPod("Pod::Block::Para");
        Value pc = Value::array(); pc.arr()->push_back(Value::str(para));
        (*p.hash())["contents"] = pc;
        cc.arr()->push_back(p);
        para.clear();
    };
    for (auto& l : lines) {
        std::string t = strip(l);
        if (t.empty()) { flush(); continue; }
        if (!para.empty()) para += " ";
        para += t;
    }
    flush();
    (*d.hash())["contents"] = cc;
    (*d.hash())["config"] = config.t == VT::Hash ? config : Value::makeHash();
    return d;
}

// head1/head2/… or item/item1/… → (base, level). base is "head"/"item"; level
// defaults to 1 when no trailing digits.
static bool splitLeveled(const std::string& kw, const std::string& base, int& level) {
    if (kw.compare(0, base.size(), base) != 0) return false;
    std::string tail = kw.substr(base.size());
    if (tail.empty()) { level = 1; return true; }
    for (char c : tail) if (!ascii::isdigit((unsigned char)c)) return false;
    level = std::stoi(tail);
    return true;
}

// A Pod table's rows, as Rakudo yields them: `headers` (empty when the table
// has none), `contents` (one list of cells per row, every row padded to the
// widest) and `caption` (from the config, "" otherwise). All three are always
// present, because a parsed table is compared against a constructed one
// (Pod::Utils' `pod-table`) and a missing key is a difference.
//
// The shape of the rules (S26-documentation/07*-tables.t spell them out):
//   * a SEPARATOR line holds only `- = + | :` and blanks, with a `-` or `=`;
//   * the header is what stands above the one `=` separator when `-` ones
//     also appear, or above the only separator there is;
//   * with row separators, the lines between two of them MERGE into one row,
//     cell by cell; without, every line is a row;
//   * cells split on `|` or `+` (`\|`, `\+` are data) when any row has a
//     visible `|`, and otherwise by COLUMN POSITION: a column starts wherever
//     text follows two or more blanks (or the line's start), in any row;
//   * a `+---+` grid drops its outer border bars;
//   * `Z<…>` comments vanish before any of this.
static std::string podCellNorm(const std::string& c) {
    // edge padding may be NO-BREAK SPACEs too (docs.raku.org's operator
    // tables use them to keep a lone `+` from reading as a divider)
    std::string t = strip(c), out;
    for (bool again = true; again; ) {
        again = false;
        if (t.compare(0, 2, "\xC2\xA0") == 0) { t = strip(t.substr(2)); again = true; }
        if (t.size() >= 2 && t.compare(t.size() - 2, 2, "\xC2\xA0") == 0) { t = strip(t.substr(0, t.size() - 2)); again = true; }
    }
    bool sp = false;
    for (char ch : t) {
        if (ch == ' ' || ch == '\t') { sp = true; continue; }
        if (sp && !out.empty()) out += ' ';
        sp = false;
        out += ch;
    }
    return out;
}
static bool podTableDivider(const std::string& s) {
    std::string t = strip(s);
    if (t.empty()) return false;
    bool rule = false;
    for (char c : t) {
        if (c == '-' || c == '=') rule = true;
        else if (c != '+' && c != '|' && c != ':' && c != ' ' && c != '\t') return false;
    }
    return rule;
}
static std::vector<std::string> podSplitBars(const std::string& line) {
    std::vector<std::string> cells;
    std::string cur;
    for (size_t k = 0; k < line.size(); k++) {
        char ch = line[k];
        if (ch == '\\' && k + 1 < line.size() && (line[k + 1] == '|' || line[k + 1] == '+')) { cur += line[++k]; continue; }
        if (ch == '|' || ch == '+') { cells.push_back(podCellNorm(cur)); cur.clear(); continue; }
        cur += ch;
    }
    cells.push_back(podCellNorm(cur));
    return cells;
}
static std::vector<std::string> podMergeRows(const std::vector<std::vector<std::string>>& g) {
    if (g.size() == 1) return g[0];
    size_t w = 0;
    for (auto& r : g) w = std::max(w, r.size());
    std::vector<std::string> m(w);
    for (auto& r : g)
        for (size_t j = 0; j < r.size(); j++) {
            if (!m[j].empty() && !r[j].empty()) m[j] += ' ';
            m[j] += r[j];
        }
    return m;
}
static std::string podStripZ(const std::string& l) {
    std::string out;
    for (size_t k = 0; k < l.size(); k++) {
        if (l[k] == 'Z' && k + 1 < l.size() && l[k + 1] == '<') {
            int d = 0;
            for (k++; k < l.size(); k++) {
                if (l[k] == '<') d++;
                else if (l[k] == '>' && --d == 0) break;
            }
            continue;
        }
        out += l[k];
    }
    return out;
}
static bool g_podStrictFwd();
static void podTableParse(const std::vector<std::string>& raw,
                          std::vector<std::vector<std::string>>& headers,
                          std::vector<std::vector<std::string>>& rows) {
    std::vector<std::string> lines;
    for (auto& l : raw) lines.push_back(podStripZ(l));
    while (!lines.empty() && strip(lines.front()).empty()) lines.erase(lines.begin());
    while (!lines.empty() && strip(lines.back()).empty()) lines.pop_back();
    // the three tables Rakudo refuses outright
    auto bad = [&](const std::string& why) {
        if (!g_podStrictFwd()) return;
        throw RakuError{Value::typeObj("X::AdHoc"), "Pod table error: " + why};
    };
    if (lines.empty()) { bad("the table has no rows"); return; }
    auto pad = [&]() {
        size_t w = 0;
        for (auto& r : headers) w = std::max(w, r.size());
        for (auto& r : rows) w = std::max(w, r.size());
        for (auto& r : headers) r.resize(w);
        for (auto& r : rows) r.resize(w);
    };
    auto isGrid = [](const std::string& l) {
        std::string t = strip(l);
        if (t.size() < 2 || t.front() != '+' || t.back() != '+') return false;
        for (char c : t) if (c != '+' && c != '-' && c != '=' && c != ' ') return false;
        return true;
    };
    if (isGrid(lines[0])) {
        bool sawEq = false;
        std::vector<std::vector<std::string>> hdr, body;
        for (auto& l : lines) {
            std::string t = strip(l);
            if (t.empty()) continue;
            if (isGrid(t)) { if (t.find('=') != std::string::npos) sawEq = true; continue; }
            if (t.size() >= 2 && t.front() == '|' && t.back() == '|') t = t.substr(1, t.size() - 2);
            std::vector<std::string> cells;
            size_t p = 0;
            for (;;) {
                size_t b = t.find('|', p);
                cells.push_back(podCellNorm(t.substr(p, b == std::string::npos ? std::string::npos : b - p)));
                if (b == std::string::npos) break;
                p = b + 1;
            }
            (sawEq ? body : hdr).push_back(cells);
        }
        if (!sawEq && !hdr.empty()) { body = hdr; hdr.clear(); hdr.push_back(body.front()); body.erase(body.begin()); }
        headers = hdr; rows = body;
        pad();
        return;
    }
    bool bars = false;
    for (auto& l : lines) {
        if (strip(l).empty() || podTableDivider(l)) continue;
        for (size_t k = 0; k < l.size(); k++) {
            if (l[k] == '\\') { k++; continue; }
            if (l[k] == '|') { bars = true; break; }
        }
        if (bars) break;
    }
    {
        bool prevSep = false;
        for (auto& l : lines) {
            if (strip(l).empty()) continue;
            bool sep = podTableDivider(l);
            if (sep && prevSep) bad("consecutive row separators");
            prevSep = sep;
            if (bars && !sep) {
                bool vis = false;
                for (size_t k = 0; k < l.size() && !vis; k++) {
                    if (l[k] == '\\') { k++; continue; }
                    vis = l[k] == '|' || l[k] == '+';
                }
                if (!vis) bad("a row mixes whitespace and visible column separators");
            }
        }
    }
    std::vector<size_t> seps, eqSeps, dashSeps;
    for (size_t k = 0; k < lines.size(); k++) {
        if (!podTableDivider(lines[k])) continue;
        seps.push_back(k);
        std::string t = strip(lines[k]);
        (t.find('-') == std::string::npos ? eqSeps : dashSeps).push_back(k);
    }
    long hdrSep = -1;
    if (eqSeps.size() == 1 && !dashSeps.empty()) hdrSep = (long)eqSeps[0];
    else if (seps.size() == 1 && seps[0] > 0) hdrSep = (long)seps[0];
    const size_t rowSeps = seps.size() - (hdrSep >= 0 ? 1 : 0);
    // group the data lines: separators (and, for a column-aligned table,
    // blank lines) end a group; everything above the header separator is
    // the header's
    std::vector<std::string> hdrLines;
    std::vector<std::vector<std::string>> groups;
    std::vector<std::string> cur;
    bool pastHdr = hdrSep < 0, sawBlank = false;
    auto flush = [&]() {
        if (cur.empty()) return;
        if (pastHdr) groups.push_back(cur);
        else hdrLines.insert(hdrLines.end(), cur.begin(), cur.end());
        cur.clear();
    };
    for (size_t k = 0; k < lines.size(); k++) {
        if (strip(lines[k]).empty()) {
            if (!bars) { sawBlank = true; flush(); }
            continue;
        }
        if (podTableDivider(lines[k])) {
            flush();
            if ((long)k == hdrSep) pastHdr = true;
            continue;
        }
        cur.push_back(lines[k]);
    }
    flush();
    std::function<std::vector<std::vector<std::string>>(const std::vector<std::string>&)> cellsOf;
    if (bars) {
        cellsOf = [](const std::vector<std::string>& ls) {
            std::vector<std::vector<std::string>> r;
            for (auto& l : ls) r.push_back(podSplitBars(strip(l)));
            return r;
        };
    }
    else {
        // column starts, over every data line at the common indent
        std::vector<std::string> all = hdrLines;
        for (auto& g : groups) all.insert(all.end(), g.begin(), g.end());
        size_t indent = std::string::npos;
        for (auto& l : all) indent = std::min(indent, l.find_first_not_of(" \t"));
        if (indent == std::string::npos) indent = 0;
        std::set<size_t> starts;
        for (auto& l : all) {
            std::string a = l.size() > indent ? l.substr(indent) : std::string();
            size_t blanks = 0; bool inBlank = true;
            for (size_t k = 0; k < a.size(); k++) {
                if (a[k] == ' ' || a[k] == '\t') { blanks++; inBlank = true; continue; }
                if (inBlank && (k == 0 || blanks >= 2)) starts.insert(k);
                inBlank = false; blanks = 0;
            }
        }
        std::vector<size_t> cols(starts.begin(), starts.end());
        cellsOf = [cols, indent](const std::vector<std::string>& ls) {
            std::vector<std::vector<std::string>> r;
            for (auto& l : ls) {
                std::string a = l.size() > indent ? l.substr(indent) : std::string();
                std::vector<std::string> cells;
                for (size_t c = 0; c < cols.size(); c++) {
                    size_t e = c + 1 < cols.size() ? cols[c + 1] : a.size();
                    cells.push_back(cols[c] < a.size() ? podCellNorm(a.substr(cols[c], std::min(e, a.size()) - cols[c])) : std::string());
                }
                r.push_back(cells);
            }
            return r;
        };
    }
    if (!hdrLines.empty() && hdrSep >= 0) headers.push_back(podMergeRows(cellsOf(hdrLines)));
    const bool merge = bars ? rowSeps > 0 : ((rowSeps > 0 || sawBlank) && groups.size() > 1);
    for (auto& g : groups) {
        auto cs = cellsOf(g);
        if (merge) rows.push_back(podMergeRows(cs));
        else rows.insert(rows.end(), cs.begin(), cs.end());
    }
    pad();
}

static void fillPodTable(Value& block, const std::vector<std::string>& lines) {
    Value headers = Value::array(), body = Value::array();
    std::vector<std::vector<std::string>> hs, rs;
    podTableParse(lines, hs, rs);
    if (hs.size() == 1) for (auto& c : hs[0]) headers.arr()->push_back(Value::str(c));
    for (auto& r : rs) {
        Value row = Value::array();
        for (auto& c : r) row.arr()->push_back(Value::str(c));
        body.arr()->push_back(std::move(row));
    }
    (*block.hash())["headers"] = headers;
    (*block.hash())["contents"] = body;
    auto cfg = block.hash()->find("config");
    Value cap = Value::str("");
    if (cfg != block.hash()->end() && cfg->second.t == VT::Hash && cfg->second.hash()) {
        auto c = cfg->second.hash()->find("caption");
        if (c != cfg->second.hash()->end()) cap = Value::str(c->second.toStr());
    }
    (*block.hash())["caption"] = cap;
}

// Map a block name to its Pod class (and level for head/item). `=begin item2`,
// `=item2`, `=for item2` all become Pod::Item level 2, etc.
static std::string classForBlock(const std::string& name, int& level) {
    level = 0; int lv = 0;
    if (splitLeveled(name, "head", lv)) { level = lv; return "Pod::Heading"; }
    if (splitLeveled(name, "item", lv)) { level = lv; return "Pod::Item"; }
    if (name == "code")    return "Pod::Block::Code";
    if (name == "comment") return "Pod::Block::Comment";
    if (name == "table")   return "Pod::Block::Table";
    return "Pod::Block::Named";
}

// The block's virtual left margin: the indent of its own `=begin` DELIMITER.
// Text at the margin is ordinary; text indented past it is verbatim — a code
// block. Reading the margin off the first CONTENT line instead meant a block
// whose content was all indented set its own margin and so had no verbatim
// text at all: `=begin pod` at column 0 with nothing but an indented example
// under it gave a Para where Rakudo gives a Pod::Block::Code, which is
// Pod::Utils' and Pod::Utilities' `first-code-block` answering "".
static int blockMargin(const std::vector<std::string>& lines, size_t begin) {
    return begin < lines.size() ? indentOf(lines[begin]) : 0;
}

static void parseSeq(const std::vector<std::string>& lines, size_t& i,
                     const std::string& closeName, bool inBlock, ValueList& out, int margin) {
    while (i < lines.size()) {
        std::string kw, rest;
        if (matchDirective(lines[i], kw, rest)) {
            if (kw == "end") {
                if (!closeName.empty() && firstWord(rest) == closeName) return; // caller consumes =end
                i++; continue; // stray =end
            }
            if (kw == "begin") {
                std::string name = firstWord(rest);
                int lv = 0; std::string cls = classForBlock(name, lv);
                i++;
                std::string cfg = cfgTextAfterName(rest, name, lines, i);
                Value block = mkPod(cls);
                if (cls == "Pod::Block::Named") (*block.hash())["name"] = Value::str(name);
                if (lv) (*block.hash())["level"] = Value::integer(lv);
                if (cfg.find(':') != std::string::npos) (*block.hash())["config"] = podParseConfig(cfg);
                ValueList inner;
                if (name == "defn") {
                    std::vector<std::string> body; std::string k2, r2;
                    while (i < lines.size() && !(matchDirective(lines[i], k2, r2) && k2 == "end" && firstWord(r2) == name))
                        { body.push_back(lines[i]); i++; }
                    if (i < lines.size()) i++;
                    out.push_back(mkDefn(body, cfg.find(':') != std::string::npos ? podParseConfig(cfg) : Value()));
                    continue;
                }
                if (cls == "Pod::Block::Table") { // rows, not pod
                    std::vector<std::string> rows; std::string k2, r2;
                    while (i < lines.size() && !(matchDirective(lines[i], k2, r2) && k2 == "end" && firstWord(r2) == name))
                        { rows.push_back(lines[i]); i++; }
                    fillPodTable(block, rows);
                    if (i < lines.size()) i++;
                    out.push_back(block);
                    continue;
                }
                if (cls == "Pod::Block::Code" || cls == "Pod::Block::Comment") { // verbatim contents
                    std::vector<std::string> code; std::string k2, r2;
                    while (i < lines.size() && !(matchDirective(lines[i], k2, r2) && k2 == "end" && firstWord(r2) == name))
                        { code.push_back(lines[i]); i++; }
                    while (!code.empty() && strip(code.back()).empty()) code.pop_back();
                    std::string text; for (size_t k = 0; k < code.size(); k++) { if (k) text += "\n"; text += code[k]; }
                    if (cls == "Pod::Block::Comment" && !text.empty()) text += "\n"; // comments keep a trailing NL
                    Value cc = Value::array(); if (!text.empty()) cc.arr()->push_back(Value::str(text));
                    (*block.hash())["contents"] = cc;
                    if (i < lines.size()) i++;
                    out.push_back(block);
                    continue;
                }
                parseSeq(lines, i, name, true, inner, blockMargin(lines, i - 1));
                Value ic = Value::array(); *ic.arr() = std::move(inner);
                (*block.hash())["contents"] = ic;
                if (i < lines.size()) i++; // consume the =end line
                out.push_back(block);
                continue;
            }
            if (kw == "for") { // paragraph block: the following paragraph is its content
                std::string name = firstWord(rest);
                int lv = 0; std::string cls = classForBlock(name, lv);
                i++;
                std::string cfg = cfgTextAfterName(rest, name, lines, i);
                Value block = mkPod(cls);
                if (cls == "Pod::Block::Named") (*block.hash())["name"] = Value::str(name);
                if (lv) (*block.hash())["level"] = Value::integer(lv);
                if (cfg.find(':') != std::string::npos) (*block.hash())["config"] = podParseConfig(cfg);
                if (name == "defn") { // `=for defn` — the paragraph below: term, then definition
                    std::vector<std::string> body; std::string k3, r3;
                    while (i < lines.size() && !strip(lines[i]).empty() && !matchDirective(lines[i], k3, r3))
                        { body.push_back(lines[i]); i++; }
                    out.push_back(mkDefn(body, cfg.find(':') != std::string::npos ? podParseConfig(cfg) : Value()));
                    continue;
                }
                if (cls == "Pod::Block::Table") { // `=for table` — the paragraph below is the rows
                    std::vector<std::string> rows; std::string k3, r3;
                    while (i < lines.size() && !strip(lines[i]).empty() && !matchDirective(lines[i], k3, r3))
                        { rows.push_back(lines[i]); i++; }
                    fillPodTable(block, rows);
                    out.push_back(block);
                    continue;
                }
                Value ic = Value::array();
                if (cls == "Pod::Block::Comment" || cls == "Pod::Block::Code") {
                    std::string v = collectVerbatim(lines, i, cls == "Pod::Block::Comment");
                    if (!v.empty()) ic.arr()->push_back(Value::str(v));
                } else {
                    std::string para = collectPara(lines, i);
                    if (!para.empty()) ic.arr()->push_back(mkPara(para));
                }
                (*block.hash())["contents"] = ic;
                out.push_back(block);
                continue;
            }
            int level = 0;
            if (splitLeveled(kw, "head", level)) {
                std::string text = rest; i++;
                std::string cont = collectPara(lines, i);
                if (!cont.empty()) text += (text.empty() ? "" : " ") + cont;
                Value h = mkPod("Pod::Heading");
                (*h.hash())["level"] = Value::integer(level);
                Value ic = Value::array(); ic.arr()->push_back(mkPara(text));
                (*h.hash())["contents"] = ic;
                out.push_back(h);
                continue;
            }
            if (splitLeveled(kw, "item", level)) {
                std::string text = rest; i++;
                std::string cont = collectPara(lines, i);
                if (!cont.empty()) text += (text.empty() ? "" : " ") + cont;
                Value it = mkPod("Pod::Item");
                (*it.hash())["level"] = Value::integer(level);
                Value ic = Value::array(); ic.arr()->push_back(mkPara(text));
                (*it.hash())["contents"] = ic;
                out.push_back(it);
                continue;
            }
            if (kw == "config") { // `=config TYPE :key<val> …` — a Pod::Config node
                std::string type = firstWord(rest);
                i++;
                std::string cfg = cfgTextAfterName(rest, type, lines, i);
                Value node = mkPod("Pod::Config");
                (*node.hash())["type"] = Value::str(type);
                (*node.hash())["config"] = podParseConfig(cfg);
                (*node.hash())["contents"] = Value::array();
                out.push_back(node);
                continue;
            }
            if (kw == "comment") { // abbreviated: verbatim body until a blank line
                i++;
                std::string v = collectVerbatim(lines, i, true);
                if (!rest.empty()) v = rest + (v.empty() ? std::string("\n") : "\n" + v);
                Value cm = mkPod("Pod::Block::Comment");
                Value cc = Value::array(); if (!v.empty()) cc.arr()->push_back(Value::str(v));
                (*cm.hash())["contents"] = cc;
                out.push_back(cm);
                continue;
            }
            if (kw == "table") { // abbreviated `=table`: rows until a blank line
                std::vector<std::string> rows;
                if (!strip(rest).empty()) rows.push_back(rest);
                i++;
                std::string k2, r2;
                while (i < lines.size() && !strip(lines[i]).empty() && !matchDirective(lines[i], k2, r2))
                    { rows.push_back(lines[i]); i++; }
                Value block = mkPod("Pod::Block::Table");
                fillPodTable(block, rows);
                out.push_back(block);
                continue;
            }
            // (`=pod` is no exception: abbreviated, it holds the ONE paragraph
            // below it — Rakudo calls a later `=end pod` a Pod syntax error)
            if (kw == "defn") { // abbreviated: the term on the `=defn` line, its paragraph below
                std::vector<std::string> body{rest}; i++;
                std::string k3, r3;
                while (i < lines.size() && !strip(lines[i]).empty() && !matchDirective(lines[i], k3, r3))
                    { body.push_back(lines[i]); i++; }
                out.push_back(mkDefn(body, Value()));
                continue;
            }
            // abbreviated `=name text` → a named block holding one paragraph
            std::string name = kw;
            std::string text = rest; i++;
            std::string cont = collectPara(lines, i);
            if (!cont.empty()) text += (text.empty() ? "" : " ") + cont;
            Value block = mkPod("Pod::Block::Named");
            (*block.hash())["name"] = Value::str(name);
            Value ic = Value::array(); if (!text.empty()) ic.arr()->push_back(mkPara(text));
            (*block.hash())["contents"] = ic;
            out.push_back(block);
            continue;
        }
        if (strip(lines[i]).empty()) { i++; continue; }
        if (inBlock) {
            if (indentOf(lines[i]) > margin) {
                // implicit code block: verbatim, may span internal blank lines; dedent by
                // the least indent of its non-blank lines.
                // A verbatim run spans internal blank lines, but only while it
                // stays at or right of where it started: a line indented LESS
                // than the run's first line ends it and (still being past the
                // margin) opens a new one. Without that, an example at 8 and a
                // following one at 6 fused into a single block whose common
                // dedent left the first two spaces in.
                std::vector<std::string> code; std::string k2, r2;
                int codeInd = indentOf(lines[i]);
                while (i < lines.size()) {
                    if (matchDirective(lines[i], k2, r2)) break;
                    if (!strip(lines[i]).empty() &&
                        (indentOf(lines[i]) <= margin || indentOf(lines[i]) < codeInd)) break;
                    code.push_back(lines[i]); i++;
                }
                while (!code.empty() && strip(code.back()).empty()) code.pop_back();
                int minInd = 1 << 30;
                for (auto& l : code) if (!strip(l).empty()) minInd = std::min(minInd, indentOf(l));
                if (minInd == (1 << 30)) minInd = 0;
                std::string text;
                for (size_t k = 0; k < code.size(); k++) {
                    if (k) text += "\n";
                    const std::string& l = code[k];
                    text += (int)l.size() > minInd ? l.substr(minInd) : "";
                }
                Value cb = mkPod("Pod::Block::Code");
                Value cc = Value::array(); cc.arr()->push_back(Value::str(text));
                (*cb.hash())["contents"] = cc;
                out.push_back(cb);
            } else {
                std::string para = collectPara(lines, i);
                out.push_back(mkPara(para));
            }
        } else {
            i++; // top-level code between POD blocks
        }
    }
}


// ---------- pod2text: Pod::To::Text, the core module, natively ----------
//
// Rakudo ships Pod::To::Text in core and modules `use` it directly — the dists
// behind it here all reach for `pod2text`. Every rule below is oracle-checked
// against that module.
//
// Two join modes matter. A Para's children run TOGETHER (a paragraph is one
// run of text, its formatting codes inline); a Named block's or a document's
// children are separated by a BLANK LINE.
static std::string podPrefixLines(const std::string& text, const std::string& pre) {
    std::string out; size_t p = 0;
    for (;;) {
        size_t nl = text.find('\n', p);
        out += pre;
        out += text.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
        if (nl == std::string::npos) break;
        out += "\n"; p = nl + 1;
    }
    return out;
}

static std::string podKids(const Value& v, bool blankBetween) {
    if (v.t != VT::Hash || !v.hash()) return "";
    auto it = v.hash()->find("contents");
    if (it == v.hash()->end()) return "";
    const Value* c = &it->second;
    if (c->t != VT::Array || !c->arr()) return "";
    std::string out; bool first = true;
    for (auto& k : *c->arr()) {
        std::string s = pod2text(k);
        if (blankBetween && s.empty()) continue;
        if (!first && blankBetween) out += "\n\n";
        out += s; first = false;
    }
    return out;
}

static std::string podTableText(const Value& v) {
    std::vector<std::vector<std::string>> rows;
    auto addRow = [&](const Value& r) {
        std::vector<std::string> cells;
        if (r.t == VT::Array && r.arr()) for (auto& c : *r.arr()) cells.push_back(c.toStr());
        else cells.push_back(r.toStr());
        rows.push_back(std::move(cells));
    };
    auto h = v.hash()->find("headers");
    if (h != v.hash()->end() && h->second.t == VT::Array && h->second.arr() && !h->second.arr()->empty())
        addRow(h->second);
    auto c = v.hash()->find("contents");
    if (c != v.hash()->end() && c->second.t == VT::Array && c->second.arr())
        for (auto& r : *c->second.arr()) addRow(r);
    std::vector<size_t> w;
    for (auto& r : rows)
        for (size_t k = 0; k < r.size(); k++) {
            if (w.size() <= k) w.push_back(0);
            w[k] = std::max(w[k], r[k].size());
        }
    std::string out;
    for (auto& r : rows) {
        std::string line;
        for (size_t k = 0; k < r.size(); k++) {
            if (k) line += "  ";
            line += r[k];
            if (k + 1 < r.size()) line.append(w[k] - r[k].size(), ' ');
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out += "  " + line + "\n";
    }
    return out;
}

std::string pod2text(const Value& v) {
    if (v.t == VT::Str) return v.s.str();
    if (v.t == VT::Array && v.arr()) {
        std::string out; bool first = true;
        for (auto& k : *v.arr()) {
            std::string s = pod2text(k);
            if (s.empty()) continue;
            if (!first) out += "\n\n";
            out += s; first = false;
        }
        return out;
    }
    if (v.t != VT::Hash || v.hashKind != "Pod" || !v.hash()) return v.toStr();
    auto pcIt = v.hash()->find("podclass");
    std::string pc = pcIt == v.hash()->end() ? std::string() : pcIt->second.s.str();
    auto level = [&]() -> long long {
        auto l = v.hash()->find("level");
        return l == v.hash()->end() ? 1 : l->second.toInt();
    };
    if (pc == "Pod::Block::Comment") return "";
    if (pc == "Pod::Block::Code")    return podPrefixLines(podKids(v, false), "    ");
    if (pc == "Pod::Block::Table")   return podTableText(v);
    if (pc == "Pod::Block::Para" || pc == "Pod::FormattingCode") return podKids(v, false);
    if (pc == "Pod::Heading")
        return std::string(2 * (size_t)std::max(0LL, level() - 1), ' ') + podKids(v, false);
    if (pc == "Pod::Item")
        return std::string(2 * (size_t)std::max(0LL, level()), ' ') + "* " + podKids(v, false);
    if (pc == "Pod::Block::Named") {
        auto n = v.hash()->find("name");
        std::string name = n == v.hash()->end() ? std::string() : n->second.s.str();
        std::string kids = podKids(v, true);
        if (name.empty() || name == "pod") return kids;
        return kids.empty() ? name : name + "\n" + kids;
    }
    return podKids(v, true);
}

static thread_local bool g_podStrict = false;
static bool g_podStrictFwd() { return g_podStrict; }
ValueList parsePod(const std::string& src, bool strict) {
    struct StrictScope { bool saved; StrictScope(bool on) : saved(g_podStrict) { g_podStrict = on; } ~StrictScope() { g_podStrict = saved; } } strictScope(strict);
    std::vector<std::string> lines;
    { std::stringstream ss(src); std::string ln; while (std::getline(ss, ln)) lines.push_back(ln); }
    ValueList top; size_t i = 0;
    parseSeq(lines, i, "", false, top, 0);
    // Declarator blocks: a run of `#|` lines (leading) or a `#=` tail (trailing)
    // documents the declaration beside it, and Rakudo lists each as a
    // Pod::Block::Declarator in `$=pod` — Pod::Load's suite reads a class's
    // `#| Base class for magicians` back out of exactly that. The text is
    // kept as the block's one content; the declaration it belongs to is not
    // modelled here (no consumer of these blocks asks for it).
    auto trim = [](std::string t) {
        size_t a = t.find_first_not_of(" \t"); if (a == std::string::npos) return std::string();
        size_t b = t.find_last_not_of(" \t\r"); return t.substr(a, b - a + 1);
    };
    std::string leading;
    std::vector<std::string> heredocs; // terminators of heredocs opened on the current line
    for (size_t k = 0; k < lines.size(); k++) {
        std::string t = trim(lines[k]);
        // a heredoc body is string content, not code: its `#|` documents nothing
        if (!heredocs.empty()) {
            if (t == heredocs.front()) heredocs.erase(heredocs.begin());
            continue;
        }
        for (size_t at = 0; (at = lines[k].find("to", at)) != std::string::npos; at += 2) {
            const std::string& L = lines[k];
            // q:to/…/ (other adverbs may sit between: qq:!c:to/…/) — the quote
            // keyword must be there, or `:to<b>` is just a Pair
            bool adverb = false;
            if (at > 0 && L[at - 1] == ':') {
                size_t b = at - 1;
                while (b > 0) {
                    size_t w = b;
                    while (w > 0 && (std::isalnum((unsigned char)L[w - 1]) || L[w - 1] == '!')) w--;
                    std::string word = L.substr(w, b - w);
                    if (w > 0 && L[w - 1] == ':' && !word.empty()) { b = w - 1; continue; }  // another adverb
                    adverb = (word == "q" || word == "qq" || word == "Q") &&
                             (w == 0 || !(std::isalnum((unsigned char)L[w - 1]) || L[w - 1] == '_' || L[w - 1] == '-'));
                    break;
                }
            }
            bool fused = at > 0 && (L[at - 1] == 'q' || L[at - 1] == 'Q') &&  // qto / qqto / Qto
                         (at < 2 || !(std::isalnum((unsigned char)L[at - 2]) && L[at - 2] != 'q'));
            if (!adverb && !fused) continue;
            size_t d = at + 2;
            if (d >= L.size()) continue;
            char o = L[d], c = o == '<' ? '>' : o == '[' ? ']' : o == '{' ? '}' : o == '(' ? ')' : o;
            if (!(o == '/' || o == '<' || o == '[' || o == '{' || o == '(' || o == '\'' || o == '"')) continue;
            size_t e = L.find(c, d + 1);
            if (e == std::string::npos || e == d + 1) continue;
            heredocs.push_back(trim(L.substr(d + 1, e - d - 1)));
        }
        // an embedded comment `#`( … )` spanning lines is not code: a `#|`
        // inside one (roast comments out whole NYI tests that way) documents nothing
        if (t.size() > 2 && t[0] == '#' && t[1] == '`' &&
            (t[2] == '(' || t[2] == '[' || t[2] == '{' || t[2] == '<')) {
            char open = t[2], close = open == '(' ? ')' : open == '[' ? ']' : open == '{' ? '}' : '>';
            int d = 0; size_t j = k; size_t pos = 2;
            std::string cur = t;
            while (true) {
                for (; pos < cur.size(); pos++) {
                    if (cur[pos] == open) d++;
                    else if (cur[pos] == close && --d == 0) break;
                }
                if (d == 0 || j + 1 >= lines.size()) break;
                cur = lines[++j]; pos = 0;
            }
            if (j > k) { leading.clear(); k = j; continue; }
        }
        // `#|{ … }` / `#={ … }` — a BRACKETED declarator block, possibly over
        // several lines: its trimmed inside is one block
        if (t.size() > 2 && t[0] == '#' && (t[1] == '|' || t[1] == '=') &&
            (t[2] == '{' || t[2] == '(' || t[2] == '[' || t[2] == '<')) {
            const bool lead = t[1] == '|';
            char open = t[2], close = open == '{' ? '}' : open == '(' ? ')' : open == '[' ? ']' : '>';
            std::string body; int d = 1; size_t j = k; size_t pos = 3;
            std::string cur = t;
            while (true) {
                for (; pos < cur.size(); pos++) {
                    if (cur[pos] == open) d++;
                    else if (cur[pos] == close && --d == 0) break;
                    body += cur[pos];
                }
                if (d == 0 || j + 1 >= lines.size()) break;
                body += '\n'; cur = lines[++j]; pos = 0;
            }
            size_t a = body.find_first_not_of(" \t\r\n"), b = body.find_last_not_of(" \t\r\n");
            body = a == std::string::npos ? std::string() : body.substr(a, b - a + 1);
            Value d2 = mkPod("Pod::Block::Declarator");
            Value pc = Value::array(); pc.arr()->push_back(Value::str(body));
            (*d2.hash())["contents"] = pc;
            if (lead) (*d2.hash())["declLine"] = Value::integer((long long)j + 2);
            else {
                (*d2.hash())["declLine"] = Value::integer((long long)k + 1);
                (*d2.hash())["trailingPod"] = Value::boolean(true);
            }
            top.push_back(d2);
            k = j;
            continue;
        }
        if (t.rfind("#|", 0) == 0) { if (!leading.empty()) leading += ' '; leading += trim(t.substr(2)); continue; }
        if (!leading.empty()) {
            Value d = mkPod("Pod::Block::Declarator");
            Value pc = Value::array(); pc.arr()->push_back(Value::str(leading));
            (*d.hash())["contents"] = pc;
            // the declaration it documents is on this line (1-based): `.WHY`
            // links the entry back to its declarand through it
            (*d.hash())["declLine"] = Value::integer((long long)k + 1);
            top.push_back(d);
            leading.clear();
        }
        // `#={yellow}` after code on the same line: the brackets delimit the
        // block, they are not its text
        auto unbracket = [&](std::string x) {
            x = trim(x);
            if (x.size() >= 2) {
                char o = x[0], c = x.back();
                if ((o == '{' && c == '}') || (o == '(' && c == ')') ||
                    (o == '[' && c == ']') || (o == '<' && c == '>'))
                    x = trim(x.substr(1, x.size() - 2));
            }
            return x;
        };
        size_t h = t.find("#=");
        if (h != std::string::npos && (h == 0 || t[h - 1] == ' ' || t[h - 1] == '\t')) {
            // not inside a string literal: no quote opened before it on the line
            bool quoted = false;
            for (size_t q = 0; q < h; q++) if (t[q] == '\'' || t[q] == '"') quoted = !quoted;
            // a whole-line `#=` right under another continues it: `#= multi`
            // then `#= line` is ONE declarator block, "multi line"
            if (!quoted && h == 0 && !top.empty() && top.back().hash() &&
                top.back().hash()->count("trailRunEnd") &&
                (*top.back().hash())["trailRunEnd"].toInt() == (long long)k) {
                auto& c = *(*top.back().hash())["contents"].arr();
                if (!c.empty()) c.back() = Value::str(c.back().toStr() + " " + trim(t.substr(h + 2)));
                (*top.back().hash())["trailRunEnd"] = Value::integer((long long)k + 1);
                continue;
            }
            // …and one right after a LEADING block for the same declaration
            // (this line, or the line above) joins it: `#| a` + `#= b` is the
            // one declarator block "a\nb"
            if (!quoted && !top.empty() && top.back().hash() &&
                top.back().hash()->count("declLine") && !top.back().hash()->count("trailingPod") &&
                ((*top.back().hash())["declLine"].toInt() == (long long)k + 1 ||
                 (h == 0 && (*top.back().hash())["declLine"].toInt() == (long long)k))) {
                auto& c = *(*top.back().hash())["contents"].arr();
                if (!c.empty()) c.back() = Value::str(c.back().toStr() + "\n" + unbracket(t.substr(h + 2)));
                (*top.back().hash())["trailRunEnd"] = Value::integer((long long)k + 1);
                continue;
            }
            if (!quoted) {
                Value d = mkPod("Pod::Block::Declarator");
                Value pc = Value::array(); pc.arr()->push_back(Value::str(unbracket(t.substr(h + 2))));
                (*d.hash())["contents"] = pc;
                // a trailing one documents this line's declaration, or the one
                // just above when it stands on a line of its own
                (*d.hash())["declLine"] = Value::integer((long long)k + 1);
                (*d.hash())["trailingPod"] = Value::boolean(true);
                (*d.hash())["trailRunEnd"] = Value::integer((long long)k + 1);   // the run's last line so far
                top.push_back(d);
            }
        }
    }
    if (!leading.empty()) {
        Value d = mkPod("Pod::Block::Declarator");
        Value pc = Value::array(); pc.arr()->push_back(Value::str(leading));
        (*d.hash())["contents"] = pc;
        top.push_back(d);
    }
    return top;
}

}
