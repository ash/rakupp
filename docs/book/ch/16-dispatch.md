# Dispatch

Raku has more dispatch than most languages: named subs, multiple dispatch on
argument types, method dispatch on an invocant, private methods, meta-object
calls, and a re-dispatch protocol (`callsame`, `nextsame`, `callwith`,
`nextwith`, `samewith`) that lets a routine hand control to the next candidate.

Raku++ implements them as **three distinct mechanisms** that meet at the edges,
and knowing which one is in play explains most of the surprises.

## Named calls

`evalCall` resolves every named call the same way:

```cpp
// src/InterpreterCore.cpp — evalCall, abridged
if (Value* f = tctx().cur->find(callAmpName(c))) {    // "&" + the name
    …
    return callCallable(*f, std::move(args), &c->args,
                        /*ownFrame=*/false, /*arityCheck=*/true);
}
…
auto it = builtins_.find(c->name);        // built-in fallback
```

The environment first, walking the parent chain; the builtin table second. That
order is the whole rule, and it has three consequences worth spelling out.

**A lexical `my sub` shadows a builtin.** That is Raku's semantics and it comes
for free.

**A module's exported sub also shadows a builtin**, because the loader copies it
into the importing scope. For a `use` at the mainline that is the global
environment, the last link of the chain; for a `use` inside a block it is the
block's own scope, and nothing is left behind after it (Chapter 33).

**A compiled program must reproduce this order**, which it cannot do by resolving
names against the builtin table at compile time. That is a real bug that
happened — `--exe` printed the built-in `val`'s answer where the interpreter
called a module's exported `val` — and Chapter 28 describes the fix.

The argument *expressions* are passed alongside the values (`&c->args`) so that
`is rw` write-back can re-resolve them.

## Multiple dispatch

A `multi sub` or `multi method` is one `Callable` with `isMultiDispatcher` set
and a `candidates` vector; each `multi` declaration pushes its `Code` onto it. A
`proto` declares the group without being a candidate.

At call time, `scoreCandidate` scores a candidate against the actual
arguments, returning `-1` for "does not apply" or a non-negative
**specificity**. It also fills a vector with one score per parameter:

```cpp
// src/Interpreter.h
int scoreCandidate(const Value& cand, const ValueList& args,
                   std::vector<int>* perParam = nullptr,
                   const Value* selfForWhere = nullptr,
                   WhereAssume* assume = nullptr);
```

```cpp
// src/InterpreterCore.cpp — scoreCandidate, a typed positional, abridged
if (subsets_.count(p->type)) {
    if (!subsetMatches(p->type, pos[i])) return -1;
    score += 12; // a satisfied subset is narrower than its base nominal
}
…
else if (!typeMatchesArg(pos[i], /* p->type, through its alias */)) return -1;
…
if (p->defConstraint == 1 && !isDefinite(pos[i])) return -1;   // :D
if (p->defConstraint == 2 && isDefinite(pos[i])) return -1;    // :U
if (p->defConstraint) { score += 2; … }   // a smiley is more specific
…
else if (!p->type.empty() && p->type != "Any" && p->type != "Mu") {
    score += 8;              // a NOMINAL type outranks a bare @/% sigil
    if (/* the type is the argument's own */)
        score += 2;          // exact type beats a supertype
}
…
score += 4; // a satisfied where-constraint is more specific
```

Arity gates run first — too few required arguments, or too many for a
non-slurpy signature, is an immediate `-1`. Literal parameters
(`multi fact(0)`) score 16, the narrowest thing there is, and sub-signature
destructuring is treated as specific too. A required named parameter that was
not supplied is `-1`.

The sum alone does not pick the winner. Rakudo compares candidates
**per parameter**: one is preferred only when it is no wider anywhere and
narrower somewhere, and a sum cannot say that — two `Str:D` parameters
outscored one literal. So the loop asks `candidateBeats`:

```cpp
// src/InterpreterCore.cpp — the dispatcher arm of callCallableRaw, abridged
for (auto& cand : c.cold().candidates) {
    if (/* cand is the proto */) continue;
    …
    int s = scoreCandidate(cand, as, &vec, nullptr, …);
    …
    if (candidateBeats(*this, cand, vec, s, candRw,
                       best, bestVec, bestScore, bestRw))
        { bestScore = s; best = &cand; bestVec = vec; bestRw = candRw; }
    if (s >= 0 && nMatched < 8) matched[nMatched++] = &cand;
}
if (!cached && best && visited.empty() && nMatched > 1)
    throwIfAmbiguous(c, best, matched, nMatched, as);
```

`candidateBeats` ranks by type first, as Rakudo sorts its candidates: for two
candidates of the same positional arity and no slurpy, a strictly narrower
nominal type wins over a wider one that carries a `where` or a smiley. Then
the per-parameter vectors are compared, then `is default` and an `is rw`
binding break a tie. Two candidates that each win a different parameter sit
in the same band, and declaration order settles it.

**Two honest notes, both changed since this chapter was first written.**
Equal candidates used to resolve silently to the first declared. Two
candidates that nothing separates are now `X::Multi::Ambiguous`, with the
tied signatures in the message; `throwIfAmbiguous` judges only signatures of
plain typed `$` positionals, so a pair it cannot judge still falls to
declaration order. And a genuine no-match throws `X::Multi::NoMatch`, with
the candidate list in the message, unless the name is a built-in's: a user
`multi infix:<+>` or `multi sub is-prime` *adds* candidates to the built-in,
which still answers a call none of them takes.

### The dispatch cache

Scoring every candidate at every call is the cost that made `multi` slow, so
a dispatcher remembers. `Callable::dispatchCache` holds, for one argument
shape, the candidate that won it, and a later call with the same shape goes
straight there. It is sound only where the winner cannot depend on anything
but the shape, so both sides are narrow. The arguments must each be a plain
Int, Num or Str or an instance of a class, keyed by kind and `ClassInfo`. The
candidates must have only `$` positionals typed by a name whose match depends
on that key alone — no literal, coercion, capture, sub-signature, default,
`is rw` or native type. An entry is good while the candidate count and the
symbol generation are what they were when it was published.

A `where` on a positional does not rule the cache out. The winner then
depends on the values, but only through the outcomes of the `where`s, so a
where-aware entry keeps a table from those outcomes to the winner. A call
evaluates the `where`s, and an outcome pattern the table has not seen runs
the full dispatch once and fills the slot. Such an entry covers at most 8
candidates and 6 `where`s. When the cache landed, the `multimeth` kernel ran
52% faster and `multiwhere` 46% (`docs/dev/plans/INTERP-SPEED-PLAN.md`,
task 6).

### Subsets

A `subset` is a refinement type that participates in dispatch:

```cpp
// src/Interpreter.h
struct SubsetInfo { std::string base; const Expr* where = nullptr; /* … */
                    std::shared_ptr<Env> declEnv; };
StrMap<SubsetInfo> subsets_;
bool subsetMatches(const std::string& name, const Value& v, int depth = 0);
```

`subsetMatches` checks the base type and then evaluates the `where` expression
with the value as the topic. The `depth` parameter is a recursion guard, because
a subset can be defined in terms of another subset and nothing prevents a cycle.

A subset match scores 12, above a plain nominal match's 8, or 10 for the
argument's exact type, which is what makes `multi f(Even $n)` beat `multi f(Int $n)`. The `where` closes over the
scope the subset was declared in, which is what `declEnv` keeps.

## Method dispatch: two worlds

An `Int`, a `Str` or an `Array` is a native `Value` — it has no `ClassInfo` and
no method table. A user class instance is a `VT::Object` with both. So there are
two dispatch worlds.

**User objects** go through `ClassInfo::findMethod`, which *is* the method
resolution order:

```cpp
// src/Value.h
Value* findMethod(const std::string& m, ClassInfo** owner) {
    auto it = methods.find(m);
    if (it != methods.end()) { if (owner) *owner = this; return &it->second; }
    if (parent) { if (Value* r = parent->findMethod(m, owner)) return r; }
    for (auto& p : extraParents) if (p)
        { if (Value* r = p->findMethod(m, owner)) return r; }
    if (owner) *owner = nullptr;
    return nullptr;
}
```

Own methods, then the first parent recursively, then each extra parent. It is a
depth-first walk, **not C3 linearisation**, and for the diamond hierarchies
where the two differ it can pick a different method than Rakudo. `.^mro` does
answer the C3 order, so on such a diamond the two disagree: with `B is A`,
`C is A` overriding `m`, and `D is B is C`, `D.^mro` lists `C` before `A` and
`D.new.m` still runs `A`'s. The `owner` out-parameter reports which class the
method was found in, so re-dispatch can resume from that class's parent.

**Built-in values** go through `methodCall`, a very long cascade of branches
keyed on the method name and, inside each branch, on the invocant's tag:

```cpp
if (m == "uc") return Value::str(mapCase(inv.toStr(), 1, 0));
if (m == "codes") return Value::integer(cpCount(inv.toStr()));
if (m == "is-prime") { /* … */ }
// … some 1,640 more, when they were counted for Chapter 10 …
```

The C++ `if`-ladder *is* the built-in method set. That is the pragmatic
counterpart to the fat `Value`: since every native value is the same struct, its
methods are one dispatch function rather than per-type classes.

The ladder is split across files as ordered segments (Chapter 2) — six
`src/MethodCall*.cpp` files and the tail of `src/Builtins.cpp` — each
returning `std::optional<Value>`. The name is wrapped in an `MName` at the top
so the comparisons are integer compares (Chapter 10).

### What runs before the ladder

Two things are consulted first, and both are bridges between the two worlds.

**Junction autothreading.** If the invocant is a junction, a small allow-list of
methods (`Bool`, `gist`, `new`, …) act on the whole junction; everything else
autothreads, returning a junction of the per-eigenstate results.

**`augment` on a built-in type.** Methods a program adds to a built-in with
`augment class Int { … }` are parked in a map of maps and consulted before the
native branches, so they can override built-ins:

```cpp
// src/Builtins.cpp — builtinExtMethod, asked before the native branches
if (builtinExt_.empty() || inv.t == VT::Object) return nullptr;
std::string tn = inv.t == VT::Type ? inv.s.copyText() : inv.typeName();
…
if (Value* f = lookup(tn)) return f;
for (const std::string& anc : typeAncestry(tn))
    if (anc != tn) if (Value* f = lookup(anc)) return f;
return nullptr;
```

The ancestry walk is what makes `augment class Cool { … }` reach an `Int` and a
`Str`. The guard on `builtinExt_.empty()` is why a program that never augments
anything pays one branch.

This is also why two of the inline built-in fast paths in Chapter 28 are
`builtinExt_`-guarded: an inlined `abs` must stop inlining the moment a program
augments `Int`.

## Re-dispatch

`callsame`, `nextsame`, `callwith`, `nextwith` and `samewith` need to know what
the *next* candidate is, and that differs by context: the next-less-specific
multi candidate, the same method on the owning class's parent, or a built-in
that a user method shadowed.

```cpp
// src/Interpreter.h
struct RedispatchCtx {
    std::function<Value(ValueList)> next;      // callsame / nextsame / …with
    std::function<Value(ValueList)> restart;   // samewith
    ValueList sameArgs;
    bool lastcall = false; bool fromChain = false;
    …
};
static std::vector<RedispatchCtx>& redispatchStack();   // this thread's chain
```

Each dispatch site that can be re-entered pushes a context knowing how to invoke
the next thing, and the original arguments for the `*same` variants.
`invokeMethodChain` is the method version: it invokes a method found from a
given class and pushes a context that resumes from that class's parent,
recursively.

One field guards a subtle case:

```cpp
// src/Interpreter.h — ExecContext
size_t redispatchFloor = 0;   // frames below this are another routine's
```

Without it, a `callsame` inside a routine could reach a re-dispatch context
belonging to a *caller*, and dispatch into something unrelated.

The stack is per thread for the reason given in Chapter 14: as a plain member
it was written by every dispatching call on every thread. It is read on every
call, so it is reached the way `tctx()` is, through a constant-initialized
`thread_local` pointer to the thread's own vector.

One case pushes no context at all. A user method that overrides a built-in —
`method clone { … callsame … }` — has no user candidate under it, and pushing
a frame for every such call would cost each one a pair of closures and a copy
of its arguments when almost none redispatch. The activation leaves a
breadcrumb instead, `ExecContext::builtinFallback`: three borrowed pointers to
the name, the invocant and the arguments, read only when no visible frame
answers.

## Private methods, qualified calls, indirect calls

The `MethodCall` node carries the variants as flags:

| Written | Flag | Meaning |
|---|---|---|
| `$o.m` | — | ordinary dispatch |
| `$o.?m` | `maybe` | return `Nil` rather than dying when absent |
| `$o.+m`, `$o.*m` | `allMode` | every candidate's result, as a List |
| `$o!m` | `bang` | private method, reachable only through `self!m` |
| `$o.Class::m` | `methodQual` | dispatch to a specific class's method, past overrides |
| `$o."$name"()` | `methodExpr` | the name is computed at run time |
| `$o.=m` | `mutate` | call and assign back to the invocant |
| `$o>>.m` | `hyper` | apply to each element |
| `$o.^m` | `meta` | a meta-object call |

The flags combine: `self!"$name"()` is `bang` *and* `methodExpr`, and the name is
not known until the expression has been evaluated — so the check that a private
call is lexically inside its class runs after that, not before the invocant. The
private branch used to take the interpolation's raw source text as the name and
dispatch to a method spelled `!$name` (issue #43).

`.^` calls go to the meta-object. Most are answered directly — `.^name`,
`.^methods`, `.^attributes`, `.^parents`, `.^roles` — but `.HOW` must return a
*persistent* object, because `T.HOW does SomeRole` mixins have to stick. So
`ClassInfo` carries its meta-object rather than building one per call:

```cpp
// src/Value.h — ClassInfo
Value howObj;   // persistent .HOW metaobject
```

## `&builtin` as a value

`&say` must be a `Callable` value, and `&dir.wrap({…})` must mutate *the*
`Callable` the call path consults — that is exactly how `File::Find`'s test
suite mocks `dir`. So a reference to a builtin is memoised:

```cpp
// src/Interpreter.h
std::map<std::string, Value> builtinRefs_;
```

populated lazily, and empty for programs that never take a builtin reference —
which keeps the check on the call path free.

## Where the time goes

After the two fixes in Chapters 10 and 11, a method call on a built-in costs
roughly three times what Rakudo charges, down from about twenty. The profile of
`for ^6000000 { "ab".uc }`:

| | share |
|---|---:|
| heap allocate and free | 31% |
| `Value` copy and destroy | 11% |
| method-name comparison | 8.5% |
| the dispatch function's own body | 6.1% |

The 42% in allocation and value churn was the remaining target when this
profile was taken, and it had a known shape: both `methodCall` and
`methodCallInner` take their invocant and argument list **by value**, so every
call copies a `Value` and allocates a `ValueList`.

All three costs in that sentence have since shrunk. The invocant is no longer
copied: both functions take `const Value& inv` (commit d880fe9c, 2026-07-29).
Four arms out of 352 rewrote their invocant, and each now takes its own copy;
the compiler enforces the rest, since an arm that quietly mutates the invocant
no longer compiles. On a 3M-method-call workload that measured −3.4%. A
`Value` carried eleven `shared_ptr` members then; Chapter 41 took that to two,
and since 2026-11-10 the whole struct is 32 bytes on 64-bit macOS and Linux
(`docs/dev/plans/VALUE32-PLAN.md`), so the copies that remain are a fraction
of what they were. And the `ValueList` for a short argument list no longer
reaches the allocator, because small blocks come off a free list (Chapter 12).

The argument list is still passed by value. Changing that trades away the
accidental safety that copying provides against a callee mutating the
container its own arguments live in, and the cost it would recover has been
reduced by changes that needed no aliasing audit at all.

## Honest limitations

- **The method resolution order is depth-first, not C3.** Diamond hierarchies
  can resolve differently from Rakudo.
- **`X::Multi::Ambiguous` is raised only where the tie can be judged**: two
  signatures of plain typed `$` positionals that nothing separates. Other
  equal candidates still resolve to the first declared.
- **Role conflicts are diagnosed**, so this is no longer a limitation: composing
  two roles that define the same method raises
  `X::Role::Unresolved::Method` unless the class resolves it, as in Rakudo.
- **The ladder's order is load-bearing and undocumented in the code beyond a
  warning.** An arm moved for readability is a behaviour change.
