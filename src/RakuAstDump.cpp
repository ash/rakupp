// `rakupp --rakuast SRC` — print the RakuAST view of a program as an indented
// tree (RAKUAST-PLAN Part III). The sibling of `--ast`, which prints OUR tree.
//
// The point is measurement, not pretty-printing: `tools/rakuast-oracle-dump.raku`
// prints Rakudo's own tree in the same serialization, so comparing the two
// engines is a shell `diff` and polishing the view is — pick a file, diff, fix
// the first differing line, repeat.
//
// THE SERIALIZATION: one node per line, two spaces of indent per depth, the
// class name with `RakuAST::` stripped, children in the order the view SET
// them, which is source order. (The attribute map is insertion-ordered — see
// ValueHash.h — so this side gets source order for free, and the oracle side
// gets it from `visit-children`. Sorting by attribute name instead, which is
// what the first version of both dumps did, aligns the two engines on a key
// order neither engine's tree is built in, and on the oracle side it also drags
// in the UPWARD links a RakuAST node carries — a declaration's containing
// block, a statement list's comp unit — turning the walk into a graph walk.)
// With `--rakuast=attrs`, each node's scalar attributes follow as ` key=value`,
// in the same order.
//
// Each line also carries the RAKU that node renders back to, in a second
// column, and `--rakuast=tree` drops it. NOT `-q`: that flag's contract is to
// drop a mode's progress and success lines and never to change its product,
// and `--ast` shows what it means for a dump mode — nothing at all. A mode
// that has knobs spells them in its own value list, the way `--slim` does.
//
// Shape by default, attributes on request, and that is a deliberate narrowing of
// what the plan specified — see the note in RAKUAST-PLAN Part IV: the
// two-position test the plan gives for deciding which attributes are
// syntax-bearing does not in fact exclude compiler state, because most of that
// state is position-invariant.

#include "RakuAstClasses.h"

#include "Interpreter.h"
#include "Value.h"

#include <algorithm>
#include <ostream>
#include <sstream>
#include <vector>

namespace rakupp {

namespace {

// A scalar attribute rendered the way the oracle side renders it: a string
// .raku-quoted, an Int plain, a Bool True/False.
std::string scalarOf(const Value& v) {
    switch (v.t) {
        case VT::Str:  {
            std::string out = "\"";
            for (char c : v.toStr()) {
                if (c == '"' || c == '\\' || c == '$' || c == '@') out += '\\';
                out += c;
            }
            return out + "\"";
        }
        case VT::Int:  return v.toStr();
        case VT::Num:  return v.toStr();
        case VT::Bool: return v.truthy() ? "True" : "False";
        default:       return std::string();
    }
}

// One line's worth: the indented class name, and the Raku that node renders
// back to. Collected before anything is printed so the source column can be
// aligned — the tree's width is not known until the walk is over.
struct DumpLine { std::string tree, src; };

// A node's own source, on ONE line. A block renders over several and its
// braces would swamp the column, so the text is flattened and cut; a node the
// renderer has no arm for simply contributes nothing rather than an error.
std::string oneLineSource(Interpreter& I, const Value& node) {
    std::string s;
    try { s = rakuAstDeparse(I, node); } catch (...) { return std::string(); }
    std::string flat;
    bool sp = false;
    for (char c : s) {
        if (c == '\n' || c == '\t' || c == ' ') { sp = true; continue; }
        if (sp && !flat.empty()) flat += ' ';
        sp = false;
        flat += c;
    }
    const size_t cap = 64;
    if (flat.size() > cap) {
        // Cut on a UTF-8 boundary, or the ellipsis lands mid-character.
        size_t k = cap;
        while (k > 0 && ((unsigned char)flat[k] & 0xC0) == 0x80) k--;
        flat = flat.substr(0, k) + "\xE2\x80\xA6";
    }
    return flat;
}

void dumpNode(Interpreter& I, const Value& node, int depth, bool withAttrs,
              bool withSource, std::vector<DumpLine>& lines) {
    if (!isRakuAstNode(node) || depth > 60) return;
    const std::string& full = node.obj()->cls->name;
    std::ostringstream out;
    out << std::string(depth * 2, ' ') << full.substr(9);

    // Attributes and children both come out of the node's own map, in
    // insertion order.
    if (withAttrs)
        for (auto& kv : node.obj()->attrs) {
            if (isRakuAstNode(kv.second) || kv.second.t == VT::Array) continue;
            std::string s = scalarOf(kv.second);
            if (!s.empty()) out << " " << kv.first << "=" << s;
        }

    lines.push_back({out.str(), withSource ? oneLineSource(I, node) : std::string()});

    for (auto& kv : node.obj()->attrs) {
        const Value& v = kv.second;
        if (isRakuAstNode(v)) { dumpNode(I, v, depth + 1, withAttrs, withSource, lines); continue; }
        if (v.t == VT::Array && v.arr())
            for (auto& e : *v.arr())
                if (isRakuAstNode(e)) dumpNode(I, e, depth + 1, withAttrs, withSource, lines);
    }
}

} // namespace

void dumpRakuAst(Interpreter& I, const std::string& source, std::ostream& out,
                 bool compUnit, bool withAttrs, bool withSource) {
    std::vector<DumpLine> lines;
    dumpNode(I, rakuAstView(I, source, compUnit), 0, withAttrs, withSource, lines);
    size_t w = 0;
    if (withSource)
        for (auto& l : lines) if (!l.src.empty()) w = std::max(w, l.tree.size());
    for (auto& l : lines) {
        out << l.tree;
        if (withSource && !l.src.empty()) out << std::string(w - l.tree.size() + 2, ' ') << "\xE2\x94\x82 " << l.src;
        out << "\n";
    }
}

} // namespace rakupp
