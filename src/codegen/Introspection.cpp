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

// The first construct in `prog` a native program cannot answer, or "".
std::string introspectionRefusal(const Program& prog) {
    // `.^name` and the ones the native class table answers as the
    // interpreter's does; every other metamodel call asks the interpreter
    static const std::set<std::string> nativeMeta = {"name", "shortname", "mro", "can", "isa"};
    std::string why;
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
            default: break;
        }
        forEachAstChild(n, walk);
    };
    for (auto& st : prog.stmts) walk(st.get());
    return why;
}

} // namespace rakupp
