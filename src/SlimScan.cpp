// The SLIM feature scan (SLIM-PLAN P4). See SlimScan.h for the contract.
//
// Two kinds of evidence come out of the walk:
//
//   * per-feature USE — a call, operator, variable or regex construct that
//     can reach a cuttable table or the parser. Matching is by exact name
//     (with the sigil/& stripped), so `uniname(…)`, `'A'.uniname` and a
//     `&uniname` reference all count, and `$my-uniname-ish` does not.
//
//   * force-full TRIGGERS — EVAL/require, symbolic lookup, an indirect
//     method call, a regex whose content is computed at run time, a `use`d
//     module that is not riding along in the embedded graph. Any of these
//     means code the scan never saw can run, so `auto` keeps everything.
//
// Regex pattern STRINGS are scanned textually (escape- and char-class-aware),
// because the engine compiles them at run time: `\c[NAME]` reaches the name
// table, `<:Prop>` reaches the props tables when uniPropNeedsCutTables() says
// so, and every embedded-code construct (`{…}`, `<?{…}>`, `**{…}`, `:my`,
// `:adverb(arg)`) reaches the parser via evalString at match time — all
// measured against slim binaries, not assumed. A literal `{…}` block's source
// is visible, so it is EXTRACTED, parsed with the real Parser and walked like
// any other code; if the extraction or the parse goes wrong, the answer
// degrades to a trigger, never to a guess.
//
// The walker must cover every child edge of every node — a missed edge is a
// missed use is a wrong cut. The switch below mirrors Ast.h's inventory;
// when a node grows a child, this file is on the change-both list.

#include "SlimScan.h"
#include "AstSerial.h"
#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"
#include "Unicode.h"

#include <set>

namespace rakupp {

bool isPragmaName(const std::string& name); // Interpreter.cpp — `use` names with no file behind them

namespace {

// kSlimFeatures order (main.cpp): names, coll, props, eval.
enum { F_NAMES = 0, F_COLL = 1, F_PROPS = 2, F_EVAL = 3 };

struct Scan {
    SlimScanResult r;
    std::set<std::string> useNames;     // every `use`/`need` module seen anywhere
    std::string where;                  // "" = mainline, else module name (for trigger text)
    int curLine = 0;                    // nearest enclosing node line (walkE/walkS maintain it)

    void use(int f, const std::string& what) {
        r.used[f] = true;
        r.sites.push_back({f, what, where, curLine});
    }
    void trigger(const std::string& what) {
        std::string at = where.empty() ? "the program" : "module " + where;
        if (curLine) at += ", line " + std::to_string(curLine);
        r.triggers.push_back(what + " (in " + at + ")");
    }

    // ---- name matching ------------------------------------------------------
    static std::string stripped(const std::string& n) {
        size_t i = 0;
        while (i < n.size() && (n[i] == '&' || n[i] == '$' || n[i] == '@' || n[i] == '%')) i++;
        return n.substr(i);
    }
    void noteName(const std::string& raw) {           // a reference by name
        std::string n = stripped(raw);
        static const std::set<std::string> namesFns = {"uniname", "uninames", "uniparse",
                                                       "unival", "univals"};
        static const std::set<std::string> propsFns = {"uniprop", "uniprops", "unimatch"};
        if (namesFns.count(n)) use(F_NAMES, n);
        if (propsFns.count(n)) { use(F_PROPS, n); use(F_NAMES, n); } // bare &uniprop: args unknown
        if (n == "collate") use(F_COLL, "collate");
        if (n == "infix:<unicmp>" || n == "infix:<coll>" ||
            n == "[unicmp]" || n == "[coll]") use(F_COLL, "a " + n + " reference");
        if (n == "EVAL" || n == "EVALFILE") { use(F_EVAL, n); trigger("EVAL"); }
        if (raw == "$*COLLATION") use(F_COLL, "$*COLLATION");
    }
    void noteOp(const std::string& op) {
        if (op == "unicmp" || op == "coll") use(F_COLL, "the " + op + " operator");
    }
    // uniprop-family call with these arguments (invocant excluded).
    void noteUniprop(const std::string& fn, const std::vector<ExprPtr>& args, size_t propIdx) {
        if (fn == "uniprops") { use(F_PROPS, fn); use(F_NAMES, fn); return; } // reports many properties
        if (args.size() <= propIdx) return;            // default = General_Category: never cut
        Expr* a = args[propIdx].get();
        if (a && a->kind == NK::StrLit) {
            std::string p = static_cast<StrLit*>(a)->v;
            std::string norm;
            for (char c : p) if (std::isalnum((unsigned char)c)) norm += (char)std::tolower((unsigned char)c);
            if (norm == "name" || norm == "na" || norm == "numericvalue" || norm == "nv")
                { use(F_NAMES, fn + "('" + p + "')"); return; } // Name/Numeric_Value live in the names tables
            if (uniPropNeedsCutTables(p)) use(F_PROPS, fn + "('" + p + "')");
            return;
        }
        use(F_PROPS, fn + " with a computed property name");
        use(F_NAMES, fn + " with a computed property name");
    }

    // ---- regex pattern text -------------------------------------------------
    // Extract a brace-balanced block starting at pat[i] == '{'; returns the
    // inside or fails (quote-aware — a '}' inside a string must not close it).
    static bool extractBlock(const std::string& pat, size_t i, std::string& body, size_t& end) {
        int depth = 0; char q = 0; bool esc = false;
        size_t start = i + 1;
        for (; i < pat.size(); i++) {
            char c = pat[i];
            if (esc) { esc = false; continue; }
            if (c == '\\') { esc = true; continue; }
            if (q) { if (c == q) q = 0; continue; }
            if (c == '\'' || c == '"') { q = c; continue; }
            if (c == '{') depth++;
            else if (c == '}' && --depth == 0) { body = pat.substr(start, i - start); end = i; return true; }
        }
        return false;
    }
    void scanEmbeddedCode(const std::string& code);     // parse + walk (defined below)

    void scanPattern(const std::string& pat) {
        int cls = 0; bool esc = false;
        for (size_t i = 0; i < pat.size(); i++) {
            char c = pat[i];
            if (esc) { esc = false; continue; }
            if (c == '\\') {
                esc = true;
                if (i + 2 < pat.size() && (pat[i+1] == 'c' || pat[i+1] == 'C') && pat[i+2] == '[')
                    use(F_NAMES, "\\c[…] in a regex");   // engine resolves the name at run time
                continue;
            }
            if (cls > 0) {                              // <[…]> contents are literal
                if (c == ']') {
                    size_t j = i + 1;
                    while (j < pat.size() && (pat[j] == '+' || pat[j] == '-')) j++;
                    if (j < pat.size() && pat[j] == '[') { i = j; continue; }
                    cls = 0;
                }
                continue;
            }
            if (c == '<' && i + 1 < pat.size()) {
                size_t j = i + 1;
                while (j < pat.size() && (pat[j] == '+' || pat[j] == '-' || pat[j] == '!' || pat[j] == '?')) j++;
                if (j < pat.size() && pat[j] == '[') { cls = 1; i = j; continue; }
                if (j < pat.size() && (pat[j] == '$' || pat[j] == '{')) {
                    use(F_EVAL, "a regex interpolating a subregex"); // parsed and matched at run time
                    trigger("a regex interpolating a subregex (<$…>/<{…}>)");
                    if (pat[j] == '{') { std::string b; size_t e;   // still walk what IS visible
                        if (extractBlock(pat, j, b, e)) { scanEmbeddedCode(b); i = e; continue; } }
                    continue;
                }
                if (j < pat.size() && pat[j] == ':') {  // <:Prop> / <:!Prop> / <:sc<…>>
                    size_t k = j + 1;
                    if (k < pat.size() && pat[k] == '!') k++;
                    size_t s = k;
                    while (k < pat.size() && (std::isalnum((unsigned char)pat[k]) || pat[k] == '_')) k++;
                    std::string name = pat.substr(s, k - s);
                    if (k < pat.size() && pat[k] == '<') {          // value form: name<val>
                        size_t v = k + 1, vend = pat.find('>', v);
                        if (vend != std::string::npos) {
                            if (uniPropNeedsCutTables(name + "<" + pat.substr(v, vend - v) + ">"))
                                use(F_PROPS, "<:" + name + "<…>> in a regex");
                            i = vend; continue;
                        }
                    }
                    if (!name.empty() && uniPropNeedsCutTables(name))
                        use(F_PROPS, "<:" + name + "> in a regex");
                    i = k - 1; continue;
                }
                continue;
            }
            if (c == '{') {                             // a literal code block: visible source
                use(F_EVAL, "a regex code block");      // evalString runs it at match time
                std::string b; size_t e;
                if (extractBlock(pat, i, b, e)) { scanEmbeddedCode(b); i = e; }
                else trigger("a regex code block the scan could not extract");
                continue;
            }
            if (c == ':' && i + 1 < pat.size() &&
                (std::isalpha((unsigned char)pat[i+1]) || pat[i+1] == '_')) {
                size_t k = i + 1;
                while (k < pat.size() && (std::isalnum((unsigned char)pat[k]) || pat[k] == '_')) k++;
                std::string adv = pat.substr(i + 1, k - i - 1);
                if (adv == "my") use(F_EVAL, ":my in a regex"); // executes via evalString
                if (k < pat.size() && pat[k] == '(')
                    use(F_EVAL, ":" + adv + "(…) in a regex");  // the argument evaluates
                i = k - 1; continue;
            }
        }
    }
    void scanRepl(const std::string& repl) {
        // The replacement is qq-interpolated through evalString when it holds
        // anything beyond plain text; `{…}` closures additionally run code.
        bool esc = false;
        for (size_t i = 0; i < repl.size(); i++) {
            char c = repl[i];
            if (esc) { esc = false; continue; }
            if (c == '\\') { esc = true; continue; }
            if (c == '{') {
                use(F_EVAL, "an s/// replacement block");
                std::string b; size_t e;
                if (extractBlock(repl, i, b, e)) { scanEmbeddedCode(b); i = e; }
                else trigger("an s/// replacement block the scan could not extract");
                continue;
            }
            if ((c == '$' || c == '@') && i + 1 < repl.size()) {
                char n = repl[i + 1];
                // $0 / $<name> are capture refs the engine resolves itself; a
                // method chain on one (`$0.uc`) goes through evalString.
                if (std::isalpha((unsigned char)n) || n == '_' || n == '*')
                    use(F_EVAL, "an s/// replacement interpolating a variable");
                if (std::isdigit((unsigned char)n) || n == '<') {
                    size_t k = i + 1;
                    if (n == '<') { k = repl.find('>', k); if (k == std::string::npos) continue; k++; }
                    else while (k < repl.size() && std::isdigit((unsigned char)repl[k])) k++;
                    if (k < repl.size() && repl[k] == '.')
                        use(F_EVAL, "an s/// replacement with a method chain");
                }
            }
        }
    }

    // ---- the AST walk -------------------------------------------------------
    void walkParams(const std::vector<Param>& ps) {
        for (const auto& p : ps) {
            walkE(p.whereExpr.get());
            walkE(p.litVal.get());
            walkE(p.defaultVal.get());
            if (p.subSig) walkParams(*p.subSig);
        }
    }
    void walkBlock(const Block* b) { if (b) for (auto& s : b->stmts) walkS(s.get()); }

    void walkE(Expr* e) {
        if (!e) return;
        int prevLine = curLine;
        if (e->line) curLine = e->line;
        walkE_(e);
        curLine = prevLine;
    }
    void walkE_(Expr* e) {
        switch (e->kind) {
            case NK::IntLit: case NK::NumLit: case NK::StrLit: case NK::BoolLit:
            case NK::Whatever: case NK::SelfTerm: case NK::AllomorphLit: break;
            case NK::InterpStr:
                for (auto& p : static_cast<InterpStr*>(e)->parts) walkE(p.get());
                break;
            case NK::VarExpr: {
                auto* v = static_cast<VarExpr*>(e);
                noteName(v->name);
                walkE(v->declDefault.get());
                walkE(v->declShape.get());
                break;
            }
            case NK::NameTerm: noteName(static_cast<NameTerm*>(e)->name); break;
            case NK::ListExpr: for (auto& x : static_cast<ListExpr*>(e)->items) walkE(x.get()); break;
            case NK::ArrayLit: for (auto& x : static_cast<ArrayLit*>(e)->items) walkE(x.get()); break;
            case NK::HashLit:  for (auto& x : static_cast<HashLit*>(e)->items) walkE(x.get()); break;
            case NK::SymbolicRef: {
                auto* sr = static_cast<SymbolicRef*>(e);
                trigger("a symbolic reference (::(…))");
                walkE(sr->nameExpr.get());
                for (auto& s : sr->segs) walkE(s.get());
                break;
            }
            case NK::Assign: {
                auto* a = static_cast<Assign*>(e);
                walkE(a->target.get()); walkE(a->value.get());
                break;
            }
            case NK::Binary: {
                auto* b = static_cast<Binary*>(e);
                noteOp(b->op);
                walkE(b->lhs.get()); walkE(b->rhs.get());
                break;
            }
            case NK::ChainExpr: {
                auto* c = static_cast<ChainExpr*>(e);
                for (auto& op : c->ops) noteOp(op);
                for (auto& x : c->operands) walkE(x.get());
                break;
            }
            case NK::Unary: {
                auto* u = static_cast<Unary*>(e);
                if (u->op == "require") { use(F_EVAL, "require"); trigger("require"); }
                walkE(u->operand.get());
                break;
            }
            case NK::Call: {
                auto* c = static_cast<Call*>(e);
                if (!c->name.empty()) {
                    noteName(c->name);
                    if (c->name == "uniprop" || c->name == "uniprops" || c->name == "unimatch")
                        noteUniprop(c->name, c->args, 1);
                    if (c->name == "EVALFILE") { use(F_EVAL, "EVALFILE"); trigger("EVALFILE"); }
                }
                walkE(c->callee.get());
                for (auto& a : c->args) walkE(a.get());
                break;
            }
            case NK::MethodCall: {
                auto* m = static_cast<MethodCall*>(e);
                if (m->methodExpr) {
                    trigger("an indirect method call (.\"$name\"())");
                    walkE(m->methodExpr.get());
                }
                else {
                    noteName(m->method);
                    if (m->method == "uniprop" || m->method == "uniprops" || m->method == "unimatch")
                        noteUniprop(m->method, m->args, 0);
                    if (m->meta && (m->method == "lookup" || m->method == "find_method" ||
                                    m->method == "can"))
                        trigger("a metamodel lookup (.^" + m->method + ")");
                }
                walkE(m->inv.get());
                for (auto& a : m->args) walkE(a.get());
                break;
            }
            case NK::Index: {
                auto* ix = static_cast<Index*>(e);
                walkE(ix->base.get()); walkE(ix->index.get());
                break;
            }
            case NK::Ternary: {
                auto* t = static_cast<Ternary*>(e);
                walkE(t->cond.get()); walkE(t->then.get()); walkE(t->els.get());
                break;
            }
            case NK::NqpOp: for (auto& a : static_cast<NqpOp*>(e)->args) walkE(a.get()); break;
            case NK::Range: {
                auto* r0 = static_cast<RangeExpr*>(e);
                walkE(r0->from.get()); walkE(r0->to.get());
                break;
            }
            case NK::Pair: {
                auto* p = static_cast<PairExpr*>(e);
                walkE(p->keyExpr.get()); walkE(p->value.get());
                break;
            }
            case NK::BlockExpr: {
                auto* b = static_cast<BlockExpr*>(e);
                walkParams(b->params);
                for (auto& s : b->body) walkS(s.get());
                break;
            }
            case NK::RegexLit: scanPattern(static_cast<RegexLit*>(e)->pattern); break;
            case NK::SubstLit: {
                auto* s = static_cast<SubstLit*>(e);
                scanPattern(s->pattern);
                scanRepl(s->repl);
                break;
            }
            default:
                // A node kind this walker does not model: treat like a trigger
                // rather than silently under-scanning.
                trigger("an AST node the scan does not model (NK " +
                        std::to_string((int)e->kind) + ")");
                break;
        }
    }

    void walkS(Stmt* s) {
        if (!s) return;
        int prevLine = curLine;
        if (s->line) curLine = s->line;
        walkS_(s);
        curLine = prevLine;
    }
    void walkS_(Stmt* s) {
        switch (s->kind) {
            case NK::ExprStmt: walkE(static_cast<ExprStmt*>(s)->e.get()); break;
            case NK::VarDecl: walkE(static_cast<VarDecl*>(s)->init.get()); break;
            case NK::NamedRegexDecl: scanPattern(static_cast<NamedRegexDecl*>(s)->pattern); break;
            case NK::SubDecl: {
                auto* d = static_cast<SubDecl*>(s);
                walkParams(d->params);
                for (auto& alt : d->altParams) walkParams(alt);
                for (auto& t : d->traits) walkE(t.arg.get());
                walkE(d->retLiteral.get());
                walkE(d->nativeLibExpr.get());
                for (auto& a : d->immediateArgs) walkE(a.get());
                for (auto& b : d->body) walkS(b.get());
                break;
            }
            case NK::ClassDecl: {
                auto* c = static_cast<ClassDecl*>(s);
                for (auto& a : c->attrs) {
                    walkE(a.def.get());
                    for (auto& ut : a.userTraits) walkE(ut.second.get());
                }
                for (auto& m : c->methods) walkS(m.get());
                for (auto& r0 : c->rules) scanPattern(r0.pattern);
                walkE(c->verExpr.get()); walkE(c->authExpr.get()); walkE(c->apiExpr.get());
                walkParams(c->roleParams);
                for (auto& ra : c->roleArgs) for (auto& a : ra.second) walkE(a.get());
                for (auto& b : c->body) walkS(b.get());
                break;
            }
            case NK::Block: walkBlock(static_cast<Block*>(s)); break;
            case NK::EnumDecl: walkE(static_cast<EnumDecl*>(s)->values.get()); break;
            case NK::IfStmt: {
                auto* i = static_cast<IfStmt*>(s);
                for (auto& br : i->branches) { walkE(br.first.get()); walkBlock(br.second.get()); }
                walkBlock(i->elseBlock.get());
                break;
            }
            case NK::WhileStmt: {
                auto* w = static_cast<WhileStmt*>(s);
                walkE(w->cond.get()); walkParams(w->params); walkBlock(w->body.get());
                break;
            }
            case NK::ForStmt: {
                auto* f = static_cast<ForStmt*>(s);
                walkE(f->list.get()); walkParams(f->params); walkBlock(f->body.get());
                break;
            }
            case NK::LoopStmt: {
                auto* l = static_cast<LoopStmt*>(s);
                walkE(l->init.get()); walkE(l->cond.get()); walkE(l->incr.get());
                walkBlock(l->body.get());
                break;
            }
            case NK::RepeatStmt: {
                auto* r0 = static_cast<RepeatStmt*>(s);
                walkE(r0->cond.get()); walkBlock(r0->body.get());
                break;
            }
            case NK::ReturnStmt: walkE(static_cast<ReturnStmt*>(s)->value.get()); break;
            case NK::UseStmt: {
                auto* u = static_cast<UseStmt*>(s);
                if (!u->module.empty() && !u->isNo) useNames.insert(u->module);
                walkE(u->argExpr.get());
                break;
            }
            case NK::GivenStmt: {
                auto* g = static_cast<GivenStmt*>(s);
                walkE(g->topic.get()); walkBlock(g->body.get()); walkBlock(g->elseBody.get());
                break;
            }
            case NK::WhenStmt: {
                auto* w = static_cast<WhenStmt*>(s);
                walkE(w->cond.get()); walkBlock(w->body.get());
                break;
            }
            case NK::SubsetDecl: walkE(static_cast<SubsetDecl*>(s)->where.get()); break;
            case NK::LastStmt: case NK::NextStmt: case NK::RedoStmt: case NK::EmptyStmt: break;
            default:
                trigger("an AST statement the scan does not model (NK " +
                        std::to_string((int)s->kind) + ")");
                break;
        }
    }
};

void Scan::scanEmbeddedCode(const std::string& code) {
    // A visible `{…}` block from a regex: parse it with the real Parser (this
    // runs inside the rakupp CLI, which always has one) and walk it like any
    // other code. Parse trouble degrades to a trigger — never to a guess.
    try {
        Lexer lx(code);
        Parser ps(lx.tokenize());
        Program p = ps.parseProgram();
        for (auto& s : p.stmts) walkS(s.get());
    } catch (...) {
        trigger("a regex code block the scan could not parse");
    }
}

} // namespace

SlimScanResult slimScan(const Program& prog, const std::vector<BundledModule>& mods) {
    Scan sc;
    for (auto& s : prog.stmts) sc.walkS(s.get());
    std::set<std::string> embedded;
    for (const auto& m : mods) {
        embedded.insert(m.name);
        Program mp;
        try { deserializeAst(m.blob, mp); }
        catch (...) { sc.trigger("module " + m.name + " could not be re-read for scanning"); continue; }
        sc.where = m.name;
        for (auto& s : mp.stmts) sc.walkS(s.get());
        sc.where.clear();
    }
    // Trigger 4: a `use`d module that is NOT riding along in the embedded
    // graph — unresolvable, unparseable or unserializable, all of which
    // collectModuleGraph skips silently — will be loaded from DISK at run
    // time: code the scan never saw, plus the parser to load it.
    for (const auto& name : sc.useNames) {
        if (embedded.count(name) || isPragmaName(name)) continue;
        sc.curLine = 0;
        sc.use(F_EVAL, "module " + name + " loaded from disk at run time");
        sc.trigger("module " + name + " is not embedded (loaded from disk at run time)");
    }
    return sc.r;
}

}
