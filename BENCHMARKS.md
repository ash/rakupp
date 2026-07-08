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

- **Startup:** Raku++ is dramatically faster either way — ~3 ms cold vs
  Rakudo's ~100 ms. For one-liners, CLI glue, and small programs it feels instant.
- **Native (`--exe`) beats Rakudo on every benchmark here** — from 2.4× on
  `fib` to 7.4× on `loopsum` and 17× on `strcat`. Compiling removes interpreter
  overhead; the collection/hash workloads land 2.4–6.7× ahead.
- **`fib` used to be Rakudo's one win** — deep recursion of a tiny body is where
  an optimizing JIT is hardest to beat — but hot-pathing integer arithmetic in
  the runtime pulled native Raku++ to ~2.4× *ahead*.
- Even the **interpreter** beats Rakudo on most of these — its startup and lean
  operations outweigh Rakudo's JIT except on the two heaviest loop/recursion
  kernels (`loopsum`, roughly even; `fib`), where compiling (`--exe`) takes over.
- **String building (`~=`) appends in place** in every mode (interp and `--exe`),
  so `strcat` is now O(n) rather than O(n²) — the interpreter went from 82 ms to
  13 ms, and it's no longer the interpreter's weakest row.

## Methodology

- **Machine:** macOS (Darwin 25.5).
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
- **Harness overhead:** spawning + capturing a subprocess adds a small fixed cost
  per run to every engine (on this machine the `startup` row reads ~2.5 ms, close
  to the bare-binary ~3 ms). It hits all three equally.

## Results

Best of 6 runs through the harness (includes process startup); lower is better.
Rows are ordered most-Raku++-favourable first.

### Interpreter vs Rakudo

Even without compiling, the tree-walker wins on most of these — startup, string,
regex, and the collection/hash workloads; Rakudo's VM only leads on the heaviest
loop and recursion.

| Benchmark | Raku++ (interp) | Rakudo | Faster |
|---|---:|---:|---|
| startup  | 3.4 ms    | 109.7 ms | **Raku++ 32×**  |
| strcat   | 13.5 ms   | 128.8 ms | **Raku++ 9.5×** |
| bigint   | 44.8 ms   | 217.2 ms | **Raku++ 4.8×** |
| sortnums | 72.0 ms   | 246.0 ms | **Raku++ 3.4×** |
| hash     | 53.1 ms   | 164.9 ms | **Raku++ 3.1×** |
| regex    | 98.9 ms   | 227.4 ms | **Raku++ 2.3×** |
| arrayops | 100.5 ms  | 235.2 ms | **Raku++ 2.3×** |
| loopsum  | 265.9 ms  | 241.6 ms | Rakudo 1.1× |
| fib      | 1032.2 ms | 391.0 ms | Rakudo 2.6× |

### Native (`--exe`) vs Rakudo

Compiling removes interpreter overhead on top of that — pushing **every row
clearly ahead of Rakudo**, `fib` now included. The last column is the speed-up
over interpreting the same program.

| Benchmark | Raku++ (`--exe`) | Rakudo | Faster | vs interp |
|---|---:|---:|---|---:|
| startup  | 2.6 ms    | 109.7 ms | **Raku++ 42×**  | 1.3× |
| strcat   | 7.4 ms    | 128.8 ms | **Raku++ 17.4×** | 1.8× |
| loopsum  | 32.7 ms   | 241.6 ms | **Raku++ 7.4×** | 8.1× |
| hash     | 24.5 ms   | 164.9 ms | **Raku++ 6.7×** | 2.2× |
| bigint   | 43.0 ms   | 217.2 ms | **Raku++ 5.1×** | 1.0× |
| sortnums | 53.6 ms   | 246.0 ms | **Raku++ 4.6×** | 1.3× |
| regex    | 80.3 ms   | 227.4 ms | **Raku++ 2.8×** | 1.2× |
| arrayops | 97.6 ms   | 235.2 ms | **Raku++ 2.4×** | 1.0× |
| fib      | 165.0 ms  | 391.0 ms | **Raku++ 2.4×** | 6.2× |

**Reading the `vs interp` column:** compiling helps most where a tree-walker
hurts — `loopsum` 8.1×, `fib` 6.2× (both re-dispatch a tiny body a huge number of
times). It's a near no-op (1.0–1.3×) for the workloads whose time is spent
*inside* runtime methods — `arrayops`/`sortnums` (`.grep`/`.map`/`.sort`) and
especially `bigint`, which lives almost entirely in `BigInt` multiply. There the
driving loop is trivial, so removing interpreter overhead changes little.

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

- **Startup wins are real and matter.** A ~3 ms cold start vs ~100 ms is the
  difference between "instant" and "noticeable" for scripts, editor tooling, and
  shelling out in a loop. This is the natural advantage of a tiny native binary
  with no VM to spin up.
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

_Snapshot taken with Raku++ at 280 / 1,464 Roast files fully passing, on
Darwin 25.5 against Rakudo v2026.06 (kernels: best of 6 harness runs; YAMLish:
best of 5)._
