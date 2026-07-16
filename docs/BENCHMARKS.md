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
WebAssembly — is measured against `interp` on these same kernels in
[rakujs/README.md](../rakujs/README.md#performance-vs-native): 3.4–27× slower
than native, dominated by the `-fexceptions` call trampolines.)

## The short version

- **Startup:** ~2 ms cold on this machine (best of a 200-spawn loop: 1.8 ms) —
  two orders of magnitude quicker than a VM-backed engine (~166 ms). For
  one-liners, CLI glue, and small programs it is instant.
- **Native (`--exe`) beats Rakudo on every benchmark here** — from 2.7× on
  `arrayops` to 9.6× on `loopsum`, 13.9× on `hash`, and 39× on `strcat`.
  Compiling removes interpreter overhead.
- **`fib` is Rakudo's one remaining interpreter win (1.7×)** — deep recursion
  of a tiny body is where an optimizing JIT is hardest to beat. Compiling takes
  over: `--exe` puts it 2.8× *ahead*.
- Even the **interpreter** beats Rakudo on 8 of 9 — everything except `fib`,
  including the heavy `loopsum` loop kernel (1.5×) that Rakudo's JIT used to
  lead.
- **String building (`~=`) appends in place** in every mode, so `strcat` is
  O(n) rather than O(n²) — 16× ahead of Rakudo even interpreted.

## Methodology

- **Machine:** macOS (Darwin 24.6), measured 2026-07-16 on an otherwise idle
  machine. (An earlier revision of these tables was measured under desktop
  ambient load; on top of that, Raku++'s call path and runtime gained real
  speed between the two measurements, so rows are not comparable across
  revisions — the Rakudo column, measured both times, moved only a little.)
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
  cost per run to every engine (the `startup` row reads 2.1 ms through the
  harness vs 1.8 ms for a bare 200-spawn loop). It hits all three equally.

## Results

Best of 6 runs through the harness (includes process startup); lower is better.
Rows are ordered most-Raku++-favourable first.

### Interpreter vs Rakudo

Even without compiling, the tree-walker wins on all of these except the
recursion kernel — Rakudo's VM leads only on `fib`.

| Benchmark | Raku++ (interp) | Rakudo | Faster |
|---|---:|---:|---|
| startup  | 2.1 ms   | 166.0 ms | **Raku++ 79×** |
| strcat   | 11.5 ms  | 185.8 ms | **Raku++ 16.2×** |
| bigint   | 31.6 ms  | 256.8 ms | **Raku++ 8.1×** |
| hash     | 35.9 ms  | 227.6 ms | **Raku++ 6.3×** |
| sortnums | 64.4 ms  | 256.0 ms | **Raku++ 4.0×** |
| regex    | 80.8 ms  | 283.3 ms | **Raku++ 3.5×** |
| arrayops | 109.7 ms | 288.4 ms | **Raku++ 2.6×** |
| loopsum  | 179.6 ms | 265.2 ms | **Raku++ 1.5×** |
| fib      | 768.6 ms | 461.6 ms | Rakudo 1.7× |

### Native (`--exe`) vs Rakudo

Compiling removes interpreter overhead on top of that — pushing **every row
clearly ahead of Rakudo**, `fib` included. The last column is the speed-up
over interpreting the same program.

| Benchmark | Raku++ (`--exe`) | Rakudo | Faster | vs interp |
|---|---:|---:|---|---:|
| startup  | 1.8 ms   | 166.0 ms | **Raku++ 92×**   | 1.2× |
| strcat   | 4.8 ms   | 185.8 ms | **Raku++ 39×**   | 2.4× |
| hash     | 16.4 ms  | 227.6 ms | **Raku++ 13.9×** | 2.2× |
| loopsum  | 27.7 ms  | 265.2 ms | **Raku++ 9.6×**  | 6.5× |
| bigint   | 30.2 ms  | 256.8 ms | **Raku++ 8.5×**  | 1.0× |
| sortnums | 53.1 ms  | 256.0 ms | **Raku++ 4.8×**  | 1.2× |
| regex    | 63.3 ms  | 283.3 ms | **Raku++ 4.5×**  | 1.3× |
| fib      | 167.5 ms | 461.6 ms | **Raku++ 2.8×**  | 4.6× |
| arrayops | 106.4 ms | 288.4 ms | **Raku++ 2.7×**  | 1.0× |

**Reading the `vs interp` column:** compiling helps most where a tree-walker
hurts — `loopsum` 6.5×, `fib` 4.6× (both re-dispatch a tiny body a huge number
of times). It's a near no-op (1.0–1.3×) for the workloads whose time is spent
*inside* runtime methods — `arrayops`/`sortnums` (`.grep`/`.map`/`.sort`) and
especially `bigint`, which lives almost entirely in `BigInt` multiply. There
the driving loop is trivial, so removing interpreter overhead changes little.

`fib` — a tiny function called 1.6M times, the case a JIT specializes best — used
to be the one place Rakudo led even the default `--exe`; hot-pathing integer
arithmetic in the runtime (`applyArith`) closed that gap and put native ~2.8×
ahead.

### `-O` (the optimizer flag)

The `native` column above is the default `--exe`. Adding **`-O`** enables two
speculative codegen passes:

1. **direct-arity calls** — a fixed-arity positional sub gets direct `Value`
   parameters (plus a boxed adapter), skipping the per-call `ValueList` heap
   allocation;
2. **inline int arithmetic** — `+ - * ** % %% < <= > >= == !=` emit inline helpers
   that do the small-int case as native `int64` (overflow promotes to bignum),
   instead of the string-dispatched `applyArith`.

(In-place `~=` string building is *not* one of these — it is now the default in
both the interpreter and `--exe`.) Measured by
[`tools/run-optbench.raku`](../tools/run-optbench.raku) on five showcase kernels
written to exercise the passes (each program is verified to produce identical
output all three ways before timing). Best of 5 runs; Rakudo (v2026.06) shown
for reference:

| Benchmark | `--exe` | `--exe -O` | `-O` vs `--exe` | Rakudo | showcases |
|---|---:|---:|---:|---:|---|
| powmod      | 557.9 ms  | **51.1 ms**  | **10.9×** | 730.1 ms  | 1M `** 3` then `% 1000` — inline pow + mod |
| sieve       | 1028.7 ms | **368.6 ms** | **2.8×**  | 1006.0 ms | primes < 200k by trial division — inline `* <= %%` |
| fibcalls    | 693.2 ms  | **279.3 ms** | **2.5×**  | 1387.2 ms | fib(32) — direct-arity calls + inline `< + -` |
| intsum      | 290.1 ms  | 249.2 ms     | 1.2×      | 644.3 ms  | 5M int accumulation — inline `+ - *` |
| stringbuild | 23.8 ms   | 24.2 ms      | 1.0×      | 220.1 ms  | 400k `~=` appends — in-place O(n) string build |

`-O` buys the most where the generated code otherwise round-trips every
operation through the generic runtime: `powmod` (10.9× — `**` and `%` become
inline int ops) and the call-bound `fibcalls` (2.5× — direct-arity calls skip
the per-call `ValueList` heap allocation). `sieve` is the honest caveat at
plain `--exe`: its trial-division inner loop is the one kernel here where
default codegen ties Rakudo (1028.7 vs 1006.0 ms) — and `-O` flips it to 2.7×
ahead. `stringbuild` gains nothing because in-place append is already the
default everywhere. It's opt-in, off by default, and produces identical output.
See [OPTIMIZATION.md](OPTIMIZATION.md) for what each pass emits and the C++
optimization-level forwarding (`-O3`/`-Os`/`-Ofast`).

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

- **Startup wins are real and matter.** A ~2 ms cold start (vs ~166 ms on this
  machine) is the difference between "instant" and "noticeable" for scripts,
  editor tooling, and shelling out in a loop. This is the natural advantage of a
  tiny native binary with no VM to spin up.
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

_Snapshot taken 2026-07-16 with Raku++ 0.7.0 at 501 / 1,462 Roast files fully
passing, on Darwin 24.6 against Rakudo v2026.06 (kernels: best of 6 harness
runs; `-O` kernels: best of 5; YAMLish: best of 5)._
