# Plan: v3.22.0 — instruments first, then the review

*Written 2026-08-29, the day v3.21.0 shipped, before any code. The second of
the three consolidation releases in
[VERSIONS.md](VERSIONS.md#v3210--v3230--the-consolidation-arc-planned-2026-08-28).
The findings it starts from are in
[findings/OPEN-3.21.md](../findings/OPEN-3.21.md), each with its repro.*

The v3.22.0 section of VERSIONS.md, written before the v3.21.0 sitting, said
this release was a 360° source review plus the slow runs. That is still half of
it. The sitting changed the order.

## Why the order changed

v3.21.0 passed all seven RELEASING.md gates and **shipped a silent wrong
answer**: `Digest`'s RIPEMD returns incorrect hashes for every input. One gate
of seven saw it, and the first reading of that gate was wrong too.

Six of the seven have a defect:

| gate | defect |
|---|---|
| 1 Roast | the file LIST — which RELEASING calls *the* gate — is parsed from status lines the harness corrupts, 4 per run; and no `roast.txt` was archived from v3.20.1, so the documented diff had no baseline |
| 3 perf | the baseline is not reproducible by the commit that recorded it; re-recorded to ship, so it now certifies numbers nobody understands. Its documented command picks the x86_64 build and reports inconclusive |
| 4b slim | reported `ALL SLIM-DIFF CHECKS PASSED` while leaving a self-replicating chain of 1,253 processes running |
| 6 battery | the run rewrites `scans/dist-tests.tsv` in place, so the obvious comparison is against the file the run just overwrote — circular, and it is how the RIPEMD regression was first missed |
| — doc refresh | the verification recipe greps for `198,791 / 218,608` shapes and is blind to a count written as a bare table cell; two tables kept stale numbers, one of them the row `gen-dashboard.raku` parses |

A review produces verdicts, and verdicts inherit the reliability of whatever
measured them. Reviewing the engine through instruments in this state means
doing careful work and then not being able to believe the result.

**The number a stranger can re-measure:** *every gate detects a planted
defect.* For each of the seven, inject a known break — a wrong hash, a >5%
slowdown, a regressed Roast file, a leaked process, a stale doc figure — run
the gate unmodified, and confirm it goes red. No release has claimed this
before, and it is exactly what failed in v3.21.0.

Environment for every measurement below: the machine of record (Darwin 24.6.0,
Apple clang 17.0.0, arm64, `build-arm64/`). Note `build/` on that box is an
**x86_64** build — see A5.

---

## Part A — the instruments

Bounded work, each item already diagnosed with a repro. Days, not weeks.

### A1. `flat` over a BOUND array does not spread — the shipped wrong answer

The gate in `src/Builtins.cpp` and `src/MethodCallTail.cpp` reads

```cpp
if (x.t == VT::Array && x.arr() && !x.itemized && !ofArray)
```

`0ae9387` changed it from `(x.isList || !ofArray)`. That is right about
*assignment* — `my @a = (1,2),(3,4)` itemises each element, so `@a.flat` is two
— but signature destructuring **binds**, and binding never itemises, so Rakudo
still spreads:

```raku
sub f([&a, $b, @c, $d]) { (flat @c »xx» 2).elems }
say f([{;}, 1, (7,8,9), 2]);     # Rakudo 6, rakupp 3
```

A plain `my @c = 1,2,3; (flat @c »xx» 4).elems` is 3 on both — the divergence
needs the bound slot. `Digest::RIPEMD` builds its 80-entry constant table
through exactly that shape, so `@K` arrives with 5 entries.

> **Superseded — this candidate fix is wrong.** Measured against Rakudo,
> dropping `ofArray` makes `my @c = 1,2,3; (flat @c »xx» 4).elems` answer 6
> where both engines agree on 3: a new divergence in the other direction. The
> `.flat` gate is right. The bug is one layer down, in **binding**: Rakudo keeps
> a List argument a List (`sub f(@c) { @c.WHAT }` over `(1,2,3)` is `List`, not
> `Array`), and a List's slots are bare, which is what spreads. rakupp coerced
> every bound `@` parameter to an Array. Fixed in `bindParams`
> (`src/Interpreter.cpp`); see [findings/GATES-3.22.md](../findings/GATES-3.22.md)
> for the six-cell before/after table and the 68-form differential.

**Done when:** all nineteen forms `0ae9387` enumerates still agree with Rakudo;
`Digest-1.1.0`'s `t/ripemd.t` scores 9 ok; a `t/regression/` case covers the
bound-slot form (add it **with** the fix — the suite has no known-failure
marker, only `#?requires`, so a failing file turns gate 2 red for everyone).

### A2. The Roast harness corrupts its own status lines

At `--workers=4` the children inherit stdout and their diagnostics splice
mid-line into the parent's per-file status output — consistently four a run,
across four runs:

```
  [PASS]    4/4  S03-smar# Failed test '$obj ~~ Pair, nonexistent, dies (1)' at …
```

The tallies are computed from data and are sound; the **file list** is parsed
from these lines, so four files per run silently lose their path and read as
regressions. The site's roast map inherits the undercount (639 written where
the run measured 643).

Emitters: `tools/run-roast.raku` — the `[TIME]` line and the
`sprintf('  [%s]  %5s  %s', …)` status line.

**Done when:** four consecutive runs produce zero status lines that fail to end
in `.t`, and `gen-roast-map.raku` writes the same fully-passing count the
runner reports.

### A3. Archive the release's `roast.txt`

RELEASING.md says to keep it so the file-list diff has a baseline. None existed
from v3.20.1, so v3.21.0's diff fell back to a development run found in
`rc-work/`. **Done when:** the procedure archives one per release and the
diff recipe names where it lives.

### A4. A compiled binary ignores `-e`; `slim-diff` does not reap

> **The scope here is too narrow, and it was measured the hard way.** With the
> `-e` refusal implemented, the v3.22.0 sitting still reached **2,633 processes
> and load average 95** — from `run $*EXECUTABLE, '-I', $dir, $prog`, which
> contains no `-e`. The confusion is about `$*EXECUTABLE`, not about `-e`, and
> **49 of the 388 corpus files spawn it**. What the binary needs is a re-entry
> bound, not an argument blacklist; and slim-diff's group kill could never have
> worked, because every child rakupp spawns calls `setpgid(0,0)` and so leads
> its own group. See [findings/GATES-3.22.md](../findings/GATES-3.22.md).

`t/regression/anton-batch-round2.raku` does `run $*EXECUTABLE, '-e', $code`.
Under the interpreter that is `rakupp -e …`. Compiled, `$*EXECUTABLE` is the
compiled binary, which **accepts and silently ignores `-e`** and re-runs its
own embedded program, reaching the same line and spawning another copy. Each
generation names its temp dylib after the previous PID:

```
87130 → 87134 → 87139 → …    (rk-cstruct-87118, -87130, -87134, …)
```

It reached 1,253 processes and load average 450 during gate 4b, which reported
`2 timed out (not judged)` and then `ALL SLIM-DIFF CHECKS PASSED`.

**Done when:** a compiled binary refuses `-e` with a message and a non-zero
exit rather than running something else; `slim-diff`'s timeout kills the
process tree; and a run of `tools/slim-diff.raku` leaves no descendants.

### A5. `perf-guard` picks the wrong binary by default

It takes the first of `build/`, `build-arm64/`, `./rakupp`. On the machine of
record `build/` is the x86_64 build, so the gate command **exactly as
RELEASING.md writes it** reports `INCONCLUSIVE — … is x86_64 on a arm64 host`
instead of measuring. Every run in the v3.21.0 sitting needed
`RAKUPP=build-arm64/rakupp`. (Same shape as the `rakujs/build.sh` trap.)

**Done when:** the documented command measures on the machine of record with no
environment variable.

### A6. The battery destroys its own baseline

`tier2/run-dist-tests.raku` (in `raku-module-battery`) rewrites
`scans/dist-tests.tsv` in place. Reading that file after a run compares the run
against itself — which is how v3.21.0's gate-6 report initially concluded "no
regression" while `Digest` had gone `PASS 4/4 → DIFF 3/4`. The truth needed
`git show HEAD:scans/dist-tests.tsv`.

**Done when:** a run reports its verdict changes against the committed baseline
directly — gained, lost, unchanged — without the reader having to reach into
git.

### A7. The doc-refresh verification is blind to bare table cells

RELEASING.md step 3 greps for figures in their `198,791 / 218,608` shape. Two
standing tables write the count as a bare cell and were missed:

- `docs/status/ROAST.md` — `| **Fully passing** | **638** | **44%** |`, the
  exact row `gen-dashboard.raku` parses, so the dashboard put `main` at 638
  while the tag read 643.
- `docs/guide/GUIDE.md` — a v1.x-era table (528 fully passing, 238 no-TAP, 12
  timeouts) directly beneath assertion figures the refresh had just updated.

**Done when:** the recipe catches a planted stale bare cell, or those tables
carry denominators so the existing pattern reaches them.

---

## Part B — why the perf baseline moved

The real open question, and the one to allocate genuine time to. Until it is
understood, gate 3 certifies numbers nobody can defend.

Five of nine kernels moved up against the 2026-08-27 record (fib +7.8%,
strscan +10.0%, subcall +11.0%, rats +26.0%, regexloop +18.5%); four did not
(asg, loopsum, hash, strpass held or improved).

Already ruled out **by measurement**, so do not re-derive:

- **Not the code.** Eight commits across the window were built arm64 and
  measured one at a time on a settled machine; no kernel moves outside noise at
  any of them. The control is the finding: `d19263b`, the commit that RECORDED
  the old baseline, rebuilt, measures fib 377.7 / rats 237.0 / regexloop 134.8
  — today's numbers, not the ones it wrote. The one real movement runs the
  other way, `ea81a5f` making regexloop ~10% faster.
- **Not load.** Identical results at load 2.5 and 4.0, before and after a
  reboot, with and without `mediaanalysisd`/`mds` running.
- **Not the OS or toolchain.** Darwin 24.6.0 and clang 17.0.0, unchanged since
  February.
- **Not power or thermal.** AC, no low-power mode, no thermal warnings, M3.
- **Not the kernels.** `tools/perf-guard.raku` byte-identical since the record.
- **Not a different machine.** Four of nine kernels still match the old
  numbers; a slower box would move all nine.

What the surviving pattern suggests: the five that moved are the call- and
allocation-heavy kernels, the four that held are register-bound. Worth trying —
memory-layout or allocator behaviour, binary layout/alignment differences
between builds of the same source, and measuring with the P-core/E-core
placement pinned rather than left to the scheduler.

**The trap to state in whatever is concluded:** if the cause is environmental
and the box returns to its old form, the gate will read ~25% *faster* than
baseline on `rats` and say nothing. `best` retains every earlier figure
(fib 350.6, rats 176.4, regexloop 99.7) so `vs best` still carries the debt in
the open.

**Done when:** either the cause is identified and the baseline re-recorded with
it named, or the investigation is written up with what was eliminated and gate
3's meaning is stated honestly in RELEASING.md.

---

## Part C — prove the gates

The release's number. For each gate, plant a defect, run the gate unmodified,
confirm red:

| gate | planted defect |
|---|---|
| 1 Roast | revert a fix so a known file stops fully passing — the file list must name it |
| 2 local suite | a case that returns the wrong answer |
| 3 perf | a deliberate >5% slowdown in one kernel |
| 4 optbench | make `--exe` disagree with the interpreter on one kernel |
| 4b slim | a cut that changes observable behaviour; separately, a program that leaks a child |
| 5 GCC | code Clang accepts and GCC does not |
| 6 battery | a dist regressed from PASS to DIFF — must be reported against the committed baseline |
| 7 conformance | a documentation example whose answer changes |

Keep the planted defects as a script or a documented recipe, so this is
repeatable at every future release rather than a one-off.

---

## Part D — the review, and the slow runs

Only now, through instruments that work. A complete source review in the form
the last two took ([REVIEW-1.0.md](../findings/REVIEW-1.0.md),
[REVIEW-3.7.md](../findings/REVIEW-3.7.md)) — batched, each batch gated, every
finding either fixed or written down as deliberate. Plus the runs skipped
between releases because they are slow: the whole Roast suite, the whole
2,524-distribution ecosystem sweep, the conformance matrix.

**Done when:** the review's finding list is closed, and the Roast file list is
re-derived from a clean full run rather than carried forward from deltas.

---

## Starting cold

1. Read [findings/OPEN-3.21.md](../findings/OPEN-3.21.md) — the same seven
   findings with fuller forensics.
2. A1 first: it is both a correctness fix and the natural test case for
   Part C's "would the gates catch this now?".
3. A2–A7 are independent of each other and of A1; they can land in any order,
   each with its own gate run.
4. Part B is research, not a task — start it early so it has time to run
   alongside the rest.
5. Standing gates apply to every batch, as always: zero Roast regressions, the
   local suite, `perf-guard --check`, methodology in
   [COUNTING.md](../../status/COUNTING.md).

## What this buys the campaign after it

The target beyond the arc is 1000 of the ecosystem's 2,524 distributions
passing their own suites, up from 637. That campaign is measured *by* the
battery and the sweep. Fixing the instruments is not a detour from it — chasing
that number with a battery that cannot be diffed against its own history would
produce a figure nobody could defend.
