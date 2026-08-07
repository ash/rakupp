# Cutting a release

The gates below are the whole checklist. They exist because each one has caught
something real at least once; the notes say what.

## The gates

Run these in order. Every one must pass **before** the version is bumped.

### 1. Roast — no regression

```bash
ROAST=/path/to/roast rakupp tools/run-roast.raku --workers=4 | tee roast.txt
```

Compare against the previous release's figure in [CHANGELOG.md](../../CHANGELOG.md).

Keep that `roast.txt` — the per-file `[PASS] 3/3 …` lines are what step 5 below
feeds to `gen-roast-map.raku`, and re-running the suite just to get them back
costs an hour.

Read the **denominators**, not just the pass count. A file that dies removes its
tests from *both* sides of "tests that ran", so a real regression can leave the
percentage untouched — a falling denominator means a file stopped emitting TAP.
See [COUNTING.md](../status/COUNTING.md), which works through exactly that case.

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

The gate has three outcomes, not two:

| exit | meaning |
|---|---|
| 0 | no kernel is more than `tolerance-pct` slower |
| 1 | a regression, confirmed by re-measuring the failing kernels |
| **2** | **inconclusive** — the machine is loaded, so the timings say nothing |

That third state exists because every false alarm this gate has raised was a
background process, not the build: Spotlight indexing, `ecosystemanalyticsd`,
WindowServer, and once four `pandoc` jobs at 50% each. A failing kernel is
re-measured before the gate believes it, and if the load average is above 60% of
the core count it reports **inconclusive and exits 2** rather than accusing the
code. Treat exit 2 as "run it again on an idle machine", never as a pass.

If the gate genuinely fails, the options are to fix it, or — when the cost is
understood and deliberate — to re-record the baseline and **say why in the
CHANGELOG**:

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

This leaves `src/data/typerun.raku` and `src/data/matrix.raku` rewritten but
**unpublished** — step 5 in the next section is what turns them into pages.

The count has a **±5 flap band**: the `Set`/`Bag`/`Mix`/`Map` examples move in
both directions between runs of identical code, because Rakudo randomizes hash
iteration order per process. Do not read a ±5 move as progress.

## Then

1. Bump `project(RakuPP VERSION …)` in [CMakeLists.txt](../../CMakeLists.txt),
   the matching `(version "X.Y.Z-git")` in
   [.guix/modules/rakupp-package.scm](../../.guix/modules/rakupp-package.scm)
   (the Guix package, PR #6), and `version = "X.Y.Z"` in
   [flake.nix](../../flake.nix) (the Nix package, issue #5).
2. Write the CHANGELOG entry — measured numbers, not projected, with the
   methodology link. Note anything deliberately left open.
3. Refresh the figures in README, `docs/status/ROAST.md`, `docs/status/COUNTING.md`,
   `docs/guide/FEATURES.md`, `docs/guide/GUIDE.md`, `docs/guide/HIGHLIGHTS.md`, `docs/guide/OVERVIEW.md`
   and `docs/status/ROADMAP.md` — all from **one** run, so they agree with each other.
   `docs/status/BENCHMARKS.md` too if the benchmarks were re-run.
4. Tag, and publish.
5. **Republish the site data** — the graphs and listings under
   <https://raku.online/spec/> and <https://raku.online/spec/rules/>.

Step 5 is the one that gets forgotten, because the release itself is already
out by then and everything looks finished. It isn't: gate 5 rewrote the
conformance data in the raku.online checkout and left it sitting there, so until
this runs, the site shows the *previous* release's divergences, coverage meters
and Roast map while announcing the new version.

**After the tag, not before.** `gen-dashboard.raku` mines the rakupp repo's `v*`
tags and reads `docs/status/ROAST.md` / `docs/status/BENCHMARKS.md` *as committed at each one*.
Run it before tagging and the new release is simply absent from the timeline —
and since it only collects, never measures, nothing warns you.

```bash
# in the raku.online checkout, sites/spec
rakupp tools/gen-roast-map.raku /path/to/roast.txt $(date +%F)  # gate 1's output
rakupp tools/snapshot.raku --rakupp=/path/to/rakupp --oracle=raku   # BEFORE the dashboard
rakupp tools/gen-dashboard.raku --rakupp-repo=/path/to/raku++ --battery=/path/to/raku-module-battery
../../../raku++/rakujs/build.sh    # the playground engine — see step 6 below
./verify.sh                                    # both sites, every example, publishes nothing
cd ../.. && ./build.sh spec                    # sites/spec -> www/spec
```

Then commit `www/` *together with* `sites/spec/src/data/`, and push. The Pages
workflow publishes `www/` **verbatim** — there is no build step in CI, so
anything not built and committed locally does not go live, and the regenerated
data files alone change nothing that a visitor sees.

`snapshot.raku` sits between two ordering hazards, one on each side — the
v2.0.0 release hit the second one and shipped a dashboard whose conformance
trend ended at the previous release:

- **After gate 5's sweep, never before.** It appends one line to
  `src/data/history.jsonl` describing whatever the other data files say *at
  that moment*, and that file is what the trend chart on
  [/spec/rules/divergences/](https://raku.online/spec/rules/divergences/)
  draws. Snapshot before regenerating and the release's line permanently
  records the previous release's numbers: the file is append-only and nothing
  ever rewrites an earlier line.
- **Before `gen-dashboard`, never after.** The dashboard's conformance series
  is mined *from* `history.jsonl`, so a dashboard generated before the
  snapshot lacks the release's own point. If the order slips, re-running
  `gen-dashboard` is safe (it only collects); re-running `snapshot` is NOT
  (it would append a duplicate line).

Two more collectors go stale silently — neither fails, they just stop:

- **The dashboard's modules series** is mined from the *battery repo's commit
  subjects* (`Tier-2: N/50` for the old probe bar, `battery: N/59` for the
  zef bar). Commit the battery repo with a subject in that shape before
  running `gen-dashboard`, or the graph flatlines at the last such commit
  while the README says something better.
- **The playground engine** (`/play/` and every embedded run button) is the
  Raku.js WASM bundle, built separately by `rakujs/build.sh` from the tagged
  source and copied into the site's `www/` (`rakujs.js` + `rakujs.wasm`).
  Nothing regenerates it: skip this and the playground banner keeps
  announcing the previous release — v2.0.0 shipped while `/play/` still said
  1.7.0. Rebuild it, copy the pair over, and let `build.sh` re-stamp the
  cache-busting hashes.

If a sweep was skipped, skip its regeneration too rather than re-running an old
tool against new data — the point of the history is that each line is one
coherent run. `inventory.raku` and `typedoc.raku` need a Rakudo doc checkout and
only change when the *documentation* does, not when Raku++ does; they are not
per-release work.
