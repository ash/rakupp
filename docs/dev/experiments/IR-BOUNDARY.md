# The IR boundary — what an interpreter IR would actually cost and buy

*2026-08-08. Phase I0 of [IR-PLAN.md](IR-PLAN.md), run before any
opcode was written. It changed the plan. Nothing was built.*

The plan's design rested on an escape hatch: an `EVAL_NODE <const Expr*>`
instruction that runs today's tree-walker on any subtree the lowerer does not
understand, so the IR never has to be complete. I0 existed to price that
crossing before anything depended on it — and, while the instrument was there,
to price what lowering a node actually saves.

Measured with [`tools/ir-boundary.cpp`](../../../tools/ir-boundary.cpp) against
the real `Value`, `Env` and `applyArith`; node counts from a
`-DRAKUPP_NODE_COUNT` build (the counters are compiled out otherwise).
Methodology as in [BENCHMARKS.md](../../status/BENCHMARKS.md): 7 reps, first
discarded, minimum reported; every figure below reproduced across three runs.

**Build on arm64.** `cmake -S . -B build` on this machine is driven by an
x86_64 cmake under Rosetta and silently produces an x86_64 binary that runs
~2× slower (`fib` 1.38 s vs 0.70 s). `perf-guard` already refuses that
mismatch; `tools/ir-bench.raku` now refuses it too. Every number here is
arm64.

## 1. Opcode dispatch is free. The register store is not.

| shape | ns/op |
|---|---:|
| A — `Value v = eval(node)` into a fresh local (today) | 5.6 |
| B — dispatch + `regs[d] = eval(node)` (a register VM) | 17.3 |
| D — the opcode `switch` alone | **0.28** |
| C — the register store alone, no dispatch | 17.2 |

The crossing costs **+11.2 to +11.8 ns per un-lowered node**, and essentially
none of it is dispatch. The premise "a flat instruction sequence beats
re-dispatching a tree" is worth **0.28 ns**.

### Where the 11.5 ns comes from — and it is not the move

Three candidate causes were separated, because they lead to different designs:

| variant | tax vs A |
|---|---:|
| E — materialise a temporary, then move it into the slot | +12.1 ns |
| H — destroy the dead slot, then construct in place (heap/static memory) | +11.2 ns |
| A2 — move-assign into a **live stack local** | +11.5 ns |
| **A3 — destroy + construct in place, in a stack buffer** | **−0.01 ns** |

A3 and H run the *same code shape*. The only difference is where the slot
lives — a stack buffer whose address never escapes, versus memory the optimiser
cannot reason about. A3 is free; H costs 11.2 ns.

So the tax is not the move, not the assignment, and not the store discipline.
It is **escape analysis**. A tree-walker's intermediates are non-escaping
allocas, and LLVM elides most of the work of constructing and destroying a
376-byte `Value` with 5 strings and 11 `shared_ptr`s. A VM's register file
cannot have that property: it is reachable from the interpreter *by
construction* — which is exactly what `MY::`, `callframe`, and lexical `EVAL`
require, and what the plan promised to preserve.

**No store discipline fixes this.** It was worth measuring three of them to
find that out.

## 2. What lowering one node saves

The tree-walk's fast path for `$a + $b` ([NODE-SPECIALIZATION.md](../../internals/NODE-SPECIALIZATION.md)
shape 3, `Interpreter.cpp:15613`) is two `Env::find` calls, a plain-scalar
guard, then `applyArith(op, …)` — the string-keyed dispatcher.

| shape | ns/op |
|---|---:|
| G — 2× `find` + guard + `applyArith` → local (today) | 32.1 |
| G2 — the same with `rtAdd` instead | 29.1 |
| F2 — lowered: dispatch + `rtAdd(regs[a], regs[b])` | 17.2 |

**Saving: ~15.4 ns per lowered arithmetic node**, of which only ~1.5–3.0 ns is
the op-string dispatch; the rest is the two name lookups.

That splits out a number worth keeping: **~5.7 ns per `Env::find`**. On
`fib(29)` — 1.66 M calls × 4 `$n` lookups × 5.7 ns ≈ 38 ms of ~760 ms — which
independently **reproduces the 4% ceiling** that
[PERF-CAMPAIGN.md](PERF-CAMPAIGN.md) item 4 measured for slot-indexed locals.
Two unrelated methods, the same answer; the probe is measuring what it claims.

### Break-even

Tax 11.2 ns against saving 15.4 ns:

> **~42% of all executed nodes must be lowered before the IR breaks even.**

A half-lowered program is a wash. The escape hatch does not make the campaign
incremental in the way the plan assumed — it makes the first ~40% of the work
unpaid.

## 3. The number that actually decides it

The `-DRAKUPP_NODE_COUNT` build says how many nodes a run visits:

| kernel | eval + exec nodes | interp time | ns per node visit |
|---|---:|---:|---:|
| fib | 9,984,480 | 750 ms | 75 |
| asg | 4,000,009 | 550 ms | 137 |
| loopsum | 2,000,009 | 210 ms | 105 |
| method | 15,000,012 | 1,770 ms | 118 |
| call | 14,000,009 | 1,380 ms | 99 |

**75–137 ns per node visit.** The IR's opcode dispatch addresses 0.28 ns of
that. Even the full crossing tax, applied to *every* node, is 8–15% of runtime.

The interpreter is not slow because it walks a tree. It is slow because of what
it does *at* each node.

## 4. Where the time really is: two allocations per call

`fib(29)` runs 1.66 M calls in ~750 ms interpreted (~451 ns/call) and in 160 ms
compiled (~96 ns/call) — a gap of ~355 ns per call. Priced directly:

| per-call cost | ns |
|---|---:|
| P1 — `make_shared<Env>` + parent + one bound variable | 103.5 |
| P2 — a `ValueList` holding one argument | 51.3 |
| **P3 — both, as a call actually does them** | **161** |

**Two allocations account for ~46% of the entire interpreted-vs-compiled gap**
— and about 36% of `fib`'s total runtime (1.66 M × 161 ns ≈ 268 ms of 750 ms).

This is the same finding the perf campaign reached from the other end (`malloc`
24%, `Value` ctor/dtor 18.5%), now attributed to specific call-path lines.

## The verdict

The `vs interp` column in [BENCHMARKS.md](../../status/BENCHMARKS.md) — up to
9.6× — is real, but I0 says it is **not** paid for by flattening the tree. It
is paid for by `--exe` not building an `Env` and a `ValueList` per call, and by
its intermediates being non-escaping C++ locals.

An IR delivers neither of those. It delivers 0.28 ns of dispatch saving per
node, and it *charges* 11.2 ns for every node it has not yet lowered.

**So the plan inverts.** The two phases that were scaffolding for the IR are
the ones carrying the money, and neither needs an IR to exist:

- **frames instead of `make_shared<Env>` + a name-keyed map** — 103 ns/call;
- **direct-arity calls instead of a per-call `ValueList`** — 51 ns/call, the
  in-process twin of the change that measured −9.0% compiled.

Do those as ordinary AST-level work, measure, and only then ask whether an IR
adds anything on top. On this evidence it does not, and the honest expectation
is that **I2 never happens**.

### What would revive the IR

One thing, and it is the phase the plan already marked speculative: **unboxed
typed registers (I4)**. If a register can hold a `long long` without a `Value`,
the 11.2 ns escape tax and the 375-byte-object costs both stop applying to
lowered code — and an IR is the only structure in which that analysis can be
expressed. That is a genuine reason to build one. "Flat instructions are
faster than a tree" is not, and this file is why.

## Instruments left behind

- [`tools/ir-boundary.cpp`](../../../tools/ir-boundary.cpp) — the measurements
  above; re-run before trusting any of them on another machine.
- [`tools/ir-bench.raku`](../../../tools/ir-bench.raku) — compares **two builds**
  on seven kernels, alternating round by round, with `bigint` as a control that
  must not move and a refusal if the two binaries differ in architecture. It
  adds the two kernels `perf-guard` is blind to: `method` (dispatch-dominated)
  and `call` (non-recursive sub calls). It does not replace `perf-guard`, which
  is the release gate against a recorded baseline.
- `-DRAKUPP_NODE_COUNT` — a build flag that reports `eval`/`exec` node counts at
  exit. Compiled out of every normal build.
