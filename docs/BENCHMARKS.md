# Raku++ vs Rakudo — speed

A small, honest performance comparison on the subset of Raku that **both**
engines run identically. This is not a claim that Raku++ is "better" — Rakudo
is the mature, complete, production reference implementation, and Raku++ is
[far behind it on Roast coverage](ROAST.md). The point is only to give a fair
picture of where Raku++ — as both a tree-walking interpreter and a native
compiler — lands relative to an optimizing VM.

Raku++ can run a program three ways, and this compares all of them against
Rakudo:

- **interp** — Raku++ interpreting the source (tree-walk)
- **native** — Raku++ `--exe`: the program transpiled to C++ and compiled to a
  native binary (no interpreter inside)
- **rakudo** — Rakudo interpreting the source (MoarVM + JIT)

Raku++ has two other standalone-binary modes — `--bundle` and `--aot` — but both
*tree-walk* the program, so they run at **`interp` speed** and aren't shown
separately (`--aot` fib runs at interp's ~770 ms). `--exe` is the only
mode that changes runtime performance, so it's the one the `native` column
tracks.

(A fourth environment — **[Raku.js](../rakujs/)**, the interpreter compiled to
WebAssembly — is measured against `interp` on these same kernels under Node,
Bun, and the browser in
[rakujs/README.md](../rakujs/README.md#performance-vs-native-and-node-vs-bun-vs-browser):
1.3–6.8× slower than native on a clean host, dominated by the `-fexceptions`
call trampolines. That comparison is still experimental — see the status note
there.)

## The short version

- **Startup:** ~2 ms cold on this machine (best of a 200-spawn loop: 1.8 ms) —
  a tiny native binary with no VM to spin up. For one-liners, CLI glue, and
  small programs it is instant.
- **Native (`--exe`) beats Rakudo on every benchmark here** — from 2.6× on
  `arrayops` to 9.6× on `loopsum`, 13.7× on `hash`, and 43.0× on `strcat`.
  Compiling removes interpreter overhead.
- **Rakudo's JIT keeps two interpreter wins**: `fib` (1.8×) — deep recursion of
  a tiny body — and `streq` (1.9×; string comparisons walk the interpreter's
  operator-dispatch chain). Compiling flips both: `--exe` puts `fib` 2.9× ahead
  and `streq` 6.4× ahead (string `eq`/`lt` compile to inline byte-compares — see
  [dev/DISPATCH.md](dev/DISPATCH.md) for the dispatch story).
- Even the **interpreter** beats Rakudo on 7 of 9 — everything except `fib` and
  `streq`, including the `loopsum` loop kernel (1.4×).
- **String building (`~=`) appends in place** in every mode, so `strcat` is
  O(n) rather than O(n²) — 14× ahead of Rakudo even interpreted.

## Methodology

- **Machine:** macOS (Darwin 24.6), measured 2026-07-22 on a lightly loaded
  desktop. (Rows are not comparable across doc revisions — absolute times
  shift a few percent with machine state; the Rakudo column, measured every
  time, is the fixed yardstick. A per-iteration `std::function` allocation that
  had crept into the loop path over the v0.7.1→v0.9.0 cycle was found by bisect
  and removed, restoring the tight-loop kernels to their v0.7.1 speed.)
- **Raku++:** built `-O3 -DNDEBUG` (CMake Release).
- **Rakudo:** `raku` v2026.06 (MoarVM backend).
- **Harness:** [`tools/run-bench.raku`](../tools/run-bench.raku) — itself a Raku
  program, run *by Raku++* (it also runs under Rakudo). It only spawns each
  engine as a fresh subprocess and times it with `now`, so the language the
  harness is written in does not bias either contestant.
- **Timing:** 7 runs per benchmark, first discarded as warm-up, **minimum of the
  remaining 6** reported (least noisy; the mean was within a few percent). For
  the `native` column each program is compiled with `--exe` once, then the
  resulting binary is timed — the compile step is not counted.
- **Fairness:** the harness runs each program under all three engines and
  compares their stdout **before** timing anything; if `interp`, `native`, and
  `rakudo` don't emit byte-identical output it flags the row and exits non-zero,
  so a divergent kernel can't be silently benchmarked. (Rakudo is the reference;
  every benchmark program is deterministic, so identical output is expected.)
- **Harness overhead:** spawning + capturing a subprocess adds a small fixed
  cost per run. On top of that each engine pays its *own* process startup —
  negligible for Raku++'s native binary, but Rakudo loads a full precompiled
  runtime, a fixed cost included in every row below. See "How to read this".

## Results

Best of 6 runs through the harness (includes process startup); lower is better.
Rows are ordered most-Raku++-favourable first.

### Interpreter vs Rakudo

Even without compiling, the tree-walker wins on all of these except two:
Rakudo's VM leads on `fib` (tiny-body recursion, a JIT's best case) and on
`streq` (string comparisons walk the interpreter's operator-dispatch chain).

| Benchmark | Raku++ (interp) | Rakudo | Faster |
|---|---:|---:|---|
| strcat   | 13.1 ms  | 189.3 ms | **Raku++ 14.4×** |
| bigint   | 31.8 ms  | 258.9 ms | **Raku++ 8.1×** |
| hash     | 38.4 ms  | 231.0 ms | **Raku++ 6.0×** |
| sortnums | 70.7 ms  | 261.6 ms | **Raku++ 3.7×** |
| regex    | 86.3 ms  | 285.7 ms | **Raku++ 3.3×** |
| arrayops | 114.9 ms | 287.0 ms | **Raku++ 2.5×** |
| loopsum  | 195.9 ms | 269.8 ms | **Raku++ 1.4×** |
| streq    | 562.5 ms | 296.2 ms | Rakudo 1.9× |
| fib      | 818.4 ms | 465.1 ms | Rakudo 1.8× |

### Native (`--exe`) vs Rakudo

Compiling removes interpreter overhead on top of that — pushing **every row
clearly ahead of Rakudo**, `fib` and `streq` included. The last column is the
speed-up over interpreting the same program.

| Benchmark | Raku++ (`--exe`) | Rakudo | Faster | vs interp |
|---|---:|---:|---|---:|
| strcat   | 4.4 ms   | 189.3 ms | **Raku++ 43.0×** | 3.0× |
| hash     | 16.9 ms  | 231.0 ms | **Raku++ 13.7×** | 2.3× |
| loopsum  | 28.0 ms  | 269.8 ms | **Raku++ 9.6×**  | 7.0× |
| bigint   | 30.4 ms  | 258.9 ms | **Raku++ 8.5×**  | 1.0× |
| streq    | 46.1 ms  | 296.2 ms | **Raku++ 6.4×**  | 12.2× |
| sortnums | 54.3 ms  | 261.6 ms | **Raku++ 4.8×**  | 1.3× |
| regex    | 67.8 ms  | 285.7 ms | **Raku++ 4.2×**  | 1.3× |
| fib      | 161.7 ms | 465.1 ms | **Raku++ 2.9×**  | 5.1× |
| arrayops | 111.2 ms | 287.0 ms | **Raku++ 2.6×**  | 1.0× |

**Reading the `vs interp` column:** compiling helps most where a tree-walker
hurts — `streq` 12.2× (per-node walking around what is, after the fast path, a
trivial byte-compare — see [dev/DISPATCH.md](dev/DISPATCH.md)), `loopsum` 7.0×,
`fib` 5.1× (both re-dispatch a tiny body a huge number of times). It's a near
no-op (1.0–1.3×) for the workloads whose time is spent *inside* runtime
methods — `arrayops`/`sortnums` (`.grep`/`.map`/`.sort`) and especially
`bigint`, which lives almost entirely in `BigInt` multiply. There the driving
loop is trivial, so removing interpreter overhead changes little.

`fib` — a tiny function called 1.6M times, the case a JIT specializes best — used
to be the one place Rakudo led even the default `--exe`; hot-pathing integer
arithmetic in the runtime (`applyArith`) closed that gap and put native ~2.9×
ahead. `streq` got the same treatment on 2026-07-17: string comparisons used to
walk `applyArith`'s full dispatch chain (~118 ns per `eq`). Compiled code now
emits inline plain-`Str` byte-compares and calls builtins through pointers
resolved once at startup; the interpreter gained a matching char-dispatched
Str/Str fast path at the top of `applyArith` (909.7 → 562.5 ms — the remaining
gap to Rakudo is per-node tree-walk cost, not operator dispatch).
[dev/DISPATCH.md](dev/DISPATCH.md) has the measurements.

### `-O` (the optimizer flag)

The `native` column above is the default `--exe`. Adding **`-O`** enables three
speculative codegen passes:

1. **direct-arity calls** — a fixed-arity positional sub gets direct `Value`
   parameters (plus a boxed adapter), skipping the per-call `ValueList` heap
   allocation;
2. **inline arithmetic & comparisons** — `+ - * ** % %% < <= > >= == !=` emit
   inline helpers that do the small-int case as native `int64` (overflow
   promotes to bignum), and `eq ne lt gt le ge` do the plain-`Str` case as a
   byte-compare, instead of the string-dispatched `applyArith`;
3. **guarded native-int expression lanes** — statement-position int assignments
   (`$x = …`, `$x += …`, `$x++`) and int conditions compute in raw `int64` with
   runtime tag guards and store into the target's existing box, constructing no
   `Value` at all; any guard/overflow failure re-runs the boxed form.

(In-place `~=` string building is *not* one of these — it is now the default in
both the interpreter and `--exe`.) Measured by
[`tools/run-optbench.raku`](../tools/run-optbench.raku) on five showcase kernels
written to exercise the passes (each program is verified to produce identical
output all three ways before timing). Best of 5 runs; Rakudo (v2026.06) shown
for reference:

| Benchmark | `--exe` | `--exe -O` | `-O` vs `--exe` | Rakudo | showcases |
|---|---:|---:|---:|---:|---|
| sieve       | 1189.9 ms | **23.5 ms**  | **50.5×** | 991.9 ms  | primes < 200k by trial division — `* <= %%` all laned |
| powmod      | 571.0 ms  | **53.2 ms**  | **10.7×** | 748.8 ms  | 1M `** 3` then `% 1000` — inline pow + mod lane |
| intsum      | 298.7 ms  | **31.8 ms**  | **9.4×**  | 656.5 ms  | 5M int accumulation — `+=` lane, zero boxing |
| fibcalls    | 670.6 ms  | **169.6 ms** | **4.0×**  | 1417.4 ms | fib(32) — direct-arity calls + int-lane condition |
| stringbuild | 24.0 ms   | 25.6 ms      | 1.0×      | 219.5 ms  | 400k `~=` appends — in-place O(n) string build |

The lanes (pass 3) dominate this table: `sieve`'s inner loop — `while $d * $d
<= $n`, `if $n %% $d`, `$d++` — runs as raw `int64`, taking it from a tie with
Rakudo at plain `--exe` to 42× ahead, and `intsum` shed its four
per-iteration `Value` constructions. On the main kernels above, `-O` puts fib
at 43.3 ms (3.7× over `--exe`, 10.7× over Rakudo), loopsum at 9.2 ms, and streq
at 17.5 ms (the `$c++`/`$c--` counters lane on top of the inline `eq`/`lt`).
`stringbuild` gains nothing because in-place append is already the default
everywhere. It's opt-in, off by default, and produces identical output
(validated per-program before timing, plus every deterministic example against
its golden). See [OPTIMIZATION.md](OPTIMIZATION.md) for what each pass emits
and the C++ optimization-level forwarding (`-O3`/`-Os`/`-Ofast`).

### Real-world: grammar parsing (YAMLish)

The benchmarks above are small kernels. This one is a whole real module doing
real work: the zef-installed **YAMLish** grammar (unmodified) parsing the Raku
course's table-of-contents YAML (`_data/toc/en.yaml`, 2576 lines) with
`load-yamls`. It exercises the backtracking grammar engine — subrules, LTM `|`,
lookbehind assertions, `:my` side-effects, indentation-parameterised rules, and
action-method tree building — against the same source under both engines.

Best of 5 runs, wall-clock (`time`), lower is better.

| Workload | Raku++ (interp) | Rakudo | Faster |
|---|---:|---:|---|
| load-yamls, one parse per process | 0.54 s | 0.98 s | **Raku++ 1.8×** |
| load-yamls, 10 parses in-process   | 5.48 s | 6.43 s | **Raku++ 1.2×** |

The single-parse figure is the realistic one — `raku-pages.raku` (the course
generator) reads the TOC once per invocation, and Raku++ regenerates the entire
1,483-page course byte-for-byte identically to Rakudo. The gap narrows for
repeated in-process parses because each `load-yamls` rebuilds and recompiles the
grammar, so grammar-construction cost isn't amortised across calls; the
per-parse matching itself is where Raku++'s lead comes from. Getting here took
targeted work on the match engine — bounding the lookbehind scan window (the
grammar had an O(N²) start-position rescan), caching per-rule name resolution on
the compiled AST, resolving tail-position `return` without a C++ exception, and
non-owning match continuations. An earlier build of this same parse took ~195 s;
that was an unbounded lookbehind scan on a document this large.

## How to read this

- **The short kernels are startup-inclusive — read them that way.** Every row is
  a fresh spawn, so each engine's process startup is part of its time. Rakudo
  loads a full precompiled runtime once per run, a fixed cost that dominates its
  number on the fastest kernels — so those multipliers are *not* a pure
  execution-speed comparison. The compute-heavy `-O` kernels and the in-process
  YAMLish parse (10 parses in one process) are the execution-only picture.
  Raku++'s own ~2 ms start is a real convenience for scripts, editor tooling, and
  shelling out in a loop, but it's a convenience, not an execution-speed claim.
- **Interpreter throughput losses are real, and `--exe` addresses them.** A
  tree-walker re-dispatches every AST node on every execution; compiling to
  native code removes that, which is why `loopsum`/`fib` catch or pass Rakudo.
  What compiling *can't* speed up is time spent inside the runtime's own methods
  — matching Rakudo there would mean optimizing (or JIT-ing) those, which is
  further out on the [roadmap](ROADMAP.md).
- **This says nothing about coverage.** Rakudo runs essentially all of Roast;
  Raku++ runs a growing fraction. These benchmarks deliberately use only the
  overlap. Speed on a subset is not parity — it's just a fair snapshot of the
  engine's execution model.

## Reproducing

```sh
./build/rakupp tools/run-bench.raku          # checks all three engines agree, then times them
```

The harness compiles each program with `--exe` for the native column; the
benchmark programs are plain, readable Raku in `tools/bench/*.raku` (edit or add
freely).

_Snapshot taken 2026-07-22 with Raku++ 1.0.0 at 583 / 1,462 Roast files fully
passing, on Darwin 24.6 against Rakudo v2026.06 (kernels: best of 6 harness
runs; `-O` kernels: best of 5; YAMLish: best of 5, re-measured). The YAMLish
rows drifted ~8% slower than the 2026-07-20 snapshot (0.50→0.54 s single
parse) — snapshot-binary bisect shows the cost accreted gradually across the
90%-campaign legs (5.06 s → 5.21 s → 5.50 s on the 10-parse row, Jul 15 → 19 →
22), spread over the parse/regex hot paths rather than one change; profiling
shows no single new hotspot. Tracked as a post-1.0 item._

_**v1.1.0 (2026-07-24) re-run** (598 / 1,462 files fully passing): the
release-gate benchmark pass found **no regression**. All engines produced
identical output, and every engine-vs-engine ratio held within a hair of the
1.0.0 snapshot (strcat 14.1× vs 14.4×, hash 5.7× vs 6.0×, loopsum 1.3× vs 1.4×,
fib Rakudo-1.9× vs 1.8×). Absolute wall-clock ran ~5% higher uniformly, but the
measurement machine had heavy background load at the time (macOS `photoanalysisd`
pegged near 85% of a core, load-avg ~3), which inflates every row equally and
leaves the ratios intact — so the pristine 1.0.0 absolute numbers above are
retained pending a quiet-machine re-snapshot. The typed-blob / `.lines` /
`signal()` / module work in v1.1.0 does not touch these kernels' hot paths._
