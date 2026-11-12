// Is the native multi dispatcher's candidate order Raku's? (Codegen::multiDef)
//
// The dispatcher `--exe` emits for a `multi sub` tries the candidates in one
// fixed order — literal parameters before typed ones before untyped ones,
// declaration order on a tie — and takes the first whose positional guards
// pass. It reads no named argument and knows no type hierarchy, so wherever
// that order is not the narrowest-first order the interpreter dispatches by, a
// compiled program silently ran another candidate: `multi f(Mu $x)` declared
// before `multi f(Any $x)` answered 'Mu' for every argument.
//
// This decides, from the signatures alone, whether the order is right for
// every call that two candidates could both take: either the two can never
// take the same arguments (different literals, disjoint core types, unrelated
// classes, arities that do not meet), or the earlier one is at least as
// narrow at every position. Anything it cannot decide refuses, and the
// program bundles with the interpreter, which dispatches it properly.
#include "../Ast.h"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rakupp {

namespace {

const std::set<std::string>& coreConcrete() {
    // the core value types no instance of one of which is an instance of another
    // (Bool is an Int, so it is folded into Int before asking)
    static const std::set<std::string> s = {"Int", "Str", "Num", "Rat", "FatRat", "Complex"};
    return s;
}

struct Classes {
    std::map<std::string, std::vector<std::string>> parents;   // class -> its `is` and `does` names
    std::set<std::string> roles;
    // `a` is `b` or inherits / does it, as far as the program shows
    bool isA(const std::string& a, const std::string& b, int depth = 0) const {
        if (a == b) return true;
        if (depth > 32) return true;   // (a cycle: be unsure, which reads as related)
        auto it = parents.find(a);
        if (it == parents.end()) return false;
        for (auto& p : it->second) if (isA(p, b, depth + 1)) return true;
        return false;
    }
    bool known(const std::string& t) const { return parents.count(t) != 0; }
};

std::string litKind(const Expr* e) {
    if (!e) return "";
    if (e->kind == NK::IntLit) return "Int";
    if (e->kind == NK::StrLit) return "Str";
    return "?";
}
bool sameLit(const Expr* a, const Expr* b) {
    if (a->kind != b->kind) return false;
    if (a->kind == NK::IntLit) {
        auto* x = static_cast<const IntLit*>(a); auto* y = static_cast<const IntLit*>(b);
        return x->v == y->v && x->big == y->big;
    }
    if (a->kind == NK::StrLit) return static_cast<const StrLit*>(a)->v == static_cast<const StrLit*>(b)->v;
    return true;   // (some other literal: assume the same, which is the unsure answer)
}
bool broad(const std::string& t) { return t.empty() || t == "Any"; }
std::string folded(const std::string& t) { return t == "Bool" ? "Int" : t; }

// Can no argument satisfy both parameters?
bool disjoint(const Param& a, const Param& b, const Classes& cl) {
    const Expr* la = a.litVal.get(); const Expr* lb = b.litVal.get();
    if (la && lb) return litKind(la) != "?" && litKind(lb) != "?" && !sameLit(la, lb);
    if (la || lb) {
        const std::string k = litKind(la ? la : lb), t = folded(la ? b.type : a.type);
        return k != "?" && coreConcrete().count(t) && t != k;
    }
    const std::string x = folded(a.type), y = folded(b.type);
    if (x == y || broad(x) || broad(y) || x == "Mu" || y == "Mu") return false;
    if (coreConcrete().count(x) && coreConcrete().count(y)) return true;
    // two classes of this program, neither of which is the other, and no role
    // between them (a role can be done by a subclass of either)
    if (cl.known(x) && cl.known(y) && !cl.roles.count(x) && !cl.roles.count(y))
        return !cl.isA(x, y) && !cl.isA(y, x);
    // a class of this program against a core value type
    if ((cl.known(x) && !cl.roles.count(x) && coreConcrete().count(y)) ||
        (cl.known(y) && !cl.roles.count(y) && coreConcrete().count(x)))
        return true;
    return false;
}

// Does every argument `a` accepts also satisfy `b`?
bool within(const Param& a, const Param& b, const Classes& cl) {
    const Expr* la = a.litVal.get(); const Expr* lb = b.litVal.get();
    if (lb) return la && sameLit(la, lb);
    const std::string y = folded(b.type);
    if (y == "Mu") return true;
    if (la) {
        const std::string k = litKind(la);
        return k != "?" && (broad(y) || y == k || (y == "Cool" || y == "Stringy" ? k == "Str" : false) ||
                            ((y == "Numeric" || y == "Real" || y == "Cool") && k == "Int"));
    }
    const std::string x = a.type == "Bool" && y == "Int" ? "Int" : a.type;
    if (x == b.type) return true;
    if (broad(y)) return x != "Mu" && x != "Junction";
    return cl.known(x) && cl.isA(x, y);
}

struct Shape {
    std::vector<const Param*> pos;
    size_t req = 0, max = 0;
    bool unbounded = false;
};

} // namespace

// "" when the order is sound; else why not
std::string multiOrderRefusal(const std::vector<SubDecl*>& sorted, const std::vector<const Program*>& units,
                              int (*natCount)(const SubDecl*)) {
    Classes cl;
    std::function<void(const std::vector<StmtPtr>&)> collect = [&](const std::vector<StmtPtr>& ss) {
        for (auto& st : ss) {
            if (!st || st->kind != NK::ClassDecl) continue;
            auto* c = static_cast<const ClassDecl*>(st.get());
            auto& ps = cl.parents[c->name];
            if (!c->parent.empty()) ps.push_back(c->parent);
            for (auto& p : c->extraParents) ps.push_back(p);
            for (auto& r : c->roles) ps.push_back(r);
            if (c->isRole) cl.roles.insert(c->name);
            collect(c->body);
        }
    };
    for (const Program* u : units) if (u) collect(u->stmts);

    std::vector<Shape> shapes;
    std::set<std::string> namedSet;
    bool first = true;
    for (const SubDecl* d : sorted) {
        Shape s;
        std::set<std::string> named;
        for (auto& p : d->params) {
            if (p.invocant) continue;
            if (p.typeCapture || !p.captureName.empty() || p.coerce || p.subSig || p.codeSig)
                return "a multi candidate with a capture, coercion or sub-signature parameter";
            if (p.named) {
                if (p.required) return "multi candidates told apart by a required named parameter";
                named.insert(p.name);
                continue;
            }
            if (p.slurpy) { s.unbounded = true; continue; }
            s.pos.push_back(&p);
            if (p.defaultVal || p.optional) s.max++;
            else { s.req++; s.max++; }
        }
        // a named argument only some candidates declare picks among them
        if (first) namedSet = named, first = false;
        else if (named != namedSet) return "multi candidates told apart by their named parameters";
        shapes.push_back(std::move(s));
    }
    for (size_t i = 0; i < sorted.size(); i++)
        for (size_t j = i + 1; j < sorted.size(); j++) {
            const Shape& a = shapes[i]; const Shape& b = shapes[j];
            const bool meet = (a.unbounded || b.req <= a.max) && (b.unbounded || a.req <= b.max);
            if (!meet) continue;
            // (a candidate with a native parameter is ordered by the native rules —
            // natives first, rtNatArg's kinds, nativeWidthTie — which this leaves be)
            if (natCount(sorted[i]) || natCount(sorted[j])) continue;
            const size_t n = std::min(a.pos.size(), b.pos.size());
            bool apart = false, narrower = true, wider = true;
            for (size_t k = 0; k < n; k++) {
                if (disjoint(*a.pos[k], *b.pos[k], cl)) { apart = true; break; }
                narrower = narrower && within(*a.pos[k], *b.pos[k], cl);
                wider = wider && within(*b.pos[k], *a.pos[k], cl);
            }
            // (equally narrow at every position is a tie, which Raku does not
            // settle by declaration order)
            if (apart || (narrower && !wider)) continue;
            return "multi candidates `" + sorted[i]->name + "` whose order the native dispatcher cannot settle";
        }
    return "";
}

} // namespace rakupp
