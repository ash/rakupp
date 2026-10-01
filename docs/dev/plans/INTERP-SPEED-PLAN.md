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

## The order, refreshed 2026-10-01

Tasks 2–9 below have each landed in part. This is the order for what is left,
across this plan, [CNP-PLAN.md](CNP-PLAN.md), [TYPES-PLAN.md](TYPES-PLAN.md)
and V6's P2/P3, by evidence per cost. Each item keeps its own section and gates.

**Tier 1, in progress:**

1. **Typed values in value position** — task 9's rest, which is also
   [NATIVE-MATH-PLAN.md](NATIVE-MATH-PLAN.md) phase 4: comparisons and
   arithmetic whose result is read (not only a condition), with a tag check at
   each leaf, and `++`/`op=` on element and attribute targets (`@a[$i]++`,
   `$!n++`). TYPES N0 measured typed evaluation at 4.8× on the arithmetic.
   *In part (task 9 below):* typed subtrees, Num in the fast shape, and the
   `my $x = EXPR` declaration lane — `types/mandel-plain` −63%; and `++`/`--`
   on elements and attributes in place — perf-guard `hash` −24%.
2. **`--cnp` on x86-64** (CNP-PLAN P1). The one hard blocker for tier-up
   (task 10), for `--cnp` as the default, and for retiring `--jit`.
   *Fixed for Mach-O 2026-10-01:* the GOT slot held value − 4. Linux x86-64
   stays refused until a CI run of `t/jit/run.raku --cnp` there agrees.
3. **Inline slots for `Env::vars`, `Env` on the slab, and pads for
   `given`/`when` blocks** (task 6's last item, task 7's second). 99% of the
   mallocs in `for %h.kv -> $k, $v`, 30% of those in `objects`, and a loop
   3.3× slower inside a `given`. *Re-priced 2026-10-01 with
   tools/malloc-census.c:* `objects` makes no Env any more (method frames
   are pooled), and the `given` loop is level with a bare block since
   ee4a533a. What allocated per iteration was an `if`/`else` branch's Env,
   the multi-variable `for`'s fresh Env and two map nodes, and the topic's
   map node in a `for` body that declares — all gone, see task 7. Left: a
   block called as a Code (`.map({ … })`) defines `$_` and `@_` as map
   nodes per call, and `@_` is built even when the body never reads it
   (Rakudo gives `@_` only to a block that mentions it, but this engine's
   6.d fall-through for nested blocks needs a deep scan first).
4. **The multi-dispatch cache, and the redispatch context built only for a
   body that redispatches** (task 6, V6 P2, issue #47). `multiwhere` is the
   last perf-guard kernel slower than v4.0.1. *Done (task 6):* `multimeth`
   −52%, `multiwhere` −46%. The redispatch frame is still built per call, but
   it no longer allocates, so making it lazy is not worth a body scan.

**Tier 2:**

5. `--cnp` sees more loop sources: `for @array`, `for %h.kv`, the
   statement-modifier `for`, then `.map`. Of the 40 programs in `examples/` and
   `tools/bench/`, 20 have no loop it counts at all, and since 2026-09-29 a
   kernel may index and call, so reach is now the limit.
6. Memory without touching `Value`: `ObjectData::boxed` behind a pointer
   (128 of 280 bytes), the 83 `make_shared<Value>` sites onto the slab and
   Pairs with an inline key, `VarExpr`'s declaration-only fields behind a
   pointer, and `Callable` split into a per-AST part and a small closure
   (V6 P3 items 4, 5, 7, 8).
7. Packed native arrays: `my int @a` as contiguous `int64` (V6 P3 item 6).
   The 26× row in V6's memory table, SPEC-DIVERGENCES #14, and contiguous data
   for `--cnp`.
8. Task 5's rest, done as hot/rare splits of whole functions, and the
   frame-size ceiling in `budget.raku`.
9. Task 10 on arm64: `--cnp` on by default for hot loops, the threaded-program
   decision, the kernel arena (CNP-PLAN P2), then `--jit` retired once item 2
   has landed.

**Tier 3, measure first or wait:** the built-in method call-site cache (task 3,
blocked on a per-segment audit; also what `textsplit` waits on); handlers for
more node kinds (task 8, first reading −1.1% mean); Rats without their
`shared_ptr<BigInt>` pair (task 7); `--types` N1–N3 (a correctness contract
since N0); and `Value` below 80 bytes (VALUE32-PLAN: an architecture change
now, not a layout one).

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
`streq` +41% ([BENCHMARKS-HISTORY.md](../findings/BENCHMARKS-HISTORY.md#v401-to-v500)). The
split has already recovered part of it.

*Moved after task 6:* tasks 3–6 rework the same paths, so a regression found
now would mostly be fixed by them anyway. What is left then is measured once.

*Measured 2026-09-30,* local arm64 builds of v4.0.1, v5.1.0 and a1be403f,
interleaved, best of 5. Of the seventeen BENCHMARKS.md programs, a1be403f is
faster than v4.0.1 on fourteen (`fib` −22%, `loopsum` −39%, `mainwhen` −66%,
`arraypush` −7%, `objects` −4%, `streq` −4%, …) and level on `sortnums`
(+0.8%). Two are still slower: `textsplit` +8–10% (v5.1.0: +18%) and
`multiwhere` +8–9% (v5.1.0: +14%). All nineteen perf-guard kernels but
`multiwhere` (+4%) are faster than v4.0.1.

Neither has a commit to blame. Both were already there before the file split
(890424e0: +21% and +17%), and a bisect of v4.0.1..890424e0 by ratio to
v4.0.1 climbs without a step: `textsplit` 0.99 at 84 of 168 source commits,
1.03 at 95, 1.07 at 105, 1.08 at 110, 1.12 at 115, 1.16 at 120 — through
the Roast bursts (1e77393d … b3119f71, 214,524 → 218,150 passing), each of
which adds its checks to the method-dispatch and assignment paths the two
programs live in. The profiles agree: the growth is in `methodCall` and its
segments and in `evalAssignInner`, spread thin. It is the price of those
fixes; half of it is won back, and the rest waits on method-call-site caching
(task 3) and the representation work (task 7).

- [x] Take task 1's table: which kernels are still slower than v4.0.1.
- [x] For each, find the commit by ratio against a fixed reference binary,
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
  *In part:* the 37 method-name compares in `eval`'s own MethodCall arm, the
  `CORE-SETTING-REV` test every variable read made, `methodCall`'s 58 and
  the NameTerm arm's, are `opEq` (a length test first) — they were `strlen`
  calls (6% of the objects benchmark); a class name's CORE-enum and
  core-type lookups are decided once per node (`objnew` −12%). The segments'
  1,828 compare through `MName` (length and first 8 bytes) already. What is
  left is the call-site cache itself: skipping the segments that cannot
  answer is not safe without auditing, segment by segment, which of their
  arms depend on the arguments or the invocant's state.

*Done when:* no operator or method-name string compare is left on the path any
`perf-guard` kernel takes.

### 4. Read `tctx_` once per function, not once per use

About 19% of an integer loop's samples. `evalAssignInner` reads `tctx_` 120
times, `exec` 96.

- [ ] Take `ExecContext& tc = tctx_;` at the top of the hot functions —
  `runLoopBody`, `evalAssign`, `evalAssignInner`, `exec`, `tryCondBool`,
  `evalBinary`'s operand lambda — and use it throughout. *Done:*
  `execBlock` (already), `runLoopBody`, `execPlainBlock`, `callPlainSub`,
  exec's expression-statement arm, eval's inline variable read, the
  assignment lane and evalBinary's fast shape (`padPtrIn` takes the scope
  the caller already read). The recursion guard's two thread_locals are one
  `StackBounds t_stack`; exec reads the subscript-refusal counter only for
  a subscript statement. `redispatchStack_` is still a guarded
  thread_local read on every call: it has 65 uses and an element type
  nested in `Interpreter`, so it did not move with the call registers.
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

- [x] Measure each hot function's frame (`-fstack-usage`). Over 4 KB (the
  probe interval) before this task: `exec` 10.1 KB, `evalAssignInner` 9.4,
  `eval` 9.3, `bindParams` 8.8, `lvalue` 7.3, `applyArith` 6.9,
  `evalBinary` 6.7, `evalIndex` 6.7, `callCallableRaw` 6.6, `evalUnary` 5.8,
  `evalCall` 4.5.
- [x] `exec` 3.5 KB: its `for` and `given` arms are `execForStmt` and
  `execGivenStmt`. `eval` 2.2 KB: its MethodCall arm is
  `evalMethodCallExpr`, and its VarExpr arm keeps only the common read
  (`VarExpr::plainRead`), the rest in `evalVarExpr`. `applyArith` answers
  Int op Int itself and hands the rest to `applyArithGeneral`.
- [ ] The rest of the list: `evalAssignInner`, `evalBinary`, `evalIndex`,
  `evalUnary`, `lvalue`, `evalCall`, and `callCallableRaw`/`bindParams` for
  the calls that do not take the lean path. *Measured:* these have no
  dominant arm. Clang's frame layout (`-Rpass-analysis=stack-frame-layout`)
  shows the largest single object at 456–608 bytes against 4.3–9 KB, dozens
  of 128-byte Value temporaries across many rare arms. Moving one block out
  of `evalBinary` (its X/Z metaops, then its DateTime arm) changed its frame
  by 0 and 48 bytes; a small front answering its fast shape, the general
  path behind it, made `fib` 4–5% SLOWER (two calls where there was one).
  What is left needs each function split into a hot half and a rare half,
  not a block at a time.
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
- [x] A lean path for a plain sub. What was left of `callCallableRaw`'s own
  ~20% was spread thin across ~24 KB of code. *Done:* `callPlainSub`, for a
  sub whose `Callable::plainShape` is 1 (untyped `$` positionals, no trait,
  `bodyWork` 0, no CATCH/CONTROL, no `-->`/`is rw`) called with exactly its
  arity and no named, Junction, Mu or Buf/Blob argument: the frame, the
  binding, the call bookkeeping, the statement loop and the exits, and none
  of the rest. The one-shot call registers it has to see are
  `ExecContext` members now (they were six thread_locals). `fib` −22.6%.
- [x] Method frames take pads. `invokeMethod` never attached the body's
  `PadLayout` (only `callCallableRaw` did), so every method call put `self`
  and each parameter into the frame's hash map, and the pooled frame freed
  the nodes on release. *Done:* `attachPads` on both paths, a `self` slot in
  a method's layout (reads still go through `Env::selfSlot`), no `%_` hash
  built for a call with no named argument, and `methodCallInner`'s
  per-call name tests after the type test that guards them. Interleaved
  against 3ce93e4d, best of 3: `method` −22.9%, `privmeth` −22.5%,
  `attrread` −16.3%, `multimeth` −9.3%, `multiwhere` −3.5%, mean −4.0%.
  Then two more per-call costs: a method with no parameters built an `@_`
  array on every call (only a body that reads `@_` gets one now,
  `Callable::atArgsScan`; Rakudo rejects `@_` in a method outright), and an
  assignment to an attribute walked the whole Env chain four times, for the
  `where`/smiley/coercion/default tables that only a lexical declaration
  writes (`$!n = $!n + 1` −29%, `$!n++` −17%, a paramless call −20%, on
  scratch kernels; perf-guard has no attribute-store kernel). With them,
  best of 3 against 3ce93e4d: `privmeth` −23.6%, `method` −22.8%,
  `attrread` −16.8%, `multimeth` −12.0%, mean −4.7%.
  *Not done:* a call-site cache that skips `methodCall`/`methodCallInner`
  for a user object. Too many arms in front of the user-object path still
  apply to one (a Junction argument, custom HOWs, the Seq tag on a
  `keys`/`values`-named result), so a bypass would have to copy those rules.
- [x] The multi-dispatch cache (V6 P2, issue #47), and a dispatch that
  allocates nothing. Four parts:
  - **The dispatch itself.** Both dispatchers (subs in callCallableRaw,
    methods in invokeMethod) kept the dispatch in a std::function holding
    ~250 bytes of captures, a shared `visited` vector, score vectors, a
    positional-parameter vector per candidate, a positional-argument copy per
    candidate and a samewith closure that copied the invocant: 10 heap
    blocks per multi-method call. The dispatch is a lambda on the frame, the
    vectors are inline or leased from a per-thread pool, and the frame's
    closures capture one pointer. A multi-method call allocates nothing now.
  - **`where` Code built once** (`Param::whereStatic`, `staticWhereCode`): a
    `where` whose Code reads nothing per call — a block over its own `$_`
    and outer lexicals, or a WhateverCode over literals — is built once in
    the candidate's declaration scope, not in a fresh scope holding `self`,
    the earlier parameters and `$_` at every check. A WhateverCode over a
    variable (`* > $lim`) reads it when it is made, so it is rebuilt as before.
  - **The cache** (`Callable::dispatchCache`, `dispatchCacheLookup`): a
    monomorphic entry per dispatcher, published once, keyed on each argument's
    kind (with native width) and class, and the winner by index. Only where
    the winner cannot depend on more: plain Int/Num/Str or tag-free class
    instances, and candidates whose parameters are all `$` positionals typed
    by a core numeric/string type or a user class, with no `where`, literal,
    coercion, capture, sub-signature, default, `is rw` or `is default`.
    Invalid once the candidate count or the symbol generation (bumped in
    noteSymbolMutation) moves.
  - **Measured** against 96c07845, interleaved best of 3: `multimeth` −52%,
    `multiwhere` −46%, perf-guard geomean −7.3%; a two-candidate multi sub
    −48% in retired instructions; every other kernel within ±1% in retired
    instructions.
- [ ] The redispatch frame is still built per call (a copy of the arguments
  for callsame); building it only for a body that can redispatch would save
  that copy.
- [ ] Split `Callable` into an immutable per-AST part and a ~64-byte closure
  (V6 P3 item 7).
- [ ] A variable declared in a `given`/`when` block falls back to map lookups:
  the same loop runs 3.3× slower there. Give those blocks pads
  ([PADS-PLAN.md](PADS-PLAN.md)).

*Done when:* `fib`'s gap is under 4×.

### 7. Smaller values, fewer allocations (V6 P3)

Each is its own plan, in V6's order of evidence per cost:

- [x] [VALUEHASH-SMALL-PLAN.md](VALUEHASH-SMALL-PLAN.md): a small first chunk
  instead of the 4,032-byte deque block. Allocation on `objects` −74%, but
  its peak RSS and time did not move (the block was churn); a program that
  keeps 100k objects and 50k small hashes: peak RSS 531 → 175 MB, time −16%.
- [x] Per-iteration Envs and map nodes, which is what inline slots were
  for. An `if`/`else` branch takes its scope from the call-frame pool
  (whose release now resets every Env field, so a `no strict` or
  `use fatal` in a branch cannot outlive it); the multi-variable `for`
  reuses one scope as the single-variable path does; and a reused loop
  scope keeps its loop variables' map nodes (`clearBindingsKeeping`).
  Against 24c577db, best of 3: `for %h.kv -> $k, $v` −35%, an if/else
  loop −12%, `for ^N { my $x = … }` −12%; the census shows no allocation
  per iteration in any of the three.
- [ ] `$_`/`@_` for a block called as a Code (`.map({ … })`): two map
  nodes and an Array per call.
- [ ] Small Rats inline. *In part, without touching `Value`:* a BigInt's limbs
  are a `LimbVec` with four limbs inline (anything under 10^36), and the
  assignment lane takes a Rat accumulator. `rats` −7% and −7%, `bigint` −7%;
  the kernel's allocations per iteration 10 → 6. What is left per Rat is its
  cold block and the two `shared_ptr<BigInt>`s — the part that needs the
  172 `ratN()`/`ratD()` sites to stop handing out a `shared_ptr`.
- [x] [VALUE32-PLAN.md](VALUE32-PLAN.md) design A: `Value` is 80 bytes, not
  128 (22605246). perf-guard −2.6% mean, every kernel level or faster, and a
  third less memory (a million-element array 258 → 164 MB, `hashfill` 100 →
  69 MB). The step to 72 (the cold block out of the struct) measured +1.8%
  mean and was not kept, so design A ends at 80. Going lower is an
  architecture change (kind-specific bodies, or the container leaving the
  value), not a batch.

*Done when:* V6's memory table has no row worse and the kernels that allocate
(`hashfill`, `arraypush`, `objects`) have closed half their gap.

### 8. Closure compilation (the ladder's second rung)

−24…−37% on the lean walker, where quickening was already in place.

- [x] Prototype in the engine for the node kinds the kernels spend time in:
  each node compiled once, on first evaluation, into a small handler with its
  slot, operator id and fast path bound; the `switch` stays as the fallback.
  *Done for one kind (cc2af7fc):* `Expr::handler`, which eval calls ahead of
  its switch; `Interpreter::binaryFastHandler`, installed by evalBinary on a
  node that reaches its fast shape.
- [ ] Judge it against the probe's number with the usual discount before going
  wider. *First reading* (interleaved against a1be403f, best of 7):
  mean −1.1% — `asg` −6.4%, `mainnext` −6.2%, `hash` −5.5%, `fib` −3.7% —
  and `objnew` +2.1% (15 runs): every eval now pays the handler test, and a
  program of method calls and object construction has little for a handler
  to take. Far from the probe's −24…−37%, which is expected: round 3 had
  already taken eval's frame from 9.3 to 2.2 KB and moved its big arms out,
  so most of what the probe's L2 → L3 step removed was gone. What is left
  for handlers is the kinds whose own frame is still large — the next is
  the assignment lane (evalAssign), then evalIndex and evalUnary's hot paths
  — each judged the same way. *Measured and not kept:* a second Binary
  handler for a node with no fast shape (`fib($n-1) + fib($n-2)`), which
  evaluated the operands and went straight to evalBinary's post-operand
  arms, lifted into a function of their own: `fib` +1.1%, mean +0.4% — that
  function's frame is as large as the one it bypassed. It is not the flat threaded loop or register IR V6 measured at a ~4%
  ceiling: those change the loop, this removes the per-node `switch` and its
  shared giant frame.

*Done when:* the prototype has a measured verdict, and either lands for the hot
node kinds or goes into V6's "measured, and not pursued" table with its numbers.

### 9. Fused integer leaf operations (the ladder's top rung)

2.1–3.6× on the probe. A `Binary` whose operands are variables or literals runs
an int64 lane with the overflow check and stores the Int in place — the
UNBOX / `--cnp` idea applied per node rather than per loop.

- [ ] Build it on task 8's handlers, for `+ - * < <= > >= == !=` over native
  and plain Int operands. *In part:*
  - `$x = $a op $b` for `+ - *` (`fusedIntAssign`): into the slot of a plain
    Int or a full-width signed `int`, for a right side compiled to its fast
    shape; `asg` −9%, and the kernels counting with `my int $n` −2…−7%.
  - `$x op= $y` on two machine Ints writes `.i` (`intOpAssignInPlace`, 477dac44).
  - The six comparisons as a CONDITION: `while`/`loop` had `tryCondBool`
    (TARG lever B); the ternary takes it now (`fib` −5%), and it refuses a
    shadowing `infix:<…>` as evalBinary does.
  - An `if`/`unless` condition whose value nothing reads
    (`IfStmt::condValueUsed`: no branch or else binder, no placeholder)
    takes `tryCondBool` too, which now also answers `eq ne lt gt le ge` on
    two untagged Strs; a sunk if that is not taken no longer allocates the
    empty Slip it returns. `streq` −23%.
  - `$n++`/`$n--` on an untyped pad variable holding a plain Int steps `.i`
    (`plainIntStepSlot`). `streq` another −37% (216 → 106 ms over the two),
    `regexloop` −13%.
  - Typed subtrees (`Binary::typedTree`, `typedTreeValue`): a Binary whose
    operands are themselves `+ - * / %` and prefix `-` over plain `$`
    lexicals and Int/Num literals, under at most one comparison, is evaluated
    as machine numbers with one Value built at the end. The shape is decided
    once; each leaf's type is checked on every evaluation, and anything the
    typed rules would answer differently from applyArith (an Int overflow,
    Int `/` Int, a zero divisor, a native, a shadowing operator) declines and
    takes the full path, which is safe because the leaves have no side
    effects. A condition takes it through tryCondBool; `$x = <subtree>` writes
    the slot in place (`fusedTypedAssign`); a node whose leaves keep declining
    retires after 64 misses.
  - A Num in the fast shape answers in binaryFastHandler (`numFastArith`)
    instead of walking applyArithGeneral to its double arm.
  - The declaration lane (`Assign::simpleSlot` 6, `declLane`): `my $x = EXPR`
    with no type, trait or constraint, and an EXPR that is a literal, a plain
    `$` read, a typed subtree or a fast-shape Binary over two machine numbers,
    declares and stores without lvalue's declaration arm and the full store.
    The EXPR is evaluated without side effects, so a value the lane cannot take
    leaves the full path to start over.
  - Measured against 10a4b97a, best of 3: `types/mandel-plain` 0.71 → 0.26 s
    (−63%), `types/numloop-plain` −33%, `types/intloop-plain` −34%, a Num
    compare-and-declare loop −41%. `examples/mandel.raku` is level: its
    decimal literals are Rats. perf-guard level (retired instructions within
    ±0.4% on 15 of 19 kernels).
  - `@a[I]++`, `%h{K}++` and `$!n++` step an Int in place
    (`plainIntStepTarget`): an existing element of a plain `@`/`%` lexical
    (no element type, shape, laziness, List or Bag) at a subscript evaluated
    without side effects — a literal, a plain `$`, a typed or fast-shape
    expression; a hash key is its Str, or an Int's decimal — or an untyped,
    unconstrained `$!` attribute, through the same slot lvalue reaches
    (`attrSlotFor`). Anything else, a missing element included, takes the
    full path. Against 8cc40284: perf-guard `hash` −24% (geomean −1.3%), and
    in retired instructions an element-increment loop −22%, an attribute one
    −43%. A loop keyed by another subscript (`%h{@k[$i]}++`) is +2% in
    retired instructions: the lane's own refusal is 16 per iteration, the
    rest is how the compiler laid out the surrounding path.
  - *Not done:* typed Rat leaves.

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
