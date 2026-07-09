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

static void parseSeq(const std::vector<std::string>& lines, size_t& i,
                     const std::string& closeName, bool inBlock, ValueList& out);

static void parseSeq(const std::vector<std::string>& lines, size_t& i,
                     const std::string& closeName, bool inBlock, ValueList& out) {
    while (i < lines.size()) {
        std::string kw, rest;
        if (matchDirective(lines[i], kw, rest)) {
            if (kw == "end") {
                if (!closeName.empty() && firstWord(rest) == closeName) return; // caller consumes =end
                i++; continue; // stray =end
            }
            if (kw == "begin") {
                std::string name = firstWord(rest);
                i++;
                Value block = mkPod("Pod::Block::Named");
                (*block.hash)["name"] = Value::str(name);
                ValueList inner;
                parseSeq(lines, i, name, true, inner);
                Value ic = Value::array(); *ic.arr = std::move(inner);
                (*block.hash)["contents"] = ic;
                if (i < lines.size()) i++; // consume the =end line
                out.push_back(block);
                continue;
            }
            if (kw == "for") { // paragraph block: the following paragraph is its content
                std::string name = firstWord(rest);
                i++;
                std::string para = collectPara(lines, i);
                Value block = mkPod("Pod::Block::Named");
                (*block.hash)["name"] = Value::str(name);
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
                ValueList inner; parseSeq(lines, i, name, true, inner);
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
            std::string para = collectPara(lines, i);
            out.push_back(mkPara(para));
        } else {
            i++; // top-level code between POD blocks
        }
    }
}

std::vector<Value> parsePod(const std::string& src) {
    std::vector<std::string> lines;
    { std::stringstream ss(src); std::string ln; while (std::getline(ss, ln)) lines.push_back(ln); }
    ValueList top; size_t i = 0;
    parseSeq(lines, i, "", false, top);
    return top;
}

}
