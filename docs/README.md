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
- **[METAPROGRAMMING.md](METAPROGRAMMING.md)** — language-mutation coverage: custom operators, precedence traits, phasers, MOP, macros/slangs.
- **[DOGFOODING.md](DOGFOODING.md)** — the Raku tools Raku++ uses to build, test, and measure itself.

## Measurements

- **[ROAST.md](ROAST.md)** — Roast suite overview and per-section statistics.
- **[COUNTING.md](COUNTING.md)** — how the pass-rate numbers are defined and computed (the authoritative methodology).
- **[BENCHMARKS.md](BENCHMARKS.md)** — a fair speed comparison with Rakudo on the shared subset.
- **[NATIVE.md](NATIVE.md)** — interpreter vs compiled (`--exe`) on the example programs.
- **[OPTIMIZATION.md](OPTIMIZATION.md)** — the `--exe -O` optimizer: what it does and how fast it gets.
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
- **[dev/PLAN-gil-removal.md](dev/PLAN-gil-removal.md)** — the design plan for removing the
  GIL and reaching true CPU parallelism (incremental steps, risks, status).
- **[dev/100.md](dev/100.md)** — what stands between the current pass rate and 100% of Roast.
- **[dev/JOURNEY.md](dev/JOURNEY.md)** — a memoir of how Raku++ was built. Historical,
  not maintained as current reference.
- **[dev/CONFORMANCE.md](dev/CONFORMANCE.md)** — a dated docs-conformance audit log
  (feature-by-feature against docs.raku.org). Historical, not maintained as
  current reference.
- **[dev/MANDEL.md](dev/MANDEL.md)** — notes on the Mandelbrot example and its performance history.
