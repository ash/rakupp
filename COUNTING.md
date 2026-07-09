# How Raku++ counts Roast results

This is the authoritative definition of the numbers quoted everywhere else
(README, OVERVIEW, GUIDE, FEATURES, ROADMAP, ROAST). If a figure disagrees with
this file, this file wins. The self-hosted harness
([`tools/run-roast.raku`](tools/run-roast.raku)) computes all of it and prints it
on every run.

## The one-line summary

> **Per-test: ~57% of all declared tests pass. Coverage: ~20% of files fully pass.**

Quote both, per-test first. The ~57% is the primary correctness number (the fair
per-test bar); the ~20% is the stricter all-or-nothing file bar.

## The measures

Each `.t` file emits [TAP](https://testanything.org/): a `1..N` plan and `ok`/`not
ok` lines. The harness runs every file with a 10-second timeout and reports four
ratios, from strictest presentation to fairest, and from widest denominator to
narrowest:

| # | Measure | Current | Definition |
|---|---|---|---|
| 1 | **Files fully passing** | 300 / 1,464 (**~20%**) | a file counts only if *every* planned assertion passes (or it legitimately `plan skip-all`s) |
| 2 | Assertions of **tests that ran** | 131,320 / 189,081 (~69%) | numerator ÷ assertions the files actually emitted |
| 3 | Assertions of **tests planned** | 131,320 / 199,944 (~66%) | ÷ the plan `N` of every file that emitted a plan (so tests lost to a mid-file abort count against us) |
| 4 | Assertions of **all declared tests** | 131,320 / 231,092 (**~57%**) | ÷ every test any file declares — including files that abort before emitting TAP, whose `plan N` is read from source |

**Measure 1 (files, ~20%)** and **measure 4 (all declared tests, ~57%)** are the
two headline numbers. 2 and 3 are diagnostic context, not headlines.

## Why measure 4 is the honest per-test number

The trap is a file that **parse-errors on compile**: it aborts before printing
its `1..N` line, so it emits *nothing*. Under measures 2 and 3 that file
contributes 0 to both numerator and denominator — its tests simply vanish, which
silently flatters the rate. Measure 4 closes that hole: for any file that emitted
no plan at runtime, the harness reads the intended `plan N` straight from the
source and counts all N as failing. That is why 4's denominator (231,092) is
~31k larger than 3's (199,944) — those 31,148 tests live in 385 parse-error
files, recovered from source. A parse error can no longer hide its tests.

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
  denominator (16 such files at present). A file that skips-all at runtime is
  scored as a *passing file* contributing 0 tests.
- **Timeouts** (3 files) are excluded from the assertion denominators.
- **`# SKIP` / `# TODO`** lines that rakupp itself emits count as **passed** in
  the numerator — this is standard TAP (a skip/todo is not a failure), and it is
  how every TAP harness, Rakudo's included, scores.

## What we deliberately do NOT subtract

Roast's raw files carry **fudge directives** — `#?rakudo skip`, `#?rakudo.jvm
todo`, etc. Rakudo doesn't run the raw files; it preprocesses them with `fudge`,
which converts those comments into real skip/todo for the target backend. So a
`#?rakudo skip` marks a test the reference implementation is not expected to pass.

**Raku++ ignores fudge and attempts every test.** We do **not** subtract
fudge-skipped or backend-specific (`#?rakudo.jvm …`) tests from the denominator:

- an all-backends `#?rakudo skip` is Rakudo's limitation, not a reason we
  shouldn't try — and if we pass it, that win should count;
- a `#?rakudo.jvm skip` still runs and passes on Rakudo-moar (the real
  reference), so it plainly belongs in the count.

There are ~1,088 such directives (489 skip + 599 todo) across 404 files —
under ~1% of the 231k. Net effect: our denominator is **~1% stricter** than
Rakudo's own effective target, which is the honest direction to err. We never
pad the denominator down to flatter the percentage.

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
Files fully passing:  300 / 1464   (20.5%)
Assertions passed:    131320 / 189081  (69.5%)  of tests that ran
Assertions passed:    131320 / 199944  (65.7%)  of tests planned by files that emitted a plan
Assertions passed:    131320 / 231092  (56.8%)  of ALL declared tests (+31148 from 385 no-TAP files read from source; 16 more have no static plan)
```

(No `ROAST` env var is required — the tests' own `use lib` resolves the
Test-Helpers now. Setting `ROAST=<checkout>` is still harmless.)
