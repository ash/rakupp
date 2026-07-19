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
- **Native (`--exe`) beats Rakudo on every benchmark here** — from 2.7× on
  `arrayops` to 10.2× on `loopsum`, 13.7× on `hash`, and 41× on `strcat`.
  Compiling removes interpreter overhead.
- **Rakudo's JIT keeps two interpreter wins**: `fib` (1.7×) — deep recursion of
  a tiny body — and `streq` (2.0×; string comparisons walk the interpreter's
  operator-dispatch chain). Compiling flips both: `--exe` puts `fib` 2.8× ahead
  and `streq` 6.6× ahead (string `eq`/`lt` compile to inline byte-compares — see
  [dev/DISPATCH.md](dev/DISPATCH.md) for the dispatch story).
- Even the **interpreter** beats Rakudo on 7 of 9 — everything except `fib` and
  `streq`, including the `loopsum` loop kernel (1.2×).
- **String building (`~=`) appends in place** in every mode, so `strcat` is
  O(n) rather than O(n²) — 14× ahead of Rakudo even interpreted.

## Methodology

- **Machine:** macOS (Darwin 24.6), measured 2026-07-19 on a lightly loaded
  desktop. (Rows are not comparable across doc revisions — the engine's speed
  shifts between measurements, in both directions: the compiled `--exe` and
  `-O` paths held steady from the previous snapshot, while the interpreter's
  hot-loop kernels — `loopsum`, `hash`, `sortnums`, `streq` — regressed ~8–22%
  as feature code accumulated in the eval/exec path over the v0.7.1→v0.9.0
  cycle. The Rakudo column, measured every time, moves only a little and is the
  fixed yardstick.)
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
- **Fairness:** every benchmark program is byte-for-byte identical across all
  three and was verified to produce identical output before timing.
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
| strcat   | 13.5 ms  | 186.9 ms | **Raku++ 13.8×** |
| bigint   | 32.2 ms  | 258.6 ms | **Raku++ 8.0×** |
| hash     | 39.9 ms  | 230.6 ms | **Raku++ 5.8×** |
| sortnums | 71.1 ms  | 261.8 ms | **Raku++ 3.7×** |
| regex    | 85.5 ms  | 291.5 ms | **Raku++ 3.4×** |
| arrayops | 110.4 ms | 291.1 ms | **Raku++ 2.6×** |
| loopsum  | 222.3 ms | 273.3 ms | **Raku++ 1.2×** |
| streq    | 591.1 ms | 290.5 ms | Rakudo 2.0× |
| fib      | 791.2 ms | 475.6 ms | Rakudo 1.7× |

### Native (`--exe`) vs Rakudo

Compiling removes interpreter overhead on top of that — pushing **every row
clearly ahead of Rakudo**, `fib` and `streq` included. The last column is the
speed-up over interpreting the same program.

| Benchmark | Raku++ (`--exe`) | Rakudo | Faster | vs interp |
|---|---:|---:|---|---:|
| strcat   | 4.5 ms   | 186.9 ms | **Raku++ 41.5×** | 3.0× |
| hash     | 16.8 ms  | 230.6 ms | **Raku++ 13.7×** | 2.4× |
| loopsum  | 26.8 ms  | 273.3 ms | **Raku++ 10.2×** | 8.3× |
| bigint   | 31.1 ms  | 258.6 ms | **Raku++ 8.3×**  | 1.0× |
| streq    | 44.1 ms  | 290.5 ms | **Raku++ 6.6×**  | 13.4× |
| sortnums | 53.7 ms  | 261.8 ms | **Raku++ 4.9×**  | 1.3× |
| regex    | 66.5 ms  | 291.5 ms | **Raku++ 4.4×**  | 1.3× |
| fib      | 169.3 ms | 475.6 ms | **Raku++ 2.8×**  | 4.7× |
| arrayops | 107.7 ms | 291.1 ms | **Raku++ 2.7×**  | 1.0× |

**Reading the `vs interp` column:** compiling helps most where a tree-walker
hurts — `streq` 13.4× (per-node walking around what is, after the fast path, a
trivial byte-compare — see [dev/DISPATCH.md](dev/DISPATCH.md)), `loopsum` 8.3×,
`fib` 4.7× (both re-dispatch a tiny body a huge number of times). It's a near
no-op (1.0–1.3×) for the workloads whose time is spent *inside* runtime
methods — `arrayops`/`sortnums` (`.grep`/`.map`/`.sort`) and especially
`bigint`, which lives almost entirely in `BigInt` multiply. There the driving
loop is trivial, so removing interpreter overhead changes little.

`fib` — a tiny function called 1.6M times, the case a JIT specializes best — used
to be the one place Rakudo led even the default `--exe`; hot-pathing integer
arithmetic in the runtime (`applyArith`) closed that gap and put native ~2.7×
ahead. `streq` got the same treatment on 2026-07-17: string comparisons used to
walk `applyArith`'s full dispatch chain (~118 ns per `eq`). Compiled code now
emits inline plain-`Str` byte-compares and calls builtins through pointers
resolved once at startup; the interpreter gained a matching char-dispatched
Str/Str fast path at the top of `applyArith` (909.7 → 547.9 ms — the remaining
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
| sieve       | 1037.7 ms | **25.7 ms**  | **40.4×** | 1000.7 ms | primes < 200k by trial division — `* <= %%` all laned |
| powmod      | 559.7 ms  | **51.0 ms**  | **11.0×** | 754.6 ms  | 1M `** 3` then `% 1000` — inline pow + mod lane |
| intsum      | 283.4 ms  | **30.4 ms**  | **9.3×**  | 664.0 ms  | 5M int accumulation — `+=` lane, zero boxing |
| fibcalls    | 699.8 ms  | **194.5 ms** | **3.6×**  | 1413.3 ms | fib(32) — direct-arity calls + int-lane condition |
| stringbuild | 23.5 ms   | 22.6 ms      | 1.0×      | 222.3 ms  | 400k `~=` appends — in-place O(n) string build |

The lanes (pass 3) dominate this table: `sieve`'s inner loop — `while $d * $d
<= $n`, `if $n %% $d`, `$d++` — runs as raw `int64`, taking it from a tie with
Rakudo at plain `--exe` to 39× ahead, and `intsum` shed its four
per-iteration `Value` constructions. On the main kernels above, `-O` puts fib
at 47.0 ms (3.5× over `--exe`, 9.6× over Rakudo), loopsum at 8.6 ms, and streq
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
| load-yamls, one parse per process | 0.50 s | 0.97 s | **Raku++ 1.9×** |
| load-yamls, 10 parses in-process   | 5.03 s | 6.37 s | **Raku++ 1.3×** |

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
./build/rakupp tools/run-bench.raku          # times interp + native + rakudo, prints the table
```

The harness compiles each program with `--exe` for the native column; the
benchmark programs are plain, readable Raku in `tools/bench/*.raku` (edit or add
freely).

_Snapshot taken 2026-07-19 with Raku++ 0.9.0 at 533 / 1,462 Roast files fully
passing, on Darwin 24.6 against Rakudo v2026.06 (kernels: best of 6 harness
runs; `-O` kernels: best of 5; YAMLish: best of 5, unchanged from 2026-07-16)._
