// `'source'.AST` — the RakuAST VIEW over our own tree (RAKUAST-PLAN P1).
//
// This is the direction P2c's renderer does not cover. There, a module builds a
// tree with `.new` and we render it; here a module hands us SOURCE and wants
// Rakudo's node objects back, so the parser runs and this file walks what it
// produced, one case per NK.
//
// It is a VIEW, built on demand, and Part I is where that was decided: RakuAST
// is 2.3× the nodes of our tree and ~2.1× the visits in the fib inner loop, and
// Rakudo can afford that because it compiles RakuAST away while we would walk
// it forever. So nothing here is cached on our nodes, nothing is cloned, no
// `DecidedOnce`/`PublishedOnce` field is written, and no `padOwner` is
// dereferenced — the walk is read-only over a tree that is still running.
//
// TWO RECORDED DIVERGENCES from Rakudo, both from Part I and neither fixable
// here: `.AST` runs Lexer+Parser only, so (1) we surface syntax errors but not
// Rakudo's compile-time undeclared-variable errors, and (2) a `BEGIN` block
// EXECUTES at `.AST` time in Rakudo and does not here. The oracle harness
// filters programs containing BEGIN for exactly that reason.
//
// A kind with no faithful mapping THROWS, naming itself. A wrong tree would be
// worse than no tree: the whole point of the view is that a module can walk it.

#include "RakuAstClasses.h"

#include "Ast.h"
#include "AsciiCtype.h"
#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"
#include "Pod.h"      // parsePod — the raw material for the `Doc::` view (P5)
#include "Value.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace rakupp {

namespace {

struct Builder {
    Interpreter& I;
    explicit Builder(Interpreter& i) : I(i) {}

    [[noreturn]] void unmapped(const char* what) {
        throw RakuError{Value::typeObj("X::NYI"),
                        std::string("`.AST` does not yet build a view for ") + what};
    }

    // A node of the registry class, with the attributes given. Everything the
    // view builds goes through here, so a name the registry does not carry is
    // caught at the one place rather than producing a half-built object.
    Value node(const char* cls, std::initializer_list<std::pair<const char*, Value>> attrs) {
        std::string qualified = std::string(kRakuAstPrefix) + cls;
        const std::shared_ptr<ClassInfo>* ci = rakuAstClass(qualified);
        if (!ci) unmapped(cls);
        auto od = makePayload<ObjectData>();
        od->cls = *ci;
        for (auto& a : attrs) od->attrs[a.first] = a.second;
        return Value::object(od);
    }
    Value node(const char* cls) { return node(cls, {}); }

    Value list(ValueList items) {
        Value v = Value::array();
        for (auto& i : items) v.arr()->push_back(i);
        return v;
    }

    // `foo` / `Foo::Bar` as a RakuAST::Name — the parts are plain strings, the
    // spelling `.from-identifier` produces and the renderer joins with `::`.
    Value name(const std::string& n) {
        ValueList parts;
        size_t at = 0;
        for (;;) {
            size_t c = n.find("::", at);
            if (c == std::string::npos) { parts.push_back(Value::str(n.substr(at))); break; }
            parts.push_back(Value::str(n.substr(at, c - at)));
            at = c + 2;
        }
        return node("Name", {{"parts", list(parts)}});
    }

    // A statement list from our statements, and the Blockoid around one.
    Value statementList(const std::vector<StmtPtr>& stmts) {
        ValueList out;
        for (auto& s : stmts) {
            // (the fresh `my $/; my $!` the parser gives a `start` block are implicit
            // upstream, and line 0 here: written out, each round of `.AST.DEPARSE`
            // added another pair)
            if (s->kind == NK::ExprStmt && s->line == 0) {
                const Expr* e = static_cast<const ExprStmt*>(s.get())->e.get();
                if (e && e->kind == NK::VarExpr && e->line == 0 && static_cast<const VarExpr*>(e)->declare &&
                    (static_cast<const VarExpr*>(e)->name == "$/" || static_cast<const VarExpr*>(e)->name == "$!"))
                    continue;
            }
            out.push_back(buildStmt(s.get()));
        }
        return node("StatementList", {{"statements", list(out)}});
    }
    Value blockoid(const std::vector<StmtPtr>& stmts) {
        return node("Blockoid", {{"statement-list", statementList(stmts)}});
    }

    // `$a + $b`. The operands live in an ARGLIST beside the infix, not in
    // `left`/`right` slots — measured: 2026.08's `ApplyInfix` carries exactly
    // `$!infix` and `$!args`, and `.left`/`.right` read through the list. The
    // first version of the view invented the two slots, which put two nodes in
    // the tree that Rakudo's has not and dropped one that it has.
    Value applyInfix(Value infix, Value lhs, Value rhs) {
        return node("ApplyInfix", {{"infix", infix},
                                   {"args",  node("ArgList", {{"args", list({lhs, rhs})}})}});
    }
    Value applyInfix(const std::string& op, Value lhs, Value rhs) {
        return applyInfix(node("Infix", {{"operator", Value::str(op)}}), lhs, rhs);
    }
    // …and the two assignment spellings, which are not plain infixes upstream:
    // `=` has its own class, and `+=` is the metaop applied to `+` rather than
    // an operator named `+=`. (`:=` IS a plain infix — measured, not assumed.)
    Value assignInfix(const std::string& op) {
        if (op == "=")  return node("Assignment", {{"operator", Value::str("=")}});
        if (op == ":=") return node("Infix", {{"operator", Value::str(":=")}});
        return node("MetaInfix::Assign", {{"infix",
            node("Infix", {{"operator", Value::str(op.substr(0, op.size() - 1))}})}});
    }

    // ---- expressions ----------------------------------------------------
    // Written `( … )` is a Circumfix::Parentheses upstream, around whatever
    // was written inside. Without it `(1 + 2) * 3` came back as `1 + 2 * 3`,
    // `(-$y) ** 2` as `-$y ** 2` and `(1 .. 4).sum` as `1 .. 4.sum`, and
    // `.AST.EVAL` answered 7, -9 and a Range — while the round-trip harness,
    // which asks only that the text parse, saw the one case that did not:
    // `(0 max 5) min 3`. A list builds its own (its `parenned`), and a SECOND
    // pair is kept where the parse records one, `((* + 1))`, because it
    // finishes a WhateverCode rather than currying it.
    Value buildExpr(Expr* e) {
        Value v = buildExprBare(e);
        if (!e || !e->inParens || e->kind == NK::ListExpr) return v;
        v = parenthesized(v);
        bool twice = e->kind == NK::Binary     ? static_cast<Binary*>(e)->curryClosed
                   : e->kind == NK::MethodCall ? static_cast<MethodCall*>(e)->curryClosed
                   : e->kind == NK::Whatever   ? static_cast<WhateverExpr*>(e)->curryClosed : false;
        return twice ? parenthesized(v) : v;
    }
    Value buildExprBare(Expr* e) {
        if (!e) return Value::any();
        switch (e->kind) {
            case NK::IntLit: {
                auto* n = static_cast<IntLit*>(e);
                return node("IntLiteral", {{"value", n->big.empty() ? Value::integer(n->v)
                                                                    : Value::str(n->big)}});
            }
            case NK::NumLit: {
                auto* n = static_cast<NumLit*>(e);
                // A decimal literal is a Rat in Raku, and Rakudo's tree says so
                // with a different class — `3.14` is a RatLiteral, `4e2` a
                // NumLiteral. Reading `isRat` is what keeps them apart.
                return node(n->isRat ? "RatLiteral" : "NumLiteral",
                            {{"value", Value::number(n->v)}});
            }
            case NK::StrLit:
                return node("StrLiteral", {{"value", Value::str(static_cast<StrLit*>(e)->v)}});
            case NK::BoolLit:
                // `True`/`False` are terms naming a Bool enum VALUE, not
                // literals and not plain names: Rakudo spells a resolved enum
                // value `Term::Enum`, and these two are the one case a view can
                // spell it too without a setting to resolve against — the lexer
                // already knows it read a Bool. A capitalized name we do not
                // know (`Less`, `SeekFromBeginning`) stays a `Term::Name`,
                // because knowing it is an enum value is exactly the resolution
                // a view does not do.
                return node("Term::Enum", {{"name", name(static_cast<BoolLit*>(e)->v ? "True" : "False")}});
            case NK::VarExpr: {
                auto* v = static_cast<VarExpr*>(e);
                if (v->declare) return declaration(v);
                if (v->name.size() > 1 && v->name[1] == '*')
                    return node("Var::Dynamic", {{"name", Value::str(v->name)}});
                // `$!x` is an ATTRIBUTE, not a lexical — a different class
                // upstream, and the one a walker looks for when it asks which
                // attributes a method touches.
                if (v->name.size() > 1 && v->name[1] == '!')
                    return node("Var::Attribute", {{"name", Value::str(v->name)}});
                return node("Var::Lexical", {{"name", Value::str(v->name)}});
            }
            case NK::NameTerm: {
                auto* n = static_cast<NameTerm*>(e);
                // A bare type name in term position is a Type::Simple where a
                // type is meant; Rakudo spells the general term Term::Name.
                return node("Term::Name", {{"name", name(n->name)}});
            }
            case NK::SelfTerm:   return node("Term::Self");
            case NK::Whatever:   return node("Term::Whatever");
            case NK::Binary: {
                auto* b = static_cast<Binary*>(e);
                return applyInfix(b->written(), buildExpr(b->lhs.get()), buildExpr(b->rhs.get()));   // `×` stays `×`
            }
            case NK::Assign: {
                auto* a = static_cast<Assign*>(e);
                // `my $x = 1` reaches here as an assignment whose TARGET is a
                // declaration — our parser's usual shape. Rakudo has no such
                // shape at all: a declaration owns its initializer and the `=`
                // never becomes an operator, so building one put an ApplyInfix
                // and an Infix in the tree that Rakudo's has not and left out
                // the `Initializer::Assign` that it has. The statement-level
                // `VarDecl` path below already did this; only this one did not.
                if (a->target && a->target->kind == NK::VarExpr &&
                    static_cast<VarExpr*>(a->target.get())->declare &&
                    (a->op == "=" || a->op == ":=")) {
                    Value decl = declaration(static_cast<VarExpr*>(a->target.get()));
                    decl.obj()->attrs["initializer"] =
                        node(a->op == ":=" ? "Initializer::Bind" : "Initializer::Assign",
                             {{"expression", buildExpr(a->value.get())}});
                    return decl;
                }
                return applyInfix(assignInfix(a->written()),
                                  buildExpr(a->target.get()), buildExpr(a->value.get()));
            }
            case NK::Unary: {
                auto* u = static_cast<Unary*>(e);
                // The CONTEXTUALIZERS `@(…)`, `%(…)`, `$(…)` are their own
                // classes upstream, not prefix operators. Ours spells them
                // `ctx@`/`ctx%`/`ctx$`, and rendering that as a prefix put the
                // internal name into the source: `@($x)` came back `ctx@ $x`,
                // which is not Raku.
                if (u->op.size() == 4 && u->op.compare(0, 3, "ctx") == 0) {
                    const char* cls = u->op[3] == '@' ? "Contextualizer::List"
                                    : u->op[3] == '%' ? "Contextualizer::Hash"
                                                      : "Contextualizer::Item";
                    return node(cls, {{"target", semiList({buildExpr(u->operand.get())})}});
                }
                if (u->postfix)
                    return node("ApplyPostfix", {{"operand", buildExpr(u->operand.get())},
                                                 {"postfix", node("Postfix", {{"operator", Value::str(u->op)}})}});
                return node("ApplyPrefix", {{"prefix",  node("Prefix", {{"operator", Value::str(u->op)}})},
                                            {"operand", buildExpr(u->operand.get())}});
            }
            case NK::Ternary: {
                auto* t = static_cast<Ternary*>(e);
                return node("Ternary", {{"condition", buildExpr(t->cond.get())},
                                        {"then",      buildExpr(t->then.get())},
                                        {"else",      buildExpr(t->els.get())}});
            }
            case NK::ChainExpr: {
                // `1 < $x < 10` — Rakudo keeps the whole chain in one node, and
                // so does our tree, so this is one of the few 1:1 shapes.
                auto* c = static_cast<ChainExpr*>(e);
                ValueList ops, operands;
                for (auto& o : c->operands) operands.push_back(buildExpr(o.get()));
                for (auto& o : c->ops) ops.push_back(node("Infix", {{"operator", Value::str(o)}}));
                return node("ApplyInfix::Chaining", {{"operands", list(operands)},
                                                     {"infixes",  list(ops)}});
            }
            case NK::Call: {
                auto* c = static_cast<Call*>(e);
                ValueList args;
                // `start 5`: the parser thunks the expression in a block of its own
                // (statements of line 0, the implicit `$/` and `$!` among them),
                // which upstream is no block at all
                if ((c->name == "start" || c->name == "CORE::start") && c->args.size() == 1 &&
                    c->args[0]->kind == NK::BlockExpr) {
                    const Expr* only = nullptr; int real = 0;
                    for (auto& st : static_cast<BlockExpr*>(c->args[0].get())->body)
                        if (st->line != 0 || st->kind != NK::ExprStmt) real += 2;
                        else if (auto* x = static_cast<const ExprStmt*>(st.get())->e.get();
                                 !(x && x->kind == NK::VarExpr && static_cast<const VarExpr*>(x)->declare && x->line == 0))
                            { only = x; real++; }
                    if (real == 1 && only) args.push_back(buildExpr(const_cast<Expr*>(only)));
                }
                if (args.empty()) for (auto& a : c->args) args.push_back(buildExpr(a.get()));
                Value arglist = node("ArgList", {{"args", list(args)}});
                if (c->callee)   // `$code(…)` — a call on an expression, not a name
                    return node("ApplyPostfix", {{"operand", buildExpr(c->callee.get())},
                                                 {"postfix", node("Call::Term", {{"args", arglist}})}});
                // `parenned` is the bit P1 records at parse time: Rakudo has a
                // separate class for the listop spelling, and it is the commonest
                // call shape in the corpus (1,571 of 1,870 programs).
                return node(c->parenned ? "Call::Name" : "Call::Name::WithoutParentheses",
                            {{"name", name(c->name)}, {"args", arglist}});
            }
            case NK::MethodCall: {
                auto* m = static_cast<MethodCall*>(e);
                if (m->methodExpr) unmapped("an indirect method call (.\"$name\"())");
                ValueList args;
                for (auto& a : m->args) args.push_back(buildExpr(a.get()));
                const char* cls = m->meta ? "Call::MetaMethod"
                                : m->bang ? "Call::PrivateMethod"
                                : m->maybe ? "Call::MaybeMethod" : "Call::Method";
                Value call = args.empty()
                    ? node(cls, {{"name", name(m->method)}})
                    : node(cls, {{"name", name(m->method)},
                                 {"args", node("ArgList", {{"args", list(args)}})}});
                // `.method` with no invocant written is Rakudo's Term::TopicCall,
                // not an ApplyPostfix over `$_` — the parser records which one it
                // saw in `synthTopic`, because the two deparse the same and a
                // view that guessed would answer a different tree each way.
                Expr* inv = m->inv.get();
                // `$x.=abs` ASSIGNS the call's result: an ApplyDottyInfix with
                // the CallAssign infix upstream, not a postfix call. Built as a
                // call it came back as `$x.abs`, which leaves $x as it was.
                if (m->mutate) {
                    if (m->meta || m->bang || m->maybe || m->hyper || m->allMode)
                        unmapped("a `.=` call through `.^`, `!`, `.?`, `>>.`, `.+` or `.*`");
                    return node("ApplyDottyInfix", {{"left", buildExpr(inv)},
                                                    {"infix", node("DottyInfix::CallAssign")},
                                                    {"right", call}});
                }
                if (inv && inv->kind == NK::VarExpr && static_cast<VarExpr*>(inv)->synthTopic)
                    return node("Term::TopicCall", {{"call", call}});
                return node("ApplyPostfix", {{"operand", buildExpr(inv)}, {"postfix", call}});
            }
            case NK::Index: {
                auto* ix = static_cast<Index*>(e);
                // `%h<a>` is a different class from `%h{'a'}` upstream, and the
                // parser records which spelling it saw (`angleKey`).
                const char* cls = ix->isHash ? (ix->angleKey ? "Postcircumfix::LiteralHashIndex"
                                                             : "Postcircumfix::HashIndex")
                                             : "Postcircumfix::ArrayIndex";
                // The angle form's index is the WORD LIST upstream, a
                // QuotedString with the `words` processor — as in `enum C <a
                // b>` below. Our parse keeps the words as string literals, and
                // rendering those put quotes inside the angles: `%h<a>` came
                // back as `%h<"a">`, a lookup of the key `"a"`, quotes and all.
                // A ZEN slice (`%h<>:k`, `%h{}:k`, `@a[]:v`) has an empty
                // index, where the parse holds a stand-in Whatever.
                Value inner;
                if (ix->angleKey) {
                    std::string words;
                    Expr* w = ix->zen ? nullptr : ix->index.get();
                    if (w && w->kind == NK::StrLit) words = static_cast<StrLit*>(w)->v;
                    else if (w && w->kind == NK::ArrayLit) {
                        for (auto& it : static_cast<ArrayLit*>(w)->items) {
                            if (!it || it->kind != NK::StrLit) unmapped("an angle subscript that is not a word list");
                            if (!words.empty()) words += " ";
                            words += static_cast<StrLit*>(it.get())->v;
                        }
                    }
                    else if (w) unmapped("an angle subscript that is not a word list");
                    ValueList procs;
                    procs.push_back(Value::str("words"));
                    procs.push_back(Value::str("val"));
                    inner = node("QuotedString",
                        {{"segments",   list({node("StrLiteral", {{"value", Value::str(words)}})})},
                         {"processors", list(procs)}});
                }
                else inner = ix->zen ? node("SemiList", {{"statements", list({})}})
                                     : semiList({buildExpr(ix->index.get())});
                Value pc = node(cls, {{"index", inner}});
                // `:exists`, `:!exists`, `:delete`, `:kv`, … — the adverbs are
                // the subscript's `colonpairs` upstream. Dropped, `%h{"a"}:delete`
                // came back as a lookup that deletes nothing. The parse joins
                // them with `:`, and spells `:$delete` as `$delete` and
                // `:exists($v)` as `exists?$v`.
                if (!ix->adverb.empty()) {
                    ValueList cps;
                    size_t at = 0;
                    for (;;) {
                        size_t c = ix->adverb.find(':', at);
                        std::string a = ix->adverb.substr(at, c == std::string::npos ? std::string::npos : c - at);
                        size_t q = a.find('?');
                        if (q != std::string::npos)
                            cps.push_back(node("ColonPair::Value", {{"key", Value::str(a.substr(0, q))},
                                {"value", node("Var::Lexical", {{"name", Value::str(a.substr(q + 1))}})}}));
                        else if (!a.empty() && a[0] == '$')
                            cps.push_back(node("ColonPair::Value", {{"key", Value::str(a.substr(1))},
                                {"value", node("Var::Lexical", {{"name", Value::str(a)}})}}));
                        else if (!a.empty() && a[0] == '!')
                            cps.push_back(node("ColonPair::False", {{"key", Value::str(a.substr(1))}}));
                        else if (!a.empty())
                            cps.push_back(node("ColonPair::True", {{"key", Value::str(a)}}));
                        if (c == std::string::npos) break;
                        at = c + 1;
                    }
                    pc.obj()->attrs["colonpairs"] = list(cps);
                }
                return node("ApplyPostfix", {{"operand", buildExpr(ix->base.get())},
                                             {"postfix", pc}});
            }
            case NK::ListExpr: {
                auto* l = static_cast<ListExpr*>(e);
                ValueList items;
                for (auto& i : l->items) items.push_back(buildExpr(i.get()));
                // `(1 + 2; 3 + 4)` is a list of two; without its parentheses
                // it rendered as two statements, and the second one's value
                if (l->semicolon)
                    return l->parenned || l->inParens
                        ? node("Circumfix::Parentheses", {{"semilist", semiList(items)}})
                        : semiList(items);
                Value applied = node("ApplyListInfix",
                    {{"infix", node("Infix", {{"operator", Value::str(",")}})},
                     {"operands", list(items)}});
                return l->parenned ? node("Circumfix::Parentheses",
                                          {{"semilist", semiList({applied})}})
                                   : applied;
            }
            case NK::ArrayLit: {
                auto* a = static_cast<ArrayLit*>(e);
                ValueList items;
                for (auto& i : a->items) items.push_back(buildExpr(i.get()));
                Value inner = node("ApplyListInfix",
                    {{"infix", node("Infix", {{"operator", Value::str(",")}})},
                     {"operands", list(items)}});
                return node("Circumfix::ArrayComposer",
                            {{"semilist", semiList({inner})}});
            }
            case NK::HashLit: {
                auto* h = static_cast<HashLit*>(e);
                ValueList items;
                for (auto& i : h->items) items.push_back(buildExpr(i.get()));
                Value inner = node("ApplyListInfix",
                    {{"infix", node("Infix", {{"operator", Value::str(",")}})},
                     {"operands", list(items)}});
                return node("Circumfix::HashComposer",
                            {{"semilist", semiList({inner})}});
            }
            case NK::Range: {
                auto* r = static_cast<RangeExpr*>(e);
                std::string op = r->exFrom ? (r->exTo ? "^..^" : "^..") : (r->exTo ? "..^" : "..");
                return applyInfix(op, buildExpr(r->from.get()), buildExpr(r->to.get()));
            }
            case NK::Pair: {
                auto* p = static_cast<PairExpr*>(e);
                if (p->keyExpr) unmapped("a pair with a computed key");
                if (!p->value)  // `:x` — the flag form, a distinct class upstream
                    return node("ColonPair::True", {{"key", Value::str(p->key)}});
                // `:all` and `:!all` are their own classes upstream, and our
                // parser fills the flag form's value in with a Bool rather than
                // leaving it empty — so a flag came back out as `:all(True)`.
                // Same program, different spelling, and the spelling is what a
                // renderer is for.
                if (p->colonForm && p->value->kind == NK::BoolLit)
                    return node(static_cast<BoolLit*>(p->value.get())->v
                                    ? "ColonPair::True" : "ColonPair::False",
                                {{"key", Value::str(p->key)}});
                if (p->colonForm)
                    return node("ColonPair::Value", {{"key", Value::str(p->key)},
                                                     {"value", buildExpr(p->value.get())}});
                return node("FatArrow", {{"key",   Value::str(p->key)},
                                         {"value", buildExpr(p->value.get())}});
            }
            case NK::InterpStr: {
                auto* s = static_cast<InterpStr*>(e);
                ValueList segs;
                for (auto& p : s->parts) {
                    Value seg = buildExpr(p.get());
                    // A literal run stays a StrLiteral; anything else is `{…}`
                    // in the source, and `{…}` is a BLOCK — upstream the
                    // segment is a whole `Block → Blockoid → StatementList`,
                    // not the bare expression. A simple `$x` interpolation is
                    // the exception: it is the variable itself.
                    if (p->kind != NK::StrLit && p->kind != NK::VarExpr)
                        seg = node("Block", {{"body", node("Blockoid", {{"statement-list",
                                  node("StatementList", {{"statements", list({
                                      node("Statement::Expression", {{"expression", seg}})})}})}})}});
                    segs.push_back(seg);
                }
                return node("QuotedString", {{"segments", list(segs)}});
            }
            case NK::RegexLit: {
                // A SOURCE SLICE, on purpose: a regex literal's DEPARSE is its
                // own text, so the view carries the pattern rather than a regex
                // tree. Needle::Compile asks for exactly this and no more —
                // `"/$spec/".AST.statements.head.expression` is a QuotedRegex it
                // wraps and evaluates, never one it walks into.
                auto* r = static_cast<RegexLit*>(e);
                return node("QuotedRegex", {{"source-slice", Value::str("/" + r->pattern + "/")}});
            }
            case NK::BlockExpr: {
                auto* b = static_cast<BlockExpr*>(e);
                return blockLike(b->isSub, b->params, b->body, b->isPointy);
            }
            case NK::AllomorphLit: {
                auto* a = static_cast<AllomorphLit*>(e);
                return node("Literal", {{"value", Value::str(a->str)}});
            }
            case NK::SymbolicRef: {
                // `::($name)` — a Name whose part is an EXPRESSION rather than
                // a word. Measured: upstream it is `Term::Name` over a `Name`
                // whose parts are `Name::Part::Empty` then
                // `Name::Part::Expression`, the empty one standing for the
                // nothing before the leading `::`.
                // LaTeX::Grammar's RakuAST actions build every symbol that way
                // (`'::("' ~ $name ~ '")'`) and then `.AST` the result.
                auto* r = static_cast<SymbolicRef*>(e);
                if (!r->nameExpr || !r->segs.empty() || !r->pkg.empty() || !r->sigil.empty())
                    unmapped("a qualified or sigilled symbolic reference");
                ValueList parts;
                parts.push_back(node("Name::Part::Empty"));
                parts.push_back(node("Name::Part::Expression",
                                     {{"expr", buildExpr(r->nameExpr.get())}}));
                return node("Term::Name", {{"name", node("Name", {{"parts", list(parts)}})}});
            }
            case NK::SubstLit:    unmapped("a substitution (s///)");
            case NK::NqpOp:       unmapped("an nqp:: op");
            default: break;
        }
        unmapped("this expression");
    }

    // `my $x = 1` written as an EXPRESSION (our parser's usual shape for a
    // declaration with an initializer).
    Value declaration(VarExpr* v) {
        // A SIGILLESS binding — `my \NULL = …` — has no sigil to split off, and
        // splitting one off anyway dropped the `\`: `my NULL = 1` is a different
        // declaration and does not parse. Rakudo keeps the backslash as the
        // sigil, so the round trip does too.
        char first = v->name.empty() ? '$' : v->name[0];
        bool sigilless = !(first == '$' || first == '@' || first == '%' || first == '&');
        std::string sigil = sigilless ? "\\" : std::string(1, first);
        std::string bare  = sigilless ? v->name
                          : (v->name.size() > 1 ? v->name.substr(1) : std::string());
        std::initializer_list<std::pair<const char*, Value>> attrs = {
            {"scope", Value::str(v->d().declScope.empty() ? "my" : v->d().declScope)},
            {"sigil", Value::str(sigil)},
            {"desigilname", name(bare)},
        };
        Value n = node("VarDeclaration::Simple", attrs);
        if (!v->d().declType.empty())
            n.obj()->attrs["type"] = node("Type::Simple", {{"name", name(v->d().declType)}});
        return n;
    }

    // `EXPR if COND` — a MODIFIER upstream, not the block form. Rakudo keeps
    // `Statement::Expression` and hangs a `condition-modifier` (if / unless /
    // with / without) or a `loop-modifier` (for / while / until / given) off
    // it; our parser desugars every one of them into the block form and sets
    // `modifier`, so the view can put the shape back. It matters beyond the
    // tally: `$total += $_ for 1..10` and `for 1..10 { $total += $_ }` are the
    // same program but not the same syntax, and a walker asking "does this
    // statement have a modifier" got No from a tree that had one.
    Expr* soleExpr(const std::vector<StmtPtr>& stmts) {
        if (stmts.size() == 1 && stmts[0] && stmts[0]->kind == NK::ExprStmt)
            return static_cast<ExprStmt*>(stmts[0].get())->e.get();
        return nullptr;
    }
    Value modifierStmt(const char* cls, bool loopSlot, Expr* cond, Expr* body) {
        Value st = node("Statement::Expression", {{"expression", buildExpr(body)}});
        st.obj()->attrs[loopSlot ? "loop-modifier" : "condition-modifier"] =
            node(cls, {{"expression", buildExpr(cond)}});
        return st;
    }

    // `is rw`, `is required("why")`: a Trait::Is, whose argument upstream is
    // the parenthesized list it was written as.
    Value traitIs(const std::string& nm, const Value* arg = nullptr) {
        Value t = node("Trait::Is", {{"name", name(nm)}});
        if (arg)
            t.obj()->attrs["argument"] =
                node("Circumfix::Parentheses", {{"semilist", semiList({*arg})}});
        return t;
    }

    // `is export`, and `is export(:a, :b)` with its tags, each a colonpair
    Value exportTrait(const std::vector<std::string>& tags) {
        if (tags.empty()) return traitIs("export");
        ValueList cps;
        for (auto& t : tags) cps.push_back(node("ColonPair::True", {{"key", Value::str(t)}}));
        Value arg = cps.size() == 1 ? cps[0]
                  : node("ApplyListInfix", {{"infix", node("Infix", {{"operator", Value::str(",")}})}, {"operands", list(cps)}});
        return traitIs("export", &arg);
    }

    // The `-> $y` of an `if`, an `elsif`, an `else`, a `with` or a `given`:
    // the parse keeps one plain name, or the whole parameter list when there
    // is more to it than a name. Upstream the block it introduces IS a
    // PointyBlock, and building a plain Block dropped the binder: `if $x ->
    // $y { say $y }` came back as `if $x { say $y }`, a program whose `$y` is
    // undeclared.
    Value binderBlock(const std::string& var, const std::vector<Param>& params,
                      const std::vector<StmtPtr>& body) {
        if (!params.empty()) return blockLike(false, params, body);
        if (var.empty()) return node("Block", {{"body", blockoid(body)}});
        ValueList ps;
        ps.push_back(node("Parameter",
            {{"target", node("ParameterTarget::Var", {{"name", Value::str(var)}})}}));
        return node("PointyBlock",
                    {{"signature", node("Signature", {{"parameters", list(ps)}})},
                     {"body", blockoid(body)}});
    }

    // A block or a pointy block, with its signature when it has one (and an
    // arrow with none, `-> { }`, is a PointyBlock still).
    Value blockLike(bool isSub, const std::vector<Param>& params, const std::vector<StmtPtr>& body, bool pointy = false) {
        Value bd = blockoid(body);
        if (params.empty() && !(pointy && !isSub))
            return node(isSub ? "Sub" : "Block", {{"body", bd}});
        ValueList ps;
        for (auto& p : params) ps.push_back(parameter(p));
        Value sig = node("Signature", {{"parameters", list(ps)}});
        return node(isSub ? "Sub" : "PointyBlock", {{"signature", sig}, {"body", bd}});
    }

    // `sub` / `method` / `submethod` — one shape, three class names, told
    // apart by the declarator the parser saw. A private method keeps its `!`
    // in the name, which is where Rakudo carries it too.
    Value routine(SubDecl* d) {
        ValueList ps;
        for (auto& p : d->params) ps.push_back(parameter(p));
        const char* cls = d->isSubmethod ? "Submethod" : d->isMethod ? "Method" : "Sub";
        Value n = node(cls, {{"name", name((d->isPrivate ? "!" : "") + d->name)},
                             {"multiness", Value::str(d->isMulti ? "multi" : "")},
                             {"signature", node("Signature", {{"parameters", list(ps)}})},
                             {"body", blockoid(d->body)}});
        // `our sub f` is visible from the package, `sub f` is not, so the
        // scope is part of what the declaration says; a sub's default is `my`.
        if (!d->isMethod && !d->isSubmethod)
            n.obj()->attrs["scope"] = Value::str(d->isOur ? "our" : "my");
        // …and `is export` and `is DEPRECATED`, which the parse keeps as flags
        ValueList traits;
        if (d->isExport) traits.push_back(exportTrait(d->exportTags));
        if (d->deprecated) {
            Value why = d->deprecatedWith ? buildExpr(d->deprecatedWith.get()) : Value();
            traits.push_back(traitIs("DEPRECATED", d->deprecatedWith ? &why : nullptr));
        }
        if (!traits.empty()) n.obj()->attrs["traits"] = list(traits);
        return n;
    }

    // `has $.x` — an attribute is a `VarDeclaration::Simple` with scope `has`
    // upstream, the public/private distinction carried in the TWIGIL rather
    // than in a flag.
    Value attribute(const AttrDecl& a) {
        Value n = node("VarDeclaration::Simple",
            {{"scope", Value::str("has")},
             {"sigil", Value::str(std::string(1, a.sigil))},
             {"twigil", Value::str(a.pub ? "." : "!")},
             {"desigilname", name(a.name)}});
        if (!a.type.empty())
            n.obj()->attrs["type"] = node("Type::Simple", {{"name", name(a.type)}});
        // The traits the parse keeps as flags, each a Trait::Is upstream.
        // `has $.x is rw` came back as `has $.x`, whose accessor no longer
        // assigns. (The order is fixed, not the source's: the flags do not
        // record it, and a different order is the same attribute.)
        ValueList traits;
        if (a.rw)       traits.push_back(traitIs("rw"));
        if (a.readonly) traits.push_back(traitIs("readonly"));
        if (a.required) {
            Value why = node("QuotedString", {{"segments", list({
                node("StrLiteral", {{"value", Value::str(a.requiredWhy)}})})}});
            traits.push_back(traitIs("required", a.requiredWhy.empty() ? nullptr : &why));
        }
        if (a.built) traits.push_back(traitIs("built"));
        if (a.notBuilt) {
            Value no = node("Term::Enum", {{"name", name("False")}});
            traits.push_back(traitIs("built", &no));
        }
        if (a.defaultTrait) { Value dv = buildExpr(a.defaultTrait.get()); traits.push_back(traitIs("default", &dv)); }
        // `handles <a b>`, the word list it was written as (a renaming pair has no words to give)
        if (!a.handles.empty() && std::all_of(a.handlesTo.begin(), a.handlesTo.end(), [](const std::string& t) { return t.empty(); })) {
            std::string words;
            for (auto& h : a.handles) words += (words.empty() ? "" : " ") + h;
            traits.push_back(node("Trait::Handles", {{"term", node("QuotedString",
                {{"segments", list({node("StrLiteral", {{"value", Value::str(words)}})})},
                 {"processors", list({Value::str("words"), Value::str("val")})}})}}));
        }
        if (!traits.empty()) n.obj()->attrs["traits"] = list(traits);
        if (a.def)
            n.obj()->attrs["initializer"] =
                node("Initializer::Assign", {{"expression", buildExpr(a.def.get())}});
        return n;
    }

    // A `SemiList`'s elements are STATEMENTS upstream, not bare expressions —
    // it is a `;`-separated statement list that happens to sit inside `[ ]`,
    // `( )` or a subscript. Putting the expression straight in dropped a
    // `Statement::Expression` per composer, which `visit-children` then made
    // visible as a missing level of nesting.
    // `( … )` around one expression
    Value parenthesized(Value v) {
        return node("Circumfix::Parentheses", {{"semilist", semiList({v})}});
    }

    Value semiList(ValueList exprs) {
        ValueList sts;
        for (auto& e : exprs)
            sts.push_back(node("Statement::Expression", {{"expression", e}}));
        return node("SemiList", {{"statements", list(sts)}});
    }

    // ---- the `Doc::` view (RAKUAST-PLAN P5) ------------------------------
    //
    // Pod is the one part of the tree we do NOT have to parse twice: rakupp
    // already builds a structured DOM for `$=pod`, and for a document it
    // answers exactly what Rakudo's does — `Pod::Block::Named`, then
    // `Pod::Block::Named, Pod::Heading, Pod::Block::Para, Pod::Item, Pod::Item`
    // — so this is a view over that, the same way the rest of the file is a
    // view over the parse. The mapping is measured against `.AST.rakudoc` on
    // 2026.08 and recorded in the plan.
    //
    // The shapes differ in one way worth naming: a heading, a title or an item
    // holds its text DIRECTLY upstream (`paragraphs` is a list of Str), while
    // our Pod wraps it in a `Pod::Block::Para`. So those three flatten, and
    // only a standalone paragraph becomes a `Doc::Paragraph`.
    const Value* podKey(const Value& p, const char* k) {
        if (p.t != VT::Hash || !p.hash()) return nullptr;
        auto it = p.hash()->find(k);
        return it == p.hash()->end() ? nullptr : &it->second;
    }
    std::string podClass(const Value& p) {
        const Value* c = podKey(p, "podclass");
        return c ? c->toStr() : std::string();
    }
    // The text of a pod subtree, with the formatting codes kept as Markup —
    // i.e. the `atoms` of a paragraph.
    ValueList docAtoms(const Value& pod) {
        ValueList out;
        const Value* c = podKey(pod, "contents");
        if (!c || c->t != VT::Array || !c->arr()) return out;
        for (auto& e : *c->arr()) {
            if (e.t == VT::Str) { out.push_back(e); continue; }
            const std::string cls = podClass(e);
            if (cls == "Pod::FormattingCode") {
                const Value* ty = podKey(e, "type");
                out.push_back(node("Doc::Markup",
                    {{"letter", Value::str(ty ? ty->toStr() : std::string())},
                     {"atoms",  list(docAtoms(e))},
                     {"meta",   Value::array()}}));
            }
            // A nested block inside a paragraph flattens to its own atoms:
            // upstream a paragraph's atoms are Str or Markup and nothing else.
            else for (auto& a : docAtoms(e)) out.push_back(a);
        }
        return out;
    }
    // …and the same subtree as the `paragraphs` of a block: a `Pod::Block::Para`
    // that is the SOLE content of a heading/title/item is its text, not a
    // paragraph of its own.
    ValueList docParagraphs(const Value& pod, bool flatten) {
        ValueList out;
        const Value* c = podKey(pod, "contents");
        if (!c || c->t != VT::Array || !c->arr()) return out;
        for (auto& e : *c->arr()) {
            if (e.t == VT::Str) { out.push_back(e); continue; }
            const std::string cls = podClass(e);
            if (cls == "Pod::Block::Para") {
                if (flatten) { for (auto& a : docAtoms(e)) out.push_back(a); continue; }
                out.push_back(node("Doc::Paragraph", {{"atoms", list(docAtoms(e))}}));
                continue;
            }
            if (Value b = docBlock(e); b.t == VT::Object) out.push_back(b);
        }
        return out;
    }
    // One pod block as a `Doc::Block`. An empty Value for anything with no
    // block shape (a bare formatting code cannot stand here).
    Value docBlock(const Value& pod) {
        const std::string cls = podClass(pod);
        std::string type;
        long long level = 0;
        bool flatten = false;
        if (cls == "Pod::Block::Named") {
            const Value* n = podKey(pod, "name");
            type = n ? n->toStr() : "pod";
            // `=TITLE`, `=SUBTITLE` and friends hold their text directly.
            flatten = !type.empty() && ascii::isupper((unsigned char)type[0]);
        }
        else if (cls == "Pod::Heading")      { type = "head"; flatten = true; }
        else if (cls == "Pod::Item")         { type = "item"; flatten = true; }
        else if (cls == "Pod::Block::Code")  { type = "code"; flatten = true; }
        else if (cls == "Pod::Block::Comment") { type = "comment"; flatten = true; }
        else if (cls == "Pod::Block::Declarator") { type = "declarator"; flatten = true; }
        else if (cls == "Pod::Block::Para")  { type = "para"; flatten = true; }
        else return Value();
        if (const Value* lv = podKey(pod, "level")) level = lv->toInt();
        // `level` is a STR upstream, not an Int, and it is EMPTY for everything
        // that is not a heading — `=item` included, which our pod DOM does give
        // a level. Measured: `.level.raku` answers `""` for a `rakudoc`, a
        // `TITLE` and an `item`, and `1` for `=head1`.
        std::string lvl = type == "head" ? std::to_string(level) : std::string();
        // `config` and `resolved-config` are Maps upstream and a renderer reads
        // both before it reads anything else — RakuDoc::Render asks
        // `$ast.config` on the very first block it handles. Our pod DOM does
        // not carry block adverbs yet, so they are EMPTY rather than absent:
        // an absent one is a missing method, which is a different failure from
        // a block that simply configures nothing.
        Value cfg = Value::makeHash(); cfg.hashKind = "Map";
        Value rcfg = Value::makeHash(); rcfg.hashKind = "Map";
        return node("Doc::Block", {{"margin",          Value::str("")},
                                   {"type",            Value::str(type)},
                                   {"level",           Value::str(lvl)},
                                   {"config",          cfg},
                                   {"resolved-config", rcfg},
                                   {"paragraphs",      list(docParagraphs(pod, flatten))}});
    }

    // `last` / `next` / `redo`, with an optional label.
    Value loopControl(const char* kw, const std::string& target) {
        ValueList args;
        if (!target.empty()) args.push_back(node("Term::Name", {{"name", name(target)}}));
        return node("Statement::Expression", {{"expression",
            node("Call::Name::WithoutParentheses",
                 {{"name", name(kw)}, {"args", node("ArgList", {{"args", list(args)}})}})}});
    }

    Value parameter(const Param& p) {
        Value n = node("Parameter");
        if (!p.name.empty())
            n.obj()->attrs["target"] = node("ParameterTarget::Var", {{"name", Value::str(p.name)}});
        // `-> ($a, $b)` / `@y ($head, *@tail)`: the DESTRUCTURING binder is a
        // sub-signature on the parameter upstream. Without it `for @x -> ($a,
        // $b) { say $a }` came back as `for @x { say $a }`, which parses and
        // has no `$a` — the round-trip harness counted that file as fine.
        if (p.subSig) {
            if (!p.subSigRet.empty())
                unmapped("a sub-signature with a return type (`-->`)");
            ValueList sps;
            for (auto& q : *p.subSig) sps.push_back(parameter(q));
            n.obj()->attrs["sub-signature"] = node("Signature", {{"parameters", list(sps)}});
        }
        if (!p.type.empty())
            n.obj()->attrs["type"] = node("Type::Simple", {{"name", name(p.type)}});
        // The slurpy marker is a TYPE OBJECT on the parameter upstream, not a
        // node. Losing it does not merely read wrong — `multi f($p, *@rest)`
        // rendered `($p, @rest)`, a different signature.
        if (p.slurpy) {
            const char* sl = p.slurpyKind == 'n' ? "Parameter::Slurpy::Unflattened"
                           : p.slurpyKind == '1' ? "Parameter::Slurpy::SingleArgument"
                                                  : "Parameter::Slurpy::Flattened";
            n.obj()->attrs["slurpy"] = Value::typeObj(std::string(kRakuAstPrefix) + sl);
        }
        // A NAMED parameter is `:$x`, and losing the colon does not merely read
        // wrong — it moves the parameter into the positional list, so a
        // signature like `(*@a, :$solar)` came back as `(*@a, $solar)` and the
        // parser refused it outright ("required parameter after variadic").
        if (p.named) {
            ValueList keys;
            keys.push_back(Value::str(p.namedKey.empty()
                ? (p.name.size() > 1 ? p.name.substr(1) : p.name) : p.namedKey));
            n.obj()->attrs["names"] = list(keys);
        }
        if (p.optional) n.obj()->attrs["optional"] = Value::boolean(true);
        if (p.required) n.obj()->attrs["required"] = Value::boolean(true);
        // `$a is rw` writes back to the caller and `$a is copy` may be
        // assigned to; without the trait `sub f($a is rw) { $a++ }` came back
        // as a sub whose body cannot run. A `<->` block sets the flag on every
        // parameter and records nothing else, so it renders `-> $x is rw`:
        // the same block, where Rakudo writes `<-> $x`.
        ValueList traits;
        if (p.isRw)   traits.push_back(traitIs("rw"));
        if (p.isCopy) traits.push_back(traitIs("copy"));
        if (p.isRaw)  traits.push_back(traitIs("raw"));
        if (!traits.empty()) n.obj()->attrs["traits"] = list(traits);
        if (p.defaultVal) n.obj()->attrs["default"] = buildExpr(p.defaultVal.get());
        return n;
    }

    // ---- grammar rules: the `Regex::` subtree ------------------------------
    // Our parser keeps a rule's body as TEXT, so the view parses that text
    // here, at the surface, because Rakudo's tree records what was written: a
    // quote is a Regex::Quote and a bare word a Regex::Literal, `\d` is a
    // CharClass::Digit, and whitespace after an atom is a Regex::WithWhitespace
    // around it, while leading whitespace and a leading `|` or `||` leave
    // nothing. The matcher's own tree has normalized all of that away. Each
    // shape was read off 2026.09's `.AST`; anything else refuses by name, as a
    // partial tree would be a wrong one (rakudo#5479, #5978, #5991).
    Value fragment(const std::string& code, bool block) {
        Program p;
        try {
            Lexer lexer(code);
            Parser parser(lexer.tokenize());
            p = parser.parseProgram();
        } catch (ParseError&) { unmapped("a grammar rule whose code does not parse alone"); }
        if (block) return node("Block", {{"body", blockoid(p.stmts)}});
        if (p.stmts.size() != 1) unmapped("a grammar rule with a `:my` of this form");
        return buildStmt(p.stmts[0].get());
    }
    struct RxView {
        Builder& B;
        const std::string& s;
        size_t i = 0;
        int parens = 0;
        bool tookWs = false;   // the atom just built took the whitespace after it
        [[noreturn]] void no(const std::string& what) {
            const std::string m = "a grammar rule with " + what;
            B.unmapped(m.c_str());
        }
        bool at(const char* t) const { return s.compare(i, std::strlen(t), t) == 0; }
        bool word(size_t k) const { return k < s.size() && (ascii::isalnum((unsigned char)s[k]) || s[k] == '_'); }
        Value with(Value v, const char* k, Value x) { v.obj()->attrs[k] = std::move(x); return v; }
        Value wrap(Value v) { return B.node("Regex::WithWhitespace", {{"regex", std::move(v)}}); }
        bool ws() {   // whitespace and `#` comments, which the tree treats alike
            const size_t from = i;
            while (i < s.size()) {
                if (ascii::isspace((unsigned char)s[i])) i++;
                else if (s[i] == '#') { while (i < s.size() && s[i] != '\n') i++; }
                else break;
            }
            return i > from;
        }
        void leading() { ws(); if (at("||")) i += 2; else if (at("|")) i++; ws(); }
        Value top() {
            leading();
            Value r = branches(0);
            if (i < s.size()) no(std::string("the regex syntax `") + s[i] + "`");
            return r;
        }
        // `||` loosest, then `&&`, `|`, `&`
        Value branches(int level) {
            static const char* const ops[] = {"||", "&&", "|", "&"};
            static const char* const cls[] = {"Regex::SequentialAlternation", "Regex::SequentialConjunction",
                                              "Regex::Alternation", "Regex::Conjunction"};
            if (level == 4) return sequence();
            auto atOp = [&] { return at(ops[level]) && (level < 2 || !at(level == 2 ? "||" : "&&")); };
            ValueList br{branches(level + 1)};
            while (atOp()) { i += std::strlen(ops[level]); ws(); br.push_back(branches(level + 1)); }
            return br.size() == 1 ? br[0] : B.node(cls[level], {{"branches", B.list(br)}});
        }
        bool ends() const {
            if (i >= s.size()) return true;
            const char c = s[i];
            if (c == ']' || c == '|' || c == '&') return true;
            if (c == ')') return parens > 0 || !at(")>");
            return c == '>' && !at(">>");
        }
        Value sequence() {
            ValueList terms;
            while (!ends()) {
                tookWs = false;
                Value t = quantified();
                if (!tookWs && ws()) t = wrap(t);
                terms.push_back(t);
            }
            if (terms.empty()) no("an empty regex");
            return terms.size() == 1 ? terms[0] : B.node("Regex::Sequence", {{"terms", B.list(terms)}});
        }
        Value quantified() {
            Value a = atom();
            if (tookWs) return a;
            const size_t pre = i;
            if (ws() && at("**")) a = wrap(a);   // `x ** 2`: the space is the atom's
            else i = pre;
            Value q;
            if (!quantifier(q)) { i = pre; return a; }
            Value qa = B.node("Regex::QuantifiedAtom", {{"atom", a}, {"quantifier", q}});
            const size_t back = i;
            const bool w1 = ws();
            if (!at("%")) { i = back; return qa; }
            const bool trail = at("%%");
            i += trail ? 2 : 1;
            ws();
            Value sep = atom();
            if (tookWs) no("a separator of this form");
            if (ws()) sep = wrap(sep);
            qa = with(qa, "separator", sep);
            if (trail) qa = with(qa, "trailing-separator", Value::boolean(true));
            tookWs = true;
            return w1 ? wrap(qa) : qa;
        }
        long long number() {
            const size_t a = i;
            while (i < s.size() && ascii::isdigit((unsigned char)s[i])) i++;
            if (a == i || i - a > 18) no("a `**` quantifier of this form");
            return std::stoll(s.substr(a, i - a));
        }
        bool quantifier(Value& q) {
            if (at("**")) {
                i += 2; ws();
                const long long mn = number();
                q = B.node("Regex::Quantifier::Range", {{"min", Value::integer(mn)}});
                if (!at("..")) return q = with(q, "max", Value::integer(mn)), true;
                i += 2;
                if (at("*")) { i++; return true; }
                q = with(q, "max", Value::integer(number()));
                return true;
            }
            const char* cls = at("?") ? "Regex::Quantifier::ZeroOrOne" : at("*") ? "Regex::Quantifier::ZeroOrMore"
                            : at("+") ? "Regex::Quantifier::OneOrMore" : nullptr;
            if (!cls) return false;
            i++;
            q = B.node(cls);
            const bool colon = at(":") && !(i + 1 < s.size() && (ascii::isalpha((unsigned char)s[i + 1]) || s[i + 1] == '!'));
            const char* bt = at("?") ? "Frugal" : at("!") ? "Greedy" : colon ? "Ratchet" : nullptr;
            if (bt) { i++; q = with(q, "backtrack", Value::typeObj(std::string("RakuAST::Regex::Backtrack::") + bt)); }
            return true;
        }
        Value atom() {
            const char c = s[i];
            if (word(i)) {
                const size_t a = i;
                while (word(i)) i++;
                if (i - a > 1 && (at("?") || at("*") || at("+"))) i--;   // the quantifier takes the last letter
                return B.node("Regex::Literal", {{"text", Value::str(s.substr(a, i - a))}});
            }
            if (c == '\'' || c == '"') return quote();
            if (c == '.') { i++; return B.node("Regex::CharClass::Any"); }
            if (c == '\\') return escape();
            if (at("^^")) { i += 2; return B.node("Regex::Anchor::BeginningOfLine"); }
            if (c == '^') { i++; return B.node("Regex::Anchor::BeginningOfString"); }
            if (at("$$")) { i += 2; return B.node("Regex::Anchor::EndOfLine"); }
            if (at("$<") || (c == '$' && i + 1 < s.size() && ascii::isdigit((unsigned char)s[i + 1]))) return capture();
            if (c == '$') {
                if (word(i + 1) || (i + 1 < s.size() && std::strchr("!*?.", s[i + 1]))) no("a variable");
                i++;
                return B.node("Regex::Anchor::EndOfString");
            }
            if (at("<<") || at("\xC2\xAB")) { i += 2; return B.node("Regex::Anchor::LeftWordBoundary"); }
            if (at(">>") || at("\xC2\xBB")) { i += 2; return B.node("Regex::Anchor::RightWordBoundary"); }
            if (c == '[' || c == '(') {
                i++;
                if (c == '(') parens++;
                leading();
                Value r = branches(0);
                if (c == '(') parens--;
                if (!at(c == '[' ? "]" : ")")) no(std::string("an unclosed `") + c + "`");
                i++;
                return B.node(c == '[' ? "Regex::Group" : "Regex::CapturingGroup", {{"regex", r}});
            }
            if (at(")>")) { i += 2; return B.node("Regex::MatchTo"); }
            if (c == '<') return assertion();
            if (c == ':') return modifier();
            if (c == '{') {
                const size_t a = i;
                int depth = 0;
                for (; i < s.size(); i++) {
                    if (s[i] == '{') depth++;
                    else if (s[i] == '}' && --depth == 0) break;
                    else if (s[i] == '\'' || s[i] == '"') no("a code block with a quote in it");
                }
                if (i >= s.size()) no("an unclosed `{`");
                i++;
                return B.node("Regex::Block", {{"block", B.fragment(s.substr(a + 1, i - a - 2), true)}});
            }
            no(std::string("the regex syntax `") + c + "`");
        }
        Value quote() {
            const char q = s[i++];
            std::string text;
            for (; i < s.size() && s[i] != q; i++) {
                if (s[i] == '\\') {
                    if (q == '\'' && i + 1 < s.size() && (s[i + 1] == '\\' || s[i + 1] == '\'')) { text += s[++i]; continue; }
                    no("an escape in a quote");
                }
                if (q == '"' && std::strchr("$@{&", s[i])) no("an interpolating quote");
                text += s[i];
            }
            if (i >= s.size()) no("an unclosed quote");
            i++;
            return B.node("Regex::Quote", {{"quoted", B.node("QuotedString", {{"segments",
                B.list({B.node("StrLiteral", {{"value", Value::str(text)}})})}})}});
        }
        Value escape() {
            const char e = i + 1 < s.size() ? s[i + 1] : '\0';
            static const struct { char lc; const char* cls; } kinds[] = {
                {'d', "Digit"}, {'w', "Word"}, {'s', "Space"}, {'n', "Newline"}, {'t', "Tab"},
                {'h', "HorizontalSpace"}, {'v', "VerticalSpace"}, {'e', "Escape"}, {'f', "FormFeed"},
                {'r', "CarriageReturn"}};
            for (auto& k : kinds)
                if (ascii::tolower((unsigned char)e) == k.lc) {
                    i += 2;
                    Value n = B.node((std::string("Regex::CharClass::") + k.cls).c_str());
                    return ascii::isupper((unsigned char)e) ? with(n, "negated", Value::boolean(true)) : n;
                }
            if (e && !ascii::isalnum((unsigned char)e) && !ascii::isspace((unsigned char)e) && (unsigned char)e < 0x80) {
                i += 2;
                return B.node("Regex::Literal", {{"text", Value::str(std::string(1, e))}});
            }
            no(std::string("the escape `\\") + e + "`");
        }
        Value capture() {   // `$<name>=…` and `$0=…`
            std::string nm;
            if (at("$<")) {
                const size_t e = s.find('>', i);
                if (e == std::string::npos) no("an unclosed `$<`");
                nm = s.substr(i + 2, e - i - 2);
                i = e + 1;
                for (char ch : nm) if (!ascii::isalnum((unsigned char)ch) && ch != '_' && ch != '-') no("a capture name of this form");
            } else {
                i++;
                while (i < s.size() && ascii::isdigit((unsigned char)s[i])) nm += s[i++];
            }
            if (!at("=")) no("a backreference");
            i++;
            Value r = quantified();
            if (tookWs) no("a capture of this form");
            return B.node("Regex::NamedCapture", {{"name", Value::str(nm)}, {"regex", r}});
        }
        std::string ident(size_t& k) const {
            const size_t a = k;
            if (k < s.size() && (ascii::isalpha((unsigned char)s[k]) || s[k] == '_')) {
                while (k < s.size()) {
                    if (word(k)) k++;
                    else if ((s[k] == '-' || s[k] == '\'') && k + 1 < s.size() && ascii::isalpha((unsigned char)s[k + 1])) k++;
                    else if (s.compare(k, 2, "::") == 0 && k + 2 < s.size() && ascii::isalpha((unsigned char)s[k + 2])) k += 2;
                    else break;
                }
            }
            return s.substr(a, k - a);
        }
        Value assertion() {
            if (at("<(")) { i += 2; return B.node("Regex::MatchFrom"); }
            if (at("<?>")) { i += 3; return B.node("Regex::Assertion::Pass"); }
            if (at("<!>")) { i += 3; return B.node("Regex::Assertion::Fail"); }
            if (at("<[") || at("<-[") || at("<+[")) return charClass();
            size_t k = i + 1;
            const bool look = k < s.size() && (s[k] == '?' || s[k] == '!');
            const bool neg = look && s[k] == '!';
            if (look) k++;
            const bool dot = !look && k < s.size() && s[k] == '.';
            if (dot) k++;
            const std::string nm = ident(k);
            if (nm.empty() || nm == "sym") no("the assertion `" + s.substr(i, std::min<size_t>(12, s.size() - i)) + "`");
            if (look) {
                if ((nm != "before" && nm != "after") || k >= s.size() || !ascii::isspace((unsigned char)s[k]))
                    no("a `<?…>` or `<!…>` assertion of this form");
                i = k;
                leading();
                const int savedParens = parens;
                parens = 0;
                Value inner = branches(0);
                parens = savedParens;
                if (!at(">")) no("an unclosed assertion");
                i++;
                Value arg = B.node("Regex::Assertion::Named::RegexArg", {{"name", B.name(nm)}, {"regex-arg", inner}});
                Value la = B.node("Regex::Assertion::Lookahead", {{"assertion", arg}});
                return neg ? with(la, "negated", Value::boolean(true)) : la;
            }
            if (k >= s.size() || s[k] != '>') no("an assertion with arguments or an alias");
            i = k + 1;
            Value n = B.node("Regex::Assertion::Named", {{"name", B.name(nm)}});
            return dot ? n : with(n, "capturing", Value::boolean(true));
        }
        uint32_t classChar() {
            if (s[i] == '\\') {
                const char e = i + 1 < s.size() ? s[i + 1] : '\0';
                if (!e || ascii::isalnum((unsigned char)e) || (unsigned char)e >= 0x80) no("an escape in a character class");
                i += 2;
                return (unsigned char)e;
            }
            const unsigned char c0 = (unsigned char)s[i];
            if (c0 < 0x80) { i++; return c0; }
            const int n = c0 >= 0xF0 ? 4 : c0 >= 0xE0 ? 3 : 2;
            uint32_t cp = c0 & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
            for (int k = 1; k < n; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
            i += n;
            return cp;
        }
        Value charClass() {
            i++;
            const bool neg = s[i] == '-';
            if (s[i] == '-' || s[i] == '+') i++;
            i++;   // `[`
            ValueList els;
            auto spaces = [&] { while (i < s.size() && ascii::isspace((unsigned char)s[i])) i++; };
            for (;;) {
                spaces();
                if (i >= s.size()) no("an unclosed character class");
                if (s[i] == ']') { i++; break; }
                const size_t from = i;
                const uint32_t a = classChar();
                const size_t after = i;
                spaces();
                if (at("..")) {
                    i += 2;
                    spaces();
                    const uint32_t b = classChar();
                    els.push_back(B.node("Regex::CharClassEnumerationElement::Range",
                        {{"from", Value::integer(a)}, {"to", Value::integer(b)}}));
                } else {
                    i = after;
                    els.push_back(B.node("Regex::CharClassEnumerationElement::Character",
                        {{"character", Value::str(s.substr(from, after - from).front() == '\\'
                                                      ? s.substr(from + 1, after - from - 1)
                                                      : s.substr(from, after - from))}}));
                }
            }
            spaces();
            if (!at(">")) no("a combined character class");
            i++;
            Value en = B.node("Regex::CharClassElement::Enumeration", {{"elements", B.list(els)}});
            if (neg) en = with(en, "negated", Value::boolean(true));
            return B.node("Regex::Assertion::CharClass", {{"elements", B.list({en})}});
        }
        Value modifier() {
            if (at(":my ") || at(":our ") || at(":state ")) {
                size_t e = s.find(';', i);
                if (e == std::string::npos || s.find_first_of("'\"{", i) < e) no("a `:my` of this form");
                Value st = B.fragment(s.substr(i + 1, e - i), false);
                i = e + 1;
                return B.node("Regex::Statement", {{"statement", st}});
            }
            size_t k = i + 1;
            const bool neg = k < s.size() && s[k] == '!';
            if (neg) k++;
            const size_t a = k;
            while (k < s.size() && ascii::isalpha((unsigned char)s[k])) k++;
            const std::string m = s.substr(a, k - a);
            static const struct { const char* sp; const char* cls; } mods[] = {
                {"i", "IgnoreCase"}, {"ignorecase", "IgnoreCase"}, {"m", "IgnoreMark"}, {"ignoremark", "IgnoreMark"},
                {"r", "Ratchet"}, {"ratchet", "Ratchet"}, {"s", "Sigspace"}, {"sigspace", "Sigspace"}};
            for (auto& md : mods)
                if (m == md.sp && !(k < s.size() && (word(k) || s[k] == '('))) {
                    i = k;
                    ws();
                    tookWs = true;
                    Value n = B.node((std::string("Regex::InternalModifier::") + md.cls).c_str());
                    if (m.size() > 1) n = with(n, "modifier", Value::str(m));
                    return neg ? with(n, "negated", Value::boolean(true)) : n;
                }
            no("the modifier `:" + m + "`");
        }
    };
    // A rule's `#|` and `#=` docs, as the WHY Rakudo attaches to it
    // (`.declarator-docs(leading => …, trailing => …)`). The pod splice below
    // then leaves out the declarator block it would otherwise append.
    std::set<std::string> attachedDocs;
    Value ruleDecl(const GrammarRuleDecl& r) {
        const char* rcls = r.kind == "token" ? "TokenDeclaration" : r.kind == "regex" ? "RegexDeclaration"
                                                                                        : "RuleDeclaration";
        RxView rx{*this, r.pattern};
        Value n = node(rcls, {{"name", name(r.name)}, {"body", rx.top()}});
        if (!r.pod.empty()) {
            const std::string lead = r.podTrail.empty() ? r.pod
                                   : r.pod == r.podTrail ? std::string()
                                   : r.pod.substr(0, r.pod.size() - r.podTrail.size() - 1);
            auto one = [](const std::string& t) { return t.empty() ? ValueList{} : ValueList{Value::str(t)}; };
            n.obj()->attrs["WHY"] = node("Doc::Declarator", {{"leading", list(one(lead))},
                                                             {"trailing", list(one(r.podTrail))}});
            attachedDocs.insert(r.pod);
        }
        return n;
    }

    // ---- statements ------------------------------------------------------
    Value buildStmt(Stmt* s) {
        if (!s) return Value::any();
        switch (s->kind) {
            case NK::ExprStmt: {
                Expr* e = static_cast<ExprStmt*>(s)->e.get();
                // `whenever $chan -> $v { … }` is a STATEMENT upstream, and a
                // two-argument call here (the trigger and a block). Rendering
                // it as the call put a COMMA between them — `whenever $chan,
                // -> $v { … }` — which is valid Raku that hands `react` two
                // arguments instead of a handler, so the block never ran. The
                // round trip cannot see that; L10N::ZH's concurrency test can,
                // and did.
                if (e && e->kind == NK::Call) {
                    auto* c = static_cast<Call*>(e);
                    if (c->name == "whenever" && c->args.size() == 2 &&
                        c->args[1] && c->args[1]->kind == NK::BlockExpr)
                        return node("Statement::Whenever",
                            {{"trigger", buildExpr(c->args[0].get())},
                             {"body",    buildExpr(c->args[1].get())}});
                }
                return node("Statement::Expression", {{"expression", buildExpr(e)}});
            }
            case NK::EmptyStmt: return node("Statement::Empty");
            case NK::VarDecl: {
                auto* d = static_cast<VarDecl*>(s);
                if (d->names.size() != 1) unmapped("a multi-name declaration (`my ($a, $b)`)");
                const std::string& nm = d->names[0];
                std::string sigil = nm.empty() ? "$" : nm.substr(0, 1);
                Value decl = node("VarDeclaration::Simple",
                    {{"scope", Value::str(d->scope.empty() ? "my" : d->scope)},
                     {"sigil", Value::str(sigil)},
                     {"desigilname", name(nm.size() > 1 ? nm.substr(1) : std::string())}});
                if (d->init)
                    decl.obj()->attrs["initializer"] =
                        node(d->op == ":=" ? "Initializer::Bind" : "Initializer::Assign",
                             {{"expression", buildExpr(d->init.get())}});
                return node("Statement::Expression", {{"expression", decl}});
            }
            case NK::Block: {
                auto* b = static_cast<Block*>(s);
                Value blk = node("Block", {{"body", blockoid(b->stmts)}});
                // `BEGIN { … }` is a STATEMENT PREFIX upstream, one class per
                // phaser, named for the keyword title-cased — measured over all
                // seventeen. The block hangs off `blorst` (block-or-statement),
                // which is also the `PHASER statement;` form's slot.
                if (!b->phaser.empty()) {
                    std::string cls = "StatementPrefix::Phaser::";
                    cls += (char)ascii::toupper((unsigned char)b->phaser[0]);
                    for (size_t i = 1; i < b->phaser.size(); i++)
                        cls += (char)ascii::tolower((unsigned char)b->phaser[i]);
                    return node("Statement::Expression",
                                {{"expression", node(cls.c_str(), {{"blorst", blk}})}});
                }
                return node("Statement::Expression", {{"expression", blk}});
            }
            case NK::IfStmt: {
                auto* i = static_cast<IfStmt*>(s);
                if (i->branches.empty()) unmapped("an `if` with no branches");
                if (i->modifier && i->branches.size() == 1 && !i->elseBlock)
                    if (Expr* b = soleExpr(i->branches[0].second->stmts))
                        return modifierStmt(i->isUnless ? "StatementModifier::Unless"
                                                        : "StatementModifier::If",
                                            false, i->branches[0].first.get(), b);
                // branchVars / branchParams run beside branches, the first
                // entry being the `if`'s own `-> $y`
                auto then = [&](size_t k) {
                    static const std::vector<Param> none;
                    return binderBlock(k < i->branchVars.size() ? i->branchVars[k] : std::string(),
                                       k < i->branchParams.size() ? i->branchParams[k] : none,
                                       i->branches[k].second->stmts);
                };
                Value n = node(i->isUnless ? "Statement::Unless" : "Statement::If",
                    {{"condition", buildExpr(i->branches[0].first.get())},
                     {"then", then(0)}});
                if (i->branches.size() > 1) {
                    ValueList elsifs;
                    for (size_t k = 1; k < i->branches.size(); k++)
                        elsifs.push_back(node("Statement::Elsif",
                            {{"condition", buildExpr(i->branches[k].first.get())},
                             {"then", then(k)}}));
                    n.obj()->attrs["elsifs"] = list(elsifs);
                }
                if (i->elseBlock)
                    n.obj()->attrs["else"] = binderBlock(i->elseVar, i->elseParams, i->elseBlock->stmts);
                return n;
            }
            case NK::WhileStmt: {
                auto* w = static_cast<WhileStmt*>(s);
                if (w->modifier && w->body)
                    if (Expr* b = soleExpr(w->body->stmts))
                        return modifierStmt(w->isUntil ? "StatementModifier::Until"
                                                       : "StatementModifier::While",
                                            true, w->cond.get(), b);
                // `while`/`until` are `Statement::Loop::While` / `::Until`
                // upstream. We had invented `Statement::While` — a name Rakudo
                // has no class for, which the table generator then emitted an
                // ancestry-less row for instead of saying so, so the node could
                // not even reach `RakuAST::Node`'s methods.
                return node(w->isUntil ? "Statement::Loop::Until" : "Statement::Loop::While",
                    {{"condition", buildExpr(w->cond.get())},
                     {"body", node("Block", {{"body", w->body ? blockoid(w->body->stmts) : blockoid({})}})}});
            }
            case NK::ForStmt: {
                auto* f = static_cast<ForStmt*>(s);
                if (f->modifier && f->body)
                    if (Expr* b = soleExpr(f->body->stmts))
                        return modifierStmt("StatementModifier::For", true, f->list.get(), b);
                // The parse keeps plain names in `vars`, with an rw/raw flag
                // each, and moves ANY richer binder — a type, a default, a
                // sub-signature — whole into `params`. Reading only `vars`
                // made `for @a -> Int $x { … }` come back as `for @a { … }`,
                // whose `$x` is undeclared.
                ValueList ps;
                for (auto& p : f->params) ps.push_back(parameter(p));
                for (size_t k = 0; k < f->vars.size(); k++) {
                    Value pn = node("Parameter",
                        {{"target", node("ParameterTarget::Var", {{"name", Value::str(f->vars[k])}})}});
                    // VT_RAW is also set for a sigilless `\e`, which is raw by
                    // nature and says nothing, so only rw is written
                    if (k < f->varTraits.size() && (f->varTraits[k] & ForStmt::VT_RW))
                        pn.obj()->attrs["traits"] = list({traitIs("rw")});
                    ps.push_back(pn);
                }
                // A TOPIC-LESS `for` has a plain Block upstream, not a pointy
                // one with an empty signature — measured. Building the pointy
                // block anyway rendered `for 1, 2 -> { … }`, which takes no
                // parameter at all, so the body saw the OUTER `$_`. It parses,
                // so the round trip called it stable; it is a different program.
                // (…while `for 1, 2 -> { … }`, written with its arrow, is that pointy block)
                Value bd = f->body ? blockoid(f->body->stmts) : blockoid({});
                Value body = ps.empty() && !f->emptyPointy
                    ? node("Block", {{"body", bd}})
                    : node("PointyBlock",
                           {{"signature", node("Signature", {{"parameters", list(ps)}})},
                            {"body", bd}});
                return node("Statement::For", {{"source", buildExpr(f->list.get())}, {"body", body}});
            }
            case NK::ReturnStmt: {
                auto* r = static_cast<ReturnStmt*>(s);
                Value call = r->value
                    ? node("Call::Name::WithoutParentheses",
                           {{"name", name("return")},
                            {"args", node("ArgList", {{"args", list({buildExpr(r->value.get())})}})}})
                    : node("Call::Name::WithoutParentheses",
                           {{"name", name("return")}, {"args", node("ArgList", {{"args", list({})}})}});
                return node("Statement::Expression", {{"expression", call}});
            }
            case NK::SubDecl:
                return node("Statement::Expression",
                            {{"expression", routine(static_cast<SubDecl*>(s))}});
            case NK::UseStmt: {
                auto* u = static_cast<UseStmt*>(s);
                // `import`, `need` and `no` are their OWN statements upstream,
                // not a `use` with a flag — and the difference is not academic:
                // rendering `import Foo` as `use Foo` makes the round trip try
                // to LOAD a package that is declared in the same file, which is
                // what L10N::ZH's import test does and how this was found.
                if (u->isImport)
                    return node("Statement::Import", {{"module-name", name(u->module)}});
                if (u->isNeed)
                    return node("Statement::Need", {{"module-name", name(u->module)}});
                // The argument, where the parse keeps it whole: `use lib`'s
                // one plain path as text, and any expression list. Dropping it
                // made `use lib "x"` a `use lib` that adds no path. (Strings,
                // tags and angle words for any other module are kept as bare
                // words, `<a>` and `:a` alike, so they cannot be rendered.)
                Value arg;
                if (u->argExpr) arg = buildExpr(u->argExpr.get());
                else if (u->module == "lib" && !u->arg.empty())
                    arg = node("QuotedString", {{"segments", list({
                        node("StrLiteral", {{"value", Value::str(u->arg)}})})}});
                // A pragma is RakuAST::Pragma upstream, `no` and all; `no
                // strict` was built as `use strict`, the opposite pragma.
                if (isRakuAstPragmaName(u->module)) {
                    Value n = node("Pragma", {{"name", Value::str(u->module)},
                                              {"off",  Value::boolean(u->isNo)}});
                    if (arg.t == VT::Object) n.obj()->attrs["argument"] = arg;
                    return n;
                }
                // Rakudo builds no tree for `no` on a module either
                if (u->isNo) unmapped("`no` with a module that is not a pragma");
                Value n = node("Statement::Use", {{"module-name", name(u->module)}});
                if (arg.t == VT::Object) n.obj()->attrs["argument"] = arg;
                return n;
            }
            case NK::ClassDecl: {
                // `class` / `role` / `grammar` / `module` / `package` — four
                // classes upstream, told apart by the declarator the parser saw.
                // Twelve of the corpus's fifty-nine programs stop here, which is
                // why it is the first widening and not the tidiest.
                auto* cd = static_cast<ClassDecl*>(s);
                // `module` and `package` are DIFFERENT classes upstream and
                // answer different metaobjects, so collapsing both to `Module`
                // rendered `package P` as `module P` — and the round trip
                // through `.AST.EVAL`, which is how every L10N test runs, then
                // declared the wrong kind of thing.
                const char* cls = cd->isRole    ? "Role"
                                : cd->isGrammar ? "Grammar"
                                : cd->isPackage ? (cd->isModuleDecl ? "Module" : "Package")
                                                : "Class";
                // The body is NOT `cd->body`: our parser lifts attributes,
                // methods and grammar rules into their own vectors and leaves
                // only the loose statements there. Rendering just those said a
                // class with twenty methods had an EMPTY body — a wrong tree
                // rather than a named refusal, which is the one thing the view
                // is not allowed to produce, and the tree oracle is what found
                // it. Rules have no view at all yet (the whole `Regex::*`
                // subtree), so a grammar with any is refused by name.
                // GRAMMAR RULES, each body parsed from its text (RxView).
                ValueList ruleNodes;
                for (auto& r : cd->rules) {
                    if (!r.params.empty()) unmapped("a grammar rule with a signature");
                    ruleNodes.push_back(node("Statement::Expression", {{"expression", ruleDecl(r)}}));
                }
                // In SOURCE order, as Rakudo keeps them: the parse files them by
                // kind, and each kind keeps its line. (A part with no line, one
                // the parser made, leaves the order by kind.)
                std::vector<std::pair<int, Value>> parts;
                for (auto& a : cd->attrs)
                    parts.push_back({a.declLine, node("Statement::Expression", {{"expression", attribute(a)}})});
                for (auto& m : cd->methods)
                    parts.push_back({m->line, node("Statement::Expression", {{"expression", routine(m.get())}})});
                for (auto& st : cd->body) parts.push_back({st->line, buildStmt(st.get())});
                for (size_t k = 0; k < ruleNodes.size(); k++) parts.push_back({cd->rules[k].declLine, ruleNodes[k]});
                if (std::all_of(parts.begin(), parts.end(), [](const std::pair<int, Value>& p) { return p.first > 0; }))
                    std::stable_sort(parts.begin(), parts.end(),
                                     [](const std::pair<int, Value>& x, const std::pair<int, Value>& y) { return x.first < y.first; });
                ValueList body;
                for (auto& p : parts) body.push_back(p.second);
                // `is Parent` and `does Role`, each a trait upstream. The parse
                // keeps the first of them apart and the rest by kind, so they
                // come back as the first, then the `is`, then the `does` —
                // not the source's order when the two interleave, and the same
                // class. A role's arguments (`does R[Str]`) come with it.
                ValueList traits;
                auto roleType = [&](const std::string& r) {
                    Value t = node("Type::Simple", {{"name", name(r)}});
                    for (auto& ra : cd->roleArgs) {
                        if (ra.first != r) continue;
                        ValueList as;
                        for (auto& x : ra.second) as.push_back(buildExpr(x.get()));
                        return node("Type::Parameterized",
                                    {{"base-type", t}, {"args", node("ArgList", {{"args", list(as)}})}});
                    }
                    return t;
                };
                auto isParent = [&](const std::string& t) {
                    return node("Trait::Is", {{"type", node("Type::Simple", {{"name", name(t)}})}});
                };
                if (!cd->parent.empty())
                    traits.push_back(cd->parentIsDoes ? node("Trait::Does", {{"type", roleType(cd->parent)}})
                                                      : isParent(cd->parent));
                for (auto& t : cd->extraParents) traits.push_back(isParent(t));
                for (auto& r : cd->roles) traits.push_back(node("Trait::Does", {{"type", roleType(r)}}));
                if (cd->classRw) traits.push_back(traitIs("rw"));
                if (cd->isExport) traits.push_back(exportTrait(cd->exportTags));
                Value pkg = node(cls, {{"name", name(cd->name)},
                                       {"scope", Value::str(cd->unitForm ? "unit" : cd->isMy ? "my" : "our")},
                                       {"traits", list(traits)},
                                       {"body", node("Block", {{"body",
                                           node("Blockoid", {{"statement-list",
                                               node("StatementList", {{"statements", list(body)}})}})}})}});
                return node("Statement::Expression", {{"expression", pkg}});
            }
            case NK::EnumDecl: {
                auto* d = static_cast<EnumDecl*>(s);
                if (d->name.empty()) unmapped("an anonymous enum");
                // The VALUE LIST. `enum C <a b c>` is a `QuotedString` with the
                // `words`/`val` processors upstream — our parser normalises the
                // angle form to an array of string literals and loses the
                // spelling, so the words are put back. That is a
                // reconstruction, and it is the only faithful option: Rakudo
                // REFUSES `enum C ["a", "b"]`, so rendering what our tree
                // literally holds would produce text upstream cannot parse.
                Value term;
                if (d->values && d->values->kind == NK::ArrayLit) {
                    auto* al = static_cast<ArrayLit*>(d->values.get());
                    std::string words;
                    bool allPlain = !al->items.empty();
                    for (auto& it : al->items) {
                        if (!it || it->kind != NK::StrLit) { allPlain = false; break; }
                        const std::string& w = static_cast<StrLit*>(it.get())->v;
                        if (w.find_first_of(" \t\n") != std::string::npos) { allPlain = false; break; }
                        if (!words.empty()) words += " ";
                        words += w;
                    }
                    if (!allPlain) unmapped("an enum whose values are not a word list");
                    ValueList procs;
                    procs.push_back(Value::str("words"));
                    procs.push_back(Value::str("val"));
                    term = node("QuotedString",
                        {{"segments",   list({node("StrLiteral", {{"value", Value::str(words)}})})},
                         {"processors", list(procs)}});
                }
                else if (d->values && d->values->kind == NK::ListExpr &&
                         static_cast<ListExpr*>(d->values.get())->parenned) {
                    // `enum C (a => 1, b => 2)`: the list brings its own
                    // parentheses, and wrapping it again rendered `((…))`.
                    term = buildExpr(d->values.get());
                }
                else if (d->values) {
                    term = node("Circumfix::Parentheses",
                                {{"semilist", semiList({buildExpr(d->values.get())})}});
                }
                else unmapped("an enum with no values");
                return node("Statement::Expression", {{"expression",
                    node("Type::Enum", {{"name",  name(d->name)},
                                        {"scope", Value::str(d->isMy ? "my" : "our")},
                                        {"traits", list(d->isExport ? ValueList{exportTrait(d->exportTags)} : ValueList{})},
                                        {"term",  term}})}});
            }
            case NK::SubsetDecl: {
                auto* d = static_cast<SubsetDecl*>(s);
                if (d->name.empty()) unmapped("an anonymous subset");
                Value n = node("Type::Subset", {{"name",  name(d->name)},
                                                {"scope", Value::str(d->isMy ? "my" : "our")}});
                if (!d->baseType.empty())
                    n.obj()->attrs["base-type"] = node("Type::Simple", {{"name", name(d->baseType)}});
                if (d->isExport) n.obj()->attrs["traits"] = list({exportTrait(d->exportTags)});
                if (d->where) n.obj()->attrs["where"] = buildExpr(d->where.get());
                return node("Statement::Expression", {{"expression", n}});
            }
            case NK::NamedRegexDecl: unmapped("a named regex declaration");
            case NK::GivenStmt: {
                auto* g = static_cast<GivenStmt*>(s);
                // `given` is a LOOP modifier upstream and `with`/`without` are
                // CONDITION modifiers — measured, and not what the names
                // suggest: `given` runs its body once, `with` tests a value.
                if (g->modifier && g->body && !g->hasElse)
                    if (Expr* b = soleExpr(g->body->stmts))
                        return modifierStmt(g->defGuard == 1 ? "StatementModifier::With"
                                          : g->defGuard == 2 ? "StatementModifier::Without"
                                                             : "StatementModifier::Given",
                                            g->defGuard == 0, g->topic.get(), b);
                static const std::vector<StmtPtr> noStmts;
                Value body = binderBlock(g->var, g->params, g->body ? g->body->stmts : noStmts);
                // `given`, `with` and `without` share one node in OUR tree and
                // are three classes upstream — and the two defined-guards are
                // shaped like the `if` family (`condition`/`then`), not like
                // `given` (`source`/`body`). Building all three as
                // `Statement::Given` also dropped the `else` on the floor, so
                // `with $x { } orwith $y { }` — which our parser rewrites to
                // `with $x { } else { with $y { } }` — presented as a `given`
                // with one branch and no alternative. L10N::ZH's block suite is
                // what caught it; nothing else in the corpus writes `orwith`.
                if (g->defGuard == 0)
                    return node("Statement::Given",
                                {{"source", buildExpr(g->topic.get())}, {"body", body}});
                Value n = node(g->defGuard == 1 ? "Statement::With" : "Statement::Without",
                    {{"condition", buildExpr(g->topic.get())},
                     {g->defGuard == 1 ? "then" : "body", body}});
                if (g->elseBody)
                    n.obj()->attrs["else"] = binderBlock(g->elseVar, g->elseParams, g->elseBody->stmts);
                return n;
            }
            case NK::WhenStmt: {
                auto* w = static_cast<WhenStmt*>(s);
                // `default` is its own class upstream, not a `when` with no
                // condition — a walker dispatches on the difference.
                if (!w->cond)
                    return node("Statement::Default",
                        {{"body", node("Block", {{"body", w->body ? blockoid(w->body->stmts) : blockoid({})}})}});
                return node("Statement::When",
                    {{"condition", buildExpr(w->cond.get())},
                     {"body", node("Block", {{"body", w->body ? blockoid(w->body->stmts) : blockoid({})}})}});
            }
            case NK::LoopStmt: {
                auto* l = static_cast<LoopStmt*>(s);
                Value n = node("Statement::Loop",
                    {{"body", node("Block", {{"body", l->body ? blockoid(l->body->stmts) : blockoid({})}})}});
                if (l->init)  n.obj()->attrs["setup"]     = buildExpr(l->init.get());
                if (l->cond)  n.obj()->attrs["condition"] = buildExpr(l->cond.get());
                if (l->incr)  n.obj()->attrs["increment"] = buildExpr(l->incr.get());
                return n;
            }
            // `last` / `next` / `redo` are CALLS upstream, not statements of
            // their own — measured: `for … { last }` puts a
            // Call::Name::WithoutParentheses in the body, and it deparses `last`.
            case NK::LastStmt: return loopControl("last", static_cast<LastStmt*>(s)->target);
            case NK::NextStmt: return loopControl("next", static_cast<NextStmt*>(s)->target);
            case NK::RedoStmt: return loopControl("redo", static_cast<RedoStmt*>(s)->target);
            case NK::RepeatStmt: {
                auto* r = static_cast<RepeatStmt*>(s);
                return node(r->isUntil ? "Statement::Loop::RepeatUntil"
                                       : "Statement::Loop::RepeatWhile",
                    {{"condition", buildExpr(r->cond.get())},
                     {"body", node("Block", {{"body",
                         r->body ? blockoid(r->body->stmts) : blockoid({})}})}});
            }
            default: break;
        }
        unmapped("this statement");
    }
};

} // namespace

Value rakuAstView(Interpreter& I, const std::string& source, bool compUnit,
                  const TokenXform* xform) {
    // Lexer + Parser only. No BEGIN runs, nothing is executed — which is one of
    // the two recorded divergences from Rakudo, whose `.AST` compiles far
    // enough to run BEGIN blocks and to refuse undeclared variables.
    // A ParseError is a C++ type the Raku level cannot see, so it has to become
    // a Raku exception here or it escapes `.AST` as a top-level `===SORRY!===`
    // and takes the program with it — past any `try` or `CATCH` around the call.
    // The round-trip harness is what found that: it re-parses what the renderer
    // produced, which is precisely the call most likely to meet bad source.
    Program prog;
    try {
        Lexer lexer(source);
        std::vector<Token> toks = lexer.tokenize();
        // The one place a LOCALIZED parse differs (P1-L10N): the keywords are
        // German, and by here they are ordinary identifier tokens.
        if (xform) (*xform)(toks);
        Parser parser(std::move(toks));
        parser.declPod_ = lexer.declPod_;   // `#=` and `#|`, for the WHY of a rule
        parser.leadPod_ = lexer.leadPod_;
        prog = parser.parseProgram();
    } catch (ParseError& e) {
        throw RakuError{Value::typeObj("X::Syntax::Confused"),
                        std::string("`.AST` could not parse the source: ") + e.what()};
    }
    Builder b{I};
    Value sl = b.statementList(prog.stmts);
    // The `Doc::` blocks are STATEMENTS upstream — measured: `say 1; =begin
    // rakudoc … =end rakudoc; say 2` gives `Statement::Expression, Doc::Block,
    // Statement::Expression` from `visit-children`. Our lexer strips pod before
    // the parser ever sees it, so they are appended rather than interleaved:
    // for a document (all pod, which is what `.rakudoc` is called on) that is
    // exact, and for a mixed file the blocks are present and in their own order
    // but sit after the code. Splicing them would need a line stamp on each pod
    // block, which nothing asks for yet — recorded rather than half-done.
    if (Value* ss = [&]() -> Value* {
            auto it = sl.obj()->attrs.find("statements");
            return it == sl.obj()->attrs.end() ? nullptr : &it->second;
        }()) {
        for (auto& pod : parsePod(source)) {
            if (b.podClass(pod) == "Pod::Block::Declarator" && !b.attachedDocs.empty()) {
                std::string text;
                for (auto& a : b.docParagraphs(pod, true)) if (a.t == VT::Str) text += (text.empty() ? "" : "\n") + a.toStr();
                if (b.attachedDocs.count(text)) continue;   // attached to its rule as WHY
            }
            Value d = b.docBlock(pod);
            if (d.t == VT::Object && ss->arr()) ss->arr()->push_back(d);
        }
    }
    if (!compUnit) return sl;
    // `.AST(:compunit)` — what Needle::Compile asks for when the needle is a
    // whole program, so it can unshift a statement into the list.
    return b.node("CompUnit", {{"statement-list", sl},
                               {"comp-unit-name", Value::str("EVAL")}});
}

} // namespace rakupp
