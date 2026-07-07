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
- **Native (`--exe`) beats Rakudo on every benchmark here except `fib`.**
  Compiling removes interpreter overhead; on `loopsum` it's 2.9×, and on the
  collection/hash workloads 2.4–4.9× ahead of Rakudo.
- **`fib` is the one Rakudo win** — deep recursion of a tiny body is where an
  optimizing JIT is hardest to beat; native Raku++ lands ~1.4× behind.
- Even the **interpreter** beats Rakudo on most of these — its startup and lean
  operations outweigh Rakudo's JIT except on the heaviest loops/recursion
  (`loopsum`, `fib`), where compiling (`--exe`) takes over.

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
| startup  | 2.5 ms    | 104.4 ms | **Raku++ 42×**  |
| bigint   | 44.5 ms   | 211.7 ms | **Raku++ 4.8×** |
| sortnums | 81.3 ms   | 238.2 ms | **Raku++ 2.9×** |
| hash     | 63.5 ms   | 160.7 ms | **Raku++ 2.5×** |
| arrayops | 97.8 ms   | 224.7 ms | **Raku++ 2.3×** |
| regex    | 100.7 ms  | 221.4 ms | **Raku++ 2.2×** |
| strcat   | 82.0 ms   | 124.6 ms | **Raku++ 1.5×** |
| loopsum  | 325.1 ms  | 235.8 ms | Rakudo 1.4× |
| fib      | 1576.9 ms | 382.8 ms | Rakudo 4.1× |

### Native (`--exe`) vs Rakudo

Compiling removes interpreter overhead on top of that — pushing **every row
except `fib` clearly ahead of Rakudo**. The last column is the speed-up over
interpreting the same program.

| Benchmark | Raku++ (`--exe`) | Rakudo | Faster | vs interp |
|---|---:|---:|---|---:|
| startup  | 2.5 ms    | 104.4 ms | **Raku++ 42×**  | 1.0× |
| bigint   | 42.4 ms   | 211.7 ms | **Raku++ 5.0×** | 1.0× |
| hash     | 32.7 ms   | 160.7 ms | **Raku++ 4.9×** | 1.9× |
| sortnums | 59.0 ms   | 238.2 ms | **Raku++ 4.0×** | 1.4× |
| loopsum  | 82.7 ms   | 235.8 ms | **Raku++ 2.9×** | 3.9× |
| regex    | 82.2 ms   | 221.4 ms | **Raku++ 2.7×** | 1.2× |
| strcat   | 50.4 ms   | 124.6 ms | **Raku++ 2.5×** | 1.6× |
| arrayops | 93.9 ms   | 224.7 ms | **Raku++ 2.4×** | 1.0× |
| fib      | 548.8 ms  | 382.8 ms | Rakudo 1.4×     | 2.9× |

**Reading the `vs interp` column:** compiling helps most where a tree-walker
hurts — `loopsum` 3.9×, `fib` 2.9× (both re-dispatch a tiny body a huge number of
times). It's a near no-op (1.0–1.4×) for the workloads whose time is spent
*inside* runtime methods — `arrayops`/`sortnums` (`.grep`/`.map`/`.sort`) and
especially `bigint`, which lives almost entirely in `BigInt` multiply. There the
driving loop is trivial, so removing interpreter overhead changes little.

`fib` is the one benchmark where Rakudo's JIT pulls ahead: a tiny function called
1.6M times is exactly what a JIT specializes best, and native Raku++ lands ~1.4×
behind it. The `native` column above is the default `--exe`; adding **`-O`** (the
direct-arity-call pass — fixed-arity subs get direct `Value` params instead of a
per-call `ValueList` allocation) takes `fib` from 548 ms to **~450 ms**, closing
the Rakudo gap to ~1.18×. The remaining gap is boxed, string-dispatched
arithmetic (`applyArith`) — int specialization is the next lever.

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

_Snapshot taken with Raku++ at 275 / 1,464 Roast files fully passing, on
Darwin 25.5 against Rakudo v2026.06 (kernels: best of 3 harness runs; YAMLish:
best of 5)._
