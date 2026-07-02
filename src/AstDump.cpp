#include "Ast.h"
#include <ostream>
#include <string>

namespace rakupp {
namespace {

std::string trunc(std::string s, size_t n = 48) {
    for (auto& c : s) if (c == '\n') c = '|';
    return s.size() > n ? s.substr(0, n) + "…" : s;
}

struct Dumper {
    std::ostream& o;
    void line(int ind, const std::string& s) { o << std::string(ind * 2, ' ') << s << "\n"; }

    void params(const std::vector<Param>& ps, int ind) {
        for (auto& p : ps) {
            std::string desc = std::string("param ") + p.sigil + (p.name.empty() ? "" : p.name.substr(p.name.empty() ? 0 : 1));
            if (!p.type.empty()) desc += " : " + p.type;
            if (p.named) desc += " [named]";
            if (p.slurpy) desc += " [slurpy]";
            if (p.optional) desc += " [optional]";
            if (p.invocant) desc += " [invocant]";
            line(ind, desc);
            if (p.litVal) { line(ind + 1, "literal:"); expr(p.litVal.get(), ind + 2); }
            if (p.defaultVal) { line(ind + 1, "default:"); expr(p.defaultVal.get(), ind + 2); }
        }
    }
    void body(const std::vector<StmtPtr>& stmts, int ind) { for (auto& s : stmts) stmt(s.get(), ind); }

    void expr(const Expr* e, int ind) {
        if (!e) { line(ind, "<null>"); return; }
        switch (e->kind) {
            case NK::IntLit: { auto* n = static_cast<const IntLit*>(e);
                line(ind, "IntLit " + (n->big.empty() ? std::to_string(n->v) : n->big)); return; }
            case NK::NumLit: { auto* n = static_cast<const NumLit*>(e);
                line(ind, "NumLit " + std::to_string(n->v) + (n->imaginary ? "i" : "")); return; }
            case NK::StrLit: line(ind, "StrLit \"" + trunc(static_cast<const StrLit*>(e)->v) + "\""); return;
            case NK::BoolLit: line(ind, std::string("BoolLit ") + (static_cast<const BoolLit*>(e)->v ? "True" : "False")); return;
            case NK::VarExpr: { auto* v = static_cast<const VarExpr*>(e);
                line(ind, "VarExpr " + v->name + (v->declare ? " [decl " + (v->declScope.empty() ? "my" : v->declScope) +
                          (v->declType.empty() ? "" : " " + v->declType) + "]" : "")); return; }
            case NK::NameTerm: line(ind, "NameTerm " + static_cast<const NameTerm*>(e)->name); return;
            case NK::SelfTerm: line(ind, "self"); return;
            case NK::Whatever: line(ind, "Whatever *"); return;
            case NK::RegexLit: line(ind, "RegexLit /" + trunc(static_cast<const RegexLit*>(e)->pattern) + "/"); return;
            case NK::SubstLit: { auto* s = static_cast<const SubstLit*>(e);
                line(ind, std::string(s->nonMut ? "S" : "s") + "/" + trunc(s->pattern) + "/" + trunc(s->repl) + "/"); return; }
            case NK::ListExpr:  line(ind, "ListExpr");  for (auto& i : static_cast<const ListExpr*>(e)->items)  expr(i.get(), ind + 1); return;
            case NK::ArrayLit:  line(ind, "ArrayLit");  for (auto& i : static_cast<const ArrayLit*>(e)->items)  expr(i.get(), ind + 1); return;
            case NK::HashLit:   line(ind, "HashLit");   for (auto& i : static_cast<const HashLit*>(e)->items)   expr(i.get(), ind + 1); return;
            case NK::Assign: { auto* a = static_cast<const Assign*>(e);
                line(ind, "Assign " + a->op); expr(a->target.get(), ind + 1); expr(a->value.get(), ind + 1); return; }
            case NK::Binary: { auto* b = static_cast<const Binary*>(e);
                line(ind, "Binary " + b->op); expr(b->lhs.get(), ind + 1); expr(b->rhs.get(), ind + 1); return; }
            case NK::Unary: { auto* u = static_cast<const Unary*>(e);
                line(ind, std::string("Unary ") + u->op + (u->postfix ? " [postfix]" : "")); expr(u->operand.get(), ind + 1); return; }
            case NK::Ternary: { auto* t = static_cast<const Ternary*>(e);
                line(ind, "Ternary"); line(ind + 1, "cond:"); expr(t->cond.get(), ind + 2);
                line(ind + 1, "then:"); expr(t->then.get(), ind + 2); line(ind + 1, "else:"); expr(t->els.get(), ind + 2); return; }
            case NK::Range: { auto* r = static_cast<const RangeExpr*>(e);
                line(ind, std::string("Range ") + (r->exFrom ? "^" : "") + ".." + (r->exTo ? "^" : ""));
                expr(r->from.get(), ind + 1); expr(r->to.get(), ind + 1); return; }
            case NK::Pair: { auto* p = static_cast<const PairExpr*>(e);
                line(ind, "Pair " + (p->keyExpr ? "" : p->key));
                if (p->keyExpr) { line(ind + 1, "key:"); expr(p->keyExpr.get(), ind + 2); }
                if (p->value) { line(ind + 1, "value:"); expr(p->value.get(), ind + 2); } return; }
            case NK::Call: { auto* c = static_cast<const Call*>(e);
                line(ind, "Call " + (c->name.empty() ? "(callee)" : c->name));
                if (c->callee) { line(ind + 1, "callee:"); expr(c->callee.get(), ind + 2); }
                for (auto& a : c->args) expr(a.get(), ind + 1); return; }
            case NK::MethodCall: { auto* m = static_cast<const MethodCall*>(e);
                std::string f; if (m->maybe) f += " .?"; if (m->mutate) f += " .="; if (m->hyper) f += " >>."; if (m->meta) f += " .^";
                line(ind, "MethodCall ." + m->method + f); line(ind + 1, "invocant:"); expr(m->inv.get(), ind + 2);
                for (auto& a : m->args) expr(a.get(), ind + 1); return; }
            case NK::Index: { auto* x = static_cast<const Index*>(e);
                line(ind, std::string("Index ") + (x->isHash ? "{}" : "[]") + (x->adverb.empty() ? "" : " :" + x->adverb));
                expr(x->base.get(), ind + 1); expr(x->index.get(), ind + 1); return; }
            case NK::InterpStr: line(ind, "InterpStr"); for (auto& p : static_cast<const InterpStr*>(e)->parts) expr(p.get(), ind + 1); return;
            case NK::ChainExpr: { auto* c = static_cast<const ChainExpr*>(e);
                std::string ops; for (auto& op : c->ops) ops += " " + op;
                line(ind, "ChainExpr" + ops); for (auto& op : c->operands) expr(op.get(), ind + 1); return; }
            case NK::BlockExpr: { auto* b = static_cast<const BlockExpr*>(e);
                line(ind, "BlockExpr"); params(b->params, ind + 1); body(b->body, ind + 1); return; }
            default: line(ind, "Expr?"); return;
        }
    }

    void stmt(const Stmt* s, int ind) {
        if (!s) { line(ind, "<null>"); return; }
        switch (s->kind) {
            case NK::ExprStmt: expr(static_cast<const ExprStmt*>(s)->e.get(), ind); return;
            case NK::EmptyStmt: return;
            case NK::VarDecl: { auto* d = static_cast<const VarDecl*>(s);
                std::string names; for (auto& n : d->names) names += (names.empty() ? "" : ", ") + n;
                line(ind, "VarDecl " + d->scope + " (" + names + ")" + (d->init ? " " + d->op : ""));
                if (d->init) expr(d->init.get(), ind + 1); return; }
            case NK::SubDecl: { auto* d = static_cast<const SubDecl*>(s);
                line(ind, std::string(d->isMulti ? "MultiSub " : d->isMethod ? "Method " : "Sub ") + (d->name.empty() ? "(anon)" : d->name));
                params(d->params, ind + 1); body(d->body, ind + 1); return; }
            case NK::IfStmt: { auto* f = static_cast<const IfStmt*>(s);
                line(ind, f->isUnless ? "Unless" : "If");
                for (auto& br : f->branches) { line(ind + 1, "cond:"); expr(br.first.get(), ind + 2);
                    line(ind + 1, "then:"); body(br.second->stmts, ind + 2); }
                if (f->elseBlock) { line(ind + 1, "else:"); body(f->elseBlock->stmts, ind + 2); } return; }
            case NK::WhileStmt: { auto* w = static_cast<const WhileStmt*>(s);
                line(ind, w->isUntil ? "Until" : "While"); line(ind + 1, "cond:"); expr(w->cond.get(), ind + 2);
                body(w->body->stmts, ind + 1); return; }
            case NK::RepeatStmt: { auto* r = static_cast<const RepeatStmt*>(s);
                line(ind, std::string("Repeat ") + (r->isUntil ? "until" : "while")); body(r->body->stmts, ind + 1);
                line(ind + 1, "cond:"); expr(r->cond.get(), ind + 2); return; }
            case NK::ForStmt: { auto* f = static_cast<const ForStmt*>(s);
                std::string vars; for (auto& v : f->vars) vars += (vars.empty() ? "" : ", ") + v;
                line(ind, "For " + (vars.empty() ? "($_)" : "-> " + vars) + (f->destructure ? " [destructure]" : ""));
                line(ind + 1, "list:"); expr(f->list.get(), ind + 2); body(f->body->stmts, ind + 1); return; }
            case NK::LoopStmt: { auto* l = static_cast<const LoopStmt*>(s);
                line(ind, "Loop");
                if (l->init) { line(ind + 1, "init:"); expr(l->init.get(), ind + 2); }
                if (l->cond) { line(ind + 1, "cond:"); expr(l->cond.get(), ind + 2); }
                if (l->incr) { line(ind + 1, "incr:"); expr(l->incr.get(), ind + 2); }
                body(l->body->stmts, ind + 1); return; }
            case NK::GivenStmt: { auto* g = static_cast<const GivenStmt*>(s);
                line(ind, g->defGuard == 1 ? "With" : g->defGuard == 2 ? "Without" : "Given");
                line(ind + 1, "topic:"); expr(g->topic.get(), ind + 2); body(g->body->stmts, ind + 1); return; }
            case NK::WhenStmt: { auto* w = static_cast<const WhenStmt*>(s);
                line(ind, w->isDefault ? "Default" : "When");
                if (w->cond) expr(w->cond.get(), ind + 1); body(w->body->stmts, ind + 1); return; }
            case NK::Block: { auto* b = static_cast<const Block*>(s);
                line(ind, b->isCatch ? "CATCH" : b->phaser.empty() ? "Block" : "Phaser " + b->phaser);
                body(b->stmts, ind + 1); return; }
            case NK::ReturnStmt: { auto* r = static_cast<const ReturnStmt*>(s);
                line(ind, "Return"); if (r->value) expr(r->value.get(), ind + 1); return; }
            case NK::LastStmt: line(ind, "last"); return;
            case NK::NextStmt: line(ind, "next"); return;
            case NK::RedoStmt: line(ind, "redo"); return;
            case NK::UseStmt: { auto* u = static_cast<const UseStmt*>(s);
                line(ind, "Use " + u->module + (u->arg.empty() ? "" : " '" + u->arg + "'")); return; }
            case NK::EnumDecl: { auto* d = static_cast<const EnumDecl*>(s);
                line(ind, "Enum " + (d->name.empty() ? "(anon)" : d->name)); if (d->values) expr(d->values.get(), ind + 1); return; }
            case NK::ClassDecl: { auto* c = static_cast<const ClassDecl*>(s);
                line(ind, std::string(c->isRole ? "Role " : c->isGrammar ? "Grammar " : c->isPackage ? "Package " : "Class ")
                        + c->name + (c->parent.empty() ? "" : " is " + c->parent));
                for (auto& a : c->attrs) line(ind + 1, std::string("attr ") + a.sigil + (a.pub ? "." : "!") + a.name);
                for (auto& m : c->methods) stmt(m.get(), ind + 1);
                body(c->body, ind + 1); return; }
            default: line(ind, "Stmt?"); return;
        }
    }
};

} // namespace

void dumpAst(const Program& prog, std::ostream& out) {
    Dumper d{out};
    d.line(0, "Program");
    for (auto& s : prog.stmts) d.stmt(s.get(), 1);
}

} // namespace rakupp
