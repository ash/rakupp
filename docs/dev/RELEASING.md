# Cutting a release

The gates below are the whole checklist. They exist because each one has caught
something real at least once; the notes say what.

## The gates

Run these in order. Every one must pass **before** the version is bumped.

### 1. Roast — no regression

```bash
ROAST=/path/to/roast rakupp tools/run-roast.raku --workers=4
```

Compare against the previous release's figure in [CHANGELOG.md](../../CHANGELOG.md).

Read the **denominators**, not just the pass count. A file that dies removes its
tests from *both* sides of "tests that ran", so a real regression can leave the
percentage untouched — a falling denominator means a file stopped emitting TAP.
See [COUNTING.md](../COUNTING.md), which works through exactly that case.

The suite flaps by a file — 624↔625 fully passing and 10↔11 timeouts on the same
build — worth about 20 assertions either way. Take three runs and use the
repeating profile, not the best one seen.

### 2. The local suite

```bash
rakupp t/run.raku
```

All checks, including every `t/regression/` case. A regression file passes iff it
exits 0 with `PASS` as its last line — it is not TAP.

### 3. Performance — no regression

```bash
rakupp tools/perf-guard.raku --check      # exits non-zero on a regression
```

**A release must not ship a performance regression.** This gate compares the
build against [`tools/perf-baseline.raku`](../../tools/perf-baseline.raku), which
holds the last release's measured times, and fails if any kernel is more than
`tolerance-pct` (5%) slower.

Two numbers are tracked per kernel:

- `baseline` — the last release. This is what the gate enforces.
- `best` — the fastest ever measured, and when. Not enforced, but reported, so
  that a regression which was once accepted does not quietly become the new
  normal.

If the gate fails, the options are to fix it, or — when the cost is understood
and deliberate — to re-record the baseline and **say why in the CHANGELOG**:

```bash
rakupp tools/perf-guard.raku --record
```

Re-recording without an explanation is how the debt gets lost. It has happened
twice: an 8–22% interpreter regression slipped through the v0.7.1→v0.9.0 cycle
(and is why `loopsum`/`hash` were added to the guard at all), and an ~11.6% `fib`
regression accreted across v1.2.x and was only found by re-measuring for a
release five days later. Both were gradual, spread across the dispatch path, with
no single hotspot — which is exactly the shape a per-change eyeball misses and a
recorded baseline catches.

Absolute times are machine-specific. The gate compares two builds on the *same*
machine; on a new one, re-record before trusting a failure.

### 4. The compiler agrees with the interpreter

```bash
rakupp tools/run-optbench.raku            # exits non-zero if any mode disagrees
```

Checks that the interpreter, `--exe`, `--exe -O` and Rakudo produce *identical*
output on every kernel before reporting a timing — so it catches an optimisation
that changed an answer, not just one that got slower.

The code generator is a second implementation of the language, so a bug can live
there and nowhere else: [#8](https://github.com/ash/rakupp/issues/8) and the
declared-type loss found while fixing
[#9](https://github.com/ash/rakupp/issues/9) were both compiler-only.

### 5. Conformance — only before a release

```bash
# in the raku.online checkout, sites/spec
rakupp tools/typerun.raku --rakupp=/path/to/rakupp --oracle=raku   # types
rakupp tools/matrix.raku  --rakupp=/path/to/rakupp --oracle=raku   # operators
rakupp tools/conformance.raku && rakupp tools/divergences.raku     # reports
```

Both halves feed <https://raku.online/spec/rules/divergences/>. This is slow
(~25 min for the types sweep), which is why it runs per release rather than per
batch.

The count has a **±5 flap band**: the `Set`/`Bag`/`Mix`/`Map` examples move in
both directions between runs of identical code, because Rakudo randomizes hash
iteration order per process. Do not read a ±5 move as progress.

## Then

1. Bump `project(RakuPP VERSION …)` in [CMakeLists.txt](../../CMakeLists.txt).
2. Write the CHANGELOG entry — measured numbers, not projected, with the
   methodology link. Note anything deliberately left open.
3. Refresh the figures in README, `docs/ROAST.md`, `docs/COUNTING.md`,
   `docs/FEATURES.md`, `docs/GUIDE.md`, `docs/HIGHLIGHTS.md`, `docs/OVERVIEW.md`
   and `docs/ROADMAP.md` — all from **one** run, so they agree with each other.
   `docs/BENCHMARKS.md` too if the benchmarks were re-run.
4. Tag, and publish.
