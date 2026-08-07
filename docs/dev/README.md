# docs/dev/ — working notes

Not the manual. This is the engineering record: what we plan to do, what we
found broken, and what we measured. Some of it is historical and deliberately
not maintained — each such entry says so.

The user-facing documentation is one level up, in [../](../README.md).

## Process

- **[RELEASING.md](RELEASING.md)** — the release checklist: the Roast,
  local-suite, **performance** and compiler-agreement gates that must pass before
  a version is bumped, and why each one exists.

## plans/ — what we intend to build (and what we decided not to)

- **[plans/VERSIONS.md](plans/VERSIONS.md)** — the single per-version plan
  index: what each major release set out to do, where its plan lived, and
  what it shipped — v1.0.0 through the **v3.0.0** campaign (2026-08-07).

The three v3.0.0 pillar plans, smallest first:

- **[plans/CLI-PLAN.md](plans/CLI-PLAN.md)** — a real command-line surface:
  one option parser instead of the position-sensitive `argv[1]` cascade, the
  completed Perl one-liner family (`-i` in-place editing, `-a`/`-F`
  autosplit, `-0777`), flags borrowed from other compilers (`-M`, `-v`,
  `--target`), each with a decision recorded, and `--profile` — a
  routine-level instrumented profiler whose disabled hooks measured at zero
  cost.
- **[plans/PARALLEL-PLAN.md](plans/PARALLEL-PLAN.md)** — the campaign to make
  true multicore parallelism the default: the measured starting point
  (scaling curve, contention, the crash that defines the work), the phases
  (memory model → TSan CI → runtime hardening → container strategy →
  scheduler → flip), and the gates. Executes the Option-2 design chosen in
  PLAN-gil-removal.md.
- **[plans/LTM-PLAN.md](plans/LTM-PLAN.md)** — true Longest-Token Matching:
  what LTM and the declarative prefix are, the two oracle-verified
  divergences the current probe-and-rank approach has, and the design — a
  side-effect-free NFA per alternative as the ranking oracle for `|` and
  protoregex dispatch, with the probe path kept switchable during rollout.

Earlier plans:

- **[plans/100.md](plans/100.md)** — what stands between the current pass rate and
  100% of Roast, starting with the fact that 100% does not exist.
- **[plans/PLAN-gil-removal.md](plans/PLAN-gil-removal.md)** — the design doc
  behind PARALLEL-PLAN.md: the three options for removing the GIL, why
  Option 2 (harden the runtime, not every user structure) won, and the
  related code.
- **[plans/RAKUAST-PLAN.md](plans/RAKUAST-PLAN.md)** — how RakuAST would be added
  **without touching the hot path**: why it must be a view built on demand rather
  than our internal tree (measured: 2.2× the nodes, ~1.8× the visits in the fib
  inner loop), why `.DEPARSE` + the existing parser replaces a RakuAST→AST
  compiler, and the one case where that text bridge is lossy. Deferred, not built.
- **[plans/LIBFFI-PLAN.md](plans/LIBFFI-PLAN.md)** — moving NativeCall onto
  `libffi`: where NativeCall was, what libffi bought, and the measurements behind
  each decision (why `dlopen` rather than link or vendor, why one marshaller
  rather than a fast path). Implemented — the file records what landed, and the
  two things deliberately left open: by-value structs (§6) and statically
  linking libffi instead of loading it (§10), which is what would give the
  Windows binaries a full FFI.

## ecosystem/ — the v2.0 campaign

- **[ecosystem/ECOSYSTEM-TOP50.md](ecosystem/ECOSYSTEM-TOP50.md)** — the measured
  top-50 by reverse dependencies over the Raku Ecosystem Archive: the ranking, the
  version pins, and the observations that shaped the working set.
- **[ecosystem/V2-MODULES-PLAN.md](ecosystem/V2-MODULES-PLAN.md)** — the campaign
  plan: why running real zef modules is the layer that makes an implementation
  useful, and the phases to get there.
- **[ecosystem/MODULE-FINDINGS.md](ecosystem/MODULE-FINDINGS.md)** — the triage
  log: every gap found while running battery modules under `rakupp`, batch by
  batch, with what was fixed and what is deliberately left. Kept for us; never
  reported upstream to module authors.
- **[ecosystem/MODULE-WISHLIST.md](ecosystem/MODULE-WISHLIST.md)** — a survey of
  the modules worth *writing* so that Raku is comfortable for ordinary day-to-day
  programming, each annotated with what already exists on raku.land.

## findings/ — what is broken, and where we differ

Living logs. Each one is a corpus we ran both engines over, with the divergences
classified and (mostly) repro'd.

- **[findings/ROAST-GAPS.md](findings/ROAST-GAPS.md)** — classification of
  everything that still blocks a full Roast pass (from a systematic scan of all
  failing files), with a suggested attack order.
- **[findings/TRIAGE.md](findings/TRIAGE.md)** — behavioural quirks found *outside*
  the harness (while writing real programs), each with a minimal repro, the
  correct behaviour, and the workaround used.
- **[findings/BUGS.md](findings/BUGS.md)** — divergences found while writing the
  article series, by cross-checking every snippet under both engines.
- **[findings/BUGS-JS-SHOWCASE.md](findings/BUGS-JS-SHOWCASE.md)** — divergences
  found while building the JavaScript/TypeScript showcase.
- **[findings/SPEC-DIVERGENCES.md](findings/SPEC-DIVERGENCES.md)** — divergences
  found by cross-checking every runnable example on the Raku++ specification site.
- **[findings/COURSE-DIVERGENCES.md](findings/COURSE-DIVERGENCES.md)** —
  divergences over the examples of the Complete Course of the Raku Programming
  Language. Data in [`course-diff/`](findings/course-diff/).
- **[findings/PWC-DIVERGENCES.md](findings/PWC-DIVERGENCES.md)** — divergences over
  six years of Perl Weekly Challenge community solutions, in both `.p6` and
  `.raku` style. Data in [`pwc/`](findings/pwc/).
- **[findings/CORPUS-DIFF.md](findings/CORPUS-DIFF.md)** — the round-by-round
  differential against the raku-corpus of real programs. Data in
  [`corpus-diff/`](findings/corpus-diff/).
- **[findings/ROSETTACODE.md](findings/ROSETTACODE.md)** — Raku++ vs Rakudo on real
  [RosettaCode](https://rosettacode.org/wiki/Category:Raku) programs: the
  `tools/rc-compare.raku` harness, results, and the gaps it surfaces.
- **[findings/REVIEW-1.0.md](findings/REVIEW-1.0.md)** — the pre-1.0 independent
  review, and what it turned up.
- **[findings/REVIEW-2.0.md](findings/REVIEW-2.0.md)** — the pre-2.0 review of the
  whole hand-written source: nine parallel fresh-eyes passes, five gated fix
  batches, the gates before and after, and what was deliberately deferred.
- **[findings/CONFORMANCE.md](findings/CONFORMANCE.md)** — a dated docs-conformance
  audit log (feature-by-feature against docs.raku.org). Historical, not maintained
  as current reference.

## experiments/ — measured, recorded, sometimes reverted

- **[experiments/PERF-CAMPAIGN.md](experiments/PERF-CAMPAIGN.md)** — interpreter
  performance: where the profile says the time goes, the ranked candidates, and
  what each attempt measured.
- **[experiments/METHOD-DISPATCH-EXPERIMENT.md](experiments/METHOD-DISPATCH-EXPERIMENT.md)**
  — why the interpreter's `if (m == …)` dispatch chain was **not** replaced with a
  hash map or a switch: the measurements, and the direction that would actually pay.
- **[experiments/QUOTE-WORD-SHADOWING.md](experiments/QUOTE-WORD-SHADOWING.md)** —
  a declared `sub s` / `sub q` / `sub ms` versus the quoting syntax. Attempted,
  measured, reverted; the record exists so it does not have to be rediscovered.
- **[experiments/MANDEL.md](experiments/MANDEL.md)** — notes on the Mandelbrot
  example and its performance history.

## History

- **[JOURNEY.md](JOURNEY.md)** — a memoir of how Raku++ was built. Historical, not
  maintained as current reference.
