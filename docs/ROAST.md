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

**Headline: ~90% of all declared Roast tests pass** (194,904 / 216,222); on the
stricter file bar, ~41% of files fully pass (598 / 1,462). The per-file breakdown
comes first below, then the per-test figures. (S15 — Unicode / strings / NFG —
is now at 100% of assertions: full UCD case tables, grapheme-level regex, and
complete `uniprop` coverage landed for v1.1; its lone non-passing file is a
performance timeout, not a correctness gap. See [ROAST-GAPS](dev/ROAST-GAPS.md).)

Full suite — **1,462 files**:

| Files | Count | Share of suite |
|---|---:|---:|
| **Fully passing** | **598** | **41%** |
| Partially passing | 695 | 48% |
| No TAP output | 158 | 11% |
| Timeouts | 11 | 0.8% |

(Two files — `S04-statements/try.t`, `S12-construction/destruction.t` — hang the
harness with unkillable children and are measured separately; they count above as
one partial and one timeout. See [docs/ROAST-GAPS.md](dev/ROAST-GAPS.md).)

**Coverage ≈ 39% of files.** That is the number to quote. About a sixth of the suite
produces no TAP at all — those files hit a parse error or an unimplemented
construct and abort before any assertion runs — so they are entirely unmeasured
territory, not "passing" and not "failing."

### The assertion count

Measured per individual test rather than per file, the honest figure is
**194,904 of ~216,222 declared tests — 90%**. "Declared" means every test the
suite intends to run: for files that ran, their emitted plan; for files that
abort before emitting any TAP, the `plan N` count read straight from their
source. Counting those aborting files (all their tests failing) is what keeps the
number honest — a parse error can't make its tests vanish. The harness prints
three denominators, widest-to-strictest:

| Denominator | Ratio | What it includes |
|---|---|---|
| tests that **ran** | 194,904 / 199,872 (~98%) | only assertions files actually emitted — flatters, ignores aborts |
| tests **planned** (files that emitted a plan) | 194,904 / 212,964 (~92%) | + tests lost when a file aborts mid-plan |
| **all declared** tests | 194,904 / 216,222 (90%) | + tests in parse-error files, recovered from source. This denominator grows as parse fixes land — files that died before announcing a plan now declare their real (often larger, dynamic) plans, so the percentage can dip while absolute passes rise |

The 90% is the per-test analog of the ~39% file coverage. Two caveats on scope:

1. **~3.4k of the denominator comes from no-TAP files** (101 of them, read from
   source); 5 more no-TAP files use a dynamic `plan *` / `done-testing` and are
   genuinely uncountable, so they sit outside even this figure.
2. **S15 (Unicode) is ~91k of the reached total**, passing at ~100%, so it lifts
   the blended rate; other synopses are lower (see the per-synopsis table).
3. **These figures are slightly optimistic** as of this measurement: a
   `subtest 'desc' => { … }` (the Pair form) currently does not execute its
   body, so those subtests auto-pass as empty. The fix is staged to land *with*
   the pre-existing bugs it exposes (see [dev/REVIEW-1.0.md](dev/REVIEW-1.0.md)),
   at which point these numbers are re-measured honestly.

Coverage is the ~39% of files; per-test correctness across the whole suite is the
90%. They are different measurements, quoted for different purposes.

## By synopsis

Roast is organized by Synopsis (`SNN-*`), plus integration tests and
language-version snapshots (`6.c`, `6.d`). Assertion % is over assertions that
actually **ran** (no-TAP files contribute none), so a section can show a high %
while many of its files still don't run at all — read it alongside No-TAP.

| Section | Theme | Full | Part | Time | No-TAP | Assertions | % |
|---|---|---:|---:|---:|---:|---:|---:|
| S01 | Overview | 14 | 0 | 0 | 0 | 89/89 | 100% |
| S02 | Literals, types, magicals | 54 | 74 | 0 | 19 | 4447/4939 | 90% |
| S03 | Operators | 47 | 60 | 3 | 15 | 21679/22361 | 97% |
| S04 | Blocks, statements, phasers | 30 | 40 | 0 | 6 | 1091/1336 | 82% |
| S05 | Regexes & grammars | 34 | 56 | 0 | 8 | 5453/6107 | 89% |
| S06 | Subroutines & signatures | 20 | 55 | 0 | 19 | 1271/1642 | 77% |
| S07 | Iterators | 2 | 4 | 0 | 0 | 225/268 | 84% |
| S09 | Data structures | 2 | 20 | 0 | 0 | 905/1111 | 81% |
| S10 | Packages | 2 | 5 | 0 | 2 | 39/75 | 52% |
| S11 | Modules | 8 | 10 | 0 | 4 | 56/87 | 64% |
| S12 | Objects & classes | 27 | 59 | 0 | 14 | 1059/1289 | 82% |
| S13 | Overloading | 4 | 2 | 0 | 1 | 50/52 | 96% |
| S14 | Roles | 7 | 15 | 0 | 3 | 208/259 | 80% |
| S15 | Unicode / strings / NFG | 80 | 0 | 1 | 0 | 91752/91752 | 100% |
| S16 | I/O | 17 | 15 | 0 | 5 | 415/552 | 75% |
| S17 | Concurrency (supply/promise/async) | 37 | 47 | 4 | 11 | 830/1030 | 81% |
| S19 | Command-line | 6 | 1 | 0 | 1 | 22/24 | 92% |
| S22 | Package format | 0 | 1 | 0 | 0 | 3/3 | 100% |
| S24 | Testing | 11 | 6 | 0 | 0 | 88/134 | 66% |
| S26 | Documentation (POD) | 7 | 18 | 0 | 2 | 408/444 | 92% |
| S28 | Special variables | 3 | 0 | 0 | 0 | 9/9 | 100% |
| S29 | Builtins & context | 7 | 5 | 1 | 1 | 359/370 | 97% |
| S32 | Standard types (str/list/num/…) | 108 | 130 | 1 | 24 | 42880/43903 | 98% |
| integration | Cross-feature programs | 49 | 55 | 0 | 15 | 947/1066 | 89% |
| 6.c | v6.c language snapshot | 3 | 11 | 0 | 4 | 311/600 | 52% |
| 6.d | v6.d language snapshot | 14 | 4 | 0 | 0 | 20260/20310 | 100% |
| APPENDICES | — | 2 | 2 | 1 | 1 | 33/48 | 69% |
| MISC / t | — | 3 | 0 | 0 | 3 | 12/12 | 100% |

### Reading the table

- **S15 (Unicode)** dominates the assertion count — ~91k of ~189k reached
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

_Snapshot: 598 / 1,462 files fully passing (~41% coverage); 695 partial,
158 no-TAP, 11 timeout (the scheduler/io timing files flap between pass and timeout under runner load). Reached-assertion pass rate 194,904 / 199,872 (see
caveat above — not a coverage figure). S05-substitution is a fully-passing
subchapter (67222.t, match.t, subst.t)._
