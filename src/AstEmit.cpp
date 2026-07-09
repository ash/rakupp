// Real AOT backend: transpile a parsed Program into C++ that reconstructs the
// identical AST at startup, then interprets it. So parsing happens ahead of time
// (at `rakupp --aot` build time); the produced binary walks the embedded tree.
#include "Ast.h"
#include <cstdio>
#include <ostream>
#include <sstream>
#include <string>

namespace rakupp {
namespace {

[[noreturn]] void bail(const std::string& w) { throw AstEmitError{w}; }

std::string S(const std::string& s) { // C++ string literal
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '\\' || c == '"') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') o += "\\r";
        else if (c < 0x20 || c >= 0x7f) { char b[8]; snprintf(b, sizeof b, "\\%03o", c); o += b; } // 3-digit octal: unambiguous
        else o += (char)c;
    }
    o += "\"";
    return o;
}
const char* B(bool b) { return b ? "true" : "false"; }

struct Emit {
    std::ostream& defs;   // node-builder function definitions accumulate here
    int cnt = 0;
    std::string sym(const char* p) { return std::string(p) + std::to_string(cnt++); }

    // Emit a builder function `static <ret> <name>() { <body> }` and return its name.
    std::string fn(const char* prefix, const std::string& ret, const std::string& body) {
        std::string name = sym(prefix);
        defs << "static " << ret << " " << name << "() {\n" << body << "}\n";
        return name;
    }

    void params(const std::vector<Param>& ps, std::ostringstream& b) {
        for (auto& p : ps) {
            b << "  { Param p; p.name=" << S(p.name) << "; p.sigil='" << (p.sigil ? p.sigil : '$') << "'; p.type="
              << S(p.type) << "; p.named=" << B(p.named) << "; p.slurpy=" << B(p.slurpy) << "; p.optional="
              << B(p.optional) << "; p.invocant=" << B(p.invocant) << "; p.isRw=" << B(p.isRw)
              << "; p.defConstraint=" << p.defConstraint << ";\n";
            if (p.whereExpr)  b << "    p.whereExpr=" << expr(p.whereExpr.get()) << "();\n";
            if (p.litVal)     b << "    p.litVal=" << expr(p.litVal.get()) << "();\n";
            if (p.defaultVal) b << "    p.defaultVal=" << expr(p.defaultVal.get()) << "();\n";
            b << "    n->params.push_back(std::move(p)); }\n";
        }
    }
    void stmtVec(const std::vector<StmtPtr>& v, const char* field, std::ostringstream& b) {
        for (auto& s : v) b << "  n->" << field << ".push_back(" << stmt(s.get()) << "());\n";
    }

    // ---- expressions -> a `static ExprPtr eN()` builder ----
    std::string expr(const Expr* e) {
        std::ostringstream b;
        switch (e->kind) {
            case NK::IntLit: { auto* n = static_cast<const IntLit*>(e);
                b << "  auto n = std::make_unique<IntLit>(" << n->v << "LL);\n";
                if (!n->big.empty()) b << "  n->big=" << S(n->big) << ";\n"; break; }
            case NK::NumLit: { auto* n = static_cast<const NumLit*>(e); std::ostringstream v; v.precision(17); v << n->v;
                b << "  auto n = std::make_unique<NumLit>(" << v.str() << ");\n  n->imaginary=" << B(n->imaginary) << ";\n";
                if (n->isRat) b << "  n->isRat=true; n->ratNum=" << n->ratNum << "LL; n->ratDen=" << n->ratDen << "LL;\n"; break; }
            case NK::StrLit: b << "  auto n = std::make_unique<StrLit>(" << S(static_cast<const StrLit*>(e)->v) << ");\n"; break;
            case NK::BoolLit: b << "  auto n = std::make_unique<BoolLit>(" << B(static_cast<const BoolLit*>(e)->v) << ");\n"; break;
            case NK::RegexLit: b << "  auto n = std::make_unique<RegexLit>(" << S(static_cast<const RegexLit*>(e)->pattern) << ");\n"; break;
            case NK::SubstLit: { auto* n = static_cast<const SubstLit*>(e);
                b << "  auto n = std::make_unique<SubstLit>(" << S(n->pattern) << ", " << S(n->repl) << ", " << B(n->nonMut) << ");\n"; break; }
            case NK::VarExpr: { auto* n = static_cast<const VarExpr*>(e);
                b << "  auto n = std::make_unique<VarExpr>(" << S(n->name) << ");\n  n->declare=" << B(n->declare)
                  << "; n->declScope=" << S(n->declScope) << "; n->declType=" << S(n->declType) << ";\n"; break; }
            case NK::NameTerm: b << "  auto n = std::make_unique<NameTerm>(" << S(static_cast<const NameTerm*>(e)->name) << ");\n"; break;
            case NK::SelfTerm: b << "  auto n = std::make_unique<SelfTerm>();\n"; break;
            case NK::Whatever: b << "  auto n = std::make_unique<WhateverExpr>();\n"; break;
            case NK::InterpStr: { b << "  auto n = std::make_unique<InterpStr>();\n";
                for (auto& p : static_cast<const InterpStr*>(e)->parts) b << "  n->parts.push_back(" << expr(p.get()) << "());\n"; break; }
            case NK::ListExpr: { b << "  auto n = std::make_unique<ListExpr>();\n";
                for (auto& i : static_cast<const ListExpr*>(e)->items) b << "  n->items.push_back(" << expr(i.get()) << "());\n"; break; }
            case NK::ArrayLit: { b << "  auto n = std::make_unique<ArrayLit>();\n";
                for (auto& i : static_cast<const ArrayLit*>(e)->items) b << "  n->items.push_back(" << expr(i.get()) << "());\n"; break; }
            case NK::HashLit: { b << "  auto n = std::make_unique<HashLit>();\n";
                for (auto& i : static_cast<const HashLit*>(e)->items) b << "  n->items.push_back(" << expr(i.get()) << "());\n"; break; }
            case NK::Assign: { auto* n = static_cast<const Assign*>(e);
                b << "  auto n = std::make_unique<Assign>();\n  n->op=" << S(n->op) << ";\n  n->target=" << expr(n->target.get())
                  << "();\n  n->value=" << expr(n->value.get()) << "();\n"; break; }
            case NK::Binary: { auto* n = static_cast<const Binary*>(e);
                b << "  auto n = std::make_unique<Binary>();\n  n->op=" << S(n->op) << ";\n  n->lhs=" << expr(n->lhs.get())
                  << "();\n  n->rhs=" << expr(n->rhs.get()) << "();\n"; break; }
            case NK::Unary: { auto* n = static_cast<const Unary*>(e);
                b << "  auto n = std::make_unique<Unary>();\n  n->op=" << S(n->op) << ";\n  n->postfix=" << B(n->postfix)
                  << ";\n  n->operand=" << expr(n->operand.get()) << "();\n"; break; }
            case NK::Call: { auto* n = static_cast<const Call*>(e);
                b << "  auto n = std::make_unique<Call>();\n  n->name=" << S(n->name) << ";\n";
                if (n->callee) b << "  n->callee=" << expr(n->callee.get()) << "();\n";
                for (auto& a : n->args) b << "  n->args.push_back(" << expr(a.get()) << "());\n"; break; }
            case NK::MethodCall: { auto* n = static_cast<const MethodCall*>(e);
                b << "  auto n = std::make_unique<MethodCall>();\n  n->inv=" << expr(n->inv.get()) << "();\n  n->method=" << S(n->method)
                  << ";\n  n->maybe=" << B(n->maybe) << "; n->mutate=" << B(n->mutate) << "; n->hyper=" << B(n->hyper) << "; n->meta=" << B(n->meta) << ";\n";
                for (auto& a : n->args) b << "  n->args.push_back(" << expr(a.get()) << "());\n"; break; }
            case NK::Index: { auto* n = static_cast<const Index*>(e);
                b << "  auto n = std::make_unique<Index>();\n  n->base=" << expr(n->base.get()) << "();\n  n->index=" << expr(n->index.get())
                  << "();\n  n->isHash=" << B(n->isHash) << "; n->adverb=" << S(n->adverb) << ";\n"; break; }
            case NK::Ternary: { auto* n = static_cast<const Ternary*>(e);
                b << "  auto n = std::make_unique<Ternary>();\n  n->cond=" << expr(n->cond.get()) << "();\n  n->then=" << expr(n->then.get())
                  << "();\n  n->els=" << expr(n->els.get()) << "();\n"; break; }
            case NK::Range: { auto* n = static_cast<const RangeExpr*>(e);
                b << "  auto n = std::make_unique<RangeExpr>();\n  n->from=" << expr(n->from.get()) << "();\n  n->to=" << expr(n->to.get())
                  << "();\n  n->exFrom=" << B(n->exFrom) << "; n->exTo=" << B(n->exTo) << ";\n"; break; }
            case NK::Pair: { auto* n = static_cast<const PairExpr*>(e);
                b << "  auto n = std::make_unique<PairExpr>();\n  n->key=" << S(n->key) << ";\n";
                if (n->keyExpr) b << "  n->keyExpr=" << expr(n->keyExpr.get()) << "();\n";
                if (n->value) b << "  n->value=" << expr(n->value.get()) << "();\n"; break; }
            case NK::ChainExpr: { auto* n = static_cast<const ChainExpr*>(e);
                b << "  auto n = std::make_unique<ChainExpr>();\n";
                for (auto& op : n->operands) b << "  n->operands.push_back(" << expr(op.get()) << "());\n";
                for (auto& op : n->ops) b << "  n->ops.push_back(" << S(op) << ");\n"; break; }
            case NK::BlockExpr: { auto* n = static_cast<const BlockExpr*>(e);
                b << "  auto n = std::make_unique<BlockExpr>();\n"; params(n->params, b); stmtVec(n->body, "body", b); break; }
            default: bail("an expression this backend can't rebuild");
        }
        b << "  return n;\n";
        return fn("e", "ExprPtr", b.str());
    }

    // ---- Block -> `static std::unique_ptr<Block> bN()` ----
    std::string block(const Block* bl) {
        std::ostringstream b;
        b << "  auto n = std::make_unique<Block>();\n  n->isCatch=" << B(bl->isCatch) << "; n->phaser=" << S(bl->phaser) << ";\n";
        stmtVec(bl->stmts, "stmts", b);
        b << "  return n;\n";
        return fn("b", "std::unique_ptr<Block>", b.str());
    }

    // ---- SubDecl -> `static std::unique_ptr<SubDecl> subN()` (for class methods) ----
    std::string subDecl(const SubDecl* d) {
        std::ostringstream b;
        b << "  auto n = std::make_unique<SubDecl>();\n  n->name=" << S(d->name) << "; n->isMulti=" << B(d->isMulti)
          << "; n->isMethod=" << B(d->isMethod) << ";\n";
        params(d->params, b); stmtVec(d->body, "body", b);
        b << "  return n;\n";
        return fn("sub", "std::unique_ptr<SubDecl>", b.str());
    }

    // ---- statements -> `static StmtPtr sN()` ----
    std::string stmt(const Stmt* s) {
        std::ostringstream b;
        switch (s->kind) {
            case NK::ExprStmt: b << "  auto n = std::make_unique<ExprStmt>();\n  n->e=" << expr(static_cast<const ExprStmt*>(s)->e.get()) << "();\n"; break;
            case NK::EmptyStmt: b << "  auto n = std::make_unique<EmptyStmt>();\n"; break;
            case NK::VarDecl: { auto* d = static_cast<const VarDecl*>(s);
                b << "  auto n = std::make_unique<VarDecl>();\n  n->scope=" << S(d->scope) << "; n->op=" << S(d->op) << ";\n";
                for (auto& nm : d->names) b << "  n->names.push_back(" << S(nm) << ");\n";
                if (d->init) b << "  n->init=" << expr(d->init.get()) << "();\n"; break; }
            case NK::SubDecl: return fn("s", "StmtPtr", "  return " + subDecl(static_cast<const SubDecl*>(s)) + "();\n");
            case NK::IfStmt: { auto* f = static_cast<const IfStmt*>(s);
                b << "  auto n = std::make_unique<IfStmt>();\n  n->isUnless=" << B(f->isUnless) << "; n->thenVar=" << S(f->thenVar) << ";\n";
                for (auto& br : f->branches) b << "  n->branches.emplace_back(" << expr(br.first.get()) << "(), " << block(br.second.get()) << "());\n";
                if (f->elseBlock) b << "  n->elseBlock=" << block(f->elseBlock.get()) << "();\n"; break; }
            case NK::WhileStmt: { auto* w = static_cast<const WhileStmt*>(s);
                b << "  auto n = std::make_unique<WhileStmt>();\n  n->isUntil=" << B(w->isUntil) << "; n->var=" << S(w->var)
                  << ";\n  n->cond=" << expr(w->cond.get()) << "();\n  n->body=" << block(w->body.get()) << "();\n"; break; }
            case NK::RepeatStmt: { auto* r = static_cast<const RepeatStmt*>(s);
                b << "  auto n = std::make_unique<RepeatStmt>();\n  n->isUntil=" << B(r->isUntil) << ";\n  n->cond=" << expr(r->cond.get())
                  << "();\n  n->body=" << block(r->body.get()) << "();\n"; break; }
            case NK::ForStmt: { auto* f = static_cast<const ForStmt*>(s);
                b << "  auto n = std::make_unique<ForStmt>();\n  n->destructure=" << B(f->destructure) << ";\n  n->list=" << expr(f->list.get()) << "();\n";
                for (auto& v : f->vars) b << "  n->vars.push_back(" << S(v) << ");\n";
                b << "  n->body=" << block(f->body.get()) << "();\n"; break; }
            case NK::LoopStmt: { auto* l = static_cast<const LoopStmt*>(s);
                b << "  auto n = std::make_unique<LoopStmt>();\n";
                if (l->init) b << "  n->init=" << expr(l->init.get()) << "();\n";
                if (l->cond) b << "  n->cond=" << expr(l->cond.get()) << "();\n";
                if (l->incr) b << "  n->incr=" << expr(l->incr.get()) << "();\n";
                b << "  n->body=" << block(l->body.get()) << "();\n"; break; }
            case NK::GivenStmt: { auto* g = static_cast<const GivenStmt*>(s);
                b << "  auto n = std::make_unique<GivenStmt>();\n  n->defGuard=" << g->defGuard << "; n->hasElse=" << B(g->hasElse)
                  << ";\n  n->topic=" << expr(g->topic.get()) << "();\n  n->body=" << block(g->body.get()) << "();\n";
                if (g->elseBody) b << "  n->elseBody=" << block(g->elseBody.get()) << "();\n"; break; }
            case NK::WhenStmt: { auto* w = static_cast<const WhenStmt*>(s);
                b << "  auto n = std::make_unique<WhenStmt>();\n  n->isDefault=" << B(w->isDefault) << ";\n";
                if (w->cond) b << "  n->cond=" << expr(w->cond.get()) << "();\n";
                b << "  n->body=" << block(w->body.get()) << "();\n"; break; }
            case NK::Block: return fn("s", "StmtPtr", "  return " + block(static_cast<const Block*>(s)) + "();\n");
            case NK::ReturnStmt: { auto* r = static_cast<const ReturnStmt*>(s);
                b << "  auto n = std::make_unique<ReturnStmt>();\n";
                if (r->value) b << "  n->value=" << expr(r->value.get()) << "();\n"; break; }
            case NK::LastStmt: b << "  auto n = std::make_unique<LastStmt>();\n"; break;
            case NK::NextStmt: b << "  auto n = std::make_unique<NextStmt>();\n"; break;
            case NK::RedoStmt: b << "  auto n = std::make_unique<RedoStmt>();\n"; break;
            case NK::UseStmt: { auto* u = static_cast<const UseStmt*>(s);
                b << "  auto n = std::make_unique<UseStmt>();\n  n->module=" << S(u->module) << "; n->arg=" << S(u->arg) << ";\n";
                if (u->argExpr) b << "  n->argExpr=" << expr(u->argExpr.get()) << "();\n"; break; }
            case NK::EnumDecl: { auto* d = static_cast<const EnumDecl*>(s);
                b << "  auto n = std::make_unique<EnumDecl>();\n  n->name=" << S(d->name) << ";\n";
                if (d->values) b << "  n->values=" << expr(d->values.get()) << "();\n"; break; }
            case NK::ClassDecl: { auto* c = static_cast<const ClassDecl*>(s);
                b << "  auto n = std::make_unique<ClassDecl>();\n  n->name=" << S(c->name) << "; n->parent=" << S(c->parent)
                  << "; n->isRole=" << B(c->isRole) << "; n->isGrammar=" << B(c->isGrammar) << "; n->isPackage=" << B(c->isPackage) << ";\n";
                for (auto& a : c->attrs) {
                    b << "  { AttrDecl a; a.name=" << S(a.name) << "; a.sigil='" << (a.sigil ? a.sigil : '$') << "'; a.pub=" << B(a.pub) << ";\n";
                    if (a.def) b << "    a.def=" << expr(a.def.get()) << "();\n";
                    b << "    n->attrs.push_back(std::move(a)); }\n";
                }
                for (auto& r : c->rules)
                    b << "  n->rules.push_back(GrammarRuleDecl{" << S(r.name) << ", " << S(r.pattern) << ", " << S(r.kind) << "});\n";
                for (auto& m : c->methods) b << "  n->methods.push_back(" << subDecl(m.get()) << "());\n";
                stmtVec(c->body, "body", b); break; }
            default: bail("a statement this backend can't rebuild");
        }
        b << "  return n;\n";
        return fn("s", "StmtPtr", b.str());
    }
};

} // namespace

void emitAstProgram(const Program& prog, std::ostream& out,
                    const std::string& fileName, const std::string& finish) {
    std::ostringstream defs;
    Emit em{defs};
    std::vector<std::string> tops;
    for (auto& s : prog.stmts) tops.push_back(em.stmt(s.get())); // fills `defs`

    out << "// Generated by `rakupp --aot`. The Raku program was parsed ahead of time;\n"
           "// this rebuilds that exact AST at startup and interprets it (no parsing here).\n"
           "#include \"Ast.h\"\n#include \"Runtime.h\"\n#include <memory>\n#include <string>\n#include <vector>\n"
           "#include <cstdlib>\n#include <unistd.h>\n"
           "using namespace rakupp;\n\n";
    out << defs.str() << "\n";
    out << "int main(int argc, char** argv) {\n"
           "  Program prog;\n";
    for (auto& t : tops) out << "  prog.stmts.push_back(" << t << "());\n";
    out << "  std::vector<std::string> args; for (int i = 1; i < argc; i++) args.push_back(argv[i]);\n"
           "  std::string exe = argc > 0 ? argv[0] : \"program\"; char rp[4096]; if (realpath(exe.c_str(), rp)) exe = rp;\n"
           "  return rakupp::rakuppRunProgramBigStack(prog, args, " << S(fileName) << ", exe, " << S(finish) << ");\n"
           "}\n";
}

} // namespace rakupp
