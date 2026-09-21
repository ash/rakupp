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
| **timeout** | did not finish within the ceiling (10 s for rakupp, 6x that for any other engine). Scored like a mid-plan abort: what it emitted counts, the rest of its plan counts against us |

Two things are worth reading together: **files fully passing** (a strict,
all-or-nothing bar) and **assertions passing** (partial credit — a better
gauge of how much of the language actually works).

## Current standing

The exact definition of every figure below — and how the harness computes it — is
in [COUNTING.md](COUNTING.md); that file is authoritative if anything here drifts.

**Headline: ~95% of all declared Roast tests pass** (208,005 / 219,915); on the
stricter file bar, ~54% of files fully pass (788 / 1,464). The per-file breakdown
comes first below, then the per-test figures. That assertion figure is the
**shielded** one, as every implementation's is: it counts `ok … # skip` and
`not ok … # todo` lines as passes. Net of both it is 94.0% rather than 94.6% —
1,240 assertions, 0.60% of the pass count. mutsu's equivalent shield is 1,438
(0.66%), so it is a wash between the two; the measured breakdown is in
[COUNTING.md](COUNTING.md#the-assertion-figures-net-of-skip-and-todo). (S15 —
Unicode / strings / NFG — is a **fully-passing chapter** again: 91,752 of 91,752
assertions and all 81 of its files, on the strength of full UCD case tables,
grapheme-level regex and complete `uniprop` coverage. The four that had regressed
are back — the character-class predicates were too permissive at the start of an
identifier and in general radix digits, and `uniprop("")` answered the empty
string where Raku answers Nil. All 81 of its files pass in the run
these figures come from.)

Full suite — **1,464 files**:

| Files | Count | Share of suite |
|---|---:|---:|
| **Fully passing** | **788** | **54%** |
| Partially passing | 574 | 39% |
| No TAP output | 92 | 6% |
| Timeouts | 10 | 0.7% |

(Both files that once wedged the harness with unkillable children are measured
in-run now: `S04-statements/try.t` scores as an ordinary partial, and
`S12-construction/destruction.t` fully passes since the DESTROY protocol
landed. See [dev/findings/ROAST-GAPS.md](../dev/findings/ROAST-GAPS.md).)

**Coverage ≈ 54% of files.** That is the number to quote. About a sixteenth of
the suite produces no TAP at all — those files hit a parse error or an
unimplemented construct and abort before any assertion runs — so they are
entirely unmeasured territory, not "passing" and not "failing."

### The assertion count

Measured per individual test rather than per file, the honest figure is
**208,005 of ~219,915 declared tests — 94.6%**. "Declared" means every test the
suite intends to run: for files that ran, their emitted plan; for files that
abort before emitting any TAP, the `plan N` count read straight from their
source. Counting those aborting files (all their tests failing) is what keeps the
number honest — a parse error can't make its tests vanish. The harness prints
three denominators, widest-to-strictest:

| Denominator | Ratio | What it includes |
|---|---|---|
| tests that **ran** | 208,005 / 213,191 (97.6%) | only assertions files actually emitted — flatters, ignores aborts |
| tests **planned** (files that emitted a plan) | 208,005 / 217,651 (95.6%) | + tests lost when a file aborts mid-plan |
| **all declared** tests | 208,005 / 219,915 (94.6%) | + tests in parse-error files, recovered from source. This denominator grows as parse fixes land — files that died before announcing a plan now declare their real (often larger, dynamic) plans, so the percentage can dip while absolute passes rise |

The 95% is the per-test analog of the ~54% file coverage. Three notes on scope:

1. **~2.1k of the denominator comes from no-TAP files** (64 of them, read from
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

Coverage is the ~54% of files; per-test correctness across the whole suite is the
95%. They are different measurements, quoted for different purposes.

## By synopsis

Roast is organized by Synopsis (`SNN-*`), plus integration tests and
language-version snapshots (`6.c`, `6.d`). Assertion % is over assertions that
actually **ran** (no-TAP files contribute none), so a section can show a high %
while many of its files still don't run at all — read it alongside No-TAP.

| Section | Theme | Full | Part | Time | No-TAP | Assertions | % |
|---|---|---:|---:|---:|---:|---:|---:|
| S01 | Overview | 14 | 0 | 0 | 0 | 89/89 | 100% |
| S02 | Literals, types, magicals | 60 | 71 | 0 | 16 | 7448/8100 | 92% |
| S03 | Operators | 85 | 34 | 1 | 5 | 25702/26063 | 99% |
| S04 | Blocks, statements, phasers | 34 | 39 | 0 | 4 | 1243/1488 | 84% |
| S05 | Regexes & grammars | 40 | 54 | 0 | 4 | 5874/6273 | 94% |
| S06 | Subroutines & signatures | 24 | 57 | 0 | 13 | 1574/1840 | 86% |
| S07 | Iterators | 2 | 4 | 0 | 0 | 224/268 | 84% |
| S09 | Data structures | 2 | 20 | 0 | 0 | 4001/4685 | 85% |
| S10 | Packages | 2 | 7 | 0 | 0 | 55/104 | 53% |
| S11 | Modules | 9 | 11 | 0 | 2 | 90/123 | 73% |
| S12 | Objects & classes | 33 | 57 | 0 | 11 | 1398/1614 | 87% |
| S13 | Overloading | 7 | 0 | 0 | 0 | 88/88 | 100% |
| S14 | Roles | 6 | 17 | 0 | 2 | 282/333 | 85% |
| S15 | Unicode / strings / NFG | 81 | 0 | 0 | 0 | 91807/91807 | 100% |
| S16 | I/O | 20 | 14 | 0 | 3 | 585/749 | 78% |
| S17 | Concurrency (supply/promise/async) | 73 | 17 | 6 | 3 | 1290/1343 | 96% |
| S19 | Command-line | 7 | 0 | 0 | 1 | 24/24 | 100% |
| S22 | Package format | 0 | 1 | 0 | 0 | 6/7 | 86% |
| S24 | Testing | 11 | 4 | 0 | 2 | 95/112 | 85% |
| S26 | Documentation (POD) | 10 | 17 | 0 | 0 | 407/587 | 69% |
| S28 | Special variables | 3 | 0 | 0 | 0 | 9/9 | 100% |
| S29 | Builtins & context | 14 | 0 | 0 | 0 | 465/465 | 100% |
| S32 | Standard types (str/list/num/…) | 150 | 100 | 1 | 12 | 43081/44677 | 96% |
| integration | Cross-feature programs | 78 | 32 | 1 | 8 | 1172/1250 | 94% |
| 6.c | v6.c language snapshot | 2 | 14 | 0 | 2 | 648/723 | 90% |
| 6.d | v6.d language snapshot | 18 | 0 | 0 | 0 | 20310/20310 | 100% |
| APPENDICES | — | 1 | 3 | 1 | 1 | 27/48 | 56% |
| MISC / t | — | 2 | 1 | 0 | 3 | 11/12 | 92% |

### Reading the table

- **S15 (Unicode)** dominates the assertion count — ~91k of ~208k reached
  assertions live here (grapheme-break and normalization tables are enormous).
  Raku++'s generated UCD 17.0 tables clear **all** of it: every assertion and
  every file. It is why the overall assertion rate is high, so read the other
  chapters' rows on their own.
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
  format-conversion files (`sprintf-{b,c,d,e,f,o,s,u,x}.t`); all of them pass,
  and so does every other file in the chapter.

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

_Snapshot 2026-09-21 (latest), main at `b9a76b9` + a second per-file sitting
(`--workers=4 --cpu=3`, one pass): 788 / 1,464 files fully passing (53.8%); 574
partial, 92 no-TAP, 10 timeout; 208,005 / 219,915 declared assertions (94.6%).
Fifteen files, worked down the per-file failure counts rather than by chapter:
**S02** 53 to 60, **S26 (POD)** 7 to 10, **S16** 18 to 20, **S04** 32 to 34,
**S05** 39 to 40. The table and the headline figures above are from this run.

Most of it is Raku naming an error it already refused. Consecutive underscores
in a number are X::Comp::Group where a trailing one stays Confused; a numeric
LITERAL must already be the declared type, so `my Int $x = NaN` (and `my Rat $x =
42`, and `my Num $x = 1.5`) is X::Syntax::Number::LiteralType with the type and
the value as objects; a numeric adverb has nowhere to put a second value, so
`:69th($_)` is refused rather than calling the 69; a combining mark tight against
a digit makes a SYNTHETIC numeral, which is a malformed radix number inside a
colon pair and "not a valid number" anywhere else; X::Syntax::UnlessElse carries
the `.keyword` that displaced the `if`; a routine redeclaration suggests a
multi-sub and a package redeclaration does not; a Perl 5 TRAILING regex modifier
(`m/…/i`, `/…/g`) is X::Obsolete, one message per modifier; `&?ROUTINE` outside a
routine is an undeclared name; and X::IO::Closed says what it was `.trying`.

Four are behaviour rather than diagnosis. `.tree(0)` is the identity and
`.tree(*)` is no limit at all. `Whatever.new` is `*` and `HyperWhatever.new` is
X::Cannot::New. `.comb`, `.words` and `.split` on an IO::Path are questions about
the FILE, as `.lines` and `.slurp` already were — they had been combing the path
STRING. And the declarator-pod family: a method's own `#|`/`#=` now reaches
`.^find_method(…).WHY`, a leading and a trailing doc are JOINED rather than
one-or-the-other, a module keeps its doc (a package has no ClassInfo to hang it
on), and a parameter's `#|` is looked up at the PARAMETER's line — asking at
wherever the parse had reached meant the LAST parameter never found its own.
`--doc=Text` is accepted beside the bare `--doc`.

Gated file for file against the run below: nothing lost._

_Snapshot 2026-09-21 main at `f25f793` + the S15/6.d/S03/S32 working
tree (`--workers=4 --cpu=3`, one pass): 773 / 1,464 files fully passing (52.8%);
589 partial, 92 no-TAP, 10 timeout; 207,991 / 219,915 declared assertions
(94.6%). Two more fully-passing chapters: **S15 (Unicode / strings / NFG)**, 77
files to 81, and **6.d (v6.d snapshot)**, 15 to 18; **S03** went 79 files to 85
and **S32** 138 to 150. The by-synopsis table and the headline figures above are
refreshed from this run.

The four S15 files were four separate rejection bugs: an identifier may not
start with a non-ASCII digit (`X::Syntax::Variable::Numeric`) or a combining
mark (`X::Syntax::Malformed`) in a declaration; a general `:36<…>` radix number
is `X::Syntax::Malformed` where a `0b`/`0o`/`0x` prefixed literal is
`X::Syntax::Confused`, and typing both the same way traded three passes for
nine; `uniprop("")` is Nil, not the empty string; and a `todo` in force when a
`subtest` starts must not leak into the subtest's own tests.

6.d cost four: a shaped array's fixed dimensions are checked on READ and on
`:delete`, not only on assignment (`:exists`/`:kv`/`:p`/`:k`/`:v` still answer
softly); `%#08x` zero-pads in FRONT of the `0x` prefix at 6.c/6.d ("000000x1",
not "0x000001" — octal reads the same either way, binary does not); `%f` renders
the value's shortest round-trip decimal and rounds or zero-pads THAT string, so
`%.50f` of 1.115 is 1.115 followed by zeros and `%.2f` of it is 1.12; and
sprintf now raises `X::Str::Sprintf::Directives::{Count,BadType,Unsupported}`
where it used to format an (Any), render a Junction's eigenstates or echo an
unknown directive back as text.

The Count check found one real bug elsewhere: the lexer captured a `s{…} = EXPR`
replacement up to the first top-level comma, so `s{a} = join "|", 1, 2` lost the
list operator's arguments. A parenless listop owns those commas now.

The other seventeen came from working down the per-file failure counts in the
two chapters closest to 100%. In **S03**: `xor` in sink context sinks each of
its operands, so the constant among them is reported; feeding an endless list
into an `@` target dies where `my @a = 0..Inf` does not; the `$*TOLERANCE` a
Complex comparison uses is RELATIVE to the real part with no floor of 1;
`++++$x` matches no candidate, because `prefix:<++>` returns a value and not the
container; `succ`/`pred` know the superscript digits, which are not a contiguous
codepoint run; `eqv` refuses two lazy iterables of the same type; and flip-flop
state is keyed per CLONE, so a `sub` declared inside a loop body starts fresh
each time round. In **S32**: a term called as a routine (`pi()`, `e()`) is
X::Undeclared rather than an undefined routine; `end`/`kv` are one-argument
protos; `.push` and its family on a scalar are a NoMatch; `.batch(0)`'s
X::OutOfRange carries `.got`; only integers are prime, and a Complex becomes
Real first; a superscript run after an Nl/No numeral is the power operator
(`²¹²` is 4096) but never inside a `< … >` word list; the imaginary half of a
numeric string needs a digit of its own, so `"3+Infi"` is refused where
`"3+Inf\i"` is not; and `-i` has a POSITIVE zero real part.

Gated against the run below, file for file: nothing lost. One near-miss worth
recording — the `eqv` refusal first fired on two RANGES as well, which killed
`S02-types/range.t` at test 185 of 259. No file left the pass list, and it was
measure 2's denominator that gave it away, exactly as
[COUNTING](COUNTING.md#watch-measure-2s-denominator-not-just-its-numerator) says to expect._

_Snapshot 2026-09-21 (later still), main at `6aba9e9` + the S19 working tree
(`--workers=4 --cpu=3`, one pass): 749 / 1,464 files fully passing (51.2%); 614
partial, 92 no-TAP, 9 timeout; 207,912 / 219,915 declared assertions (94.5%).
The sitting was **S19 (Command-line)**: 6 fully-passing files to 7 and its one
partial to none, which is the whole reachable chapter. Rakudo passes 7 of these
8 as well, and it is the same 7 — the file neither engine passes,
`01-dash-uppercase-i.t`, is a Pugs-era test that wants `$*OS` and
`$*EXECUTABLE_NAME` (removed from the language; no other Roast file names
either), a global `@*INC` (replaced by `$*REPO`), and a `run $string` that
shells out and honours `>` (Raku's `run` does neither). It is absent from
Rakudo's `spectest.data` and from
[rakudo-2026.08.list](roast-lists/rakudo-2026.08.list), and it is one of the 31
files in the ceiling section above.

The cause was in `applyRakudoFudge`, not on the command line. A `#?rakudo todo`
in front of a column-0 block was read as a one-test todo; roast's own `fudge`
instead recurses into the block — "do all in block as one action" — and prefixes
`todo(<reason>);` to every test statement inside it. `04-negation.t` puts three
`is_run`s under one such directive, so two of its three tests ran exposed
against a bar Rakudo's fudged spectest shields. The directive is suite-wide, not
an S19 one: 41 block-form `todo`s sit in 31 files, and the fix also shields 4
more tests in `S12-enums/basic.t` and 3 in `S03-operators/identity.t`. All three
deltas reproduce standalone. Gated against a clean build of `6aba9e9` measured
the same way: no file lost. The two files gained beyond `04-negation.t` —
`S15-nfg/concat-stable.t` and `S17-supply/syntax-nonblocking-await.t` — pass in
isolation under either build, the load flappers COUNTING's timeout section
describes. The by-synopsis table above and COUNTING's measures are deliberately
not refreshed from this run: the build is stamped `-modified`, and those figures
want a clean one._

_Snapshot 2026-09-21 (later), main at `a23f1ba` + the S13 working tree
(`--workers=4 --cpu=3`, one pass): 747 / 1,464 files fully passing (51.0%); 615
partial, 92 no-TAP, 10 timeout; 207,830 / 219,677 declared assertions (94.6%).
**S13 (Overloading) is a fully-passing chapter**, 5 files to 7. Gated against the
run below: no file lost. The 19 fewer assertions EMITTED are accounted for file
by file — `S17-promise/start.t` flapped from 44 to 1 under load (3 real failures
either way, measured in isolation), against +17 from `metaoperators.t` running at
all and +7 from `S03-metaops/reverse.t` reaching further. Not a release run._

_Snapshot 2026-09-21, main at `0f77ba5` + the S03 working tree (`--workers=4
--cpu=3`, one pass): 745 / 1,464 files fully passing (50.9% coverage); 616
partial, 93 no-TAP, 10 timeout; 207,843 / 219,677 declared assertions (94.6%).
The sitting was **S03 (Operators) alone**, 48 fully-passing files to 79 and 97%
of assertions to 99%, with its no-TAP count falling 13 → 5. Gated against a clean
build of `0f77ba5` measured the same way (714 files): 32 files gained, and the
only file in the baseline's pass list absent from this one is
`S17-channel/stress.t`, which passes in isolation three times out of three — the
load flapper COUNTING's timeout section describes, not a regression. Everything
outside S03 moved only where an operator fix reached it (S02 +12 assertions, S05
+21, S32 +88). Not a release run._

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
