# What v3.22.0 did to the instruments, and what each fix was measured against

*The work of [plans/GATES-PLAN.md](../plans/GATES-PLAN.md) Part A, with the
before-and-after number for each item. The findings it started from are in
[OPEN-3.21.md](OPEN-3.21.md).*

Every item below is stated as a measurement, not as a description of a change.
Where a fix could not be proved at the scale it matters, that is said too.

## A1 — `flat` over a bound array: the diagnosis in the plan was wrong

The plan (and OPEN-3.21 before it) blamed `.flat`'s spread gate and proposed
dropping `ofArray` from it. **Measured against Rakudo, that would have been a
new divergence in the other direction.** The gate is right; the bug is one layer
down, in parameter binding.

Rakudo, for `sub f(@c) { @c.WHAT }`:

| argument | Rakudo | rakupp before | after |
|---|---|---|---|
| `(1,2,3)` a List | `List` | **`Array`** | `List` |
| `@a` an Array | `Array` | `Array` | `Array` |
| `(1,2,3).Seq` | `List` | **`Array`** | `List` |
| `@c is copy` | `Array` | `Array` | `Array` |
| `*@c` slurpy | `Array` | `Array` | `Array` |
| `1..3` a Range | `Range` | **`Array`** | **`Array`** (open, below) |

Binding never itemises, so a List argument stays a List — and a List's slots are
BARE, which is exactly what `.flat` spreads. rakupp coerced every bound `@`
parameter to an Array, whose slots are itemised and do not spread, so
`Digest::RIPEMD`'s `flat @c »xx» 16` over a destructured `@c` produced 5 entries
instead of 80 and every input hashed wrong.

The fix is one arm in `bindParams` (`src/Interpreter.cpp`): a non-`is copy` `@`
parameter binds an Array-or-List as it is, rather than falling through to
`coerceArray`, which clears `isList`. A reified `Seq` additionally drops its
`Seq` marker, because Rakudo answers `List` there.

**Measured:** a 68-form differential corpus run against Rakudo as oracle went
**66 → 67 of 68 agreeing** (and the cells that moved are the six marked above,
not one). `Digest-1.1.0`'s `t/ripemd.t` scores **9 ok**, its whole suite 21 ok /
0 not ok. The local suite is **579 checks green**, including a new
`t/regression/bound-array-param-stays-a-list.raku` whose every expectation is
Rakudo's own answer — **that file passes unmodified under Rakudo too**, which is
what makes it an oracle test rather than a record of our opinion.

**Still open:** a Range bound to `@c` answers `Array`, where Rakudo keeps
`Range`. Untouched by this change (it was `Array` before as well) and a wider
fix; the one form of 68 that still diverges.

## A2 — the Roast harness corrupts its own status lines

**Proved at full scale.** The v3.22.0 gate-1 run, 1,464 files at `--workers=4`:
**0 corrupted status lines** (was exactly 4) and **0 child diagnostics in the
parent's stream** (was 14,026). The archived list is 642 paths, 0 malformed.


`run-roast.raku` taps the child's stdout and **never taps its stderr**, which
`Proc::Async` therefore INHERITS: the children write their TAP diagnostics
straight into the parent's own stream.

**Measured on the real thing.** `rc-work/roast-issue41.txt`, the 2026-08-28 full
run of the v3.21.0 window, contains **14,026 child diagnostic lines** and
**exactly 4 corrupted `[PASS]` status lines** — matching the finding's "four a
run" precisely:

```
  [PASS]    4/4  S03-smart# Failed test 'smartmatch with list RHS …' at …
  [PASS]  29# Failed test 'uniprop an empty string yields Nil' at …
  [PASS]    1/1  S17-procasync===SORRY!=== Parse error at line 59: …
  [PASS]  2301/2301  S32-str/Co# Failed test 'negative limit returns no matches' …
```

`awk '{print $NF}'` over those four yields no path, which is the documented
undercount: 644 fully passing measured, 640 paths recoverable.

The fix taps stderr. **Measured on a harness reproducing the same shape:
475,929 child lines in the parent's stream → 0.**

Note what could NOT be proved cheaply: the splice needs a buffer-boundary
coincidence, so a 126-file subset run reproduces it less than once on average,
and two attempts to plant it at that scale produced zero corrupted lines. The
mechanism is proved and the source is closed; the "four consecutive clean runs"
of the plan's done-when is a full-suite claim and belongs to gate 1.

A second change makes the gate stop depending on that output at all:
`--list=FILE` writes the fully-passing paths as **data**, collected as each file
is judged, and warns loudly if any entry does not end in `.t`.

## A3 — nothing was archived, so the gate had no baseline

New: [`docs/status/roast-lists/`](../../status/roast-lists/), one
`vX.Y.Z.list` per release, written by `--list=` and committed. RELEASING.md gate
1 now names it and diffs with `comm` against the previous release's file instead
of reconstructing both sides with `grep | awk`.

`v3.21.0-devrun.list` is included as **reference, not a baseline**: it is
recovered from that development run, and its four corrupted paths were repaired
by hand. The recovered list is **644 paths, exactly the 644 the run reported**,
and no entry fails to end in `.t`.

**The first recovery was wrong, and how it was wrong is the finding.** Three of
the four paths were read off the DIAGNOSTIC that had overwritten the line — and
a diagnostic is emitted by a file that is FAILING, which is by definition not
the file whose `[PASS]` line it landed in. The correct evidence is the part of
the status line that survived: its path PREFIX and its score. Redone that way,
and confirmed by running each candidate:

| corrupted line | true file | check |
|---|---|---|
| `[PASS] 4/4 S03-smart…` | `S03-smartmatch/any-method.t` | 4 ok |
| `[PASS] 29…` (score cut) | `S15-normalization/nfc-concat.t` | 2943 ok |
| `[PASS] 1/1 S17-procasync…` | `S17-procasync/many-processes-no-close-stdin.t` | 1 ok |
| `[PASS] 2301/2301 S32-str/Co…` | `S32-str/CollationTest_NON_IGNORABLE-2.t` | 2301 ok |

The wrong version named `array-array.t`, `uniprop.t` and `comb.t` — the three
files that were failing loudly enough to corrupt someone else's line. The diff
against v3.22.0 is what exposed it: those three showed as "regressed" and three
unrelated files as "gained", which is the signature of a mis-attributed list
rather than of a real change. Corrected, the same diff reads **0 gained**.

## A4 — the fork bomb was bigger than `-e`, and it happened again during this sitting

The plan asked for a compiled binary to refuse `-e`. That was implemented — and
then **the machine went to 2,633 processes and load average 95 with the `-e`
guard already in place.**

The cause is not `-e`. It is `$*EXECUTABLE`.
`t/regression/private-call-and-import-shadowing.raku` does
`run $*EXECUTABLE, '-I', $dir, $prog` — no `-e` anywhere. Under the interpreter
that reaches rakupp; compiled, `$*EXECUTABLE` is the binary, which carries one
program, so the spawn re-runs the program that did the spawning, which spawns
again. **49 of the 388 corpus files spawn `$*EXECUTABLE`**, so this is not one
stray test.

Three fixes, in order of how much they matter:

1. **A re-entry bound in the binary** (`rakuppRefuseInterpreterEval`, shared by
   the `--exe` and `--bundle` stubs). Depth rides in the environment keyed to the
   binary's own realpath. At depth 0 only `-e`/`--eval` is refused — a compiled
   linter invoked as `mylint foo.raku` must keep working. At depth ≥ 1 everything
   is refused: a binary running a copy of itself is the `$*EXECUTABLE` confusion
   in every case this corpus contains. `RAKUPP_ALLOW_SELF_EXEC=1` opts out, for
   a program that re-executes itself on purpose.
   **Measured:** the file above, compiled, went from **2,633 processes** to a
   **peak of 1**, finishing in seconds.
2. **slim-diff kills the process TREE, not the group.** The group kill could
   never have worked: every child rakupp spawns calls `setpgid(0,0)`
   (`src/Builtins.cpp:373`, so rakupp's own `run(:timeout)` can reap
   grandchildren), which makes each generation its own group leader.
   **Measured:** a bounded leaker left **6 stray processes → 0**. The reaper
   walks `ps` deepest-first and repeats, and runs BEFORE `wait`, while the links
   still exist — an orphan re-parents to launchd and is unfindable from the root.
3. **A run that leaks cannot report PASSED.** slim-diff now scans for surviving
   processes whose command names its own scratch directory, kills them, and exits
   non-zero. Gate 4b said `ALL SLIM-DIFF CHECKS PASSED` while leaving 1,253
   processes behind; that specific sentence is now unreachable in that state.

Those 49 files are still judged, and correctly: both sides of the comparison are
compiled binaries, so both refuse the re-entry identically and the `--slim`
question is still answered. What makes that safe is the bound in the binary, not
the harness noticing the shape.

## A5 — the documented perf command did not measure

`perf-guard.raku` defaulted to `build/rakupp`, which on the machine of record is
an **x86_64** build beside a native `build-arm64/`. The gate command exactly as
RELEASING.md writes it reported `INCONCLUSIVE — … is x86_64 on a arm64 host`.

The default now searches `build/`, `build-arm64/`, `./rakupp` and picks the first
built for THIS host, saying so when that is not the first candidate. An explicit
`RAKUPP=` is still honoured verbatim — the A/B usage depends on that.

**Measured:** `rakupp tools/perf-guard.raku --check` with no environment variable
now reports
`perf-guard: …/build-arm64/rakupp  (chosen over an earlier candidate: it is the arm64 build)`
and proceeds to measure.

### A5 has a sibling, and it was making gate 4 lie

`tools/run-optbench.raku` — **gate 4**, the one RELEASING.md says exists because
"the code generator is a second implementation of the language" — defaults to
`build/rakupp` the same way. Run exactly as documented on the machine of record,
it compiled every kernel with the **x86_64** build, and those binaries SIGSEGV
here (exit 139). The gate's report was:

```
stringbuild    ⚠ MISMATCH: --exe did not run; --exe -O did not run
intsum         ⚠ MISMATCH: --exe did not run; --exe -O did not run
…all eight…
⚠ OUTPUT MISMATCH — a flagged engine disagreed with the reference
```

which reads as a code-generator failure across the board and is nothing of the
kind. Fixed with the same architecture-filtered search, plus an explicit
INCONCLUSIVE for a cross-architecture binary — because for THIS gate a wrong-arch
compiler is worse than a missing one: it compiles successfully and emits
binaries that crash, so the failure arrives dressed as a correctness bug.

**Measured after the fix:** gate 4 exits 0 and reports real timings
(`sieve` 758.6ms → 20.6ms at `-O`, a 36.9× speedup), naming the build it chose.

That is the third instance of this shape in the repo (perf-guard, optbench, and
the `rakujs/build.sh` trap the plan cites). Anything that picks a binary by
taking the first path that exists has it.

## A6 — the battery compared a run against itself

`tier2/run-dist-tests.raku` rewrites `scans/dist-tests.tsv` in place, so reading
that file after a run compares the run against what it just wrote. It now reads
`git show HEAD:scans/dist-tests.tsv` **before** writing, and reports gained /
regressed / other / new / no-longer-scanned directly — exiting non-zero if any
dist fell from PASS.

**Measured, and it is the independent confirmation of A1:**

```
--- against the COMMITTED baseline (git HEAD:scans/dist-tests.tsv) ---
  gained (1): Digest: DIFF -> PASS
```

## A7 — the figure grep cannot see a bare table cell

RELEASING.md step 3 greps for figures in their `198,791 / 218,608` shape. Two
standing tables write the count as a bare cell, so the pattern never reaches
them, and both went stale through the v3.21.0 refresh.

The cells cannot simply be given denominators: raku-spec's
`gen-dashboard.raku` strips every non-digit from that exact cell, so writing
`643 / 1,464` there would make the dashboard read **6431464**. So this is a
checker, `tools/check-figures.raku`, which reads the headline figures
structurally and requires them to agree with each other and with `--expect=N`.

**Measured against the planted v3.21.0 defect** (638 in ROAST.md's cell while
the tag read 643): the checker reports `DISAGREEMENT … (638, 643)` and exits 1.
The old grep, run on the same planted tree, matches **zero** lines.

---

# Part B — why the perf baseline moved: four more eliminations, and no cause

The plan allocated genuine time to this and allowed two endings: identify the
cause, or write up what was eliminated and state gate 3's meaning honestly.
**This is the second ending.** Four hypotheses were tested and all four are dead,
including the plan's own leading candidate.

The gap to explain: against the 2026-08-27 record, five of nine kernels moved up
(fib +7.8%, strscan +10.0%, subcall +11.0%, **rats +26.0%**, regexloop +18.5%)
and four did not (asg, loopsum, hash, strpass). The control is that `d19263b` —
the commit that RECORDED the old baseline — rebuilt today measures today's
numbers, not the ones it wrote.

## Eliminated: build nondeterminism

Two builds of identical source, into the same tree, minutes apart:

```
control   12324952 bytes, sha a92c9ca57cc78efe
control2  12324952 bytes, sha a92c9ca57cc78efe
```

**Byte-identical.** The build is deterministic, so "the same commit built twice
gives two different binaries" is not available as an explanation.

## Eliminated: binary layout — the plan's leading hypothesis

The plan proposed "memory-layout or allocator behaviour, binary layout/alignment
differences between builds of the same source". Tested directly by inserting
inert code into one translation unit and rebuilding — a change that cannot
affect what any kernel executes, only where it lands:

| kernel | control | +8 fns | +64 fns | +512 fns | control2 | max swing |
|---|---:|---:|---:|---:|---:|---:|
| fib | 384.5 | 378.8 | 382.7 | 382.2 | 384.7 | 1.5% |
| asg | 161.4 | 162.5 | 163.9 | 165.2 | 164.1 | 2.4% |
| loopsum | 93.9 | 94.9 | 95.5 | 95.0 | 95.4 | 1.7% |
| hash | 18.4 | 18.1 | 18.4 | 18.7 | 18.2 | 3.3% |
| strscan | 125.2 | 121.0 | 122.9 | 124.3 | 126.2 | 4.3% |
| strpass | 75.3 | 75.2 | 75.3 | 75.6 | 76.5 | 1.7% |
| subcall | 173.3 | 171.9 | 174.3 | 175.3 | 173.5 | 2.0% |
| rats | 240.7 | 240.8 | 239.7 | 247.7 | 242.8 | 3.3% |
| regexloop | 119.8 | 123.9 | 121.8 | 121.0 | 119.7 | 3.5% |

`control` vs `control2` are the SAME BINARY measured twice, and they differ by up
to 1.7% — that is the run-to-run floor. Against it, deliberate layout
perturbation buys at most **~3.5%**. The move to explain is 8–26%.
**Layout does not explain it, and cannot.**

The table has a second use: it is the first measurement of this gate's own noise
floor. A 5% tolerance sits barely above a 3.5% layout swing plus a 1.7% run
floor — worth knowing before reading any 6% result as a regression.

## Eliminated: the macOS nano allocator

The movers are the allocation-heavy kernels, so the allocator was the natural
suspect. Re-measured with `MallocNanoZone=0`, which takes the nano zone out:

| | fib | asg | loopsum | hash | strscan | strpass | subcall | rats | regexloop |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| default | 380.0 | 165.8 | 93.9 | 18.2 | 122.6 | 75.1 | 171.2 | 235.7 | 118.3 |
| nano off | 383.8 | 162.1 | 95.2 | 17.8 | 123.8 | 75.9 | 170.0 | 240.3 | 119.1 |

Every kernel within the run-to-run floor. **No effect.**

## Eliminated: a statistical artifact of the metric

`perf-guard` reports `@ms.skip(1).min` — the **minimum of three runs** after a
discarded warm-up. A min is sensitive to the distribution's tail, so a baseline
recorded on a lucky draw was a live possibility. Twenty consecutive runs of the
`rats` kernel on the same binary, sorted (ms, including process startup):

```
250.5 251.8 252.2 253.4 254.5 254.6 255.1 256.0 256.7 256.9
257.9 258.4 259.7 260.3 260.6 260.7 261.0 264.2 269.5 278.7
```

Min 250.5, median ~257, and eighteen of twenty inside 5%. **The distribution is
tight.** For 185.9 to be a draw from this, the whole distribution would have to
sit 28% lower; nothing in twenty runs comes near it. The old number is not a
lucky reading of today's machine — it is from a regime this machine is not in.

## Where that leaves gate 3

Eliminated by measurement, across this sitting and the last: the code (eight
commits built and measured one at a time), load, OS and toolchain, power and
thermal state, the kernels themselves (`perf-guard.raku` is sha-identical
between `d19263b` and `HEAD`, verified here rather than assumed), a different
machine, build nondeterminism, binary layout, the allocator, and the metric.

**No cause is identified.** What is now known that was not before: it is not a
measurement artifact and not a build artifact, so it is a real property of this
machine that changed between 2026-08-27 and 2026-08-29 and has persisted — the
numbers re-measured today (fib 379.9, rats 240.2, regexloop 120.3) sit within
1.5% of the re-recorded baseline, days later.

The trap the plan named is still live and must stay named: **if the box returns
to its old form, gate 3 will read ~25% FASTER than baseline on `rats` and say
nothing at all.** `best` retains every earlier figure (fib 350.6, rats 176.4,
regexloop 99.7) so `vs best` keeps the debt in the open — at today's numbers
that column reads `rats +36.2%`, and that is the honest headline, not the `+1.4%`
next to it.

---

# Part C — every gate detects a planted defect

> **Corrected in v3.23.0:** read this as *every gate that can fail* — 8 plants
> across 7 gates. Gate 7 (conformance) has no red path and was never planted;
> see [TOOLS-3.23.md](TOOLS-3.23.md).

**The release's number: 8 of 8.** For each gate a defect it is supposed to catch
was injected, the gate's own documented command was run unmodified, and the
result had to be RED. No release has claimed this before, and it is exactly what
failed in v3.21.0, where all seven gates passed over a wrong cryptographic hash.

```
2        local suite (t/run.raku)                 CAUGHT  (79s)  exit 1, and it names the file
3        perf-guard --check                       CAUGHT   (7s)  exit 1; baseline fib 377.7 -> 339.9
4b       slim-diff (behaviour)                    CAUGHT   (7s)  exit 1: 0 identical of 1 programs
4b-reap  slim-diff (leaves no processes)          CAUGHT  (72s)  sleeps before 0, after 0
4        optbench (modes agree)                   CAUGHT  (85s)  exit 1, naming intsum as the mismatch
6        battery (vs the COMMITTED baseline)      CAUGHT  (43s)  states its changes against git HEAD
1        Roast file list                          CAUGHT  (11s)  comm found 1 regression (want 1)
5        GCC build                                CAUGHT   (4s)  GCC build exit 2
```

It is repeatable: `rakupp tools/prove-gates.raku --all`, and `--list` says what
each plant is and what it costs. Every plant records how to undo itself before it
acts, and the undo runs from a `LEAVE`, so a failed or interrupted run still
leaves the tree as it found it — verified after each run above.

## What the plants actually are

Two of them are only meaningful because the obvious version does not work:

- **Gate 4b** cannot be provoked by any corpus program. `--slim` (auto) removes
  only what it has *proven* unused, so a correct implementation can never differ
  and no test file can force it. The faithful plant is a bug in the **cutting
  machinery**: drop `uniname` from the set of names that force `unicode-names` to
  be kept, and a program calling `uniname` has the feature cut out from under it.
  That is precisely the class SLIM-PLAN's defence 5 exists to stop. Verified by
  hand as well as by the harness — planted: `exit 0 vs 1; stdout differs (28 vs 0
  chars)`; restored: `ALL SLIM-DIFF CHECKS PASSED`.
- **Gate 3** is planted by doctoring the recorded baseline 10% faster rather than
  building a genuinely slower engine. The arithmetic is identical, and it is
  honest to say so: it proves the gate's comparison and its red path, **not** that
  a real slowdown is measurable above the noise floor Part B measured.

## Three plants were wrong before they were right

Worth recording, because a bad plant produces a **false accusation against a
working gate** — and a harness that cries wolf is how a gate gets ignored:

1. **Gate 4** — `@benches` in `run-optbench.raku` is a hardcoded list, not a
   directory scan, so a new file dropped into `tools/optbench/` is never run. The
   plant now appends to an EXISTING kernel.
2. **Gate 4b** — the first plant was a corpus program, which as above cannot work.
3. **Gate 1** — `($list.lines, 'extra')` is a TWO-element list in Raku (a Seq and
   a Str), so sorting it sorted two items and joining stringified the Seq's gist.
   `comm` then reported 2 regressions against a garbled predecessor. Fixed with
   `|$list.lines`.

Each of the three first reported `MISSED` against a gate that was working
correctly. The lesson for anyone extending this harness: **when a gate reports
MISSED, suspect the plant first** — verify by hand that the defect is really
present before believing the gate is blind.

A fourth near-miss went the other way: gate 4b's `CAUGHT` arrived in 7 seconds,
which looked far too fast for the two rebuilds it does, and nearly got recorded
as a false positive. It was genuine — `SlimScan.cpp` is 514 lines and compiles in
about two seconds. Timing intuition is not evidence; the hand check was.

---

# Part D — the review, started from the bug this release fixed

The plan puts a complete source review here, "through instruments that work".
**That review is not finished** — 83 files and 172,708 lines is more than one
sitting, and the previous two (REVIEW-1.0, REVIEW-3.7) are 357 and 411 lines of
findings each. What follows is the first thread of it, and it was worth pulling
immediately because it came free from A1.

## The thread: where else does a value lose `isList` crossing a boundary?

A1 was not a `.flat` bug. It was a value losing an attribute at a boundary —
`isList` cleared by `coerceArray` on a path that BINDS rather than assigns. That
is a shape, not an incident, so the review's first question was where else it
occurs. `coerceArray` has 13 call sites in `src/Interpreter.cpp`; each was read
and classified.

**Two more instances of the same defect**, both confirmed against Rakudo before
being touched:

| site | shape | Rakudo | rakupp before |
|---|---|---|---|
| the NAMED parameter arm | `sub f(:@c)` with a List | `List` / `.flat` → 4 | `Array` / 2 |
| the named-destructuring arm | `sub f(@a [$x, *@y])` | `List` / `.flat` → 4 | `Array` / 2 |

Both are now fixed the same way as the positional arm. The named arm sits
fifty lines above the one that shipped the wrong hashes and had been written the
same way; the destructuring arm binds "the whole argument" alongside the
destructure and coerced it unconditionally.

**Correctly coercing, and left alone** — the other eleven sites are assignment or
storage, where itemising is the right answer and Rakudo agrees:

- `@a = expr`, `my @a = …`, and the shaped-array stores (assignment itemises).
- The attributive-parameter write (`method m(@!a)`): verified `Array` on both
  engines, because `has @.a` IS an Array slot.
- `.=`'s target coercion, which the code already documents as deliberate: `@a .=
  repeated` stores an Array, not the Seq the method answered.

## What this says about the review still to come

Three instances of one defect, in one function, across two releases — the first
of them shipped a silently wrong cryptographic hash. None of the three was found
by a gate; the first was found by the battery only after release, and the other
two by asking "where else" once the shape was known.

That is the argument for the review the plan asks for, and the reason it belongs
*after* the instruments rather than before: the question "where else does this
shape occur" is only worth asking when the answer can be gated. It now can be —
the local suite, the differential, and the battery all ran green over these two
fixes, and the battery reports its verdict changes against a baseline it cannot
overwrite.

**Not yet reviewed:** everything else. The remaining 80 files have not been read
in this sitting, and no claim is made about them.

---

# The gates, run

Every figure below is from this sitting on the machine of record (Darwin 24.6.0,
Apple clang 17.0.0, arm64, `build-arm64/`).

| gate | result |
|---|---|
| 1 Roast | **642 / 1,464 fully passing** (43.9%), 198,751 / 218,467 declared assertions (91.0%). File list archived as `docs/status/roast-lists/v3.22.0.list`. **0 corrupted status lines, 0 child diagnostics** — A2 proved at scale. |
| 1 diff | **0 gained, 2 "regressed"** against the recovered v3.21.0 list — both `[TIME]` in this run and both passing when run alone, so **0 real regressions**. This run had 24 timeouts against the reference run's 12. |
| 2 local suite | **579 checks, all passed** (578 + the new bound-parameter regression case) |
| 3 perf | **OK — no kernel more than 5% slower**, and measured with **no `RAKUPP=`**, which is A5's whole point. `vs best` still reads `rats +36.2%`: the open debt of Part B, in the open. |
| 4b slim-diff | **356 identical of 388**, 23 do-not-compile, 6 nondeterministic, **0 timed out** (was 2), and **no leaked processes**. |
| 6 battery | `Digest: DIFF -> PASS` against the **committed** baseline — the independent confirmation of A1. |

## One caveat on gate 1, stated rather than buried

RELEASING.md asks for three runs and the repeating profile before quoting a
file count. **This is one run.** The 642 is therefore a single reading inside a
band whose previous member is 644, and the difference is entirely accounted for
by the timeout count (24 against 12). The *list diff* — which RELEASING calls
the gate — does not depend on that: both deltas were re-run individually and
both pass. Quote the diff; treat 642 as provisional until two more runs agree.

## Gate 4b caught a defect this work introduced

Worth recording, because it is the only time this sitting that a gate caught
something before a human did. The first version of the compiled-binary guard
printed `argv[0]` in its message. `slim-diff` compares two builds of one program
byte-for-byte, and those builds differ only in filename — so every
`$*EXECUTABLE`-spawning program reported `stderr differs (640 vs 640 chars)`,
the equal lengths being the tell. Three files failed; the fix was to drop the
path from the message. That is the gate doing exactly what Part C asks it to
prove it can do, unplanted.

## A second Part D thread: what `--exe` cannot compile, and why no gate looks

While checking whether the compiled path binds `@` parameters the way the
interpreter now does, `--exe` **failed to compile the check itself**:

```raku
sub k([$b, @c]) { @c.elems }; say k([1, (7,8,9)])
```
```
bindmode-exe.rakupp.gen.cpp:55:85: error: use of undeclared identifier 'v_ac'
```

Sub-signature destructuring emits C++ that references the mangled `@c` without
declaring it. Pre-existing — the only Codegen change in this release adds a
declaration to the generated `main()` prologue — and narrowed by elimination:
plain `@c`, named `:@c`, and `@a [$x, *@y]` all compile; the bare `[$b, @c]`
sub-signature does not.

**No gate was looking.** `slim-diff` puts such a program in `@nocompile` with the
comment "does not compile at all: not this gate's business", and `run-optbench`
only ever compiles its own nine kernels. So the code generator can fail on a
valid construct and every gate stays green — the exact failure mode RELEASING.md
says gate 4 exists to prevent ("the code generator is a second implementation of
the language, so a bug can live there and nowhere else").

Worse, `slim-diff` reported only the COUNT: `23 skipped: do not compile`. Twenty-
three programs were being silently not-judged and never named, so no one could
tell an unsupported-by-design construct from a compiler bug. It now names them.

The interpreter half of the check did pass, and is worth recording:

| | interpreter | Rakudo | 
|---|---|---|
| `sub f(@c)` over a List → `.WHAT` | List | List |
| `sub g(@c)` over `((1,2),(3,4))` → `.flat` | 4 | 4 |
| `sub h(:@c)` → `.flat` | 4 | 4 |
| the RIPEMD destructure | 6 | 6 |
| the same over an ASSIGNED array | 2 | 2 |
