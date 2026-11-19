// Three scans of a program's routines that `--exe` makes before it emits
// anything, moved here from Codegen.cpp unchanged (it sits at its size
// ceiling): the MAIN signature blob a binary dispatches on, why a module
// routine keeps its interpreted body, and the names emitted C++ declares.
#include "../Ast.h"
#include "../AstSerial.h"
#include "../AsciiCtype.h"
#include "../Codegen.h"
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace rakupp {

std::vector<std::string> computePlaceholders(const std::vector<StmtPtr>& body);   // InterpreterCalls.cpp
bool introspectiveCallName(const std::string&); const char* routineIntrospectionRefusal(const Program&);   // codegen/Introspection.cpp

// The signature blob a compiled binary hands RT.registerCompiledMain: the MAIN
// candidates with their bodies detached, written by the SAME serializer as the
// module cache — so the compiled dispatcher scores and prints usage from the
// very Params the interpreter would build for this source. The decls are moved
// into a scratch Program for the write and restored afterwards; on any
// serializer error the AST is restored and "" is returned, which downgrades
// the binary to the legacy direct call instead of failing the compile.
std::string mainSigBlob(Program& prog) {
    std::vector<std::pair<size_t, std::vector<StmtPtr>>> stash; // stmt index + its body
    // `where` clauses stay OUT of the blob: scoring evaluates them, and one that
    // reaches past its own parameter (`where * < $limit`) would look $limit up
    // in the runtime env, where compiled lexicals never go — the throw would
    // read as "no match" and refuse an argv the interpreter accepts. Unscored
    // is the pre-protocol permissiveness; sometimes-wrong would be worse.
    std::vector<std::pair<Param*, ExprPtr>> wheres;
    for (size_t i = 0; i < prog.stmts.size(); i++) {
        if (prog.stmts[i]->kind != NK::SubDecl) continue;
        auto* d = static_cast<SubDecl*>(prog.stmts[i].get());
        if (d->name != "MAIN" || d->isProto) continue; // a proto is not a candidate
        for (auto& p : d->params)
            if (p.whereExpr) {
                wheres.push_back({&p, std::move(p.whereExpr)});
                p.hadWhere = true; // the blob's copy still stringifies as `where { ... }`
            }
        stash.push_back({i, std::move(d->body)});
        d->body.clear();
    }
    if (stash.empty()) return "";
    Program sig;
    for (auto& s : stash) sig.stmts.push_back(std::move(prog.stmts[s.first]));
    std::string blob;
    try { blob = serializeAst(sig); } catch (...) { blob.clear(); }
    for (size_t k = 0; k < stash.size(); k++) {
        prog.stmts[stash[k].first] = std::move(sig.stmts[k]);
        static_cast<SubDecl*>(prog.stmts[stash[k].first].get())->body = std::move(stash[k].second);
    }
    for (auto& w : wheres) { w.first->whereExpr = std::move(w.second); w.first->hadWhere = false; }
    return blob;
}

// Why a routine keeps its interpreted body before the emitter is even asked.
// These are the shapes whose meaning lives in the call protocol around the
// statements rather than in them, so a native body would have to reproduce
// that protocol: a phaser or CATCH the interpreter registers for the call, a
// result that is a container (`is rw`), parameters that alias the caller's
// variables, placeholders the interpreter names from the body.
const char* aotIneligible(SubDecl* d) {
    if (d->isNative) return "a NativeCall sub";
    if (d->isProto) return "a proto";
    if (d->body.empty()) return "an empty body";
    if (d->retRw) return "an `is rw` routine";
    if (d->nameExpr) return "a computed name";
    if (!d->altParams.empty()) return "alternative signatures";
    if (d->params.empty() && !d->hadSig && !computePlaceholders(d->body).empty())
        return "placeholder parameters";
    for (auto& p : d->params) {
        if (p.isRw || p.isRaw) return "an `is rw`/`is raw` parameter";
        if (p.subSig) return "a sub-signature";
        if (!p.name.empty() && p.name[0] != '$' && p.name[0] != '@' && p.name[0] != '%' && p.name[0] != '&')
            return "a sigilless parameter";
    }
    for (auto& st : d->body)
        if (st && st->kind == NK::Block) {
            auto* b = static_cast<Block*>(st.get());
            if (b->isCatch || !b->phaser.empty()) return "a phaser or CATCH block";
        }
    // Code that finds a variable by its NAME at run time — EVAL, EVALFILE, a
    // symbolic or pseudo-package lookup — looks in the frame for the routine's
    // locals, which a native body keeps as C++ variables the interpreter
    // cannot see: `my $pod; EVAL '… $pod = …'; $pod` answered Any (roast
    // S26-documentation/12-non-breaking-space.t, once a bundled program's own
    // routines had native bodies). Anywhere in the body, closures included;
    // a node the dump cannot name (`Expr?`: `::(…)`, CALLER::) counts as one.
    {
        Program p;
        for (auto& st : d->body) p.stmts.emplace_back(st.get());
        std::ostringstream dump;
        try { dumpAst(p, dump); } catch (...) {}
        const char* intro = routineIntrospectionRefusal(p);   // (codegen/Introspection.cpp)
        for (auto& st : p.stmts) st.release();   // the routine's AST still owns them
        if (intro) return intro;
        std::istringstream in(dump.str());
        for (std::string ln; std::getline(in, ln);) {
            const size_t a = ln.find_first_not_of(' ');
            if (a == std::string::npos) continue;
            const std::string row = ln.substr(a);
            if (row.rfind("Expr?", 0) == 0 || row.rfind("Stmt?", 0) == 0)
                return "a construct that may look names up at run time";
            const size_t bar = row.find("\xE2\x94\x82 ");
            if (bar == std::string::npos) continue;
            const std::string node = row.substr(0, row.find(' '));
            std::string detail = row.substr(bar + 4);
            detail.erase(0, detail.find_first_not_of(' '));
            const std::string first = detail.substr(0, detail.find(' '));
            if ((node == "Call" && (first == "EVAL" || first == "EVALFILE" || introspectiveCallName(first))) ||
                (node == "MethodCall" && (first == ".EVAL" || first == ".EVALFILE")))
                return "an EVAL or a name looked up at run time";
        }
    }
    return nullptr;
}

// The C++ identifiers the emitted text DECLARES for Raku names — whichever
// emitter path wrote the declaration (a `my`, a loop variable, a pointy
// parameter, a cell alias). A name the body declares nowhere is an outer name.
std::set<std::string> declaredIdents(const std::string& t) {
    std::set<std::string> out;
    auto ident = [](char c) { return ascii::isalnum((unsigned char)c) || c == '_'; };
    for (size_t i = t.find("v_"); i != std::string::npos; i = t.find("v_", i + 2)) {
        if (i > 0 && ident(t[i - 1])) continue;
        size_t j = i - (i > 0 ? 1 : 0);
        while (j > 0 && (t[j] == ' ' || t[j] == '&')) j--;
        size_t k = j + 1;
        while (k > 0 && ident(t[k - 1])) k--;
        std::string ty = t.substr(k, j + 1 - k);
        if (ty != "Value" && ty != "auto") continue;
        size_t e = i;
        while (e < t.size() && ident(t[e])) e++;
        out.insert(t.substr(i, e - i));
    }
    return out;
}

}  // namespace rakupp
