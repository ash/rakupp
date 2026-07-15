# Raku++ against Roast

[Roast](https://github.com/Raku/roast) is the official Raku specification test
suite — the executable definition of what "being Raku" means. Raku++ treats it
as the north star:

> **Any compiler that can run Roast can be officially called a Raku compiler.**

Every feature in Raku++ is driven by a failing Roast test, and progress is
measured here — not in lines of code.

## How files are classified

Each `.t` file emits [TAP](https://testanything.org/) (`ok`/`not ok` lines
under a `1..N` plan). The harness runs every file with a 10-second timeout and
buckets it:

| Class | Meaning |
|---|---|
| **fully-pass** | every planned assertion passed (or the file legitimately `# SKIP`s all) |
| **partial** | the file ran and produced TAP, but some assertions failed |
| **no-TAP** | the file produced no plan/assertions — usually a parse error or an unimplemented construct that aborts before any test runs |
| **timeout** | did not finish within 10s |

Two things are worth reading together: **files fully passing** (a strict,
all-or-nothing bar) and **assertions passing** (partial credit — a better
gauge of how much of the language actually works).

## Current standing

The exact definition of every figure below — and how the harness computes it — is
in [COUNTING.md](COUNTING.md); that file is authoritative if anything here drifts.

**Headline: ~79% of all declared Roast tests pass** (162,807 / 205,004); on the
stricter file bar, ~30% of files fully pass (441 / 1,462). The per-file breakdown
comes first below, then the per-test figures.

Full suite — **1,464 files**:

| Files | Count | Share of suite |
|---|---:|---:|
| **Fully passing** | **441** | **30%** |
| Partially passing | 645 | 44% |
| No TAP output | 376 | 26% |
| Timeouts | 8 | 0.5% |

(Two files — `S04-statements/try.t`, `S12-construction/destruction.t` — hang the
harness with unkillable children and are measured separately; they count above as
one partial and one timeout. See [docs/ROAST-GAPS.md](dev/ROAST-GAPS.md).)

**Coverage ≈ 29% of files.** That is the number to quote. Just under a third of the suite
produces no TAP at all — those files hit a parse error or an unimplemented
construct and abort before any assertion runs — so they are entirely unmeasured
territory, not "passing" and not "failing."

### The assertion count

Measured per individual test rather than per file, the honest figure is
**162,807 of ~205,004 declared tests — ~79%**. "Declared" means every test the
suite intends to run: for files that ran, their emitted plan; for files that
abort before emitting any TAP, the `plan N` count read straight from their
source. Counting those aborting files (all their tests failing) is what keeps the
number honest — a parse error can't make its tests vanish. The harness prints
three denominators, widest-to-strictest:

| Denominator | Ratio | What it includes |
|---|---|---|
| tests that **ran** | 162,807 / 170,460 (~96%) | only assertions files actually emitted — flatters, ignores aborts |
| tests **planned** (files that emitted a plan) | 162,807 / 194,391 (~84%) | + tests lost when a file aborts mid-plan |
| **all declared** tests | 162,807 / 205,004 (~79%) | + tests in parse-error files, recovered from source. This denominator grows as parse fixes land — files that died before announcing a plan now declare their real (often larger, dynamic) plans, so the percentage can dip while absolute passes rise |

The ~82% is the per-test analog of the ~29% file coverage. Two caveats on scope:

1. **~15k of the denominator comes from no-TAP files** (261 of them, read from
   source); 13 more no-TAP files use a dynamic `plan *` / `done-testing` and are
   genuinely uncountable, so they sit outside even this figure.
2. **S15 (Unicode) is ~91k of the reached total**, passing at ~100%, so it lifts
   the blended rate; other synopses are lower (see the per-synopsis table).
3. **These figures are slightly optimistic** as of this measurement: a
   `subtest 'desc' => { … }` (the Pair form) currently does not execute its
   body, so those subtests auto-pass as empty. The fix is staged to land *with*
   the pre-existing bugs it exposes (see [dev/REVIEW-1.0.md](dev/REVIEW-1.0.md)),
   at which point these numbers are re-measured honestly.

Coverage is the ~29% of files; per-test correctness across the whole suite is the
~82%. They are different measurements, quoted for different purposes.

## By synopsis

Roast is organized by Synopsis (`SNN-*`), plus integration tests and
language-version snapshots (`6.c`, `6.d`). Assertion % is over assertions that
actually **ran** (no-TAP files contribute none), so a section can show a high %
while many of its files still don't run at all — read it alongside No-TAP.

| Section | Theme | Full | Part | Time | No-TAP | Assertions | % |
|---|---|---:|---:|---:|---:|---:|---:|
| S01 | Overview | 14 | 0 | 0 | 0 | 89/89 | 100% |
| S02 | Literals, types, magicals | 34 | 59 | 0 | 54 | 1868/2380 | 78% |
| S03 | Operators | 27 | 43 | 1 | 54 | 2090/3291 | 64% |
| S04 | Blocks, statements, phasers | 22 | 32 | 0 | 22 | 525/653 | 80% |
| S05 | Regexes & grammars | 25 | 62 | 0 | 11 | 4477/5212 | 86% |
| S06 | Subroutines & signatures | 10 | 50 | 0 | 34 | 559/895 | 62% |
| S07 | Iterators | 1 | 3 | 0 | 2 | 46/54 | 85% |
| S09 | Data structures | 0 | 13 | 0 | 9 | 145/304 | 48% |
| S10 | Packages | 2 | 5 | 0 | 2 | 23/50 | 46% |
| S11 | Modules | 8 | 9 | 0 | 5 | 54/86 | 63% |
| S12 | Objects & classes | 20 | 57 | 0 | 23 | 602/812 | 74% |
| S13 | Overloading | 4 | 0 | 0 | 3 | 25/25 | 100% |
| S14 | Roles | 5 | 12 | 0 | 8 | 126/172 | 73% |
| S15 | Unicode / strings / NFG | 69 | 8 | 0 | 4 | 91206/91248 | 100% |
| S16 | I/O | 14 | 14 | 0 | 9 | 257/373 | 69% |
| S17 | Concurrency (supply/promise/async) | 23 | 49 | 4 | 23 | 493/790 | 62% |
| S19 | Command-line | 6 | 1 | 0 | 1 | 22/24 | 92% |
| S22 | Package format | 0 | 1 | 0 | 0 | 3/3 | 100% |
| S24 | Testing | 10 | 6 | 0 | 1 | 67/111 | 60% |
| S26 | Documentation (POD) | 6 | 9 | 0 | 12 | 164/192 | 85% |
| S28 | Special variables | 2 | 0 | 0 | 1 | 6/6 | 100% |
| S29 | Builtins & context | 4 | 7 | 1 | 2 | 337/359 | 94% |
| S32 | Standard types (str/list/num/…) | 60 | 136 | 1 | 66 | 33125/34825 | 95% |
| integration | Cross-feature programs | 33 | 56 | 1 | 29 | 625/779 | 80% |
| 6.c | v6.c language snapshot | 2 | 6 | 0 | 10 | 56/88 | 64% |
| 6.d | v6.d language snapshot | 14 | 4 | 0 | 0 | 20259/20310 | 100% |
| APPENDICES | — | 1 | 3 | 1 | 1 | 32/42 | 76% |
| MISC / t | — | 3 | 0 | 0 | 3 | 12/12 | 100% |

### Reading the table

- **S15 (Unicode)** dominates the assertion count — ~91k of ~163k reached
  assertions live here (grapheme-break and normalization tables are enormous). Raku++'s
  generated UCD 17.0 tables clear **~100%** of it, which is why the overall
  assertion rate is high.
- **S01** is fully green: those files skip-all unless a Perl-5 interop bridge
  exists, and Raku++ handles the skip path spec-correctly.
- **S32** (standard types), **S05** (regexes) and **S17** (concurrency) are the
  biggest pools of *reachable* work — high partial counts mean the files run but
  trip a long tail of individual assertions.
- High **No-TAP** counts (S02, S03, S06, S32, S12) mark constructs that abort
  before any assertion runs — the frontier where a single parser/feature gap
  unlocks a whole cluster of files.
- The **6.d** snapshot's assertion total (~20k) is dominated by the sprintf
  format-conversion files (`sprintf-{b,c,d,e,f,o,s,u,x}.t`), now largely passing.

## Reproducing these numbers

```sh
build/rakupp tools/run-roast.raku          # self-hosted harness (Raku, run by rakupp)
```

It runs the full ~1,460-file suite in about **3½ minutes** — the millisecond
cold-start means spawning a fresh process per file is cheap, so the whole run
is quick enough to re-do after any change. It streams a per-file line
(`[PASS] n/m path`, `[part]`, `[TIME]`) and ends with the summary **plus a
paste-ready copy of the by-synopsis table above** — so refreshing that table
is a copy-paste, not a hand computation. Filter by path
substring: `build/rakupp tools/run-roast.raku S05`.

`--workers=N` runs N test files at a time (`… tools/run-roast.raku
--workers=8`): each file runs from a `start` worker and the interpreter parks
the GIL while a worker waits on its child process, so the children genuinely
overlap. Output and totals are identical to a sequential run — results are
tallied and printed in file order regardless of N.

_Snapshot: 441 / 1,462 files fully passing (~30% coverage); 686 partial,
329 no-TAP, 6 timeout. Reached-assertion pass rate 162,807 / 170,460 (see
caveat above — not a coverage figure). S05-substitution is a fully-passing
subchapter (67222.t, match.t, subst.t). The +19-file jump came from honoring
roast's `#?rakudo skip` fudge directives (see [docs/ROAST-GAPS.md](dev/ROAST-GAPS.md))._
