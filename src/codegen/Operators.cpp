// The program's own operators, for the C++ backend (Codegen.cpp) and the JS
// backend: which routines declare one, and which of those compiled code cannot
// honour. Free functions over the AST, kept apart from Codegen.cpp, which is at
// its size ceiling (tools/source-helpers/budget.txt).
#include "../Codegen.h"
#include "../Ast.h"
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace rakupp {

// ---- the program's own operators (Codegen::lexOps_, and the JS backend's) ----

// `infix:<+>` → the category and the spelling; false for any other name
bool operatorRoutineName(const std::string& n, std::string& cat, std::string& op) {
    const size_t c = n.find(":<");
    if (c == std::string::npos || n.size() < c + 4 || n.back() != '>') return false;
    cat = n.substr(0, c);
    if (cat != "infix" && cat != "prefix" && cat != "postfix") return false;
    op = n.substr(c + 2, n.size() - c - 3);
    return true;
}

std::vector<OperatorDecl> declaredOperators(Program& prog) {
    std::vector<OperatorDecl> out;
    std::string cat, op;
    // where: 1 the unit's statements, 2 a class body's, 3 inside one of those, 0 anywhere else
    std::function<void(Node*, int)> walk = [&](Node* n, int where) {
        if (!n) return;
        const bool inBody = where >= 2;
        auto binding = [&](const std::string& nm) {
            if (nm.size() > 1 && nm[0] == '&' && operatorRoutineName(nm.substr(1), cat, op))
                out.push_back({nm.substr(1), nullptr, false, inBody, n->line});
        };
        auto params = [&](const std::vector<Param>& ps) { for (auto& p : ps) binding(p.name); };
        switch (n->kind) {
            case NK::SubDecl: {
                auto* d = static_cast<SubDecl*>(n);
                if (!d->isMethod && operatorRoutineName(d->name, cat, op))
                    out.push_back({d->name, d, where == 1, inBody, d->line});
                params(d->params);
                for (auto& a : d->altParams) params(a);
                break;
            }
            case NK::BlockExpr: params(static_cast<BlockExpr*>(n)->params); break;
            case NK::ForStmt: params(static_cast<ForStmt*>(n)->params); break;
            case NK::WhileStmt: params(static_cast<WhileStmt*>(n)->params); break;
            case NK::GivenStmt: params(static_cast<GivenStmt*>(n)->params); break;
            case NK::IfStmt: {
                auto* i = static_cast<IfStmt*>(n);
                for (auto& ps : i->branchParams) params(ps);
                params(i->elseParams);
                break;
            }
            case NK::VarDecl: for (auto& nm : static_cast<VarDecl*>(n)->names) binding(nm); break;
            case NK::VarExpr: if (static_cast<VarExpr*>(n)->declare) binding(static_cast<VarExpr*>(n)->name); break;
            case NK::ClassDecl: {   // (an attribute's default cannot declare an operator)
                auto* c = static_cast<ClassDecl*>(n);
                for (auto& m : c->methods) walk(m.get(), 0);
                for (auto& st : c->body) walk(st.get(), 2);
                return;
            }
            default: break;
        }
        forEachAstChild(n, [&](Node* ch) { walk(ch, inBody ? 3 : 0); });
    };
    for (auto& st : prog.stmts) walk(st.get(), 1);
    return out;
}

std::set<std::string> declaredOperatorNames(Program& prog) {
    std::set<std::string> out;
    for (auto& d : declaredOperators(prog)) out.insert(d.name);
    return out;
}

std::string operatorDeclRefusal(const std::string& name, bool classBody) {
    // Spellings with an arm of their own — a thunk, a list infix, a container
    // test, a sequence or range, a junction, a mixin — in the emitters or in
    // evalBinary: the call to the user's routine is never reached for them.
    static const std::set<std::string> kOwnInfix = {
        "&&", "and", "||", "or", "//", "andthen", "orelse", "notandthen", "^^", "xor", "&", "|", "^",
        "~~", "!~~", "xx", "...", "...^", "^...", "^...^", "..", "..^", "^..", "^..^", "Z", "X",
        "does", "but", "=:=", "!=:=", "==>", "<==", "ff", "fff", "ff^", "fff^", "^ff", "^fff", "^ff^",
        "^fff^", ",", "=>", "o", "\xE2\x88\x98"};
    // The lexer spells `×` `÷` `−` `≤` `≥` `≠` as their ASCII twins, so a
    // routine under one of these names never meets the operator it overloads.
    static const std::set<std::string> kTwins = {
        "\xC3\x97", "\xC3\xB7", "\xE2\x88\x92", "\xE2\x89\xA4", "\xE2\x89\xA5", "\xE2\x89\xA0"};
    std::string cat, op;
    if (!operatorRoutineName(name, cat, op)) return "";
    if ((cat == "infix" && kOwnInfix.count(op)) || (cat == "prefix" && (op == "++" || op == "--")) ||
        (cat == "postfix" && (op == "++" || op == "--" || op == "i")))
        return "the program's own " + name + ", an operator compiled code evaluates itself";
    if (cat == "infix" && kTwins.count(op))
        return "the program's own " + name + ", which compiled code meets under its ASCII twin";
    if (classBody) return "the operator " + name + " declared inside a class body";
    return "";
}

// A candidate over CORE types joins the built-in's own dispatch in the
// interpreter (userInfixOverCore: narrower wins, a tie is ambiguous), which no
// compiled dispatcher models — and a lane that reads only Ints would never ask
// it. The test is noteUserInfixCandidate's, which arms that dispatch.
std::string coreTypedMultiRefusal(const std::vector<Program*>& units, const std::string& name,
                                  const std::vector<SubDecl*>& cands, int& line) {
    static const std::set<std::string> kModelled = {
        "+", "-", "*", "/", "%", "**", "==", "!=", "<", ">", "<=", ">=", "<=>", "div", "mod",
        "~", "eq", "ne", "lt", "gt", "le", "ge", "leg"};
    static const std::set<std::string> kCore = {"Int", "UInt", "Num", "Rat", "FatRat", "Str", "Bool", "Complex"};
    static const std::set<std::string> kBroad = {
        "Any", "Mu", "Cool", "Numeric", "Real", "Rational", "Stringy", "Positional", "Associative",
        "Callable", "Code", "Iterable", "Junction", "List", "Array", "Hash", "Seq", "Range"};
    std::string cat, op;
    if (!operatorRoutineName(name, cat, op) || cat != "infix" || !kModelled.count(op)) return "";
    std::map<std::string, std::string> subsetOf;
    std::set<std::string> ownTypes;
    auto own = [&](const std::string& n) {   // full and short, as a parameter may spell it
        ownTypes.insert(n);
        if (size_t c = n.rfind("::"); c != std::string::npos) ownTypes.insert(n.substr(c + 2));
    };
    std::function<void(std::vector<StmtPtr>&)> types = [&](std::vector<StmtPtr>& ss) {
        for (auto& st : ss) {
            if (st->kind == NK::SubsetDecl)
                subsetOf[static_cast<SubsetDecl*>(st.get())->name] = static_cast<SubsetDecl*>(st.get())->baseType;
            else if (st->kind == NK::ClassDecl) {
                own(static_cast<ClassDecl*>(st.get())->name);
                types(static_cast<ClassDecl*>(st.get())->body);
            }
            else if (st->kind == NK::EnumDecl) own(static_cast<EnumDecl*>(st.get())->name);
        }
    };
    for (Program* u : units) types(u->stmts);
    auto concrete = [&](const Param& p) {
        if (p.litVal) return true;
        std::string t = p.type;
        for (int guard = 0; guard < 16; guard++) {
            auto it = subsetOf.find(t);
            if (it == subsetOf.end() || it->second.empty()) break;
            t = it->second;
        }
        if (kCore.count(t)) return true;
        return !t.empty() && !kBroad.count(t) && !ownTypes.count(t);   // a type nothing here declares: maybe a core one
    };
    for (const SubDecl* d : cands) {
        std::vector<const Param*> pos;
        bool shaped = true;
        for (auto& p : d->params) {
            if (p.invocant) continue;
            if (p.named) { if (p.required) shaped = false; continue; }
            if (p.pastDoubleSemi || p.slurpy || p.optional || p.defaultVal || p.subSig || p.coerce ||
                p.typeCapture || p.sigil != '$')
                shaped = false;
            pos.push_back(&p);
        }
        if (shaped && pos.size() == 2 && concrete(*pos[0]) && concrete(*pos[1])) {
            line = d->line;
            return "a multi " + name + " with a candidate over core types, which meets the built-in operator's "
                   "own candidates";
        }
    }
    return "";
}

} // namespace rakupp
