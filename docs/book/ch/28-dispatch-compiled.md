# Dispatch Cost in Compiled Code

A compiled Raku++ program reaches code four different ways, and they are not
equally fast. This chapter measures each, describes the three cuts made to them,
and — because it is the more useful part — records the analysis that turned out
to be wrong.

Method: micro-benchmarks against the built runtime, 2 million iterations per
shape, seven repetitions, first discarded, minimum reported.

## The four shapes

For `sub square($n) { $n * $n }` plus `say square(5)`, the generated C++
contains all four:

| Raku | Generated C++ | Dispatch |
|---|---|---|
| `square(5)` | `u_square(ValueList{…})` | direct C++ call; inlinable |
| `say …` | `rtCallB(RT, __bfp0, "say", …)` | cached pointer |
| `$n * $n` | `applyArith("*", …)`, or `rtMul` under `-O` | string-dispatch chain, or inline |
| `$x.method` | `RT.methodCall(inv, "m", …)` | name-keyed `if`-ladder |

## What dispatch itself costs

Trivial function body, identical `ValueList` construction in every variant, so
the differences are pure dispatch:

| Shape | ns/call | |
|---|---:|---|
| direct C++ call | 46.3 | the floor |
| cached `BuiltinFn*` call | 47.4 | **+1.1 — dispatch eliminated** |
| by-name: hash, `find`, `std::function` | 55.0 | +8.7 vs direct |
| `RT.callBuiltin("chr", …)` end to end | 66.0 | the pre-change path |

Two readings. The by-name lookup tax is real but modest, and caching the pointer
recovers essentially all of it.

**The more important reading is that the floor itself is high.** About 46
nanoseconds for a *trivial* call, nearly all of it the `ValueList` — at the
time, a heap-allocating `std::vector` built per call. Dispatch was a quarter of
the overhead; the argument list was the rest.

That floor has since moved twice: `ValueList` stopped being a `std::vector`,
and its small blocks stopped reaching the allocator (Chapter 12), which took
the one-argument shape from 32 nanoseconds to 9.5. The passes below still pay —
they remove the list rather than making it cheap, and removing is worth more
than cheapening — but the gap they close is narrower than this table implies.

Operators are a different story, because `applyArith` has integer and number
fast paths at the top of its chain:

| Op shape | ns/call |
|---|---:|
| `applyArith("+", int, int)` — early fast path | 7.8 |
| `rtAdd(int, int)` — the `-O` lane | 5.6 |
| `applyArith("~", str, str)` — **late in the chain** | **118.1** |
| `rtConcat(str, str)` — direct | 9.6 |

The late-chain operators pay for everything they walk past: the mixin-delegation
check, negated-operator handling, set operations, junction autothreading,
`Whatever` currying, the `Version` branch — about 110 nanoseconds of "is this
something special?" before the actual string work.

## Cut 1 — cached builtin pointers

Every `RT.callBuiltin("name", …)` site now routes through a per-name pointer
resolved once at program startup:

```cpp
// src/Interpreter.h
const BuiltinFn* builtinPtr(const std::string& name) const;

inline Value rtCallB(Interpreter& I, const BuiltinFn* f, const char* name,
                     ValueList args) {
    if (f) return (*f)(I, args);
    return I.callBuiltin(name, std::move(args));   // by-name fallback
}
```

The generated call `rtCallB(RT, __bfp0, "say", ValueList{…})` **is** the cached
call, not a lookup: `rtCallB` is a three-line `inline` shim whose hot path is
one indirect call through the already-resolved pointer, and the `"say"` literal
is touched only on the fallback branch — nothing is hashed or searched.

The shim also does a required mechanical job. `BuiltinFn` takes `ValueList&`,
which a temporary cannot bind to; `rtCallB`'s by-value parameter materialises it
into an lvalue.

This cut is **not `-O`-gated**: it is plumbing rather than speculation, so plain
`--exe` gets it too. The fallback keeps semantics byte-identical for names that
are not registered builtins.

One portability landmine, recorded because it cost an afternoon: the cached
pointer names were originally `__bfN`, so the seventeenth builtin in a program
emitted `__bf16` — which is a **reserved built-in type** (bfloat16) on arm64
clang. Hence the `__bfpN` spelling.

## Cut 2 — inline string comparisons

`eq ne lt gt le ge` get the `rtEqS` family: two *plain* `Str`s compare byte-wise
inline, which is exactly what `applyArith`'s tail does for them. Anything tagged
falls back to the full chain.

Value-context forms sit in the `-O` table; **bool-context forms join the
always-on condition table**, beside the existing integer `rtLtB` family.

End to end, 2 to 3 million iteration loops:

| Loop | Before | After | |
|---|---:|---:|---|
| builtin-heavy (`ord(chr(…))`), `--exe` | 407.8 ms | 383.5 ms | 1.06× |
| `$c++ if $a eq $b`, `--exe` | 712.0 ms | 129.2 ms | **5.5×** |
| `$c++ if $a eq $b`, `--exe -O` | 621.7 ms | 29.0 ms | **21×** |

The string-compare cut is the headline. An `eq` in a condition used to compile
to a boolified `applyArith` call: build a `Bool` `Value`, walk the whole
dispatch chain, then read the truthiness back out. It now compiles to an inline
`l.s == r.s`.

## Cut 3 — true named builtins, and the analysis that was wrong

An earlier revision of this analysis claimed that a *symbol* call like `b_say(…)`
had a measured ceiling of about 1 nanosecond per call, and therefore was not
worth building.

**That figure was correct only for renaming the same call shape** — an
out-of-line call still taking a `ValueList`. What a real named function unlocks
is different in kind:

- **direct `Value` arguments**, so no per-call `ValueList` allocation — which is
  the actual floor;
- **an inlinable hot path**, because a `std::function` is an opaque wall to the
  optimizer and an `inline` function is not.

Worse, some builtin lambdas hid extra cost. `abs`'s delegated to `methodCall` —
the full method-name ladder on every call, about 370 nanoseconds per iteration
in an `abs` loop.

```cpp
// src/Interpreter.h
inline Value rtBAbs(Interpreter& I, const Value& v) {
    if (v.t == VT::Int && !v.big && I.builtinExt_.empty())
        return Value::integer(v.i < 0 ? -v.i : v.i);
    return rtBAbsSlow(I, v);
}
```

`abs`, `chr` and `ord` became real functions. The interpreter's map entries wrap
them, **so the interpreter and the generic compiled path get the win too**, and
`-O` emits direct calls when a single plain positional argument lines up.

Note the `builtinExt_.empty()` guard: the inline fast path switches itself off
the moment a program `augment`s a built-in type, so an augmented `.abs` still
wins (Chapter 17).

| Loop | cached pointer | named function | |
|---|---:|---:|---|
| `abs`, `--exe -O` | 1112.9 ms | 198.3 ms | **5.6×** |
| `abs`, plain `--exe` | 1120.5 ms | 363.9 ms | 3.1× |
| `chr`+`ord`, `--exe -O` | 393.6 ms | 200.8 ms | 2.0× |

The recipe then swept the rest of the viable set — **36 names**: the numeric
family, the string family, the twelve trigonometric and hyperbolic functions,
and the I/O quartet.

The I/O quartet is worth its own sentence, because it disproved an assumption. A
*buffered* one-argument `say` costs about 125 nanoseconds, so the call plumbing
was roughly 45% of it: the `say` loop went from 124.9 to 69.6 milliseconds, with
byte-identical output. "I/O dominates, so the call cost does not matter" was
simply false at this scale.

Each named function is the old registered lambda's one-argument case
**verbatim**. Delegators keep their `methodCall`, so `augment`, user objects and
junctions are untouched; only `abs` and `sign` have bypassing fast paths, and
both are guarded.

Additional measurements: a `sqrt`+`sin`+`floor` loop went 731.5 to 363.6
milliseconds under `-O`; `uc`+`chars` went 672.5 to 574.3 — a smaller win,
because the real work in `mapCase` and `graphemeCount` dominates, as it should.

## The interpreter got the same treatment

A follow-up the same day: `applyArith` gained a character-dispatched `Str`/`Str`
fast path at the top of its chain, beside the existing integer and number ones,
so the *interpreter* also skips the late-chain walk for plain-string comparisons
and concatenation.

Interpreted `streq` went from 909.7 to 547.9 milliseconds. The remaining gap to
Rakudo there is per-node tree-walk cost, which no operator fast path can remove.

## What is deliberately not done

- **The `ValueList` per call** — the dominant cost of the calling convention,
  about 40 of the 46-nanosecond floor. `-O`'s direct-arity pass removes it for
  fixed-arity user subs, and cut 3 removes it for the named builtins, but the
  other ~165 builtins still take `ValueList&` by contract. A wholesale change —
  small-buffer or span arguments — is a runtime-wide refactor with interpreter
  implications.
- **`methodCall`'s `if`-ladder.** After the `MName` fix (Chapter 10) name
  comparison is 8.5% of the profile. A dispatch table would be chasing that
  8.5%, and it is not a drop-in: the ladder is not a pure dispatch on the name.
- **The remaining late-chain operators** — `x`, `xx`, `gcd`/`lcm`, bitwise,
  `min`/`max`, `leg`/`cmp` — still walk the chain. Same recipe as `rtEqS` if any
  shows up hot; none of the current benchmarks exercise them.

## The general lesson

Three cuts, and the third one contradicted the written analysis of the first
two. What made the difference was not better reasoning — it was measuring the
*right thing*: the earlier figure benchmarked a rename, not the change that a
rename enables.

That is the recurring failure mode in performance work, and the defence is the
same one used throughout this book: benchmark the full proposal, not the piece
of it that is easy to isolate, and keep a control kernel that the change cannot
touch.
