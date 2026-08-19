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
// src/Interpreter.cpp — evalCall, abridged
if (Value* f = tctx_.cur->find("&" + c->name))
    return callCallable(*f, std::move(args), &c->args, false, …);
…
auto it = builtins_.find(c->name);        // built-in fallback
```

The environment first, walking the parent chain; the builtin table second. That
order is the whole rule, and it has three consequences worth spelling out.

**A lexical `my sub` shadows a builtin.** That is Raku's semantics and it comes
for free.

**A module's exported sub also shadows a builtin**, because the loader copies it
into the global environment, which is the last link of the chain (Chapter 31).

**A compiled program must reproduce this order**, which it cannot do by resolving
names against the builtin table at compile time. That is a real bug that
happened — `--exe` printed the built-in `val`'s answer where the interpreter
called a module's exported `val` — and Chapter 27 describes the fix.

The argument *expressions* are passed alongside the values (`&c->args`) so that
`is rw` write-back can re-resolve them.

## Multiple dispatch

A `multi sub` or `multi method` is one `Callable` with `isMultiDispatcher` set
and a `candidates` vector; each `multi` declaration pushes its `Code` onto it. A
`proto` declares the group without being a candidate.

At call time, `scoreCandidate` scores every candidate against the actual
arguments, returning `-1` for "does not apply" or a non-negative **specificity**:

```cpp
// src/Interpreter.cpp — scoreCandidate, per positional parameter
if (subsets_.count(p->type)) {
    if (!subsetMatches(p->type, pos[i])) return -1;
    score += 2;
} else if (!typeMatchesArg(pos[i], p->type)) return -1;
if (p->defConstraint == 1 && !isDefined(pos[i])) return -1;   // :D
if (p->defConstraint == 2 &&  isDefined(pos[i])) return -1;   // :U
if (p->defConstraint) score++;
if (!p->type.empty() && p->type != "Any" && p->type != "Mu") {
    score++;                                    // constrained beats not
    if (p->type == pos[i].typeName()) score++;  // exact beats supertype
}
```

Arity gates run first — too few required arguments, or too many for a
non-slurpy signature, is an immediate `-1`. Literal parameters
(`multi fact(0)`) and sub-signature destructuring are treated as very specific.
A required named parameter that was not supplied is `-1`.

The winner is the highest score:

```cpp
const Value* best = nullptr; int bestScore = -1;
for (auto& cand : c.candidates) {
    int s = scoreCandidate(cand, args);
    if (s > bestScore) { bestScore = s; best = &cand; }   // strict >
}
if (!best || bestScore < 0) throw /* X::Multi::NoMatch */;
```

**Two honest notes.** The comparison is a strict `>`, so on equal scores the
*first-declared* candidate wins and there is no `X::Multi::Ambiguous`
diagnostic. And a genuine no-match does throw `X::Multi::NoMatch`, with the
candidate list in the message.

### Subsets

A `subset` is a refinement type that participates in dispatch:

```cpp
// src/Interpreter.h
struct SubsetInfo { std::string base; const Expr* where = nullptr; };
std::unordered_map<std::string, SubsetInfo> subsets_;
bool subsetMatches(const std::string& name, const Value& v, int depth = 0);
```

`subsetMatches` checks the base type and then evaluates the `where` expression
with the value as the topic. The `depth` parameter is a recursion guard, because
a subset can be defined in terms of another subset and nothing prevents a cycle.

A subset match scores `+2`, above a plain nominal match, which is what makes
`multi f(Even $n)` beat `multi f(Int $n)`.

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
where the two differ it can pick a different method than Rakudo. The `owner`
out-parameter reports which class the method was found in, so re-dispatch can
resume from that class's parent.

**Built-in values** go through `methodCall`, a very long cascade of branches
keyed on the method name and, inside each branch, on the invocant's tag:

```cpp
if (m == "uc")       return Value::str(mapCase(inv.toStr(), true, 0));
if (m == "chars")    return Value::integer(graphemeCount(inv.toStr()));
if (m == "is-prime") { /* Miller–Rabin on inv.toInt() */ }
// … some 1,640 more …
```

The C++ `if`-ladder *is* the built-in method set. That is the pragmatic
counterpart to the fat `Value`: since every native value is the same struct, its
methods are one dispatch function rather than per-type classes.

The ladder is split across four files as ordered segments (Chapter 2), each
returning `std::optional<Value>`. The name is wrapped in an `MName` at the top
so the comparisons are integer compares (Chapter 9).

### What runs before the ladder

Two things are consulted first, and both are bridges between the two worlds.

**Junction autothreading.** If the invocant is a junction, a small allow-list of
methods (`Bool`, `gist`, `new`, …) act on the whole junction; everything else
autothreads, returning a junction of the per-eigenstate results.

**`augment` on a built-in type.** Methods a program adds to a built-in with
`augment class Int { … }` are parked in a map of maps and consulted before the
native branches, so they can override built-ins:

```cpp
// src/Builtins.cpp — before the native branches
if (!builtinExt_.empty() && inv.t != VT::Object) {
    std::string tn = inv.t == VT::Type ? inv.s : inv.typeName();
    if (Value* f = lookup(tn))
        return invokeMethod(*f, inv, std::move(args), rwArgs);
    for (const std::string& anc : typeAncestry(tn))
        if (anc != tn) if (Value* f = lookup(anc))
            return invokeMethod(*f, inv, …);
}
```

The ancestry walk is what makes `augment class Cool { … }` reach an `Int` and a
`Str`. The guard on `builtinExt_.empty()` is why a program that never augments
anything pays one branch.

This is also why two of the inline built-in fast paths in Chapter 27 are
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
};
static thread_local std::vector<RedispatchCtx> redispatchStack_;
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

The stack is `static thread_local` for the reason given in Chapter 13: as a
plain member it was written by every dispatching call on every thread.

## Private methods, qualified calls, indirect calls

The `MethodCall` node carries the variants as flags:

| Written | Flag | Meaning |
|---|---|---|
| `$o.m` | — | ordinary dispatch |
| `$o.?m` | `maybe` | return `Nil` rather than dying when absent |
| `$o!m` | `bang` | private method, reachable only through `self!m` |
| `$o.Class::m` | `methodQual` | dispatch to a specific class's method, past overrides |
| `$o."$name"()` | `methodExpr` | the name is computed at run time |
| `$o.=m` | `mutate` | call and assign back to the invocant |
| `$o>>.m` | `hyper` | apply to each element |
| `$o.^m` | `meta` | a meta-object call |

`.^` calls go to the meta-object. Most are answered directly — `.^name`,
`.^methods`, `.^attributes`, `.^parents`, `.^roles` — but `.HOW` must return a
*persistent* object, because `T.HOW does SomeRole` mixins have to stick. So
`ClassInfo` carries its meta-object rather than building one per call:

```cpp
// src/Value.h — ClassInfo
Value howObj;   // the persistent .HOW metaobject
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

After the two fixes in Chapters 9 and 10, a method call on a built-in costs
roughly three times what Rakudo charges, down from about twenty. The profile of
`for ^6000000 { "ab".uc }`:

| | share |
|---|---:|
| heap allocate and free | 31% |
| `Value` copy and destroy | 11% |
| method-name comparison | 8.5% |
| the dispatch function's own body | 6.1% |

The 42% in allocation and value churn is the remaining target, and it has a
known shape: both `methodCall` and `methodCallInner` take their invocant and
argument list **by value**, so every call copies a `Value` — with its eleven
`shared_ptr` members — and heap-allocates a `ValueList`.

Switching both to `const&` is mechanically small: eighteen compile errors, each
either "make a local copy here" or "let this helper take `const&`". It has not
been done because it trades away the accidental safety that copying provides
against a callee mutating the container its own invocant lives in. That wants an
aliasing audit of 178 call sites, not a quick pass.

## Honest limitations

- **The method resolution order is depth-first, not C3.** Diamond hierarchies
  can resolve differently from Rakudo.
- **No `X::Multi::Ambiguous`.** Equal-specificity candidates resolve silently to
  the first declared.
- **Role composition is last-writer-wins.** Composing two roles that define the
  same method copies both into the table with no conflict diagnostic.
- **The ladder's order is load-bearing and undocumented in the code beyond a
  warning.** An arm moved for readability is a behaviour change.
