# Plan: a loop's variables stop being boxed while it runs

*Written 2026-09-19, before any code; the results were added after it landed the
same day, and are in "Where it stands". This is P3 of
[JIT-PLAN.md](JIT-PLAN.md) and phase 4 of
[NATIVE-MATH-PLAN.md](NATIVE-MATH-PLAN.md), which named it three weeks earlier
and deferred it: "a `Binary` node whose entire subtree is provably numeric gets
an `evalInt`/`evalNum` that never materialises a `Value` for an intermediate".
[IR-EXPERIMENT.md](../experiments/IR-EXPERIMENT.md) closes by naming the same
thing as the one change that would revive a register IR — "a slot known to hold
a `long long` for the extent of a loop, with a `Value` built only where it
escapes" — and [PADS-PLAN.md](PADS-PLAN.md) landed the frame machinery such a
slot would live in.*

**The numbers a stranger can re-measure**, both from probes taken before any
code, on the benchmark machine (Darwin 25.5, arm64, Apple clang, Release
static libs):

```bash
c++ -std=c++17 -O2 -w -Isrc -Iinclude tools/unbox-probe-int.cpp \
    build/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props,stubs}.a -o /tmp/u1 && /tmp/u1
c++ -std=c++17 -O2 -w -Isrc -Iinclude tools/unbox-probe-num.cpp \
    build/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props,stubs}.a -o /tmp/u2 && /tmp/u2
```

| shape | today's emission | unboxed for the loop | |
|---|---:|---:|---:|
| 50M-iteration integer loop | 141.1 ms | 16.3 ms | **8.6×** |
| 300×260 Mandelbrot, `Num` throughout | 177.2 ms | 2.6 ms | **67.7×** |

The second number is the larger for a reason worth stating at the top: **`-O`
had no float lane at all** before this batch. `laneInt` is integer-only, and
`rtAdd` and its family fast-path `rtBothInt` and fall through to `applyArith`
otherwise — so every floating-point operator in a compiled program was
dispatched on an operator *string* at run time, and a `Num` loop was paying the
fully general path. That is what the 67.7× is measured against, and it is why
the float lane, which looked like the smaller half of this work, turned out to
be the larger one.

## Why this, and not another JIT back end

It is not a JIT feature. The emitter is shared, so **`--exe -O` gets the same
factor on the same day** — and `--exe` is the mode that already works, ships a
binary, and needs no compiler on the user's machine. A stencil back end
(JIT-PLAN P2) would make kernels arrive in microseconds instead of 0.83 s and
would remove the run-time compiler dependency, but it would emit the *same code
quality* as today, because each stencil guards its operands and stores back to
the box exactly as the current lanes do. Latency is worth less than 8–68×.

## What is wrong with today's emission

`-O` already removes the operator-string dispatch and the allocation for
integers. What it does not remove is the **box**. Per statement, every
iteration, the emitted code re-checks a type tag, computes, and writes the
result back into a `Value`:

```cpp
{ bool __lok1 = false; do {                                  // every iteration
    if (!(rtIntBox(v_ss) && rtIntBox(v_si))) break;          //   re-check the tags
    long long __ln0; if (rakupp::add_ovf(v_ss.i, v_si.i, &__ln0)) break;
    if (rtIntSlot(v_ss)) v_ss.i = __ln0; else v_ss = Value::integer(__ln0);
    __lok1 = true;
  } while (0);
  if (!__lok1) { v_ss = rtAdd(v_ss, v_si); } }
```

Nothing in a loop can change a plain scalar's tag if the loop calls nothing. So
the check belongs at the entry, once, and the value belongs in a C++ local for
the loop's extent.

## Design

### The shape emitted

```cpp
{ bool __ok = false;
  do {
    if (!(rtIntSlot(v_si) && rtNumSlot(v_zr))) break;   // ONE guard, at entry
    long long u_i = v_si.i; double u_zr = v_zr.n;       // unbox
    bool __bail = false;
    <the loop, emitted against u_* locals>
    v_si.i = u_i; v_zr.n = u_zr;                        // store back where it escapes
    if (__bail) break;
    __ok = true;
  } while (0);
  if (!__ok) { <today's boxed emission of the same loop> } }
```

The fallback is today's code, unchanged, which is what makes this safe to land
incrementally: anything the analysis cannot prove simply does not get a lane.

### Resuming after a bail is the subtle part

A guard that fails at entry costs nothing — the boxed loop runs from the top.
A bail *mid-loop* (integer overflow) is different: state has already moved, and
re-entering the boxed loop must not redo work. Two rules make it correct:

1. **Store back before falling through.** The boxed loop re-evaluates the
   condition and continues from the state the lane left.
2. **Restore the iteration's starting values first.** A bail can happen halfway
   through a body that has already assigned something, and the boxed loop would
   then re-run that assignment. Each iteration snapshots its slots into shadow
   locals at the top — free, they are registers — and a bail restores them, so
   the boxed loop redoes exactly one iteration from a clean start.

`Num` needs no bail at all: IEEE 754 has no overflow trap, and division by zero
gives `Inf`, which is what this engine's boxed path already answers — so the
lane agrees with it, which is the contract. It is worth recording that **Rakudo
throws `X::Numeric::DivideByZero` there instead**, so `1e0 / 0e0` is a
pre-existing divergence of this interpreter's, visible with no lane in sight and
not introduced here.

### Lane types, and the rule that keeps them sound

Each slot gets one lane type, `I64` or `F64`, inferred by a fixpoint over the
loop body seeded from literals (`1` is `I64`, `1e0` is `F64`) and propagated
through the operators. Then the rule that makes the whole thing safe:

> **A slot's lane type never changes, and every assignment to it must produce
> that same type.** Anything else refuses the lane.

That is what lets the store-back be `v.i = u` or `v.n = u` rather than a
retyping of the variable, and it is why the entry guard can be exact
(`rtIntSlot` / `rtNumSlot`) rather than a coercion. A loop mixing an integer
counter with float accumulators is fine, because the types are per slot.

**`Rat` gets no lane.** `2.0` in Raku is a `Rat`, not a `Num` — exact rational
arithmetic with `BigInt` parts — so `examples/mandel.raku`, written in the
Parrot era with `0.1` and `2.0`, is *rational* code and stays on the boxed path.
Only `2e0` spelling reaches the `F64` lane. Saying so here because the obvious
demo program is the one this does not speed up, and that will look like a bug.

### What may carry a lane, in v1

The loop's condition, step and whole body, made only of: plain `$` scalars with
no twigil that `laneVar` accepts; `Int` and `Num` literals; `+ - * %` and the
comparisons on `I64`; `+ - * /` and the comparisons on `F64`; assignment and its
compound forms to a lane slot; `if`/`elsif`/`else`; nested laneable loops;
unlabelled `last`/`next`.

Nothing else — no call of any kind, no method, no index, no regex, no closure,
no declaration carrying a type or trait. The reason is the same one JIT-PLAN
gives for its whitelist and it is load-bearing here too: **if the loop can call
something, that something can observe the boxed variable**, which is stale while
the lane holds the live value in a register. The JIT's `Scan` already answers
this question for a loop subtree and is the model to follow.

## Where it stands

**Landed 2026-09-19: P0, P1 and P2** — the `I64` lane, the `F64` lane, and
`for A..B` over integer endpoints. Measured on the benchmark machine, minimum
of 5–7 runs, load 2–4 (so these are factors, not percentages). `/usr/bin/time
-p` resolves to 10 ms, so every cell here at 10 or 20 ms is one or two ticks and
re-measures anywhere in that band — the ROW is the result, not the digit. The
probes at the top of this file are the sub-millisecond version of the same two
numbers.

| | interp | `--exe` | `--exe -O` | Rakudo |
|---|---:|---:|---:|---:|
| 50M-iteration integer `while` | — | 230 ms | **10 ms** | — |
| `loopsum` × 60 (`for 1 .. 60_000_000`) | 7 870 ms | 750 ms | **20 ms** | 7 660 ms |
| Mandelbrot 1200×1040, `Num` throughout | 26 510 ms | — | **40 ms** | 7 530 ms |
| 5M-iteration integer `while`, under `--jit` | 1 020 ms | | **20 ms** | |

**`--jit` inherits all of it**, which is the last row: a JIT kernel is emitted by
this same code, so a `while` loop tiered up at run time went from 50 ms to 20 ms
the moment the lanes landed — 51× the interpreter, in process, with no build
step. (A C-style `loop` kernel does not yet: `emitJitKernel` writes that loop's
header itself, to drop the init the interpreter has already run, so it does not
pass through the `stmt()` case the lane hooks into. Worth closing, and small.)

The integer loop lands on the hand-written C kernel's 10 ms. `loopsum` is 37×
its own unoptimized compile and 383× Rakudo. The Mandelbrot is 663× the
interpreter and 188× Rakudo, and every one of them prints what Rakudo prints.

### The finding that shaped the batch, and it is not a good one

After P0 and P1 the pass fired on **one** of the fifteen benchmark kernels and
on **zero of 701** programs in `examples/` and `t/regression/`. The reason is
banal: real Raku is written with `for`, not `while`. Across that corpus, 377
files have a `for`, 126 a `while`, 8 a C-style `loop`, and nearly every
benchmark kernel is a `for`. A pass that skipped it would have measured
beautifully and done nothing, which is exactly the kind of result that gets
believed because the number is large.

So P2 came forward into this batch. With it, `loopsum` and `bigint` lane, and
the corpus is **still 1 of 701** — because a test file's loop body prints on
every iteration, and the no-calls rule refuses it. That is the honest ceiling
of this design, and it is the right one to state: **the pass is for arithmetic
loops, and a loop that calls something on every iteration is dominated by the
call, not by the boxing.** Widening past it means letting a lane survive a call,
which is P3 below and a different kind of problem.

### Gates

- **`t/exe/run.raku`, new** — the optimizer differential, over all 702 programs
  of `t/regression/` and `examples/`: **666 agreeing, 4 differing, 27 refused**
  (the backend fell back to bundling), 5 skipped. All four differences carry
  **zero lanes** and reproduce on a build without this pass; they are
  [findings/EXE-O-DIVERGENCES-2026-09-19.md](../findings/EXE-O-DIVERGENCES-2026-09-19.md).
  Every program the native backend accepts is compiled twice, with `-O` and
  without, and the two BINARIES are compared with each other rather than with
  the interpreter.
  That axis is the point: every legitimate difference between compiled and
  interpreted code — `$*EXECUTABLE` being the binary, a spawned child, `EVAL` —
  is present in both lanes and cancels, and what is left is the optimizer. The
  first draft of this gate compared against the interpreter and produced 122
  failures, none of them the optimizer's.
### Eight bugs in this pass, and what found each one

Worth listing in full, because the ratio is the useful part: **three by reading
the code back, one by the compiler, four by tests** — and of those four, the two
that mattered most were found by tests that had to be GENERATED, because the
shapes they need appear nowhere in this repository.

| # | the bug | found by |
|---|---|---|
| 1 | the bail on overflow used `break`, which only leaves the INNERMOST loop — the ones outside carried on with a value the lane had already abandoned | reading it back |
| 2 | the `if`/`elsif` chain emitted unbalanced braces | reading it back |
| 3 | `next` in a C-style loop would skip the step, where a three-clause `for` runs it | reading it back |
| 4 | the scope the lane opened was never closed | the C++ compiler, on the first build |
| 5 | a `my` in a loop HEADER is scoped to the enclosing block, not to the loop, so `loop (my $n = 0; …); say $n` printed the count interpreted and nothing compiled | `t/exe/run.raku`, its first run |
| 6 | a bare `Num` in boolean context was asked for as an integer, and `(long long)0.25` is 0 — `while $f` with `$f = 0.25` ran zero times compiled and ten interpreted | hand-written stress cases |
| 7 | `$x %= $y` on two `Num`s emitted `double % double`, which C++ refuses, taking the whole program's native build down | the generated operator matrix |
| 8 | a NESTED loop's header `my` was written back to a C++ name that no longer existed, because the declaration belonged to an emission the lane had replaced | the generated shape matrix |

Bugs 6 and 8 are the ones to take seriously, and neither appears in any corpus
this repository has: 6 needs a float strictly between 0 and 1 used as a truth
value, 8 needs one C-style loop inside another. Both were found by generating
the cases rather than by collecting them, which is the argument for the fuzz
gate existing at all. The operator matrix is 82 programs and the shape
matrix 100; both are cheap to regenerate and neither is checked in, because what
they found is now in `t/regression/unboxed-loop-lanes.raku` as 36 laned
assertions.
- **`t/regression/unboxed-loop-lanes.raku`, new** — 36 laned assertions, each
  one a rule the lane has to get right, checked against values Rakudo prints.
  It passes interpreted, compiled, compiled with `-O`, and under `--jit`.
- `t/run.raku` **1049 checks, the same 4 pre-existing failures**; `t/jit/run.raku`
  **710 agreeing, 0 differing** (kernels come off the same emitter);
  Roast **737 / 1464 files**, unchanged — the interpreter does not run `Codegen`
  at all, so that sweep is a control.

### One defect found and NOT caused by this work

`my int8` is ignored by the native backend entirely: 300 increments answer 44
interpreted and under Rakudo, and 300 compiled. It fails with `-O` and without,
and the unoptimized lane never reaches this pass. Native container widths are
simply not honoured in `Codegen`, and `t/exe/run.raku` is what surfaced it.

### `t/exe/fuzz.raku`, and why a generated gate rather than a corpus

The corpus cannot test this pass: one of its 701 programs carries a lane. So
the cases are generated, and the generator is checked in rather than run by
hand, because two of the seven bugs above exist in no corpus anywhere.

| matrix | programs | carrying a lane | what it varies |
|---|---:|---:|---|
| operators | 82 | 16 | every binary, compound and unary operator × every Int/Num operand pairing, plus four truth-value shapes |
| shapes | 100 | 76 | five loop kinds × ten body shapes, each also nested one level inside itself |

Both matrices pass: 182 programs, 0 mismatching, 0 failing to compile.

Both run the program interpreted and compiled with `-O` and compare. A program
that fails to **compile** counts as a failure, not a skip: the lane emits C++,
and emitting C++ that does not build takes the whole native build down with it —
which is exactly what bug 6 did.

It also reports how many programs carried a lane at all. The rest exercise the
refusal path, which is the half of this pass that has to be right for a wrong
answer never to appear.

Every run is under a wall-clock bound, and a generated program that does not
terminate is reported as a failure of THIS FILE. That is not hypothetical: the
first generator wrote `while` bodies whose `next` skipped the increment at the
bottom — correct Raku, an infinite loop — and three of them span for half an
hour before anyone noticed, because a hang and a slow compile look identical
from outside. A generator that can hang is worse than no generator.

### The gate this needed and does not have

`t/exe/run.raku` compares the two compiled lanes with each other, which is what
isolates the optimizer — and it therefore **cannot see a bug that is in both**.
Exactly such a bug turned up while shape-fuzzing, and it is not this pass's:
a nested loop that redeclares a name shares one C++ variable, so the outer loop
runs once and the answer is silently a fraction of the right one, with `-O` and
without. It is written up in
[findings/EXE-O-DIVERGENCES-2026-09-19.md](../findings/EXE-O-DIVERGENCES-2026-09-19.md)
and filed as its own task. Catching that class needs a compiled-against-
interpreted comparison on a corpus that contains the shape, and the naive
version of that comparison produces 122 false positives — see the header comment
in the gate for why.

## Phases

| | | |
|---|---|---|
| **P0** | the `I64` lane: analysis, fixpoint, guards, entry snapshot and bail, store-back, fallback | **DONE** |
| **P1** | the `F64` lane — the first floating-point lane the compiler has ever had | **DONE** |
| **P2** | `for` over a Range, which is the most common loop in real code and is not a `while` | **DONE** |
| **P3** | let a lane survive a call to a lane-able user sub, by inlining it — the only route past 1-of-701 | next |
| **P4** | declared natives (`my int`) honoured as lanes — NATIVE-MATH phase 3. Note `my int8` is not honoured by the backend AT ALL today | later |
| **P5** | `Rat` — `2.0` is a Rat and gets no lane, so the Parrot-era Mandelbrot in `examples/` is untouched by all of this | later |

## Gates

The standing ones, plus two this plan needs because it changes generated code
rather than adding a mode:

1. **`t/exe/` differential** — every program that `--exe` compiles, run compiled
   and interpreted, output compared byte for byte, with `-O` and without. A
   lane that computes a different answer from the boxed path is the failure
   mode this work has, and it is invisible to any test that only runs one of
   them.
2. **`t/jit/run.raku` unchanged**, since kernels are emitted by the same code
   and every JIT case is a loop.
3. `perf-guard --check`, `t/run.raku`, and a full Roast sweep.

The number to report at the end: the two probe rows above, re-measured against
the real emitter rather than a hand-written stand-in, plus `BENCHMARKS.md`'s
`native` column on the numeric kernels.
