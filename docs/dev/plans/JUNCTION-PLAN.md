# Plan: junctions — stop paying for eigenstates nobody asked about

*Written 2026-09-18, before any code. Prompted by a question about `5 ~~ 1|3|5`:
whether the autothread loop could be spread across cores. It could — the engine
has had true parallelism since v3 — but measuring first turned up two serial
costs that are larger than anything threading could return, and a reason the
parallel version would be slower at every width that occurs in practice. The
parallel answer is recorded here (section 5) so it does not have to be
re-derived. Sibling of [DISPATCH-PERF-PLAN.md](DISPATCH-PERF-PLAN.md), which is
the same shape of problem one layer down: per-call work that belongs outside the
loop.*

## The measurement that frames everything

Apple M1 (4 performance + 4 efficiency cores), `build/rakupp` 4.0.1-22-g3beaa88
against Rakudo 2026.08, best of three, 2026-09-18.

These were taken at 3beaa88. HEAD moved to 80b16d1 (the Nil-Any sheet
implemented) while they were being taken, and that commit adds 260 lines to
`src/Interpreter.cpp` including work on the smartmatch path — the line numbers
below are 80b16d1's, but **re-take the baseline before phase 1** rather than
measuring a change against a binary built from the commit before it.

Every program below lives in
[`tools/bench/junction/`](../../../tools/bench/junction) and runs under both
engines unchanged.

A junction of width W, built once with the n-ary constructor, then smartmatched
in a loop — so this times the autothread and nothing else (`width.raku`):

| W | rakupp per eigenstate | Rakudo per eigenstate |
|---|---|---|
| 3 | 491 ns | 128 ns |
| 10 | 425 ns | 78 ns |
| 100 | 399 ns | 52 ns |
| 1000 | 397 ns | 50 ns |
| 10000 | 397 ns | 50 ns |

**7.9x per eigenstate** at the asymptote — the small-W rows carry the loop's own
per-iteration cost, which is why both columns fall as W grows. And the same
junction (W=2000, `any`) matched against a needle at four positions:

| needle | rakupp | Rakudo |
|---|---|---|
| matches eigenstate 1 | 795.1 us | 1.8 us |
| matches eigenstate 1000 | 794.5 us | 51.2 us |
| matches eigenstate 2000 | 795.6 us | 102.8 us |
| matches nothing | 794.5 us | 115.8 us |

**We do not short-circuit. Rakudo does** — its cost is linear in where the match
is, ours is flat. On an early hit that is 442x, from a missing `break`.

Where the 397 ns goes. With the per-iteration block call subtracted out:

| | rakupp | Rakudo |
|---|---|---|
| `$a == $b` | 281 ns | 168 ns |
| `$a ~~ $b` | 844 ns | 161 ns |
| `$a ~~ any(1,3,5)` | 1611 ns | 464 ns |

In Rakudo a smartmatch costs what a numeric compare costs. Here it costs 3x,
because each eigenstate re-enters `applyArith` at
[Interpreter.cpp:24823](../../../src/Interpreter.cpp) and walks ~230 lines of
operator-string dispatch — `isSetOpStr`, the `>>OP>>` prefix tests, the `R`
metaop test, the junction-constructor arm — before reaching the comparison. The
junction loop pays that W times for one decision that cannot change between
eigenstates.

And the expression as actually written, `5 ~~ 1 | 3 | 5`, 300k times, net of
loop overhead: **1.922 us here, 0.349 us in Rakudo**. Of ours, 0.656 us is
building the junction (two nested `applyArith("|")` calls, the left-assoc bug)
and 1.457 us is matching it.

## How wide are real junctions

Every literal `any`/`all`/`one`/`none` constructor call in the Roast suite, 388
of them:

| width | count | cumulative |
|---|---|---|
| 1 | 144 | 37.1% |
| 2 | 153 | 76.5% |
| 3 | 88 | **99.2%** |
| 4 | 3 | 100% |

The widest junction written down anywhere in the suite has four eigenstates.
This number governs everything below: an optimisation whose benefit starts at
W=40 is an optimisation for a program nobody has written.

## The sites

Nine collapse loops, all the same shape, none of them short-circuiting:

| where | reached by |
|---|---|
| [`Value.cpp:117`](../../../src/Value.cpp) | `Value::truthy()` |
| [`Interpreter.cpp:14984`](../../../src/Interpreter.cpp) | `Interpreter::boolify()` |
| `Interpreter.cpp:25060` | `applyArith`, `~~` with a junction matcher |
| `Interpreter.cpp:25073` | `applyArith`, `~~` with a junction topic |
| `Interpreter.cpp:30431` | `evalBinary`, junction topic — **the hot path for a written-out `$x ~~ $j`** |
| `Interpreter.cpp:30467` | `evalBinary`, junction matcher — **ditto** |
| [`Builtins.cpp:1938`](../../../src/Builtins.cpp) | `matcherAccepts` — `.grep(none /…/)`, `.first(any …)` |
| `Builtins.cpp:2722`, `:2728` | `deepEq` / `is-deeply` |
| `Builtins.cpp:11727`, `:11736` | Test's `is` |

A tenth loop, `Interpreter.cpp:25093`, threads a non-`~~` operator into a
*preserved* junction of results (`(5 == 3|5|7).gist` is `any(False, True,
False)`). That one cannot short-circuit — every eigenstate's answer is part of
the result — and is out of scope for phase 1.

The two `evalBinary` loops are the ones a program actually hits when the
smartmatch is written out; the `applyArith` pair is the fallback for compiled
and folded paths. Both pairs need the same change, and they are not shared code.

## Design, in the order worth doing it

### 1. Short-circuit the collapse — DONE (2026-09-19)

Landed as d34120f. All nine loops now feed a `JunctionCollapse` (src/Value.h)
and break as soon as the verdict is settled.

Measured with [`tools/bench/junction/compare.raku`](../../../tools/bench/junction),
which interleaves the cases inside one run; the two binaries were then alternated
three times and the best of each cell taken. 292af2e against d34120f, both built
Release, Rakudo 2026.08 alongside, load average ~4, 2026-09-19. All three engines
agree on every checksum.

| case | before | after | change | rakudo |
|---|---|---|---|---|
| `any` w=2000 hit first | 806.61 | **1.10** | **732x** | 0.37 |
| `any` w=2000 hit middle | 799.46 | **414.23** | 1.93x | 52.18 |
| `any` w=2000 hit last | 799.00 | 818.82 | — | 104.02 |
| `any` w=2000 no hit | 800.64 | 826.95 | — | 95.15 |
| `all` w=2000 fails first | 811.64 | **1.52** | **534x** | 1.04 |
| `none` w=2000 hit first | 808.24 | **1.10** | **735x** | 0.81 |
| `one` w=2000 single hit | 804.02 | 823.12 | — | 278.24 |
| `one` w=2000 two hits | 801.32 | **1.50** | **535x** | 1.00 |
| `bool any(2000 True)` | 9.38 | **0.59** | **16.0x** | 0.64 |
| `bool all(2000 False)` | 9.34 | **0.58** | **16.2x** | 0.65 |
| `5 ~~ 1\|3\|5` (hits last) | 2.39 | 2.42 | — | 0.81 |
| `1 ~~ 1\|3\|5` (hits first) | 2.40 | **1.56** | 1.54x | 0.69 |
| `'a' ~~ any(<a b c>)` | 2.26 | **1.42** | 1.59x | 0.76 |
| `grep any(2,4,6)` over 200 | 247.54 | 252.19 | — | 79.81 |

us per operation. Every case where the verdict can settle early moves by two to
three orders of magnitude; every case where it cannot is unchanged, which is the
shape the position table predicted, so the falsifier at the foot of this plan did
not fire. Boolification gains 16x because `?any(2000 True)` settles on the first
eigenstate.

At the real widths the gain is 1.5x and only when the match is early — `5 ~~ 1|3|5`
matches the LAST eigenstate and so is flat, which is the honest headline for this
phase.

Read as distance from Rakudo rather than as our own before/after, which is the
measure that says what actually changed:

| case | gap before | gap after |
|---|---|---|
| `any` w=2000 hit first | 2190x | **2.99x** |
| `none` w=2000 hit first | 1000x | **1.36x** |
| `one` w=2000 two hits | 798x | **1.49x** |
| `all` w=2000 fails first | 779x | **1.46x** |
| `bool any(2000 True)` | 14.6x | **0.91x — we lead** |
| `bool all(2000 False)` | 14.4x | **0.89x — we lead** |
| `any` w=2000 hit last | 7.68x | 7.87x |
| `any` w=2000 no hit | 8.41x | 8.69x |

Every case that can settle early went from hundreds or thousands of times behind
to within 1.4-3x, and boolification now runs slightly ahead of Rakudo. What
remains on those rows is not a junction deficit: `prebuilt any(1,3,5)` sits at
2.31x with no width involved at all, which is simply what this interpreter costs
against MoarVM. The rows that did NOT move are the ones where no short-circuit
is possible, and they sit at the ~8x that is phase 2's actual target.

One methodological note, since it produced a wrong reading once: an earlier
version of this table had us *beating* Rakudo on the early hit. That came from
`shortcircuit.raku`, whose W=2000 rows run only 500 reps — plenty for us, too few
for MoarVM to warm up, which reported Rakudo at 1.8 us on a case that measures
0.37 us warm. Cross-engine ratios need `compare.raku`'s rep counts or higher.

Verification, each against a real before/after baseline built from the same
tree: `t/run.raku` 4 failures before and after, the SAME four (proven by
building HEAD with none of this change: they are another session's in-flight
`.values`/reduce/assoc-index work, no junction in any assertion);
`S03-junctions/*.t` 23 failures before and after, byte-identical lists; a 68-case
semantics diff against Rakudo 2026.08, identical but for one pre-existing
`.first`-with-a-junction-argument gap that is argument autothreading, not
collapse. Eigenstate side-effect counts match Rakudo exactly, including `one`
running two blocks and stopping.

`verdict()` is provably the same function of the inputs as the old
count-and-compare (`all`'s `!sawFalse` ≡ `t == total`, and each kind's `done()`
fires only where more input cannot change the answer), so the ONLY observable
difference is side effects in skipped eigenstates — which is what the
side-effect check above covers.

Found on the way, pre-existing and filed separately: matching against a junction
of regexes leaves `$/` holding an eigenstate's Match, where Rakudo leaves it
untouched. Independent of this change — the `all`-with-every-eigenstate-matching
case runs every eigenstate under both the old loop and the new one and still
diverges.

Not yet done: `tools/perf-guard.raku --record` for the two new kernels. They
measure (`junction` 430.9 ms, `junctionwide` 412.7 ms, spreads 0.3%/0.1%) but
carry no baseline, so they are not gated yet. Record on an idle machine — the
run that added them sat at load average 10 and the guard refused to judge any
kernel, including untouched ones.

#### The design, as built

The largest win, the smallest diff, no new machinery. Per kind:

| kind | stop when | verdict |
|---|---|---|
| `any` | first true | True |
| `all` | first false | False |
| `one` | **second** true | False |
| `none` | first true | False |

The trap that shaped the design: every loop counted hits into `t` and tested
`all` as `t == total`, with `total` incremented INSIDE the loop. Under an early
exit `total` stops being the width and `t == total` goes trivially true. Rather
than get that right nine times, the decision lives in one type — `all` is
`!sawFalse`, no count comparison anywhere — so the trap is unreachable rather
than merely avoided. `one` stops at the SECOND true, never the first
(`one(1, 1)` is False), and the header says so where a reviewer will try to
"fix" it.

The kind is resolved to an enum in the constructor, not compared as a string per
eigenstate: `done()` runs on every pass, and charging each eigenstate for what
the junction already knows is the very cost phase 2 exists to remove.

Legality: evaluation order and short-circuiting are explicitly undefined for
autothreading, and the position table above shows Rakudo already short-circuits,
so this matches the reference implementation rather than taking liberties. The
observable difference is how many times a side-effecting eigenstate runs, and
the side-effect check above confirms those counts now agree with Rakudo's.

### 2. Hoist the per-eigenstate dispatch — DONE (2026-09-19)

Three lines, and not the extraction this plan expected. `sample` over
`5 ~~ any(1 .. 2000)`, as the falsifier demanded, before writing anything:

| | samples | % |
|---|---|---|
| `strlen` (platform + stub) | 1657 | 24.6% |
| `memcmp` (platform + stub) | 1405 | 20.9% |
| `std::operator==<char>` | 929 | 13.8% |
| `isSetOpStr` | 156 | 2.3% |
| `applyArith`'s own frame | 286 | 4.3% |

**59% of the loop was comparing the operator NAME against string literals**, and
not one allocation frame appears in the top eighteen — the opposite of
DISPATCH-PERF-PLAN's profile, so the falsifier did not fire and the work belonged
here.

The cause turned out to be smaller than "extract a kernel". `applyArith` already
opens with a char-dispatched Int/Int fast path — `+ - * < <= > >= == != %` — and
`~~` was simply not in it. So every eigenstate fell through and walked the ~200
lines of operator string-matching below to reach what is an integer comparison.
`Int ~~ Int` is numeric identity (`Any.ACCEPTS` is `===`, which on two plain Ints
is `==`; verified against Rakudo over a grid including negatives, zero and values
past 2**32), so it answers in the fast path. It sits deliberately AFTER the
`valueSmartmatch_` consumption: the collapse arms re-arm that one-shot before each
eigenstate, and an early return that skipped it would leak the flag into the next
operation.

d34120f against this change, both built Release in a worktree, interleaved by
`drive.raku`, best of 3, Rakudo 2026.08 alongside, all three agreeing on every
checksum:

| case | phase 1 | phase 2 | change | rakudo |
|---|---|---|---|---|
| `any` w=2000 no hit | 802.94 | **34.55** | **23.2x** | 93.54 |
| `any` w=2000 hit last | 802.24 | **35.06** | **22.9x** | 104.03 |
| `one` w=2000 single hit | 801.79 | **34.40** | **23.3x** | 273.07 |
| `any` w=2000 hit middle | 401.53 | **17.72** | **22.7x** | 52.17 |
| `grep any(2,4,6)` over 200 | 247.48 | **16.58** | **14.9x** | 77.23 |
| `any` w=200 no hit | 81.08 | **4.14** | **19.6x** | 10.17 |
| `prebuilt any(1,3,5)` | 1.95 | **0.74** | 2.62x | 0.83 |
| `5 ~~ 1\|3\|5` | 2.41 | **1.22** | 1.98x | 0.80 |
| `str any 3-wide` | 2.30 | 2.33 | — | 0.99 |
| `bool any(2000 True)` | 0.58 | 0.57 | — | 0.63 |

As distance from Rakudo, which is what phase 2 set out to close:

| case | gap after phase 1 | gap after phase 2 |
|---|---|---|
| `any` w=2000 no hit | 8.58x | **0.37x — we lead** |
| `any` w=2000 hit last | 7.71x | **0.34x — we lead** |
| `one` w=2000 single hit | 2.94x | **0.13x — we lead** |
| `grep any(2,4,6)` | 3.20x | **0.21x — we lead** |
| `prebuilt any(1,3,5)` | 2.35x | **0.89x — we lead** |
| `5 ~~ 1\|3\|5` | 3.01x | 1.53x |

The ~8x this phase existed to close is gone, and on wide Int junctions we now run
2.7x to 7.9x faster than Rakudo. At the widths real code writes, the gain is about
2x.

Two cases deliberately untouched. Str eigenstates (`$x ~~ any(<a b c>)`) still go
the long way: the fast path is Int/Int, and the Str equivalent needs guards for
allomorphs, enums and hashKind that each risk a miss — it never showed an 8x gap
anyway, sitting at 2.3x, which is this interpreter's general overhead rather than
junction dispatch. And the boolification rows do not move, because they settle on
the first eigenstate and never reach the dispatch.

The obvious next increment is the same trick for Str, measured at ~2x on a 3-wide
string junction; it is a separate change with a separate risk profile.

### 3. List-associative `|`, `&`, `^`

Already designed and agreed — see the `junction-list-assoc` note. Parser
collects a run of the same junction infix into one n-ary construction; the
runtime splice at `Interpreter.cpp:25034` and its twin at
`JsRuntimeSrc.cpp:2349` come out; `rtReduce` at `Interpreter.cpp:16510` grows an
n-ary fold for list-assoc ops, because `[|] 1,3,5` works today only *because*
of the splice.

Two payoffs: the quadratic build goes away (30k builds at width 80: 1.464 s here
against Rakudo's 0.091 s), and `any(1,3)|5` stops over-flattening into
`any(1,3,5)`. At the widths that actually occur it is worth 0.656 us of the
1.922 us in `5 ~~ 1|3|5`.

**Was blocked, probably is not now**: the note this phase came from recorded
another machine's session editing `src/Interpreter.cpp` for the `Nil-Any`
semantics sheet. That work landed as **80b16d1** during the sitting that wrote
this plan — `docs/dev/findings/semantics/Nil-Any.md` plus 260 lines of
`Interpreter.cpp`, 220 of `Builtins.cpp`, 415 of `MethodCallTail.cpp` and 8 of
`Parser.cpp` — and the working tree is clean. Phases 1-3 touch
`Interpreter.cpp`, `Builtins.cpp` and `Parser.cpp`, so confirm that session has
nothing further in flight before starting; the clash it was avoiding is a
commit, not a lock.

### 4. A hash index for wide junctions — NOT NOW

The one real case for a wide junction is membership: `$x ~~ any(@list)` with a
few thousand plain values. Caching a hash of the eigenstates on the junction
turns that into O(1) per match instead of O(W), and it beats anything threading
could do by a wide margin.

It is not in this plan because the precondition is narrow — every eigenstate a
plain Int or Str, topic likewise, so that smartmatch degenerates to equality —
and the junction has to be matched many times for the O(W) build to amortise.
Revisit if a real program shows up wanting it. Phase 1 already covers the early
hit, which is the common half of this case.

### 5. Parallel autothreading — NOT WORTH DOING, AND HERE IS THE ARITHMETIC

Recorded so it does not get re-proposed.

The infrastructure is not the obstacle. `parallelMode_` has been the default
since v3 ([Interpreter.cpp:2969](../../../src/Interpreter.cpp)) — real threads,
no GIL, thread_local execution contexts. What is missing is a pool: `.hyper` and
`.race` are serial stand-ins ([MethodCallTail.cpp:1069](../../../src/MethodCallTail.cpp)),
so a parallel junction would have to bring its own. Call it a day's work.

Measured on this machine:

- **Ceiling.** A CPU-bound fan-out of equal-sized units: 3.14x at 4 threads,
  2.72x at 8 (the efficiency cores do not help). So 3.3x is the most any
  parallel junction could ever return.
- **Floor.** A fan-out/join round trip on a warm pool, written in C++ with no
  interpreter in the picture: 5.65 us at 2 workers, 10.89 us at 4, 16.68 us at
  8. Through the engine's own `start`, 69.8 us at 4.

Break-even width, using the *ideal* 10.9 us pool and today's 397 ns/eigenstate:
W ≈ 39 to break even, W ≈ 139 for a 2x end-to-end win. After phase 2 brings the
eigenstate to 50 ns, the same pool needs W ≈ 313 to break even and W ≈ 1107 for
2x — fixing the serial path makes parallelism *less* attractive, not more.

Against a Roast distribution where 99.2% of junctions are three eigenstates or
fewer. At W=3 the whole serial match is 1.2 us and the pool handoff alone is
10.9 us: the parallel version is ~9x slower at best, ~58x through `start` as it
stands. It would be easy to measure; it would measure as a regression.

Two further reasons, both smaller than the arithmetic. Eigenstates can hold
arbitrary code — a junction of Callables autothreads the invocation
(`Interpreter.cpp:17993`) — and `spawnPromise`'s own comments record a `$/` data
race of exactly that shape, found when v3 made parallel the default. And
short-circuiting fights parallelism directly: phase 1 does one eigenstate's work
where a parallel version does W/N.

## Gates and measurement

`tools/perf-guard.raku` had no junction kernel: every kernel in it was Int,
loop, string, regex or OO work, so a change that made junctions 10x slower would
have passed the gate — the same blind spot that the string kernels (2026-08-09),
the regex kernel (2026-08-27) and the OO kernels (2026-08-30) were each added to
close. Two were added with phase 1 (2026-09-19):

- `junction` — `$x ~~ 1|3|5` in a tight loop, the shape 99% of real junctions
  have. Matches the LAST eigenstate on purpose, so no short-circuit flatters it;
  this is the kernel phase 2's dispatch work has to move.
- `junctionwide` — a prebuilt `any` of 1000, needle in the middle, 2000 matches.
  Guards the short-circuit: remove it and this kernel doubles while nothing else
  moves.

**Both still need `--record` on an idle machine.** They measure (430.9 ms and
412.7 ms, spreads 0.3% and 0.1%) but carry no baseline, so nothing is gated on
them yet.

Per phase: the guard must not regress any existing kernel, the two new kernels
move in the expected direction, `t/run.raku` stays green, and the Roast junction
files hold or improve — `S03-junctions/` is the direct suite, and
`S03-smartmatch/` and `S02-types/` carry the collapse rules. Verify the
short-circuit semantics against Rakudo before committing, on a junction of
side-effecting closures: the observable call count is the contract.

## What this does NOT do

Not the preserved-junction thread at `Interpreter.cpp:25093` — that one has to
visit every eigenstate by definition. Not `Junction.THREAD` or the autothreading
of method calls and routine arguments (`Builtins.cpp:4415`, `:5248`), which are
a different loop with a different contract: they build junctions of results, not
verdicts. Not the JS runtime's `junctionBool` (`JsRuntimeSrc.cpp:2354`), except
for the splice removal that phase 3 already carries — the JS side has its own
performance story and no measurements here.

## What would falsify the design

Phase 2 is the load-bearing bet: that ~350 of the 397 ns is operator dispatch
rather than the comparison itself. The `$a ~~ $b` / `$a == $b` split (844 vs
281 ns, against Rakudo's 161 vs 168) is the evidence, and it is indirect —
it says a plain smartmatch is expensive, not specifically that the *junction
loop's* smartmatch is expensive for the same reason. Profile one junction loop
with `sample` before building the extraction. If the leaf attribution is
dominated by allocation rather than by string compares and `memcmp`, this is
DISPATCH-PERF-PLAN's problem wearing a junction costume, and the fix belongs
there instead.

Second falsifier: if phase 1 does not move the `junctionwide` kernel by roughly
the ratio the position table predicts, the collapse loop is not where the time
goes at that width, and the site inventory above is missing a caller.
