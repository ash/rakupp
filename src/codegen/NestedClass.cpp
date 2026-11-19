// Classes a routine or block declares, registered by `--exe` as the program's
// top-level ones are (transpileToCpp's declaration loop, classRegister).
//
// The native class tables are built from the top-level declarations, so a
// class found inside a routine or block had none — no methods — and refused
// the whole program: the first blocker of 92 Roast files on 2026-11-18, most
// of them a test block's `class Dog {}`. Such a class is lifted to the top
// when that changes nothing a program can see:
//  - it is a class or a grammar: not a role, package, augment, stub,
//    monitor, anonymous, parameterized or computed-name declaration, and not
//    in the body of another type or of a routine inside one (whose package
//    prefixes its name)
//  - no other type of the program has its name, so registering it at startup,
//    where the interpreter registers it when the declaration runs, decides no
//    lookup differently (one `my class` of a name shadowing another stays
//    interpreted: the registry's restore is the interpreter's)
//  - its body runs nothing: a statement there runs when the declaration does
//  - it is not the last statement of a block or routine, whose value it would
//    be: `(class A { }).new` is `do { class A { } }.new`
//  - nothing in it names a variable, sub or term the top-level statement
//    around it declares: its methods become free functions, which see the
//    program's top-level variables and nothing of a routine's or block's.
//    Any name the class mentions that is declared anywhere in that statement
//    keeps the class where it is, even one the class declares itself. A
//    grammar's rules are pattern text the walk does not see: each word in
//    them counts as a mention, bare words as subs and regexes (`<word>`,
//    `{ word() }`), so a rule calling a `my token` of the block keeps it.
// A class that is not lifted is refused where it stands, as before.
#include "../Ast.h"
#include "../Codegen.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rakupp {

namespace {

// Names a subtree declares (`declared`) or mentions (`mentioned`), with their
// sigils: variables, routine parameters, subs as `&name`, sigilless terms.
struct Names {
    std::set<std::string> declared, mentioned;
    void params(const std::vector<Param>& ps) { for (auto& p : ps) if (!p.name.empty()) declared.insert(p.name); }
    // every identifier in a pattern, with the sigil before it, or as `&word`
    void pattern(const std::string& p) {
        auto word = [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c >= 0x80; };
        for (size_t i = 0; i < p.size(); i++) {
            if (!word((unsigned char)p[i]) || (i && word((unsigned char)p[i - 1]))) continue;
            size_t j = i;
            while (j < p.size() && word((unsigned char)p[j])) j++;
            const char sig = i && std::strchr("$@%&", p[i - 1]) ? p[i - 1] : '&';
            mentioned.insert(std::string(1, sig) + p.substr(i, j - i));
            i = j;
        }
    }
    void walk(Node* n) {
        if (!n) return;
        switch (n->kind) {
            case NK::VarExpr: {
                auto* v = static_cast<VarExpr*>(n);
                (v->declare ? declared : mentioned).insert(v->name);
                break;
            }
            case NK::Call: mentioned.insert("&" + static_cast<Call*>(n)->name); break;
            case NK::NameTerm: mentioned.insert(static_cast<NameTerm*>(n)->name); break;
            case NK::SubDecl: {
                auto* d = static_cast<SubDecl*>(n);
                if (!d->name.empty()) declared.insert("&" + d->name);
                params(d->params);
                break;
            }
            case NK::BlockExpr: params(static_cast<BlockExpr*>(n)->params); break;
            case NK::ForStmt: params(static_cast<ForStmt*>(n)->params);
                for (auto& v : static_cast<ForStmt*>(n)->vars) declared.insert(v);
                break;
            case NK::WhileStmt: params(static_cast<WhileStmt*>(n)->params); break;
            case NK::GivenStmt: params(static_cast<GivenStmt*>(n)->params); break;
            case NK::NamedRegexDecl: declared.insert("&" + static_cast<NamedRegexDecl*>(n)->name); break;
            case NK::ClassDecl:
                for (auto& r : static_cast<ClassDecl*>(n)->rules) pattern(r.pattern);
                break;
            default: break;
        }
        forEachAstChild(n, [&](Node* c) { walk(c); });
    }
};

// A name no enclosing scope can hand a method: a twigil (`$*d`, `$!a`, `$.a`,
// `$?CLASS`, `$^a`, `$:n`, `$=pod`, `$~MAIN`) or a method's own topic, match,
// error and argument list.
bool ownOrSpecial(const std::string& n) {
    if (n == "$_" || n == "$/" || n == "$!" || n == "@_" || n == "%_" || n == "self") return true;
    return n.size() > 2 && std::string("$@%&").find(n[0]) != std::string::npos &&
           std::string("*!.?^:=~").find(n[1]) != std::string::npos;
}

// Why a class of this shape stays where it is; "" when the shape may be lifted.
std::string shapeRefusal(const ClassDecl* cd) {
    if (cd->isRole || cd->isPackage || cd->isMonitor) return "a role, package or monitor";
    if (cd->isAugment || cd->isStubDecl || cd->parameterized) return "an augment, stub or parameterized type";
    if (cd->isAnonDecl || cd->nameExpr || cd->name.empty()) return "an anonymous or computed name";
    if (!std::all_of(cd->body.begin(), cd->body.end(), [](const StmtPtr& s) { return !s || s->kind == NK::EmptyStmt; }))
        return "a statement in its body";
    return "";
}

std::map<const Node*, std::string> g_kept;   // a class not lifted, and why (nestedClassRefusal)

}  // namespace

std::vector<Stmt*> withLiftedClasses(Program& prog) {
    std::vector<Stmt*> out;
    std::map<std::string, int> typeNames;     // every type declaration of the program, by name
    std::vector<std::pair<ClassDecl*, Stmt*>> nested;   // a candidate and the top-level statement around it
    std::set<const Node*> valued;   // the last statement of a block or routine
    auto last = [&](const std::vector<StmtPtr>& v) { if (!v.empty()) valued.insert(v.back().get()); };
    std::function<void(Node*, Stmt*, bool)> scan = [&](Node* n, Stmt* top, bool inType) {
        if (!n) return;
        if (n->kind == NK::ClassDecl) typeNames[static_cast<ClassDecl*>(n)->name]++;
        else if (n->kind == NK::EnumDecl) typeNames[static_cast<EnumDecl*>(n)->name]++;
        else if (n->kind == NK::SubsetDecl) typeNames[static_cast<SubsetDecl*>(n)->name]++;
        if (n->kind == NK::ClassDecl && n != top && !inType) nested.push_back({static_cast<ClassDecl*>(n), top});
        if (n->kind == NK::BlockExpr) last(static_cast<BlockExpr*>(n)->body);
        else if (n->kind == NK::SubDecl) last(static_cast<SubDecl*>(n)->body);
        else if (n->kind == NK::Block) last(static_cast<Block*>(n)->stmts);
        const bool type = inType || n->kind == NK::ClassDecl;
        forEachAstChild(n, [&](Node* c) { scan(c, top, type); });
    };
    for (auto& s : prog.stmts) {
        out.push_back(s.get());
        scan(s.get(), s.get(), false);
    }
    std::map<Stmt*, Names> around;   // what each enclosing top-level statement declares
    g_kept.clear();
    for (auto& [cd, top] : nested) {
        std::string why = shapeRefusal(cd);
        if (why.empty() && typeNames[cd->name] != 1) why = "a name another type of the program has";
        if (why.empty() && valued.count(cd)) why = "the last statement of its block, whose value it is";
        if (why.empty()) {
            auto it = around.find(top);
            if (it == around.end()) { it = around.emplace(top, Names{}).first; it->second.walk(top); }
            Names in;
            in.walk(cd);
            for (auto& n : in.mentioned)
                if (!ownOrSpecial(n) && it->second.declared.count(n)) { why = "a name `" + n + "` the code around it declares"; break; }
        }
        if (why.empty()) out.push_back(cd);
        else g_kept[cd] = why;
    }
    return out;
}

std::string nestedClassRefusal(const Node* cd) {
    auto it = g_kept.find(cd);
    const std::string& name = static_cast<const ClassDecl*>(cd)->name;
    return "a class" + (name.empty() ? std::string() : " `" + name + "`") + " declared inside a routine or block" +
           (it == g_kept.end() ? std::string() : " (" + it->second + ")");
}

}  // namespace rakupp
