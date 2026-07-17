# Call dispatch in `--exe` code — what each shape costs

A compiled Raku++ program reaches code four different ways, and they are not
equally fast. This note documents the shapes, the measured cost of each, and
the three dispatch cuts made on 2026-07-17 (cached builtin pointers, inline
string comparisons, true named builtins) — plus what's deliberately left on
the table.

Method: micro-benchmarks against `librakupp_rt.a` (clang -O2, arm64 macOS),
2M iterations per shape, 7 reps, first discarded, min reported — the
[BENCHMARKS.md](../BENCHMARKS.md) policy. End-to-end numbers are `--exe`
binaries of small hot-loop programs, same policy.

## The four shapes

For `sub square($n) { $n * $n }` + `say square(5)`, the generated C++ contains
all four:

| Raku | Generated C++ | Dispatch |
|---|---|---|
| `square(5)` (user sub) | `u_square(ValueList{…})` | **direct C++ call** — the compiler can inline it |
| `say …` (named builtin) | was `RT.callBuiltin("say", …)`, now `rtCallB(RT, __bfp0, "say", …)` | **cached pointer** (was: by-name map lookup per call) |
| `$n * $n` (operator) | `applyArith("*", …)`, or `rtMul(…)` under `-O` | **string-dispatch chain**, or inline fast path |
| `$x.method` | `RT.methodCall(inv, "m", …)` | **name-keyed `if`-ladder** in Builtins.cpp |

## Measured: what dispatch itself costs

Trivial function body, identical `ValueList` construction in every variant, so
the differences are pure dispatch (ns/call, min of 6):

| Shape | ns/call | |
|---|---:|---|
| direct C++ call (user-sub shape) | 46.3 | the floor |
| cached `BuiltinFn*` call | 47.4 | **+1.1 — dispatch eliminated** |
| by-name: hash + `unordered_map::find` + `std::function` | 55.0 | +8.7 vs direct |
| `RT.callBuiltin("chr", …)` end to end (real chr) | 66.0 | the pre-change builtin path |

Two readings. First, the by-name lookup tax is real but modest (~8–9 ns);
caching the pointer recovers essentially all of it. Second — and more
important — **the floor itself is high**: ~46 ns for a *trivial* call, nearly
all of it the `ValueList` (a heap-allocating `std::vector`) built per call.
Dispatch was a quarter of the overhead; the argument vector is the rest. That
is why `-O`'s direct-arity pass (plain `Value` parameters, no `ValueList`)
exists and why it buys more than any lookup cache can.

Operators are a different story. `applyArith` has an Int/Int and a Num fast
path at the top of its chain, so hot arithmetic was never badly off:

| Op shape | ns/call |
|---|---:|
| `applyArith("+", int, int)` (early fast path) | 7.8 |
| `rtAdd(int, int)` (`-O` lane) | 5.6 |
| `applyArith("~", str, str)` — **late in the chain** | **118.1** |
| `rtConcat(str, str)` — direct | 9.6 |

The late-chain ops pay for everything they walk past: the mixin-delegation
check, negated-op handling, set ops, junction autothreading, Whatever-currying,
the Version branch — ~110 ns of "is this something special?" before the actual
string work. `"+"` skips all of it via the early fast path; `"~"`, `eq`, `lt`
and friends did not.

## The three cuts (38ed193, bf88539, 1c407bf)

**1. Cached builtin pointers.** Every `RT.callBuiltin("name", …)` site now
routes through a per-name `static const BuiltinFn* __bfpN`, resolved once at
program startup (`__rakupp_register` → `Interpreter::builtinPtr`) and called
directly. `rtCallB` keeps the by-name fallback for names that aren't
registered builtins (module-loaded routines), so semantics are byte-identical.
Not `-O`-gated: it's plumbing, not speculation — plain `--exe` gets it too.

*Reading the emitted call:* `rtCallB(RT, __bfp0, "say", ValueList{…})` **is**
the cached call, not a lookup — `rtCallB` is a three-line `inline` shim whose
hot path is `(*f)(I, args)`, so at `-O2` the call site compiles to a
predicted-not-null test plus an indirect call through the already-resolved
pointer. The `"say"` literal is a `const char*` touched only on the fallback
branch (nothing is hashed or searched). The shim also does a required
mechanical job: `BuiltinFn` takes `ValueList&`, which a temporary
`ValueList{…}` can't bind to — `rtCallB`'s by-value parameter materializes it
into an lvalue.

**2. Inline string comparisons.** `eq ne lt gt le ge` get the `rtEqS…` family:
two *plain* Strs (no `hashKind` tag — Version/IO/Buf — and no enum identity)
compare byte-wise inline, which is exactly what `applyArith`'s tail does for
them (a plain Str's `toStr()` is its `s`). Anything tagged falls back to the
full chain, preserving Version part-comparison, enum stringification, junction
autothreading, and Whatever-currying. Value-context forms sit in the `-O`
`fastBin` table; bool-context forms (`rtEqSB…`) join the always-on condition
table beside the existing int `rtLtB` family.

End-to-end (2–3M-iteration loops, min of 6):

| Loop | Before | After | |
|---|---:|---:|---|
| builtin-heavy (`ord(chr(…))` ×2/iter), `--exe` | 407.8 ms | 383.5 ms | 1.06× |
| `$c++ if $a eq $b`, `--exe` | 712.0 ms | 129.2 ms | **5.5×** |
| `$c++ if $a eq $b`, `--exe -O` | 621.7 ms | 29.0 ms | **21×** |

The string-compare cut is the headline: `eq` in a condition went from
`RT.boolify(applyArith("eq", …))` — Bool `Value` built, chain walked, truthy
re-read — to an inline `l.s == r.s`. The builtin cut is the modest, broad one:
every compiled program calling `say`/`push`/`chr`/… saves the lookup on every
call.

**3. True named builtins (the follow-up that proved the first analysis
wrong).** An earlier revision of this note claimed a *symbol* call like
`b_say(…)` had a measured ceiling of ~1 ns/call. That figure was correct only
for *renaming the same call shape* — an out-of-line call still taking a
`ValueList`. What a real named function unlocks is different in kind:
**direct `Value` arguments** (no per-call `ValueList` heap allocation — the
actual floor) and an **inlinable hot path** (a `std::function` is an opaque
wall to the optimizer; an `inline` function is not). Worse, some builtin
lambdas hid extra cost: `abs`'s delegated to `methodCall` — the full
method-name ladder on every call, ~370 ns/iteration in an `abs` loop.

`abs`, `chr`, `ord` are now real functions (`rtBAbs`/`rtBChr`/`rtBOrd`,
declared in `Interpreter.h`, defined in `Builtins.cpp`). The interpreter's map
entries wrap them — so the interpreter and the generic compiled path get the
win too — and `-O` emits direct calls when a single plain positional argument
lines up. `rtBAbs`'s plain-Int case is header-inline (guarded off while any
`augment` is live, so an augmented `.abs` still wins; everything non-trivial
delegates back to `methodCall`). Measured (3M/2M-iteration loops, min of 6):

| Loop | cached-pointer | named fn | |
|---|---:|---:|---|
| `abs`, `--exe -O` | 1112.9 ms | 198.3 ms | **5.6×** |
| `abs`, plain `--exe` | 1120.5 ms | 363.9 ms | **3.1×** — the map entry itself got fast, so this flows to the interpreter too |
| `chr`+`ord`, `--exe -O` | 393.6 ms | 200.8 ms | **2.0×** |

The recipe then swept the rest of the viable set — **32 names** in codegen's
`fastB` table: the numeric family (`sign floor ceiling round truncate sqrt exp
log log10 log2 is-prime`), the string family (`uc lc chars flip trim chomp
chop`), and the twelve trig/hyperbolic functions. Each named function is the
old registered lambda's 1-arg case *verbatim* (delegators keep their
`methodCall`, so augment/objects/junctions are untouched; only `abs` and
`sign` have bypassing fast paths, both `builtinExt_`-guarded). Additional
measurements: a `sqrt`+`sin`+`floor` loop 731.5 → 363.6 ms under `-O` (2.0×);
`uc`+`chars` 672.5 → 574.3 ms (1.2× — `mapCase`/`graphemeCount` real work
dominates, as it should). The remaining per-call cost everywhere is the
`Value` construction/moves themselves.

One portability landmine for the record: the cached-pointer names were
originally `__bfN`, and the 17th builtin in a program emitted `__bf16` —
which is a **reserved built-in type** (bfloat16) on arm64 clang. Hence the
`__bfpN` spelling.

Verified: `t/run.raku` 55/55; 20 deterministic examples byte-identical across
interp / `--exe` / `--exe -O`; tagged-value edge cases (Version `eq`, enum
`eq`, junctions, `Int eq Str` coercion) match the interpreter.

## What's deliberately not done (and what it would buy)

- **The `ValueList` per call** — the actual dominant cost of the calling
  convention (~40 of the 46 ns floor). `-O`'s direct-arity pass removes it for
  fixed-arity user subs, and cut 3 removes it for the *named* builtins — but
  the other ~165 builtins still take `ValueList&` by contract. The named-fn
  recipe extends one builtin at a time; a wholesale ABI change (small-buffer /
  span args) remains a runtime-wide refactor with interpreter implications.
- **`methodCall`'s `if`-ladder** — `$x.method` dispatches through a chain of
  name comparisons in Builtins.cpp (see [RUNTIME.md](../RUNTIME.md)). A
  perfect-hash or interned-name switch would help every method call in both
  the interpreter and compiled code. Big surface, separate project.
- **Remaining `applyArith` late-chain ops** — `x`, `xx`, `gcd`/`lcm`,
  bitwise, `min`/`max`, `leg`/`cmp` still walk the chain. Same recipe as
  `rtEqS` if any shows up hot; none of the current benchmarks exercise them.
- ~~**The interpreter** pays the same string dispatches per AST node~~ —
  follow-up, same day: `applyArith` gained a char-dispatched Str/Str fast path
  at the top of its chain (beside the existing Int/Int and Num ones), so the
  interpreter now skips the late-chain walk for plain-string `eq ne lt gt le
  ge ~` too. `streq` interpreted: 909.7 → 547.9 ms (1.7×). The remaining gap
  to Rakudo there (1.9×) is per-node tree-walk cost, which no operator fast
  path can remove (`Binary::simpleOp` already caches the dispatch decision
  per node).

## Reproducing

Both benchmarks are in the repo. The micro-benchmark is
[`tools/dispatch-bench.cpp`](../../tools/dispatch-bench.cpp), compiled against
the built runtime:

```sh
clang++ -std=c++17 -O2 -DNDEBUG -Isrc tools/dispatch-bench.cpp \
        build/librakupp_rt.a -o /tmp/dispatch-bench && /tmp/dispatch-bench
```

The end-to-end string-compare loop is the `streq` kernel of the official suite,
[`tools/bench/streq.raku`](../../tools/bench/streq.raku):

```sh
build/rakupp --exe -o /tmp/b tools/bench/streq.raku && for i in {1..7}; do time /tmp/b; done
build/rakupp tools/run-bench.raku      # or: the full three-engine table
```

The official cross-engine numbers live in [BENCHMARKS.md](../BENCHMARKS.md) /
[OPTIMIZATION.md](../OPTIMIZATION.md).
