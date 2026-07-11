# How Raku++ counts Roast results

This is the authoritative definition of the numbers quoted everywhere else
(README, OVERVIEW, GUIDE, FEATURES, ROADMAP, ROAST). If a figure disagrees with
this file, this file wins. The self-hosted harness
([`tools/run-roast.raku`](tools/run-roast.raku)) computes all of it and prints it
on every run.

## The one-line summary

> **Per-test: ~82% of all declared tests pass. Coverage: ~29% of files fully pass.**

Quote both, per-test first. The ~82% is the primary correctness number (the fair
per-test bar); the ~29% is the stricter all-or-nothing file bar.

## The measures

Each `.t` file emits [TAP](https://testanything.org/): a `1..N` plan and `ok`/`not
ok` lines. The harness runs every file with a 10-second timeout and reports four
ratios, from strictest presentation to fairest, and from widest denominator to
narrowest:

| # | Measure | Current | Definition |
|---|---|---|---|
| 1 | **Files fully passing** | 419 / 1,464 (**~29%**) | a file counts only if *every* planned assertion passes (or it legitimately `plan skip-all`s) |
| 2 | Assertions of **tests that ran** | 157,293 / 163,209 (~96%) | numerator ÷ assertions the files actually emitted |
| 3 | Assertions of **tests planned** | 157,293 / 176,967 (~89%) | ÷ the plan `N` of every file that emitted a plan (so tests lost to a mid-file abort count against us) |
| 4 | Assertions of **all declared tests** | 157,293 / 191,537 (**~82%**) | ÷ every test any file declares — including files that abort before emitting TAP, whose `plan N` is read from source |

**Measure 1 (files, ~29%)** and **measure 4 (all declared tests, ~82%)** are the
two headline numbers. 2 and 3 are diagnostic context, not headlines.

## Why measure 4 is the honest per-test number

The trap is a file that **parse-errors on compile**: it aborts before printing
its `1..N` line, so it emits *nothing*. Under measures 2 and 3 that file
contributes 0 to both numerator and denominator — its tests simply vanish, which
silently flatters the rate. Measure 4 closes that hole: for any file that emitted
no plan at runtime, the harness reads the intended `plan N` straight from the
source and counts all N as failing. That is why 4's denominator (191,537) is ~15k larger
than 3's (176,967) — those 14,570 tests live in 261 no-TAP files (parse errors
and runtime aborts), recovered from source. A parse error can no longer hide
its tests.

## The declared denominator grows with coverage — ~82% here is really ~76% of the whole suite

Measure 4's denominator is **not a fixed property of the suite**; it depends on
how much of the suite a run can actually execute, and it's worth remembering why
so this doesn't spawn the same question twice.

For the **1,400 files with a literal `plan 42;`**, the count is recoverable from
source whether or not the file runs — so those contribute the same to *any* run.
But **over a hundred files declare their plan dynamically** — `plan +@tests`,
`plan $n * 6`, or `done-testing` (45 files) with no up-front number. For those,
the test count is knowable *only by running the file*. When a run aborts such a
file (no-TAP), there is no static integer to read from source, so the file
contributes **0** — its tests are genuinely uncountable for that run.

The consequence: **a run that executes more of the suite gets a larger
denominator.** Our current run recovers **191,537** declared tests. A run that
executes essentially every file surfaces **~206,000** — the extra ~14,500 live
in dynamic-plan files we abort on and therefore cannot count.

So our same 157,293 passes read two ways:

- **~82%** against *our* denominator (157,293 / 191,537) — *"of the tests we can
  account for, how many pass."* This is what a single harness run can measure,
  and it is the number we quote.
- **~76%** against the suite's *full* declared total (157,293 / ~206,000) —
  *"of every test the whole suite could declare, how many pass."*

Both are honest; they answer different questions. Keep the ~76% in mind, because
it means our headline **~82% is, if anything, slightly flattering** — tests in
files we can't even run aren't charged against us. It also means **raw per-test
percentages from two different runs aren't directly comparable** until they're
put over a common denominator: a run that unlocks more files *raises* its own
denominator, so a rising numerator can hide behind a rising denominator (or vice
versa). The zero-regression gate (below) is on the *file list*, precisely to
sidestep this.

## Exactly how the denominators are built

Per file, `parse-tap` yields `(planned, ran, passed)` where `planned` is the
runtime `1..N` (`-1` if none was emitted). The harness accumulates:

- `tot-pass`  += `passed`                               — the numerator, shared by all ratios
- `tot-ran`   += `ran`                                  — denominator of measure 2
- `tot-plan`  += `planned >= 0 ? planned : ran`         — denominator of measure 3
- `declared`  = `tot-plan` + (static `plan N` read from the source of each file
  that emitted **no** plan at runtime)                  — denominator of measure 4

The numerator is the **same** in every ratio — only the denominator widens.

### Edge cases

- **skip-all / dynamic-plan files** (`plan skip-all`, `plan *`, `done-testing`
  with no count) have no static test count, so they are **excluded** from every
  denominator (15 such files at present). A file that skips-all at runtime is
  scored as a *passing file* contributing 0 tests.
- **Timeouts** (11 files) are excluded from the assertion denominators.
- **`# SKIP` / `# TODO`** lines that rakupp itself emits count as **passed** in
  the numerator — this is standard TAP (a skip/todo is not a failure), and it is
  how every TAP harness, Rakudo's included, scores.

## Fudge directives (`#?rakudo …`)

Roast's raw files carry **fudge directives** — `#?rakudo skip`, `#?rakudo todo`,
`#?rakudo.jvm todo`, etc. Rakudo doesn't run the raw files; it preprocesses them
with `fudge`, converting those comments into real skip/todo for the target
backend. A `#?rakudo todo` marks a test even the reference implementation cannot
yet pass; a `#?rakudo.jvm …` marks one that fails only on the JVM backend.

Raku++ is a **moar-like backend**, so it honours exactly the directives Rakudo-moar
would — and only those:

- **`#?rakudo todo` / `#?rakudo.moar todo` are honoured.** The lexer rewrites each
  such line into a `todo('reason', N)` call, so those N tests emit `# TODO` and
  their failures don't count — identical to what Rakudo's own harness does. These
  are **Rakudo-compatibility passes, not genuine feature coverage**: they mark
  tests the spec itself flags as not-yet-passable. Only a handful of files flip
  on this (they had *no other* failure), and the effect on the assertion
  numerators is a rounding error.
- **`#?rakudo skip` is NOT honoured — we attempt the test.** Skipping a block
  correctly needs its emitted-test count (which we can't know without running),
  and, more importantly, if we *can* pass a skipped test that is a real win worth
  counting. Same for backend-specific `#?rakudo.jvm …` / `#?rakudo.js …`: those
  run and pass on Rakudo-moar, so they plainly belong in the count.

So the denominator is **not** padded down: we still attempt every `skip`-marked
and backend-specific test. The only concession is honouring `todo` exactly as the
moar backend does — the honest, Rakudo-faithful direction to err.

## Zero-regression discipline

A change ships only if the sorted list of fully-passing files (`[PASS]` lines) has
**no removals** versus the prior baseline. Per-assertion numbers may wobble by a
few on timing-sensitive files; the file list is the gate.

## Reproducing

```sh
build/rakupp tools/run-roast.raku          # whole suite; prints all four ratios
build/rakupp tools/run-roast.raku S05      # filter by path substring
```

The tail of the output is the summary block:

```
Files fully passing:  419 / 1462  (28.7%)
Assertions passed:    157293 / 163209  (96.4%)  of tests that ran
Assertions passed:    157293 / 176967  (88.9%)  of tests planned by files that emitted a plan
Assertions passed:    157293 / 191537  (82.1%)  of ALL declared tests (+14570 from 261 no-TAP files read from source; 13 more have no static plan)
```

(No `ROAST` env var is required — the tests' own `use lib` resolves the
Test-Helpers now. Setting `ROAST=<checkout>` is still harmless.)
