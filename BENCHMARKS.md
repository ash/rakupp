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
separately (`--aot` fib runs at interp's ~1580 ms). `--exe` is the only
mode that changes runtime performance, so it's the one the `native` column
tracks.

## The short version

- **Startup:** ~12 ms cold on this machine (best of a 200-spawn loop) — an
  order of magnitude quicker than a VM-backed engine. For one-liners, CLI glue,
  and small programs it feels instant. (The `startup` table rows below read
  higher — ~16–21 ms — because they include the harness's subprocess capture.)
- **Native (`--exe`) beats Rakudo on every benchmark here** — from 1.8× on
  `arrayops` to 6× on `loopsum`/`hash` and 9.5× on `strcat`. Compiling removes
  interpreter overhead.
- **`fib` used to be Rakudo's one win** — deep recursion of a tiny body is where
  an optimizing JIT is hardest to beat — but hot-pathing integer arithmetic in
  the runtime keeps native Raku++ ~2.1× *ahead*.
- Even the **interpreter** beats Rakudo on 7 of 9 — startup, string, regex, and
  the collection/hash workloads; Rakudo's JIT leads only on the two heaviest
  loop/recursion kernels (`loopsum` 1.4×, `fib` 3.5×), where compiling
  (`--exe`) takes over.
- **String building (`~=`) appends in place** in every mode, so `strcat` is
  O(n) rather than O(n²) — 5× ahead of Rakudo even interpreted.

## Methodology

- **Machine:** macOS (Darwin 24.6), measured 2026-07-11 under normal desktop
  ambient load — absolute times on an idle machine run lower for *both*
  engines; the ratios were stable across two full harness runs.
- **Raku++:** built `-O3 -DNDEBUG` (CMake Release).
- **Rakudo:** `raku` v2026.06 (MoarVM backend).
- **Harness:** [`tools/run-bench.raku`](tools/run-bench.raku) — itself a Raku
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
  cost per run to every engine (the `startup` row reads ~20 ms through the
  harness vs ~12 ms for a bare 200-spawn loop). It hits all three equally.

## Results

Best of 6 runs through the harness (includes process startup); lower is better.
Rows are ordered most-Raku++-favourable first.

### Interpreter vs Rakudo

Even without compiling, the tree-walker wins on most of these — startup, string,
regex, and the collection/hash workloads; Rakudo's VM only leads on the heaviest
loop and recursion.

| Benchmark | Raku++ (interp) | Rakudo | Faster |
|---|---:|---:|---|
| startup  | 20.7 ms   | 167.3 ms | **Raku++ 8.1×** |
| strcat   | 37.4 ms   | 195.1 ms | **Raku++ 5.2×** |
| bigint   | 55.2 ms   | 261.4 ms | **Raku++ 4.7×** |
| hash     | 83.9 ms   | 235.6 ms | **Raku++ 2.8×** |
| sortnums | 121.5 ms  | 262.3 ms | **Raku++ 2.2×** |
| regex    | 148.3 ms  | 294.1 ms | **Raku++ 2.0×** |
| arrayops | 164.5 ms  | 286.3 ms | **Raku++ 1.7×** |
| loopsum  | 384.1 ms  | 274.6 ms | Rakudo 1.4× |
| fib      | 1693.5 ms | 480.0 ms | Rakudo 3.5× |

### Native (`--exe`) vs Rakudo

Compiling removes interpreter overhead on top of that — pushing **every row
clearly ahead of Rakudo**, `fib` included. The last column is the speed-up
over interpreting the same program.

| Benchmark | Raku++ (`--exe`) | Rakudo | Faster | vs interp |
|---|---:|---:|---|---:|
| startup  | 16.1 ms  | 167.3 ms | **Raku++ 10.4×** | 1.3× |
| strcat   | 20.6 ms  | 195.1 ms | **Raku++ 9.5×**  | 1.8× |
| loopsum  | 45.0 ms  | 274.6 ms | **Raku++ 6.1×**  | 8.5× |
| hash     | 38.7 ms  | 235.6 ms | **Raku++ 6.1×**  | 2.2× |
| bigint   | 49.4 ms  | 261.4 ms | **Raku++ 5.3×**  | 1.1× |
| sortnums | 79.9 ms  | 262.3 ms | **Raku++ 3.3×**  | 1.5× |
| regex    | 108.4 ms | 294.1 ms | **Raku++ 2.7×**  | 1.4× |
| fib      | 228.8 ms | 480.0 ms | **Raku++ 2.1×**  | 7.4× |
| arrayops | 158.7 ms | 286.3 ms | **Raku++ 1.8×**  | 1.0× |

**Reading the `vs interp` column:** compiling helps most where a tree-walker
hurts — `loopsum` 8.5×, `fib` 7.4× (both re-dispatch a tiny body a huge number
of times). It's a near no-op (1.0–1.5×) for the workloads whose time is spent
*inside* runtime methods — `arrayops`/`sortnums` (`.grep`/`.map`/`.sort`) and
especially `bigint`, which lives almost entirely in `BigInt` multiply. There
the driving loop is trivial, so removing interpreter overhead changes little.

`fib` — a tiny function called 1.6M times, the case a JIT specializes best — used
to be the one place Rakudo led even the default `--exe`; hot-pathing integer
arithmetic in the runtime (`applyArith`) closed that gap and put native ~2.4×
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
both the interpreter and `--exe`.) Best of 8 runs; Rakudo (v2026.06) shown for
reference:

| Benchmark | `--exe` | `--exe -O` | Rakudo | `-O` vs `--exe` |
|---|---:|---:|---:|---:|
| fib      | 165 ms | **66 ms** | 391 ms | **2.5×** |
| arrayops | 97 ms  | **77 ms** | 235 ms | 1.3× |
| loopsum  | 32 ms  | 30 ms  | 242 ms | 1.1× |
| hash     | 24 ms  | 24 ms  | 165 ms | 1.0× |
| sortnums | 53 ms  | 53 ms  | 246 ms | 1.0× |
| regex    | 80 ms  | 80 ms  | 227 ms | 1.0× |
| bigint   | 43 ms  | 43 ms  | 217 ms | 1.0× |
| strcat   | 8 ms   | 7 ms   | 129 ms | 1.0× |

`-O`'s margin over plain `--exe` **narrowed** once the runtime's `applyArith`
grew a char-dispatched `Int`/`Int` fast path — `--exe` links that runtime, so it
no longer pays string-dispatch for `+ - * < … %`. What `-O` still
buys is what it removes *entirely*: the per-call `ValueList` (direct-arity
calls — the 2.5× on `fib`) and, in `arrayops`, the `Value` boxing in the map/grep
bodies. Every kernel here is already ahead of Rakudo at plain `--exe`; `-O`
extends the lead where calls or boxing dominate. It's opt-in, off by default, and
produces identical output. See [OPTIMIZATION.md](OPTIMIZATION.md) for what each
pass emits and the C++ optimization-level forwarding (`-O3`/`-Os`/`-Ofast`).

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
| load-yamls, one parse per process | 0.62 s | 0.91 s | **Raku++ 1.5×** |
| load-yamls, 10 parses in-process   | 6.23 s | 7.25 s | **Raku++ 1.2×** |

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

- **Startup wins are real and matter.** A ~12 ms cold start (vs ~150 ms on this
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

_Snapshot taken with Raku++ at 281 / 1,464 Roast files fully passing, on
Darwin 25.5 against Rakudo v2026.06 (kernels: best of 6 harness runs; YAMLish:
best of 5)._
