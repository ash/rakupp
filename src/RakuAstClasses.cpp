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
