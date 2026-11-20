// Does an expression curry over a `*` it holds — the WhateverCode `* + 1`,
// `*.foo`, `1 < * < 5` — or is the star one side's own? Codegen::hasWhatever,
// moved here unchanged (Codegen.cpp sits at its size ceiling): it reads
// nothing of the emitter, only the tree.
#include "../Ast.h"
#include "../Codegen.h"
#include <set>
#include <string>

namespace rakupp {

bool codegenHasWhatever(Expr* e) {
    if (!e) return false;
    switch (e->kind) {
        case NK::Whatever: return true;
        // The sequence op CONSUMES its own `*` (seed generators, an infinite or
        // predicate endpoint), so it is never part of a surrounding curry —
        // `say (1 ... * > 5).List` is a say of a Seq, not a say of a closure.
        case NK::Binary: { auto* b = static_cast<Binary*>(e);
            if (b->op == "..." || b->op == "...^" || b->op == "^..." || b->op == "^...^") return false;
            // `xx` never curries either: a RHS `*` means an endless repetition
            // and a LHS `*` is repeated as a plain Whatever VALUE (`* xx 3` is
            // (* * *)) — both handled at runtime, like the interpreter.
            if (b->op == "xx") return false;
            // Nor do the SHORT-CIRCUIT infixes: they inspect the left operand
            // at run time and hand back one side or the other, so `* > 2 && *
            // < 5` is not one two-argument closure — the `&&` sees a truthy
            // WhateverCode on its left and IS the right side, `* < 5`, arity
            // one (Rakudo answers the same, and so does the interpreter).
            // Curried statically it read a second argument that no caller
            // passes: `(* > 2 && * < 5)(9)` came back True.
            if (b->op == "&&" || b->op == "and" || b->op == "||" || b->op == "or" || b->op == "//")
                return false;
            // A smartmatch composes only when a BARE `*` is one side — `.grep(* ~~ /rx/)`,
            // the matcher idiom, and `5 ~~ *` (a WhateverCode, as Rakudo's). A `*` anywhere
            // else is part of ONE SIDE: `(* < 1) ~~ Callable` asks whether
            // that WhateverCode is a Callable (True), and `$x ~~ *.foo` asks
            // the curried method about $x. Currying the whole expression
            // answered a closure to both. This is the rule evalBinary
            // applies, spelled syntactically.
            if (b->op.size() >= 2 && b->op.compare(b->op.size() - 2, 2, "~~") == 0) return b->lhs->kind == NK::Whatever || b->rhs->kind == NK::Whatever;   // (`X~~`, `Z!~~` too)
            return codegenHasWhatever(b->lhs.get()) || codegenHasWhatever(b->rhs.get()); }
        case NK::Unary:  return static_cast<Unary*>(e)->op != "capture" && codegenHasWhatever(static_cast<Unary*>(e)->operand.get());   // (`\(*)` holds the star)
        case NK::Ternary: { auto* t = static_cast<Ternary*>(e); return codegenHasWhatever(t->cond.get()) || codegenHasWhatever(t->then.get()) || codegenHasWhatever(t->els.get()); }
        // Only the invocant is part of THIS whatever-curry; arguments are their
        // own closure scopes (e.g. `.grep(* %% 3)`), handled when each arg is emitted.
        case NK::MethodCall: {
            auto* m = static_cast<MethodCall*>(e);
            if (!codegenHasWhatever(m->inv.get())) return false;
            // The metamodel MACROS answer about the star itself rather than
            // composing: `*.WHAT` is `(Whatever)` and `(* < 1).WHAT` is
            // `(WhateverCode)`, both on Rakudo and in the interpreter, where
            // curried they answered a closure. `.WHICH` is NOT one of them —
            // `.map(*.WHICH)` asks each element for its identity.
            static const std::set<std::string> kMetaMacros = {"WHAT", "WHO", "HOW", "VAR", "WHY"};
            return m->meta || !kMetaMacros.count(m->method);
        }
        // A CALL whose callee is a `*` expression is the call OF that
        // WhateverCode, not a bigger curry: `(* < 1)(0)` is False. (The
        // interpreter's own whatever-literal walk has no Call arm either.)
        case NK::Call: { auto* c = static_cast<Call*>(e); if (!c->dotAmp || c->args.empty() || !codegenHasWhatever(c->args[0].get())) return false; if (c->args[0]->kind == NK::Whatever) return true; throw CodegenError{"a `.&` call on a `*` chain"}; }   // `*.&{…}` curries (dotAmpCurry); a chain's invocant would curry apart
        case NK::NqpOp:      { for (auto& x : static_cast<NqpOp*>(e)->args) if (codegenHasWhatever(x.get())) return true; return false; }
        case NK::Index:      return codegenHasWhatever(static_cast<Index*>(e)->base.get()); // *<key> / *[0]
        // A chained comparison composes like any other operator chain:
        // `1 < * < 5` is one WhateverCode of arity one (the chain emitter
        // binds each operand once, so the star is read once too).
        case NK::ChainExpr:  { for (auto& o : static_cast<ChainExpr*>(e)->operands) if (codegenHasWhatever(o.get())) return true; return false; }
        // (a LIST does not compose: `(* < 1, * > 9)` is two closures, and a
        // trailing comma makes `(* < 1,)` a one-element list holding one —
        // each item curries on its own, where argList already puts it)
        default: return false;
    }
}

}  // namespace rakupp
