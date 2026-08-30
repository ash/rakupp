# Plan: native math — and the three things measured to be worth more first

*Written 2026-08-31, before any code. Started from a question that had no code
attached to it: "we should somehow switch to native types automatically — assume
a variable that started as an integer stays numeric, and in the AST or the
optimisation step stop using our massive `Value` until something forces us to."*

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

## The phases

Ordered by measured value, which is not the order the question arrived in.

### Phase 1 — the mainline loop-control sentinel

Give "no native loop is active" a value that is not also a real frame id: a
separate `bool`/depth counter, or `curLoopFrame` initialised to `UINT64_MAX`
with the five guards testing that instead of `!= 0`. Five call sites and one
arming site.

**Not yet verified by a patch.** The diagnosis is a code read plus the
mainline-vs-routine A/B above, and it should be falsified before it is built:
flip the sentinel, re-run `for ^200000 { next if $_ % 2 }`, and require it to
land near the 0.07 s of the `unless` rewrite. If it does not, the cause is
elsewhere and this section is wrong.

Then check the neighbours, which are armed from the same two lines and so are
likely to share the defect exactly: `givenCtl`/`curGivenFrame` (a mainline
`given`/`when` — the header comment at Interpreter.h:614 calls this "the
hot-path shape"), and `returning`/`curRoutineFrame`, which is gated on a routine
boundary and so may be sound by construction. A mainline `when` costing 80 µs
per match would be the same defect wearing a different name.

Gate: `t/run.raku`, and a `perf-guard` kernel that puts a `next` in a **mainline**
loop — there is currently none, which is why this lasted.

### Phase 2 — the 36% and the 16%

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
