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
separately (e.g. `--aot` fib is 1810 ms vs interp's 1820 ms). `--exe` is the only
mode that changes runtime performance, so it's the one the `native` column
tracks.

## The short version

- **Startup:** Raku++ is dramatically faster either way — ~13 ms cold vs
  Rakudo's ~160 ms. For one-liners, CLI glue, and small programs it feels instant.
- **Native (`--exe`) now beats Rakudo on every benchmark here except `fib`**
  (which is a tie). Compiling removes interpreter overhead; on `loopsum` it's
  2.8×, and on the collection/hash workloads 2.3–4.7× ahead of Rakudo.
- **`fib` is the lone tie** — deep recursion of a tiny body is where an
  optimizing JIT is hardest to beat; native Raku++ lands level with Rakudo.
- Even the **interpreter** beats Rakudo on most of these — its startup and lean
  operations outweigh Rakudo's JIT except on the heaviest loops/recursion
  (`loopsum`, `fib`), where compiling (`--exe`) takes over.

## Methodology

- **Machine:** macOS (Darwin 24.6).
- **Raku++:** built `-O3 -DNDEBUG` (CMake Release).
- **Rakudo:** `raku` v6.d (MoarVM backend). Both engines report `v6.d`.
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
- **Harness overhead:** spawning + capturing a subprocess adds a few ms of fixed
  cost per run to every engine (so the `startup` row reads ~17 ms rather than the
  bare-binary ~13 ms). It hits all three equally.

## Results

Best of 6 runs through the harness (includes process startup); lower is better.
Rows are ordered most-Raku++-favourable first.

### Interpreter vs Rakudo

Even without compiling, the tree-walker wins on most of these — startup, string,
regex, and the collection/hash workloads; Rakudo's VM only leads on the heaviest
loop and recursion.

| Benchmark | Raku++ (interp) | Rakudo | Faster |
|---|---:|---:|---|
| startup  | 18.8 ms   | 166.8 ms | **Raku++ 8.9×** |
| bigint   | 50.7 ms   | 257.6 ms | **Raku++ 5.1×** |
| regex    | 93.2 ms   | 291.4 ms | **Raku++ 3.1×** |
| hash     | 75.6 ms   | 232.1 ms | **Raku++ 3.1×** |
| sortnums | 107.2 ms  | 259.2 ms | **Raku++ 2.4×** |
| arrayops | 131.6 ms  | 285.1 ms | **Raku++ 2.2×** |
| strcat   | 90.1 ms   | 188.5 ms | **Raku++ 2.1×** |
| loopsum  | 332.7 ms  | 271.8 ms | Rakudo 1.2× |
| fib      | 1824.3 ms | 470.8 ms | Rakudo 3.9× |

### Native (`--exe`) vs Rakudo

Compiling removes interpreter overhead on top of that — pushing **every row
except `fib` clearly ahead of Rakudo**. The last column is the speed-up over
interpreting the same program.

| Benchmark | Raku++ (`--exe`) | Rakudo | Faster | vs interp |
|---|---:|---:|---|---:|
| startup  | 19.9 ms   | 166.8 ms | **Raku++ 8.4×** | 1.0× |
| bigint   | 52.0 ms   | 257.6 ms | **Raku++ 5.0×** | 1.0× |
| hash     | 49.4 ms   | 232.1 ms | **Raku++ 4.7×** | 1.5× |
| regex    | 71.5 ms   | 291.4 ms | **Raku++ 4.1×** | 1.3× |
| sortnums | 75.9 ms   | 259.2 ms | **Raku++ 3.4×** | 1.4× |
| strcat   | 56.9 ms   | 188.5 ms | **Raku++ 3.3×** | 1.6× |
| loopsum  | 96.0 ms   | 271.8 ms | **Raku++ 2.8×** | 3.5× |
| arrayops | 123.9 ms  | 285.1 ms | **Raku++ 2.3×** | 1.1× |
| fib      | 480.8 ms  | 470.8 ms | tie             | 3.8× |

**Reading the `vs interp` column:** compiling helps most where a tree-walker
hurts — `loopsum` 3.5×, `fib` 3.8× (both re-dispatch a tiny body a huge number of
times). It's a near no-op (1.0–1.5×) for the workloads whose time is spent
*inside* runtime methods — `arrayops`/`sortnums`/`hash` (`.grep`/`.map`/`.sort`,
hashing) and especially `bigint`, which lives almost entirely in `BigInt`
multiply. There the driving loop is trivial, so removing interpreter overhead
changes little.

`fib` is the one benchmark where Rakudo's JIT stays level: a tiny function called
1.6M times is exactly what a JIT specializes best, and native Raku++ ties it.

### Real-world: grammar parsing (YAMLish)

The benchmarks above are small kernels. This one is a whole real module doing
real work: the zef-installed **YAMLish** grammar (unmodified) parsing the Raku
course's table-of-contents YAML (`_data/toc/en.yaml`, 2471 lines) with
`load-yamls`. It exercises the backtracking grammar engine — subrules, LTM `|`,
lookbehind assertions, `:my` side-effects, indentation-parameterised rules, and
action-method tree building — against the same source under both engines.

Best of 5 runs, wall-clock (`time`), lower is better.

| Workload | Raku++ (interp) | Rakudo | Faster |
|---|---:|---:|---|
| load-yamls, one parse per process | 0.55 s | 1.06 s | **Raku++ 1.9×** |
| load-yamls, 10 parses in-process   | 5.46 s | 6.92 s | **Raku++ 1.3×** |

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

- **Startup wins are real and matter.** A ~13 ms cold start vs ~160 ms is the
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

_Snapshot taken with Raku++ at 251 / 1,464 Roast files fully passing._
