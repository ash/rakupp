# Parallel scaling — plan

*Written 2026-10-07 at `d78d1cb0`, before any code. Goal: a CPU-bound fan-out
with `start` scales the way the hardware allows. A variable that no other
thread can reach costs nothing extra while workers are live, and a compiled
loop stays compiled inside `start`. No Raku-visible behaviour changes: what
is racy stays racy, and what is guarded stays guarded.*

Follows the PARALLEL-PLAN.md campaign (parallel mode by default, the
no-crash contract) and the 2026-10-07 re-measurement of
[PARALLEL-SPEEDUP.md](../../guide/PARALLEL-SPEEDUP.md) for #129.

**The numbers a stranger can re-measure:**

```bash
rakupp tools/bench/parallel/cpu-fanout.raku 4 300000 serial
rakupp tools/bench/parallel/cpu-fanout.raku 4 300000 parallel
```

Measured while planning: 2026-10-07, M3 (4 performance + 4 efficiency
cores), `5.2.1-116-g333ebb29`, best of 3. The machine was **not quiet**: the
load average was 3.7, and one stray process was spinning at 100%. P0 redoes
these numbers on a quiet machine. The ratios are what matter here:

| threads | C threads, same loop (the hardware) | Raku++ `cpu-fanout.raku` |
|---|---|---|
| 2 | 1.95× | 1.64× |
| 4 | 3.47× | 2.78× |
| 8 | 4.81× | 2.79× |

The gap is inside each thread. One unit of `work` takes 0.22 s alone. With 2
workers, each worker takes 0.26 s; with 4 workers, 0.29–0.33 s. A
contention-free fan-out should leave each worker at its solo time. Rakudo's
workers run at solo time.

## Status, 2026-10-08

P0, P1, P2, P3, P5 and P6 are implemented. P4 turned out not to be needed. P7 and P8 are open. Measured on
the same M3, interleaved best of 9 under `nice -n 10`, with
`tools/bench/parallel/sweep.raku`. The baseline is a binary built at
`e4ee17cd`. The load average was 3.4, so ratios matter more than the
absolute times:

| | before | after | C threads, same sitting |
|---|---|---|---|
| `cpu-fanout` N=1 | 0.86× | 0.94× | 1.00× |
| `cpu-fanout` N=2 | 1.70× | 1.88× | 1.97× |
| `cpu-fanout` N=4 | 3.26× | 3.65× | 3.81× |
| `cpu-fanout` N=8 | 3.16× | 5.32× | 6.13× |
| each worker at N=4, against its solo time | 1.21–1.45× | 1.09–1.13× | |
| `atomic-counter` contended / sharded / counters, N=4 | 0.82× / 3.28× / 3.24× | 0.93× / 3.49× / 3.30× | |
| example 3, `$M` from outside, N=4 | 0.41× | 3.84× | |
| example 3, `$m` a parameter, N=4 | 3.76× | 3.84× | |
| the same, first call from four workers at once, runs over 0.1 s | 47 of 50 | 0 of 50 | |
| a compiled loop beside one idle worker (0.036 s alone) | 0.347 s | 0.037 s | |
| an interpreted loop beside one idle worker (0.062 s alone) | 0.074 s | 0.070 s | |
| the FAQ program, its `start` side | 0.244 s | 0.224 s | |

- **What is left per worker is not the stripe.** In `cpu-fanout` at N=4
  `RAKUPP_STRIPE_STATS=1` counts 8 stripe acquisitions in the whole run, where
  `RAKUPP_PRIVATE_SLOTS=0` counts 3,600,024. The FAQ program also counts 8.
  The C loop on the same machine pays 5% per thread at N=4.
- **The targets in P3's exit.** N=4 within 10% of the C ceiling is met: 0.96
  of it. A max/min spread of 1.10 over 9 runs is met at N=4 (1.10) and N=8
  (1.11). N=1 at 0.97× is not met: 0.94×. Each worker within 5% of its solo
  time is not met: 9–13%, against C's 5%.
- **Single-threaded speed is level, within the layout noise of this binary.**
  `perf-guard` A/B, best of 3 pairs: 15 of 19 kernels within ±2%. `fib` read
  +6.5%, `strscan` +3.2%, `hash` +2.5% and `loopsum` +1.8%. The code `fib`
  runs is a routine kernel no change of this plan touches per call, and
  100 never-called lines in `IntKernel.cpp` alone moved it +7%. A `my int`
  interpreted loop read +2–4% in four sittings; the same padding in
  `InterpreterCore.cpp` moved it +1.2%. One real cost was found and removed:
  an `Env` 8 bytes larger cost 3% on that loop, so `Env::layout` became a raw
  pointer (see P2). The official gate is `perf-guard --check` in release
  conditions.
- **The gates run:** Roast unchanged (218,413 / 218,420 assertions without
  skip/todo, 1,421 / 1,424 files, the same three files failing). `t/stress`:
  54 / 54 in both modes. The `--cnp` lane of `t/jit/run.raku`: 987 agree,
  0 differ. `t/race/evalcall.raku`: 0 deaths in 4,000 runs. Not yet run:
  `t/run.raku`, the module battery, the adopters gate and the TSan job.

---

## Why `start` scales badly today

**1. Every lexical access takes a pool lock while any worker is live.** In
parallel mode, while `liveWorkers_ > 0`, every plain scalar read copies the
Value under `ParStripe`. So does every store
([Interpreter.h:2377](../../../src/Interpreter.h),
[InterpreterCore.cpp:28945](../../../src/InterpreterCore.cpp) for reads,
`evalAssign` for writes). That is the PARALLEL-PLAN P3 "torn-copy contract".
A Value is 80 bytes and carries pointers, so a copy torn by a concurrent store
addrefs a dead control block. The stripe is one of 64 static
`std::recursive_mutex` picked by `(addr >> 4) & 63`
([InterpreterOperators.cpp:2306](../../../src/InterpreterOperators.cpp)).
The pool has no bit mixing and no padding, and two 64-byte mutexes share each
128-byte cache line.

- **The flat cost.** A lock and an unlock on every `$s` and `$x`, with
  nothing to contend with: this is the 0.85× at N=1 in PARALLEL-SPEEDUP.md.
- **The collisions.** Workers whose variables land on the same stripe take
  turns, though they share no data. `sample` of a 4-worker run: about 30% of
  worker time is in mutex code, mostly `__psynch_mutexwait` under
  `ParStripe`. lldb attached mid-run (launching under lldb changes the
  allocation pattern): in 1 run of 3, two workers sat on the same two stripes,
  5 and 10. Which run collides depends on where malloc puts each frame. That
  is the run-to-run spread: the FAQ program measured 1.83× to 2.81× over five
  runs, where Rakudo measured 2.35× to 2.53×.

**2. Kernels refuse while any worker is live.**
[IntKernel.cpp:2743](../../../src/IntKernel.cpp) refuses every loop kernel
while `liveWorkers_ > 0 || cuedLoads_ > 0`, and `--cnp` refuses at
[Jit.cpp:1497](../../../src/Jit.cpp). Inside `start` a loop therefore runs
interpreted, and it also pays the stripes. The plain run is compiled, so four
threads can lose to one: 0.143 s against 0.372 s, 0.38×, in PARALLEL-SPEEDUP.md
example 3.

Long-lived workers make this permanent. A `signal(SIGINT).tap`, a socket
listener or a live `Supply.interval` keeps the count above zero for the whole
run. So a server turns off every loop kernel, and pays every stripe, from its
first tap to its exit.

**3. The first-call race.** A routine kernel is compiled on its first call.
A worker that calls the routine while another thread compiles it sees state 2
and runs that whole call interpreted
([IntKernel.cpp:2648](../../../src/IntKernel.cpp)). That happened in 1 run of
10 at N=4.

**What the three have in common:** each treats every variable as reachable
from another thread. Most are not. In example 1's `work`, `$s` and `$x` are
declared in the routine and nothing closes over them, so only the thread
running that call can see them.

## The idea: a slot is shared only if something can share it

Two layers, both conservative. Either layer saying "shared" is enough.

- **Static: a per-slot bit computed once per layout.** `PadLayout` grows a
  `sharedMask` (a `uint64_t`, since a layout holds at most 64 slots), next to
  `simple`. `resolvePads` sets it under `padMu_`, before any frame of that
  body runs. It is set for a slot that any deferred body mentions (closures,
  `start`, `gather`, `lazy`, nested `sub`s and methods), or that a
  `hyper for`/`race for` body mentions. Every slot of the owner and of its
  enclosing owners is set when the subtree contains `EVAL`, a symbolic
  `::("…")`, a pseudo-package, `take-rw` or `.BIND-POS`.
- **Dynamic: a per-frame escape mask, set at run time.** `Env` grows a
  `padEscaped` (an atomic `uint64_t`, beside `padLive`). It is set wherever a
  slot is aliased or exposed by name while the program runs: cell
  promotion, rw binding, rw-links, Env-slot Proxies, pseudo-stashes, `EVAL`.
  Every such event happens on the frame's own thread before the alias can be
  published, so the owner sees its own write.

A slot is **private** when neither bit is set, it is not a cell
(`pad[ps].isCell()`, as a backstop), and the program has not switched the
feature off. A private slot is read and written without the stripe, and a loop
kernel may hold it in its frame while workers are live.

**The invariant to keep:** another thread can reach a frame's slot only
through a path that is either visible in the owner's own source (the static
layer) or created at run time on the owner's thread before the alias exists
(the dynamic layer). The phases below enumerate the paths. A path that
neither layer covers is a crash bug in this plan.

### Every way another thread can reach a slot

| # | Route | How it reaches the slot | Caught by |
|---|---|---|---|
| 1 | A closure value over the frame: pointy and bare blocks, `sub {}`, blocks given to `map`/`grep`/`sort`, `start`, `gather`, `lazy`, `supply`, `.then`, taps, `whenever`, `signal`, `cue`, `Thread.start` | The closure holds a `shared_ptr<Env>` (InterpreterBinding.cpp:165) and reads outer names through `Env::find` → `padFind` | static: names mentioned in deferred bodies |
| 2 | A named `sub` or method declared inside the routine and called from a worker | `closure = tctx_.cur` at hoist time (InterpreterModules.cpp:4360) | static: `SubDecl` bodies, position-insensitive because subs are hoisted |
| 3 | A `hyper for`/`race for` body | It is an inline block with annotated references, run on workers whose scope parent is the spawner's (InterpreterModules.cpp:2819–2846) | static: everything a body with `f->hyper` mentions |
| 4 | `$a := $b`, `my \x = $b` | The source is promoted to a cell (InterpreterCore.cpp:15120–15128) | dynamic |
| 5 | `is rw`/`is raw`/`\x` parameters (`bindArgCell`) | The caller's slot is promoted (InterpreterBinding.cpp:6314, 6330) | dynamic |
| 6 | An rw parameter that `bindArgCell` declined (`rwLinks`) | Writes re-evaluate the caller's annotated argument, from any thread (InterpreterCalls.cpp:341–358) | dynamic: `setupRwLinks` marks the caller's slot (InterpreterCore.cpp:9554) |
| 7 | `for $a -> $v is rw` on one scalar | Promotion (InterpreterCore.cpp:3201–3217) | dynamic |
| 8 | `given`/`with $v`, `f(:$v)`, `a => $v`, `\($v)`, `List.new($v)`, `*@l is raw`, `@a[i] := $v`, slice binds | `varCell` → a cell (InterpreterCalls.cpp:1459) | dynamic |
| 9 | `take-rw $x`, `.BIND-POS(i, $x)`, `k => my $x` | An Env-slot Proxy, read by name from wherever it travels (InterpreterCalls.cpp:1636–1660) | dynamic: Proxy creation marks the slot |
| 10 | `MY::`, `OUTER::`, `LEXICAL::`, `UNIT::` | The stash holds a `shared_ptr<Env>` and can be handed to a worker (InterpreterBinding.cpp:2930) | static (`viaPseudoPkg`), plus dynamic (stash creation marks the Envs it reaches) |
| 11 | `EVAL`/`EVALFILE`, direct or through `&EVAL` | The EVAL scope parents on `tctx_.cur` and runs by name (Interpreter.h:2639) | static (`nameEvalsCode`), plus dynamic (EVAL marks the whole scope chain) |
| 12 | `::("$name")` | `find` by name (InterpreterCore.cpp:28146, 10979) | static: `SymbolicRef` marks all |
| 13 | `CALLER::`, `CALLERS::`, `DYNAMIC::`, `callframe(N).my` | A worker's `dynStack` starts with the spawner's scope (Interpreter.cpp:5527) | the program-wide off switch: any use, at parse or EVAL time |
| 14 | `state`, `our`, `$*x`, `is dynamic` | — | never slots (`slottableDecl`, Interpreter.cpp:4351); they keep the stripe |
| 15 | `$_`, `$/`, `$!`, loop variables, pointy parameters | — | never slots, so they keep the stripe until P4 |
| 16 | Module code finding a mainline slot through `global_` | Module units chain to `global_` (InterpreterModules.cpp:673); unverified | P1 verifies; if real, mainline slots are shared once a module is loaded |

Containers are outside the analysis. An Array or Hash *object* in a private
slot can still reach a worker as an argument (`start f(@a)`), so element and
structural stripes stay as they are. Object-level escape analysis is a
non-goal.

---

## Phases

### P0 — Measure first (no engine change)

**Done** 2026-10-08. The benches are
`per-worker.raku`, `idle-worker.raku`, `faq.raku`, `kernel-fanout.raku`
(example 3), `ceiling.c` and the driver `sweep.raku`, which interleaves
configurations inside rounds and A/Bs a second binary (`--vs`) or an
environment (`--vs-env`). `RAKUPP_STRIPE_STATS=1` landed with P6. The
baseline was measured beside the result, interleaved, rather than first: the
status table above.

- **Benches**, in `tools/bench/parallel/`:
  - `per-worker.raku`: each worker's own time against one unit run alone.
    This is the direct measure of the tax.
  - `idle-worker.raku`: a loopsum-shaped loop on the main thread with one idle
    long-lived worker (`signal(SIGINT).tap` or `start { sleep … }`).
  - The FAQ program (a `my int` loop with `-> $i`).
  - `ceiling.c`: the same LCG loop on C pthreads, the hardware number for the
    sitting.
- **Baseline at the base commit**, on a quiet machine, interleaved best of 9:
  cpu-fanout N=1/2/4/8, per-worker, atomic-counter (three strategies),
  PARALLEL-SPEEDUP.md example 3 (both shapes), FAQ, idle-worker. Add the share
  of mutex frames in `sample` at N=4.
- **Optional `RAKUPP_STRIPE_STATS=1`:** contended acquisitions per stripe
  (`try_lock` then `lock`), printed at exit. It engages only inside the
  live-workers branch. It is the instrument for P3 and P6.

Exit: the baseline table, written into this plan.

### P1 — The analysis, with no consumer yet

**Done** 2026-10-08, in a different shape from the one
below. Collecting mentions at each point where the annotation walk skips a
node depends on that list being complete. Instead, a second pass over the
WHOLE owner body (`ShareSweep` in Interpreter.cpp) sees every child of every
node, signatures, class bodies and regex text included. It marks a slot
shared when:

- a mention of its name is not annotated, since it is then looked up by name
  and may run on any thread;
- an annotated mention sits where the runtime keeps the expression and
  evaluates it later in the creating scope: a statement with a curried `*`
  (WhateverCode keeps `scope` and the expression, InterpreterCore.cpp:13371),
  the left side of `xx` (a thunk, lazy for `*` and for an Inf count),
  sequence operators, `take-rw`/`lazy`/`.BIND-POS` arguments, phasers, and
  `hyper for`/`race for` bodies;
- the owner reaches names at run time (EVAL, a computed symbolic name or
  method name, a pseudo-stash). Then every slot it can name is marked.

A name marks the owner's layout and the inline blocks around the mention, not
sibling blocks: marking every layout by name made the `--cnp` case
`inline.raku` lose all eight of its kernels.

Two corrections to the route table:

- Statement-level `hyper for` parses as `do { for … }`, so its body is a
  closure of its own and reaches the routine's variables by name. Route 3
  through annotated slots does not occur in any shape the parser makes today.
  The sweep's rule for it stays.
- **Route 16 is real.** A module's unit scope chains to `global_`, so
  `::('$secret')` in a module reads the mainline's `my $secret`. Raku++ also
  accepts a bare `$secret` there, which Rakudo refuses at compile time. A
  module's sweep records the names it mentions (`unitSharingScan`), and
  `applyModuleEscapes` sets those mainline slots escaped. That happens when
  the module loads, or when the mainline layout is made, for a `use`
  executed while parsing.

The program-wide switch is set by the sweeps (resolvePads, a module's or an
EVAL's unit) rather than by the parser, so precompiled and `--slim` ASTs get
it too. `RAKUPP_SLOT_TRACE=1` prints one line per slot.
`t/regression/slot-sharing.raku` checks a snippet per route, and fails 24
checks on a binary without the trace. Not done: the dynamic-variable fallback
(InterpreterBinding.cpp:3219) is still unverified.

- `PadLayout::sharedMask`, plus a reason per set bit for the trace.
- The walk, in `resolvePads`
  ([Interpreter.cpp:4319](../../../src/Interpreter.cpp)): at each point where
  `annE`/`annStmts` skip a deferred node today (the `BlockExpr` default arm,
  non-`kNowUnary` operands, `SubDecl`/`ClassDecl` at 4469–4513 and 4669), run
  the mention collector (`collectMentionedE/S`, Interpreter.cpp:4845). Mark
  the matching slots of the owner and of every inline layout on the way. The
  collector also needs to:
  - walk class and method bodies, regex code blocks and attribute defaults;
  - count implicit topic uses (`.say`, `when`, `m//`, `s///`) as mentions of
    `$_`;
  - mark everything a `ForStmt` body mentions when `f->hyper != 0` (the arm
    at 4600 does not look today).
  
  A routine's walk covers every body nested in it, so its mask is complete
  when its `resolvePads` returns.
- **The program-wide switch:** the parser sets it on the first `CALLER::`,
  `CALLERS::`, `DYNAMIC::` or `callframe`, and so does EVAL'd text when it
  compiles. Once set, no slot is private for the rest of the run.
- **`RAKUPP_SLOT_TRACE=1`** prints one line per slot, for example:
  - `slot: work $s private`
  - `slot: <mainline> $M shared (mentioned by sub work)`
- **A test:** `t/regression/slot-sharing.raku` holds one snippet per route in
  the table and asserts its trace line.
- **Verify the two unverified routes:**
  - Can module code find a mainline slot by name?
  - Can the dynamic-variable fallback (InterpreterBinding.cpp:3219) ever
    resolve to a non-twigil lexical?

Exit: the trace is right for every route. Roast and perf-guard are unchanged,
since nothing reads the mask yet.

### P2 — The runtime escape mask

**Done** 2026-10-08, except the raw-pointer audit.
`Env::padEscaped` is set by `setupRwLinks` (three sites, through
`escapeRefs`), `makeEnvSlotProxy`, `substrRwProxy`, `makePathProxy`, the
hash-element Proxy of `:=`, `makePseudoStash`, an EVAL'd unit, and a module
that names a mainline variable. A promotion to a cell needs no mask bit: the
`isCell` backstop covers the slot from then on. Pooled frames and reused loop
scopes clear the mask, under the existing `use_count() == 1` rules.

- **Hazard 1 is fixed.** `promotionSafe` lets `bindArgCell`, `:=`, for-rw and
  `varCell` promote while workers are live only for a private slot. The first
  three fall back to their link or Proxy path. `varCell` now promotes a
  private slot where it used to refuse every one.
- **Hazard 2 is fixed.** The rw write-through copies and stores under the
  stripes.
- **Three more holes, each crashing the baseline binary, are fixed:** the
  Env-slot Proxy's FETCH and STORE (`take-rw`), `substr-rw`'s, and the
  pseudo-stash's key reads and `ASSIGN-KEY`, which all copied or stored with
  no stripe.
- **`makeTypedEx` registered a new exception class in `classes_`, and
  extended an existing one's attributes, from any thread.** Four workers
  failing one bind at once died 6 times in 3,000 runs of
  `t/race/evalcall.raku` (macOS crash report, then lldb: three workers in
  `makeTypedEx`). While workers are live a throw now carries a class of its
  own instead. 0 deaths in 4,000 runs.
- **`Env::layout` is a raw pointer now.** Every layout lives as long as the
  program (the `padLayouts_` cache, or its Block in a kept AST). The 8 bytes
  the mask needed made `Env` 168 bytes, which measured 3% on an interpreted
  loop wherever the field went. Back at 160, with two fewer atomic
  reference-count updates per frame.

Open: the audit of `localRaw`/`findRaw`/`padPtr` callers that hand a raw slot
pointer outward.

- `Env::padEscaped`, set with a relaxed `fetch_or` at:
  - cell promotion: `promoteToCell` callers, i.e. `varCell`, `bindArgCell`,
    `:=` and for-rw;
  - `setupRwLinks`;
  - Env-slot Proxy creation (`take-rw`, `.BIND-POS`, `k => my $x`);
  - pseudo-stash creation;
  - `evalOwnScope`, which marks every Env up the chain.
- **Pooled frames.** Clear the mask when a frame is reused, and only where the
  existing `use_count() == 1` rules already prove nothing references it
  (InterpreterCore.cpp:8217–8228, 3254–3262).
- **The audit:** every caller of `localRaw`, `findRaw` and `padPtr` that hands
  a raw slot pointer outward. Each one is either shown not to outlive its call
  on its own thread, or it sets the mask. The list goes into this plan.
- **Fix the pre-existing unguarded promotions** (hazard 1 below) while
  touching those sites.

Exit: the escape mask is set on every route 4–11. The audit list is complete.

### P3 — Private slots skip the stripe

**Done** 2026-10-08, with one simplification. Only its
own thread can touch a private slot, so ANY access to it may skip the stripe,
whatever path produced the pointer. `SlotStripe(I, p)` therefore takes the
pointer alone, as `ParStripe` does. In the live-workers branch,
out of line, it finds the frame on the current scope chain whose pad holds
`p`. Passing the reference and the frame through the store helpers kept them
live across the store in `evalAssign`, so the helpers keep their old
signatures.

The contract programs are `t/stress/private-*.raku`, 13 of them. Under
`RAKUPP_PRIVATE_SLOTS=all` 12 fail within a few runs (the two EVAL ones in
about half), and by default all pass. `private-hyper-for` cannot fail under
`=all`: its body reaches `$v` by name (see P1). The `CALLERS::` program was
dropped. `$CALLERS::v` in a `start` block finds no frame of the spawner's
(X::Undeclared), so that route does not reach anything here. The switch
stays.

- **The predicate** is evaluated only inside the live-workers branch, so a
  single-threaded program does not run it:
  `!(layout->sharedMask >> ps & 1) && !(frame->padEscaped >> ps & 1) && !frame->pad[ps].isCell() && !privateOff`.
- **Consumers:** the lexical-slot sites, and only when the pad path produced
  the pointer:
  - reads: InterpreterCore.cpp:28945, 29538, 21302;
  - the `evalAssign` lane: 12506–12591, and its single-caller helpers 12369,
    20959, 21247, 7477 (pass the verdict as a parameter);
  - `++`/`--` on the slot branch: 24102;
  - `evalAssignInner`: 16441, 16566–16578, when `lvalue` came from `padPtr`.
  
  The name-path fallbacks (28955, 29559) keep the stripe.
- **Two switches on the same binary:**
  - `RAKUPP_PRIVATE_SLOTS=0` restores today's behaviour. This is the A/B
    instrument: P0's benches run interleaved with and without it.
  - Debug-only `RAKUPP_PRIVATE_SLOTS=all` treats every slot as private. Every
    contract test below must crash or report under TSan with `=all` and pass
    by default. That is how each test proves it can fail.
- **Contract tests,** in `t/stress/` (both modes, the TSan job), one per route
  family, each in the `ub-torn-values` shape: workers store pointer-carrying
  values through the route while the owner copies the variable.
  - a routine-level `start` capture;
  - a `map` Seq handed to a worker;
  - a nested sub called from a worker;
  - a `hyper for` body;
  - `:=`;
  - an rw parameter and an rw-link decline;
  - for-rw;
  - a Pair `.value`;
  - `take-rw`;
  - a `MY::` stash carried into `start`;
  - `EVAL`, direct and through `&EVAL`;
  - `CALLERS::` from a worker.

Exit: the P0 table re-measured. Targets, to be measured rather than promised:
cpu-fanout N=1 at 0.97× or better, each worker at N=4 within 5% of its solo
time, N=4 within 10% of the sitting's C ceiling, and a max/min spread of 1.10
or less over 9 runs.

### P4 — Loop variables and `$_`

**Not needed**, measured 2026-10-08. After P3 the FAQ program takes 8 stripe
acquisitions in its whole run (`RAKUPP_STRIPE_STATS=1`). Its `-> $i` costs no
stripe, and the program reaches 3.50×.

Loop variables, pointy parameters and `$_` live in the iteration scope's map,
not in slots. So the FAQ program's `-> $i` still pays the stripe after P3.

- **Which loops qualify:** a `for` that is not `hyper`/`race`, whose body
  contains no deferred node, `EVAL` or pseudo-package. Implicit topic counts
  as a mention. Such a loop creates its iteration scopes with
  `Env::privateMap`. Do the same for `while`/`loop`/`given` scopes where it
  is just as cheap.
- **Reads:** a `find` variant reports the Env it hit. The name-path reads skip
  the stripe when that Env is `privateMap` and the entry is not a cell or
  Proxy.
- **The P1 trace** reports these loops too.

Exit: the FAQ program's workers run at solo time. Its contract test is
`for ^N -> $i { start { … $i … } }`, plus `start { .say }` inside a loop.

### P5 — Kernels under threads

**Done** 2026-10-08, except the reentrancy audit and the
TSan pass. `tryLoopKernel` checks every `LOuter` with `cellPrivate` before
reading it. While threads are live, a compile reads a variable another thread
can reach under its stripe, and a loop with a container declines.
`kThreadsDeclined`, the high bit of `loopKernelTries`, keeps such a loop from
being recompiled on every entry until the threads are gone. The first-call
race is fixed for both routes: a caller that sees state 2 waits on
`g_compileMu` (`awaitKernelVerdict`), including the borrowed path's lost
compare-exchange. `--cnp` applies the same per-variable rule. It refuses an
array or a call while threads are live, and it holds a kernel that calls out
to the per-variable rule even with no thread live, which closes hazard 6.

The polling tests are `t/cnp/cases/shared-int-flag.raku` and
`t/regression/kernel-shared-flag.raku`. The latter runs three programs by
default, under `--cnp` and with kernels off, and hangs under `=all`.

- **`tryLoopKernel`:** while workers are live, replace the blanket refusal
  with a check at entry of each `LOuter`.
  - Resolve each to its Env and slot through a `findRaw` variant (the owning
    Env is already found at IntKernel.cpp:2934).
  - Every `LOuter` must pass the P3 predicate, or sit in a P4 `privateMap`.
  - A loop with any `LCont` container still refuses while workers are live,
    because the object may be shared through an argument.
  - `cuedLoads_` folds into the same check.
- **Trace lines:** `kernel: loop at line N compiled under threads (2 private variables)`
  and `declined under threads: $M is shared (mentioned by sub work)`.
- **Reentrancy audit** of the loop-kernel run path. Confirm that `fr[]`,
  `strs[]` and the undo log are per-activation, with no static scratch. Then
  run a TSan pass with N workers in one compiled loop.
- **The first-call race:** a caller that sees state 2 waits on `g_compileMu`
  and re-reads the state, instead of running the call interpreted. The same
  applies to the borrowed path (IntKernel.cpp:2648, 2658–2671).
- **`--cnp`:** the same per-variable check in `runIfReady`. `rk_cnp_call` also
  re-checks after each call: if the callee spawned a worker and any held
  variable is not private, write back and leave the kernel. This closes
  hazard 6.
- **Polling tests,** which must terminate in every mode (default, `--cnp`,
  `RAKUPP_NO_KERNELS=1`):
  - `t/cnp/cases/shared-flag.raku` (exists);
  - a new Int-flag variant for loop kernels. Today's flag is a Bool, which the
    kernel declines anyway (IntKernel.cpp:613), so an Int is the shape that
    actually exercises the kernel.

Exit: example 3's first shape compiles under threads. Its only outer
variables are `$x` and `$s`, because `$M` reaches the kernel as the range
bound. idle-worker's loop runs as a kernel beside the idle worker, and the
first-call race is gone in 50 runs of 50.

### P6 — The stripe pool, for what stays shared

**Done** 2026-10-08: 256 stripes, `alignas(128)`,
Fibonacci hashing of the address, still recursive mutexes, and
`RAKUPP_STRIPE_STATS=1`.

- **The stripes:** a multiplicative hash of the address, `alignas(128)` per
  stripe (the Apple cache line), 256 stripes. Keep `recursive_mutex`, because
  `cas` re-enters its own stripe.
- **Measure** with `RAKUPP_STRIPE_STATS`, on atomic-counter `counters` and
  `ub-env-sharing`.

This phase is independent of the others and can go first if a cheap win is
wanted.

### P7 — `--exe` and the C++ `--jit` (probe first)

Open.

- **Two shapes to probe:**
  - a top-level Int flag that a `start` loop polls, under `--exe` and
    `--exe -O`;
  - the C++ `--jit` `tryUnboxedLoop` (Codegen.cpp:5612) with a live worker.
- **If either hangs or loses writes** (hazard 7), Codegen's `analyzeCells`
  takes P1's mask, and `topVars_` captured by deferred bodies count as cells.
- **Either way**, use the mask to keep private locals in C++ locals under
  threads, which `--exe` already does.

### P8 — Docs

Open, apart from this plan.

- **PARALLEL-SPEEDUP.md:** every table re-measured. Rewrite example 3 and the
  "N=1 is 0.85×" bullet. Remove the checklist items about kernels and the
  warm-up call wherever the engine no longer needs them.
- **faq/threads.md:** the "Why did `start` make my program slower?" list.
- **Elsewhere:** the fan-out numbers in ASYNC.md, the parallel row in
  BENCHMARKS.md, and the kernel-refusal prose in JIT.md (320–336),
  internals/CNP.md (551–576) and the book's chapter 43.
- **The memory-model section of ASYNC.md:** the guarantees are unchanged.

---

## Pre-existing hazards found while planning

All were read in the code and none has been reproduced yet. Each needs a probe
before it is called a bug. Hazard 1 is fixed in P2 and hazard 6 in P5; the
rest go to TODO.md.

1. ~~**Unguarded promotion of a shared slot.** `bindArgCell`
   (InterpreterBinding.cpp:6314, 6330), `:=` (InterpreterCore.cpp:15124) and
   for-rw (3201–3217) call `promoteToCell` with workers live. `varCell` refuses
   in exactly that case, because promotion rewrites a multi-word Value in
   place (InterpreterCalls.cpp:1465–1472).~~ **Done** 2026-10-08:
   `promotionSafe` (P2).
2. ~~**rw-link write-through with no stripe.** A worker's write through an rw
   parameter stores into the caller's slot with no stripe
   (InterpreterCalls.cpp:341–358).~~ **Done** 2026-10-08.
   Reproduced first: `t/stress/private-rw-link.raku` crashed the baseline
   binary in every run (P2).
3. **Element and structural stripes don't exclude each other.** An element
   read keyed on the element (InterpreterCore.cpp:26094, 27708) does not
   exclude a reallocating push keyed on the container
   (MethodCallTail.cpp:4755). The comment at InterpreterCore.cpp:2950 records
   a crash of this shape.
4. **In-place operand reads of shared slots with no stripe.** `tryCondBool`
   reads a Str body (InterpreterCore.cpp:20771); see also `binaryFastHandler`
   (21022) and `typedOperand` (21156). The compound op= tail of
   `evalAssignInner` (16714–16855) stores unstriped.
5. **A stale comment.** `cuedLoads_ > 0` implies `liveWorkers_ > 0`, so the
   comment at Jit.cpp:1494–1496 is wrong.
6. ~~**A `--cnp` loop can spawn after its gate.** `--cnp` admits calls (Jit.cpp
   440–478), so a callee can spawn a worker after the entry check has passed.~~
   **Done** 2026-10-08: a kernel that calls out holds only
   private variables (P5).
7. **`--exe` lanes ignore top-level `my`s that `start` captures.**
   `analyzeCells` excludes them (Codegen.cpp:1173, 1185), and the C++ `--jit`
   `tryUnboxedLoop` has no worker gate (Codegen.cpp:5612–5663).

## Gates

Every phase must pass all of these. Run them alone, never beside another
session's gate.

- **Roast:** the full sweep after every change under `src/`, plus parallel
  Roast parity.
- **Stress:** `t/stress/run.raku` in both modes, the TSan job with the new
  contract programs off the skip list, and `t/race/*`.
- **Module suites:** the battery and the adopters gate, at P3 and P5.
- **`perf-guard --check`:** single-threaded numbers unchanged. Every new check
  sits inside the branch that already requires live workers. The extra
  `resolvePads` walk is once per body, so also time Roast as a whole for
  compile cost.
- **The P0 table,** re-measured after P3, P4, P5 and P6, interleaved against
  `RAKUPP_PRIVATE_SLOTS=0` on the same binary.

## Non-goals

- Object-level escape analysis. Containers keep their stripes, and the packed
  store and push lanes stay off while workers are live
  (InterpreterCore.cpp:13157, Builtins.cpp:6014).
- Fanning out the `.hyper`/`.race` methods.
- A thread pool (PARALLEL-PLAN P4).
- Any change to the memory model, or making racy programs deterministic.
- The `tctx_` thread-local cost (its own TODO.md item).

## Risks

- **Soundness.** A route missed by both layers means a torn copy and a crash.
  Mitigations:
  - two independent layers, plus the `isCell` backstop;
  - one contract test per route, each proven able to fail with `=all`;
  - TSan, which reports an unlocked copy racing a store;
  - the program-wide switch for the dynamic-scope pseudo-packages;
  - `RAKUPP_PRIVATE_SLOTS=0` in the field.
- **Polling loops.** A kernel that holds a shared flag spins forever. Only
  private variables enter a kernel under threads. A variable that another
  thread can set is by definition not private.
- **Single-threaded speed.** Nothing new runs unless workers are live.
  perf-guard is the gate.
- **The 64-slot limit.** Owners with more than 64 candidates get no slots and
  keep the stripe everywhere. That is acceptable for now.
