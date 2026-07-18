#include "Pod.h"
#include <cctype>
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include "BigInt.h"

namespace rakupp {

static std::string ltrim(const std::string& s) {
    size_t i = 0; while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    return s.substr(i);
}
static std::string strip(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}
// collapse runs of whitespace to single spaces, trimmed
static std::string collapseWs(const std::string& s) {
    std::string out; bool sp = false;
    for (char c : s) {
        if (std::isspace((unsigned char)c)) { sp = true; }
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
    (*v.hash)["podclass"] = Value::str(cls);
    (*v.hash)["contents"] = Value::array();
    return v;
}
static Value mkPara(const std::string& text) {
    Value p = mkPod("Pod::Block::Para");
    Value pc = Value::array();
    pc.arr->push_back(Value::str(collapseWs(text)));
    (*p.hash)["contents"] = pc;
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
            if (!std::isdigit((unsigned char)t[i])) { allDigits = false; break; }
        if (t.size() == 1 && (t[0] == '+' || t[0] == '-')) allDigits = false;
        if (allDigits) {
            errno = 0;
            char* end = nullptr;
            long long iv = std::strtoll(t.c_str(), &end, 10);
            if (errno != ERANGE) return Value::integer(iv);
            return Value::bigint(BigInt::fromString(t[0] == '+' ? t.substr(1) : t));
        }
        char* end = nullptr;
        double dv = std::strtod(t.c_str(), &end);
        if (end && *end == '\0' && end != t.c_str()) return Value::number(dv);
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
            if (std::isspace((unsigned char)c)) { if (!w.empty()) { words.push_back(w); w.clear(); } }
            else w += c;
        }
        if (!w.empty()) words.push_back(w);
        if (words.size() == 1) return Value::str(words[0]);
        Value lst = Value::array(); lst.isList = true;
        for (auto& s : words) lst.arr->push_back(Value::str(s));
        return lst;
    }
    if (open == '{') { // hash: `k => v` pairs; a bare element is the KEY of the next element
        Value h = Value::makeHash();
        std::vector<std::string> pieces; cfgSplitTop(body, ',', pieces);
        std::string pendingKey; bool havePending = false;
        for (auto& p : pieces) {
            size_t ar = cfgFindArrow(p);
            if (ar != std::string::npos) {
                (*h.hash)[cfgKey(p.substr(0, ar))] = cfgScalar(p.substr(ar + 2));
                continue;
            }
            if (havePending) { (*h.hash)[pendingKey] = cfgScalar(p); havePending = false; }
            else { pendingKey = cfgKey(p); havePending = true; }
        }
        return h;
    }
    // ( … ) / [ … ]: a comma list; one element collapses to the element itself
    std::vector<std::string> pieces; cfgSplitTop(body, ',', pieces);
    if (pieces.size() == 1) return cfgScalar(pieces[0]);
    Value lst = Value::array(); lst.isList = true;
    for (auto& p : pieces) lst.arr->push_back(cfgScalar(p));
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
        while (!neg && i < s.size() && std::isdigit((unsigned char)s[i])) digits += s[i++];
        std::string key;
        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '_'))
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
        (*h.hash)[key] = val;
    }
    return h;
}

// `=   :more<config>` continuation of the previous directive's config line
static bool cfgContLine(const std::string& ln, std::string& content) {
    size_t p = ln.find_first_not_of(" \t");
    if (p == std::string::npos || ln[p] != '=') return false;
    if (p + 1 >= ln.size() || !std::isspace((unsigned char)ln[p + 1])) return false;
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
    while (i < t.size() && (std::isalnum((unsigned char)t[i]) || t[i] == '_')) w += t[i++];
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

// head1/head2/… or item/item1/… → (base, level). base is "head"/"item"; level
// defaults to 1 when no trailing digits.
static bool splitLeveled(const std::string& kw, const std::string& base, int& level) {
    if (kw.compare(0, base.size(), base) != 0) return false;
    std::string tail = kw.substr(base.size());
    if (tail.empty()) { level = 1; return true; }
    for (char c : tail) if (!std::isdigit((unsigned char)c)) return false;
    level = std::stoi(tail);
    return true;
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

// The block's virtual left margin: the indent of its first non-blank, non-directive
// content line. Text at the margin is ordinary; text indented past it is a code block.
static int blockMargin(const std::vector<std::string>& lines, size_t start) {
    for (size_t j = start; j < lines.size(); j++) {
        std::string kw, rest;
        if (matchDirective(lines[j], kw, rest)) { if (kw == "end") return 0; continue; }
        if (!strip(lines[j]).empty()) return indentOf(lines[j]);
    }
    return 0;
}

static void parseSeq(const std::vector<std::string>& lines, size_t& i,
                     const std::string& closeName, bool inBlock, ValueList& out, int margin);

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
                if (cls == "Pod::Block::Named") (*block.hash)["name"] = Value::str(name);
                if (lv) (*block.hash)["level"] = Value::integer(lv);
                if (cfg.find(':') != std::string::npos) (*block.hash)["config"] = podParseConfig(cfg);
                ValueList inner;
                if (cls == "Pod::Block::Code" || cls == "Pod::Block::Comment") { // verbatim contents
                    std::vector<std::string> code; std::string k2, r2;
                    while (i < lines.size() && !(matchDirective(lines[i], k2, r2) && k2 == "end" && firstWord(r2) == name))
                        { code.push_back(lines[i]); i++; }
                    while (!code.empty() && strip(code.back()).empty()) code.pop_back();
                    std::string text; for (size_t k = 0; k < code.size(); k++) { if (k) text += "\n"; text += code[k]; }
                    if (cls == "Pod::Block::Comment" && !text.empty()) text += "\n"; // comments keep a trailing NL
                    Value cc = Value::array(); if (!text.empty()) cc.arr->push_back(Value::str(text));
                    (*block.hash)["contents"] = cc;
                    if (i < lines.size()) i++;
                    out.push_back(block);
                    continue;
                }
                parseSeq(lines, i, name, true, inner, blockMargin(lines, i));
                Value ic = Value::array(); *ic.arr = std::move(inner);
                (*block.hash)["contents"] = ic;
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
                if (cls == "Pod::Block::Named") (*block.hash)["name"] = Value::str(name);
                if (lv) (*block.hash)["level"] = Value::integer(lv);
                if (cfg.find(':') != std::string::npos) (*block.hash)["config"] = podParseConfig(cfg);
                Value ic = Value::array();
                if (cls == "Pod::Block::Comment" || cls == "Pod::Block::Code") {
                    std::string v = collectVerbatim(lines, i, cls == "Pod::Block::Comment");
                    if (!v.empty()) ic.arr->push_back(Value::str(v));
                } else {
                    std::string para = collectPara(lines, i);
                    if (!para.empty()) ic.arr->push_back(mkPara(para));
                }
                (*block.hash)["contents"] = ic;
                out.push_back(block);
                continue;
            }
            int level = 0;
            if (splitLeveled(kw, "head", level)) {
                std::string text = rest; i++;
                std::string cont = collectPara(lines, i);
                if (!cont.empty()) text += (text.empty() ? "" : " ") + cont;
                Value h = mkPod("Pod::Heading");
                (*h.hash)["level"] = Value::integer(level);
                Value ic = Value::array(); ic.arr->push_back(mkPara(text));
                (*h.hash)["contents"] = ic;
                out.push_back(h);
                continue;
            }
            if (splitLeveled(kw, "item", level)) {
                std::string text = rest; i++;
                std::string cont = collectPara(lines, i);
                if (!cont.empty()) text += (text.empty() ? "" : " ") + cont;
                Value it = mkPod("Pod::Item");
                (*it.hash)["level"] = Value::integer(level);
                Value ic = Value::array(); ic.arr->push_back(mkPara(text));
                (*it.hash)["contents"] = ic;
                out.push_back(it);
                continue;
            }
            if (kw == "config") { // `=config TYPE :key<val> …` — a Pod::Config node
                std::string type = firstWord(rest);
                i++;
                std::string cfg = cfgTextAfterName(rest, type, lines, i);
                Value node = mkPod("Pod::Config");
                (*node.hash)["type"] = Value::str(type);
                (*node.hash)["config"] = podParseConfig(cfg);
                (*node.hash)["contents"] = Value::array();
                out.push_back(node);
                continue;
            }
            if (kw == "comment") { // abbreviated: verbatim body until a blank line
                i++;
                std::string v = collectVerbatim(lines, i, true);
                if (!rest.empty()) v = rest + (v.empty() ? std::string("\n") : "\n" + v);
                Value cm = mkPod("Pod::Block::Comment");
                Value cc = Value::array(); if (!v.empty()) cc.arr->push_back(Value::str(v));
                (*cm.hash)["contents"] = cc;
                out.push_back(cm);
                continue;
            }
            if (kw == "pod") { // =pod … =end pod delimiter-less start OR abbreviated; treat like a named block
                std::string name = "pod"; i++;
                Value block = mkPod("Pod::Block::Named");
                (*block.hash)["name"] = Value::str(name);
                ValueList inner; parseSeq(lines, i, name, true, inner, blockMargin(lines, i));
                Value ic = Value::array(); *ic.arr = std::move(inner);
                (*block.hash)["contents"] = ic;
                if (i < lines.size()) i++;
                out.push_back(block);
                continue;
            }
            // abbreviated `=name text` → a named block holding one paragraph
            std::string name = kw;
            std::string text = rest; i++;
            std::string cont = collectPara(lines, i);
            if (!cont.empty()) text += (text.empty() ? "" : " ") + cont;
            Value block = mkPod("Pod::Block::Named");
            (*block.hash)["name"] = Value::str(name);
            Value ic = Value::array(); if (!text.empty()) ic.arr->push_back(mkPara(text));
            (*block.hash)["contents"] = ic;
            out.push_back(block);
            continue;
        }
        if (strip(lines[i]).empty()) { i++; continue; }
        if (inBlock) {
            if (indentOf(lines[i]) > margin) {
                // implicit code block: verbatim, may span internal blank lines; dedent by
                // the least indent of its non-blank lines.
                std::vector<std::string> code; std::string k2, r2;
                while (i < lines.size()) {
                    if (matchDirective(lines[i], k2, r2)) break;
                    if (!strip(lines[i]).empty() && indentOf(lines[i]) <= margin) break;
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
                Value cc = Value::array(); cc.arr->push_back(Value::str(text));
                (*cb.hash)["contents"] = cc;
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

std::vector<Value> parsePod(const std::string& src) {
    std::vector<std::string> lines;
    { std::stringstream ss(src); std::string ln; while (std::getline(ss, ln)) lines.push_back(ln); }
    ValueList top; size_t i = 0;
    parseSeq(lines, i, "", false, top, 0);
    return top;
}

}
