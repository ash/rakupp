# A bytecode/register IR for the interpreter — measured, not adopted

*2026-08-08. Nothing here landed. This is the record of an experiment so it does
not have to be run twice.*

> **Re-measured 2026-09-02 — read the last section too.** The verdict below
> still stands, but one of its two legs broke: the escape-analysis tax that
> made a partially lowered IR impossible was a property of a 376-byte `Value`
> and is now zero. Dispatch is still worth under 1% of a node visit.

The proposal was the standard one: stop walking the AST, lower each routine to a
flat register IR, and execute it in a small VM — keeping the existing `Value`,
the existing runtime, and the tree-walker as a per-node fallback (`EVAL_NODE
<const Expr*>`) so the lowerer would never have to be complete.

It was measured before any opcode was written. The measurements killed the IR
and found where the time actually is. Both halves are below.

Companions: [PERF-CAMPAIGN.md](PERF-CAMPAIGN.md) is the profile this builds on
(`malloc` ~24%, `Value` ctor/dtor ~18.5%);
[METHOD-DISPATCH-EXPERIMENT.md](METHOD-DISPATCH-EXPERIMENT.md) is the other
"the obvious O(1) rewrite is a regression" record. The full campaign plan, the
two-build benchmark harness (`tools/ir-bench.raku`) and a `-DRAKUPP_NODE_COUNT`
instrumentation patch live on the **`ir` branch**; the microbenchmark behind
every number here is [`tools/ir-boundary.cpp`](../../../tools/ir-boundary.cpp).

## What motivated it

[BENCHMARKS.md](../../status/BENCHMARKS.md) has a `vs interp` column: how much
faster the *same program* runs when `--exe` compiles it rather than the
interpreter walking it — 9.6× on `streq`, 7.1× on `loopsum`, 4.2× on `fib`.
`--exe` uses the same `Value`, the same `applyArith`, the same runtime, and has
no JIT. So that spread is structural, and an IR looked like the way to get it
in process — where it would also serve `EVAL`, the REPL, modules and Raku.js,
none of which can use `--exe`.

The reasoning was sound. The attribution was wrong.

## 1. Opcode dispatch is worth 0.28 ns

Against the real `Value`, 7 reps, first discarded, minimum reported, arm64:

| shape | ns/op |
|---|---:|
| `Value v = eval(node)` into a fresh local (today) | 5.6 |
| dispatch + `regs[d] = eval(node)` (a register VM) | 17.3 |
| the register store alone, no dispatch | 17.2 |
| **the opcode `switch` alone** | **0.28** |

The premise "a flat instruction sequence beats re-dispatching a tree" is worth
**0.28 ns per node**. Everything else in that 11.7 ns is the *store*.

## 2. The escape hatch costs 11.2 ns/node, and no store discipline fixes it

Three ways of writing the result into a slot were measured, because they lead to
different designs:

| variant | tax vs a plain local |
|---|---:|
| materialise a temporary, then move it into the slot | +12.1 ns |
| destroy the dead slot, then construct in place (heap/static memory) | +11.2 ns |
| move-assign into a **live stack local** | +11.5 ns |
| **destroy + construct in place, in a stack buffer** | **−0.01 ns** |

The last two rows run the *same code shape* as the two above them. The only
difference is where the slot lives: a stack buffer whose address never escapes,
versus memory the optimiser cannot reason about.

So the tax is not the move, not the assignment, and not the store discipline.
It is **escape analysis**. A tree-walker's intermediates are non-escaping
allocas, and LLVM elides most of the cost of constructing and destroying a
376-byte `Value` carrying 5 `std::string`s and 11 `shared_ptr`s. A VM's register
file cannot have that property — it is reachable from the interpreter *by
construction*, which is precisely what `MY::`, `callframe` and lexical `EVAL`
require.

**This is the finding to remember.** It is not specific to this design: any
scheme that parks interpreter intermediates in long-lived, addressable storage
pays it.

## 3. What lowering a node saves, and the break-even

The tree-walk's fast path for `$a + $b`
([NODE-SPECIALIZATION.md](../../internals/NODE-SPECIALIZATION.md) shape 3) is
two `Env::find` calls, a plain-scalar guard, then `applyArith(op, …)`:

| shape | ns/op |
|---|---:|
| 2× `find` + guard + `applyArith` → local (today) | 32.1 |
| the same with `rtAdd` instead | 29.1 |
| lowered: dispatch + `rtAdd(regs[a], regs[b])` | 17.2 |

**~15.4 ns saved per lowered arithmetic node**, only ~1.5–3.0 ns of which is the
op-string dispatch; the rest is the two name lookups. Against an 11.2 ns tax on
every node *not* yet lowered:

> **~42% of all executed nodes must be lowered before the IR breaks even.**

The escape hatch does not make the work incremental. It makes the first ~40% of
it unpaid.

### A check that the probe is measuring what it claims

The same run prices `Env::find` at **~5.7 ns**. On `fib(29)` — 1.66 M calls × 4
`$n` lookups × 5.7 ns ≈ 38 ms of ~760 ms — which independently reproduces the
**4% ceiling** that [PERF-CAMPAIGN.md](PERF-CAMPAIGN.md) item 4 measured for
slot-indexed locals by profiling. Two unrelated methods, the same answer.

## 4. The number that actually decided it

A `-DRAKUPP_NODE_COUNT` build (counters compiled out of every normal build)
counts the nodes a run visits:

| kernel | eval + exec nodes | interp time | ns per node visit |
|---|---:|---:|---:|
| fib | 9,984,480 | 750 ms | 75 |
| asg | 4,000,009 | 550 ms | 137 |
| loopsum | 2,000,009 | 210 ms | 105 |
| method | 15,000,012 | 1,770 ms | 118 |
| call | 14,000,009 | 1,380 ms | 99 |

**75–137 ns per node visit**, against 0.28 ns of addressable dispatch. Even the
full crossing tax applied to *every* node is only 8–15% of runtime.

The interpreter is not slow because it walks a tree. It is slow because of what
it does *at* each node.

## 5. Where the time really is: two allocations per call

`fib(29)` runs 1.66 M calls in ~750 ms interpreted (~451 ns/call) and in 160 ms
compiled (~96 ns/call) — a gap of ~355 ns per call. Priced directly:

| per-call cost | ns |
|---|---:|
| `make_shared<Env>` + parent + one bound variable | 103.5 |
| a `ValueList` holding one argument | 51.3 |
| **both, as a call actually does them** | **161** |

**Two allocations are ~46% of the entire interpreted-vs-compiled gap** — about
36% of `fib`'s total runtime (1.66 M × 161 ns ≈ 268 ms of 750 ms). This is the
same conclusion [PERF-CAMPAIGN.md](PERF-CAMPAIGN.md) reached from the profiling
end (`malloc` 24%, `Value` ctor/dtor 18.5%), now attributed to specific lines on
the call path.

## The verdict

The `vs interp` column is real, but it is **not** paid for by flattening the
tree. It is paid for by `--exe` not building an `Env` and a `ValueList` per
call, and by its intermediates being non-escaping C++ locals.

An IR delivers neither. It delivers 0.28 ns of dispatch saving per node, and
charges 11.2 ns for every node it has not yet lowered.

**Not adopted.** What the same measurements say *is* worth doing, and needs no
IR, no VM and no new execution mode:

1. **Remove the per-call `Env` allocation** — 103 ns/call. Slot-resolve routine
   locals and parameters at parse time into one contiguous frame, with `Env`
   kept as a lazily-built name side-table for the features that are keyed by
   name by nature (`EVAL`, `MY::`, `callframe`, dynamic `$*vars`, `state`, `rw`
   write-through). Note this is *not* the slot-indexed-locals idea whose ceiling
   PERF-CAMPAIGN.md measured at 4% — the target here is the allocation, not the
   lookup.
2. **Remove the per-call `ValueList`** — 51 ns/call. `Codegen.cpp` already
   computes which subs are direct-arity (all plain required positionals) for
   `--exe`; the same analysis lets an interpreted call write its arguments
   straight into the callee's frame. The in-process twin of the change that
   measured −9.0% compiled.

## What would revive the IR

One thing: **unboxed typed registers** — a slot known to hold a `long long` for
the extent of a loop, with a `Value` built only where it escapes. That makes
both the 11.2 ns escape tax and the 376-byte-object costs stop applying to
lowered code, and an IR is the only structure in which that analysis can be
expressed.

"Flat instructions are faster than a tree" is not a reason, and this file is
why.

## Reproducing

```sh
# arm64 — NOT the default `build/`, which an x86_64 cmake under Rosetta
# produces on this machine (~2x slower; perf-guard refuses the mismatch)
cmake -S . -B build-arm64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-arm64 -j
clang++ -std=c++17 -O2 -DNDEBUG -Isrc tools/ir-boundary.cpp \
        build-arm64/librakupp_rt.a -o /tmp/ir-boundary && /tmp/ir-boundary
```

Node counts come from a build with `-DRAKUPP_NODE_COUNT` plus a two-line
`++counter` in `Interpreter::eval` and `Interpreter::exec`, both inside the
`#ifdef` (on the `ir` branch). Re-measure before trusting any of this on another
machine, and re-measure on a **quiet** one: a self-comparison of one binary
against itself showed ±5.8% spread at one round.

---

## Re-measured 2026-09-02: the objection is gone, the motive is not

Put again by the user, from the other end: take from Perl 5 *the way it
flattens the AST tree to speed up traversing it* (PERL5-TECHNIQUES item 3,
`run.c`'s `while ((PL_op = op = op->op_ppaddr(aTHX)));`). This file says no.
It said so on 2026-08-08, and since then the interpreter it measured has
changed enough that the answer had to be re-taken rather than quoted: pads
landed, TARG landed, `ValueHash` replaced the `std::map` payload, and
`sizeof(Value)` went 376 -> 200 -> 128. Both halves of the verdict were
re-run against today's tree (Darwin 25.5 / M1, `build/` release static libs,
`tools/ir-boundary.cpp` unchanged).

**Half of it inverted.**

| | 2026-08-08 | 2026-09-02 |
|---|---:|---:|
| opcode `switch` alone | 0.28 ns | 0.32 ns |
| crossing tax, naive register move-assign | +11.7 ns | **+4.13 ns** |
| crossing tax, destroy-dead + construct-in-place, **static** storage | +11.2 ns | **-0.02 ns** |
| the same on a stack buffer | -0.01 ns | -0.02 ns |
| break-even lowered fraction, naive VM | ~42% | **13.4%** |
| break-even lowered fraction, dead-slot VM | (not the quoted figure) | **0%** |

The escape-analysis finding — "any scheme that parks interpreter
intermediates in long-lived, addressable storage pays it" — **no longer
holds at that price**. It was a property of a 376-byte `Value` carrying five
`std::string`s and eleven `shared_ptr`s: destroying and re-constructing one
in memory LLVM cannot reason about was expensive because there was so much
of it. Today's `Value` is 128 bytes with one `CowStr` and two `shared_ptr`s,
and the same shape in `static` storage measures free.

So the structural objection this file rested on is gone. **A partially
lowered IR is now possible**: the un-lowered fallback costs nothing, which
is exactly the property that was missing.

**The other half did not invert.** Node counts from a `-DRAKUPP_NODE_COUNT`
build, against best-of-5 wall clock on the release build:

| kernel | eval + exec nodes | ns per node visit, 2026-08-08 | today |
|---|---:|---:|---:|
| fib | 9,984,480 | 75 | **46** |
| asg | 4,000,009 | 137 | **61** |
| loopsum | 2,000,009 | 105 | **72** |
| method | 3,200,015 | 118 | **85** |
| call | 3,200,012 | 99 | **65** |

(`fib`'s node count is identical to August's to the node, which is the
cross-check that the instrumentation and the kernel still agree. The
`method` and `call` counts are lower because these are re-written kernels,
not the ir-branch ones.)

Per-node work fell about 40%, and dispatch rose from 0.28 to 0.32 ns — so
flat dispatch is now **0.4% to 0.7% of a node visit**, against 0.3% before.
Flattening the tree is still worth *less than one percent*, and the reason
is unchanged: the interpreter is not slow because it walks a tree, it is
slow because of what it does at each node.

One caveat on the saving side, which matters if anyone revives this. The
probe's model of the tree-walk — two `Env::find` calls, a guard, then
`applyArith`, 33.61 ns — is the PRE-PADS interpreter. Since PADS-PLAN, an
annotated reference indexes the frame directly and those two lookups are
gone from exactly the shapes an IR would lower first. The probe's "+30.50 ns
saved per lowered node" is therefore an overstatement of what is left to
win, by roughly the two lookups it still charges.

### What was done instead, and what it measured

The file's own two recommendations were the per-call `Env` (103 ns) and the
per-call argument `ValueList` (51 ns). The first has since been softened by
the thread-local call-frame pool. The second had not been touched. It was
re-priced at **32.35 ns** today and then removed, as a thread-local free
list of small blocks inside the `ValueList` container itself — no IR, no VM,
no new execution mode, one file:

```
one-argument list, built + passed + destroyed, x5000000
  today                  32.35 ns/call
  free-list block         9.48 ns/call  (3.41x)
  inline 2-elem buffer    4.41 ns/call  (7.34x)
  reused list (ceiling)   7.96 ns/call  (4.06x)
```

In the engine: `fib` +8.8% instructions, a two-argument sub-call loop
+13.0%, `method` +7.7%, `listbuild` +24.9%. Details and the memory trade
that decided the block-sizing are in
[VALUE32-PLAN.md](../plans/VALUE32-PLAN.md) batch 3.

### The standing answer

Unchanged in direction, changed in reason. Flat dispatch is not worth
doing — it buys under 1% of a node visit. What *is* now true, and was not in
August, is that an IR could be introduced incrementally without a crossing
tax; so if the thing that would revive it ever arrives — **unboxed typed
registers**, still the only structure in which that analysis can be
expressed — the incremental path is open. The premise to keep rejecting is
still "flat instructions are faster than a tree".
