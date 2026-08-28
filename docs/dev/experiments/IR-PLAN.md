# Plan: a register IR for the interpreter

*Written 2026-08-08, before any code. The engine campaign that follows v3.0.0's
three pillars.*

> **I0 ran the same day and inverted this plan. Read
> [IR-BOUNDARY.md](IR-BOUNDARY.md) first.**
>
> Measured against the real runtime: opcode dispatch is worth **0.28 ns** per
> node; the `EVAL_NODE` escape hatch **costs 11.2 ns** per un-lowered node (the
> cause is escape analysis — no store discipline fixes it); a node visit costs
> **75–137 ns**, so dispatch is ~0.3% of it; and two per-call allocations —
> `Env` at 103 ns and the argument `ValueList` at 51 ns — are **~46% of the
> entire interpreted-vs-compiled gap**.
>
> The money is in phases **I1** and **I3**, and neither of them needs an IR.
> Those are now the plan. **I2 is conditional and expected not to happen**;
> the design below is kept as written because **I4** is the one thing that
> would revive it.

The interpreter walks the AST. This plan replaces that walk — for the parts of
a program it can understand — with a **flat register IR** lowered once per
routine and executed by a small VM, keeping the existing `Value`, the existing
runtime, and the existing tree-walker as the fallback for everything the
lowerer does not yet cover.

It is deliberately **not** framed as "a bytecode VM is faster than a tree
walker". That claim has not been measured here, and three of this project's own
measurements say the obvious reasons it would be true are false (see *What the
measurements forbid*). The claim it *is* framed on is narrower and already
measured.

---

## The one number this rests on

[BENCHMARKS.md](../../status/BENCHMARKS.md) has a column nobody has read as a
plan before: **`vs interp`** — how much faster the *same program* runs when
`--exe` compiles it instead of the interpreter walking it.

| kernel | `vs interp` | what the compiled version does differently |
|---|---:|---|
| streq | 9.6× | no per-node walk around a byte compare |
| loopsum | 7.1× | loop body is straight-line C++, locals are C++ locals |
| fib | 4.2× | no `Env`, no `ValueList` per call, inline `rtLt`/`rtSub` |
| strcat | 3.3× | |
| hash | 2.2× | |
| sortnums | 1.4× | |
| regex | 1.3× | time is inside the regex engine |
| bigint | 1.1× | time is inside `BigInt` multiply |
| arrayops | 1.0× | time is inside `.grep`/`.map`/`.sort` |

**`--exe` uses the same `Value`, the same `applyArith`, the same everything.**
It has no JIT, no type inference, no different object model. The entire spread
above comes from four structural facts, all of which are properties of *being
compiled ahead of the run* rather than of being native code:

1. the program is a linear instruction sequence, not a tree re-dispatched per
   visit;
2. locals are slots decided before the run, not names hashed during it;
3. arguments are passed positionally into a frame, not packed into a heap
   `ValueList` (this is the change that measured **−9.0%** on its own — the
   largest single win of the perf campaign);
4. operators and callees are resolved once at compile time (`rtAdd`,
   `rtLtB`, a direct call) instead of dispatched per evaluation.

An IR gets all four **in process** — which is to say: for `EVAL`, for modules,
for the REPL, for Raku.js in the browser, for every user who does not have a C++
toolchain and will never type `--exe`. That is the whole proposal. The ceiling
is the `vs interp` column; the realistic take is some fraction of it, and the
fraction is what each phase has to prove.

**What this is not:** a claim about the top of that table. `arrayops`, `bigint`
and `regex` spend their time inside runtime methods, and an IR cannot touch
them — the honest expectation is that four kernels move a lot, three move a
little, and two do not move at all.

---

## What the measurements forbid

This project has already measured most of the reasons people give for building
an IR, and they came back negative. Any design that leans on one of them is
wrong before it is written.

| idea | measured | verdict |
|---|---|---|
| **Slot-indexed locals** replace name hashing | `fib(32)`: hash lookup 2.8%, string compare 1.2%, `Env`/`bindParams` 2.6% | **~4% ceiling.** ([PERF-CAMPAIGN.md](PERF-CAMPAIGN.md) item 4.) Registers are a *precondition* for items 3 and 4 above, **not** the win. Do not justify a phase with them. |
| **Hash/`switch` the method-dispatch chain** | 682-entry map ≈ 19 `MName` compares; both variants within noise on all 8 kernels | **Rejected**, and 56% of the arms dispatch on invocant type, not name — there is nothing to index. ([METHOD-DISPATCH-EXPERIMENT.md](METHOD-DISPATCH-EXPERIMENT.md).) No "inline cache on method name" appears in this plan. |
| **Shrink `Value`** (376 → 360 by repacking) | 2.5% **slower**, six alternating rounds | Struct size is not monotonic with speed here. `Value`'s layout is out of scope. |
| **Constant folding** | 37 foldable sites in 51,353 real AST nodes (0.7 per 1k) | Nothing to fold. ([NODE-SPECIALIZATION.md](../../internals/NODE-SPECIALIZATION.md).) |

What the profile *does* say, consistently, on both `fib` and a method-heavy
probe:

```
malloc family                   : ~24%
Value ctor / dtor / move-assign : ~18.5%
```

**Every phase below is judged on one question: does it remove an allocation or
a `Value` copy?** If a phase's answer is "it removes an AST node visit", it
does not ship. That is the lesson the campaign wrote down — *the three changes
that worked all removed work or allocation; the one that removed bytes ran
slower.*

---

## Where the AST already is

Half of a lowering pass exists, scattered as verdicts cached on nodes:

- `Binary::fastShape` / `litVal` — the four shapes hot loops are made of
  (`$var OP lit`, `lit OP $var`, `$var OP $var`, `@a[$i]`), each classified once
  per node and never re-asked ([NODE-SPECIALIZATION.md](../../internals/NODE-SPECIALIZATION.md)).
- `Binary::simpleOp` — "is this a plain operator or one of the special-cased
  ones", decided once from the syntax.
- `Callable::hoistNeed`, `NumLit::cacheN/cacheD` — same pattern.
- `Codegen.cpp` (2,586 lines) already computes, for `--exe`: which subs are
  **direct-arity** (all-plain-required-positional, so the parameters can *be*
  the arguments), which operator sites can take `rtAdd`/`rtLtB`/`rtEqS`, and
  which builtin calls can go direct to `rtBAbs`/`rtBUc`/… ([OPTIMIZATION.md](../../internals/OPTIMIZATION.md)).

These are node-local decisions made lazily at run time, each on its own. The IR
is the same decisions made **once, together, into a form that can carry them** —
so `$n - 1` stops being "a `Binary` that remembers it is shape 1" and becomes
`SUB_VL r2, r0, k1`. The analysis is not new work; the representation is.

This also means the *first* consumer of a lowering pass is not the VM. If
`Codegen.cpp` lowers from the IR instead of from the AST, the two backends stop
computing the same facts twice — which is the same convergence argument
[REVIEW-2.0.md](../findings/REVIEW-2.0.md) makes about the `invokeMethod` /
`callCallableRaw` twin.

---

## The design

### 1. Frames, not `Env`

Each routine gets a **slot map** computed at lowering time: every `my`, every
parameter, every block-scoped temporary gets an index. At run time a frame is
one `std::vector<Value>` sized once — replacing the per-call `make_shared<Env>`
plus its map insertions.

`Env` does not go away. It stays as the **name side-table** that `EVAL`, `MY::`,
`callframe`, dynamic `$*vars`, `state`, and the name-keyed `rw` write-through
machinery need, built lazily and pointing *at the slots*. A routine that never
triggers one of those never materialises the names. This is the only honest way
to keep those features: they are keyed by name by their nature, and the 4%
measurement says fighting that is not where the money is anyway.

### 2. Opcodes carry pre-resolved decisions

The instruction set is small and register-based (three-address, `uint32_t`
operands):

- **arithmetic/compare** — one opcode per `(op, guard)` pair the fast paths
  already recognise: `ADD_VV`, `ADD_VK`, `LT_VK`, `CONCAT_SS`, … each with the
  *identical* guard the current fast path uses and the *identical* fallback to
  `applyArith`. Semantics are unchanged by construction: the opcode is the
  cached verdict, not a new rule.
- **variables** — `LOAD_SLOT` / `STORE_SLOT`; `LOAD_NAME` for anything the slot
  map cannot own (twigils, package vars, `MY::`).
- **calls** — `CALL_DIRECT` writes arguments straight into the callee frame's
  slots (item 3, the −9% one); `CALL_BOXED` is the existing `ValueList` path for
  named/slurpy/optional/multi/indirect.
- **control** — `JUMP`, `JUMP_UNLESS`, plus explicit opcodes for the cooperative
  control flow (`return`/`last`/`next`/`redo`) so the exception-shaped
  unwinding used today can become a jump where the target is statically known.
- **the escape hatch** — `EVAL_NODE <const Expr*>`: run the existing
  tree-walker on that subtree and put the result in a register.

### 3. `EVAL_NODE` is what makes this tractable

This is the load-bearing design decision. **The lowerer never has to be
complete.** Any construct it does not understand — and on day one that is
almost all of Raku — emits one instruction that calls the code that runs it
today, on the same AST node, with the same environment. A half-lowered program
is exactly as correct as the same program is now.

That converts a rewrite into an incremental campaign with a working build at
every commit, which is the only shape this can have in a 144k-line source at
90% Roast. It also gives every phase a cheap, honest metric: **the fraction of
executed instructions that are `EVAL_NODE`**, per kernel and across the roast
suite.

The risk it carries is named in *Risks*: the boundary is not free, and a
program that is 95% `EVAL_NODE` could be *slower* than today. That cost gets
measured in phase I0, before anything depends on it.

### 4. Unboxed registers — the second tier, decided on evidence

The four structural wins above still materialise a 376-byte `Value` for every
intermediate. The thing that would take `loopsum` and `fib` toward the `--exe`
column is a **typed register** — a slot known to hold `long long` for the
extent of a loop, with the `Value` built only where it escapes.

This is where the remaining `malloc`/copy percentage lives, and it is also the
first phase in this plan that is genuinely speculative. It is therefore **I4,
after** the boring structural phases have banked their measurements, and it is
prototyped on `loopsum` alone before any general machinery is written.

---

## Phases

Each phase is a batch under the standing gates (definitions in
[COUNTING.md](../../status/COUNTING.md)):
zero Roast regressions, the module battery unchanged, `perf-guard --check`, and
a measured number from **alternating builds with a control kernel** — a table of
improvements measured one-build-then-the-other is equally consistent with the
machine having been quieter the second time.

### I0 — the instrument (no IR yet) — **DONE 2026-08-08**

Delivered, written up in [IR-BOUNDARY.md](IR-BOUNDARY.md):

- [`tools/ir-bench.raku`](../../../tools/ir-bench.raku) — compares **two builds**
  over seven kernels, alternating round by round, with `bigint` as a control
  that must not move, and a refusal when the two binaries differ in
  architecture (this machine's default `build/` is x86_64 under Rosetta and
  runs ~2× slower). It adds the two kernels the guard is blind to — `method`
  and `call`. It does not replace `perf-guard`, which stays the release gate.
- [`tools/ir-boundary.cpp`](../../../tools/ir-boundary.cpp) — the crossing cost,
  the lowering saving, and the per-call allocation costs, against the real
  `Value`/`Env`/`applyArith`.
- `-DRAKUPP_NODE_COUNT` — a build flag reporting `eval`/`exec` node counts at
  exit; compiled out of every normal build.

**Result:** dispatch 0.28 ns, crossing tax 11.2 ns/node, lowering saving
15.4 ns/node (break-even at ~42% of nodes lowered), node visit 75–137 ns, and
`Env` + `ValueList` = 161 ns/call ≈ 46% of the interpreted-vs-compiled gap. The
probe independently reproduced the known 4% slot-locals ceiling from
[PERF-CAMPAIGN.md](PERF-CAMPAIGN.md), which is why it is
believed. The phase order below follows from those numbers.

### I1 — kill the per-call `Env` allocation (**no IR**)

103 ns of every call is `make_shared<Env>` plus a name-keyed map insert per
bound variable. Slot-resolve routine locals and parameters at parse time; the
frame becomes one contiguous allocation (or none, for a routine whose slots fit
a reusable stack), with `Env` kept as the lazily-built **name side-table** that
`EVAL`, `MY::`, `callframe`, dynamic `$*vars`, `state` and the name-keyed `rw`
write-through still need. A routine that triggers none of those never
materialises the names.

**Gate:** zero Roast regressions; `fib` and `call` move measurably on
`ir-bench` with `bigint` flat. Unlike the pre-I0 draft, this phase is now
expected to *win*, not merely to be neutral — the 4% slot-lookup ceiling is not
what it is aimed at; the allocation is.

### I2 — the lowerer and the VM — **conditional, and expected not to happen**

I0 priced this phase's mechanism at +0.28 ns of saving and −11.2 ns of cost per
node. It is not started unless I1 and I3 land and something in their
measurements argues for it, or I4's prototype needs an IR to express itself.

If it ever does start: `EVAL_NODE` for everything, then constructs move over
one at a time — the four `fastShape` binary shapes → `if`/`while`/`loop` →
assignment → indexing → `for` over a range — each with its own measurement, and
the suite green with the IR **forced off and forced on** (`RAKUPP_IR=0/1`)
every batch.

### I3 — kill the per-call `ValueList` (**no IR**)

51 ns of every call is a heap `ValueList` built to carry the arguments.
`Codegen.cpp` already computes which subs are **direct-arity** (all-plain-
required-positional); port that analysis to the interpreter so such a call
writes its arguments straight into the callee's frame slots. This is the
in-process twin of the change that measured −9.0% compiled, and with I0's
numbers it is the phase with the best prior of the lot.

Ordering note: I1 and I3 touch the same call path and are best done as one
sequence, I1 first — the frame has to exist before arguments can be written
into it.

**Gate:** `fib` and the `call` kernel move measurably with `bigint` flat;
multi-dispatch, `&sub` indirection, named/slurpy args, wrappers and `callsame`
all still route through the existing boxed path with Roast unchanged.

### I4 — unboxed integer registers (prototype first)

Prototype on `loopsum` only, behind a flag, and **measure before generalising**.
If a narrow prototype does not clearly beat the boxed IR on the one kernel it
was built for, it does not become general machinery — the reordering experiment
is the precedent.

### I5 — the flip (only if I2 ever happens)

`RAKUPP_IR` default on with `RAKUPP_IR=0` as the escape hatch, exactly the
shape [PARALLEL-PLAN.md](PARALLEL-PLAN.md) uses for `RAKUPP_GIL`. The flag is
removed no earlier than one release later. I1 and I3 need no flag of their own:
they change representation, not semantics, and are gated on Roast like any
other batch.

### I6 — converge the backends (optional, after the flip)

Point `Codegen.cpp` at the IR instead of the AST, so `--exe` and the VM stop
deriving the same facts twice. Separately gated; the `--exe` output must stay
byte-comparable in behaviour and no slower.

---

## Gates (summary)

Every batch (and, if I2 ever runs, in both `RAKUPP_IR=0` and `RAKUPP_IR=1`):

1. **Roast** — zero regressions, per-file and per-assertion.
2. **Module battery** — the 50/59 dists unchanged ([MODULE-FINDINGS.md](../ecosystem/MODULE-FINDINGS.md)).
3. **`perf-guard --check`** — no regression, and this is a *release gate*, not
   an eyeball ([RELEASING.md](../RELEASING.md)); it has slipped twice.
4. **The phase's own number** — alternating builds, a control kernel in the same
   run, quiet machine, re-measured before it is believed.
5. **TSan** under `RAKUPP_PARALLEL=1` — frames are per-thread state and must
   stay so; the IR itself is immutable and shared like the AST.
6. **The other three modes still work** — `--exe`, `--aot`/`--bundle` and
   Raku.js all build and pass their smoke tests. (Raku.js benefits *most* from
   this work — the browser has no `--exe`.)
7. **The doc-conformance sweep** unchanged: 952 byte-identical examples is a
   behaviour gate as much as Roast is.

---

## Risks, named

- **The escape-hatch boundary.** A program that stays 90% `EVAL_NODE` pays
  dispatch *plus* the walk. Measured in I0 before anything depends on it; if the
  crossing is expensive, lowering has to reach whole statements before it is
  worth anything, and the phase order changes.
- **Name-keyed features.** `rw` write-through, `temp`/`let` restoration, `is
  dynamic`, `state`, `MY::`, `callframe`, `EVAL` in a lexical scope — all keyed
  by name today. The slot map must *coexist* with names, not replace them. Any
  design that requires deleting the name path is wrong.
- **Closures.** A closure captures an `Env`; frames are vectors with a lifetime.
  Captured slots must be promoted to a heap cell (or the frame kept alive) — and
  the nested-sub closure-cycle leak fixed pre-1.0 (425 MB → 3 MB) is the warning
  that this exact area bites.
- **Line numbers.** `--profile`, backtraces, `warn`, and the roast fudge
  machinery all need the source line of the instruction. Every opcode carries
  one; this is not optional and it is not added later.
- **Parallelism.** Landing while PARALLEL's flip is in flight means frames and
  the worker pool touch the same execution state. Sequence this campaign
  **after** v3.0.0 tags, or expect to re-litigate the memory model.
- **Scope creep into a JIT.** There is no JIT here. If a phase's justification
  starts with "and then we could emit machine code", it belongs in a different
  plan.

---

## Non-goals

- A JIT, or any native code generation. `--exe` already occupies that role.
- Replacing `--exe`, `--aot` or `--bundle`.
- Changing `Value`'s layout or the object model.
- RakuAST compatibility — that has its own plan ([RAKUAST-PLAN.md](RAKUAST-PLAN.md)).
- Beating Rakudo on `arrayops`/`bigint`/`regex`. Their time is inside the
  runtime; an IR is the wrong tool and the `vs interp` column says so.

---

## What would make this stop

Any of these, and the campaign is written up as an experiment that did not pay,
the way item 2 and the dispatch-map experiment were:

- ~~I0 says the escape-hatch crossing is not cheap and no restructuring makes it
  cheap~~ — **this happened.** I0 measured the crossing at 11.2 ns/node and
  showed three store disciplines that do not fix it. It did not kill the
  campaign, because I0 also found where the time actually is (I1, I3); it
  killed **the IR**, which was the campaign's name and not its value;
- I1 or I3 lands inside the noise band on `fib` and `call` with the control
  flat — that would say the per-call allocations are not reachable without
  changing `Value`, and the campaign ends there;
- the Roast or battery gate cannot be held for a frame representation without
  special-casing behaviour per-construct.

The point of writing this down before the code is that stopping is a normal
outcome, and cheaper the earlier it is allowed to happen.
