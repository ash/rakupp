# Laziness, Junctions, and the Wilder Values

Three parts of Raku resist a straightforward tree-walker: infinite lists,
values that are several values at once, and `gather`/`take`, which wants a
coroutine. None of the three gets a new `VT`. All three are an existing tag plus
some state.

## Lazy and infinite sequences

An infinite list like `1, 2, 4 ... *` or `(1..Inf).map(*²)` obviously cannot be
a materialised `ValueList`. Raku++ attaches a generator to an otherwise ordinary
array value.

```cpp
// src/Interpreter.h
struct LazySeqState {
    std::function<bool(ValueList&)> appendNext;  // one more; false = exhausted
    bool infinite = false;   // truly unbounded: elems/pop/[*-1] must die
};
```

The already-computed **prefix** lives in the `Value`'s `arr`; the generator
lives in the opaque `ext` slot — the same slot a `Promise` or a `Channel` would
use. `appendNext` appends exactly one element and reports whether more exist.

### Building one

`seqOp` implements the `...` operator. It splits the left side into a seed list
— a trailing `Code` seed becomes the *generator* — and classifies the right
endpoint. A bounded endpoint (`1 ... 10`) is computed eagerly in a loop, capped
at a million. An infinite endpoint builds a `LazySeqState`:

```cpp
// src/Interpreter.cpp — seqOp
if (infinite) {
    auto st = std::make_shared<LazySeqState>();
    st->infinite = true;
    st->appendNext = [...](ValueList& cache) -> bool {
        /* feed the last `arity` cached elements to the generator, or step
           the detected progression; push the next value */
    };
    out.ext = st;
}
```

When there is no explicit generator, the operator *detects the progression*
from the seed: a constant arithmetic difference, a constant geometric ratio, or
— for strings — repeated `succ`/`pred`. That is Raku's rule, and it is why
`1, 3, 5 ... 99` and `'a', 'b' ... 'z'` both work with no closure written.

`...` is also **list-associative**: `1 ... 5 ... 1` and `'A'...'Z', 'a'...'z'`
are one operator over a list of lists, where each group's first element closes
the previous segment. That is `seqOpGroups`, shared with the compiled backend.

A bare `1..Inf` assigned to an array builds a simpler counting generator, in
`coerceArray`:

```cpp
auto st = std::make_shared<LazySeqState>(); st->infinite = true;
auto next = std::make_shared<long long>(start);
st->appendNext = [next](ValueList& cache) -> bool {
    cache.push_back(Value::integer((*next)++)); return true;
};
a.ext = st;
```

### Forcing elements

Consumers grow the prefix on demand:

```cpp
// src/Interpreter.cpp
void Interpreter::materializeLazy(const Value& v, size_t n) {
    auto st = std::static_pointer_cast<LazySeqState>(v.ext);
    while (v.arr->size() < n && v.arr->size() < CAP)
        if (!st->appendNext(*v.arr)) break;
}
```

So `@lazy[5]` materialises six elements and then indexes; `.head(3)`
materialises three; `.first(&pred)` pulls one at a time until the predicate
matches.

Operations that need the *end* of an infinite list cannot complete, and say so:

```cpp
// src/Builtins.cpp
if (m == "elems" || m == "end" || m == "pop" || m == "tail" ||
    m == "reverse" || m == "sort" || m == "sum" || m == "min" ||
    m == "max" || m == "join" || m == "Str" || m == "gist")
    throw RakuError{Value::typeObj("X::Cannot::Lazy"),
                    "Cannot " + m + " a lazy list onto an Array"};
```

A *finite* lazy value — a `gather` that outgrew its probe — instead forces full
materialisation, which is the right answer for it.

### Lazy `.map` and `.grep`

`.map` over a lazy source builds a **new** `LazySeqState` that pulls from the
source on demand, which is what makes `(1..Inf).grep(*.is-prime).head(5)`
terminate:

```cpp
// src/Builtins.cpp — .map over a lazy source
st->appendNext = [self, src, fn](ValueList& cache) -> bool {
    size_t si = cache.size();
    self->materializeLazy(src, si + 1);          // pull one more
    if (si >= src.arr->size()) return false;
    cache.push_back(self->callCallable(fn, { (*src.arr)[si] }));
    return true;
};
```

`.grep` loops pulling source elements until its predicate matches; `.skip`
builds a shifted view.

### The cap

Every materialisation path shares a hard ceiling of **one million elements**. It
is a runaway safety net, not a semantic limit, and it means "infinite" is
bounded in practice. A consumer that genuinely needs the millionth element of a
generated sequence will get it; one that needs the ten-millionth will not.

`Value::flatten()` has a tighter one for an endless `Range`: ten thousand
elements. The next section is about what that ceiling cost, and what replaced
it.

### Answering without the elements

A ceiling turns a non-terminating operation into a terminating one, which is
what it is for. It also turns a question the implementation cannot answer into
one it answers *wrongly*:

```
$ rakupp -e 'say [+] 1..Inf'      # before
50005000
```

That is the sum of `1..10000` — the prefix, folded and handed back as the total,
with nothing in the output to mark it partial. A reduce is written to consume
its list element by element, and an endless list has no end to consume to, so
the fold has no *value*. But its partial results usually have a **limit**, and
that limit follows from the range's endpoints without touching a single element:

| | | |
|---|---|---|
| `[+] 1..Inf` | `Inf` | the partial sums 1, 3, 6, 10, … are unbounded |
| `[*] 1..Inf` | `Inf` | so are the partial products |
| `[*] ^Inf` | `0` | zero is absorbing: every partial from there on is 0 |
| `[*] -0.5..Inf` | `-Inf` | one negative element, and the walk misses zero |
| `[-] 1..Inf` | `-Inf` | the tail it subtracts runs away |
| `[min] 1..Inf` | `1` | the partial minima settle on the low bound at once |
| `[max] 1..Inf` | `Inf` | the partial maxima chase the high one |
| `[~] 1..Inf` | `X::Cannot::Lazy` | the strings grow without approaching a string |

The rule the table follows: **answer with the limit of the partial folds when
the bounds fix it; otherwise say it cannot be done; never fold a prefix and
present it as the whole.** `~` has no limit to name in the string domain, and a
user-supplied operator has no known one at all, so both refuse.

The sign rule for `*` is the one case that needs care. A `Range` is bounded
below, so it has finitely many negative elements, and their count settles the
sign of an otherwise unbounded product — three of them in `-2.5..Inf`, hence
`-Inf`. Zero overrides all of it, but only when the walk actually lands on zero:
`-3..Inf` does, `-2.5..Inf` steps straight from `-0.5` to `0.5` and does not.

The check lives in one function, `endlessReduce`, and every entry point calls it
*before* flattening — the `[op]` metaoperator, `prefix:<[op]>(…)`, an
`&prefix:<[op]>` reference, `.reduce`, and `rtReduce`, the one the native code
generator emits, so a compiled binary agrees with the interpreter. `.sum` and
the `sum` builtin answer from the bounds too, rather than from a truncated
`toList`.

An endless **lazy** list is a different matter: its elements do not follow from
any bounds, so there is nothing to compute a limit from and every operator
refuses. A *merely* lazy one — finite, but holding only the prefix something has
pulled so far — must not be folded from its cache either, or `[+] @lazy` answers
for whatever happened to be materialised. It is drained first, and refused only
if it will not drain:

```cpp
// src/Interpreter.cpp — endlessReduce, the lazy arm
auto st = std::static_pointer_cast<LazySeqState>(v.ext);
const size_t CAP = 1000000;                       // materializeLazy's ceiling
if (st->appendNext)
    while (v.arr->size() < CAP && st->appendNext(*v.arr)) {}
if (v.arr->size() < CAP && !isEndlessLazy(v)) return false;  // drained: fold it
throw RakuError{Value::typeObj("X::Cannot::Lazy"), "Cannot reduce a lazy list"};
```

Whether it will drain cannot be decided when the view is built. A `.map` over an
endless source is endless, always — it inherits the flag. A `.grep` over one may
still end, because the predicate can stop it:

```raku
(^Inf).grep({ last if $_ > 5; True }).eager.join   # 012345
```

That is Roast's own test, so a grep view is *not* born endless. Instead it
learns: if `appendNext` ever finds the source has stopped growing because the
source is endless and hit its ceiling, the view marks itself endless from that
point on, and the second `isEndlessLazy` check above — the one after the drain —
sees it.

### One sentinel, two meanings

A `Range` keeps its endpoints in two `long long`s, and an endless one parks
`±LLONG_MAX` in them. So does a range whose endpoint is merely too large for a
`long long`, which made `1..10**100` indistinguishable from `1..Inf` and gave it
the prefix treatment as well. The written endpoint settles it — a big-`Int`
bound in `Value::big`, a real infinity or a `Whatever` in `RangeEnds` — and a
range of integers is summed by Gauss rather than walked:

```cpp
Value count = applyArith("+", applyArith("-", hi, lo), Value::integer(1));
return applyArith("div", applyArith("*", count, applyArith("+", lo, hi)),
                  Value::integer(2));
```

`count × (lo + hi) / 2`, in the exact integer tower, so `(1..10**100).sum` is a
200-digit answer and `(1..10).sum` is still `55`. Roast's
`S03-operators/range-int.t` asserts both.

Two paths still walk the prefix, deliberately. `[\+] 1..Inf`, the triangular
form, produces a *list* rather than a fold, and stops at ten thousand partial
sums instead of building a lazy sequence of them. And `.flat` over a list
containing an endless range materialises the prefix without marking the result
endless, so a reduce downstream of it never learns that anything ran away.

## `gather` and `take`, without coroutines

`gather { … take … }` should suspend the block at each `take` and resume it when
another element is demanded. That is a coroutine, and a tree-walker built on the
C++ stack does not have one.

The strategy is **probe and double**. The collector and a per-gather element cap
live on the execution context:

```cpp
// src/Interpreter.h — ExecContext
std::vector<std::shared_ptr<ValueList>> gatherStack;
std::vector<size_t> gatherLimits;   // 0 = unlimited
```

The block is first run under a small cap of 64. If it finishes within the cap it
was finite, and the result is returned eagerly. If the cap was *hit*, the result
becomes a `LazySeqState` that grows by **re-running the block** with a larger
cap:

```cpp
// src/Interpreter.cpp
st->appendNext = [this, runGather](ValueList& out) -> bool {
    ValueList grown;
    bool more = runGather(out.size() + std::max<size_t>(64, out.size()), grown);
    for (size_t i = out.size(); i < grown.size(); i++) out.push_back(grown[i]);
    return more;
};
```

Doubling keeps the re-run cost amortised linear. A `take` that pushes past the
current cap unwinds the block with an empty marker exception:

```cpp
// src/Builtins.cpp — take
auto& coll = *tctx_.gatherStack.back();
for (auto& x : a) coll.push_back(x);
if (lim && coll.size() >= lim) throw StopGatherEx{};
```

So an infinite `gather` runs its block only far enough to satisfy each demand,
stops, and re-enters later for more.

The honest cost of this design: **the block runs more than once, from the
start.** A `gather` whose block has side effects — printing, or mutating a
counter — will repeat them. That is a genuine divergence from a coroutine
implementation, and the reason the initial probe cap is generous enough that
most real `gather`s never re-run at all.

## Junctions

A junction has no `VT` of its own. `any(1, 2, 3)` is a `VT::Array` whose
elements are the eigenstates, tagged by `enumName`:

```cpp
// src/Value.cpp — typeName(), the VT::Array case
if (enumName == "any" || enumName == "all" ||
    enumName == "one" || enumName == "none") return "Junction";
```

They are built by the `any`/`all`/`one`/`none` routines, the matching methods,
and the `|`, `&`, `^` infix operators.

**Autothreading** — distributing an operation over the eigenstates and
recombining — happens at each place a value is consumed, and there are four such
places.

**Operators.** A comparison *collapses* to a single `Bool` according to the
junction type; any other operator produces a **new** junction of the
per-eigenstate results:

```cpp
// src/Interpreter.cpp — applyArith
Value out = Value::array(); out.enumName = j.enumName;
for (auto& e : *j.arr)
    out.arr->push_back(applyArith(op, jleft ? e : l, jleft ? r : e));
return out;                       // any(1,2) + 10  ==>  any(11, 12)
```

**Method calls**, with a small allow-list that acts on the whole junction.
**Callable invocation**, in `callCallableRaw`. **Smartmatch**, in `~~`.

That is why `if 3 == any(1, 2, 3)` works: the `==` sees a junction on the right,
threads the comparison across the eigenstates, and collapses `any` to `True`.

One escape hatch exists, because `Junction.THREAD` must pass each eigenstate —
junctions included — through whole:

```cpp
// src/Interpreter.h
static thread_local bool noAutothread_;   // one-shot, consumed by the next call
```

## `Whatever` and `WhateverCode`

`*` is `VT::Whatever`. An expression containing one *curries* into a
`WhateverCode` — a `Callable` with a flag and an arity:

```cpp
// src/Value.h — Callable
bool isWhateverCode = false;
long long whateverArity = 0;   // `* + *` consumes 2
```

so `* + 1` becomes a one-argument closure and `* + *` a two-argument one.
Composition works because currying an expression that already contains a
`WhateverCode` produces another one.

Deciding whether to curry is a syntactic question, answered by walking the
expression for a literal `*`:

```cpp
// src/Interpreter.h
static bool exprHasWhateverLit(const Expr* e);
```

In *subscript* position `*` means something else entirely — `@a[*-1]`,
`@a[*]` — and is handled by a dedicated path, `idxW`, rather than by currying.

## Hyper operators

`»op«` and friends apply an operator element-wise, with rules about which side
may be extended. All spellings — `>>op<<`, `»op«`, `>>[&op]<<` — funnel into one
core:

```cpp
// src/Interpreter.h
Value hyperCore(Value& l, Value& r, bool strictL, bool strictR,
                const std::function<Value(const Value&, const Value&,
                                          Value*, Value*)>& apply,
                Value* lroot = nullptr, Value* rroot = nullptr,
                bool wantSlots = false);
Value hyperUnary(const std::string& op, Value v);         // -«(…)
Value hyperPostfixApply(const std::string& op, Value v);  // @a»++
```

The `strictL`/`strictR` flags are the dwimmy-versus-strict distinction Raku
draws between `<<` and `>>` on each side; the slot parameters exist because
`@a»++` must write *through* to the elements, which is the same write-back
problem as an `is rw` parameter and uses the same one-shot register.

## Flip-flops

`ff` and `fff` need per-*site* state — the same operator at two places in a
program has two independent latches:

```cpp
// src/Interpreter.h
struct FlipFlop { bool on = false; long long seq = 0; };
std::unordered_map<const void*, FlipFlop> ffState_;
```

keyed on the AST node's address, which is stable for the life of the program.
The `seq` counter is there because the result while the latch is on is the
*count of elements since it fired*, not a plain `Bool`.

This is the one place in the interpreter where a map keyed on a node pointer is
the right answer rather than a hazard. It works here because AST nodes are never
freed. Chapter 23 describes a case where the same idea failed for exactly the
opposite reason — freed addresses being recycled.
