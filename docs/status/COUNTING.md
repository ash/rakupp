# How Raku++ counts Roast results

This is the authoritative definition of the Roast numbers quoted everywhere else
(README, OVERVIEW, GUIDE, FEATURES, ROADMAP, ROAST). If a figure disagrees with
this file, this file wins. The harness,
[`tools/run-roast.raku`](../../tools/run-roast.raku), computes all of it and
prints it on every run.

## The one-line summary

> **100.00% of Roast's assertions pass, with skipped and todo tests left out (218,420 / 218,420).**

That is the number to quote first. Second comes the same count with skip and
todo counted as passes, and third the files that fully pass. The file count is
the strictest bar and the least informative, because one failing assertion
fails a whole file.

The figures on this page were measured on 2026-10-02, on the v5.2.0 release
build, against Roast `1f749e338` and the 1,424 files `spectest.data` lists
(three runs, all three alike).

## The measures

The harness's summary prints five ratios. In order of importance:

| Rank | Measure | Current (1,424 files) | Definition |
|---|---|---:|---|
| **1st** | **Assertions passed without skip/todo** | **218,420 / 218,420 (100.00%)** | every declared test except the skipped and todo-marked ones, which leave both sides |
| 2nd | Assertions of all declared tests (measure 4) | 220,055 / 220,055 (100.00%) | the same, with `# skip` and `# todo` lines counted as passes |
| 3rd | Files fully passing (measure 1) | 1,424 / 1,424 (100.00%) | every planned assertion passes, or the file legitimately `plan skip-all`s |
| diagnostic | Assertions of tests planned (measure 3) | 220,055 / 220,055 (100.00%) | ÷ the plan `N` of every file that emitted one |
| diagnostic | Assertions of tests that ran (measure 2) | 220,055 / 220,055 (100.00%) | ÷ assertions the files actually emitted |

Measures 1 to 4 are the harness's original four ratios. The rest of this page
and the harness's own comments use those names.

## The assertion figures net of skip and todo

Roast marks tests it does not expect an implementation to pass: `#?rakudo
skip` and `#?rakudo todo` directives, and the test code's own `skip()` and
`todo()` calls. TAP scores `ok N # skip` (never executed) and `not ok N # todo`
(executed, failed, expected to fail) as passes, as every harness does, Rakudo's
included. Measure 4 follows that rule.

The headline does not. It takes every skipped or todo-marked test out of both
the pass count and the declared total, including a todo that passes. What is
left is the share of the tests Roast expects to pass that actually do:

```
Assertions passed without skip/todo: 218420 / 218420  (100.00%)  left out: 1167 skipped + 283 todo-failed + 185 todo-passed = 1635
```

The table printed under that line splits the same 1,635 tests by where each
skip or todo came from, and its Total row equals the line. It counts tests, not
directive lines (`#?rakudo 3 skip` is three), and it credits a TAP line to a
directive when the reasons match:

| Source                     | Files | Skipped | Todo-failed | Todo-passed | Tests | No-result |
|----------------------------|------:|--------:|------------:|------------:|------:|----------:|
| #?rakudo skip              |   149 |     728 |           0 |           0 |   728 |        86 |
| #?rakudo todo              |   175 |       0 |         256 |         168 |   424 |        50 |
| #?rakudo eval              |     5 |      48 |           0 |           0 |    48 |         0 |
| #?rakudo emit              |     5 |       — |           — |           — |     — |         — |
| skip()/todo() in test code |    71 |     391 |          27 |          17 |   435 |         — |
| Total                      |     — |    1167 |         283 |         185 |  1635 |       136 |

Todo-passed is a todo the engine has outgrown. No-result counts directive
lines, not tests: directives whose tests never appeared in the output.

## Which files: `spectest.data`

Roast's checkout holds more than the specification. Rakudo's run list is
`spectest.data` at the checkout root — "a list of all spec tests that are
expected to pass", in its own words — and that list is the harness's default
set. The provenance line of every run, and every `--list` sidecar, names the set
a figure is on:

| Set | Flag | Files | Fully passing | Partial | No TAP | Timeout |
|---|---|---:|---:|---:|---:|---:|
| **`spectest.data`, every entry** | *(default)* | **1,424** | 1,424 / 1,424 (100.00%) | 0 | 0 | 0 |
| `make spectest`'s set | `--skip-marker=stress` | 1,364 | 1,364 / 1,364 (100.00%) | 0 | 0 | 0 |
| the whole checkout | `--all` | 1,454 | 1,435 / 1,454 (98.69%) | 9 | 10 | 0 |

The `--skip-marker=stress` row is the second of two runs; the first lost
`S17-promise/nonblocking-await.t` to the crash
[ROAST.md](ROAST.md#the-one-file-that-flaps) describes.

- **The default runs every listed file, whatever marker its line carries**
  (`stress` 60, `moar` 117, `slow` 38, `Perl` 13, `rakuast` 2). The markers
  are Rakudo's build knobs, not the language's.
- **`--skip-marker=stress`** drops the 60 stress files, which is exactly the set
  Rakudo's `make spectest` (t/harness5) runs.
- **`--all`** adds the 30 files the list leaves out: Roast's own tooling tests
  under `t/`, files the 6.c cut left on master outside the specification, and
  seven tests added since 2020 that the list has not taken up. They carry 467
  declared tests (0.2% of the suite). On `--all` the headline is
  218,711 / 218,865 (99.93%).

The file count in a figure says its set. At Roast `1f749e338`, as at `1f521d798`, the list is 1,424
files and `--all` is 1,454; at `b2cbe8a42`, before Roast removed its eleven
`:P5` files and added `sprintf-a.t`, they were 1,434 and 1,464. Figures
published before the default changed (`5a08968d`, 2026-09-26) — including every
per-release list in [roast-lists/](roast-lists/) up to v4.0.1 — are on 1,464.

## Declared tests: why an aborted file still counts

A file that dies on compile never prints its `1..N` line. Counting only what
ran, its tests would vanish from both sides of the ratio and flatter the rate.
So for any file that emitted no plan, the harness reads the intended `plan N`
from its source and counts all N as failing. Measure 4 and the headline are
both built on that declared total, so a parse error cannot hide its tests from
either.

A file whose plan is dynamic (`plan +@tests`, `done-testing`) is countable only
by running it. If it dies before emitting TAP, there is no static number to
read and it contributes nothing. The summary reports how many ("N more have no
static plan"). So **the declared total grows with coverage**: a fix that lets
such a file run adds its real plan, and a percentage can dip while absolute
passes rise. Per-test percentages from two runs are comparable only over a
common denominator, which is one reason the regression gate is on the file list.

On the list today measures 2, 3 and 4 coincide: every file emits its plan and
none times out, so tests that ran, tests planned and tests declared are the
same 220,055.

## Exactly how the denominators are built

Per file, `parse-tap` yields `(planned, ran, passed)` and the skip/todo counts,
where `planned` is the runtime `1..N` (`-1` if none was emitted). The harness
accumulates:

- `tot-pass`  += `passed`                               — the numerator of measures 2 to 4
- `tot-ran`   += `ran`                                  — denominator of measure 2
- `tot-plan`  += `planned >= 0 ? planned : ran`         — denominator of measure 3
- `declared`  = `tot-plan` + the static `plan N` of each file that emitted
  **no** plan at runtime, whether it aborted or was killed by the ceiling — denominator of measure 4
- `fudged`    = skipped + todo-failed + todo-passed; the headline is
  `(tot-pass − fudged) / (declared − fudged)`

### Edge cases

- **`plan skip-all`** is a passing file that contributes 0 tests.
- **Timeouts** are scored like a mid-plan abort: the assertions printed before
  the kill count, and the rest of the plan counts against us. A file killed
  before its `1..N` has that N recovered from source. Files whose spec sleeps
  for real (`S29-context/sleep.t`, the S17 scheduler and supply-timing files,
  `S32-io/lock.t`) get a longer ceiling, listed in `%SLOW-FILES` in the
  harness.
- **Files with no result at all** are counted in a `LOST:` bucket and named on
  stderr, and their declared tests are charged to the declared total. The
  summary checks that the buckets add up to the file count before it prints
  anything else.

## Fudge directives (`#?rakudo …`)

Roast's files carry **fudge directives**: `#?rakudo skip`, `#?rakudo todo`,
`#?rakudo.jvm todo`, and so on. Rakudo does not run the raw files; its harness
first rewrites them with Roast's `fudge`. A `#?rakudo todo` marks a test even
the reference implementation cannot pass yet.

Raku++ is a **moar-like backend**, so it honours exactly the directives
Rakudo-moar would. `applyRakudoFudge()` in [`src/Lexer.cpp`](../../src/Lexer.cpp)
rewrites the source as it is lexed (its comment block has the authoritative
verb list):

| verb | what we do |
|---|---|
| `todo` | rewrite the line into `todo('reason', N);`, so the next N tests emit `# TODO` |
| `skip` | comment out the next N test statements or column-0 `{…}` blocks and emit `skip('reason', numtests);` in their place |
| `emit` | replace the line with its argument code, verbatim |
| `eval` / `try` | treated as `skip` |
| `#?DOES n` | the next statement, or `sub NAME`, counts as n tests |

Only bare `#?rakudo` and `#?rakudo.moar` apply. `#?rakudo.jvm`, `#?rakudo.js`
and `#?v6…` are not honoured, because those tests run and pass on Rakudo-moar.
Line numbers are preserved, one line in and one line out, so a runtime error
names the right line of the original file.

The rewriting is in the lexer, not a call to Roast's `fudge`, because `fudge`
leaves the directive comment in place: a fudged file fed to Raku++ would apply
every `todo` twice and hide a real failure. **Anything measuring Raku++ runs the
raw `.t` files.** For any other engine the harness runs Roast's `fudge` itself
(see [Measuring another engine](#measuring-another-engine)).

### Comparing our figure with another implementation's

**A file count measured with fudge applied and one measured without it are not
comparable.** Of the list's 1,424 files, 275 carry directives, and one shielded
test decides whether a file fully passes. Measured 2026-09-26, on the 280 files
that carried directives at Roast `b2cbe8a42`: Rakudo 2026.08 fails 260 of them
raw and passes 277 of them fudged.

**At the assertion level the directives barely matter.** They shield 1,635 of
the list's 220,055 declared tests (0.7%). The headline leaves them out of both
sides and measure 4 counts them as passes, and both read 100.00%.

For a file count, record the bar next to the number. Ours is always the fudged
bar: the lexer applies the directives unconditionally and there is no off
switch. Other engines may default to the unfudged bar.
[mutsu](https://github.com/tokuhirom/mutsu) applies the same verbs inside its
interpreter, but only when `MUTSU_FUDGE=1` is set; its own runner sets it, and a
bare invocation does not.

### Worked example: mutsu's 98%, and our number counted their way

mutsu's README reports **1,433 of 1,464 Roast files fully passing (97.9%)**.
Its runner's rules against ours:

| | Raku++ | mutsu |
|---|---|---|
| fudge | always on, in the lexer | `MUTSU_FUDGE=1`, exported by its runner |
| files attempted | the files `spectest.data` lists; every `.t` with `--all` | a 1,435-file whitelist (`roast-whitelist.txt`) |
| denominator published | the list (1,424 at `1f521d798`) | 1,464, the files it does not run counting as failures |
| per-file timeout | 10 s | 30 s, raised per file to 60, 90, 120 or 180 s |
| flaky files | one run each | 24 files in `flaky-tests.txt` re-run on failure |

Both are on the fudged bar, and their denominator is our `--all` set, so the
comparable file figures are **their 1,433 of 1,464 against our 1,434 of 1,454**
— the same measure on two Roast revisions ten files apart, since mutsu's figure
predates Roast's removal of the `:P5` files.

Their figure holds up under our harness: mutsu 0.23.0 with `MUTSU_FUDGE=1`
scored 1,428 / 1,464 files and 219,526 / 220,916 declared assertions (99.4%) on
2026-09-21, at the 60-second foreign ceiling.

## Zero-regression discipline

A change ships only if the sorted list of fully-passing files (`--list=FILE`)
has **no removals** against the baseline. Assertion totals may wobble by a few
on timing-sensitive files, so the file list is the gate even though it is not
the headline. Gate runs are taken alone on the machine, because a concurrent
build or second sweep manufactures timeouts that read as regressions.

### Watch measure 2's DENOMINATOR, not just its numerator

Measure 2 is the only ratio whose denominator moves with the code under test.
When a change makes a file die partway, the tests it no longer reaches leave
both sides at once, and the percentage barely moves. The denominator is where
this shows. Adding `$val ~~ :method` once moved measure 2's numerator by −2,
inside the noise, with the percentage identical to a decimal. But its
denominator fell by 13: the new code evaluated every smartmatch's right-hand
side twice, and a file with side effects there now died. The declared total
keeps charging a file's full plan, so the headline and measure 4 stay honest,
and measure 2's denominator is the early warning.

## Load sensitivity

A file close to its ceiling is killed or not depending on machine load, and a
killed file is charged its unrun plan. So assertion totals are load-banded, and
only same-machine, same-conditions runs compare. Before blaming load, run the
timed-out file alone: one that times out alone too is a hang.

## Measuring another engine

`tools/run-roast.raku` scores whatever runs it (`$*EXECUTABLE`), so
`rakudo tools/run-roast.raku` puts the reference implementation on this bar.
Rakudo 2026.08 passes 1,414 of the 1,424 files listed at Roast `1f521d798` that
way ([rakudo-2026.08-1f521d798.list](roast-lists/rakudo-2026.08-1f521d798.list)),
and Raku++ passes all ten it fails; at `b2cbe8a42` it passed 1,425 of 1,434
([rakudo-2026.08.list](roast-lists/rakudo-2026.08.list)). Three things are calibrated for Raku++, and the harness adjusts
them when it detects a foreign engine:

- **The ceiling.** A foreign engine gets 12x the budget, 120 s. Rakudo takes
  the 81 S15 files 16x longer than Raku++, and S15 is over two fifths of the
  declared tests; `S32-str/sprintf-b.t` and `sprintf-x.t` (`use v6.e.PREVIEW`,
  2,282 subtests each) take 46 s each under Rakudo alone. The spec-sleep files
  keep limits of their own, 6x Raku++'s. `ROAST_TIMEOUT` overrides the default.
- **`roast.times`.** Its wall times and CPU samples describe Raku++, so a
  foreign engine gets neither those estimates nor their ordering unless
  `--times` says so.
- **Fudging.** The harness runs Roast's `fudge` (`--keep-exit-code
  --version=v6.d rakudo.moar`, the call Rakudo's `t/harness6` makes) over every
  file with a directive line, and hands the engine the sidecar it writes beside
  the `.t`. Reports keep the `.t` name. `--fudge=IMPL` changes the
  implementation name, and `--fudge=none` runs the raw files. A file that is
  already fudge output is recognised by its `# FUDGED!` trailer and left alone.
  With `MUTSU_FUDGE=1` set, mutsu applies the directives itself and the harness
  leaves the raw files to it. The two methods agree on 279 of the 280 files.

**The harness itself runs on the foreign engine,** so that engine's bugs are the
harness's bugs. mutsu 0.23.0 once dropped `%h{$k}++` inside a named sub and
printed an empty by-synopsis table under correct headline figures. When a
foreign run looks structurally wrong rather than merely low, suspect the harness
on that engine first.

## Reproducing

```sh
build/rakupp tools/run-roast.raku                        # the files spectest.data lists (1,424)
build/rakupp tools/run-roast.raku --skip-marker=stress   # make spectest's set (1,364)
build/rakupp tools/run-roast.raku --all                  # every .t in the checkout (1,454)
build/rakupp tools/run-roast.raku S05                    # a path substring, within the chosen set
build/rakupp tools/run-roast.raku --list=pass.list --failed
```

`--list=FILE` writes the fully-passing paths, and `--failed` prints every file
that did not fully pass with its failing lines. The run ends with the summary:
the file buckets, the five ratios, the fudge-directive table and the
by-synopsis table. The harness reads the Roast checkout from `$ROAST`,
defaulting to `$HOME/roast`. Nothing else is needed: the tests' own `use lib`
resolves the Test-Helpers.
