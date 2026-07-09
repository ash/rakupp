#include "Pod.h"
#include <cctype>
#include <sstream>

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
                Value block = mkPod(cls);
                if (cls == "Pod::Block::Named") (*block.hash)["name"] = Value::str(name);
                if (lv) (*block.hash)["level"] = Value::integer(lv);
                ValueList inner;
                if (cls == "Pod::Block::Code") { // verbatim: the raw lines are the contents
                    std::vector<std::string> code; std::string k2, r2;
                    while (i < lines.size() && !(matchDirective(lines[i], k2, r2) && k2 == "end" && firstWord(r2) == name))
                        { code.push_back(lines[i]); i++; }
                    while (!code.empty() && strip(code.back()).empty()) code.pop_back();
                    std::string text; for (size_t k = 0; k < code.size(); k++) { if (k) text += "\n"; text += code[k]; }
                    Value cc = Value::array(); cc.arr->push_back(Value::str(text));
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
                std::string para = collectPara(lines, i);
                Value block = mkPod(cls);
                if (cls == "Pod::Block::Named") (*block.hash)["name"] = Value::str(name);
                if (lv) (*block.hash)["level"] = Value::integer(lv);
                Value ic = Value::array(); if (!para.empty()) ic.arr->push_back(mkPara(para));
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
            if (kw == "comment") { i++; collectPara(lines, i); out.push_back(mkPod("Pod::Block::Comment")); continue; }
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
