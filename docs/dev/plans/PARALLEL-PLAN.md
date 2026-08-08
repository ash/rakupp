# Plan: real multicore parallelism, on by default

**Status: in progress — P0 and P1 landed 2026-08-07.** The memory model is
written ([guide/ASYNC.md](../../guide/ASYNC.md), "The memory model");
`t/stress/` runs a 9-program × 2-mode matrix with a known-bad ratchet (a
new failure fails the suite; a known-bad that starts passing fails it too,
so the list only shrinks); the `linux-tsan` CI job arms it with
ThreadSanitizer. The suite's FIRST RUN found three P4 primitive gaps the
plan had predicted: `atomicint` loses updates, `Channel` with N producers
hangs, and `Supplier.emit` drops cross-thread emissions — all in parallel
mode, all on the known-bad list. Bonus datum: Rakudo itself dies on the
unguarded-hash race (`t/stress/ub-hash-write.raku` under real `raku`
produced no output), so the no-crash contract aims higher than the
reference's observed behavior. One of the three **v3.0.0** pillars
([VERSIONS.md](VERSIONS.md); the others are [CLI-PLAN.md](CLI-PLAN.md) and
[LTM-PLAN.md](LTM-PLAN.md)). The *design*
decision was already made in [PLAN-gil-removal.md](PLAN-gil-removal.md) —
Option 2, "ownership discipline: harden the runtime, not every user
structure" — and this document is the campaign plan that executes it: the
phases, the measurements we start from, and the gates that decide when the
default flips.

Measurements: 2026-08-07, `build-arm64/rakupp` at v2.0.0, macOS arm64,
8 cores.

## The concepts, briefly

**The GIL.** Raku++ workers are real OS threads, but by default a single
mutex — the *global interpreter lock*, CPython-style — is held while any
thread runs interpreter code, so only one runs at a time. Blocking
operations (I/O, `sleep`, `await`, subprocess waits) release it, which is
why I/O-bound concurrency already overlaps well. What the GIL buys is
simplicity: no interpreter data structure ever sees two writers. What it
costs is the obvious thing: CPU-bound `start` blocks cannot use a second
core.

**The memory model.** A language's memory model is the contract about what
concurrent programs mean: which outcomes a program that shares data across
threads may observe, and what is undefined. Raku's own position (and
Rakudo's) is that *unsynchronized* mutation of shared plain data is the
user's bug — the language gives you `Lock`, `Channel`, `Supply`, `atomicint`
to do it right. Our bar, set in the design doc, is therefore **not** "make
racy user programs correct" — it is: a race in *user* data must never
corrupt the *interpreter*. Today it does (see below), and that is the core
of this campaign.

**ThreadSanitizer (TSan).** A compiler mode (`-fsanitize=thread`) that
instruments every memory access and reports real data races at runtime.
Races are otherwise invisible — a racy build can pass every test for months.
The parallel configuration is only trustworthy with a TSan job in CI.

## Where we are — measured

The opt-in mode (`RAKUPP_PARALLEL=1`) already works for well-behaved
programs, and its wins and limits are both visible in five minutes of
measurement:

- **It parallelizes.** Four CPU-bound `start` workers: 0.414 s wall under
  the GIL → 0.221 s wall parallel, 374% CPU. Correct sum both ways.
- **The primitives hold.** `Lock.protect` around a shared `@out.push` from
  4 threads × 2000 iterations: 8000 elements, three runs out of three.
  `atomicint`/`⚛` operations exist and work.
- **Contention is real.** Same worker body, scaling the worker count:

  | workers | wall | user CPU | user CPU per worker |
  |---:|---:|---:|---:|
  | 1 | 0.10 s | 0.09 s | 0.09 s |
  | 2 | 0.11 s | 0.21 s | 0.11 s |
  | 4 | 0.21 s | 0.79 s | 0.20 s |
  | 8 | 0.46 s | 2.71 s | 0.34 s |

  Identical per-worker work costs 0.09 s alone and 0.34 s of CPU when eight
  run together — a ~4× inflation. That is cache-line/lock ping-pong
  (`shared_ptr` refcounts, the allocator, shared side tables), and it caps
  the useful speedup today. Finding and killing the top contenders is a
  phase of this plan, not a footnote.
- **The bar is currently failed.** The same push loop *without* the Lock —
  a user bug, UB by the memory model — dies with SIGABRT (exit 134): a
  `std::vector` raced into corruption inside the runtime. Under the
  contract, that program's *output* may be garbage but the runtime must not
  crash. This is the hardest and most important line item.

What already exists from earlier work (see the design doc's "Related code"):
GIL parking at blocking points, interpreter safe points, thread-local
execution registers (`ExecContext`), symbol-table freezing
(`symbolsFrozen_`), worker threads with big stacks.

## The phases

### P0 — write the memory model down (user-facing)

A short section in [guide/ASYNC.md](../../guide/ASYNC.md) (or a new
`guide/CONCURRENCY-MODEL.md` if it outgrows a section): what is guaranteed
(primitives synchronize; independent data parallelizes; the runtime never
crashes on your race), what is UB (unsynchronized shared mutation — your
values, not your process), and how that compares to Rakudo. This is written
*first* because every later decision cites it.

### P1 — stress suite + TSan CI

- `t/stress/` — producer/consumer over `Channel`, contended `Lock`,
  parallel `map`, shared-hash hammering, `Supply` fan-in/fan-out, promise
  chains, each parameterized by thread count and run in both modes.
- A CI job building with `-fsanitize=thread` and running the stress suite
  in parallel mode. It will be red at first; the point is to make the
  backlog visible and monotonically shrinkable.

### P2 — runtime hardening (the "never corrupt the interpreter" work)

**Progress (2026-08-07):** batch 1 thread-localed the per-thread execution
state TSan ranked highest — the five one-shot call registers
(`topicWriteback_`, `builtinTopicWB_`, `noAutothread_`, `loopPhaserCtl_`,
`pendingRwSlots_`), plus `hoistingSubs_` and `curLine_`. parallel-map's
TSan report count: **35 → 6**. Batch 2 (same day) took it to **ZERO**: the
survivors were decided-once caches on SHARED AST nodes (`hoistNeed`, the
four `hasStateCache` loop flags, the node-specialization `simpleOp`/
`fastShape`/`litVal`/`litIdx`), now relaxed atomics via a `DecidedOnce<T>`
wrapper in Ast.h; the regex byteset flag became a real acquire/release
pair (it gates a 32-byte table); `suppressLoopFirst_` joined the
thread_local family. perf-guard passed with three new best-evers (fib
−8.2%, asg −10.6%, hash −9.0%) — relaxed atomics are plain loads/stores
here. Known remaining in this class, deliberately deferred: the Rat
literal's `cacheN`/`cacheD` (`mutable shared_ptr` — needs atomic
shared_ptr ops, a different mechanism), and whatever TSan finds when the
OTHER stress programs run under it (parallel is currently skipped under
TSan; un-skipping program by program as they come clean is the next
ratchet turn).

Audit and fix, in order of blast radius, everything the TSan job and the
design doc's list point at:

- global side tables appended without locks (`keptPrograms_`, interners,
  regex memo tables if any escape per-match state);
- post-freeze symbol-table mutation (runtime `EVAL`, dynamic class/regex
  creation): route through a lock + re-freeze, or a clean "not in parallel
  regions" error — decided case by case, recorded in the doc;
- confirm every remaining execution register / capture register is
  thread-local (most moved in the earlier steps).

**P3 first delivery (2026-08-08): the no-crash contract holds for the
pinned shapes.** TSan enumerated the two contract programs' entire race
surface down to two choke points — the array-mutator block (vector growth)
and the hash find-or-insert in `lvalue()` (tree balance) — now striped in
parallel mode via `ParStripe` (constructs to nothing under the GIL;
perf-guard unchanged). 40/40 hammer runs where SIGABRT was the norm; the
stress matrix reads 18/18 with both ratchet lists EMPTY. Documented
residuals for the next stress programs: iteration during mutation,
object-attribute races, same-slot torn copies of pointer-carrying values —
each wants its own contract program before its fix.

### P3 — container strategy (the crash-elimination decision)

The design choice this plan must make concrete: how shared `Array`/`Hash`/
object-attribute mutation stops being able to corrupt the runtime.
Candidates, to be prototyped and *measured* before committing:

1. **Striped locks, parallel mode only** — a fixed pool of N mutexes hashed
   by container address, taken around structural mutation and iteration.
   Zero per-Value memory cost, zero single-thread cost (guarded by
   `parallelMode_`), bounded contention. The current favorite.
2. Per-container mutex — cleaner isolation, but fattens every container for
   a rare case.
3. Documented-UB-but-no-crash via defensive copies on iteration — cheapest,
   but it does not actually close the `push`/`push` structural race.

Whichever wins must leave the single-threaded hot path untouched —
`perf-guard --check` is a hard gate here, per the standing release rule.

**P4 primitives: DONE 2026-08-07/08, same days they were found.** All three
first-run entries fixed and forced off the ratchet by their own passing
runs: `atomicint` (the lexer DROPPED ⚛ — real operators now, lowered to
atomic-* calls, RMW under a striped recursive-lock pool shared with `cas`;
plus the typed-assign contract and `full-barrier`), `Channel` (every op
under the channel's stripe; blocking `.receive` waits outside it and now
also waits in parallel mode), and `Supplier` (emit/done/quit and tap
registration under the supplier's stripe — the serialized-emissions
contract). The deterministic known-bad list stands EMPTY; only the two
flaky P3 contract cases remain, waiting on the container strategy.

### P4 — scheduler and contention

- Replace unbounded thread-per-`start` (`workers_.push_back(BigStackThread(…))`
  today) with a real pool: cap ≈ core count, reuse threads, keep the
  big-stack property. This is also what fixes the 8-worker inflation being
  *worse* than 4× — today 8 CPU-bound workers oversubscribe and ping-pong.
- Profile and fix the top contention sources found in P1's stress runs
  (likely: allocator pressure and `shared_ptr` refcount traffic on hot
  shared values; possible responses include interning more aggressively and
  copy-on-spawn for values captured by `start` blocks).
- Verify `Channel`/`Supply`/`Promise` internals under parallel mode are
  lock-correct rather than GIL-reliant (some serialize via the GIL today).

**Pillar status after day 2 (2026-08-08)** — everything short of the flip:

- **Contract programs**: four more staged (`ub-iterate-during-push`,
  `ub-object-attrs`, `ub-torn-values`, `ub-env-sharing` — the last covers
  this plan's named Env risk). Torn-values crashed 5/5 → fixed by striping
  the scalar copy WINDOW (VarExpr fast-path copy-out + evalAssignInner
  stores, keyed on the slot); 20/20 after. The native matrix reads
  **26/26, all strict, no tolerances**.
- **Free-when-alone**: `ParStripe` engages only while `liveWorkers_ > 0` —
  the first cut taxed single-threaded programs under `RAKUPP_PARALLEL=1`
  and pushed big compute files (set_addition, relational) past the roast
  timeout with zero threads in them; both back at exact GIL scores now.
- **Scaling gate: met and published** — `tools/bench/parmap.raku`, table
  in [BENCHMARKS.md](../../status/BENCHMARKS.md): 2.96× at 4 workers,
  3.62× at 8, CPU inflation 1.41×, single-thread parity with GIL
  (987 vs 996 ms). **The P4 thread pool is deferred on these numbers** —
  at 1.4× inflation it is an optimization, not a gate.
- **TSan ratchet**: blanket parallel-skip replaced by a per-program list —
  six correctness programs run STRICT under TSan in parallel mode at zero
  reports; the list holds `promise-chain` (3 reports in eval, the last of
  the P2 class) and the deliberate-race `ub-*` family.
- **Parallel-mode Roast distance**: run 1 585 full/37 timeouts; run 2
  (after free-when-alone) **590 full / 33 timeouts** vs GIL's 593/19.
  Real count-drops: only the documented flappers.

**THE FLIP BLOCKER, named and reproducible**: ~14 thread-spawning files
(S17 thread/procasync/promise/supply, S16-io/watch) time out under
parallel mode *in isolation* — and `S17-lowlevel/thread.t` does not finish
in FIVE MINUTES: a livelock, not a tax. Minimal `Thread.start`+`join`
works, so a specific construct inside those files livelocks under real
parallelism (suspects: cooperative-GIL assumptions in waits — code shaped
like "loop until the worker ran" that yields under the GIL but spins in
parallel mode). Bisecting thread.t is the next session's first move; the
default does NOT flip until this class is gone and three consecutive
parallel Roast runs hold parity.

### P5 — flip the default

`RAKUPP_PARALLEL` behavior becomes the default; `RAKUPP_GIL=1` remains as
the escape hatch (and the GIL build keeps running in CI — it is also our
bisection tool). Only after every gate below is green.

## Gates (all must hold, both modes)

- **Full Roast**: parity with the GIL baseline, three consecutive runs
  (the known flap band applies; no *new* down-movers).
- **Battery**: 50/59 distributions passing their own suites, unchanged.
- **Spec site**: 952 byte-identical examples, unchanged.
- **perf-guard --check**: single-threaded numbers at baseline — the
  parallel machinery must be free when one thread runs.
- **Scaling bench**: a new `tools/bench/parmap.raku` (embarrassingly
  parallel map) added to the bench suite; target ≥3× wall-clock on 8 cores
  vs the same binary's single-thread run, and the per-worker CPU inflation
  table above published in BENCHMARKS.md, honestly.
- **TSan**: stress suite clean in the parallel configuration.
- **The abort test**: the unguarded shared-push program (and a family of
  nastier variants: hash rehash races, iteration-during-mutation) runs to
  completion or reports a clean Raku-level error — never a native crash.

## Risks, named

- **Env sharing for closures** is the semantic deep end: two `start` blocks
  closing over one outer `my` share an `Env`. The memory model calls
  unsynchronized use UB, but the *runtime* still must survive concurrent
  map access — P3's strategy has to cover `Env`'s var map, not just user
  containers.
- **Allocator contention** may dominate after the easy locks are fixed;
  if so, the answer is an arena/thread-cache story, which is real work —
  budget for it rather than discovering it at the end.
- **`--exe` parity**: the transpiled path shares the runtime library; every
  P2/P3 change must be built and gated there too (`run-optbench` 4-way
  agreement already exists as the harness).
- **Scope creep toward "make races correct"**: the memory model doc is the
  fence. Anything beyond "no crash, primitives correct" is out.

## Non-goals

- Making unsynchronized shared-mutable programs deterministic (UB stays UB;
  it just stops being *our* crash).
- Removing the GIL code path entirely — it stays as `RAKUPP_GIL=1`.
- Software transactional memory, actor frameworks, or any new user-facing
  concurrency API — Raku already defines the API; we implement it.
