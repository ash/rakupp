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

**Headline: ~81% of all declared Roast tests pass** (151,831 / 188,486); on the
stricter file bar, ~27% of files fully pass (400 / 1,464). The per-file breakdown
comes first below, then the per-test figures.

Full suite — **1,464 files**:

| Files | Count | Share of suite |
|---|---:|---:|
| **Fully passing** | **400** | **25%** |
| Partially passing | 605 | 41% |
| No TAP output | 449 | 31% |
| Timeouts | 10 | 0.7% |

(Two files — `S04-statements/try.t`, `S12-construction/destruction.t` — hang the
harness with unkillable children and are measured separately; they count above as
one partial and one timeout. See [docs/ROAST-GAPS.md](docs/ROAST-GAPS.md).)

**Coverage ≈ 27% of files.** That is the number to quote. Just under a third of the suite
produces no TAP at all — those files hit a parse error or an unimplemented
construct and abort before any assertion runs — so they are entirely unmeasured
territory, not "passing" and not "failing."

### The assertion count

Measured per individual test rather than per file, the honest figure is
**151,831 of ~188,486 declared tests — ~81%**. "Declared" means every test the
suite intends to run: for files that ran, their emitted plan; for files that
abort before emitting any TAP, the `plan N` count read straight from their
source. Counting those aborting files (all their tests failing) is what keeps the
number honest — a parse error can't make its tests vanish. The harness prints
three denominators, widest-to-strictest:

| Denominator | Ratio | What it includes |
|---|---|---|
| tests that **ran** | 151,831 / 157,059 (~97%) | only assertions files actually emitted — flatters, ignores aborts |
| tests **planned** (files that emitted a plan) | 151,831 / 169,221 (~90%) | + tests lost when a file aborts mid-plan |
| **all declared** tests | 151,831 / 188,486 (~81%) | + tests in parse-error files, recovered from source |

The ~81% is the per-test analog of the ~27% file coverage. Two caveats on scope:

1. **~28k of the denominator comes from no-TAP files** (306 of them, read from
   source); 15 more no-TAP files use a dynamic `plan *` / `done-testing` and are
   genuinely uncountable, so they sit outside even this figure.
2. **S15 (Unicode) is ~87k of the reached total**, passing at ~95%, so it lifts
   the blended rate; other synopses are lower (see the per-synopsis table).

Coverage is the ~27% of files; per-test correctness across the whole suite is the
~81%. They are different measurements, quoted for different purposes.

## By synopsis

Roast is organized by Synopsis (`SNN-*`), plus integration tests and
language-version snapshots (`6.c`, `6.d`). Assertion % is over assertions that
actually **ran** (no-TAP files contribute none), so a section can show a high %
while many of its files still don't run at all — read it alongside No-TAP.

| Section | Theme | Full | Part | Time | No-TAP | Assertions | % |
|---|---|---:|---:|---:|---:|---:|---:|
| S01 | Overview | 14 | 0 | 0 | 0 | 89/89 | 100% |
| S02 | Literals, types, magicals | 31 | 55 | 0 | 61 | 1752/2267 | 77% |
| S03 | Operators | 22 | 41 | 1 | 61 | 1110/1989 | 56% |
| S04 | Blocks, statements, phasers | 22 | 32 | 0 | 22 | 491/622 | 79% |
| S05 | Regexes & grammars | 25 | 61 | 0 | 12 | 4309/5025 | 86% |
| S06 | Subroutines & signatures | 10 | 41 | 0 | 43 | 403/689 | 58% |
| S07 | Iterators | 1 | 3 | 0 | 2 | 46/54 | 85% |
| S09 | Data structures | 0 | 8 | 0 | 14 | 94/199 | 47% |
| S10 | Packages | 2 | 5 | 0 | 2 | 32/72 | 44% |
| S11 | Modules | 8 | 9 | 0 | 5 | 54/86 | 63% |
| S12 | Objects & classes | 20 | 53 | 0 | 27 | 562/756 | 74% |
| S13 | Overloading | 4 | 0 | 0 | 3 | 25/25 | 100% |
| S14 | Roles | 5 | 12 | 0 | 8 | 125/172 | 73% |
| S15 | Unicode / strings / NFG | 68 | 8 | 0 | 5 | 91177/91242 | 100% |
| S16 | I/O | 12 | 13 | 1 | 11 | 216/340 | 64% |
| S17 | Concurrency (supply/promise/async) | 23 | 50 | 3 | 23 | 498/802 | 62% |
| S19 | Command-line | 6 | 1 | 0 | 1 | 22/24 | 92% |
| S22 | Package format | 0 | 1 | 0 | 0 | 3/3 | 100% |
| S24 | Testing | 9 | 6 | 0 | 2 | 73/120 | 61% |
| S26 | Documentation (POD) | 6 | 10 | 0 | 11 | 164/193 | 85% |
| S28 | Special variables | 2 | 0 | 0 | 1 | 6/6 | 100% |
| S29 | Builtins & context | 4 | 6 | 1 | 3 | 313/331 | 95% |
| S32 | Standard types (str/list/num/…) | 49 | 125 | 0 | 89 | 21776/22892 | 95% |
| integration | Cross-feature programs | 31 | 55 | 1 | 32 | 562/714 | 79% |
| 6.c | v6.c language snapshot | 1 | 6 | 0 | 11 | 49/81 | 60% |
| 6.d | v6.d language snapshot | 11 | 6 | 0 | 1 | 19750/20279 | 97% |
| APPENDICES | — | 1 | 3 | 1 | 1 | 31/42 | 74% |
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
with the summary **plus a paste-ready copy of the by-synopsis table above** —
so refreshing that table is a copy-paste, not a hand computation. Filter by path
substring: `build/rakupp tools/run-roast.raku S05`.

_Snapshot: 400 / 1,464 files fully passing (~27% coverage); 605 partial,
449 no-TAP, 10 timeout. Reached-assertion pass rate 151,831 / 157,059 (see
caveat above — not a coverage figure). S05-substitution is a fully-passing
subchapter (67222.t, match.t, subst.t). The +19-file jump came from honoring
roast's `#?rakudo skip` fudge directives (see [docs/ROAST-GAPS.md](docs/ROAST-GAPS.md))._
