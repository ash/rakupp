# Interpreter vs compiled (`--exe`) — the examples

Every program in [examples/](../examples/) compiles to a standalone native binary
with `rakupp --exe`, and every binary produces byte-identical output to the
interpreter run (`life.raku` is seeded random, so its check uses an
`srand`-pinned copy). This page compares the two modes of Raku++ itself —
interpreting the source versus running the transpiled-to-C++ binary. It is not
a comparison with any other Raku implementation.

```sh
build/rakupp examples/mandel.raku            # interp: tree-walk the source
build/rakupp --exe -o mandel examples/mandel.raku
./mandel                                     # native: no interpreter loop inside
```

`--bundle` and `--aot` also produce standalone binaries but tree-walk the
program, so they run at interp speed; `--exe` is the mode measured here.

## Numbers

User CPU time, best of 3, arm64 build on an M3 (macOS). Measured 2026-07-12.
`life` runs with `--delay=0` (its `sleep` would otherwise dominate);
`echo-server`, `sleep-sort`, and `parallel` are excluded — their runtime is
network/timer wall-clock, not compute.

**User CPU is not wall clock** — don't compare these numbers with the
`startup` row in [BENCHMARKS.md](BENCHMARKS.md). That row is wall time
through the bench harness (spawn + output capture, 16–21 ms there, ~12 ms
for a bare cold start). User CPU counts only the cycles the program itself
burns — process creation, the dynamic linker, and kernel time are excluded —
which is how a do-almost-nothing program reads ~1 ms here (measured on this
machine: `say 1` is ~3–11 ms wall but ~1.1 ms user CPU).

| Example | interp | native (`--exe`) | native is |
|---|---:|---:|---:|
| life (`--delay=0`) | 746.2 ms | 171.6 ms | 4.3× |
| nqueens | 70.2 ms | 27.9 ms | 2.5× |
| mandel | 106.6 ms | 45.8 ms | 2.3× |
| roman | 3.5 ms | 1.5 ms | 2.3× |
| sierpinski | 4.5 ms | 2.2 ms | 2.0× |
| rpn | 1.9 ms | 1.3 ms | 1.5× |
| factorize | 2.0 ms | 1.4 ms | 1.4× |
| calculator | 1.8 ms | 1.3 ms | 1.4× |
| anagrams | 2.0 ms | 1.6 ms | 1.3× |
| fibonacci | 2.1 ms | 1.6 ms | 1.3× |
| matrix | 1.6 ms | 1.2 ms | 1.3× |
| quicksort | 1.4 ms | 1.1 ms | 1.3× |
| json | 1.6 ms | 1.3 ms | 1.2× |
| pascal | 1.6 ms | 1.3 ms | 1.2× |
| rationals | 1.5 ms | 1.3 ms | 1.2× |
| wordcount | 1.4 ms | 1.2 ms | 1.2× |
| primes | 2.8 ms | 2.5 ms | 1.1× |
| cipher | 1.9 ms | 1.8 ms | 1.1× |
| hanoi | 1.1 ms | 1.0 ms | 1.1× |
| quine | 1.0 ms | 0.9 ms | 1.1× |
| brainfuck | 7.5 ms | 8.2 ms | 0.9× |

## How to read this

- **The compute-heavy programs gain the most.** `life`, `nqueens`, and
  `mandel` spend their time in loops over arrays and arithmetic — exactly the
  interpreter overhead `--exe` removes.
- **The 1–2 ms rows are startup-dominated.** Most examples finish in a couple
  of milliseconds either way; at that scale the ratio mostly measures process
  startup, not the program. They are listed for completeness, not as evidence
  of speedup.
- **`brainfuck` is slightly slower natively.** Its hot loop is already served
  by the interpreter's indexing fast path, and the generated code goes through
  the generic runtime helpers instead. Known, small, and honest.
- **Grammar programs run the same engine in both modes.** `calculator` and
  `json` register their grammars with the embedded runtime, so parsing itself
  is identical — only the surrounding code compiles.

## Reproducing

```sh
for f in examples/*.raku; do
  n=$(basename $f .raku)
  build/rakupp --exe -o /tmp/$n $f
  time build/rakupp $f > /dev/null   # interp
  time /tmp/$n > /dev/null           # native
done
```

Output-identity check: run both modes and `cmp` the outputs (seed `life.raku`
with `srand` first, and skip the wall-clock-driven examples).
