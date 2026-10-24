# Plan: `--race` — a happens-before race detector for Raku programs

*Written 2026-10-24, before any code. The idea has had an address since
2026-09-06: [CLI-BORROW-PLAN.md](CLI-BORROW-PLAN.md) parks "`--race`-style
diagnostics (Go `-race`)" with the note that "a real happens-before checker is
a project, not a task". This is that project's plan. Siblings:
[PARALLEL-PLAN.md](PARALLEL-PLAN.md), which made a race in user data unable to
crash the runtime, and [PARALLEL-SCALING-PLAN.md](PARALLEL-SCALING-PLAN.md),
whose private-slot proof this reuses. The memory model it enforces is the one
already written down in [guide/ASYNC.md](../../guide/ASYNC.md), "The memory
model". `--race` is the working name; see the last open question.*

## The measurement that frames it

Four small programs, each with or without a data race, under
`build/rakupp` 5.5.0-21-g9e0bdffd and Rakudo 2026.09, macOS arm64, 2026-10-24,
nice'd. Each worker count is 4.

| program | rakupp (default) | rakupp `RAKUPP_GIL=1` | rakudo |
|---|---|---|---|
| counter: `$n++` from 4 workers, 100k each | 164349, 163194, 164331 | 400000 ×3 | 218144–291339 in six runs; a seventh printed nothing |
| distinct keys: `%seen{"$w-$_"} = True`, 10k each | **40000 ×5** | 40000 ×5 | **dies 5/5**: `MoarVM oops: MVM_str_hash_fetch_nocheck called with a stale hashtable pointer` (once: `…called concurrently on the same hash`) |
| indexed, growing: `@r[$w * 10_000 + $_] = $_` into an empty `@r` | **crashes 8/8** (exit 133, 134, 138, 139) | 40000 ×5 | 39750, 39756, 39667 (writes lost, no crash) |
| indexed, pre-sized: the same into `my @r = Nil xx 40_000` | 40000 ×3 | — | 40000 ×3 |

The first three are races under the memory model: unsynchronized mutation of
shared plain data. The fourth is not: the workers write distinct elements of
an array whose shape nobody changes. Three things follow.

**No engine reports any of them, and the outcome depends on the engine.** The
same program is right on one engine and dies on the other, in both
directions. A user who sees 40000 has learned nothing about whether their
program is correct.

**Our own hardening hides races.** The distinct-keys program is correct five
runs out of five under rakupp because PARALLEL-PLAN P3 put hash
find-or-insert under a stripe. That contract is right, and it is also why a
program developed on rakupp can ship and then die on Rakudo. The detector is
the other half of the no-crash contract: the runtime survives your race, and
`--race` tells you it was there.

**The current diagnosis only finds races that collide.**
[faq/threads.md](../../guide/faq/threads.md), "When is `RAKUPP_GIL=1` worth
setting?", says: wrong by default and right under the lock means a race. The
distinct-keys program is right in both modes under rakupp. That diagnosis
cannot see it. A happens-before detector reports it on every run, because it
asks whether anything *ordered* the two accesses, not whether they happened
to collide.

**Side finding, not this plan's work:** the growing indexed write crashed
rakupp 8/8 in parallel mode and never under the GIL. That breaks the P3
no-crash contract. Half fixed the same day (90bc6e23):
`t/stress/ub-indexed-grow.raku` now stages it, and the auto-extend in
`lvalue()` takes push's stripe, so two threads no longer realloc one buffer
at once (the reproducer: 8/8 crashed before, 2/40 after). What is left is the
element pointer `lvalue()` hands out: it outlives the stripe, a sibling's
growth moves the buffer under it, and the store writes freed memory.
Reserving ahead so that growth never moves the buffer took the
block-per-worker phase from 2/30 to 0/60, which is the proof. The parallel
leg sits on the harness's `%known-flaky` list until the container strategy
covers element pointers. Two fixes for it were prototyped and measured:
[STALE-ELEMENT-POINTER.md](../experiments/STALE-ELEMENT-POINTER.md). The
detector would report every one of these programs whichever way the engine
survives them.

## What it reports

A **data race** is two accesses to the same location, from different threads,
at least one a write, with no happens-before between them. Happens-before is
built **only** from the primitives the memory model guarantees (ASYNC.md:
`Lock`, `Lock::Async`, `Semaphore`, `Channel`, `Supply`, `Promise`,
`atomicint` with `⚛`) plus thread start and join. That makes the reported set
exactly the guide's "Undefined — for your data" set. If a report surprises
someone, the memory model is what gets argued, not the detector.

Engine-internal synchronization never counts: the GIL, `ParStripe` and
`SlotStripe`, `sharedMut_`, `g_compileMu`, refcount atomics, `padLive`,
`liveWorkers_` reaching 0. Nor does `sleep`. A program that is correct only
because a worker "has surely finished by now" gets a report.

What a report looks like (the shape; wording settles in P3):

```
rakupp: data race on $n (declared counter.raku:1)
  write at counter.raku:2 ($n++), in the start block at counter.raku:2 (thread 3)
    in block <unit> at counter.raku:2
  previous write at counter.raku:2, in the start block at counter.raku:2 (thread 2)
  nothing orders them: no Lock, await, Channel, Supply or ⚛ between the two threads
rakupp: 1 data race (1 site pair, 199,998 occurrences)
```

Each site pair is reported once, with a count at exit. A program that ran to
completion with a race exits **66**, Go's `-race` code and the one
`TSAN_OPTIONS=exitcode=66` already gives our stress harness. Output goes to
stderr and never changes stdout, so a program's own goldens still compare.

## Design

### The algorithm: FastTrack

FastTrack (Flanagan & Freund, PLDI 2009) is a vector-clock detector with an
O(1) fast path. Each thread has a vector clock. Each location keeps the epoch
(`thread@clock`) of its last write, plus a read epoch that widens to a read
vector clock only when reads from several threads are concurrent. Most
accesses compare one epoch and update one word.

A lockset detector (Eraser) was ruled out. It flags every value handed from
one thread to another without a lock, and the Raku idiom is exactly that: a
`start` block closes over a value, then `await` hands the result back. It
would report every program that uses `start` correctly.

### Locations: what "the same location" means

The granularity is set by the fourth row of the table: a pre-sized array
written at distinct indices is correct on both engines, and it must stay
silent. So an array is two kinds of location, its elements and its shape:

| location | key | write | read |
|---|---|---|---|
| lexical scalar | (frame, slot) | assignment, `++`, `⚛` (atomic, see edges) | any read of the value |
| array element | (array, index) | store inside the current bounds | element read |
| array shape | (array) | `push`/`pop`/`shift`/`unshift`/`splice`, a store **past the end**, list assignment | every element access, iteration, `.elems` |
| hash entry | (hash, key) | store to a key that exists | lookup |
| hash shape | (hash) | insert of a new key, `:delete`, list assignment | every lookup, iteration, `.elems`, `.keys` |
| attribute | (object, attribute) | `$!x =`, an `is rw` accessor store | read |

An attribute that holds an `@` or `%` follows the array and hash rows for the
container inside it. Under this table the four programs come out right. The
counter races on a scalar. Distinct keys race on the hash shape, since every
insert of a new key writes it. Growing indexed writes race on the array
shape. Pre-sized writes take only shape reads and distinct element writes,
so they stay silent.

**Keys are identities, not addresses.** Array growth moves element storage, so
a key built from an address would follow the wrong cell. A freed container's
address is reused, so shadow entries die with their container: a destructor
hook, live only under `--race`, behind a global `const bool` (the
`g_noKernels` pattern in `IntKernel.cpp`).

### Where the hooks go

The access paths have already been enumerated once. The P3 torn-copy contract
put `ParStripe`/`SlotStripe` on every shared read and store of the four
container kinds: lexical, array element, hash element, attribute
(PARALLEL-PLAN, "second delivery"). The structural operations (array growth,
hash find-or-insert) took one too. At 9e0bdffd that is 89 `ParStripe`
mentions across 14 files, the most in `InterpreterCore.cpp` (30),
`InterpreterCalls.cpp` (17) and `InterpreterBinding.cpp` (13).

Each hook is one out-of-line call beside the stripe:
`if (g_race) raceAccess(kind, key, isWrite, site)`. On a striped path the
shadow update for a location runs under the stripe the access already holds.
The shadow table is sharded by stripe index, so the striped path needs no
lock of its own.

The hook sites will not line up with the stripes everywhere: there are fast
paths (direct `padSlot` reads, the `simpleOp`/`fastShape` specializations)
and iteration. P2 writes the list of container access paths that carry no
hook into this file. A missing hook is a **false negative**, the safe
direction to fail in, and the must-report corpus is how one is found.

### What it can skip for free

- **Private slots.** `slotPrivate` ([Interpreter.h](../../../src/Interpreter.h),
  PARALLEL-SCALING-PLAN P3) proves a slot unreachable from another thread.
  No shadow is kept for it, and a scalar-heavy worker loop pays nothing for
  its own locals. A debug switch, `RAKUPP_RACE_CHECK_PRIVATE=1`, tracks
  private slots anyway and reports any second-thread access to one as an
  **engine** bug. That makes the detector an oracle for the private-slot
  proof, which today is tested only by contract programs that crash under
  `RAKUPP_PRIVATE_SLOTS=all`.
- **Everything before the first spawn.** Until `everSpawned_` is set, one
  thread cannot race itself, and anything it did happens before every child
  (the fork edge).
- **Not everything after the last worker exits.** `ParStripe` turns off when
  `liveWorkers_` reaches 0. The race path must not: a worker that finished
  without being awaited has no happens-before edge to what main does next.
  `start { $x = 1 }; sleep 1; say $x` is a race, and it must be reported. So
  the race path keys off `everSpawned_`, not `liveWorkers_`.

### Threads

Thread ids are dense, in spawn order. A thread record holds its vector clock
and its spawn site (the file and line of the `start`, `Thread.start` or
supply ticker that made it), which is what the report's "in the start block
at …" is built from. The race state hangs off that record, not off a new
`thread_local`. PARALLEL-PLAN's `supplyCloseStack_` episode measured 6-7% on
loopsum from adding one non-trivially-destructed `thread_local`, and on Darwin
each `thread_local` mention can cost a `_tlv_get_addr` call.

### The report

The current access gets a full backtrace (the `BtRecord` machinery from
[BACKTRACE-PLAN.md](BACKTRACE-PLAN.md)). The earlier access has only what its
shadow stored: file, line and thread id, which is three integers, not a
stack. Go recovers the earlier stack from a per-thread history ring buffer;
that is P5. Names come from what the engine already has:

- a pad slot: the layout's `byName`, reversed;
- an element: `@r[17]`;
- a hash entry: `%seen<2-41>`;
- an attribute: `$!count` of `Shared`.

## Sync edges

This table is the design. Every row is release on one side and acquire on the
other, and every row gets its own must-stay-silent program in P0. Locations
are at 9e0bdffd; they drift, so the function names matter more than the line
numbers.

| primitive | edge | where |
|---|---|---|
| `start`, `Promise.start`, `Thread.start`, supply tickers and timers | **fork**: the child starts with the parent's clock; the parent ticks | `BigStackThread::run_` ([Interpreter.h:3957](../../../src/Interpreter.h)) is every worker's entry. Its `Fn` captures the parent's clock at construction, so one choke point covers `spawnPromise` ([Interpreter.cpp:6647](../../../src/Interpreter.cpp)) and the ~12 ticker and timer sites in `BuiltinsSupply.cpp` |
| keep/break → `await`, `.result` | settle releases into the promise's clock; `await` and `.result` acquire it | `PromiseState` ([Interpreter.h:1950](../../../src/Interpreter.h)) gets a clock, written under `ps->m`; `awaitPromise` ([Interpreter.cpp:6832](../../../src/Interpreter.cpp)) |
| `.status` that answers `Kept`/`Broken` | acquire, provisionally (open question 1) | the status read |
| `.then` | the continuation starts with the settler's clock | the `ps->thens` push sites (`BuiltinsRegister2.cpp`, `BuiltinsSupply.cpp`). Continuations run concurrently and unserialized (no GIL), so two `.then` blocks on one promise are **not** ordered with each other, and a shared write between them is reported |
| `Promise.allof`/`anyof`, `Promise.in`/`at` | allof joins every member's clock; anyof joins the winner's; timers keep from their own thread | the Promise rule |
| `Thread.finish`/`.join` | **join**: the joiner acquires the thread's final clock | [MethodCallPart1c.cpp:779](../../../src/MethodCallPart1c.cpp) |
| `Lock` `.lock`/`.unlock`/`.protect` | unlock releases into the lock's clock; lock acquires it | the `Lock` arm in [MethodCallPart2.cpp](../../../src/MethodCallPart2.cpp) (~4684–4710) |
| `LockCondition` `.wait` | release, then acquire on wake (the lock carries the edge; `.signal` is not one on its own) | same arm, `condition`/`wait`/`signal` |
| `Lock::Async` | rides Promises (`.lock` answers a Promise kept once the lock is the caller's), plus the lock's own clock for the uncontended path | `MethodCallPart2.cpp` (~4625) |
| `Semaphore` | `.release` releases into the semaphore's clock; `.acquire`/`.try_acquire` acquire it | `MethodCallPart2.cpp` (~4728) |
| `Channel` | each queued item carries the sender's clock; the receive that takes it acquires it; `.close` → the receive that sees the close; `.list`, `.poll` likewise | `send`/`receive` in [MethodCallPart1c.cpp:563](../../../src/MethodCallPart1c.cpp) |
| `Supplier.emit` → tap | the emit releases; each tap's handler run acquires. Runs of one tap are serialized (the per-supplier mutex), so they are ordered with each other | `supplierMutex` ([BuiltinsShared.h:275](../../../src/BuiltinsShared.h)) |
| `react`/`whenever` | enqueue releases; the react loop's dequeue acquires | `ReactCtx` queue (Interpreter.h, after `PromiseState`) |
| `Proc::Async` output | emits from reader threads, so the Supplier rule applies | — |
| `⚛` ops, `cas`, `atomic-*` | the location is **atomic**: two atomic accesses never race. Each atomic op is release+acquire on that location. A plain access mixed with an atomic one is a race (`$n⚛++` in one thread, `$n++` in another) | `atomicOn` ([InterpreterCalls.cpp:1343](../../../src/InterpreterCalls.cpp)) |
| `full-barrier` | no edge on its own: a fence orders nothing without a read that sees another thread's write; the atomic rows carry that | — |
| `hyper`/`race` `.map`, `.grep`, `for` | fork at batch dispatch, join at collection | `hyperSeqCall` ([MethodCallTail.cpp:448](../../../src/MethodCallTail.cpp)) |

**Not edges:** the GIL; `ParStripe`/`SlotStripe`; `sharedMut_`; `g_compileMu`;
refcount atomics; `padLive`; `liveWorkers_`; `sleep`; I/O; `$*THREAD.id`. The
embedding API's worker pool (`EmbedApi.cpp`) runs host-driven calls and is
out of scope until someone embeds a threaded program and asks.

## Phases

### P0 — the corpus, before any detector code

`t/race-detect/`, two halves, each program labeled by reading it against
ASYNC.md's model. (`t/race/` is taken: it holds run-until-it-shows
reproducers of the engine's own races, a different subject.) Rakudo cannot be the oracle here: it has no detector. The nearest it
comes is MoarVM's crash-time check on concurrent hash access, which is one
structure, and a crash.

- **must-report:** the counter, distinct keys, growing indexed writes; every
  `t/stress/ub-*` program, by reference; one program per location kind; one
  per missing edge:
  - an un-awaited `start` followed by `sleep`;
  - two `.then` continuations on one promise;
  - a plain access mixed with `⚛`;
  - a write beside a `Channel` send, outside the item.

  Some `t/stress/private-*` contract programs race by design.
  `private-start-capture.raku` stores into `$v` from owner and worker with no
  lock, so those go here too.
- **must-stay-silent:**
  - one program per row of the edge table;
  - the `t/stress` correctness programs (`lock-counter`, `hash-guarded`,
    `atomic-counter`, `channel-pipeline`, `supply-fanin`, `promise-chain`,
    `parallel-map`, each confirmed by reading);
  - the FAQ's `Lock` and `atomicint` counters;
  - the pre-sized indexed write.

### P1 — clocks and edges

Thread records, vector clocks, and every edge in the table, but no location
tracking yet. `RAKUPP_RACE_TRACE=edges` dumps each edge as it fires. A
must-stay-silent program has to show the edge it exists to test.

### P2 — locations

Scalar slots and attributes first: the counter and `ub-object-attrs`. Then
array and hash elements and shapes. Hooks go beside the `ParStripe`/`SlotStripe`
sites, and the private-slot skip comes in at the same time. Then the audit:
the list of unhooked container access paths, written into this file and
burned down against the must-report corpus.

### P3 — reports and the flag

Names, both sites, the backtrace of the current access, dedup, counts, exit
66. Then the flag, by the CLI rules ([CLI-PLAN.md](CLI-PLAN.md); one option
legal in every mode that runs a program):

- a `kFlagDocs` entry (help and completions come from it);
- `docs/guide/CLI.md` and the README options table;
- goldens in `t/run.raku`'s CLI section.

`--race` with `--exe`, `--jit` or `--target=js` is refused by name: native
code has no hooks. `--race` implies `RAKUPP_NO_KERNELS`. The CLI-BORROW-PLAN
entry gets struck through and points here.

### P4 — Roast S17 and the ecosystem

See "The payoff" below.

### P5 — later

- History ring buffers, so the earlier access gets a stack.
- Thread-id reuse (see risks).
- `--exe` support: codegen emits the hooks under a build flag.

## Gates

- **must-report:** every program reports, **20/20 runs**. For these programs
  no schedule orders the two accesses, so "usually" is a failure.
- **must-stay-silent:** 20/20 silent.
- **Mode-independent verdicts:** every corpus program gets the same verdict
  under `RAKUPP_GIL=1` as by default. The detector reasons about
  happens-before, not about which thread held the GIL.
- **It observes, never synchronizes:** Roast S17 per-file results are
  identical with and without `--race`. The file list is the zero-regression
  gate, as in [COUNTING.md](../../status/COUNTING.md).
- **Off-path cost:** `perf-guard --check`, plus an interleaved A/B with a null
  control on loopsum, fib, hash and `tools/bench/parmap.raku`. Sequential A/B
  lies on this box. The hooks and the destructor branch must sit inside the
  noise when the flag is off.
- **On-path cost, published, no target:** wall time and peak RSS under
  `--race` for parmap, the counter and a Supply fan-in, in raw numbers in the
  guide. The reference point is Go's documented 2–20× time and 5–10× memory.
- **The detector's own concurrency:** the `linux-tsan` job runs the corpus
  under `--race`. The shadow table is concurrent code like any other.
- **Size:** new code lives in `src/RaceDetector.{h,cpp}`. Hooks are one
  out-of-line call each, so `tools/source-helpers/budget.raku` stays green.
  `InterpreterCore.cpp` holds the most stripe sites (30) and, at 9e0bdffd,
  is 32,792 lines against a recorded ceiling of 32,830 in
  `tools/source-helpers/budget.txt`. That is 38 lines of headroom, so its
  hooks are one line each, and a block that grows stays in `RaceDetector.cpp`.
- `t/run.raku` green before every commit.

## The payoff: Roast and the ecosystem

**A race in a distribution is that distribution's bug on both engines.** The
measurement table shows each engine failing differently on the same race. So
findings go upstream to authors, as issues with the two sites and the fix
(`Lock.protect`, collect through `await`, route through a `Channel`). They
never go up as "fails on rakupp".

- **Roast S17.** Every report in a Roast file is either a detector false
  positive (ours to fix) or a race in the spec suite. The second goes
  upstream to Raku/roast once it has been read against the model: Rakudo
  passing the file is not evidence either way.
- **Selection.** Every dist whose `lib/` or `t/` mentions `start`, `Promise`,
  `Supply`/`Supplier`, `Channel`, `react`/`whenever`, `hyper`/`race`, `Thread`
  or `Lock`, by a grep over fetched tarballs. The August sweep's state dir
  (`/Users/ash/eco-sweep`) is not on this machine any more. Re-fetch from the
  REA index; [ecosweep/green.txt](../findings/ecosweep/green.txt) lists the
  637 dists that passed then, and those come first because their suites
  already run.
- **Running them.** `RAKUPP_OPT=--race rakupp test <dist>`. `RAKUPP_OPT` is
  prepended to every rakupp's option scan, so it should reach the child
  processes the installer runs `t/*` in. Check that in P4 before relying on
  it.
- **First targets**, chosen because they are the concurrency-heavy code we
  already run:
  - cro (installs since 2026-10-10; Supply-based throughout);
  - Log::Async;
  - Test::Async;
  - Red;
  - in-house: the Rakus server, rakush, Webraku and our own harnesses
    (`t/stress/run.raku`, the roast runner's `Proc::Async` fan-out).
- **Output.** `docs/dev/findings/RACE-SWEEP-<date>.md`: per dist, the report,
  the triage (real or false positive) and the upstream link.

## Risks, named

- **False positives from an unmodelled edge.** One bad report teaches a user
  to ignore the tool. The defence is the edge table plus one silent program
  per row. Nothing ships until every row has its program.
- **Hand-offs that look like synchronization but are not in the model:**
  - values a `start` block captures;
  - `state` initialization;
  - `once`;
  - a lazy Seq reified from two threads;
  - `$*DYNAMIC` lookups that read a caller's frame from a worker;
  - `EVAL` inside a worker.

  Each needs a decision, recorded under open questions. None may be settled
  silently by an engine lock happening to sit there.
- **Vector clock size against thread count.** Programs shaped like `syntax.t`
  spawn thousands of threads. A 4,000-entry clock on every Promise, Lock and
  Channel item is 32 KB each. FastTrack keeps locations small, but sync
  objects carry full clocks. The mitigation is thread-id reuse once a
  thread's final clock has been joined (TSan's approach). That is P5,
  measured first on the S17 files.
- **Missing hooks on fast paths** give false negatives. The audit list and
  the must-report corpus find them; nothing else does.
- **Off-path cost.** One predicted branch per hook, read from a global
  `const bool`, never a `thread_local` (the Darwin TLV cost). The destructor
  hook is the riskiest, because containers die on hot paths. It is measured
  separately in the off-path gate.
- **Native code.** Kernels, JIT and `--exe` run without hooks. v1 disables or
  refuses them, so programs whose hot loops ran as kernels run slower under
  `--race` than the detector alone accounts for.
- **Scope creep.** See non-goals.

## Non-goals

- Making racy programs correct. PARALLEL-PLAN's fence stands.
- Deadlock and lock-order detection. TSan does it and it could be a sibling
  later, but it is a different analysis.
- Static, compile-time race detection.
- Races inside C code reached through NativeCall. That is TSan's job on the
  library.
- Changing the memory model. The detector enforces ASYNC.md; it does not
  extend it.
- Being free. It is a diagnostic mode, and it runs slower.

## Open questions

1. **`.status` polling.** Does reading `Kept` from `.status` order the
   result read after it? Provisionally yes: Rakudo sets the status after the
   result, so status-then-`.result` is not racy there. Check Roast
   (`#?rakudo` todos included) before settling.
2. **Does an element read also read the shape?** Provisionally yes, which
   makes a concurrent `push` against an index read a race. Under Rakudo's
   reallocation that reader can see a stale buffer.
3. **A store past the end from distinct indices** is a shape write, so a race
   (provisionally yes). Both engines fail on it (table row 3). The pre-sized
   form is not a race.
4. The hand-offs listed under risks: `start` captures, `state`, `once`, a
   lazy Seq shared across threads, dynamic variables, `EVAL` in a worker.
5. **The name.** `race` is a Raku keyword (`race for`, `.race`) meaning "run
   in parallel, results in any order". So `rakupp --race prog.raku` can be
   read as "run it with race semantics". Go's `-race` is the precedent people
   will search for. The recommendation is `--detect-races`, with this file
   saying `--race` only as a working name. Decide before P3, when the flag
   lands.
