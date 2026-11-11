// What a program asks about its own lexical scope, its packages and its
// metaobjects — which a natively compiled program cannot answer as the
// interpreter does (Codegen.cpp: transpileToCpp, and routineNativeRefusal for a
// routine compiled on its own).
//
// A native program keeps its locals as C++ variables and its routines as C++
// functions; the interpreter it carries never sees them. So a pseudo-package
// (`MY::`, `UNIT::{'&f'}:exists`, `OUTER::<$x>`), a package's stash term
// (`A::`), an `EVAL` (whose code cannot call the program's subs or read its
// variables), a variable's `.VAR.dynamic`, a parameter's `.WHY`, and the
// metamodel beyond a type's name (`.^methods`, `.^ver`, `.HOW`, which the
// native class registration does not model) answered differently compiled —
// mostly empty. Each of them refuses, and the program bundles with the
// interpreter. Kept apart from Codegen.cpp, which is at its size ceiling.
#include "../Codegen.h"
#include "../Ast.h"
#include <functional>
#include <set>
#include <string>

namespace rakupp {

// A call the parser makes for a pseudo-package lookup: `__sym-lookup`,
// `__sym-exists`, `__sym-pair`, `__pseudo-stash`, `__client-lookup`, …
bool introspectiveCallName(const std::string& n) {
    return n.rfind("__sym-", 0) == 0 || n.rfind("__pseudo-", 0) == 0 || n == "__client-lookup";
}

// `@p.VAR.name` / `@p.name`: the name of the container a variable is bound
// to — the caller's `my @x` for a parameter — which a native variable has not
static bool containerName(const MethodCall* m) {
    if (m->meta || m->method != "name" || !m->inv) return false;
    const Expr* i = m->inv.get();
    if (i->kind == NK::MethodCall && static_cast<const MethodCall*>(i)->method == "VAR") return true;
    if (i->kind != NK::VarExpr) return false;
    const std::string& v = static_cast<const VarExpr*>(i)->name;
    return !v.empty() && (v[0] == '@' || v[0] == '%');
}

// `$/.make(…)` / `.make: …`: the interpreter's method-call arm attaches the
// value to the match itself (and to the node an action is building); the
// native method call finds no `make` method on a grammar's match at all
static bool madeOnMatch(const MethodCall* m) { return !m->meta && m->method == "make"; }

// The first construct in `prog` a native program cannot answer, or "".
std::string introspectionRefusal(const Program& prog) {
    // `.^name` and the ones the native class table answers as the
    // interpreter's does; every other metamodel call asks the interpreter
    static const std::set<std::string> nativeMeta = {"name", "shortname", "mro", "can", "isa"};
    std::string why;
    std::set<std::string> dyns, globals;   // `$*X` read or written as `$X`; GLOBAL's `$X`
    auto params = [&](const std::vector<Param>& ps) {
        for (auto& p : ps)
            if (!p.pod.empty() || !p.podTrail.empty() || !p.podLead.empty())
                why = "a parameter's declarator doc (its .WHY)";
    };
    std::function<void(Node*)> walk = [&](Node* n) {
        if (!n || !why.empty()) return;
        switch (n->kind) {
            case NK::Call: {
                const std::string& nm = static_cast<Call*>(n)->name;
                if (introspectiveCallName(nm))
                    why = "a pseudo-package lookup (MY::, UNIT::, OUTER::, …) — native locals and routines have no runtime names";
                else if (nm == "EVAL" || nm == "EVALFILE")
                    why = "`" + nm + "` — the evaluated code cannot see the program's native routines and variables";
                break;
            }
            case NK::MethodCall: {
                auto* m = static_cast<MethodCall*>(n);
                if (m->method == "EVAL" || m->method == "EVALFILE")
                    why = "`." + m->method + "` — the evaluated code cannot see the program's native routines and variables";
                else if (m->meta && !nativeMeta.count(m->method))
                    why = "`.^" + m->method + "` — the native class table is no metaobject";
                else if (!m->meta && (m->method == "HOW" || m->method == "WHO"))
                    why = "`." + m->method + "` — the native class table is no metaobject";
                else if (!m->meta && m->method == "dynamic")
                    why = "`.dynamic` — a native variable carries no traits";
                else if (containerName(m))
                    why = "a container's name (`.VAR.name`) — a native variable is bound to none";
                else if (madeOnMatch(m))
                    why = "a Match's `.make` (the interpreter sets the match itself)";
                break;
            }
            case NK::NameTerm: {
                const std::string& nm = static_cast<NameTerm*>(n)->name;
                if (nm.size() > 2 && nm.compare(nm.size() - 2, 2, "::") == 0)
                    why = "a package's stash (`" + nm + "`)";
                break;
            }
            case NK::SubDecl: params(static_cast<SubDecl*>(n)->params); break;
            case NK::BlockExpr: params(static_cast<BlockExpr*>(n)->params); break;
            case NK::VarExpr: {   // (the GLOBAL fallback below)
                auto* v = static_cast<VarExpr*>(n);
                const std::string& nm = v->name;
                if (nm.size() > 2 && nm[1] == '*') dyns.insert(nm.substr(0, 1) + nm.substr(2));
                else if (nm.size() > 9 && nm.compare(1, 8, "GLOBAL::") == 0) globals.insert(nm.substr(0, 1) + nm.substr(9));
                else if (v->declare && v->d().declScope == "our") globals.insert(nm);
                break;
            }
            default: break;
        }
        forEachAstChild(n, walk);
    };
    for (auto& st : prog.stmts) walk(st.get());
    // `$*X` that nothing declares is GLOBAL's `$X` — which, written as a
    // top-level `our $X` or `$GLOBAL::X`, the native program keeps in a C++
    // variable the run-time dynamic lookup does not see
    if (why.empty())
        for (auto& g : globals)
            if (dyns.count(g)) return "a dynamic variable read from GLOBAL's " + g + " — a native variable";
    return why;
}

// …and in one routine compiled on its own (module mode, aotIneligible), what
// ties its meaning to the scope around it rather than to its statements: an
// `our` (its package's variable — a module sub's `our $v; $v++` counted from
// Any every call), a `use` (its imports are the routine's scope: a sub it
// imports did not shadow an outer one), a container's name.
const char* routineIntrospectionRefusal(const Program& body) {
    const char* why = nullptr;
    std::function<void(Node*)> walk = [&](Node* n) {
        if (!n || why) return;
        if (n->kind == NK::VarExpr && static_cast<VarExpr*>(n)->declare &&
            static_cast<VarExpr*>(n)->d().declScope == "our")
            why = "an `our` variable (its package's, not the routine's)";
        else if (n->kind == NK::UseStmt) why = "a `use` inside the routine";
        else if (n->kind == NK::MethodCall && containerName(static_cast<MethodCall*>(n)))
            why = "a container's name (`.VAR.name`)";
        else if (n->kind == NK::MethodCall && madeOnMatch(static_cast<MethodCall*>(n)))
            why = "a Match's `.make`";
        forEachAstChild(n, walk);
    };
    for (auto& st : body.stmts) walk(st.get());
    return why;
}

} // namespace rakupp
