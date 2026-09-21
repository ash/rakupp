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

**Headline: ~93% of all declared Roast tests pass** (205,100 / 219,626); on the
stricter file bar, ~48% of files fully pass (705 / 1,464). The per-file breakdown
comes first below, then the per-test figures. That assertion figure is the
**shielded** one, as every implementation's is: it counts `ok … # skip` and
`not ok … # todo` lines as passes. Net of both it is 92.8% rather than 93.4% —
1,223 assertions, 0.60% of the pass count. mutsu's equivalent shield is 1,438
(0.66%), so it is a wash between the two; the measured breakdown is in
[COUNTING.md](COUNTING.md#the-assertion-figures-net-of-skip-and-todo). (S15 — Unicode / strings / NFG —
still rounds to 100% of assertions — 91,799 of 91,807 — on the strength of full
UCD case tables, grapheme-level regex and complete `uniprop` coverage landed for
v1.1. **Its file count went the wrong way this sitting**, 80 fully passing to 77:
`S15-literals/identifiers.t` (7/7 at v4.0.1, now 5/7) and
`S15-literals/numbers.t` (49/49, now 46/49) both regressed between the tag and
`a4291988`, and every one of the five lost assertions is a *rejection* test — a
non-ASCII digit or combining mark accepted at the start of an identifier, and
`Nl`/`No` numerals accepted as general radix digits. The character-class
predicates have become too permissive; see
[ROAST-GAPS](../dev/findings/ROAST-GAPS.md). `uniprop.t` fails 2 of its 203 as it
did before, and `uniname.t` improved to 45/46.)

Full suite — **1,464 files**:

| Files | Count | Share of suite |
|---|---:|---:|
| **Fully passing** | **705** | **48%** |
| Partially passing | 648 | 44% |
| No TAP output | 101 | 7% |
| Timeouts | 10 | 0.7% |

(Both files that once wedged the harness with unkillable children are measured
in-run now: `S04-statements/try.t` scores as an ordinary partial, and
`S12-construction/destruction.t` fully passes since the DESTROY protocol
landed. See [dev/findings/ROAST-GAPS.md](../dev/findings/ROAST-GAPS.md).)

**Coverage ≈ 48% of files.** That is the number to quote. About a fourteenth of
the suite produces no TAP at all — those files hit a parse error or an
unimplemented construct and abort before any assertion runs — so they are
entirely unmeasured territory, not "passing" and not "failing."

### The assertion count

Measured per individual test rather than per file, the honest figure is
**205,100 of ~219,626 declared tests — 93.4%**. "Declared" means every test the
suite intends to run: for files that ran, their emitted plan; for files that
abort before emitting any TAP, the `plan N` count read straight from their
source. Counting those aborting files (all their tests failing) is what keeps the
number honest — a parse error can't make its tests vanish. The harness prints
three denominators, widest-to-strictest:

| Denominator | Ratio | What it includes |
|---|---|---|
| tests that **ran** | 205,100 / 210,792 (97.3%) | only assertions files actually emitted — flatters, ignores aborts |
| tests **planned** (files that emitted a plan) | 205,100 / 216,666 (94.7%) | + tests lost when a file aborts mid-plan |
| **all declared** tests | 205,100 / 219,626 (93.4%) | + tests in parse-error files, recovered from source. This denominator grows as parse fixes land — files that died before announcing a plan now declare their real (often larger, dynamic) plans, so the percentage can dip while absolute passes rise |

The 93% is the per-test analog of the ~48% file coverage. Three notes on scope:

1. **~3.0k of the denominator comes from no-TAP files** (75 of them, read from
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

Coverage is the ~48% of files; per-test correctness across the whole suite is the
93%. They are different measurements, quoted for different purposes.

## By synopsis

Roast is organized by Synopsis (`SNN-*`), plus integration tests and
language-version snapshots (`6.c`, `6.d`). Assertion % is over assertions that
actually **ran** (no-TAP files contribute none), so a section can show a high %
while many of its files still don't run at all — read it alongside No-TAP.

| Section | Theme | Full | Part | Time | No-TAP | Assertions | % |
|---|---|---:|---:|---:|---:|---:|---:|
| S01 | Overview | 14 | 0 | 0 | 0 | 89/89 | 100% |
| S02 | Literals, types, magicals | 53 | 78 | 0 | 16 | 7433/8108 | 92% |
| S03 | Operators | 48 | 62 | 2 | 13 | 23233/23912 | 97% |
| S04 | Blocks, statements, phasers | 31 | 42 | 0 | 4 | 1236/1488 | 83% |
| S05 | Regexes & grammars | 38 | 56 | 0 | 4 | 5852/6273 | 93% |
| S06 | Subroutines & signatures | 24 | 57 | 0 | 13 | 1570/1840 | 85% |
| S07 | Iterators | 2 | 4 | 0 | 0 | 224/268 | 84% |
| S09 | Data structures | 2 | 20 | 0 | 0 | 3993/4685 | 85% |
| S10 | Packages | 2 | 7 | 0 | 0 | 55/104 | 53% |
| S11 | Modules | 9 | 11 | 0 | 2 | 90/123 | 73% |
| S12 | Objects & classes | 33 | 57 | 0 | 11 | 1423/1652 | 86% |
| S13 | Overloading | 5 | 1 | 0 | 1 | 64/71 | 90% |
| S14 | Roles | 6 | 17 | 0 | 2 | 282/333 | 85% |
| S15 | Unicode / strings / NFG | 77 | 4 | 0 | 0 | 91799/91807 | 100% |
| S16 | I/O | 18 | 16 | 0 | 3 | 580/749 | 77% |
| S17 | Concurrency (supply/promise/async) | 74 | 18 | 4 | 3 | 1306/1360 | 96% |
| S19 | Command-line | 6 | 1 | 0 | 1 | 22/24 | 92% |
| S22 | Package format | 0 | 1 | 0 | 0 | 6/7 | 86% |
| S24 | Testing | 11 | 4 | 0 | 2 | 95/112 | 85% |
| S26 | Documentation (POD) | 7 | 20 | 0 | 0 | 402/587 | 68% |
| S28 | Special variables | 3 | 0 | 0 | 0 | 9/9 | 100% |
| S29 | Builtins & context | 14 | 0 | 0 | 0 | 465/465 | 100% |
| S32 | Standard types (str/list/num/…) | 136 | 114 | 1 | 12 | 42967/44615 | 96% |
| integration | Cross-feature programs | 78 | 32 | 1 | 8 | 1161/1240 | 94% |
| 6.c | v6.c language snapshot | 2 | 14 | 0 | 2 | 646/723 | 89% |
| 6.d | v6.d language snapshot | 15 | 3 | 0 | 0 | 20264/20310 | 100% |
| APPENDICES | — | 1 | 3 | 1 | 1 | 27/48 | 56% |
| MISC / t | — | 2 | 1 | 0 | 3 | 11/12 | 92% |

### Reading the table

- **S15 (Unicode)** dominates the assertion count — ~91k of ~201k reached
  assertions live here (grapheme-break and normalization tables are enormous). Raku++'s
  generated UCD 17.0 tables clear **~100%** of it, which is why the overall
  assertion rate is high.
- **S01** is fully green: those files skip-all unless a Perl-5 interop bridge
  exists, and Raku++ handles the skip path spec-correctly.
- **S29** (builtins & context) is fully green too, and on the harder bar: its 14
  files run real work — `EVAL`, `run`/`shell`, `ord`/`chr`, `sleep`, `exit` —
  and all 465 assertions pass. The last five arrived in one sitting; the one
  that had been hiding was `S29-os/system.t`, which did not merely fail but
  **hung**, because `-n`'s record loop read standard input to EOF before running
  the body and the child it drove was waiting on the parent.
- **S32** (standard types), **S05** (regexes) and **S17** (concurrency) are the
  biggest pools of *reachable* work — high partial counts mean the files run but
  trip a long tail of individual assertions.
- High **No-TAP** counts (S02, S03, S06, S32, S12) mark constructs that abort
  before any assertion runs — the frontier where a single parser/feature gap
  unlocks a whole cluster of files.
- The **6.d** snapshot's assertion total (~20k) is dominated by the sprintf
  format-conversion files (`sprintf-{b,c,d,e,f,o,s,u,x}.t`), now largely passing.

## Where this stands among implementations

Roast is the common yardstick, so it is worth knowing where the other engines
land on it. **Rakudo itself passes 1,433 of the 1,464 here** — measured
2026-09-17 with this harness, Rakudo 2026.08 as the engine under test, Roast's
own `fudge` applied and a 60-second timeout
([ROAST-CEILING-2026-09-17](../dev/findings/ROAST-CEILING-2026-09-17.md)).
Seven of the 31 it does not pass, Raku++ does, so the reachable set on this
machine is **1,440 files** and the honest figure is **705 of 1,440 (49.0%)**.
The 735 files between the two are the work queue; the 764 it held when the
list was taken are recorded with each one's first failure in
[roast-queue-2026-09-17.tsv](../dev/findings/roast-queue-2026-09-17.tsv), which
is a dated snapshot and is not regenerated per sitting. The
list Rakudo passed is archived as
[roast-lists/rakudo-2026.08.list](roast-lists/rakudo-2026.08.list). Rakudo is
the oracle both other implementations check themselves against.

**[mutsu](https://github.com/tokuhirom/mutsu)**, the Rust implementation, is
**well ahead of Raku++ on Roast coverage**. Measured 2026-08-31 by running
`tools/run-roast.raku` under a mutsu binary — this harness scores whatever
engine runs it, so both rows below come from the same harness, the same Roast
revision (`b2cbe8a42`), the same 1,464 files, the same 10-second per-file
timeout, and the same counting rules:

| | files fully passing | assertions, all declared |
|---|---:|---:|
| **mutsu** 0.23.0 | **1,419 / 1,464 (96.9%)** | **216,807 / 218,173 (99.4%)** |
| **Raku++** 4.0.1-84-ga4291988 | 705 / 1,464 (48.2%) | 205,100 / 219,626 (93.4%) |

Both runs are on the **fudged bar** — Raku++ honours Roast's `#?rakudo`
directives unconditionally, and mutsu's equivalent was switched on with
`MUTSU_FUDGE=1` for the measurement. Getting that wrong is the easiest way to
produce a meaningless comparison: measured with mutsu's fudge left at its default
of *off*, the same harness scored it 1,227 rather than 1,419. See
[COUNTING.md](COUNTING.md#comparing-our-figure-with-another-implementations)
before setting either number beside anything else.

Two caveats, both in mutsu's favour: the 12 files that timed out under our
10-second budget are given 30–180 seconds by mutsu's own runner, and mutsu
reports 1,433 on its own harness. So 1,419 is a floor, not a ceiling.

That budget helps our row too, and by much less — which is the point. Re-run at
mutsu's 30-second default, Raku++ goes 643 → **647 / 1,464 (44.2%)**: four files
come back, the timeout column drops 12 → 4, and the rest of the gap is untouched.
Adopting every one of their counting conventions moves us three tenths of a
point against their 97.9%, so the difference here is coverage, not bookkeeping.
The rule-by-rule comparison is worked through in
[COUNTING.md](COUNTING.md#worked-example-mutsus-98-and-our-number-counted-their-way).

The shape of the difference is as informative as its size. Raku++ passes a high
proportion of assertions almost everywhere (90%) but leaves a residue in most
files, so the all-or-nothing file bar stays low; mutsu has cleaned up that tail
across nearly every synopsis. The one section where Raku++ is not behind is
**S15** (Unicode / strings / NFG), where both are at ~100% of assertions.

For how the three implementations are built, and why their coverage and speed
profiles differ the way they do, see
[faq/implementations.md](../guide/faq/implementations.md).

## Reproducing these numbers

```sh
build/rakupp tools/run-roast.raku          # self-hosted harness (Raku, run by rakupp)
```

It runs the full ~1,460-file suite in **under 30 seconds** on an 8-core
machine (26–27 s measured on the machine of record, with a media-indexing
daemon holding a core throughout), against 3½ minutes before the harness was
rewritten. The saving is scheduling, not spawning: rakupp cold-starts in 3 ms,
so a fresh process per file costs the whole run ~4.5 s of CPU, but the suite's
time is skewed — 1,326 files finish in under 50 ms while 25 files are three
quarters of the summed wall, and most of that is sleeping (two spec sleeps of
18 s, timeouts that hang at zero CPU). The harness keeps every core busy with
the bulk while the sleepers wait beside it. It streams a per-file line
(`[PASS] n/m path`, `[part]`, `[TIME]`) and ends with the summary **plus a
paste-ready copy of the by-synopsis table above** — so refreshing that table
is a copy-paste, not a hand computation. Filter by path
substring: `build/rakupp tools/run-roast.raku S05`.

How it schedules: one work queue ordered longest first from the previous
run's per-file wall times (`docs/status/roast-lists/roast.times`, committed;
`--times=FILE` reads and rewrites a file of your own), served by `--workers=N`
threads (default two per core) under a CPU budget of `--cpu=N` cores (default
one fewer than the machine has). Each file carries an estimated demand in
cores from the previous run's CPU sample, so a file that only waits starts at
once and a file that computes starts when a core's worth of demand is free;
the CPU-heavy files that finish just inside the 10 s timeout go first, onto
the idle machine, and the files that timed out last time — which need no
fidelity — overlap with the bulk afterwards. Output and totals are identical
to a sequential run: results are tallied and printed in file order regardless
of N. The harness also serialises its forks: the engine's spawn leaves a new
child's pipe ends inheritable for a moment, and a sibling forked in that
moment held them until it exited, which is what put a file with its complete
TAP already captured into the `[TIME]` column — the 12-to-22 timeout band
across passes in the snapshots below was that race, not the engine under test.
Two sweeps of the same build now agree file for file.

_Snapshot 2026-09-20, main at `a4291988` (`--cpu=5`, one pass, 37 s): 705 /
1,464 files fully passing (48.2% coverage); 648 partial, 101 no-TAP, 10 timeout;
205,100 / 219,626 declared assertions (93.4%). Not a release run — the figures
above were re-measured because the standing ones dated from v4.0.1 and the
dashboard reads this file. Against that tag the suite gained 29 files and 4,257
assertions, the bulk of it **S17 concurrency, 46 fully-passing files to 74**,
with its no-TAP count falling 9 → 3 as the supply and promise fixes of the
preceding days landed. Two files went backwards and are not yet fixed:
`S15-literals/identifiers.t` 7/7 → 5/7 and `S15-literals/numbers.t` 49/49 →
46/49, all five lost assertions being rejection tests that the lexer's
character-class predicates now wrongly accept. The per-commit gate could not see
them: it diffs against the immediately preceding commit, and these landed
earlier in the same 84-commit span._

_Snapshot 2026-09-07, main at `35c9691` (`--workers=4`, five passes): 660 /
1,464 files fully passing (~45% coverage); 672 partial, 117 no-TAP, 15 timeout.
COUNTING's rule is to quote the repeating profile, and the first three passes did
not repeat — the band was 661 / 660 / 658 / 662 / 660 with 13 / 14 / 22 / 12 / 15
timing out, on a box carrying another session's build load. 660 repeats, so every
figure above is from that pass; the union of the five is 662 files. Not a release
run: the standing figures were re-measured after the Grand Review's phase-1 fixes
(REVIEW-GRAND.md) and the three engine bugs its phase 2 turned up. The
documentation-example and ecosystem figures in README's table were NOT
re-measured here and still carry their v3.25.0 values._

_Snapshot 2026-09-03, the v3.25.0 release run (`--workers=4`, three passes):
651 / 1,464 files fully passing (~44% coverage); 683 partial, 117 no-TAP,
13 timeout. The file count repeats at 651 (band 651 / 651 / 650). No file
regressed: the union of the three passes, diffed against v3.24.0\x27s union, is
empty in the regressed direction and gains four (`S04-statements/loop.t`,
`S09-typed-arrays/native-decl.t`, `S15-nfg/concat-stable.t`,
`integration/advent2012-day03.t`); the one file that moved between passes,
`S03-operators/scalar-assign.t`, was `[TIME]` in the pass that lost it. Measured
on `v3.24.0-51-g4d873a8` against Roast `b2cbe8a42` — the same Roast revision
v3.24.0 used, so the list diff is an engine comparison and nothing else._

_Snapshot 2026-09-01, the v3.24.0 release run (`--workers=4`, three passes):
646 / 1,464 files fully passing (~44% coverage); 685 partial, 119 no-TAP,
14 timeout. The file count repeats at 646 (band 645 / 646 / 646). No file
regressed: the union of the three passes, diffed against v3.23.0's union, is
empty in the regressed direction and gains three (`S12-attributes/mutators.t`,
`S12-methods/lvalue.t`, `S32-io/out-buffering.t`). Measured on
`v3.23.0-64-g8ba790a` against Roast `b2cbe8a42` — the same Roast revision
v3.23.0 used, so the list diff is an engine comparison and nothing else. An
EARLIER three passes on this release's code, before two regressions were fixed,
read 640 / 641 / 640: the file LIST is what showed those, since that band
overlaps v3.23.0's own._

_Snapshot 2026-08-29, the v3.23.0 release run (`--workers=4`, three passes):
643 / 1,464 files fully passing (~44% coverage); 685 partial, 121 no-TAP,
15 timeout. The file count repeats at 643 (band 640 / 643 / 643). No file
regressed: the union of the three passes, diffed against v3.22.0's union, is
empty in both directions. Measured on `v3.22.0-6-g17b17a8` against Roast
`b2cbe8a42` — the first release whose Roast revision is recorded, in the run's
own banner and in a `.meta` sidecar beside the archived list._

_Snapshot 2026-08-29, the v3.22.0 release run (`--workers=4`, three passes):
642 / 1,464 files fully passing (~44% coverage); 686 partial, 121 no-TAP,
15 timeout. The file count repeats at 642 (band 642 / 642 / 644). No file
regressed: every file passing in the previous full run passes in at least one of
the three, and the files that vary between passes are all `S17-*` concurrency and
scheduler tests sitting near the 10-second per-file timeout — each one `[TIME]`
in the pass that lost it, and passing when run alone. The fully-passing file
LISTS are archived per release in
[roast-lists/](roast-lists/), so the next release diffs against data rather than
re-parsing this output; the three passes' union is kept beside the quoted pass
for exactly the flap described above. This is also the first run in which no
status line was corrupted: at `--workers=4` the children's TAP diagnostics used
to splice into the parent's output, consistently four a run, because
`Proc::Async` INHERITS an untapped stderr._

_Snapshot 2026-08-29, the v3.21.0 release run (`--workers=4`, four passes):
643 / 1,464 files fully passing (~44% coverage); 685 partial, 121 no-TAP,
15 timeout. The file count repeats at 643 (band 643 / 642 / 639 / 643 — the
639 came from a pass during which the OS resumed Photos analysis and Spotlight
indexing, and its timeouts rose to 20). No file regressed: every file passing
in the previous full run passes in at least one of the four, and the six that
vary between passes are all timeout-prone concurrency and exit tests._

_Snapshot 2026-08-27, the v3.20.0 release run (`--workers=4`, three passes
on an idle box): 638 / 1,464 files fully passing (~44% coverage); 687
partial, 121 no-TAP, 18 timeout. The file count repeats at 638 (band
638 / 638 / 637) and the figures quoted are from a repeating-profile pass.
Eleven files newly full in every pass (`S29-context/evalfile.t`,
`S06-multi/positional-vs-named.t`, `S17-supply/lines.t`/`words.t` among
them); the per-file diff against the v3.7.0 published map documents every
drop — five are the harness-timeout family (each passes standalone,
`S15-normalization/nfc-concat.t` at 2943/2943), one is a log-interleaving
artifact verified full standalone, and `integration/advent2012-day14.t` is
the release's one understood regression: the engine no longer invents a
step for an underivable sequence, and that file's sieve was passing on a
guessed step that put 9 into a list of primes — lazy seed streaming is the
noted follow-up._

_Snapshot 2026-08-24, the v3.7.0 release run (`--workers=4`, five passes):
633 / 1,464 files fully passing (~43% coverage); 692 partial, 124 no-TAP,
15 timeout (the band was 633 / 631 / 633 / 633 / 633, and each pass drops
exactly ONE file to the 10-second timeout — a DIFFERENT file every time, so
the union of the five is 634 and `comm -23` against the v3.6.0 published map
is empty: nothing regressed in any pass. Every file some pass dropped scores
100% run alone, `S03-operators/scalar-assign.t` (4 assertions) three times
over; the one agreed gain is `S32-list/map_function_return_values.t`. Two
files left renamed `.SKIP` in the checkout since before v3.6.0 —
`S04-statements/try.t` and `S12-construction/destruction.t`, both described
above as measured in-run — were restored for this release, which is why the
denominator reads 1,464 rather than the 1,462 a run would otherwise report.)_

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
