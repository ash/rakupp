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

Full suite — **1,464 files**:

| Files | Count | Share of suite |
|---|---:|---:|
| **Fully passing** | **278** | **19%** |
| Partially passing | 611 | 42% |
| No TAP output | 576 | 39% |
| Timeouts | 2 | 0.1% |

**Coverage ≈ 19% of files.** That is the number to quote. Over a third of the suite
produces no TAP at all — those files hit a parse error or an unimplemented
construct and abort before any assertion runs — so they are entirely unmeasured
territory, not "passing" and not "failing."

### The assertion count

Among the files that *do* run, **130,967 of 188,393** assertions pass. This
number measures correctness on the attempted subset — how much of what we run is
right — and is the signal we watch for regressions. Two facts define its scope:

1. **Its denominator is only the reached assertions.** The 568 no-TAP files emit
   nothing, so they are not in the 188,393. This is a different denominator than
   the coverage figure (files, over 1,464).
2. **S15 (Unicode) is ~87k of the total**, passing at ~95%, so it dominates the
   blended figure.

Coverage is the 19% of files; correctness-on-what-runs is this 130,967/188,393.
They are two different measurements, quoted for two different purposes.

## By synopsis

Roast is organized by Synopsis (`SNN-*`), plus integration tests and
language-version snapshots (`6.c`, `6.d`). Assertion % is over assertions that
actually **ran** (no-TAP files contribute none), so a section can show a high %
while many of its files still don't run at all — read it alongside No-TAP.

| Section | Theme | Full | Part | Time | No-TAP | Assertions | % |
|---|---|---:|---:|---:|---:|---:|---:|
| S01 | Overview | 14 | 0 | 0 | 0 | 89/89 | 100% |
| S02 | Literals, types, magicals | 25 | 56 | 0 | 66 | 949/1785 | 53% |
| S03 | Operators | 15 | 35 | 0 | 75 | 764/1582 | 48% |
| S04 | Blocks, statements, phasers | 15 | 37 | 0 | 25 | 353/481 | 73% |
| S05 | Regexes & grammars | 16 | 68 | 0 | 14 | 3117/4955 | 63% |
| S06 | Subroutines & signatures | 7 | 40 | 0 | 47 | 294/547 | 54% |
| S07 | Iterators | 0 | 1 | 0 | 5 | 8/8 | 100% |
| S09 | Data structures | 0 | 7 | 0 | 15 | 52/131 | 40% |
| S10 | Packages | 2 | 2 | 0 | 5 | 15/35 | 43% |
| S11 | Modules | 8 | 6 | 0 | 8 | 54/72 | 75% |
| S12 | Objects & classes | 12 | 44 | 1 | 44 | 290/418 | 69% |
| S13 | Overloading | 3 | 0 | 0 | 4 | 20/20 | 100% |
| S14 | Roles | 4 | 11 | 0 | 10 | 67/93 | 72% |
| S15 | Unicode / strings / NFG | 43 | 31 | 0 | 7 | 86700/91221 | 95% |
| S16 | I/O | 4 | 16 | 0 | 17 | 138/264 | 52% |
| S17 | Concurrency (supply/promise/async) | 17 | 52 | 0 | 30 | 361/660 | 55% |
| S19 | Command-line | 6 | 1 | 0 | 1 | 21/24 | 88% |
| S22 | Package format | 0 | 0 | 0 | 1 | 0/0 | — |
| S24 | Testing | 8 | 5 | 0 | 4 | 60/100 | 60% |
| S26 | Documentation (POD) | 2 | 11 | 0 | 14 | 8/100 | 8% |
| S28 | Special variables | 0 | 2 | 0 | 1 | 1/6 | 17% |
| S29 | Builtins & context | 3 | 8 | 0 | 3 | 345/370 | 93% |
| S32 | Standard types (str/list/num/…) | 38 | 107 | 0 | 118 | 36560/37401 | 98% |
| integration | Cross-feature programs | 24 | 51 | 0 | 44 | 392/527 | 74% |
| 6.c | v6.c language snapshot | 1 | 4 | 0 | 13 | 47/76 | 62% |
| 6.d | v6.d language snapshot | 4 | 13 | 0 | 1 | 122/47202 | 0% |
| APPENDICES | — | 1 | 3 | 1 | 1 | 34/42 | 81% |
| MISC / t | — | 1 | 0 | 0 | 5 | 10/10 | 100% |

### Reading the table

- **S15 (Unicode)** dominates the assertion count — ~88k of 112k assertions
  live here (grapheme-break and normalization tables are enormous). Raku++'s
  generated UCD 15.1 tables clear **95%** of it, which is why the overall
  assertion rate is high.
- **S01** is fully green: those files skip-all unless a Perl-5 interop bridge
  exists, and Raku++ handles the skip path spec-correctly.
- **S32** (standard types), **S05** (regexes) and **S17** (concurrency) are the
  biggest pools of *reachable* work — high partial counts mean the files run but
  trip a long tail of individual assertions.
- High **No-TAP** counts (S02, S03, S06, S32, S12) mark constructs that abort
  before any assertion runs — the frontier where a single parser/feature gap
  unlocks a whole cluster of files.
- The **6.d** snapshot's assertion total (47k) is dominated by a few heavy
  generated files, so its % is not representative of feature coverage.

## Reproducing these numbers

```sh
build/rakupp tools/run-roast.raku          # self-hosted harness (Raku, run by rakupp)
```

It streams a per-file line (`[PASS] n/m path`, `[part]`, `[TIME]`) and ends
with the summary. Filter by path substring: `build/rakupp tools/run-roast.raku S05`.

_Snapshot: 278 / 1,464 files fully passing (~19% coverage); 616 partial,
568 no-TAP, 2 timeout. Reached-assertion pass rate 130,967 / 188,393 (see
caveat above — not a coverage figure). S05-substitution is a fully-passing
subchapter (67222.t, match.t, subst.t)._
