# Plan: native math — and the three things measured to be worth more first

*Written 2026-08-31, before any code. Started from a question that had no code
attached to it: "we should somehow switch to native types automatically — assume
a variable that started as an integer stays numeric, and in the AST or the
optimisation step stop using our massive `Value` until something forces us to."*

> **Revisited 2026-09-06 — phase 1 is landed, and it was all of §1.** The
> sentinel was flipped and the page re-measured. The diagnosis held exactly:
> `for ^200000 { next if $_ % 2 }` went 4.60 s → **0.03 s**, landing on the
> 0.03 s of the `unless` rewrite this section used as its floor, and the
> neighbour §1 predicted — `given`/`when` — carried the identical defect, 4.70 s
> → **0.10 s**. Phases 2, 3 and 4 remain open and the numbers that order them
> have moved; see *The revisit* below, which re-prices every section of this
> plan. Everything in that section was measured on the benchmark machine of
> record (Darwin 24.6, arm64, `build-arm64`), which is **not** the box the
> tables above were taken on — compare the ratios, never the seconds.

The idea is well founded, and this repository predicted it.
[IR-EXPERIMENT.md](../experiments/IR-EXPERIMENT.md) closes by naming exactly one
thing that would revive a register IR — *"unboxed typed registers: a slot known
to hold a `long long` for the extent of a loop, with a `Value` built only where
it escapes"* — and [PADS-PLAN.md](PADS-PLAN.md) has since landed the frame
machinery such a slot would live in.

But the question was "where should native math go in the queue", and the queue
is decided by measurement, not by which idea is most interesting. So the
profiles were taken first. **They moved native math from first to fourth**, and
found a control-flow defect worth more than everything else on this page
combined.

Measured 2026-08-31 on Darwin 25.5, arm64, `build-rel/rakupp`
v3.23.0-1-g18b3151-modified, clang 21.0.0, against Homebrew `raku`. These are
`/usr/bin/time -p` best-of-2/3 runs on a working machine, **not** the
quiet-machine protocol the BENCHMARKS tables use — good to a factor, not to a
percent. Every number below is a factor.

---

## 1. Every unlabelled `next`/`last` in a MAINLINE loop throws

**Fixed 2026-09-06 — this section is history, not a live defect.** The tables
below are what it cost; the sentinel and what removing it bought are in
*The revisit* and phase 1 below.

| | rakupp | Rakudo |
|---|---:|---:|
| `for ^200000 { next if $_ % 2; $n = $n + 1 }` | **8.1 s** | 0.14 s |
| the same written `$n = $n + 1 unless $_ % 2` | 0.07 s | 0.15 s |

100,000 `next` executions account for ~8 s: **~80 µs each**, ~55× Rakudo on the
whole program. A `sample` profile is the throw path and nothing else —
`__cxa_throw` → `_Unwind_RaiseException` → `__gxx_personality_v0` →
`readEncodedPointer` (481 of ~700 non-idle samples), with dyld
`findImageMappedAt` / `forEachSection` / `forEachLoadCommand` underneath it.

### The cause is a sentinel collision, and it is already half fixed

There is already a cooperative, non-throwing path for this. `ExecContext` carries
`loopCtl` and `curLoopFrame` ([Interpreter.h:605-609](../../../src/Interpreter.h)),
and the comment a few lines below it already records the motivation: *"on macOS a
C++ throw walks dyld unwind info under a lock, ~tens of µs each."* The mechanism
was known. What was not is who falls outside the path.

The loop runner arms it with

```cpp
tctx_.curLoopFrame = tctx_.frameTop;          // Interpreter.cpp:6608
```

and all five `next`/`last`/`redo` sites gate on the same pair of conditions

```cpp
tctx_.curLoopFrame != 0 && tctx_.frameTop == tctx_.curLoopFrame
// Interpreter.cpp:8691, :8698, :8705 (each also requiring an empty label), :23879, :27893
```

`frameTop` counts `callCallableRaw` activations (`FrameGuard`,
[Interpreter.cpp:13974](../../../src/Interpreter.cpp) and :14825), so **at
mainline it is 0**. `curLoopFrame != 0` is meant to read "some native loop is
active", but 0 is also the mainline's own legitimate frame number. The liveness
sentinel and a real frame id share a value, and the mainline loses.

Three measurements say that is the whole story:

| | rakupp |
|---|---:|
| `for ^20000 { while True { $n = $n + 1; last } }` | 1.14 s |
| the identical loop body moved into `sub f()`, called from that `for` | **0.01 s** |
| `while $j < 20000 { while True { …; last }; $j = $j + 1 }` — all mainline | 1.17 s |

Inside a routine `frameTop ≥ 1`, the guard passes, the flag path runs, and the
cost disappears — ~100× on the same body. Nothing about the loop shape matters;
only whether a routine boundary happens to sit above it.

Cost also grows with each block between the `next` and its loop, which is what a
catch-and-rethrow per block level looks like:

| blocks between `next` and the loop | user |
|---|---:|
| none | 8.08 s |
| one `if` | 11.47 s |
| two `if`s | 13.80 s |

~+30 µs per intervening block, on top of an ~80 µs floor.

### Why this is the top item

Scripts are mainline. `next if …` is everyday Raku. This is not a slow path, it
is a path that was built and then missed by a `!= 0`, and the defect is invisible
in any benchmark whose hot loop lives inside a `sub` — which is most of
`perf-guard`, and which is why it survived.

---

## 2. A plain integer loop does not spend its time on boxing

`while $i < 5_000_000 { $s = $s + $i; $i = $i + 1 }`, 1.10 s interpreted.
`sample` leaf attribution, 3,367 main-thread samples:

| | share |
|---|---:|
| `_tlv_get_addr` + `__tls_init` — thread-local access | **36%** |
| `execBlock` + `runLoopBody` + `runEnterPhasers` + `runLeavePhasers` + `hoistSubs` + `hoistExprDecls` | **16%** |
| `exec` + `eval` + `evalAssign` | ~19% |
| `applyArith` + `Value::operator=` — the arithmetic and the box | **7%** |
| malloc family | absent from the list |

Two things follow. The **36%** is macOS TLV being a function call rather than a
register offset, over 47 `thread_local`s in Interpreter.cpp; the campaign
recorded in the git log ("one thread-local resolution per block and per lvalue,
instead of eighty-four") cut into it and there is clearly more there. The
**16%** is the block machinery re-deciding, on every iteration, facts about a
statement list that cannot change — which phasers exist, which subs to hoist.
That is the same *decide-once* shape
[NODE-SPECIALIZATION.md](../../internals/NODE-SPECIALIZATION.md) already uses
for `fastShape`, applied to blocks instead of expressions.

And a perfect native-math pass on this loop caps out at that **7%**. The
representation campaign
([REPRESENTATION-PLAN.md](REPRESENTATION-PLAN.md), `sizeof(Value)` 392 → 128)
already took boxing off the top of this profile. Native math is now chasing what
is left, not what dominates.

---

## 3. `my int $x` is a semantic flag, not a representation

| 5M-iteration loop | rakupp | Rakudo |
|---|---:|---:|
| `my $s` / `my $i` | 1.10 s | 1.36 s |
| `my int $s` / `my int $i` | 0.97 s | **0.29 s** |

Boxed, rakupp is *ahead*. Declared native, Rakudo is 3.4× ahead, because it
actually unboxes and we do not: `natBits`/`natSigned`/`natFloat`
([Value.h:452-455](../../../src/Value.h)) only make assignment wrap at the
declared width. The value stays a full `Value`.

Worth stating plainly because it inverts the usual order of this kind of work:
before inferring a type nobody wrote, honour the one the user *did* write. It
needs no analysis, no guard, no deopt, and no new semantics — the user opted in
and we currently charge them for it without paying anything back.

---

## 4. What that leaves for native math, bounded

The honest estimate of the prize, from the arithmetic-densest thing measured — a
Mandelbrot inner loop, `Num` literals throughout, 130×150 grid:

| | rakupp | Rakudo |
|---|---:|---:|
| with `last` | 2.09 s | 0.45 s |
| escape folded into the loop condition (no `last`) | **0.71 s** | 0.48 s |

The first row looks like a 4.6× arithmetic deficit. It is not: it is §1. With
the throw removed, float-dense numeric code is **~1.5×** off Rakudo, and *that*
1.5× is what native math is competing for. The full-size version of the same
program is 32.4 s against Rakudo's 3.7 s — 8.7× — and that headline number is
almost entirely `last`, which is exactly the trap this section exists to avoid
walking into.

One more bound, from the other direction. `--exe -O` runs the §2 loop at ~5
ns/iteration against the interpreter's ~220 — and it does that **while still
boxing every value into a `Value`**. (Checked for a folded loop: 10× the
iterations costs 12× the time, 0.02 s → 0.25 s. It really runs.) Its whole
advantage is that its intermediates are non-escaping C++ locals. Which is
IR-EXPERIMENT §2's finding restated: the escape property is worth far more than
the unboxing, and it is a property the tree-walker **already has** and any
register file loses.

---

## The revisit, 2026-09-06 — what the numbers say now

Measured on the benchmark machine of record (Darwin 24.6, arm64, Release/clang,
`build-arm64`), best-of-3 `/usr/bin/time -p`, against Rakudo 2025.x from
`/usr/local/bin/raku`. **Before** is `build/rakupp` at v3.25.0-55-gabd3d11 —
five commits behind the fix and otherwise the same tree; **after** is that tree
plus the phase-1 patch. The tables earlier in this plan came from a different
box, so their ratios carry over here and their seconds do not.

### §1 — the sentinel, before and after

| kernel | before | after | Rakudo |
|---|---:|---:|---:|
| `for ^200000 { next if $_ % 2; $n = $n + 1 }` | 4.60 s | **0.03 s** | 0.18 s |
| the same written `unless` — §1's floor | 0.02 s | 0.03 s | 0.18 s |
| `next` behind one `if` | 6.61 s | **0.05 s** | 0.19 s |
| `next` behind two `if`s | 8.66 s | **0.07 s** | 0.19 s |
| `for ^20000 { while True { …; last } }` | 0.57 s | **0.00 s** | 0.16 s |
| the same body inside a `sub` — always worked | 0.01 s | 0.01 s | 0.16 s |
| mainline `given`/`when`, 100k rows | 4.70 s | **0.10 s** | 0.15 s |
| the same inside a `sub` — always worked | 0.11 s | 0.11 s | 0.14 s |

Three things to read off it. The everyday idiom is now **6× faster than
Rakudo** where it was 25× slower. The per-block surcharge went with the throw:
one `if` between the `next` and its loop cost 2.01 s before and 0.02 s now.
And the two in-routine rows do not move, which is the control — the patch
changes which frames the cooperative path accepts, not what that path does.

### §2 — re-profiled, and the order inside it has changed

The same 5M-iteration boxed loop, `sample`, leaf attribution, 3,123 non-idle
samples (the parked worker's `__ulock_wait` excluded):

| | 2026-08-31 | today |
|---|---:|---:|
| `_tlv_get_addr` + `__tls_init` — thread-local access | 36% | **20.4%** |
| `execBlock` + `runLoopBody` + the phaser and hoist re-scans | 16% | **28.0%** |
| `exec` + `eval` + `evalAssign` | ~19% | 17.4% |
| `applyArith` + `Value::operator=` — the arithmetic and the box | 7% | 7.7% |

The thread-local campaign since 2026-08-31 took that share from 36% to 20%, and
in doing so promoted the other half of phase 2: **the per-iteration re-scan of a
statement list is now the largest single item on this page** — nearly four times
what a perfect native-math pass could win on the same loop. What it re-decides
every iteration (which phasers exist, which subs to hoist, whether a statement
is a phaser) is a static property of the block.

### §3 — unchanged, and now the biggest declared gap

| 5M-iteration loop | rakupp | Rakudo |
|---|---:|---:|
| `my $s` / `my $i` | 0.63 s | 0.65 s |
| `my int $s` / `my int $i` | 0.64 s | **0.18 s** |

Boxed, parity. Declared native, Rakudo is 3.6× ahead — the same inversion §3
recorded, with nothing yet done about it.

### §4 — re-priced, and the prize is smaller than the bound

§4 estimated its prize by subtracting §1 from a Mandelbrot: *"with the throw
removed, float-dense numeric code is ~1.5× off Rakudo, and that 1.5× is what
native math is competing for."* With the throw actually removed it can be
measured instead. The kernel is 130×150, `Num` literals throughout (`2e0`, not
`2.0` — see the trap below), written at the mainline; both variants print the
same 262608 on all three engines:

| | before | after | Rakudo |
|---|---:|---:|---:|
| escape tested with `last` | 1.14 s | **0.31 s** | 0.27 s |
| escape folded into the loop condition, no `last` | 0.33 s | 0.35 s | 0.32 s |

The two programs, in full — `mandel-cond` subtracts the one extra iteration the
fold costs, which is why both print 262608:

```raku
# mandel-last.raku                          | # mandel-cond.raku (differences only)
my $w = 150; my $h = 130; my $sum = 0;
loop (my $y = 0; $y < $h; $y++) {
    my $ci = $y * 4e0 / $h - 2e0;
    loop (my $x = 0; $x < $w; $x++) {
        my $cr = $x * 4e0 / $w - 2e0;
        my $zr = 0e0; my $zi = 0e0; my $k = 0;    # + my $esc = 0;
        loop (; $k < 112; $k++) {                 # loop (; $k < 112 && $esc == 0; $k++) {
            my $t = $zr*$zr - $zi*$zi + $cr;
            $zi = 2e0*$zr*$zi + $ci;
            $zr = $t;
            last if $zr*$zr + $zi*$zi > 1e1;      # $esc = 1 if $zr*$zr + $zi*$zi > 1e1;
        }
        $sum = $sum + $k;                         # $sum = $sum + ($esc == 1 ?? $k - 1 !! $k);
    }
}
say $sum;
```

Float-dense numeric code is at **1.15× Rakudo**, not the ~1.5× the estimate
bounded it at — and `last`, which used to cost 3.5× the whole program, is now
the *faster* way to write the loop, as it always should have been. Phase 4 does
not become worthless: 15% of the arithmetic-densest shape in the language is
real. But it is now the smallest of the four items here, and it stays last.

### What the revisit changes about the order

Phase 1 is done. The three that remain keep their order, and the reason is
firmer than it was: phase 2 is now 48% of the §2 loop between its two halves
(with the block re-scan the larger), phase 3 is a declared 3.6× that needs no
analysis and no deopt, and phase 4 competes for the 15% left over after both.

---

## The phases

Ordered by measured value, which is not the order the question arrived in.

### Phase 1 — the mainline loop-control sentinel — **DONE 2026-09-06**

Give "no native loop is active" a value that is not also a real frame id.
Landed as `ExecContext::kNoFrame` (`~uint64_t(0)`,
[Interpreter.h:738](../../../src/Interpreter.h)): `curLoopFrame` and
`curGivenFrame` start there, the two sites that disarm a `given` by hand set it
instead of 0, and the six guards drop their `!= 0` half to test
`frameTop == cur*Frame` alone. `frameTop` is a depth that `FrameGuard` restores,
so no real frame can ever hold the sentinel.

**The falsification this section asked for.** The requirement was that
`for ^200000 { next if $_ % 2 }` land near the `unless` rewrite, or the
diagnosis be wrong. It lands on it: 4.60 s → 0.03 s, against the rewrite's
0.03 s. Full table in *The revisit* above.

**The neighbours, as predicted.** `givenCtl`/`curGivenFrame` had the identical
defect — a mainline `when` cost 4.70 s where the same code inside a routine cost
0.11 s — and the same sentinel fixes it. `returning`/`curRoutineFrame` is sound
by construction and was left alone: a routine activation always has
`frameTop >= 1`, so 0 cannot collide with one, and a `return` at the mainline is
an error whose throw is on no hot path.

**What it cost elsewhere: nothing.** The guards now do one comparison where they
did two, and the two "inside a sub" rows in the revisit table do not move.

**Gates run.** `t/run.raku` **776/776**. Roast **657 files** in the 8-worker
run, with all five files that the recorded v3.25.0 list has and this run does
not — `S17-scheduler/basic.t`, `S17-scheduler/times.t`, `S29-context/exit.t`,
`S32-io/note.t`, `integration/99problems-31-to-40.t` — passing when re-run
alone, against 11 newly passing: the parallel run's usual timing flappers, and
the reason the release procedure runs a gate alone. `t/stress` **26 / 26, 0 new
failures**. And a 35-case control-flow differential against Rakudo — mainline
and in-routine `next`/`last`/`redo`/`when`, nested loops, labels, `EVAL`,
`gather`, `react`, `try`, `LEAVE`/`NEXT`/`FIRST`/`LAST` phasers — whose
disagreements are **the same before the patch and after it**. (Four are the
probe's own missing semicolons, which rakupp accepts and Rakudo does not. One is
worth its own entry and is not this batch's: `EVAL q[last]` inside a loop breaks
the loop under Rakudo and dies here with "last without a supporting loop
construct", before and after.)

**Gate added**, because the absence of one is why this lasted: `mainnext` and
`mainwhen` in `tools/perf-guard.raku` — a `next` and a `when` in a **mainline**
loop, 200k iterations each. Every existing perf-guard kernel that carries
control flow runs inside a `sub`, where this path always worked, and the four
that are mainline (`asg`, `loopsum`, `hash`, `rats`) carry no control flow at
all, so the whole class was invisible. Both are ungated until
`perf-guard --record` runs on the idle machine `perf-baseline.raku` names;
today's readings are `mainnext` 31.4 ms and `mainwhen` 206.0 ms, taken on a box
the gate itself declined to record on (load 2.3).

### Phase 2 — the 36% and the 16%

**Re-measured 2026-09-06: 20.4% and 28.0%, and the two have swapped places** —
the thread-local campaign since spent most of the first, and the block re-scan
is now the largest single item in the profile. The name of this phase is kept
as it was written; the numbers are in *The revisit* above.

Thread-local access, and the per-iteration re-scan of a block's statement list.
Both are work *removal*, the only category this codebase's history says has ever
paid ([PERF-CAMPAIGN.md](../experiments/PERF-CAMPAIGN.md)). Both are independent
of everything else here.

### Phase 3 — honour declared natives

`my int`/`uint`/`num` get real unboxed storage in the pad. Bounded, opt-in,
no inference, and it is the cheapest way to find out whether the pad machinery
can carry a typed slot at all — which is the load-bearing question for phase 4.

### Phase 4 — typed expression evaluation

The actual native-math change, and deliberately **not** typed storage.

A `Binary` node whose entire subtree is provably numeric gets an
`evalInt(Expr*) → long long` / `evalNum(Expr*) → double` recursive descent that
never materialises a `Value` for an intermediate — only at the root, where the
result escapes. The verdict is recorded on the node in the `fastShape`
decide-once style already established. Guards live at the **leaves**: check the
tag on the `Value*` just fetched from the pad, bail to the general path on
mismatch.

It scales with expression depth, which is where real numeric code lives:
`$zr*$zr - $zi*$zi + $x` is five nodes and four intermediate `Value`s today.
`$s = $s + $i` is already served by `fastShape` and would gain nearly nothing —
so measure phase 4 on the Mandelbrot kernel, never on `loopsum`.

## Why typed expressions and not typed slots

Typed pad slots are the obvious reading of "use native types until something
forces us not to", and they are the wrong shape here for two independent
reasons.

**Escape analysis.** A pad slot is addressable storage by construction — that is
what `MY::`, `callframe` and lexical `EVAL` require of it. IR-EXPERIMENT §2
measured the tax on parking interpreter intermediates in storage the optimiser
cannot reason about at **11.2 ns/node**, against 0.28 ns for the dispatch such a
scheme saves. Expression-local intermediates stay non-escaping allocas and pay
neither.

**Speculation needs deopt.** "A variable that started as an `Int` stays numeric"
is true of nearly all real programs, and is still a *speculation* — Raku lets
`EVAL`, `MY::`, a closure capture, an `is rw` binding or a `$_` alias write a
slot from outside any analysis. Speculating on **storage** obliges you to build
a deopt path, i.e. a JIT. Speculating on the **operation** does not: read the
tag at the point of use, take the branch, and a mispredict costs one general-path
evaluation instead of a bailout. Guard-per-use, not type-state-per-variable, is
what makes this implementable in a tree-walker at all.

## Two Raku-specific traps

**`Int` is arbitrary precision.** A native `int64` add still needs its overflow
check and its promotion to `BigInt`; `rtAdd` already has one. Phase 4 removes
the *box*, not the check — anyone estimating the win as "C speed" is estimating
the wrong thing.

**`2.0` is a `Rat`, not a `Num`.** The first Mandelbrot run here took 46.3 s
purely because its literals were decimal, and rewriting them as `2e0` took it to
32.4 s with nothing else changed — a 1.4× swing from literal syntax alone. A
"native float" pass that pattern-matches on decimal literals will find that most
of them are exact rationals with a `gcd` per operation. Rat's 64-bit fast paths
(REPRESENTATION-PLAN.md phase 2, `Value::rat` 9,239 ns → 484 ns) are why 46 s is
not far worse, and any numeric-typing pass has to decide what it does about
`Rat` before it can claim a Raku numeric workload.

## Reproducing

```sh
# §1 — the loop-control throw
raku -e 'my $n = 0; for ^200000 { next if $_ % 2; $n = $n + 1 }; say $n'
rakupp -e 'my $n = 0; for ^200000 { next if $_ % 2; $n = $n + 1 }; say $n'
# the same body inside a sub, which takes the cooperative path:
rakupp -e 'my $n = 0; sub f() { while True { $n = $n + 1; last } }; for ^20000 { f() }; say $n'
# vs at mainline, which throws:
rakupp -e 'my $n = 0; for ^20000 { while True { $n = $n + 1; last } }; say $n'

# §2/§3 — the integer loop, boxed and declared-native
rakupp -e 'my $s = 0; my $i = 0; while $i < 5_000_000 { $s = $s + $i; $i = $i + 1 }; say $s'
rakupp -e 'my int $s = 0; my int $i = 0; while $i < 5_000_000 { $s = $s + $i; $i = $i + 1 }; say $s'

# profiles (macOS): run in background, sample the pid
sample <pid> 4 1 -f /tmp/prof.txt && awk '/^Sort by top of stack/,0' /tmp/prof.txt
```

Re-measure on a quiet machine before trusting any of this to a percent; the
`sample` shares are leaf attribution and over-credit very hot small leaves.

## Reproducing the revisit

```sh
# §1, before and after. `build/rakupp` is the pre-patch binary in the tables above.
for b in build/rakupp build-arm64/rakupp; do
  /usr/bin/time -p $b -e 'my $n = 0; for ^200000 { next if $_ % 2; $n = $n + 1 }; say $n'
  /usr/bin/time -p $b -e 'my $n=0; for ^100000 -> $i { given $i % 3 { when 0 { $n++ } when 1 { $n += 2 } default { $n += 3 } } }; say $n'
done

# §2 — the profile. Scale to 50M so there is something to sample, and drop the
# parked worker's __ulock_wait: it is not the main thread.
build-arm64/rakupp -e 'my $s = 0; my $i = 0; while $i < 50_000_000 { $s = $s + $i; $i = $i + 1 }; say $s' & \
  sample $! 4 1 -f /tmp/prof.txt >/dev/null; awk '/^Sort by top of stack/,0' /tmp/prof.txt

# §4 — the float-dense kernel, 130x150, Num literals, AT THE MAINLINE. Both
# variants must print 262608; if they do not, the fold changed the algorithm.
```

Both §4 programs are printed in full in the revisit's §4 above.
