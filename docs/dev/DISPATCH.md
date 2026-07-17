# Call dispatch in `--exe` code — what each shape costs

A compiled Raku++ program reaches code four different ways, and they are not
equally fast. This note documents the shapes, the measured cost of each, and
the two dispatch cuts made on 2026-07-17 (cached builtin pointers, inline
string comparisons) — plus what's deliberately left on the table.

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
| `say …` (named builtin) | was `RT.callBuiltin("say", …)`, now `rtCallB(RT, __bf0, "say", …)` | **cached pointer** (was: by-name map lookup per call) |
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

## The two cuts (committed 38ed193)

**1. Cached builtin pointers.** Every `RT.callBuiltin("name", …)` site now
routes through a per-name `static const BuiltinFn* __bfN`, resolved once at
program startup (`__rakupp_register` → `Interpreter::builtinPtr`) and called
directly. `rtCallB` keeps the by-name fallback for names that aren't
registered builtins (module-loaded routines), so semantics are byte-identical.
Not `-O`-gated: it's plumbing, not speculation — plain `--exe` gets it too.

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

Verified: `t/run.raku` 55/55; 20 deterministic examples byte-identical across
interp / `--exe` / `--exe -O`; tagged-value edge cases (Version `eq`, enum
`eq`, junctions, `Int eq Str` coercion) match the interpreter.

## What's deliberately not done (and what it would buy)

- **The `ValueList` per call** — the actual dominant cost of the calling
  convention (~40 of the 46 ns floor). `-O`'s direct-arity pass already
  removes it for fixed-arity user subs; builtins still take `ValueList&` by
  contract. Changing the builtin ABI (e.g. small-buffer or span args) is a
  runtime-wide refactor with interpreter implications — the next big lever,
  not a codegen patch.
- **`methodCall`'s `if`-ladder** — `$x.method` dispatches through a chain of
  name comparisons in Builtins.cpp (see [RUNTIME.md](../RUNTIME.md)). A
  perfect-hash or interned-name switch would help every method call in both
  the interpreter and compiled code. Big surface, separate project.
- **Remaining `applyArith` late-chain ops** — `x`, `xx`, `gcd`/`lcm`,
  bitwise, `min`/`max`, `leg`/`cmp` still walk the chain. Same recipe as
  `rtEqS` if any shows up hot; none of the current benchmarks exercise them.
- **The interpreter** pays the same string dispatches per AST node, but its
  costs are dominated by tree-walking itself (`Binary::simpleOp` already
  caches the dispatch decision per node). These cuts are `--exe`-side.

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
