# How Raku++ counts Roast results

This is the authoritative definition of the numbers quoted everywhere else
(README, OVERVIEW, GUIDE, FEATURES, ROADMAP, ROAST). If a figure disagrees with
this file, this file wins. The self-hosted harness
([`tools/run-roast.raku`](../../tools/run-roast.raku)) computes all of it and prints it
on every run.

## The one-line summary

> **Per-test: ~95% of all declared tests pass. Coverage: ~51% of files fully pass.**

Quote both, per-test first. The ~95% is the primary correctness number (the fair
per-test bar); the ~51% is the stricter all-or-nothing file bar.

## The measures

Each `.t` file emits [TAP](https://testanything.org/): a `1..N` plan and `ok`/`not
ok` lines. The harness runs every file with a 10-second timeout — that ceiling is
calibrated for rakupp and scales 6x for any other engine the harness is pointed
at, see [Measuring another engine](#measuring-another-engine) — and reports four
ratios, from strictest presentation to fairest, and from widest denominator to
narrowest:

| # | Measure | Current | Definition |
|---|---|---|---|
| 1 | **Files fully passing** | 810 / 1,464 (**~55%**) | a file counts only if *every* planned assertion passes (or it legitimately `plan skip-all`s) |
| 2 | Assertions of **tests that ran** | 208,513 / 213,562 (~98%) | numerator ÷ assertions the files actually emitted |
| 3 | Assertions of **tests planned** | 208,513 / 217,652 (~96%) | ÷ the plan `N` of every file that emitted a plan (so tests lost to a mid-file abort count against us) |
| 4 | Assertions of **all declared tests** | 208,513 / 219,801 (**~95%**) | ÷ every test any file declares — including files that abort before emitting TAP, whose `plan N` is read from source |

**Measure 1 (files, ~55%)** and **measure 4 (all declared tests, ~95%)** are the
two headline numbers. 2 and 3 are diagnostic context, not headlines.

## Why measure 4 is the honest per-test number

The trap is a file that **parse-errors on compile**: it aborts before printing
its `1..N` line, so it emits *nothing*. Under measures 2 and 3 that file
contributes 0 to both numerator and denominator — its tests simply vanish, which
silently flatters the rate. Measure 4 closes that hole: for any file that emitted
no plan at runtime, the harness reads the intended `plan N` straight from the
source and counts all N as failing. That is why 4's denominator (219,801) is ~2.1k larger
than 3's (217,652) — those 2,149 tests live in 60 no-TAP files (parse errors
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
denominator.** Our current run recovers **219,801** declared tests. (This number
GROWS as parse fixes land: a file that used to die before announcing its plan now
declares its real — often larger, dynamically computed — plan, so the percentage
can dip while absolute passes rise.) Only **3 no-TAP files** still have no static
plan to read, so the uncountable remainder is now marginal.

So our same 208,513 passes read two ways:

- **~95%** against *our* denominator (208,513 / 219,801) — *"of the tests we can
  account for, how many pass."* This is what a single harness run can measure,
  and it is the number we quote.
- Essentially the **same ~95%** against the suite's *full* declared total —
  our runner now recovers almost every file's plan, so the two denominators
  have converged; *"of every test the whole suite could declare, how many pass."*

Both are honest; they answer different questions. The convergence means our
headline **~95% is no longer materially flattered** by uncountable files —
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
  that emitted **no** plan at runtime, whether it aborted or was killed by the
  timeout)                                              — denominator of measure 4

The numerator is the **same** in every ratio — only the denominator widens.

### Edge cases

- **skip-all / dynamic-plan files** (`plan skip-all`, `plan *`, `done-testing`
  with no count) have no static test count, so they are **excluded** from every
  denominator (15 such files at present). A file that skips-all at runtime is
  scored as a *passing file* contributing 0 tests.
- **Timeouts** (10 files — mostly sleep-heavy scheduler/IO tests that flap under
  parallel-runner load) are scored exactly like a file that aborts mid-plan: the
  assertions it printed before the kill count, and the rest of its plan counts
  against us. Where it died before emitting `1..N`, that N is recovered from
  source into measure 4, as for a no-TAP file. **Until 2026-09-21 a timeout was
  excluded from every denominator**, which meant its tests did not count against
  the engine — they vanished from numerator and denominator alike, so a run that
  timed out on more files quietly improved its own ratio. That is the same hole
  measure 4 closes for parse errors, and it is now closed for the clock too.
- **Files with no result at all.** Workers park their results and flush the
  *completed prefix* of the file list, so the per-file lines stay in file order.
  That means one file whose result never arrives hides every file behind it, and
  **until 2026-09-21 those files left the tally as absences** — not scored as
  failures, not scored at all, gone from both sides of every ratio and from the
  by-synopsis table, with nothing in the output saying so. A mutsu run reported
  `1,037 + 181 + 22 + 4 = 1,244` of 1,464 files that way. There is now a final
  flush after the workers stop: anything still unflushed is tallied, a file with
  no result is counted in a `LOST:` bucket and named on stderr, its declared
  tests are charged to measure 4, and the summary cross-checks that the buckets
  add up to the file count before printing anything else.
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
comparable at the FILE level, and the gap is not small.** At Roast `b2cbe8a42`
the suite carries **310 `#?rakudo`/`#?rakudo.moar` skip directives shielding 473
tests**, and **359 todo directives shielding 454**, across **277** of its 1,464
files. (Counting the backend-specific `.jvm`/`.js` directives we deliberately do
*not* honour would make it 451 and 585 directives across 386 files — so always
say which set you counted.) One shielded test is enough to decide whether a file
fully passes, which is why honouring them moves the file figure by hundreds.
Before setting our number beside anyone else's, establish that both were taken
on the same bar.

**At the assertion level the same directives are nearly irrelevant**, and that
asymmetry is the single most useful thing to know when reading the two measures
side by side — 927 shielded tests against ~218,000 declared is 0.42%. The
measured version of that is in
["The assertion figures net of skip and todo"](#the-assertion-figures-net-of-skip-and-todo).

Two things make this easy to get wrong:

- **Ours is always on.** `applyRakudoFudge` runs unconditionally in the lexer,
  so every Raku++ Roast figure ever published is a *fudged-bar* figure. There is
  no unfudged mode and no flag to compare against.
- **Other engines may default it off.** [mutsu](https://github.com/tokuhirom/mutsu),
  the Rust implementation, does the same rewriting inside its own interpreter
  (`src/runtime/run_roast_preprocess.rs`) over the same verb set plus `#?v6 …
  skip`, but gates it behind **`MUTSU_FUDGE=1`, which is off by default** and set
  by its own runner. Measured through our harness before it fudged for foreign
  engines, mutsu without that variable scored 1,227 files rather than 1,419.

`tools/run-roast.raku` takes `$*EXECUTABLE` as the engine under test, so it can
score any Raku that can run the harness itself. For an engine other than rakupp
it applies Roast's own `fudge` to every file that carries a directive line and
runs the `.rakudo.moar` sidecar fudge writes beside the `.t` — the same call
Rakudo's `t/harness6` makes — so a foreign run lands on the fudged bar with no
hand-fudged worktree. `--fudge=none` runs the raw files instead, and with
`MUTSU_FUDGE=1` set mutsu is left to apply the directives itself; the provenance
line records which. Measured 2026-09-26 over the 280 files fudge rewrites:
Rakudo 2026.08 fails 260 of them raw and passes 277 fudged, and mutsu's own
preprocessor and Roast's fudge give the same verdict on 279 (the one difference
is a `#?rakudo eval` block mutsu runs partway before dying, which fudge's EVAL
wrapper then over-counts by one test). Record the bar next to the number all the
same, because a figure without its bar attached is not evidence.

### Worked example: mutsu's 98%, and our number counted their way

mutsu's README states it **passes 1,433 out of 1,464 Roast files in full** —
**97.9%** — against our 676 / 1,464 (46.2%). That is the comparison a reader
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
| ours (10 s), v3.25.0 | 651 / 1,464 (44.5%) | 199,980 / 219,403 (91.1%) |
| **counted their way (30 s + their per-file escalations), measured at `v3.23.0-45-gb6905bf`** | **647 / 1,464 (44.2%)** | **199,331 / 219,033 (91.0%)** |

**Four files, three tenths of a point.** The four that come back are
`S15-nfg/concat-stable.t`, `S17-scheduler/at.t`, `S17-scheduler/every.t` and
`S17-scheduler/in.t` — three schedulers and an NFG concat, all of which finish
between 10 s and 30 s. Four more still time out at 30 s
(`S03-operators/minmax.t`, `S03-operators/repeat.t`,
`S17-promise/nonblocking-await.t`, `integration/99problems-41-to-50.t`), and the
remaining four run to completion but fail on assertions rather than the clock,
so no timeout budget recovers them.

That second list is a reading at `v3.23.0-45-gb6905bf` and three of its four
files have since been fixed rather than budgeted: `minmax.t` finishes in
milliseconds, and `repeat.t` and `99problems-41-to-50.t` were engine hangs that
no ceiling would ever have recovered (see the 2026-09-21 snapshot in
[ROAST.md](ROAST.md)). The point the paragraph makes is unaffected — a timeout
budget is worth tenths of a point either way — but do not read the list as
current.

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

Re-measured 2026-09-21, after the harness started giving a foreign engine 6x the
ceiling (60 s) and stopped losing the files behind a missing result: **1,428 /
1,464 files and 219,526 / 220,916 declared assertions (99.4%)**, 16 workers,
3 timeouts. The two readings agree — 1,419 at 10 s, 1,428 at 60 s, 1,433 at
their own 30-180 s — and none of them is a statement about a bar either engine
publishes on its own.

### The assertion figures net of skip and todo

Every assertion figure on this page and in [ROAST.md](ROAST.md) is a **shielded**
number, and until 2026-08-31 nothing said by how much. `parse-tap` in
[`tools/run-roast.raku`](../../tools/run-roast.raku) counts two kinds of line as
passing:

- `ok N # skip …` — the test never executed;
- `not ok N # todo …` — the test executed, genuinely failed, and is counted as a
  pass because the suite marks the failure expected.

Both are the right call: a skip is not a failure, and a TODO failure is a known
one. But a reader comparing 91% with 99% deserves to know what each figure rests
on, so the harness now reports it. Two lines were added to the summary — the
existing ones are unchanged, so the zero-regression gate is unaffected:

```
  of which shielded:  1169 skipped + 197 todo-failed = 1366 (0.69% of the pass count)
Assertions passed NET of skip/todo: 197800 / 218825  (90.4%)  of ALL declared tests
```

**Measured on both engines**, same harness, same Roast revision `b2cbe8a42`,
same 10-second bar, both on the fudged bar (`MUTSU_FUDGE=1` for theirs), one
sitting each on 2026-08-31:

| | Raku++ 3.23.0 | mutsu 0.23.0 |
|---|---:|---:|
| assertions passed (shielded, as published) | 199,166 / 218,825 (**91.0%**) | 216,784 / 218,149 (**99.4%**) |
| ├ `ok … # skip` | 1,169 | 1,159 |
| └ `not ok … # todo` | 197 | 279 |
| total shielded | 1,366 (0.69% of passes) | 1,438 (0.66% of passes) |
| **assertions passed NET of skip/todo** | **197,800 / 218,825 (90.4%)** | **215,346 / 218,149 (98.7%)** |

**The shield is a wash.** Both engines are propped up by almost exactly the same
amount — 1,366 against 1,438 assertions, 0.69% against 0.66% of their respective
pass counts — because both are running the same suite and honouring the same
directive set. Stripping it costs us 0.6 points and them 0.7. The 91-vs-99 gap
comes back as 90.4-vs-98.7, entirely intact.

So: **`#?rakudo skip` explains none of the distance between the two assertion
figures.** It explains a great deal of the distance between the two *file*
figures — measured unfudged, the same harness scores mutsu 1,227 rather than
1,419 — and that is the whole point of keeping the two measures apart.

Two honest caveats on the table:

- **The skipped counts include the suite's own `skip()`/`todo()` calls**, not
  just the ones fudge rewriting produces; TAP cannot tell the two apart after the
  fact. The static split is in the section above: of the ~1,169 skips, 473 are
  attributable to `#?rakudo skip` directives and the rest are the tests' own.
- **The denominators differ slightly** (218,825 against 218,149) because each is
  built from what that engine's run could see — we fall back to a static `plan N`
  read for 76 no-TAP files where mutsu needs it for only 4. That difference is a
  coverage fact in mutsu's favour, not a counting artefact; see
  ["The declared denominator grows with coverage"](#the-declared-denominator-grows-with-coverage).

One number on this page is deliberately *not* net: the headline stays the
shielded one, because that is what every other implementation publishes and
changing it unilaterally would make our figure incomparable rather than more
honest. The net figure is here so that anyone can do the subtraction, and so
that a future sitting cannot quietly grow the shield without it showing.

On 2026-09-25 the NET line and the "of which shielded" line were replaced by
one line. It counts only the tests with no skip or todo on them: every skipped
or todo-marked test, including one that passes under a todo (`ok N # todo`),
is left out of **both** the passed count and the declared total. What remains
is the share of the tests that the suite and its fudge directives expect to
pass that actually do:

```
Assertions passed without skip/todo: 216439 / 218837  (98.9%)  left out: 1186 skipped + 292 todo-failed + 206 todo-passed = 1684
```

The fudge-directive table below it now splits those same 1,684 tests by where
the skip or todo came from, and its Total row equals the line above. Both are
counted with one rule (`parse-tap`'s, where `# skip` wins over `# todo`, so each
line counts once). The table counts tests, not directive lines (`#?rakudo 3
skip` is three), and a TAP line is credited to a directive when its reason
matches. What no directive accounts for is the test code's own
`skip()`/`todo()` calls:

```
| Source                     | Files | Skipped | Todo-failed | Todo-passed | Tests | No-result |
|----------------------------|------:|--------:|------------:|------------:|------:|----------:|
| #?rakudo skip              |   151 |     928 |           0 |           0 |   928 |        28 |
| #?rakudo todo              |   180 |       0 |         262 |         191 |   453 |        54 |
| #?rakudo eval              |     5 |      48 |           0 |           0 |    48 |         0 |
| #?rakudo emit              |     5 |       — |           — |           — |     — |         — |
| skip()/todo() in test code |    39 |     210 |          30 |          15 |   255 |         — |
| Total                      |     — |    1186 |         292 |         206 |  1684 |        82 |
```

No-result is the one column still in directive lines: directives whose tests
never appeared in the output, so there is nothing to count them in tests.

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
Files fully passing:  810 / 1464  (55.3%)
Assertions passed:    208513 / 213562  (97.6%)  of tests that ran
Assertions passed:    208513 / 217652  (95.8%)  of tests planned by files that emitted a plan
Assertions passed:    208513 / 219801  (94.9%)  of ALL declared tests (+2038 from 60 no-TAP and +111 from 5 timed-out files, read from source; 3 more have no static plan)
```

(The harness reads the Roast checkout from `$ROAST`, defaulting to
`$HOME/roast` — set `ROAST=<checkout>` anywhere else. Nothing more is
needed: the tests' own `use lib` resolves the Test-Helpers.)

## Measuring another engine

`tools/run-roast.raku` scores whatever ran it — `$*EXECUTABLE` — so
`rakudo tools/run-roast.raku` puts the reference implementation on this exact
bar, and that comparison is the point of
[dev/findings/ROAST-CEILING-2026-09-17.md](../dev/findings/ROAST-CEILING-2026-09-17.md).
Three things in the harness are calibrated for rakupp and would otherwise be
applied silently to the other engine. Since 2026-09-21 the harness detects a
foreign engine and handles the first two itself:

- **The ceiling.** 10 s is a rakupp budget. Measured serially on an idle 8-core
  box, the 81 S15 files take 10 s of wall under rakupp and 160 s under Rakudo,
  and the heavy ones are not individually slow — `nfkd-9.t` 4.1 s, `nfd-9.t`
  4.1 s, `nfc-9.t` 3.9 s, only `nfc-concat.t` over the line at 23 s. At two
  workers per core a 3-4 s file needs ~2.5x of contention to cross 10 s, and
  S15 alone is 85,079 of the suite's 146,380 statically declared tests. A
  foreign engine gets 6x the budget — the same `ROAST_TIMEOUT=60` the ceiling
  measurement set by hand. `ROAST_TIMEOUT` still overrides.
- **`roast.times`.** Its wall times and CPU samples describe rakupp. They report
  the S15 files at ~0.0 s of CPU, which is true of rakupp and nothing else, so
  the admission controller admitted all of them at once and manufactured the
  contention that pushed them past the ceiling. A foreign engine gets neither
  those estimates nor their ordering unless `--times` says so.
- **Fudging, which the harness does for a foreign engine.** Rakudo does not
  apply `#?rakudo` directives; its own spectest runs Roast's `fudge` first, and
  rakupp applies them in its lexer. Pointed at a raw checkout, Rakudo is scored
  on a bar neither engine uses — 260 of the 280 files fudge rewrites fail. So on
  the foreign path the harness runs Roast's `fudge` (`--keep-exit-code
  --version=v6.d rakudo.moar`, the call `t/harness6` makes) over every file that
  carries a directive line and hands the engine the sidecar it writes beside the
  `.t`. Roast's `.gitignore` lists the pattern, so the checkout stays clean, a
  sidecar in the same directory keeps the tests' `$*PROGRAM.parent(2)` package
  lookups valid, and every report keeps the `.t` name. `--fudge=IMPL` changes
  the implementation name, `--fudge=none` runs the raw files, and a `.t` that
  already is fudge's output (a worktree fudged by hand) is recognised by its
  `# FUDGED!` trailer and left alone. Under rakupp nothing is rewritten: a
  fudged file fed to the lexer is fudged twice. **mutsu** does the rewriting
  itself, inside the interpreter, when `MUTSU_FUDGE=1` is set — with the
  variable set the harness leaves the raw files to it and says so; unset, it
  fudges for mutsu like for any other engine, and the two agree on 279 of the
  280 files.

One more thing travels badly, in the other direction: **the harness itself has
to run on the foreign engine.** It is Raku, and the foreign engine's bugs are
its bugs. mutsu 0.23.0 silently drops `%h{$k}++` when the statement is inside a
named sub, which is exactly where the per-synopsis rollups were written — every
headline figure was correct and the by-synopsis table printed empty. Those five
statements are now `%h{$k} += 1`, which means the same thing everywhere. When a
foreign run produces a figure that looks structurally wrong rather than merely
low, suspect the harness running on that engine before you suspect the engine.

Getting any of this wrong does not produce a slightly-off number, it produces a
meaningless one: `rakudo tools/run-roast.raku` on defaults once reported 76,285
declared tests for a suite that declares ~216,000, and 1,048 of 1,464 files for
an engine measured at 1,433 on the same machine.

## Timeout-partial sensitivity (found 2026-08-09)

The per-assertion top line is **load-banded** through one mechanism: a handful
of borderline mega-files sit right at `ROAST_TIMEOUT`, so machine conditions
decide whether they are killed.

**The mechanism stated here was wrong until 2026-09-21, and the truth was
worse.** This section said the assertions such a file printed before the kill
still counted. They did not: `tally()` skipped a timed-out file entirely, so
crossing the ceiling dropped its *whole* contribution — every assertion it had
emitted and its entire plan — from both sides of the ratio. That swings a run
far harder than partial credit does, and it swings it in the flattering
direction. The behaviour now matches what this section always claimed, so what
follows describes the harness as it stands. A handful of borderline mega-files (the two
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
