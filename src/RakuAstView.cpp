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
                // `True`/`False` are terms naming a Bool enum value, not literals.
                return node("Term::Name", {{"name", name(static_cast<BoolLit*>(e)->v ? "True" : "False")}});
            case NK::VarExpr: {
                auto* v = static_cast<VarExpr*>(e);
                if (v->declare) return declaration(v);
                if (v->name.size() > 1 && v->name[1] == '*')
                    return node("Var::Dynamic", {{"name", Value::str(v->name)}});
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
                return node("ApplyInfix", {{"left",  buildExpr(b->lhs.get())},
                                           {"infix", node("Infix", {{"operator", Value::str(b->op)}})},
                                           {"right", buildExpr(b->rhs.get())}});
            }
            case NK::Assign: {
                auto* a = static_cast<Assign*>(e);
                return node("ApplyInfix", {{"left",  buildExpr(a->target.get())},
                                           {"infix", node("Infix", {{"operator", Value::str(a->op)}})},
                                           {"right", buildExpr(a->value.get())}});
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
                return node("ApplyInfix", {{"left",  buildExpr(r->from.get())},
                                           {"infix", node("Infix", {{"operator", Value::str(op)}})},
                                           {"right", buildExpr(r->to.get())}});
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
            case NK::ExprStmt:
                return node("Statement::Expression",
                            {{"expression", buildExpr(static_cast<ExprStmt*>(s)->e.get())}});
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
                if (!b->phaser.empty()) unmapped("a phaser block");
                return node("Statement::Expression",
                            {{"expression", node("Block", {{"body", blockoid(b->stmts)}})}});
            }
            case NK::IfStmt: {
                auto* i = static_cast<IfStmt*>(s);
                if (i->branches.empty()) unmapped("an `if` with no branches");
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
                return node(w->isUntil ? "Statement::Until" : "Statement::While",
                    {{"condition", buildExpr(w->cond.get())},
                     {"body", node("Block", {{"body", w->body ? blockoid(w->body->stmts) : blockoid({})}})}});
            }
            case NK::ForStmt: {
                auto* f = static_cast<ForStmt*>(s);
                ValueList ps;
                for (auto& v : f->vars)
                    ps.push_back(node("Parameter",
                        {{"target", node("ParameterTarget::Var", {{"name", Value::str(v)}})}}));
                Value body = node("PointyBlock",
                    {{"signature", node("Signature", {{"parameters", list(ps)}})},
                     {"body", f->body ? blockoid(f->body->stmts) : blockoid({})}});
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
            case NK::SubDecl: {
                auto* d = static_cast<SubDecl*>(s);
                if (d->isMethod) unmapped("a method declaration");
                ValueList ps;
                for (auto& p : d->params) ps.push_back(parameter(p));
                Value sub = node("Sub", {{"name", name(d->name)},
                                         {"multiness", Value::str(d->isMulti ? "multi" : "")},
                                         {"signature", node("Signature", {{"parameters", list(ps)}})},
                                         {"body", blockoid(d->body)}});
                return node("Statement::Expression", {{"expression", sub}});
            }
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
                Value pkg = node(cls, {{"name", name(cd->name)},
                                       {"scope", Value::str(cd->isMy ? "my" : "our")},
                                       {"body", node("Block", {{"body", blockoid(cd->body)}})}});
                return node("Statement::Expression", {{"expression", pkg}});
            }
            case NK::EnumDecl:       unmapped("an enum declaration");
            case NK::SubsetDecl:     unmapped("a subset declaration");
            case NK::NamedRegexDecl: unmapped("a named regex declaration");
            case NK::GivenStmt: {
                auto* g = static_cast<GivenStmt*>(s);
                Value n = node("Statement::Given",
                    {{"source", buildExpr(g->topic.get())},
                     {"body", node("Block", {{"body", g->body ? blockoid(g->body->stmts) : blockoid({})}})}});
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
            case NK::RepeatStmt:     unmapped("a `repeat` loop");
            default: break;
        }
        unmapped("this statement");
    }
};

} // namespace

Value rakuAstView(Interpreter& I, const std::string& source, bool compUnit) {
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
        Parser parser(lexer.tokenize());
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
