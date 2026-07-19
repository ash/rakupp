# docs/

All Raku++ documentation. The [top-level README](../README.md) is the entry
point; everything else lives here.

## Reference

- **[HIGHLIGHTS.md](HIGHLIGHTS.md)** — the key features, in bullets, on one page.
- **[OVERVIEW.md](OVERVIEW.md)** — a one-page tour: what Raku++ is, its goals, capabilities, and how it compares to Rakudo.
- **[GUIDE.md](GUIDE.md)** — the full overview: goals, status, the compile modes, running against Roast, architecture.
- **[FEATURES.md](FEATURES.md)** — inventory of supported language features, by theme.
- **[COOKBOOK.md](COOKBOOK.md)** — a cookbook of runnable one-liner snippets, each verified against `rakupp`.
- **[UNICODE.md](UNICODE.md)** — Unicode support: graphemes, normalization, UCA collation, character introspection.
- **[ASYNC.md](ASYNC.md)** — concurrency & async: promises, supplies, channels, threads, and the two execution modes.
- **[ARCHITECTURE.md](ARCHITECTURE.md)** — how it's built, and what happens to a program in each run mode.
- **[PARSING.md](PARSING.md)** — the front end: from source text to AST — the lexer, the Pratt parser, and how user-defined operators (and other in-program grammar tweaks) are handled in a single pass.
- **[RUNTIME.md](RUNTIME.md)** — the runtime model: how statically-typed C++ runs dynamic Raku — what a `Value` is, how variables and containers relate, calls and dispatch, and lazy/infinite sequences.
- **[MEMORY.md](MEMORY.md)** — memory demands and limits: reserved vs. resident, stack sizes and measured recursion depths per mode (interpreter / `--exe` / wasm), and the data-side guardrails.
- **[METAPROGRAMMING.md](METAPROGRAMMING.md)** — language-mutation coverage: custom operators, precedence traits, phasers, MOP, macros/slangs.
- **[DOGFOODING.md](DOGFOODING.md)** — the Raku tools Raku++ uses to build, test, and measure itself.

## Native compile & the browser

- **[NATIVE.md](NATIVE.md)** — the `--exe` native compiler: interpreter vs. compiled on the example programs (byte-identical output).
- **[COMPILERS.md](COMPILERS.md)** — which compiler and architecture to use: arm64 vs. x86_64 on macOS, GCC vs. Clang, MSVC vs. MinGW on Windows — both for building Raku++ and for the compiler `--exe` invokes.
- **[OPTIMIZATION.md](OPTIMIZATION.md)** — the `--exe -O` optimizer: the codegen passes and how fast they get.
- **[../rakujs/README.md](../rakujs/README.md)** — **Raku.js**: the same runtime compiled to **WebAssembly** to run Raku in the browser (build, deploy, performance).
- **[../rakujs/TUTORIAL.md](../rakujs/TUTORIAL.md)** — writing real browser Raku programs on the WebAssembly build (feeding input, reading output, workers).

## Measurements

- **[ROAST.md](ROAST.md)** — Roast suite overview and per-section statistics.
- **[COUNTING.md](COUNTING.md)** — how the pass-rate numbers are defined and computed (the authoritative methodology).
- **[BENCHMARKS.md](BENCHMARKS.md)** — a fair speed comparison with Rakudo on the shared subset.
- **[ROADMAP.md](ROADMAP.md)** — done / in-progress / next.

## dev/ — working notes & history

- **[dev/ROAST-GAPS.md](dev/ROAST-GAPS.md)** — classification of everything that still
  blocks a full Roast pass (from a systematic scan of all failing files), with
  a suggested attack order.
- **[dev/TRIAGE.md](dev/TRIAGE.md)** — behavioural quirks found *outside* the harness
  (while writing real programs), each with a minimal repro, the correct
  behaviour, and the workaround used.
- **[dev/ROSETTACODE.md](dev/ROSETTACODE.md)** — Raku++ vs Rakudo on real
  [RosettaCode](https://rosettacode.org/wiki/Category:Raku) programs: the
  `tools/rc-compare.raku` harness, results, and the gaps it surfaces.
- **[dev/DISPATCH.md](dev/DISPATCH.md)** — call dispatch in `--exe` code: what each
  call shape costs (measured), the cached-builtin/inline-string-compare cuts, and
  what's deliberately left on the table.
- **[dev/PLAN-gil-removal.md](dev/PLAN-gil-removal.md)** — the design plan for removing the
  GIL and reaching true CPU parallelism (incremental steps, risks, status).
- **[dev/100.md](dev/100.md)** — what stands between the current pass rate and 100% of Roast.
- **[dev/JOURNEY.md](dev/JOURNEY.md)** — a memoir of how Raku++ was built. Historical,
  not maintained as current reference.
- **[dev/CONFORMANCE.md](dev/CONFORMANCE.md)** — a dated docs-conformance audit log
  (feature-by-feature against docs.raku.org). Historical, not maintained as
  current reference.
- **[dev/MANDEL.md](dev/MANDEL.md)** — notes on the Mandelbrot example and its performance history.
