# Raku++ against Roast

[Roast](https://github.com/Raku/roast) is the official Raku specification test
suite. Raku++ measures itself against it:

> **Any compiler that can run Roast can be officially called a Raku compiler.**

Every figure below is defined in [COUNTING.md](COUNTING.md), which wins if the
two ever disagree.

## Current standing

**Headline: 100.00% of Roast's tests pass** — 218,420 of 218,420 with the tests Roast marks skip or todo left out, and all declared tests (220,055 / 220,055) with those counted as passes, as TAP counts them. **Every one of the 1,424 files passes.**

Measured 2026-10-02 on the v5.2.0 release build, against Roast `1f749e338`
(2026-09-29) and the 1,424 files its `spectest.data` lists: three runs, all
three 1,424 / 1,424, with v5.1.0's file list. `S17-promise/nonblocking-await.t`
still crashes now and then (see [the note below](#the-one-file-that-flaps));
it did not in these three runs. On a loaded
machine, `S17-channel/stress.t` — marked `stress slow` in `spectest.data`, 7–12
s alone against the 10-second ceiling — can also time out with its tests
passing.

Full suite — **1,424 files** (Roast's `spectest.data`):

| Files | Count | Share of suite |
|---|---:|---:|
| **Fully passing** | **1,424** | **100.00%** |
| Partially passing | 0 | 0% |
| No TAP output | 0 | 0% |
| Timeouts | 0 | 0% |

**What changed since v5.0.0 is Roast, not the engine.** v5.0.0's one failing
test was test 3 of `S16-io/eof.t`, `.eof on TTY STDIN works right`: it runs the
child under `script(1)`, macOS `script` breaks it, and Roast marked it todo on
macOS by release *name*, a list that did not include macOS 27. Roast
`1f749e338` ("Fudge tests for MacOS generally") marks it todo on every macOS,
and gives `S32-io/out-buffering.t` a todo on macOS as well, which is why the
skip/todo-free count is one test smaller than v5.0.0's. Against v5.0.0's Roast,
`1f521d798`, v5.1.0 gives v5.0.0's figures exactly — 1,423 / 1,424 files,
218,421 / 218,422 — with the same file list, in three runs of three.

### The one file that flaps

`S17-promise/nonblocking-await.t` crashes with SIGSEGV now and then, part-way
through its "Deep Promise tree" subtest: once in three full runs here, and once
in 40 runs of the file alone. It is not new: 150 runs each, interleaved, gave 3
crashes on v5.1.0 and 2 on the v5.0.1 release binary. Every run that finishes
passes all 28 tests. The
cause is not yet found; `t/race/evalcall.raku` reproduces a crash of the same
kind (6 in 1,000 runs) in a smaller program.

### How a file is classified

Each `.t` file emits [TAP](https://testanything.org/). The harness runs every
file with a per-file ceiling (10 s for Raku++, longer for the files that sleep
by specification) and buckets it:

| Class | Meaning |
|---|---|
| **fully passing** | every planned assertion passed, or the file legitimately `plan skip-all`s |
| **partial** | the file ran and produced TAP, but some assertions failed |
| **no TAP** | the file produced no plan and no assertions — usually a parse error |
| **timeout** | killed at the ceiling; what it printed counts, the rest of its plan counts against it |

## By synopsis

Files fully passing, and tests passing: of every declared test, and with skip
and todo left out, from the by-synopsis table the harness prints.

| Section | Theme | Files | All declared | Without skip/todo |
|---|---|---:|---:|---:|
| S01 | Overview | 14 / 14 | 89 / 89 | 2 / 2 |
| S02 | Literals, types, magicals | 146 / 146 | 10,978 / 10,978 | 10,593 / 10,593 |
| S03 | Operators | 125 / 125 | 26,333 / 26,333 | 26,215 / 26,215 |
| S04 | Blocks, statements, phasers | 75 / 75 | 1,710 / 1,710 | 1,626 / 1,626 |
| S05 | Regexes & grammars | 85 / 85 | 5,628 / 5,628 | 5,520 / 5,520 |
| S06 | Subroutines & signatures | 90 / 90 | 2,361 / 2,361 | 2,269 / 2,269 |
| S07 | Iterators | 6 / 6 | 268 / 268 | 263 / 263 |
| S09 | Data structures | 22 / 22 | 4,817 / 4,817 | 4,713 / 4,713 |
| S10 | Packages | 8 / 8 | 191 / 191 | 190 / 190 |
| S11 | Modules | 21 / 21 | 320 / 320 | 306 / 306 |
| S12 | Objects & classes | 97 / 97 | 1,989 / 1,989 | 1,843 / 1,843 |
| S13 | Overloading | 5 / 5 | 81 / 81 | 76 / 76 |
| S14 | Roles | 24 / 24 | 413 / 413 | 407 / 407 |
| S15 | Unicode / strings / NFG | 81 / 81 | 91,807 / 91,807 | 91,738 / 91,738 |
| S16 | I/O | 37 / 37 | 766 / 766 | 747 / 747 |
| S17 | Concurrency (supply/promise/async) | 95 / 95 | 1,567 / 1,567 | 1,538 / 1,538 |
| S19 | Command-line | 7 / 7 | 24 / 24 | 18 / 18 |
| S22 | Package format | 1 / 1 | 19 / 19 | 19 / 19 |
| S24 | Testing | 16 / 16 | 116 / 116 | 86 / 86 |
| S26 | Documentation (POD) | 27 / 27 | 832 / 832 | 771 / 771 |
| S28 | Special variables | 3 / 3 | 8 / 8 | 8 / 8 |
| S29 | Builtins & context | 14 / 14 | 465 / 465 | 462 / 462 |
| S32 | Standard types (str/list/num/…) | 261 / 261 | 46,455 / 46,455 | 46,292 / 46,292 |
| integration | Cross-feature programs | 119 / 119 | 1,401 / 1,401 | 1,382 / 1,382 |
| 6.c | v6.c language snapshot | 18 / 18 | 1,041 / 1,041 | 964 / 964 |
| 6.d | v6.d language snapshot | 18 / 18 | 20,310 / 20,310 | 20,310 / 20,310 |
| APPENDICES | — | 6 / 6 | 57 / 57 | 53 / 53 |
| MISC / t | — | 3 / 3 | 9 / 9 | 9 / 9 |
| Total | — | 1,424 / 1,424 | 220,055 / 220,055 | 218,420 / 218,420 |

S15 (Unicode, grapheme and normalization tables) holds 91,807 of the 220,055
declared tests, and the `sprintf` conversion files hold most of 6.d's 20,310,
so the whole-suite percentage is dominated by those two; every other row is
now at 100% as well.

## Where this stands among implementations

The harness scores whatever binary runs it, so other engines can be measured on
the same bar (see [COUNTING.md](COUNTING.md#measuring-another-engine)).

**Rakudo** 2026.08, run through the same harness on the same machine against the
same 1,424 files at Roast `1f521d798` — with Roast's own `fudge` applied, as Rakudo's spectest does —
passes **1,414** files and 218,933 of 219,096 tests without skip/todo (99.93%).
Raku++ passes all ten files Rakudo does not, six of them S05 regex tests Roast
gained after Rakudo 2026.08 was released; Rakudo passes one that Raku++ does not,
`S16-io/eof.t`. Its list is
[roast-lists/rakudo-2026.08-1f521d798.list](roast-lists/rakudo-2026.08-1f521d798.list).

**[mutsu](https://github.com/tokuhirom/mutsu)**, the Rust implementation, scored
1,428 of 1,464 files and 219,526 of 220,916 declared tests (99.4%) under this
harness on 2026-09-21, on the whole checkout of the earlier Roast `b2cbe8a42`
with its own fudging switched on (`MUTSU_FUDGE=1`) and a 60-second ceiling.

How the three implementations are built, and why their coverage and speed
differ, is in [faq/implementations.md](../guide/faq/implementations.md).

## Reproducing these numbers

```sh
ROAST=~/roast build/rakupp tools/run-roast.raku           # the files spectest.data lists
ROAST=~/roast build/rakupp tools/run-roast.raku --all     # every .t in the checkout
ROAST=~/roast build/rakupp tools/run-roast.raku S05       # a path substring
ROAST=~/roast build/rakupp tools/run-roast.raku --list=pass.list --failed
```

`--list=FILE` writes the fully-passing paths (the release gate diffs these, see
[roast-lists/](roast-lists/)); `--failed` prints every file that did not fully
pass, with the source lines of its failing tests. `-j=N` sets how many cores it
uses; the default is all of them, and the full list takes about 40 seconds on
the 8-core machine of record, most of it `S17-supply/batch.t` and
`S32-io/lock.t` waiting by specification.

The run ends with the summary: the file buckets, the assertion ratios, a table
of Roast's skip and todo directives by source, and the by-synopsis table above.

The dated per-sitting notes that used to follow this section are in
[findings/ROAST-SNAPSHOTS.md](../dev/findings/ROAST-SNAPSHOTS.md); what each
release measured is in the [CHANGELOG](../../CHANGELOG.md).
