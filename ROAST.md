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
| **Fully passing** | **185** | **13%** |
| Partially passing | 508 | 35% |
| No TAP output | 752 | 51% |
| Timeouts | 19 | 1% |

**Coverage ≈ 13% of files.** That is the number to quote. Over half the suite
produces no TAP at all — those files hit a parse error or an unimplemented
construct and abort before any assertion runs — so they are entirely unmeasured
territory, not "passing" and not "failing."

### The assertion count

Among the files that *do* run, **96,059 of 111,360** assertions pass. This
number measures correctness on the attempted subset — how much of what we run is
right — and is the signal we watch for regressions. Two facts define its scope:

1. **Its denominator is only the reached assertions.** The 752 no-TAP files emit
   nothing, so they are not in the 111,360. This is a different denominator than
   the coverage figure (files, over 1,464).
2. **S15 (Unicode) is ~88k of the 111k**, passing at ~95%, so it dominates the
   blended figure.

Coverage is the 13% of files; correctness-on-what-runs is this 96,059/111,360.
They are two different measurements, quoted for two different purposes.

## By synopsis

Roast is organized by Synopsis (`SNN-*`), plus integration tests and
language-version snapshots (`6.c`, `6.d`). Assertion % is over assertions that
actually **ran** (no-TAP files contribute none), so a section can show a high %
while many of its files still don't run at all — read it alongside No-TAP.

| Section | Theme | Full | Part | Time | No-TAP | Assertions | % |
|---|---|---:|---:|---:|---:|---:|---:|
| S01 | Overview | 14 | 0 | 0 | 0 | 89/89 | 100% |
| S02 | Literals, types, magicals | 13 | 53 | 0 | 81 | 684/1048 | 65% |
| S03 | Operators | 11 | 39 | 1 | 74 | 594/980 | 61% |
| S04 | Blocks, statements, phasers | 12 | 33 | 0 | 32 | 242/342 | 71% |
| S05 | Regexes & grammars | 13 | 53 | 0 | 32 | 2565/4165 | 62% |
| S06 | Subroutines & signatures | 5 | 33 | 0 | 56 | 182/435 | 42% |
| S07 | Iterators | 0 | 0 | 0 | 6 | 0/0 | — |
| S09 | Data structures | 0 | 5 | 0 | 17 | 37/87 | 43% |
| S10 | Packages | 1 | 2 | 0 | 6 | 12/14 | 86% |
| S11 | Modules | 5 | 7 | 0 | 10 | 40/65 | 62% |
| S12 | Objects & classes | 6 | 38 | 0 | 57 | 177/285 | 62% |
| S13 | Overloading | 0 | 2 | 0 | 5 | 3/7 | 43% |
| S14 | Roles | 4 | 9 | 0 | 12 | 47/65 | 72% |
| S15 | Unicode / strings / NFG | 40 | 29 | 0 | 12 | 83395/88219 | 95% |
| S16 | I/O | 1 | 9 | 0 | 27 | 24/79 | 30% |
| S17 | Concurrency (supply/promise/async) | 2 | 41 | 0 | 56 | 59/67 | 88% |
| S19 | Command-line | 0 | 0 | 0 | 8 | 0/0 | — |
| S22 | Package format | 0 | 0 | 0 | 1 | 0/0 | — |
| S24 | Testing | 5 | 2 | 0 | 10 | 10/40 | 25% |
| S26 | Documentation (POD) | 1 | 9 | 0 | 17 | 6/91 | 7% |
| S28 | Special variables | 0 | 2 | 0 | 1 | 1/6 | 17% |
| S29 | Builtins & context | 2 | 6 | 0 | 6 | 271/274 | 99% |
| S32 | Standard types (str/list/num/…) | 27 | 85 | 8 | 143 | 7226/7868 | 92% |
| integration | Cross-feature programs | 16 | 40 | 0 | 63 | 224/354 | 63% |
| 6.c | v6.c language snapshot | 1 | 4 | 0 | 13 | 44/74 | 59% |
| 6.d | v6.d language snapshot | 4 | 5 | 8 | 1 | 114/6687 | 2% |
| APPENDICES | — | 1 | 1 | 2 | 2 | 12/17 | 71% |
| MISC / t | — | 1 | 1 | 0 | 4 | 1/2 | 50% |

### Reading the table

- **S15 (Unicode)** dominates the assertion count — ~88k of 111k assertions
  live here (grapheme-break and normalization tables are enormous). Raku++'s
  generated UCD 15.1 tables clear **95%** of it, which is why the overall
  assertion rate is high.
- **S01** is fully green: those files skip-all unless a Perl-5 interop bridge
  exists, and Raku++ handles the skip path spec-correctly.
- **S32** (standard types) and **S03/S05** (operators, regexes) are the biggest
  pools of *reachable* work — high partial counts mean the files run but trip a
  long tail of individual assertions.
- High **No-TAP** counts (S06, S12, S16, S17) mark constructs that abort before
  any assertion runs — the frontier where a single parser/feature gap unlocks a
  whole cluster of files.
- The **6.d** snapshot has 8 timeouts (heavy generated tests) which drag its
  assertion % down; it is not representative of feature coverage.

## Reproducing these numbers

```sh
build/rakupp tools/run-roast.raku          # self-hosted harness (Raku, run by rakupp)
```

It streams a per-file line (`[PASS] n/m path`, `[part]`, `[TIME]`) and ends
with the summary. Filter by path substring: `build/rakupp tools/run-roast.raku S05`.

_Snapshot: 185 / 1,464 files fully passing (~13% coverage); 508 partial,
752 no-TAP, 19 timeout. Reached-assertion pass rate 96,059 / 111,360 (see
caveat above — not a coverage figure)._
