# Plan: the interpreter at native speed

*Written 2026-09-30. The goal is the interpreter running a program about as
fast as `--exe` runs the same program. This plan puts the interpreter-speed work
into one order, as tasks that each land on their own. Several of them are
already items of [V6-PLAN.md](V6-PLAN.md) (P2 dispatch, P3 representation, P6
tier-up); this plan schedules them and adds what the 2026-09-29/30 file split
taught. Measurements come from
[../findings/DISPATCH-PROBES.md](../findings/DISPATCH-PROBES.md), the
[benchmarks page](../../status/BENCHMARKS.md) and the split's own A/B runs.*

---

## Where the gap is

How many times faster `--exe` runs each benchmark kernel than the interpreter
([BENCHMARKS.md](../../status/BENCHMARKS.md)):

| gap | kernels | where the interpreter's time goes |
|---|---|---|
| 1.0–2× | arrayops 1.0, bigint 1.3, sortby 1.5, regex 1.8, textsplit 1.9, rats 1.9, sortnums 2.0 | the runtime library both modes share |
| 2–4.5× | objects 2.1, strcat 2.8, arraypush 3.0, hash 3.1, hashfill 3.5, mainwhen 4.4 | runtime work and dispatch, mixed |
| **7–16×** | **fib 7.2, loopsum 8.3, streq 16.3** | the interpreter's own cost per AST node, and per call |

Where the work is in the runtime library, the interpreter already runs at
native speed. The gap is the tree walk. The dispatch probes split an integer
loop's samples like this:

| share | what |
|---:|---|
| ~19% | thread-local storage: `tctx_` accesses |
| ~11% | block entry and exit on a body with no phasers (since cut by c23ec4a8) |
| ~4% | `___chkstk_darwin`: stack frames over a page in `eval`, `evalBinary`, `applyArith`, `exec` |
| ~4% | `std::string` copies and compares |
| ~4% | `applyArith`'s dispatch on the operator's name |

and measured what each architectural step is worth on a lean walker (the
"ladder"): quickening −22…−29%, closure compilation −24…−37%, fused integer
leaf operations 2.1–3.6×. Calls (`fib`) move 3–7% under every one of those:
their cost is in call setup.

## What the split taught

Three lessons from splitting `Interpreter.cpp` and `Builtins.cpp`
(610c36d0, 60ee962b, 622b301f), which the tasks below apply:

- **Decide once per node, not once per evaluation.** A node's operator never
  changes. `Binary::simpleOp`, `Binary::specialArm` and `Unary::evalPath`
  store which code handles it on the first evaluation; `~~` stopped passing 82
  string tests, and `$i++` 40.
- **A cost that depends on inlining is not a cost you control.** In a function
  with a hundred `op == "lit"` compares, any edit changed which ones Clang
  inlined, and an outlined one pays `strlen` and a call. `opEq` makes each
  compare cost the same wherever it lands.
- **A `thread_local` read from another file costs a call** unless its
  declaration says it is constant-initialized (`RAKUPP_CONSTINIT`). `tctx_`
  cannot say that (it has a destructor), which is why the hot functions share
  `InterpreterCore.cpp` with its definition.

Result so far, against the build before the split: every `perf-guard` kernel
faster or level, mean −5.2%.

---

## The tasks

In order. Each is one commit or a short series, measured on its own.

### 1. Measure the gap on a quiet machine

*Folded into the other tasks, not run on its own:* each task is judged by an
interleaved A/B against the build before it, which is the baseline this task
would have recorded, and task 2 re-measures v4.0.1 once the hot-path tasks
have landed.

- [ ] Time every [BENCHMARKS.md](../../status/BENCHMARKS.md) kernel as interp
  and as `--exe`, at HEAD and at a local v4.0.1 build, interleaved
  ([perf A/B method](V6-PLAN.md#gates-every-batch)).
- [ ] Record the table in `docs/dev/findings/` as the baseline every later task
  is judged against: the interp/native gap per kernel, and interp v4.0.1 → HEAD.

*Done when:* the table exists, and each kernel's gap and v4 → HEAD change is
known on one machine in one sitting.

### 2. Win back the v4.0.1 → v5.0.0 interpreter regressions

v5.0.0 interprets 17 of 18 kernels slower than v4.0.1 did: `fib` +30%,
`streq` +41% ([BENCHMARKS.md](../../status/BENCHMARKS.md#v401-to-v500)). The
split has already recovered part of it.

*Moved after task 6:* tasks 3–6 rework the same paths, so a regression found
now would mostly be fixed by them anyway. What is left then is measured once.

- [ ] Take task 1's table: which kernels are still slower than v4.0.1.
- [ ] For each, find the commit by ratio against a fixed reference binary,
  never by an absolute threshold, and fix or explain it.

*Done when:* no kernel interprets slower than on v4.0.1, or the CHANGELOG says
why one does.

### 3. Decide once per node, everywhere on the hot path (quickening)

The ladder's first rung, −22…−29%. The pattern is the one `Unary::evalPath`
uses; each item is its own commit.

- [ ] **Assignment.** `Assign` caches which arm of `evalAssignInner` handles
  its operator (104 operator compares in that function today). *In part:*
  the decided-once pad-slot lane (`Assign::simpleSlot`: `=`, `+=`, `-=`,
  `*=`, `~=` on a `$` pad variable) now runs first in `evalAssign`, ahead of
  every compare, and writes Int op= Int over the box
  (`intOpAssignInPlace`).
- [ ] **Arithmetic.** `Binary` caches an operator id; `applyArith` gains an
  overload on the id that switches instead of comparing (301 compares today).
  V6 P2 also names `typeMatchesArg`, `coreEnumValue` and `isKnownTypeName`.
- [ ] **Built-in methods.** A method call site caches which segment and arm
  answered, keyed on the invocant's type, and goes there directly next time
  (1,828 `m == "…"` compares across the six segments). This is V6 P2's "inline
  caches keyed on type". It is not the hashed chain V6 measured slower than
  the chain: the decision is stored on the call site, not looked up per call.
  *In part:* the 37 method-name compares in `eval`'s own MethodCall arm, and
  the `CORE-SETTING-REV` test every variable read made, are `opEq` (a length
  test first) — they were `strlen` calls; the segments' 1,828 are untouched.

*Done when:* no operator or method-name string compare is left on the path any
`perf-guard` kernel takes.

### 4. Read `tctx_` once per function, not once per use

About 19% of an integer loop's samples. `evalAssignInner` reads `tctx_` 120
times, `exec` 96.

- [ ] Take `ExecContext& tc = tctx_;` at the top of the hot functions —
  `runLoopBody`, `evalAssign`, `evalAssignInner`, `exec`, `tryCondBool`,
  `evalBinary`'s operand lambda — and use it throughout. *Done:*
  `execBlock` (already), `runLoopBody`, the new `execPlainBlock`. *Left:*
  `evalAssign`, `exec`, `eval`, which are the rest of loopsum's calls.
- [x] Prove that a function never outlives a thread switch with `tc`: a
  coroutine resumes only on the thread that started it (`Coro::ownerThread`,
  checked before every resume in InterpreterOperators.cpp), so the address
  stays this thread's for as long as the function runs.
- [x] The loop's own per-iteration thread-locals — worker flag, GIL-yield
  counter, gather-probe deadline and tick — are one constant-initialized
  `LoopPoll t_poll`, one access where there were up to four.

*Done when:* `__tls_init`/`_tlv_get_addr` is under 5% of the `loopsum` profile.
It is 7.4% (from ~8.5% before this task; the loop itself is 23% faster, so
the absolute cost fell further than the share).

### 5. Shrink the hot frames

`___chkstk_darwin`, ~4%: `eval`, `evalBinary`, `evalAssignInner` and `exec`
each have a frame over a page, because their rare arms keep locals alive in
the one frame.

- [ ] Measure each hot function's frame (`-fstack-usage`).
- [x] `execBlock` first: a block with none of the entry or exit work
  (`Block::entryWork` is 0 — no phaser, CATCH, named sub, hoisted `my`) runs
  on `execPlainBlock`, whose frame holds its statement loop and nothing else;
  the rest runs on `execBlockFull`. Every phaser-free loop body takes it.
- [ ] Move the rare arms into their own `[[gnu::noinline]]` functions until the
  hot frames are under a page. This is the long-function split, done for speed.
- [ ] Add a frame-size ceiling to `tools/source-helpers/budget.raku`, so the
  frames stay small.

*Done when:* `___chkstk_darwin` is gone from the kernel profiles.

### 6. Make calls cheap (`fib`, the 7.2× gap)

Dispatch work does not move calls; call setup does
([DISPATCH-PERF-PLAN.md](DISPATCH-PERF-PLAN.md)).

- [x] Routine bodies run their ENTER/LEAVE work from the call path on a
  statement vector, so `Block::entryWork` (c23ec4a8, −10% on loops) never
  reached them. Apply it there. *Done:* `Callable::bodyWork` (the same scan,
  `stmtsEntryWork`) gates hoistSubs, the ENTER and LEAVE runners, the
  value-statement scans and the per-statement phaser tests, in
  `callCallableRaw` and `invokeMethod` both.
- [x] Per-call constants off the call path: `Call::specialName` (evalCall's
  ten name tests decided once), `ExprStmt::yieldsContainer` (exec's AST walk
  per statement), the frame pool in `ExecContext::framePool` (it was a
  guarded thread_local per acquire and per release), the CONTROL register
  and `&?ROUTINE` guard without a `shared_ptr` copy or a thread_local write
  on every call, and `PadLayout::byName` on the inline name hash.
- [ ] A lean path for a plain sub. What is left of `callCallableRaw`'s own
  ~20% is spread thin across ~24 KB of code: guards, frame bookkeeping,
  the arity tally and binding, each a few percent at most. A sub with
  positional untyped parameters, no traits and `bodyWork` 0 can skip most
  of them outright, as `execPlainBlock` does for blocks.
- [ ] Build the redispatch context lazily, only for a body that uses
  `callsame`/`nextsame` (V6 P2).
- [ ] The multi-dispatch cache (V6 P2, issue #47).
- [ ] Split `Callable` into an immutable per-AST part and a ~64-byte closure
  (V6 P3 item 7).
- [ ] A variable declared in a `given`/`when` block falls back to map lookups:
  the same loop runs 3.3× slower there. Give those blocks pads
  ([PADS-PLAN.md](PADS-PLAN.md)).

*Done when:* `fib`'s gap is under 4×.

### 7. Smaller values, fewer allocations (V6 P3)

Each is its own plan, in V6's order of evidence per cost:

- [ ] [VALUEHASH-SMALL-PLAN.md](VALUEHASH-SMALL-PLAN.md): a small first chunk
  instead of the 4,032-byte deque block.
- [ ] Inline slots in `Env::vars`, and `make_shared<Env>` on the slab.
- [ ] Small Rats inline.
- [ ] [VALUE32-PLAN.md](VALUE32-PLAN.md): a 56-byte `Value`, the endgame.

*Done when:* V6's memory table has no row worse and the kernels that allocate
(`hashfill`, `arraypush`, `objects`) have closed half their gap.

### 8. Closure compilation (the ladder's second rung)

−24…−37% on the lean walker, where quickening was already in place.

- [ ] Prototype in the engine for the node kinds the kernels spend time in:
  each node compiled once, on first evaluation, into a small handler with its
  slot, operator id and fast path bound; the `switch` stays as the fallback.
- [ ] Judge it against the probe's number with the usual discount before going
  wider. It is not the flat threaded loop or register IR V6 measured at a ~4%
  ceiling: those change the loop, this removes the per-node `switch` and its
  shared giant frame.

*Done when:* the prototype has a measured verdict, and either lands for the hot
node kinds or goes into V6's "measured, and not pursued" table with its numbers.

### 9. Fused integer leaf operations (the ladder's top rung)

2.1–3.6× on the probe. A `Binary` whose operands are variables or literals runs
an int64 lane with the overflow check and stores the Int in place — the
UNBOX / `--cnp` idea applied per node rather than per loop.

- [ ] Build it on task 8's handlers, for `+ - * < <= > >= == !=` over native
  and plain Int operands.

*Done when:* `loopsum` and `streq` have closed half their gap.

### 10. Tier hot loops up to copy-and-patch (V6 P6)

The only way the 7–16× kernels reach native: `--cnp` already turns a loop into
machine code with no C compiler at run time.

- [ ] Run it automatically on a loop that has run N times, with the interpreter
  as the fallback.
- [ ] Loops only. V6 measured that widening `--cnp` does not move real
  workloads, whose time is inside calls; the tier-up is for numeric loops.
- [ ] The prerequisites are CNP-PLAN's: stencils for index reads and writes,
  hash elements and calls, correct answers on x86-64
  ([CNP-PLAN.md](CNP-PLAN.md)).

*Done when:* `loopsum` interprets within 1.5× of `--exe`.

---

## Gates, every task

- **`perf-guard --check`,** and an interleaved A/B against the commit before,
  built locally. A task that makes any kernel slower says so and why.
- **Roast:** 100% stays 100%.
- **The module battery's own suites,** base and branch per dist, for anything
  that changes dispatch, containers or calls
  ([V6-PLAN.md](V6-PLAN.md#gates-every-batch)).
- **The raku-corpus diff** for block-entry and dispatch changes.
- **`tools/source-helpers/budget.raku`:** no file, function or header over its
  ceiling.
- **`t/run.raku`** before a release.

## What the tasks are expected to add up to

The probes' own estimate, before any discount:

- **Tasks 2–6:** about 1.2× on the scalar kernels, with no change of
  architecture.
- **Tasks 8–9:** about 2× more, from the two big rungs of the ladder.
- **Task 10:** numeric loops at native speed; everything else stays where tasks
  2–9 leave it.
