# Versions — what each major release set out to do

One section per major version: the goal it was *named* for, where the plan
lived, and (for shipped versions) what actually landed. This is the
forward-looking companion to [MILESTONES.md](../../status/MILESTONES.md) —
that file records what happened and when; this one records what each version
was *for*, so the next campaign is planned the same way the last ones were.

The pattern every shipped major has followed: **one number a stranger can
re-measure**, a written plan before the code, and the standing gates on every
batch (zero Roast regressions, the local suite, `perf-guard --check`,
methodology in [COUNTING.md](../../status/COUNTING.md)).

## v1.0.0 — "it's Raku" (shipped 2026-07-22)

- **The number:** 90% of declared Roast assertions, no architecture changes,
  no performance regressions. Shipped at **194,496 / 216,066 (90.0%)**,
  ~39% of files fully passing.
- **The plan:** the campaign ran off
  [findings/ROAST-GAPS.md](../findings/ROAST-GAPS.md) (the systematic gap
  classification) with [100.md](100.md) as the ceiling analysis — 100% does
  not exist; ~97% is the real ceiling, and the walls past ~92% are projects,
  not tasks. An independent pre-1.0 review
  ([findings/REVIEW-1.0.md](../findings/REVIEW-1.0.md)) and five fix waves
  closed the cycle.

## v1.1.0 — 100% Unicode (shipped 2026-07-24)

- **The number:** all of S15 — **91,752 / 91,752 assertions**. Full UCD case
  tables, NFG-aware regex, complete `uniprop`. A deliberate single-synopsis
  campaign between the 1.0 and ecosystem pushes.

## v1.5.x — conformance measured, properties become gates (shipped 2026-07-29/31)

- **The number (v1.5.0):** the measured divergence gap with Rakudo, halved —
  **202 → 152** across the documentation sweep and the operator behaviour
  matrix. The release's other half was structural: properties we care about
  became **gates that fail a release** (performance among them —
  [RELEASING.md](../RELEASING.md)) instead of numbers someone remembers to
  check.
- **v1.5.1:** speed with zero behaviour change (fib −17.6%), Roast
  byte-identical before and after — the release that proved the perf gate.
- **v1.5.2:** changed a standard rather than a number: a module counts as
  working only when **its own test suite passes** — the bar v2.0.0 was then
  measured on.

## v2.0.0 — other people's code (shipped 2026-08-07)

- **The number:** **50 / 59** top-50 distributions passing their own `zef`
  install-time suites (from 11 when the bar was set), counted honestly —
  the Pair-form subtest fix re-measured the whole suite on the way.
- **The plan:** [ecosystem/V2-MODULES-PLAN.md](../ecosystem/V2-MODULES-PLAN.md),
  with the batch-by-batch record in
  [ecosystem/MODULE-FINDINGS.md](../ecosystem/MODULE-FINDINGS.md) and the
  pre-2.0 source review in [findings/REVIEW-2.0.md](../findings/REVIEW-2.0.md).
  Almost none of the work was module-specific: the modules were the *finder*
  for general interpreter bugs.

## v3.0.0 — the compiler grows up (planned, 2026-08-07)

Not framed as Rakudo parity — Rakudo stays the oracle every divergence is
judged against, but the headline items are capabilities, each with its plan
written before any code:

1. **A real command line, with a first profiler**
   ([CLI-PLAN.md](CLI-PLAN.md)) — **DONE 2026-08-07**, the same day the
   campaign was planned: one option parser replacing the position-sensitive
   mode cascade (goldens written against the old binary first); the
   completed Perl one-liner family with `-i` in-place editing, `-a`/`-F`
   autosplit and `-0777` (four live perl differentials in the suite); flags
   borrowed from other compilers (`-M`, `-v`, `--target`); `--profile` —
   routine-level instrumented profiling whose disabled hooks measured at
   zero cost; MAIN usage byte-identical to Rakudo (issue #17); and
   [guide/CLI.md](../../guide/CLI.md). Building `-i` found and fixed a
   general interpreter bug (`my $*OUT = $handle` did not reroute
   say/print/put).
2. **Real multicore parallelism, on by default**
   ([PARALLEL-PLAN.md](PARALLEL-PLAN.md)) — execute the GIL-removal design
   ([PLAN-gil-removal.md](PLAN-gil-removal.md), Option 2): a written memory
   model, a TSan-gated stress suite, runtime hardening so a user data race
   can never crash the interpreter (today it SIGABRTs), a worker pool, then
   flip `RAKUPP_PARALLEL` from opt-in to default with `RAKUPP_GIL=1` as the
   escape hatch.
3. **True Longest-Token Matching** ([LTM-PLAN.md](LTM-PLAN.md)) — replace
   the regex engine's probe-and-rank approximation with a side-effect-free
   declarative-prefix NFA ranking `|` alternations and protoregex dispatch,
   fixing the oracle-verified divergences (ranking by greedy end instead of
   declarative-prefix end; user code running during candidate selection)
   without giving back the engine's measured speed advantage.

**The numbers a stranger can re-measure** at the tag: the unguarded-race
family runs without a native crash and an embarrassingly-parallel benchmark
scales ≥3× on 8 cores at unchanged single-thread speed; the LTM divergences
are gone from the oracle sweep with `longest-alternative.t` at the
fudged-Rakudo score; and the one-liner cookbook runs byte-identical to Perl's
`-i`/`-a`/`-F` behaviour. As with 1.x → 2.0.0, finished work lands in v2.x
minor releases along the way; **v3.0.0 tags when all three pillars hold
their gates at once**.

---

*Keeping this current: when a campaign is decided, add its section here
before the code starts — goal, number, plan links. When it ships, fold in
the outcome and add the row to MILESTONES.md.*
