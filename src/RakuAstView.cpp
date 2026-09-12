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
#include "Value.h"

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
        auto od = std::make_shared<ObjectData>();
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
        for (auto& s : stmts) out.push_back(buildStmt(s.get()));
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
    Value buildExpr(Expr* e) {
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
                return applyInfix(b->op, buildExpr(b->lhs.get()), buildExpr(b->rhs.get()));
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
                return applyInfix(assignInfix(a->op),
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
                    return node(cls, {{"target", node("SemiList", {{"statements",
                        list({node("Statement::Expression",
                                   {{"expression", buildExpr(u->operand.get())}})})}})}});
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
                for (auto& a : c->args) args.push_back(buildExpr(a.get()));
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
                if (inv && inv->kind == NK::VarExpr && static_cast<VarExpr*>(inv)->synthTopic)
                    return node("Term::TopicCall", {{"call", call}});
                return node("ApplyPostfix", {{"operand", buildExpr(inv)}, {"postfix", call}});
            }
            case NK::Index: {
                auto* ix = static_cast<Index*>(e);
                Value inner = node("SemiList", {{"statements",
                    list({node("Statement::Expression", {{"expression", buildExpr(ix->index.get())}})})}});
                // `%h<a>` is a different class from `%h{'a'}` upstream, and the
                // parser records which spelling it saw (`angleKey`).
                const char* cls = ix->isHash ? (ix->angleKey ? "Postcircumfix::LiteralHashIndex"
                                                             : "Postcircumfix::HashIndex")
                                             : "Postcircumfix::ArrayIndex";
                return node("ApplyPostfix", {{"operand", buildExpr(ix->base.get())},
                                             {"postfix", node(cls, {{"index", inner}})}});
            }
            case NK::ListExpr: {
                auto* l = static_cast<ListExpr*>(e);
                ValueList items;
                for (auto& i : l->items) items.push_back(buildExpr(i.get()));
                if (l->semicolon) return node("SemiList", {{"statements", list(items)}});
                Value applied = node("ApplyListInfix",
                    {{"infix", node("Infix", {{"operator", Value::str(",")}})},
                     {"operands", list(items)}});
                return l->parenned ? node("Circumfix::Parentheses",
                                          {{"semilist", node("SemiList", {{"statements", list({applied})}})}})
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
                            {{"semilist", node("SemiList", {{"statements", list({inner})}})}});
            }
            case NK::HashLit: {
                auto* h = static_cast<HashLit*>(e);
                ValueList items;
                for (auto& i : h->items) items.push_back(buildExpr(i.get()));
                Value inner = node("ApplyListInfix",
                    {{"infix", node("Infix", {{"operator", Value::str(",")}})},
                     {"operands", list(items)}});
                return node("Circumfix::HashComposer",
                            {{"semilist", node("SemiList", {{"statements", list({inner})}})}});
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
                if (p->colonForm)
                    return node("ColonPair::Value", {{"key", Value::str(p->key)},
                                                     {"value", buildExpr(p->value.get())}});
                return node("FatArrow", {{"key",   Value::str(p->key)},
                                         {"value", buildExpr(p->value.get())}});
            }
            case NK::InterpStr: {
                auto* s = static_cast<InterpStr*>(e);
                ValueList segs;
                for (auto& p : s->parts) segs.push_back(buildExpr(p.get()));
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
                return blockLike(b->isSub, b->params, b->body);
            }
            case NK::AllomorphLit: {
                auto* a = static_cast<AllomorphLit*>(e);
                return node("Literal", {{"value", Value::str(a->str)}});
            }
            case NK::SymbolicRef: unmapped("a symbolic reference (::(…))");
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
            {"scope", Value::str(v->declScope.empty() ? "my" : v->declScope)},
            {"sigil", Value::str(sigil)},
            {"desigilname", name(bare)},
        };
        Value n = node("VarDeclaration::Simple", attrs);
        if (!v->declType.empty())
            n.obj()->attrs["type"] = node("Type::Simple", {{"name", name(v->declType)}});
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

    // A block or a pointy block, with its signature when it has one.
    Value blockLike(bool isSub, const std::vector<Param>& params, const std::vector<StmtPtr>& body) {
        Value bd = blockoid(body);
        if (params.empty())
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
        return node(cls, {{"name", name((d->isPrivate ? "!" : "") + d->name)},
                          {"multiness", Value::str(d->isMulti ? "multi" : "")},
                          {"signature", node("Signature", {{"parameters", list(ps)}})},
                          {"body", blockoid(d->body)}});
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
        if (a.def)
            n.obj()->attrs["initializer"] =
                node("Initializer::Assign", {{"expression", buildExpr(a.def.get())}});
        return n;
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
        Value target = node("ParameterTarget::Var", {{"name", Value::str(p.name)}});
        Value n = node("Parameter", {{"target", target}});
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
        if (p.defaultVal) n.obj()->attrs["default"] = buildExpr(p.defaultVal.get());
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
                Value n = node(i->isUnless ? "Statement::Unless" : "Statement::If",
                    {{"condition", buildExpr(i->branches[0].first.get())},
                     {"then", node("Block", {{"body", blockoid(i->branches[0].second->stmts)}})}});
                if (i->branches.size() > 1) {
                    ValueList elsifs;
                    for (size_t k = 1; k < i->branches.size(); k++)
                        elsifs.push_back(node("Statement::Elsif",
                            {{"condition", buildExpr(i->branches[k].first.get())},
                             {"then", node("Block", {{"body", blockoid(i->branches[k].second->stmts)}})}}));
                    n.obj()->attrs["elsifs"] = list(elsifs);
                }
                if (i->elseBlock)
                    n.obj()->attrs["else"] = node("Block", {{"body", blockoid(i->elseBlock->stmts)}});
                return n;
            }
            case NK::WhileStmt: {
                auto* w = static_cast<WhileStmt*>(s);
                if (w->modifier && w->body)
                    if (Expr* b = soleExpr(w->body->stmts))
                        return modifierStmt(w->isUntil ? "StatementModifier::Until"
                                                       : "StatementModifier::While",
                                            true, w->cond.get(), b);
                return node(w->isUntil ? "Statement::Until" : "Statement::While",
                    {{"condition", buildExpr(w->cond.get())},
                     {"body", node("Block", {{"body", w->body ? blockoid(w->body->stmts) : blockoid({})}})}});
            }
            case NK::ForStmt: {
                auto* f = static_cast<ForStmt*>(s);
                if (f->modifier && f->body)
                    if (Expr* b = soleExpr(f->body->stmts))
                        return modifierStmt("StatementModifier::For", true, f->list.get(), b);
                ValueList ps;
                for (auto& v : f->vars)
                    ps.push_back(node("Parameter",
                        {{"target", node("ParameterTarget::Var", {{"name", Value::str(v)}})}}));
                // A TOPIC-LESS `for` has a plain Block upstream, not a pointy
                // one with an empty signature — measured. Building the pointy
                // block anyway rendered `for 1, 2 -> { … }`, which takes no
                // parameter at all, so the body saw the OUTER `$_`. It parses,
                // so the round trip called it stable; it is a different program.
                Value bd = f->body ? blockoid(f->body->stmts) : blockoid({});
                Value body = ps.empty()
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
                return node("Statement::Use", {{"module-name", name(u->module)}});
            }
            case NK::ClassDecl: {
                // `class` / `role` / `grammar` / `module` / `package` — four
                // classes upstream, told apart by the declarator the parser saw.
                // Twelve of the corpus's fifty-nine programs stop here, which is
                // why it is the first widening and not the tidiest.
                auto* cd = static_cast<ClassDecl*>(s);
                const char* cls = cd->isRole    ? "Role"
                                : cd->isGrammar ? "Grammar"
                                : cd->isPackage ? "Module" : "Class";
                // The body is NOT `cd->body`: our parser lifts attributes,
                // methods and grammar rules into their own vectors and leaves
                // only the loose statements there. Rendering just those said a
                // class with twenty methods had an EMPTY body — a wrong tree
                // rather than a named refusal, which is the one thing the view
                // is not allowed to produce, and the tree oracle is what found
                // it. Rules have no view at all yet (the whole `Regex::*`
                // subtree), so a grammar with any is refused by name.
                if (!cd->rules.empty()) unmapped("a grammar rule (the regex tree)");
                ValueList body;
                for (auto& a : cd->attrs)
                    body.push_back(node("Statement::Expression", {{"expression", attribute(a)}}));
                for (auto& m : cd->methods)
                    body.push_back(node("Statement::Expression", {{"expression", routine(m.get())}}));
                for (auto& st : cd->body) body.push_back(buildStmt(st.get()));
                Value pkg = node(cls, {{"name", name(cd->name)},
                                       {"scope", Value::str(cd->isMy ? "my" : "our")},
                                       {"body", node("Block", {{"body",
                                           node("Blockoid", {{"statement-list",
                                               node("StatementList", {{"statements", list(body)}})}})}})}});
                return node("Statement::Expression", {{"expression", pkg}});
            }
            case NK::EnumDecl:       unmapped("an enum declaration");
            case NK::SubsetDecl:     unmapped("a subset declaration");
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
                Value body = node("Block", {{"body",
                    g->body ? blockoid(g->body->stmts) : blockoid({})}});
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
                    n.obj()->attrs["else"] =
                        node("Block", {{"body", blockoid(g->elseBody->stmts)}});
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
        prog = parser.parseProgram();
    } catch (ParseError& e) {
        throw RakuError{Value::typeObj("X::Syntax::Confused"),
                        std::string("`.AST` could not parse the source: ") + e.what()};
    }
    Builder b{I};
    Value sl = b.statementList(prog.stmts);
    if (!compUnit) return sl;
    // `.AST(:compunit)` — what Needle::Compile asks for when the needle is a
    // whole program, so it can unshift a statement into the list.
    return b.node("CompUnit", {{"statement-list", sl},
                               {"comp-unit-name", Value::str("EVAL")}});
}

} // namespace rakupp
