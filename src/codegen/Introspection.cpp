// What a program asks about its own lexical scope, its packages and its
// metaobjects — which a natively compiled program cannot answer as the
// interpreter does (Codegen.cpp: transpileToCpp, and routineNativeRefusal for a
// routine compiled on its own).
//
// A native program keeps its locals as C++ variables and its routines as C++
// functions; the interpreter it carries never sees them. So a pseudo-package
// (`MY::`, `UNIT::{'&f'}:exists`, `OUTER::<$x>`), a package's stash term
// (`A::`), an `EVAL` whose code may ask for the program's subs or variables
// (one that reads none of them runs natively), a variable's `.VAR.dynamic`,
// a parameter's `.WHY`, and the metamodel beyond a type's name (`.^methods`, `.^ver`, `.HOW`, which the
// native class registration does not model) answered differently compiled —
// mostly empty. Each of them refuses, and the program bundles with the
// interpreter. Kept apart from Codegen.cpp, which is at its size ceiling.
#include "../Codegen.h"
#include "../Ast.h"
#include "../AsciiCtype.h"
#include "../Lexer.h"
#include "../Parser.h"
#include <cstring>
#include <functional>
#include <set>
#include <string>

namespace rakupp {

std::vector<std::string> suggestNames(const std::string& name, const std::vector<std::string>& cands);   // Interpreter.h

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

// The text of a literal string — `'…'`, or `"…"` with nothing interpolated
// (an InterpStr of one StrLit) — or null.
static const std::string* literalText(const Expr* e) {
    if (!e) return nullptr;
    if (e->kind == NK::StrLit) return &static_cast<const StrLit*>(e)->v;
    if (e->kind == NK::InterpStr) {
        auto* is = static_cast<const InterpStr*>(e);
        if (is->parts.size() == 1 && is->parts[0]->kind == NK::StrLit)
            return &static_cast<const StrLit*>(is->parts[0].get())->v;
    }
    return nullptr;
}

// The names a program declares or reads that a native EVAL cannot see —
// its variables (a constant or `my \x` among them, without a sigil), its
// parameters, its subs and its enum values — and whether it declares an
// operator, which an EVAL'd text would parse differently without.
struct ProgNames {
    std::set<std::string> names;
    std::vector<std::string> subs;   // its routines' names, which an undeclared call's error suggests
    std::set<std::string> ops;   // the symbols of the operators it declares (`infix:<pp>`: "pp")
    bool opaqueOp = false;       // …one whose symbol is not spelled `<…>`
    bool enums = false;          // an enum, whose values a `.raku` names
    bool ownRaku = false;        // a `raku` or `perl` method of its own
};
static void collectProgNames(Node* n, ProgNames& pn) {
    if (!n) return;
    auto params = [&](const std::vector<Param>& ps) { for (auto& p : ps) if (!p.name.empty()) pn.names.insert(p.name); };
    switch (n->kind) {
        case NK::VarExpr: pn.names.insert(static_cast<VarExpr*>(n)->name); break;
        case NK::SubDecl: {
            auto* s = static_cast<SubDecl*>(n);
            size_t lt = s->name.find(":<");
            if (lt != std::string::npos && s->name.size() > lt + 3 && s->name.back() == '>')
                pn.ops.insert(s->name.substr(lt + 2, s->name.size() - lt - 3));
            else if (s->name.find(":\u00ab") != std::string::npos || s->name.find(":(") != std::string::npos)
                pn.opaqueOp = true;
            if (s->name == "raku" || s->name == "perl") pn.ownRaku = true;
            if (!s->name.empty()) { pn.names.insert(s->name); pn.names.insert("&" + s->name); pn.subs.push_back(s->name); }
            params(s->params);
            break;
        }
        case NK::BlockExpr: params(static_cast<BlockExpr*>(n)->params); break;
        case NK::EnumDecl: {
            pn.enums = true;
            std::function<void(Node*)> words = [&](Node* c) {
                if (!c) return;
                if (c->kind == NK::StrLit) {
                    const std::string& v = static_cast<StrLit*>(c)->v;
                    size_t i = 0;
                    while (i < v.size()) {
                        size_t j = v.find_first_of(" \t\n", i);
                        if (j == std::string::npos) j = v.size();
                        if (j > i) pn.names.insert(v.substr(i, j - i));
                        i = j + 1;
                    }
                }
                forEachAstChild(c, words);
            };
            words(static_cast<EnumDecl*>(n)->values.get());
            break;
        }
        default: break;
    }
    forEachAstChild(n, [&](Node* c) { collectProgNames(c, pn); });
}

// What makes an EVAL's code ask the program for something a native program
// does not keep where the interpreter looks: its variables, subs, constants
// and enum values (C++ variables and functions here), an OUTER::/MY::
// lookup, a package-qualified name, `$!`, and a change to or a metaobject
// question about one of the program's own classes. Its classes, grammars,
// `$_`/`$/` at the top level, dynamic variables and `take` it sees as
// interpreted, and its own declarations are its own.
static std::string evalCodeAsks(const Program& code, const ProgNames& pn, const std::set<std::string>& ownPkgs) {
    static const std::set<std::string> nativeMeta = {"name", "shortname", "mro", "can", "isa"};
    static const std::set<std::string> pseudo = {"GLOBAL", "OUTER", "MY", "OUR", "CALLER", "DYNAMIC", "LEXICAL",
                                                 "UNIT", "SETTING", "CLIENT", "PROCESS"};
    std::set<std::string> own;                 // what the EVAL'd code declares
    std::vector<const std::string*> used;      // what it reads by name
    std::string asks;
    bool ownPkgUse = false, metaOrDecl = false;
    std::function<void(Node*)> walk = [&](Node* n) {
        if (!n || !asks.empty()) return;
        auto params = [&](const std::vector<Param>& ps) { for (auto& p : ps) if (!p.name.empty()) own.insert(p.name); };
        switch (n->kind) {
            case NK::VarExpr: {
                auto* v = static_cast<VarExpr*>(n);
                if (v->declare) own.insert(v->name);
                else if (v->name == "$!" || v->name.find("::") != std::string::npos) asks = "reads `" + v->name + "`";
                else used.push_back(&v->name);
                break;
            }
            case NK::Call: {
                const std::string& nm = static_cast<Call*>(n)->name;
                if (introspectiveCallName(nm)) asks = "looks up a name through a pseudo-package";
                else if (nm.find("::") != std::string::npos) asks = "calls `" + nm + "`";
                else used.push_back(&nm);
                break;
            }
            case NK::NameTerm: {
                const std::string& nm = static_cast<NameTerm*>(n)->name;
                size_t c = nm.find("::");
                std::string head = c == std::string::npos ? nm : nm.substr(0, c);
                if (c != std::string::npos && pseudo.count(head)) asks = "names `" + nm + "`";
                else if (ownPkgs.count(head)) ownPkgUse = true;
                else used.push_back(&nm);
                break;
            }
            case NK::SymbolicRef: asks = "looks up a name by a string"; break;
            case NK::MethodCall: {
                auto* m = static_cast<MethodCall*>(n);
                if ((m->meta && !nativeMeta.count(m->method)) || (!m->meta && (m->method == "HOW" || m->method == "WHO")))
                    metaOrDecl = true;
                break;
            }
            case NK::ClassDecl: {
                const std::string& nm = static_cast<ClassDecl*>(n)->name;
                if (ownPkgs.count(nm)) asks = "declares the program's class " + nm;   // `augment class C`, or a redeclaration
                metaOrDecl = true;
                own.insert(nm);
                break;
            }
            case NK::SubDecl: {
                auto* s = static_cast<SubDecl*>(n);
                if (!s->name.empty()) { own.insert(s->name); own.insert("&" + s->name); }
                params(s->params);
                break;
            }
            case NK::BlockExpr: params(static_cast<BlockExpr*>(n)->params); break;
            default: break;
        }
        forEachAstChild(n, walk);
    };
    for (auto& st : code.stmts) walk(st.get());
    if (!asks.empty()) return asks;
    if (ownPkgUse && metaOrDecl) return "asks the metamodel about the program's own classes";
    for (auto* u : used) {
        // the topic and the match at the top level (an EVAL in a routine or
        // a loop keeps the routine interpreted already) and a dynamic
        // variable are looked up where a native program keeps them too
        const std::string& v = *u;
        if (v == "$_" || v == "$/" || (v.size() > 1 && v[0] == '$' && ascii::isdigit((unsigned char)v[1])) ||
            (v.size() > 2 && (v[1] == '*' || v[1] == '?'))) continue;   // ($0, $1: the match's)
        if (own.count(v)) continue;
        if (pn.names.count(v)) return "reads the program's `" + v + "`";
        // a variable nothing declares, or a call to a routine whose error
        // would suggest one of the program's: the message is built from the
        // scope the EVAL runs in, which a native program's interpreter lacks
        if (v.size() > 1 && std::strchr("$@%", v[0]) && v[1] != '*' && v[1] != '?')
            return "reads `" + v + "`, which it does not declare";
        if (!v.empty() && v[0] != '&' && !suggestNames(v, pn.subs).empty())
            return "calls `" + v + "`, which the program's routines would be suggested for";
    }
    return "";
}

// The first construct in `prog` a native program cannot answer, or "".
// `evalCut` (--slim cut eval): an EVAL only throws X::Feature::NotBuilt, so
// what its code could not see does not matter, and it refuses nothing.
std::string introspectionRefusal(const Program& prog, bool evalCut) {
    // `.^name` and the ones the native class table answers as the
    // interpreter's does; every other metamodel call asks the interpreter
    static const std::set<std::string> nativeMeta = {"name", "shortname", "mro", "can", "isa"};
    std::string why;
    std::set<std::string> dyns, globals;   // `$*X` read or written as `$X`; GLOBAL's `$X`
    // (a parameter's `#=` doc matters only to a program that asks for it:
    // MAIN's usage text is built from the same signature either way)
    bool paramDoc = false, asksWhy = false;
    // the packages the program itself declares, which compile into the native
    // class table and have no stash; a `use`d module's live in the interpreter
    // and answer `M::` and `.WHO` as they do interpreted (issue #202)
    std::set<std::string> ownPkgs;
    std::function<void(Node*)> pkgs = [&](Node* n) {
        if (!n) return;
        if (n->kind == NK::ClassDecl) ownPkgs.insert(static_cast<ClassDecl*>(n)->name);
        forEachAstChild(n, pkgs);
    };
    for (auto& st : prog.stmts) pkgs(st.get());
    // An EVAL of a literal whose code reads nothing of the program's runs
    // natively as interpreted; any other EVAL refuses. Code that does not
    // parse on its own throws the same error at run time, unless the program
    // declares an operator it would have parsed with.
    ProgNames progNames;
    bool namesDone = false;
    // …the reason it refuses, or "" when it runs natively
    auto evalRefusal = [&](const Expr* arg) -> std::string {
        if (!namesDone) {
            for (auto& st : prog.stmts) collectProgNames(st.get(), progNames);
            progNames.names.insert(prog.declaredTermNames.begin(), prog.declaredTermNames.end());
            namesDone = true;
        }
        // `$v.raku.EVAL`: a value's `.raku` names no variable or routine of the
        // program, only types — unless it is one of the program's enum values
        // or the program writes its own `.raku`
        if (arg && arg->kind == NK::MethodCall) {
            auto* m = static_cast<const MethodCall*>(arg);
            if (!m->meta && m->method == "raku" && m->args.empty() && !progNames.enums && !progNames.ownRaku &&
                !progNames.opaqueOp)
                return "";
            if (!m->meta && m->method == "raku")
                return "its code is a `.raku`, and the program has an enum, an operator or a `.raku` of its own";
        }
        const std::string* text = literalText(arg);
        if (!text) return "its code is not a literal string";
        if (progNames.opaqueOp) return "the program declares an operator its code may use";
        for (auto& op : progNames.ops)
            if (text->find(op) != std::string::npos) return "its code may use the program's operator " + op;
        Program code;
        try {
            Lexer lx(*text);
            Parser ps(lx.tokenize());
            ps.src_ = text;
            code = ps.parseProgram();
            // `CATCH { when X::Y {} }` is refused by what the scope declares
            if (!ps.gobbleSites_.empty()) return "its code has a name before a block, which the scope decides";
        } catch (...) { return ""; }
        std::string r = evalCodeAsks(code, progNames, ownPkgs);
        return r.empty() ? r : "its code " + r;
    };
    auto params = [&](const std::vector<Param>& ps) {
        for (auto& p : ps)
            if (!p.pod.empty() || !p.podTrail.empty() || !p.podLead.empty()) paramDoc = true;
    };
    int line = 0;   // the nearest line: a call node carries none of its own
    std::function<void(Node*)> walk = [&](Node* n) {
        if (!n || !why.empty()) return;
        if (n->line) line = n->line;
        switch (n->kind) {
            case NK::Call: {
                const std::string& nm = static_cast<Call*>(n)->name;
                if (introspectiveCallName(nm))
                    why = "a pseudo-package lookup (MY::, UNIT::, OUTER::, …) — native locals and routines have no runtime names";
                else if ((nm == "EVAL" || nm == "EVALFILE") && !evalCut) {
                    auto* c = static_cast<Call*>(n);
                    // the code, and a `:lang` other than Raku's, which throws
                    // X::Eval::NoSuchLang compiled as interpreted
                    const Expr* code = nullptr;
                    int positional = 0;
                    bool otherLang = false, otherNamed = false;
                    for (auto& a : c->args) {
                        if (a->kind == NK::Pair) {
                            auto* pr = static_cast<const PairExpr*>(a.get());
                            if (!pr->quotedKey && !pr->parenned && !pr->keyExpr) {
                                const std::string* lang = pr->key == "lang" ? literalText(pr->value.get()) : nullptr;
                                if (lang && *lang != "Raku" && *lang != "raku" && *lang != "perl6" && *lang != "Perl6")
                                    otherLang = true;
                                else if (!lang || pr->key != "lang") otherNamed = true;
                                continue;
                            }
                        }
                        ++positional;
                        code = a.get();
                    }
                    std::string r = nm == "EVALFILE" ? "its code is a file"
                                  : c->callee || positional != 1 ? "its code is not one argument"
                                  : otherNamed ? "a named argument besides :lang"
                                  : otherLang ? "" : evalRefusal(code);
                    if (!r.empty())
                        why = "`" + nm + "` (line " + std::to_string(line) + "): " + r +
                              " — the evaluated code cannot see the program's native routines and variables";
                }
                break;
            }
            case NK::MethodCall: {
                auto* m = static_cast<MethodCall*>(n);
                if (m->method == "WHY") asksWhy = true;
                std::string evalWhy;
                if ((m->method == "EVAL" || m->method == "EVALFILE") && !evalCut)
                    evalWhy = m->method == "EVALFILE" ? "its code is a file" : evalRefusal(m->args.empty() ? m->inv.get() : nullptr);
                if (!evalWhy.empty())
                    why = "`." + m->method + "` (line " + std::to_string(line) + "): " + evalWhy +
                          " — the evaluated code cannot see the program's native routines and variables";
                else if (m->meta && !nativeMeta.count(m->method))
                    why = "`.^" + m->method + "` — the native class table is no metaobject";
                else if (!m->meta && (m->method == "HOW" || (m->method == "WHO" && !ownPkgs.empty())))
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
                if (nm.size() > 2 && nm.compare(nm.size() - 2, 2, "::") == 0 && ownPkgs.count(nm.substr(0, nm.size() - 2)))
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
    if (why.empty() && paramDoc && asksWhy) why = "a parameter's declarator doc (its .WHY)";
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
        // (`no fatal`, which a compiled `try` block reads itself, imports nothing)
        else if (n->kind == NK::UseStmt && !(static_cast<UseStmt*>(n)->isNo && static_cast<UseStmt*>(n)->module == "fatal"))
            why = "a `use` inside the routine";
        else if (n->kind == NK::MethodCall && containerName(static_cast<MethodCall*>(n)))
            why = "a container's name (`.VAR.name`)";
        // (CALLER::, MY:: … and EVAL see the interpreter's frames, where a native
        // body's locals are not: `CALLER::<$y>` read the callee's own `$y`)
        else if (n->kind == NK::SymbolicRef) why = "a symbolic or pseudo-package reference in the routine";
        // (…and the other side of it: a `my $y is dynamic` is looked up BY NAME
        // from a callee — CALLER::<$y> — where a native local has none)
        else if (n->kind == NK::VarExpr && static_cast<VarExpr*>(n)->declare && static_cast<VarExpr*>(n)->d().declDynamic)
            why = "an `is dynamic` variable in the routine";
        else if (n->kind == NK::Call && (introspectiveCallName(static_cast<Call*>(n)->name) ||
                                         static_cast<Call*>(n)->name == "EVAL"))
            why = "a pseudo-package lookup or EVAL in the routine";
        else if (n->kind == NK::MethodCall && madeOnMatch(static_cast<MethodCall*>(n)))
            why = "a Match's `.make`";
        forEachAstChild(n, walk);
    };
    for (auto& st : body.stmts) walk(st.get());
    return why;
}

} // namespace rakupp
