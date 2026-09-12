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
// Shape by default, attributes on request, and that is a deliberate narrowing of
// what the plan specified — see the note in RAKUAST-PLAN Part IV: the
// two-position test the plan gives for deciding which attributes are
// syntax-bearing does not in fact exclude compiler state, because most of that
// state is position-invariant.

#include "RakuAstClasses.h"

#include "Interpreter.h"
#include "Value.h"

#include <ostream>

namespace rakupp {

namespace {

bool isNodeV(const Value& v) {
    return v.t == VT::Object && v.obj() && v.obj()->cls && isRakuAstName(v.obj()->cls->name);
}

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

void dumpNode(const Value& node, int depth, bool withAttrs, std::ostream& out) {
    if (!isNodeV(node) || depth > 60) return;
    const std::string& full = node.obj()->cls->name;
    out << std::string(depth * 2, ' ') << full.substr(9);

    // Attributes and children both come out of the node's own map, in
    // insertion order.
    if (withAttrs)
        for (auto& kv : node.obj()->attrs) {
            if (isNodeV(kv.second) || kv.second.t == VT::Array) continue;
            std::string s = scalarOf(kv.second);
            if (!s.empty()) out << " " << kv.first << "=" << s;
        }
    out << "\n";

    for (auto& kv : node.obj()->attrs) {
        const Value& v = kv.second;
        if (isNodeV(v)) { dumpNode(v, depth + 1, withAttrs, out); continue; }
        if (v.t == VT::Array && v.arr())
            for (auto& e : *v.arr())
                if (isNodeV(e)) dumpNode(e, depth + 1, withAttrs, out);
    }
}

} // namespace

void dumpRakuAst(Interpreter& I, const std::string& source, std::ostream& out,
                 bool compUnit, bool withAttrs) {
    dumpNode(rakuAstView(I, source, compUnit), 0, withAttrs, out);
}

} // namespace rakupp
