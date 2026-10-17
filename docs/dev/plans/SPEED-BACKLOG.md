# Speed backlog: what is open, by theme

*Compiled 2026-10-15 from [TODO.md](TODO.md), [V6-PLAN.md](V6-PLAN.md) and
the plans they point to. V5-PLAN has no speed items left: its one, the cost
of a thrown exception, moved to v6 and is done (9cc30857).*

This is a view of the open speed, memory and load-time work, not a plan of
its own. Each row names the plan that owns the details. When an item closes,
take it off here and off TODO.md together.

**Complexity** is an estimate from what each plan describes. The plans give
no effort figures:

| mark | meaning |
|---|---|
| XL | a project of its own, months |
| L | a large change across several files, with its own measurements |
| M | a contained change, but one that needs design or a bisect first |
| S–M | small, with one question to settle first |
| S | small |

The numbers are stable labels from the list this was made from, so a row can
be named in a commit or a conversation. They are not priorities. Within a
theme the rows are sorted by complexity, largest first.

---

## 1. Interpreter core: calls, dispatch, hot paths

| # | Item | Complexity | Next step / source |
|---|---|---|---|
| 1 | Interpreter at native speed (17 of 35 tasks open) | XL | Quicken `evalAssign`/`evalIndex`/`evalUnary`, then task 10 — [INTERP-SPEED-PLAN.md](INTERP-SPEED-PLAN.md) |
| 5 | Pass the thread context down instead of reading `tctx_` (20% of a walking `loopsum`) | XL | runLoopBody, execPlainBlock, exec, evalAssign — INTERP-SPEED-PLAN task 4 |
| 6 | A dispatch table and type-keyed inline caches for built-in methods | XL | Design first: hashing the method chain alone measured slower — V6 P2, [DISPATCH-PERF-PLAN.md](DISPATCH-PERF-PLAN.md) |
| 16 | Graph #47: a lean `invokeMethod` path, then one for closures | M | A method or closure call is ~0.2 µs against Rakudo's ~0.08 — TODO.md |
| 19 | Operator names as integers; the Int fast path for `<=>`, `cmp`, `div`, `mod`, `gcd`, `**` | M | V6 P2 |
| 20 | The call-path cost shipped in v5.3.0 (`privmeth` +9.5%, `method` +8.6%) | M | `setupRwLinks` on calls with no rw parameter, SlotStripe single-threaded; A/B against v5.2.1 — TODO.md |
| 23 | Cache a class name's resolution on its node | M | A NameTerm also resolves lexically (`::T`, `my class`), so the cache needs a record that no scope binds the name; at most ~7% of a 10× `objects` run — V6 P1 |
| 26 | Built-in methods in interpreted loops: `.map`/`.flat`/`.any` on a Seq; a CStruct's CArray field built once per pointer | M | TODO.md |
| 30 | Bisect the interpreted slowdowns shipped in v5.4.0 (`mainwhen` +11%, `sortby` +8%) | S–M | Instruction counts make the bisect deterministic — TODO.md |
| 42 | `$v ~~ Int` against a core type object: ~0.55 µs compiled, ~1 µs interpreted | M | rtSmartmatch's type-object arm, ahead of the general smartmatch — float report 2026-10-15, item 8 |
| 43 | A `constant` reference keeps a sub off the int kernels: two of them 561 ns a call, two literals 7.8 | S–M | The kernel compiler refuses the NameTerm; a top-level `constant` bound to an Int literal can be read as the literal — float report, item 11 |

## 2. Memory and value representation

| # | Item | Complexity | Next step / source |
|---|---|---|---|
| 4 | Values stop being refcounted, which also answers reference cycles | XL | B or C was measured 2026-10-15: C. B adds 7–17% allocations (+357% on one Roast file), C 0.5%. Next is C's 16-byte string, `Value` 80 -> 64 with ~1,549 `string_view` sites; then the refcount question — [VALUE32-PLAN.md](VALUE32-PLAN.md) "B versus C, measured", V6 P3.10 |
| 29 | Tail: UNBOX P3–P5 (`--exe` lanes across inlined calls, `my int` lanes, Rat lanes) | M–L | Which backend gets this, now that `--cnp` is to be the default, is the maintainer's decision — [UNBOX-PLAN.md](UNBOX-PLAN.md) |

## 3. Compiled tiers: `--cnp`, `--exe`, kernels, `--types`

| # | Item | Complexity | Next step / source |
|---|---|---|---|
| 2 | `--cnp` becomes the default and `--jit` goes | XL | P5a–c (one table per architecture, `-fno-jump-tables`, the CI gate), then P2, P3, P4 — [CNP-PLAN.md](CNP-PLAN.md) |
| 3 | `--exe` compiles at least 90% of the fully passing Roast files natively (65% now) | XL | Work down the fallback table — V6 P5 |
| 12 | Loop and sub kernels beyond scalars, `--exe` parity | L | Tasks 5–8 — [KERNEL-PLAN.md](KERNEL-PLAN.md) |
| 13 | `--exe` native module bodies: the `RT.methodCall` ladder | L | TODO.md |
| 14 | `--types` (paused 2026-09-29) | L | N1 the switch, N2 the checked rule, N3 measure, N7 Str — [TYPES-PLAN.md](TYPES-PLAN.md) |
| 15 | `--exe` build time: split the mainline into chunks (`range-int.t` 52 s at -O2) | L | V6 P5 |
| 33 | The compile modes (`--exe`, `--cpp`, `--target=js`) parse the program twice | S–M | Reuse the parse `declCheckGate` makes — src/main.cpp |
| 35 | `--slim` review: what it actually removes | S–M | Binary size; the 12.5 / 12.75 MB budgets are a stopgap — [SLIM-PLAN.md](SLIM-PLAN.md) |
| 44 | `--exe`: one refused construct (a CATCH, a phaser, `my num @a`, a kept List) bundles the program — its routines now keep native bodies, but its mainline and anonymous blocks are interpreted, and a call into a native body costs an interpreted call (~250 ns) | M–L | Mainline loops and block closures as native bodies, and a cheaper interpreter-to-native call — float report, items 2, 4, 14; feeds #3 and #45 |
| 45 | `--exe`: a module routine called from the program costs ~6× a local one (68 vs 10.6 ns) | L | The call goes through callEnvFirstAmp and the interpreter's binding, and the native body runs inside that — float report, item 10; near #13 |
| 46 | `--exe`: a native-typed value is still a boxed Value; a `num`-typed sub is ~2.4× its untyped twin (62 vs 26 ns) | L | Typed locals as C++ doubles/int64s; the stores, ops and `int` kernels are done — float report, item 16; near #29 |
| 47 | `--exe`: a `my sub` is called through `RT.dynVar("&name")` and callCallable, ~170 ns against ~25 for a top-level sub | M | A direct call needs the arity proved at the call site and a handle to the closure that cannot outlive it — float report, item 9 |

## 4. Lists, strings and laziness

| # | Item | Complexity | Next step / source |
|---|---|---|---|
| 7 | `map` and `grep` over a finite list become lazy, as in Rakudo | L | The eager loop is the fast path, so this needs a map-heavy perf-guard kernel beside it — TODO.md |
| 18 | Append plan: `eq` and printing read views directly; a grapheme table for non-ASCII views | M | [APPEND-PLAN.md](APPEND-PLAN.md) |
| 25 | `$s ~~ m:c/…/` in a loop is quadratic (×58 for 8× the size) | M | `run/str-match-continue` in t/scaling — TODO.md |
| 31 | `@a.unshift` / `.prepend` are quadratic (×60) | S–M | Headroom at the front of the ValueList storage — TODO.md |
| 36 | `.join` reads the array in place, as `.sum`/`.min`/`.max` do since 02fb7c6a | S | Elements that can run no user code only — V6 P1 |

## 5. Parallelism

| # | Item | Complexity | Next step / source |
|---|---|---|---|
| 11 | Parallel scaling P7/P8; `.hyper` worker scaling (~2.2× on 7 workers) | L | The gates not yet run, then the `--exe` probes — [PARALLEL-SCALING-PLAN.md](PARALLEL-SCALING-PLAN.md) |
| 28 | The worker tax: a worker's loop runs ~15% slower than the main thread's | M | V6 P6, [PARALLEL-PLAN.md](PARALLEL-PLAN.md) |

## 6. Compile and load time

| # | Item | Complexity | Next step / source |
|---|---|---|---|
| 22 | The operator pre-scan, `precompRead`, the precomp key | M | The precomp key is the maintainer's decision — V6 P4 |

## 7. Bindings and ecosystem

| # | Item | Complexity | Next step / source |
|---|---|---|---|
| 27 | Python per-leaf slowdown (a quarter to a half against 4.0.1) | M | Profile a tight `rk_call` loop, then bisect v4.0.1..v5.2.0 — TODO.md §4 |
| 37 | Re-time Cro::HTTP's 14.7 s cold install with the parallel fetch | S | Needs the network — [DEPS-PLAN.md](DEPS-PLAN.md) D6 |

## 8. Measurement

| # | Item | Complexity | Next step / source |
|---|---|---|---|
| 34 | V6 P0: steady-state kernels, `tools/bench/memory.raku`, the malloc census | S–M | Everything else is judged against these — V6 P0 |
| — | A quiet-machine `perf-guard --check`, owed by APPEND, KERNEL, SANDBOX, the phaser work and the V6 P1 batch | S | The batch's interleaved A/B was within ±4%, on a loaded machine |
| — | Close [GRAMMAR-SPEED-PLAN.md](GRAMMAR-SPEED-PLAN.md), or fold what is left into INTERP-SPEED | S | The maintainer's decision |

---

## Closed since the first list (2026-10-14)

- **`do { }` and `quietly { }`, after 38** (205f5e97): the same in-place
  path as `try`. `do { … }` −32.5% and `quietly { … }` −33.4% in
  instructions; a `when` in a `do`, `state`, `temp` or a phaser keep the call.
- **38, `try { }` as a closure call** (6279175b): the block runs where it
  stands, in a pooled scope with its own pad, when nothing in it needs the
  call; a body that does keeps it. `try { $t += $_ }` −24%, a `try` that
  catches a `die` −43% in instructions. The row's "30 for a bare block" was
  a fused loop kernel: the call was ~2,700 of a `try`'s cost, not 6,000.
- **The float report** (2026-10-15, a float-heavy program run interpreted
  and as `--exe -O`; t/regression/float-exe-report.raku). Per probe, ns/op,
  `--exe -O` before → after: Num `$a * $b + $a` 41 → 17, `+&`/`+>` 84 → 9,
  `($x * 2**52).Int` 125 → 19, `Any` 210 → 8, `101 <= $f < 200` 30 → 13,
  `.key`/`.value` 386 → 27, three labelled `next` 21,282 → 177 (interpreted
  209,490 → 1,962). An `int`-typed sub interpreted 555 → 5.2 ns (it is an
  int kernel now); a compiled loop that reads a Num it never writes 339 → 18
  ns a call untyped, 506 → 59 as a `num` parameter.
  Correctness on the way: a Failure stored in a native, a typed `for`
  parameter, `for $*IN.lines` and a gather in a compiled loop, `Empty`,
  `Int(True)`/`Int(NaN)`. And a program `--exe` has to bundle keeps native
  bodies for its own routines, matched to the binary's run-time parse by an
  AST fingerprint (AotModules.h attachMainAot): a Mandelbrot routine in a
  program with one `CATCH` elsewhere 30 → 3.7 ms (3.5 fully native). Open
  from it: 42–47.
- **9, `Env::vars` and `Env` on the slab** (dca29704): re-measured first. Of 24
  block and call shapes, three still allocated per iteration, and inline
  slots were not the answer to any of them. A bare block's `$_` was a map
  node per call, because pads cannot hold the topic. A `try { }`, or any
  closure made per iteration, paid for the `state` frame each Callable makes
  on its first call. Both now come from the slab, and a body that cannot
  reach a `state` frame has none. `.map({ … })` −8.9% and `try` −12.5% in
  instructions, and 200k kept closures that each run once 152 -> 115 MB.
- **10, Pairs with an inline Int key** (9b3ed082): a plain Int key in the
  Pair's own word; 100k Int-keyed Pairs 303k -> 104k mallocs, 53.8 -> 21.6 MB.
  The Rat half had no mallocs left to save. The concurrency it surfaced in
  a file handle's hash is fixed in d278bc5f.
- **8, `Callable` split** (f7d4dc67, 6d67f0cd): the code is a copy-on-write
  CodeInfo every closure of one block shares; a Callable is its scope and
  its state, 704 -> 88 bytes. 100k kept closures 125.6 -> 61.6 MB.
- **29, two of its three tails** (c37ce984, e46e839b): a hash of up to four
  entries has no probe index (VALUEHASH step 4: one malloc less per object),
  and the shared-cell proxy shares one FETCH/STORE pair (DISPATCH-PERF Ph6:
  an element bind 5 -> 3 mallocs, 2.1 -> 0.6 KB).
- **17, packed native arrays batch 3** (ff3fcc5b): reductions, op= and `++`
  on the words, `--cnp` kernels over packed arrays, and the unpack census
  (`RAKUPP_PACKED_CENSUS=1`). A 1M `@a[$i] += 1` loop 0.16 -> 0.04 s.
- **21, slim `VarExpr`** (28989c10): a declaration's fields behind one
  pointer that a plain read shares; 400 -> 136 bytes, `-c` on a 52k-line
  program 151 -> 136 MB.
- **24, a shaped native array at ~160 B an element** (6021c2fa, 44ed66bc):
  the mainline pre-declaration built the array a second time, and each
  element was an 80-byte Value; a full-width `int`/`num` shaped array now
  keeps its rows as words. deep-recursion-initing-native-array.t 8.07 ->
  1.05 GB.
- **32, `my @d = <fresh list>`** (4a1edb88): a right side no other Value
  holds hands its buffer to the array instead of being copied. 2M Nums from
  `»*«`: 46 -> 13 ms.
- **34–41, the V6 P1 batch** (777f23cf .. 1d7002a5): the lexer and parser
  (`-c` −23%), borrowed `.sum`/`.min`/`.max` and `STMT for ^N` by its ends,
  `exprYieldsContainer` on `return`, placeholders in the pad, grammar replay
  keys and the parser's tokens, `$*DISTRO` without `sw_vers`, the Order
  values, the scope by reference in runLoopBody, and parallel archive
  fetches (DEPS D6). The numbers are in V6-PLAN P1 and the commits.
- **31, the Windfall round** (783bc53a, d1dacd52): `--cnp` kernel entry and
  the native-parameter divergences.
- **`Array.shift`** (V6 P1) was already linear: 8× the work, 7.8× the time.
