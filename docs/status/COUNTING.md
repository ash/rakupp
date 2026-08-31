# How Raku++ counts Roast results

This is the authoritative definition of the numbers quoted everywhere else
(README, OVERVIEW, GUIDE, FEATURES, ROADMAP, ROAST). If a figure disagrees with
this file, this file wins. The self-hosted harness
([`tools/run-roast.raku`](../../tools/run-roast.raku)) computes all of it and prints it
on every run.

## The one-line summary

> **Per-test: ~90% of all declared tests pass. Coverage: ~43% of files fully pass.**

Quote both, per-test first. The ~90% is the primary correctness number (the fair
per-test bar); the ~43% is the stricter all-or-nothing file bar.

## The measures

Each `.t` file emits [TAP](https://testanything.org/): a `1..N` plan and `ok`/`not
ok` lines. The harness runs every file with a 10-second timeout and reports four
ratios, from strictest presentation to fairest, and from widest denominator to
narrowest:

| # | Measure | Current | Definition |
|---|---|---|---|
| 1 | **Files fully passing** | 643 / 1,464 (**~43%**) | a file counts only if *every* planned assertion passes (or it legitimately `plan skip-all`s) |
| 2 | Assertions of **tests that ran** | 198,939 / 205,416 (~97%) | numerator ÷ assertions the files actually emitted |
| 3 | Assertions of **tests planned** | 198,939 / 215,661 (~92%) | ÷ the plan `N` of every file that emitted a plan (so tests lost to a mid-file abort count against us) |
| 4 | Assertions of **all declared tests** | 198,939 / 218,773 (**~90%**) | ÷ every test any file declares — including files that abort before emitting TAP, whose `plan N` is read from source |

**Measure 1 (files, ~43%)** and **measure 4 (all declared tests, ~90%)** are the
two headline numbers. 2 and 3 are diagnostic context, not headlines.

## Why measure 4 is the honest per-test number

The trap is a file that **parse-errors on compile**: it aborts before printing
its `1..N` line, so it emits *nothing*. Under measures 2 and 3 that file
contributes 0 to both numerator and denominator — its tests simply vanish, which
silently flatters the rate. Measure 4 closes that hole: for any file that emitted
no plan at runtime, the harness reads the intended `plan N` straight from the
source and counts all N as failing. That is why 4's denominator (218,773) is ~3.1k larger
than 3's (215,661) — those 3,112 tests live in 77 no-TAP files (parse errors
and runtime aborts), recovered from source. A parse error can no longer hide
its tests.

## The declared denominator grows with coverage

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
denominator.** Our current run recovers **218,773** declared tests. (This number
GROWS as parse fixes land: a file that used to die before announcing its plan now
declares its real — often larger, dynamically computed — plan, so the percentage
can dip while absolute passes rise.) Only **3 no-TAP files** still have no static
plan to read, so the uncountable remainder is now marginal.

So our same 198,939 passes read two ways:

- **~90%** against *our* denominator (198,939 / 218,773) — *"of the tests we can
  account for, how many pass."* This is what a single harness run can measure,
  and it is the number we quote.
- Essentially the **same ~90%** against the suite's *full* declared total —
  our runner now recovers almost every file's plan, so the two denominators
  have converged; *"of every test the whole suite could declare, how many pass."*

Both are honest; they answer different questions. The convergence means our
headline **~90% is no longer materially flattered** by uncountable files —
almost every declared test is charged for or against us. It also means **raw per-test
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
- **Timeouts** (14 files — mostly sleep-heavy scheduler/IO tests that flap
  under parallel-runner load) are excluded from the assertion denominators.
- **`# SKIP` / `# TODO`** lines that rakupp itself emits count as **passed** in
  the numerator — this is standard TAP (a skip/todo is not a failure), and it is
  how every TAP harness, Rakudo's included, scores.

## Fudge directives (`#?rakudo …`)

Roast's raw files carry **fudge directives** — `#?rakudo skip`, `#?rakudo todo`,
`#?rakudo.jvm todo`, etc. Rakudo doesn't run the raw files; it preprocesses them
with `fudge`, converting those comments into real skip/todo for the target
backend. A `#?rakudo todo` marks a test even the reference implementation cannot
yet pass; a `#?rakudo.jvm …` marks one that fails only on the JVM backend.

Raku++ is a **moar-like backend**, so it honours exactly the directives
Rakudo-moar would — and only those. The implementation is `applyRakudoFudge()`
in [`src/Lexer.cpp`](../../src/Lexer.cpp) (see the comment block at the top of
that function for the authoritative verb list), which rewrites the source as it
is lexed:

| verb | what we do |
|---|---|
| `todo` | rewrite the directive line into `todo('reason', N);` — the next N tests emit `# TODO`, so their failures don't count |
| `skip` | comment out the next N test statements / column-0 `{…}` blocks and emit `skip('reason', numtests);` in their place, so the plan stays satisfied without running the guarded construct |
| `emit` | replace the directive line with its argument code, verbatim |
| `eval` / `try` | treated as `skip` (they guard constructs that need `EVAL` protection) |
| `#?DOES n` | the next statement — or `sub NAME` — counts as n tests |

Only bare `#?rakudo` and `#?rakudo.moar` apply. **`#?rakudo.jvm`, `#?rakudo.js`
and `#?v6…` are deliberately not honoured**: those tests run and pass on
Rakudo-moar, so they plainly belong in the count. Line numbers are preserved
throughout — one line in, one line out — so a runtime error still names the
right line in the original file.

We do the rewriting **in the lexer rather than by shelling out to Roast's own
`fudge`**, and that is not a stylistic choice. `fudge` prepends the generated
call to the test line but *leaves the directive comment in place*, so a file
preprocessed by `fudge` and then run by Raku++ would have every `todo` applied
twice — the second one leaking onto the following test and silently hiding a
real failure. Anything measuring Raku++ must run the raw `.t` files.

> **History.** Until fudge-skip landed (commit `25c25ee`, "Roast 350 → 378")
> this section said `skip` was *not* honoured, and that claim outlived the code
> by some weeks. If you are reconciling an old number with a new one, that is
> the discontinuity.

### Comparing our figure with another implementation's

**A Roast number measured with fudge applied and one measured without it are not
comparable, and the gap is not small.** The suite carries 262 `#?rakudo skip`
and 282 `#?rakudo todo` directives spread across 407 of its 1,464 files, so
whether an engine honours them moves the file-level figure by hundreds of files.
Before setting our number beside anyone else's, establish that both were taken
on the same bar.

Two things make this easy to get wrong:

- **Ours is always on.** `applyRakudoFudge` runs unconditionally in the lexer,
  so every Raku++ Roast figure ever published is a *fudged-bar* figure. There is
  no unfudged mode and no flag to compare against.
- **Other engines may default it off.** [mutsu](https://github.com/tokuhirom/mutsu),
  the Rust implementation, does the same rewriting inside its own interpreter
  (`src/runtime/run_roast_preprocess.rs`) over the same verb set plus `#?v6 …
  skip`, but gates it behind **`MUTSU_FUDGE=1`, which is off by default** and set
  by its own runner. Pointing our harness at a mutsu binary without that variable
  measures it unfudged and understates it by a wide margin.

`tools/run-roast.raku` takes `$*EXECUTABLE` as the engine under test, so it can
score any Raku that can run the harness itself. When you do that, set whatever
the other engine's fudge switch is — and record it next to the number, because a
figure without its bar attached is not evidence.

### Worked example: mutsu's 98%, and our number counted their way

mutsu's README states it **passes 1,433 out of 1,464 Roast files in full** —
**97.9%** — against our 643 / 1,464 (43.9%). That is the comparison a reader
will make, so this section does it properly: first what their number means, then
what ours becomes under their rules.

**Their rules, from their own runner** (`Makefile` target `roast`, driving
`prove` over `scripts/run-roast-test.sh`), against ours:

| | Raku++ | mutsu |
|---|---|---|
| fudge | unconditional, no off switch | `MUTSU_FUDGE=1`, exported by the runner |
| files attempted | all **1,464** | a **1,435-file whitelist** (`roast-whitelist.txt`) |
| denominator published | 1,464 | 1,464 — the 29 unrun files count against them |
| per-file timeout | **10 s** | **30 s** default, escalated per file to 60 / 90 / 120 / **180 s** |
| flaky files | none; one run each | 24 files in `flaky-tests.txt` are **re-rolled** on failure |

Both engines therefore measure on the **same fudged bar**, which is the
difference that would have mattered most. The whitelist does not flatter them
either: they publish against the full 1,464, so the 29 files they never run are
counted as not passing. That is the conservative choice, and it is worth saying
plainly rather than implying otherwise.

**The one rule that moves our number is the timeout**, and it moves it by very
little. Re-running the whole suite at their 30-second default — the only change,
same binary (`v3.23.0-45-gb6905bf`), same Roast revision `b2cbe8a42`, same
counting rules — takes the timeout column from 12 files to 4:

| bar | fully passing | assertions, all declared |
|---|---:|---:|
| ours (10 s) | 643 / 1,464 (43.9%) | 198,939 / 218,773 (90.9%) |
| **counted their way (30 s + their per-file escalations)** | **647 / 1,464 (44.2%)** | **199,331 / 219,033 (91.0%)** |

**Four files, three tenths of a point.** The four that come back are
`S15-nfg/concat-stable.t`, `S17-scheduler/at.t`, `S17-scheduler/every.t` and
`S17-scheduler/in.t` — three schedulers and an NFG concat, all of which finish
between 10 s and 30 s. Four more still time out at 30 s
(`S03-operators/minmax.t`, `S03-operators/repeat.t`,
`S17-promise/nonblocking-await.t`, `integration/99problems-41-to-50.t`), and the
remaining four run to completion but fail on assertions rather than the clock,
so no timeout budget recovers them.

**So the counting rules are not the story.** Adopting every one of mutsu's
conventions moves Raku++ from 43.9% to 44.2%, against their 97.9%. The gap is
coverage, and the shape of ours is the all-or-nothing file bar: we pass ~91% of
declared assertions but leave a residue in most files, so the file count stays
low while the assertion count does not. See
["Why measure 4 is the honest per-test number"](#why-measure-4-is-the-honest-per-test-number).

**Their figure independently checks out.** Running mutsu 0.23.0 under *our*
harness with `MUTSU_FUDGE=1` — our 10-second bar, not theirs — gives 1,419 /
1,464, and twelve of the files it loses are timeouts their own runner budgets 30
to 180 seconds for. 1,419 at 10 s and 1,433 at 30 s+ are the same engine
measured on two bars, not a discrepancy. Note also that the README's 1,433 was
written 2026-07-23 and the file itself points at their site for the live figure,
so treat it as a floor rather than a current reading.

## Zero-regression discipline

A change ships only if the sorted list of fully-passing files (`[PASS]` lines) has
**no removals** versus the prior baseline. Per-assertion numbers may wobble by a
few on timing-sensitive files; the file list is the gate.

### Watch measure 2's DENOMINATOR, not just its numerator

Measure 2 is the only ratio whose denominator moves with the code under test:
it counts assertions the files *actually emitted*. When a change makes a file die
partway through, that file stops printing TAP, so the lost tests leave **both**
sides of measure 2 at once. The percentage barely twitches while real coverage
walks out of the room.

A worked example, from the 2026-07-26 conformance batch. Baseline:

```
Assertions passed:    195924 / 200440  (97.8%)  of tests that ran
```

Adding `$val ~~ :method` (a Pair on the right of a smartmatch names a method to
call) gave:

```
Assertions passed:    195922 / 200427  (97.8%)  of tests that ran
```

The numerator moved by −2, comfortably inside the flapper band, and the
percentage was identical to one decimal place. The **denominator** is what gave
the change away: 13 assertions stopped being emitted, so some file was now dying
mid-run. The cause was that the new code evaluated the smartmatch's right-hand
side eagerly to find out whether it was a Pair — which meant every *other*
right-hand side got evaluated twice, and anything with side effects ran twice.
Restricting the check to a syntactic pair node restored the emission count and
turned the −2 into a +10:

```
Assertions passed:    195934 / 200440  (97.8%)  of tests that ran
```

Note that 200,440 is measure 2's denominator — *tests that ran*. The ~214k and
~216k figures on the next two lines of the same summary block are measures 3 and
4, and they do **not** move like this: a file that dies mid-run still declared
its `plan N`, so measures 3 and 4 keep charging us for every test it failed to
reach. That is exactly what makes them the honest headline numbers — and exactly
what makes measure 2's moving denominator the sharpest *early warning* while
iterating.

## Reproducing

```sh
build/rakupp tools/run-roast.raku          # whole suite; prints all four ratios
build/rakupp tools/run-roast.raku S05      # filter by path substring
```

The tail of the output is the summary block:

```
Files fully passing:  584 / 1462  (39.9%)
Assertions passed:    194901 / 199872  (97.5%)  of tests that ran
Assertions passed:    194901 / 212964  (91.5%)  of tests planned by files that emitted a plan
Assertions passed:    194901 / 216222  (90.0%)  of ALL declared tests (+3258 from 93 no-TAP files read from source; 4 more have no static plan)
```

(No `ROAST` env var is required — the tests' own `use lib` resolves the
Test-Helpers now. Setting `ROAST=<checkout>` is still harmless.)

## Timeout-partial sensitivity (found 2026-08-09)

The per-assertion top line is **load-banded** through one mechanism: a file
that hits `ROAST_TIMEOUT` is killed, but the assertions it printed *before*
the kill still count. A handful of borderline mega-files (the two
2,282-assertion sprintf files, the S03/S32 minmax pair, several S15 tables)
sit right at the timeout on the default binary, so how far each gets before
the kill moves the total by **thousands per run** with machine conditions.
Concretely: the v3.0.1 release band (197,056–197,098 over four runs, 595 /
593 / 594 / 594 files, 13/13/12/14 timing out) was measured on a machine
carrying the release campaign's own build load, and the quoted figure is the
repeating profile rather than the best of them — the fourth run exists only
because the first three produced no repeating file count, which is the
situation this rule is for. The v3.0.0 notes quoted the top of their own band,
which is part of why the two releases' headline numbers differ by more than
the code does. An earlier
commit configuration re-measured twice on an idle machine scored
210,239/216,557 with an identical 9-file timeout list (±2 assertions
between runs). Neither number is wrong — they are the same binary under
different load — but only same-day, same-conditions runs are comparable,
which is how the batch gate has always used them. The all-or-nothing
files-fully-passing bar and the distribution bar do not have this
sensitivity.
