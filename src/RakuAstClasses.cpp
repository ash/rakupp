// The RakuAST:: class registry — see RakuAstClasses.h for why it is its own
// map and not part of `classes_`.
//
// Lives in the PARSE archive (RAKUPP_PARSE_SOURCES), with throwing doubles in
// src/stubs/stub_eval.cpp: `.AST` *is* the parser, so a `--slim=-eval` binary
// that meets a RakuAST name has to fail the same way it fails an EVAL. A
// program that never mentions the namespace never reaches either, and SlimScan
// counts a `RakuAST::` name as an eval use so such a program keeps its parser.

#include "RakuAstClasses.h"

#include "Ast.h"     // PublishedOnce
#include "Interpreter.h"   // callCallable — `visit-children` runs user code
#include "Value.h"   // ClassInfo

#include <unordered_map>

namespace rakupp {

namespace {

// One row per class: the class, its ancestors nearest-first, then a null.
// Names carry no `RakuAST::` prefix; the builder adds it once. Measured
// against Rakudo — see the generator named in the file itself.
const char* const kClassTable[] = {
#include "rakuast-classes.inc"
    nullptr
};

struct Registry {
    std::unordered_map<std::string, std::shared_ptr<ClassInfo>> byName;
    std::unordered_map<std::string, std::vector<std::string>> ancestry;
};

// Published exactly once, by whichever thread gets there first; the loser of
// the compare-exchange frees its copy. Readers acquire-load, so a reader that
// sees the pointer cannot see a half-built map behind it.
PublishedOnce<const Registry*> g_registry;

const Registry* build() {
    auto* reg = new Registry();
    // Two passes: every ClassInfo has to exist before any parent link is made,
    // because the table is alphabetical and a class names ancestors that come
    // after it.
    for (const char* const* p = kClassTable; *p; ) {
        std::string name = kRakuAstPrefix + std::string(*p);
        auto ci = std::make_shared<ClassInfo>();
        ci->name = name;
        reg->byName.emplace(name, std::move(ci));
        // `.^mro` is the row itself plus Any and Mu — verified on 2026.08 for
        // every class in the table, which is why nothing here recomputes it.
        auto& anc = reg->ancestry[name];
        for (const char* const* q = p; *q; q++) anc.push_back(kRakuAstPrefix + std::string(*q));
        anc.push_back("Any");
        anc.push_back("Mu");
        while (*p) p++;
        p++;                              // past the row's null
    }
    for (const char* const* p = kClassTable; *p; ) {
        ClassInfo* ci = reg->byName[kRakuAstPrefix + std::string(*p)].get();
        const char* const* q = p + 1;
        // The FIRST ancestor is the primary parent and the rest are extra
        // parents: one level of `extraParents` carries the whole linearization,
        // which is what makes `$node ~~ RakuAST::Expression` answer without a
        // walk, and `findMethod` recurses through both — so the methods the
        // renderer puts on `RakuAST::Node` are inherited by every node class.
        if (*q) {
            auto it = reg->byName.find(kRakuAstPrefix + std::string(*q));
            if (it != reg->byName.end()) ci->parent = it->second;
            for (q++; *q; q++) {
                auto e = reg->byName.find(kRakuAstPrefix + std::string(*q));
                if (e != reg->byName.end()) ci->extraParents.push_back(e->second);
            }
        }
        while (*p) p++;
        p++;
    }
    // The four operations live on `RakuAST::Node` as builtin Code values and
    // are inherited by every node class through the ordinary parent walk, so
    // they cost nothing on any path that never materializes the registry, and
    // instance dispatch finds them on its own fast path rather than needing a
    // ladder arm. A builtin method receives `self` as its first argument.
    auto method = [](BuiltinFn fn) {
        Value v; v.t = VT::Code;
        auto c = std::make_shared<Callable>();
        c->builtin = std::move(fn);
        c->isMethod = true;
        v.setCode(std::move(c));
        return v;
    };
    auto rest = [](ValueList& a) {
        ValueList out;
        for (size_t i = 1; i < a.size(); i++) out.push_back(a[i]);
        return out;
    };
    // `new` goes on EVERY class, not just Node. A handful of them have no
    // ancestors at all — `RakuAST::Statement::Elsif` is the one that taught
    // this, and `.^mro` says the same upstream — so they never reach Node's
    // methods, and inheriting the constructor from there silently gave them the
    // DEFAULT one instead: `Statement::Elsif.new(condition => …, then => …)`
    // built a node with nothing in it, and the `if` around it then rendered
    // `elsif` with no condition and no block. Valid Raku, different program.
    Value newMethod = method([rest](Interpreter& I, ValueList& a) -> Value {
        std::string cls;
        if (!a.empty() && a[0].t == VT::Type) cls = a[0].s;
        else if (!a.empty() && a[0].t == VT::Object && a[0].obj() && a[0].obj()->cls)
            cls = a[0].obj()->cls->name;
        ValueList args = rest(a);
        return rakuAstNew(I, cls, args);
    });
    for (auto& kv : reg->byName) kv.second->methods["new"] = newMethod;

    ClassInfo* node = reg->byName["RakuAST::Node"].get();
    node->methods["DEPARSE"] = method([](Interpreter& I, ValueList& a) -> Value {
        return Value::str(a.empty() ? std::string() : rakuAstDeparse(I, a[0]));
    });
    node->methods["EVAL"] = method([](Interpreter& I, ValueList& a) -> Value {
        return a.empty() ? Value::any() : rakuAstEval(I, a[0]);
    });
    // The three `StatementList` accessors Needle::Compile drives: `.statements`
    // reads the list, `.statement-list` reaches it through a CompUnit, and
    // `.unshift-statement` puts one in front — which is how a needle gets its
    // `my $/;` declaration. Plain attribute reads, and a mutation that writes
    // the node it was called on, so the caller sees it.
    auto attrOf = [](const Value& self, const char* key) -> Value* {
        if (self.t != VT::Object || !self.obj()) return nullptr;
        auto it = self.obj()->attrs.find(key);
        return it == self.obj()->attrs.end() ? nullptr : &it->second;
    };
    node->methods["statements"] = method([attrOf](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::array();
        if (Value* v = attrOf(a[0], "statements")) return *v;
        // …and NOT through a CompUnit. Measured: Rakudo's CompUnit has no
        // `.statements`, only `.statement-list`, and reaching through would be
        // a leniency a module's `.^can` probe could read as a different API.
        Value empty = Value::array(); empty.isList = true;
        return empty;
    });
    node->methods["statement-list"] = method([attrOf](Interpreter&, ValueList& a) -> Value {
        if (a.empty()) return Value::any();
        if (Value* v = attrOf(a[0], "statement-list")) return *v;
        return a[0];   // a StatementList IS its own statement list
    });
    // `.replace-statement-list($sl)` — Needle::Compile's CompUnit path swaps the
    // whole list rather than unshifting into it. (The plan's summary named three
    // accessors; grepping the tarball for what it calls on an AST object named
    // the fourth, which is why that grep is the tool and not the summary.)
    node->methods["replace-statement-list"] = method([](Interpreter&, ValueList& a) -> Value {
        if (a.size() < 2 || a[0].t != VT::Object || !a[0].obj()) return Value::any();
        a[0].obj()->attrs["statement-list"] = a[1];
        return a[0];
    });
    // `.push($node)` on a LIST-shaped node — an ArgList, a Name, a StatementList.
    // Needle::Compile builds an ArgList and then pushes each adverb onto it
    // (`$args.push(RakuAST::ColonPair::True.new($_))`) rather than passing them
    // all to `.new`. The slot is the same one `.new`'s positionals land in.
    node->methods["push"] = method([](Interpreter&, ValueList& a) -> Value {
        if (a.size() < 2 || a[0].t != VT::Object || !a[0].obj() || !a[0].obj()->cls)
            return Value::any();
        const std::string& n = a[0].obj()->cls->name;
        std::string cls = isRakuAstName(n) ? n.substr(9) : n;
        auto plus = cls.find("+{");            // `$node but Role`, as shortName does
        if (plus != std::string::npos) cls.resize(plus);
        const char* slot = rakuAstListSlot(cls);
        if (!slot) return Value::any();
        Value& lst = a[0].obj()->attrs[slot];
        if (lst.t != VT::Array || !lst.arr()) { lst = Value::array(); }
        lst.arr()->push_back(a[1]);
        return a[0];
    });
    // `.set-expression($e)` — the mutator beside `.expression`, which a statement
    // uses to rewrite what it evaluates in place. Needle::Compile's `not` needle
    // negates the LAST statement of a compiled unit this way. Named one by one
    // rather than served by a generic `set-*`: a `.^can` probe must not be told
    // this node can set an attribute Rakudo gives it no setter for.
    node->methods["set-expression"] = method([](Interpreter&, ValueList& a) -> Value {
        if (a.size() < 2 || a[0].t != VT::Object || !a[0].obj()) return Value::any();
        a[0].obj()->attrs["expression"] = a[1];
        return a[0];
    });
    node->methods["unshift-statement"] = method([attrOf](Interpreter&, ValueList& a) -> Value {
        if (a.size() < 2) return Value::any();
        Value target = a[0];
        if (Value* sl = attrOf(target, "statement-list")) target = *sl;   // reach through a CompUnit
        Value* ss = attrOf(target, "statements");
        if (!ss || ss->t != VT::Array || !ss->arr()) return Value::any();
        ss->arr()->insert(ss->arr()->begin(), a[1]);
        return a[0];
    });
    // `$node.visit-children(&cb)` — the callback once per SYNTACTIC child, in
    // source order (RAKUAST-PLAN P4). Upstream this is a per-class method with
    // a hand-written body; here it is one walk over the node's own attribute
    // map, which is insertion-ordered (see ValueHash.h), so the order is the
    // order the view SET the children — the same order `--rakuast` prints and
    // the same order the oracle's `visit-children` walk produces.
    //
    // This is the whole of what a query engine needs. ASTQuery's walker is this
    // method plus `@*LINEAGE`, and the lineage is the WALKER's: Rakudo does not
    // populate it during `visit-children` either (measured), it is an ordinary
    // dynamic the visitor re-declares as it descends.
    //
    // `.parent` answers Nil upstream for every tree a program can get hold of,
    // and storing a real parent link on our refcounted nodes would be a cycle
    // and a leak — so there is nothing to BUILD there. There is still something
    // to DECLARE: without a method of its own the call fell through to the
    // generic `.parent`, which reads any invocant as a path, so a RakuAST node
    // answered `IO::Path.new(".")` where Rakudo answers `Nil`. "Not implemented"
    // has to be said out loud, or the fallback answers for it.
    node->methods["parent"] = method([](Interpreter&, ValueList&) -> Value {
        return Value::nil();
    });
    node->methods["visit-children"] = method([](Interpreter& I, ValueList& a) -> Value {
        if (a.size() < 2 || a[0].t != VT::Object || !a[0].obj()) return Value::any();
        // The children are COLLECTED before any of them is visited. The
        // callback is user code and may replace an attribute — a rewriter
        // would — and mutating the map while iterating it is the
        // rehash-under-reader crash `noteSymbolMutation` exists to police.
        ValueList kids;
        for (auto& kv : a[0].obj()->attrs) {
            const Value& v = kv.second;
            if (isRakuAstNode(v)) { kids.push_back(v); continue; }
            if (v.t == VT::Array && v.arr())
                for (auto& e : *v.arr())
                    if (isRakuAstNode(e)) kids.push_back(e);
        }
        Value cb = a[1];
        for (auto& k : kids) { ValueList one{k}; I.callCallable(cb, one); }
        return Value::any();
    });
    // `$node.rakudoc` — the `Doc::Block`s this unit carries (RAKUAST-PLAN P5).
    // Upstream they ARE statements, so this is a filter over the statement list
    // rather than a separate store; `Rakuast::RakuDoc::Render` opens with
    // exactly `$source.AST.rakudoc` and renders what comes back.
    node->methods["rakudoc"] = method([attrOf](Interpreter&, ValueList& a) -> Value {
        Value out = Value::array(); out.isList = true;
        if (a.empty()) return out;
        Value target = a[0];
        if (Value* sl = attrOf(target, "statement-list")) target = *sl;   // reach through a CompUnit
        Value* ss = attrOf(target, "statements");
        if (!ss || ss->t != VT::Array || !ss->arr()) return out;
        for (auto& st : *ss->arr())
            if (isRakuAstNode(st) && st.obj()->cls->name == "RakuAST::Doc::Block")
                out.arr()->push_back(st);
        return out;
    });
    // `.from-identifier("foo")` and `.from-identifier-parts("Foo","Bar")` both
    // make a Name out of plain strings — the spelling every dist uses.
    for (const char* m : {"from-identifier", "from-identifier-parts"})
        node->methods[m] = method([rest](Interpreter& I, ValueList& a) -> Value {
            ValueList args = rest(a);
            return rakuAstNameFrom(I, args);
        });
    // `RakuAST::Literal.from-value($x)` — a literal holding a live value, which
    // renders as that value's own `.raku`.
    node->methods["from-value"] = method([rest](Interpreter& I, ValueList& a) -> Value {
        ValueList args = rest(a);
        std::string cls = "RakuAST::Literal";
        if (!a.empty() && a[0].t == VT::Type) cls = a[0].s;
        return rakuAstNew(I, cls, args);
    });

    const Registry* won = g_registry.publish(reg);
    if (won != reg) delete reg;
    return won;
}

const Registry& registry() {
    const Registry* r = g_registry.get();
    return r ? *r : *build();
}

} // namespace

void rakuAstMaterialize() { registry(); }

const std::shared_ptr<ClassInfo>* rakuAstClass(const std::string& qualifiedName) {
    const Registry& r = registry();
    auto it = r.byName.find(qualifiedName);
    return it == r.byName.end() ? nullptr : &it->second;
}

const std::vector<std::string>& rakuAstAncestry(const std::string& qualifiedName) {
    static const std::vector<std::string> none;
    const Registry& r = registry();
    auto it = r.ancestry.find(qualifiedName);
    return it == r.ancestry.end() ? none : it->second;
}

} // namespace rakupp
