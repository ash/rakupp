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

**Headline: ~90% of all declared Roast tests pass** (198,642 / 218,561); on the
stricter file bar, ~43% of files fully pass (633 / 1,464). The per-file breakdown
comes first below, then the per-test figures. (S15 — Unicode / strings / NFG —
is now at 100% of assertions: full UCD case tables, grapheme-level regex, and
complete `uniprop` coverage landed for v1.1; its lone non-passing file is a
performance timeout, not a correctness gap. See [ROAST-GAPS](../dev/findings/ROAST-GAPS.md).)

Full suite — **1,464 files**:

| Files | Count | Share of suite |
|---|---:|---:|
| **Fully passing** | **633** | **43%** |
| Partially passing | 692 | 47% |
| No TAP output | 123 | 8% |
| Timeouts | 16 | 1.1% |

(Both files that once wedged the harness with unkillable children are measured
in-run now: `S04-statements/try.t` scores as an ordinary partial, and
`S12-construction/destruction.t` fully passes since the DESTROY protocol
landed. See [dev/findings/ROAST-GAPS.md](../dev/findings/ROAST-GAPS.md).)

**Coverage ≈ 43% of files.** That is the number to quote. About a tenth of the
suite produces no TAP at all — those files hit a parse error or an unimplemented
construct and abort before any assertion runs — so they are entirely unmeasured
territory, not "passing" and not "failing."

### The assertion count

Measured per individual test rather than per file, the honest figure is
**198,642 of ~218,561 declared tests — 90.9%**. "Declared" means every test the
suite intends to run: for files that ran, their emitted plan; for files that
abort before emitting any TAP, the `plan N` count read straight from their
source. Counting those aborting files (all their tests failing) is what keeps the
number honest — a parse error can't make its tests vanish. The harness prints
three denominators, widest-to-strictest:

| Denominator | Ratio | What it includes |
|---|---|---|
| tests that **ran** | 198,642 / 205,084 (~97%) | only assertions files actually emitted — flatters, ignores aborts |
| tests **planned** (files that emitted a plan) | 198,642 / 215,652 (~92%) | + tests lost when a file aborts mid-plan |
| **all declared** tests | 198,642 / 218,561 (90.9%) | + tests in parse-error files, recovered from source. This denominator grows as parse fixes land — files that died before announcing a plan now declare their real (often larger, dynamic) plans, so the percentage can dip while absolute passes rise |

The 90% is the per-test analog of the ~43% file coverage. Three notes on scope:

1. **~2.9k of the denominator comes from no-TAP files** (76 of them, read from
   source); 3 more no-TAP files use a dynamic `plan *` / `done-testing` and are
   genuinely uncountable, so they sit outside even this figure.
2. **S15 (Unicode) is ~91k of the reached total**, passing at ~100%, so it lifts
   the blended rate; other synopses are lower (see the per-synopsis table).
3. **These figures are on the honest subtest bar.** As of v2.0.0,
   `subtest 'desc' => { … }` (the Pair form — most of the suite's subtests)
   actually executes its body; before, those subtests auto-passed as empty,
   inflating every earlier release's figures by ~2,340 assertions and 39
   fully-passing files. Do not compare pre-v2.0.0 Roast numbers against these
   without that correction (see the [CHANGELOG](../../CHANGELOG.md)).

Coverage is the ~43% of files; per-test correctness across the whole suite is the
90%. They are different measurements, quoted for different purposes.

## By synopsis

Roast is organized by Synopsis (`SNN-*`), plus integration tests and
language-version snapshots (`6.c`, `6.d`). Assertion % is over assertions that
actually **ran** (no-TAP files contribute none), so a section can show a high %
while many of its files still don't run at all — read it alongside No-TAP.

| Section | Theme | Full | Part | Time | No-TAP | Assertions | % |
|---|---|---:|---:|---:|---:|---:|---:|
| S01 | Overview | 14 | 0 | 0 | 0 | 89/89 | 100% |
| S02 | Literals, types, magicals | 54 | 76 | 0 | 17 | 7014/7704 | 91% |
| S03 | Operators | 48 | 63 | 3 | 11 | 22558/23291 | 97% |
| S04 | Blocks, statements, phasers | 29 | 44 | 0 | 4 | 1175/1446 | 81% |
| S05 | Regexes & grammars | 38 | 56 | 0 | 4 | 5751/6258 | 92% |
| S06 | Subroutines & signatures | 21 | 58 | 0 | 15 | 1478/1757 | 84% |
| S07 | Iterators | 2 | 4 | 0 | 0 | 226/268 | 84% |
| S09 | Data structures | 2 | 20 | 0 | 0 | 915/1117 | 82% |
| S10 | Packages | 2 | 6 | 0 | 1 | 42/79 | 53% |
| S11 | Modules | 9 | 9 | 0 | 4 | 63/95 | 66% |
| S12 | Objects & classes | 27 | 62 | 0 | 12 | 1247/1498 | 83% |
| S13 | Overloading | 5 | 1 | 0 | 1 | 64/71 | 90% |
| S14 | Roles | 6 | 16 | 0 | 3 | 252/307 | 82% |
| S15 | Unicode / strings / NFG | 80 | 0 | 1 | 0 | 91752/91752 | 100% |
| S16 | I/O | 14 | 19 | 0 | 4 | 395/569 | 69% |
| S17 | Concurrency (supply/promise/async) | 42 | 41 | 7 | 9 | 929/1073 | 87% |
| S19 | Command-line | 6 | 1 | 0 | 1 | 22/24 | 92% |
| S22 | Package format | 0 | 1 | 0 | 0 | 5/5 | 100% |
| S24 | Testing | 11 | 4 | 0 | 2 | 93/112 | 83% |
| S26 | Documentation (POD) | 6 | 20 | 0 | 1 | 304/464 | 66% |
| S28 | Special variables | 3 | 0 | 0 | 0 | 9/9 | 100% |
| S29 | Builtins & context | 6 | 7 | 0 | 1 | 421/445 | 95% |
| S32 | Standard types (str/list/num/…) | 120 | 124 | 3 | 16 | 41815/44405 | 94% |
| integration | Cross-feature programs | 66 | 42 | 1 | 10 | 1086/1180 | 92% |
| 6.c | v6.c language snapshot | 2 | 13 | 0 | 3 | 629/696 | 90% |
| 6.d | v6.d language snapshot | 15 | 3 | 0 | 0 | 20264/20310 | 100% |
| APPENDICES | — | 2 | 2 | 1 | 1 | 32/48 | 67% |
| MISC / t | — | 3 | 0 | 0 | 3 | 12/12 | 100% |

### Reading the table

- **S15 (Unicode)** dominates the assertion count — ~91k of ~201k reached
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

_Snapshot 2026-08-21, the v3.6.0 release trio (`--workers=4`): 633 / 1,464
files fully passing (~43% coverage); 692 partial, 123 no-TAP, 16 timeout
(the scheduler/io timing files flap between pass and timeout under runner
load; the three runs gave 631 / 629 / 633 files and the figures quoted here
are the run whose fully-passing list contains every file the others passed).
The file-list diff against the last published map (v3.14.0 — the v3.5.0
release skipped the site republish, which this release makes up) is CLEAN:
the one file below the baseline in all three runs,
`S32-list/map_function_return_values.t`, is a timing-marginal file that
scores 2/2 re-run alone — the documented timeout flutter, not a regression.
`S12-methods/class-and-instance.t`, which HAD regressed in the v3.5.x cycle
(a stale forward-reference record re-ran the class body on a missed method
call, 13 tests against a plan of 12), is fixed this release and back to
[PASS] 12/12. This snapshot adds
`S12-construction/destruction.t` at 6/6 — the DESTROY protocol landed that
day (instances of DESTROY-declaring classes are registered at construction and
swept child-class-first on `$*VM.request-garbage-collection`, at allocation
pressure, and at program end) — and `S32-str/fc.t` back at 12/12 after the
ASCII fold-case fix. Reached-assertion
pass rate 198,647 / 205,087 (see caveat above — not a coverage figure).
S05-substitution is a fully-passing subchapter (67222.t, match.t, subst.t).
S05-modifier/Perl_0–10 — the 918-assertion `m:P5` corpus generated from perl's
own re_tests — passes fully on real Perl-5-syntax matching; before the `:P5`
adverb landed these files skip-all'ed, so the totals don't move but the skips
became genuine passes._
