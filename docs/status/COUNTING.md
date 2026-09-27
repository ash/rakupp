# How Raku++ counts Roast results

This is the authoritative definition of the Roast numbers quoted everywhere else
(README, OVERVIEW, GUIDE, FEATURES, ROADMAP, ROAST). If a figure disagrees with
this file, this file wins. The harness,
[`tools/run-roast.raku`](../../tools/run-roast.raku), computes all of it and
prints it on every run.

## The one-line summary

> **Per-test: 99.8% of all declared tests pass (219,748 / 220,207). Files: 86.5% of the files `spectest.data` lists fully pass (1,241 / 1,434).**

Quote both, per-test first. The per-test figure is the primary correctness
number; the file figure is the stricter all-or-nothing bar, and the one the v5
target is stated on: all 1,434 files.

The Raku++ figures on this page are from the full `--all` run behind
`597cbf9c` (2026-09-27, Roast `b2cbe8a42`, 6 workers, no timeouts). The
`spectest.data` figures are the same run's files, restricted to the list.

## Which files: `spectest.data`

Roast's checkout holds more than the specification. Rakudo's run list is
`spectest.data` at the checkout root — "a list of all spec tests that are
expected to pass", in its own words — and that list is the harness's default
set. Every figure is stated against one of these sets, and the provenance line
of every run (and every `--list` sidecar) names which:

| Set | Flag | Files | Fully passing | Partial | No TAP | Timeout |
|---|---|---:|---:|---:|---:|---:|
| **`spectest.data`, every entry** | *(default)* | **1,434** | **1,241 / 1,434 (86.5%)** | 190 | 3 | 0 |
| `make spectest`'s set | `--skip-marker=stress` | 1,374 | 1,183 / 1,374 (86.1%) | 188 | 3 | 0 |
| the whole checkout | `--all` | 1,464 | 1,254 / 1,464 (85.7%) | 199 | 11 | 0 |

- **The default runs every listed file, whatever marker its line carries**
  (`stress` 60, `moar` 117, `slow` 38, `Perl` 13, `rakuast` 2). The markers
  are Rakudo's build knobs, not the language's.
- **`--skip-marker=stress`** drops the 60 stress files, which is exactly the set
  Rakudo's `make spectest` (t/harness5) runs.
- **`--all`** adds the 30 files the list leaves out: Roast's own tooling tests
  under `t/`, files the 6.c cut left on master outside the specification, and
  seven tests added since 2020 that the list has not taken up. They carry 467
  declared tests (0.2% of the suite), and 13 of them pass.

The file count in a figure says its set: 1,434 is the list, 1,464 is `--all`.
Figures published before the default changed (`5a08968d`, 2026-09-26) —
including every per-release list in [roast-lists/](roast-lists/) up to
v4.0.1 — are on 1,464.

## The measures

Each `.t` file emits [TAP](https://testanything.org/): a `1..N` plan and
`ok`/`not ok` lines. The harness runs every file under a 10-second ceiling and
reports four ratios, from strictest to fairest:

| # | Measure | `spectest.data` (1,434 files) | `--all` (1,464 files) | Definition |
|---|---|---:|---:|---|
| 1 | **Files fully passing** | 1,241 / 1,434 (**86.5%**) | 1,254 / 1,464 (85.7%) | every planned assertion passes, or the file legitimately `plan skip-all`s |
| 2 | Assertions of tests that **ran** | 219,748 / 220,138 (99.8%) | 220,066 / 220,486 (99.8%) | ÷ assertions the files actually emitted |
| 3 | Assertions of tests **planned** | 219,748 / 220,207 (99.8%) | 220,066 / 220,637 (99.7%) | ÷ the plan `N` of every file that emitted one, so tests lost to a mid-file abort count against us |
| 4 | Assertions of **all declared** tests | 219,748 / 220,207 (**99.8%**) | 220,066 / 220,674 (99.7%) | ÷ every test any file declares, including files that abort before emitting TAP, whose `plan N` is read from source |

**Measures 1 and 4 are the headline numbers.** 2 and 3 are diagnostics.

## Why measure 4 is the honest per-test number

A file that dies on compile never prints its `1..N` line. Under measures 2 and
3 its tests vanish from both sides of the ratio, which flatters the rate.
Measure 4 reads the intended `plan N` from the source of any file that emitted
no plan and counts all N as failing, so a parse error cannot hide its tests.

A file whose plan is dynamic (`plan +@tests`, `done-testing`) is countable only
by running it. If it dies before emitting TAP there is no static number to read,
and it contributes nothing; the summary line reports how many ("N more have no
static plan"). So **measure 4's denominator grows with coverage**: a fix that
lets such a file run adds its real plan, and the percentage can dip while
absolute passes rise. Per-test percentages from two runs are comparable only
over a common denominator; the regression gate is on the file list for that
reason.

On the list today, measures 3 and 4 coincide. Its three no-TAP files are
`S11-repository/cur-candidates.t`, `S32-io/IO-Socket-Async-UDP.t` and
`integration/precompiled.t`. Two of them emit a plan before they die, so their
tests are already in measure 3, and the third has no static plan to recover.

## Exactly how the denominators are built

Per file, `parse-tap` yields `(planned, ran, passed)`, where `planned` is the
runtime `1..N` (`-1` if none was emitted). The harness accumulates:

- `tot-pass`  += `passed`                               — the numerator, shared by all four ratios
- `tot-ran`   += `ran`                                  — denominator of measure 2
- `tot-plan`  += `planned >= 0 ? planned : ran`         — denominator of measure 3
- `declared`  = `tot-plan` + the static `plan N` of each file that emitted
  **no** plan at runtime, whether it aborted or was killed by the ceiling — denominator of measure 4

Only the denominator widens; the numerator is the same in every ratio.

### Edge cases

- **`plan skip-all`** is a passing file that contributes 0 tests.
- **Timeouts** are scored like a mid-plan abort: the assertions printed before
  the kill count, and the rest of the plan counts against us. A file killed
  before its `1..N` has that N recovered from source into measure 4. Files
  whose spec sleeps for real (`S29-context/sleep.t`, the S17 scheduler and
  supply-timing files, `S32-io/lock.t`) get a longer ceiling, listed in
  `%SLOW-FILES` in the harness.
- **Files with no result at all** are counted in a `LOST:` bucket and named on
  stderr, and their declared tests are charged to measure 4. The summary
  checks that the buckets add up to the file count before it prints anything
  else.
- **`# SKIP` / `# TODO`** lines count as passed, as in every TAP harness,
  Rakudo's included. What that shield is worth is measured in
  [the assertion figures net of skip and todo](#the-assertion-figures-net-of-skip-and-todo).

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

**A figure measured with fudge applied and one measured without it are not
comparable at the file level.** Of the list's 1,434 files, 280 carry
directives, and one shielded test decides whether a file fully passes. Measured
2026-09-26: Rakudo 2026.08 fails 260 of those 280 files raw and passes 277 of
them fudged.

**At the assertion level the directives barely matter.** They shield 1,692 of
the list's 220,207 declared tests (0.8%), and removing every skipped and todo
test from both sides leaves the figure where it was
([below](#the-assertion-figures-net-of-skip-and-todo)).

So record the bar next to any number. Ours is always the fudged bar: the lexer
applies the directives unconditionally and there is no off switch. Other
engines may default to the unfudged bar. [mutsu](https://github.com/tokuhirom/mutsu)
applies the same verbs inside its interpreter, but only when `MUTSU_FUDGE=1` is
set; its own runner sets it, and a bare invocation does not.

### Worked example: mutsu's 98%, and our number counted their way

mutsu's README reports **1,433 of 1,464 Roast files fully passing (97.9%)**.
Its runner's rules against ours:

| | Raku++ | mutsu |
|---|---|---|
| fudge | always on, in the lexer | `MUTSU_FUDGE=1`, exported by its runner |
| files attempted | the 1,434 `spectest.data` lists; 1,464 with `--all` | a 1,435-file whitelist (`roast-whitelist.txt`) |
| denominator published | 1,434 | 1,464, the files it does not run counting as failures |
| per-file timeout | 10 s | 30 s, raised per file to 60, 90, 120 or 180 s |
| flaky files | one run each | 24 files in `flaky-tests.txt` re-run on failure |

Both are on the fudged bar, and their denominator is our `--all` set, so the
comparable figures are **their 1,433 against our 1,254 / 1,464 (85.7%)**. Their
longer timeouts would recover nothing for us, because our run has no timeouts.
Re-running flaky files could only add the odd timing-sensitive file. The gap
is coverage, not bookkeeping.

Their figure holds up under our harness: mutsu 0.23.0 with `MUTSU_FUDGE=1`
scored 1,428 / 1,464 files and 219,526 / 220,916 declared assertions (99.4%) on
2026-09-21, at the 60-second foreign ceiling.

### The assertion figures net of skip and todo

The headline assertion figure is **shielded**, as every implementation's is:
`ok N # skip` (never executed) and `not ok N # todo` (executed, failed,
expected to fail) both count as passes. The harness prints the unshielded
figure beside it. That figure leaves every skipped or todo-marked test out of
both the pass count and the declared total, including a todo that passes:

```
Assertions passed without skip/todo: 218056 / 218515  (99.79%)  left out: 1193 skipped + 290 todo-failed + 209 todo-passed = 1692
```

That is 99.79% against the shielded 99.79%, so on the list the shield is worth
nothing at two decimals. The table under that line splits the same 1,692 tests
by where each skip or todo came from, and its Total row equals the line. It
counts tests, not directive lines (`#?rakudo 3 skip` is three), and it credits
a TAP line to a directive when the reasons match. The list's figures:

| Source                     | Files | Skipped | Todo-failed | Todo-passed | Tests | No-result |
|----------------------------|------:|--------:|------------:|------------:|------:|----------:|
| #?rakudo skip              |   151 |     754 |           0 |           0 |   754 |        86 |
| #?rakudo todo              |   180 |       0 |         262 |         195 |   457 |        50 |
| #?rakudo eval              |     5 |      48 |           0 |           0 |    48 |         0 |
| #?rakudo emit              |     5 |       — |           — |           — |     — |         — |
| skip()/todo() in test code |    70 |     391 |          28 |          14 |   433 |         — |
| Total                      |     — |    1193 |         290 |         209 |  1692 |       136 |

Todo-passed is a todo the engine has outgrown. No-result counts directive
lines, not tests: directives whose tests never appeared in the output.

## Zero-regression discipline

A change ships only if the sorted list of fully-passing files (`--list=FILE`)
has **no removals** against the baseline. Per-assertion totals may wobble by a
few on timing-sensitive files; the file list is the gate. Gate runs are taken
alone on the machine, because a concurrent build or second sweep manufactures
timeouts that read as regressions.

### Watch measure 2's DENOMINATOR, not just its numerator

Measure 2 is the only ratio whose denominator moves with the code under test.
When a change makes a file die partway, the tests it no longer reaches leave
both sides at once, and the percentage barely moves. The denominator is where
this shows. Adding `$val ~~ :method` once moved measure 2's numerator by −2,
inside the noise, with the percentage identical to a decimal. But its
denominator fell by 13: the new code evaluated every smartmatch's right-hand
side twice, and a file with side effects there now died. Measures 3 and 4 keep
charging a file's full plan, so they are the honest headlines, and measure 2's
denominator is the early warning.

## Load sensitivity

A file close to its ceiling is killed or not depending on machine load, and a
killed file is charged its unrun plan. So the per-assertion total is
load-banded, and only same-machine, same-conditions runs compare. The three
gate runs behind the current figures had no timeouts. The file bar moves by a
borderline file or two at most.

## Measuring another engine

`tools/run-roast.raku` scores whatever runs it (`$*EXECUTABLE`), so
`rakudo tools/run-roast.raku` puts the reference implementation on this bar.
Rakudo 2026.08 passes 1,425 of the 1,434 listed files that way
([rakudo-2026.08.list](roast-lists/rakudo-2026.08.list)), and Raku++ passes all
nine it fails. Three things are calibrated for Raku++, and the harness adjusts
them when it detects a foreign engine:

- **The ceiling.** A foreign engine gets 6x the budget, 60 s. Rakudo takes the
  81 S15 files 16x longer than Raku++, and S15 is over two fifths of the declared
  tests. `ROAST_TIMEOUT` overrides.
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
build/rakupp tools/run-roast.raku                        # the 1,434 files spectest.data lists
build/rakupp tools/run-roast.raku --skip-marker=stress   # make spectest's 1,374
build/rakupp tools/run-roast.raku --all                  # every .t in the checkout, 1,464
build/rakupp tools/run-roast.raku S05                    # a path substring, within the chosen set
build/rakupp tools/run-roast.raku --list=pass.list --failed
```

`--list=FILE` writes the fully-passing paths, and `--failed` prints every file
that did not fully pass with its failing lines. The run ends with the summary:
the file buckets, the four measures, the unshielded line, the fudge-directive
table and the by-synopsis table. The harness reads the Roast checkout from
`$ROAST`, defaulting to `$HOME/roast`. Nothing else is needed: the tests' own
`use lib` resolves the Test-Helpers.
