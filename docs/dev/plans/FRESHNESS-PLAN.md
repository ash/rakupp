# Freshness — the weekly sweep of what the ecosystem just published

Both of our real-world corpora are snapshots of a date. The module battery is
pinned at 2026-08-12 (top 200 by reverse-dependency rank); the Weekly Challenge
ledger stops at challenge 387 (2026-08-20). The ecosystem does not stop. Every
week brings a new challenge — twenty-odd authors solving the same two problems
in twenty-odd idioms — and a handful of new or bumped distributions.

That new material is the cheapest bug source we have. Round two of the Weekly
Challenge sweep found **62 mismatches in 246 counted files (25%)** on code
written after the engine was already passing 196k Roast assertions; seven fix
batches took it to 36. A corpus that renews itself weekly keeps producing that
signal without anyone hunting for it.

This plan sets up a **recurring session**: one sitting a week that pulls what
appeared since the last one, measures both engines against it, and spends the
rest of the session turning the largest cluster into one gated fix batch.

The session is started by hand — there is no cron. What the plan buys is that
starting it costs one sentence and the first hour is mechanical.

---

## The two corpora

### A. Weekly Challenge — a rolling window

A challenge is published each Monday and closes the following Sunday, so on any
Monday the previous challenge's directory is essentially final. The weekly
window is therefore small: **~20 new files**, of which the sweep's rules
(Rakudo must run it headlessly and reproduce itself across two runs) typically
count about half.

Twenty files a week is too thin to fill a session on its own, so the weekly PWC
leg has three parts, in this order:

1. **The new challenge** — the one that closed yesterday, plus late additions
   to the two before it (authors backfill).
2. **The open set** — the 36 mismatches still standing from challenges
   371–387. Re-run every week: this is what confirms last week's batch and
   catches a regression the Roast gate cannot see.
3. **The pass set, sampled** — a rotating slice of the files that matched last
   week. Incremental sweeps only re-test mismatches, so a file that silently
   stops matching is invisible until a from-scratch re-run. Round one lost ~50
   ledger passes to exactly this. A weekly 10% slice makes the whole set
   re-verified every ten weeks without ever paying for a full re-run.

The round-one backlog (2,707 mismatches over challenges 1–370, clustered in
[findings/PWC-DIVERGENCES.md](../findings/PWC-DIVERGENCES.md)) stays where it
is: the deep well to draw on when a week is thin, not part of the weekly
measurement.

**Refresh mechanics.** `~/perlweeklychallenge-club` is a clone of *our fork*
(`ash/perlweeklychallenge-club`); it has no `upstream` remote, so it does not
currently see new challenges at all. One-time setup:

```
git -C ~/perlweeklychallenge-club remote add upstream \
    https://github.com/manwar/perlweeklychallenge-club.git
```

and then `git fetch upstream && git merge --ff-only upstream/master` each week.
Fetching is read-only; nothing is ever pushed to the fork.

### B. Ecosystem releases — the delta since the last sweep

REA's `META.json` (https://raw.githubusercontent.com/raku/REA/main/META.json,
one dist-version object per line, each carrying `release-date`) is the whole
mechanism. Keep last week's copy; this week's new material is every entry whose
`release-date` is later than the last sweep. That is both **new dists** and
**new versions of pinned ones** — the second kind matters more than it sounds,
because a bumped version of a top-200 dist is code we already claim to support.

Expect roughly 10–40 releases a week, most of them tiny.

Per dist, three legs, cheapest first:

1. **Parse** — does `rakupp -c` read every file in the dist? A module that will
   not compile blocks every test it has, and parse gaps are the cheapest real
   estate (batches 7–9 of the 200-green campaign).
2. **Suite under rakupp** — `rakupp test NAME` against the vendored source.
3. **Rakudo control**, but only for what fails leg 2. Standing policy: **a dist
   that fails identically under Rakudo is upstream-broken and out of scope** —
   recorded, never reported upstream, never worked on. The control has been
   wrong before (URI was recorded as upstream-broken and was ours), so the
   control leg runs fresh each week rather than being looked up.

**Vendoring rule.** Freshness dists land in their own directory
(`dists/fresh/`), never in the pinned `dists/` tree — the pins are what the
200-green campaign measures against and nothing may move under it. Same safety
model as the battery: checksummed REA tarballs only, symlink-free extraction,
path-checked, and every execution inside `sandbox-exec` with the
`harness/battery.sb` profile (network denied, writes confined). This is freshly
published third-party code being run for the first time; it gets the same
treatment as the pinned set, not less.

**Promotion.** A freshness dist that goes green stays a freshness row; it does
not join the top-200 board. The pin refresh is a separate, deliberate act (a
re-rank + re-vendor), not a weekly side effect.

---

## The session

Four phases, roughly two hours, in this order. The point of fixing the order is
that measurement finishes before any temptation to fix begins.

**Phase 0 — refresh (~10 min, mechanical).**
`git fetch` the challenge clone; download today's REA index beside last week's.
No analysis.

**Phase 1 — sweep (~30 min, mostly waiting).**
PWC: new challenge + open set + the 10% pass slice. Ecosystem: parse leg over
the delta, then suites for what parses. Both write JSONL/TSV; both print one
line of tally. Nothing is diagnosed yet.

Two numbers come out of this phase and go straight into the ledger, whatever
they say:

- PWC: byte-identical / counted, for the rolling window.
- Ecosystem: parse-clean / suite-green / upstream-broken, for the delta.

**Phase 2 — triage (~20 min).**
Cluster the mismatches by first divergence, not by file. Rank clusters by files
affected. Pick **one** batch — the largest cluster that looks like one cause.
Anything not picked is written down in the ledger's open list; it does not
evaporate because we did not get to it.

A regression (a file that matched last week and does not now) outranks
everything else and is fixed the same session, however small.

**Phase 3 — one batch, fully gated (~50 min).**
Fix, then the standing gates, all of them, in this order:

- `rakupp t/run.raku` green (the local example/showcase suite);
- the full Roast run with **no per-file regression** — files that appear to
  move are re-run alone, because the parallel harness times a handful out under
  load;
- the install gate;
- `tools/perf-guard.raku --check` — and remember its blind spot: it only calls
  INCONCLUSIVE above 60% of core count, so on a loaded desktop a uniform
  slowdown across unrelated kernels is load, not code. A/B two binaries under
  the same load rather than trusting a recorded baseline.

Rebuild **build-arm64** — that is the binary on `PATH`, and gating in `build/`
(x86_64 under Rosetta) while the user runs `build-arm64` has already produced
one "it still fails" on a stale binary.

If the batch does not fit the hour, it is not the right batch. Shrink it; a
half-gated batch is worse than none.

**Phase 4 — ledger (~10 min).**
Append one row per corpus to the history TSV, update the two findings docs, and
leave the changes in the working tree. Commits are batched by hand, as always;
the session does not prompt for one.

---

## What has to be built first

Four small pieces. All Raku, run by `rakupp` — the tools are also tests.

1. **`tools/pwc-sweep.raku` gains state.** Today it sweeps a challenge range and
   writes mismatches. It needs `--state=FILE` (last week's per-file verdicts),
   so it can (a) re-run only the open set plus the new window plus a
   `--sample=10%` slice of passes, and (b) print **regressions** — pass→mismatch
   — as their own line rather than folding them into the tally. Same file also
   becomes the input for the next week.

2. **`tools/eco-fresh.raku`** — new. Takes two REA index snapshots, emits the
   delta, vendors it into `dists/fresh/` under the existing safety rules
   (reuse `vendor-dists.raku`'s fetch/checksum/extract path rather than writing
   a second one), then runs the three legs and writes
   `scans/FRESH-YYYY-MM-DD.tsv`. Sandbox profile on every execution leg.

3. **`docs/dev/freshness-history.tsv`** — one appended row per sweep, per
   corpus, in the shape of `rakugrid-history.tsv`: date, corpus, window,
   counted, matched, mismatched, regressions, Roast at the time, batch commit.
   One file, machine-readable, so the series can be plotted later without
   re-reading prose.

4. **`.claude/skills/weekly-sweep/SKILL.md`** — the session itself: the four
   phases above as instructions, with the exact commands, the gate list, the
   scope rules (Rakudo control, headless/reproducible), and the honesty rules
   (report the number that came out; a module that compiles is not a module
   that passes). Starting the session becomes `/weekly-sweep`.

Order: (1) and (3) first — the PWC leg can run the very next session with no
new tooling beyond state. (2) is the larger piece and can land a week later,
with the first ecosystem delta swept by hand meanwhile. (4) last, written from
what the first two sessions actually did rather than from this document.

---

## Rules carried over

- **Measure, then fix.** Phase 1 finishes before Phase 2 starts. The number
  reported is the number that came out, including when a batch moved nothing.
- **One batch per session**, fully gated. No partial gates.
- **Upstream-broken is out of scope** — for modules, verified against a fresh
  Rakudo control run; for challenge solutions, a file Rakudo cannot run
  headlessly or reproduce is skipped, not counted as agreement.
- **Comparing the two engines is not the same as being right.** When a fix
  turns on a semantic question, the finished regression case runs under Rakudo
  before it counts.
- **A module that compiles is not a module that passes.** Parse fixes move
  dists from dep-fail to self-fail; say that, do not call it a pass.
- **Error-message prose is not a divergence** unless Roast or the docs assert
  it. Fix behaviour.
- **Untrusted code.** Everything in the ecosystem leg runs sandboxed with
  network denied. The challenge leg runs with stdin closed, in its own process
  group, with a timeout — macOS produces unkillable children when bulk-running
  unknown programs, and the harness must abandon what it cannot reap.

## What a thin week looks like

Some weeks the new challenge yields two mismatches and the ecosystem delta is
six documentation releases. That is a *result*, not a failed session. The
fallback, in order: a regression if one appeared; then the largest open cluster
from the rolling window; then one stratum of the round-one backlog (the author
strata — six authors account for 51% of the 2,707, each reusing one personal
template, so one shape fixed corpus-wide moves hundreds of files).

## Decisions still open

- **Day.** Monday gives the freshest closed challenge; Tuesday gives the
  backfillers a day. Proposed: Monday, with the previous two challenges re-swept
  to catch late arrivals.
- **Whether the ecosystem leg should also track *removed*/yanked dists.** Cheap
  to detect from the index delta, unclear that it is worth a row.
- **Pin refresh cadence.** The top-200 pins are three months stale by
  November at this rate. Proposed: re-rank and re-vendor quarterly, as a
  separate session, not folded into a weekly one.
