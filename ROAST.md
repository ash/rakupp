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

**Headline: ~73% of all declared Roast tests pass** (137,199 / 187,714); on the
stricter file bar, ~22% of files fully pass (350 / 1,464). The per-file breakdown
comes first below, then the per-test figures.

Full suite — **1,464 files**:

| Files | Count | Share of suite |
|---|---:|---:|
| **Fully passing** | **350** | **23%** |
| Partially passing | 605 | 41% |
| No TAP output | 498 | 34% |
| Timeouts | 11 | 0.8% |

**Coverage ≈ 20% of files.** That is the number to quote. Over a third of the suite
produces no TAP at all — those files hit a parse error or an unimplemented
construct and abort before any assertion runs — so they are entirely unmeasured
territory, not "passing" and not "failing."

### The assertion count

Measured per individual test rather than per file, the honest figure is
**137,199 of ~187,714 declared tests — ~73%**. "Declared" means every test the
suite intends to run: for files that ran, their emitted plan; for files that
abort before emitting any TAP, the `plan N` count read straight from their
source. Counting those aborting files (all their tests failing) is what keeps the
number honest — a parse error can't make its tests vanish. The harness prints
three denominators, widest-to-strictest:

| Denominator | Ratio | What it includes |
|---|---|---|
| tests that **ran** | 137,199 / 146,982 (~93%) | only assertions files actually emitted — flatters, ignores aborts |
| tests **planned** (files that emitted a plan) | 137,199 / 157,967 (~87%) | + tests lost when a file aborts mid-plan |
| **all declared** tests | 137,199 / 187,714 (~73%) | + tests in parse-error files, recovered from source |

The ~73% is the per-test analog of the ~20% file coverage. Two caveats on scope:

1. **~31k of the denominator comes from no-TAP files** (385 of them, read from
   source); 16 more no-TAP files use a dynamic `plan *` / `done-testing` and are
   genuinely uncountable, so they sit outside even this figure.
2. **S15 (Unicode) is ~87k of the reached total**, passing at ~95%, so it lifts
   the blended rate; other synopses are lower (see the per-synopsis table).

Coverage is the ~20% of files; per-test correctness across the whole suite is the
~73%. They are different measurements, quoted for different purposes.

## By synopsis

Roast is organized by Synopsis (`SNN-*`), plus integration tests and
language-version snapshots (`6.c`, `6.d`). Assertion % is over assertions that
actually **ran** (no-TAP files contribute none), so a section can show a high %
while many of its files still don't run at all — read it alongside No-TAP.

| Section | Theme | Full | Part | Time | No-TAP | Assertions | % |
|---|---|---:|---:|---:|---:|---:|---:|
| S01 | Overview | 14 | 0 | 0 | 0 | 89/89 | 100% |
| S02 | Literals, types, magicals | 25 | 56 | 0 | 66 | 949/1785 | 53% |
| S03 | Operators | 16 | 35 | 0 | 74 | 770/1586 | 48% |
| S04 | Blocks, statements, phasers | 17 | 36 | 0 | 25 | 396/541 | 73% |
| S05 | Regexes & grammars | 17 | 68 | 0 | 14 | 3119/4955 | 62% |
| S06 | Subroutines & signatures | 8 | 39 | 0 | 47 | 304/576 | 52% |
| S07 | Iterators | 1 | 1 | 0 | 4 | 42/42 | 100% |
| S09 | Data structures | 0 | 7 | 0 | 15 | 52/131 | 39% |
| S10 | Packages | 2 | 2 | 0 | 5 | 15/35 | 42% |
| S11 | Modules | 8 | 8 | 0 | 6 | 54/84 | 64% |
| S12 | Objects & classes | 14 | 49 | 0 | 38 | 378/553 | 68% |
| S13 | Overloading | 3 | 0 | 0 | 4 | 20/20 | 100% |
| S14 | Roles | 5 | 11 | 0 | 9 | 112/159 | 70% |
| S15 | Unicode / strings / NFG | 43 | 31 | 0 | 7 | 86701/91222 | 95% |
| S16 | I/O | 11 | 15 | 0 | 11 | 211/348 | 60% |
| S17 | Concurrency (supply/promise/async) | 22 | 47 | 0 | 30 | 420/680 | 62% |
| S19 | Command-line | 6 | 1 | 0 | 1 | 21/24 | 87% |
| S22 | Package format | 0 | 0 | 0 | 1 | 0/0 | — |
| S24 | Testing | 8 | 5 | 0 | 4 | 60/100 | 60% |
| S26 | Documentation (POD) | 6 | 10 | 0 | 11 | 164/193 | 85% |
| S28 | Special variables | 2 | 0 | 0 | 1 | 6/6 | 100% |
| S29 | Builtins & context | 3 | 8 | 0 | 3 | 345/370 | 93% |
| S32 | Standard types (str/list/num/…) | 45 | 112 | 0 | 118 | 20743/21665 | 96% |
| integration | Cross-feature programs | 27 | 50 | 0 | 42 | 411/545 | 75% |
| 6.c | v6.c language snapshot | 1 | 4 | 0 | 13 | 47/76 | 61% |
| 6.d | v6.d language snapshot | 10 | 7 | 0 | 1 | 19748/20279 | 97% |
| APPENDICES | — | 1 | 3 | 1 | 1 | 33/42 | 78% |
| MISC / t | — | 3 | 0 | 0 | 3 | 12/12 | 100% |

### Reading the table

- **S15 (Unicode)** dominates the assertion count — ~87k of ~147k reached
  assertions live here (grapheme-break and normalization tables are enormous). Raku++'s
  generated UCD 16.0 tables clear **95%** of it, which is why the overall
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

It streams a per-file line (`[PASS] n/m path`, `[part]`, `[TIME]`) and ends
with the summary. Filter by path substring: `build/rakupp tools/run-roast.raku S05`.

_Snapshot: 350 / 1,464 files fully passing (~23% coverage); 605 partial,
498 no-TAP, 11 timeout. Reached-assertion pass rate 137,199 / 146,982 (see
caveat above — not a coverage figure). S05-substitution is a fully-passing
subchapter (67222.t, match.t, subst.t)._
