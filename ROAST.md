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
| **Fully passing** | **252** | **17%** |
| Partially passing | 566 | 39% |
| No TAP output | 638 | 44% |
| Timeouts | 9 | 0.6% |

**Coverage ≈ 17% of files.** That is the number to quote. Nearly half the suite
produces no TAP at all — those files hit a parse error or an unimplemented
construct and abort before any assertion runs — so they are entirely unmeasured
territory, not "passing" and not "failing."

### The assertion count

Among the files that *do* run, **119,645 of 164,029** assertions pass. This
number measures correctness on the attempted subset — how much of what we run is
right — and is the signal we watch for regressions. Two facts define its scope:

1. **Its denominator is only the reached assertions.** The 643 no-TAP files emit
   nothing, so they are not in the 164,029. This is a different denominator than
   the coverage figure (files, over 1,464).
2. **S15 (Unicode) is ~88k of the 112k**, passing at ~95%, so it dominates the
   blended figure.

Coverage is the 17% of files; correctness-on-what-runs is this 119,645/164,029.
They are two different measurements, quoted for two different purposes.

## By synopsis

Roast is organized by Synopsis (`SNN-*`), plus integration tests and
language-version snapshots (`6.c`, `6.d`). Assertion % is over assertions that
actually **ran** (no-TAP files contribute none), so a section can show a high %
while many of its files still don't run at all — read it alongside No-TAP.

| Section | Theme | Full | Part | Time | No-TAP | Assertions | % |
|---|---|---:|---:|---:|---:|---:|---:|
| S01 | Overview | 14 | 0 | 0 | 0 | 89/89 | 100% |
| S02 | Literals, types, magicals | 18 | 49 | 0 | 80 | 729/999 | 73% |
| S03 | Operators | 13 | 37 | 1 | 74 | 634/1018 | 62% |
| S04 | Blocks, statements, phasers | 13 | 35 | 0 | 29 | 287/425 | 68% |
| S05 | Regexes & grammars | 14 | 56 | 0 | 28 | 2595/4200 | 62% |
| S06 | Subroutines & signatures | 6 | 35 | 0 | 53 | 225/509 | 44% |
| S07 | Iterators | 0 | 1 | 0 | 5 | 8/8 | 100% |
| S09 | Data structures | 0 | 6 | 0 | 16 | 49/115 | 43% |
| S10 | Packages | 1 | 2 | 0 | 6 | 12/14 | 86% |
| S11 | Modules | 6 | 7 | 0 | 9 | 41/60 | 68% |
| S12 | Objects & classes | 11 | 36 | 1 | 53 | 213/320 | 67% |
| S13 | Overloading | 0 | 3 | 0 | 4 | 4/10 | 40% |
| S14 | Roles | 4 | 10 | 0 | 11 | 66/92 | 72% |
| S15 | Unicode / strings / NFG | 40 | 29 | 0 | 12 | 83698/88219 | 95% |
| S16 | I/O | 2 | 13 | 0 | 22 | 42/136 | 31% |
| S17 | Concurrency (supply/promise/async) | 10 | 57 | 1 | 31 | 264/505 | 52% |
| S19 | Command-line | 0 | 6 | 0 | 2 | 1/18 | 6% |
| S22 | Package format | 0 | 0 | 0 | 1 | 0/0 | — |
| S24 | Testing | 6 | 7 | 0 | 4 | 24/74 | 32% |
| S26 | Documentation (POD) | 1 | 12 | 0 | 14 | 6/100 | 6% |
| S28 | Special variables | 0 | 2 | 0 | 1 | 1/6 | 17% |
| S29 | Builtins & context | 2 | 7 | 0 | 5 | 265/279 | 95% |
| S32 | Standard types (str/list/num/…) | 30 | 89 | 8 | 136 | 7418/8095 | 92% |
| integration | Cross-feature programs | 21 | 48 | 0 | 50 | 309/438 | 71% |
| 6.c | v6.c language snapshot | 1 | 4 | 0 | 13 | 44/74 | 59% |
| 6.d | v6.d language snapshot | 4 | 5 | 8 | 1 | 116/6702 | 2% |
| APPENDICES | — | 1 | 2 | 2 | 1 | 24/29 | 83% |
| MISC / t | — | 1 | 2 | 0 | 3 | 4/12 | 33% |

### Reading the table

- **S15 (Unicode)** dominates the assertion count — ~88k of 112k assertions
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

_Snapshot: 252 / 1,464 files fully passing (~17% coverage); 568 partial,
638 no-TAP, 9 timeout. Reached-assertion pass rate 119,873 / 164,321 (see
caveat above — not a coverage figure)._
