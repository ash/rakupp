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

*Status, 2026-10-25: **P0 to P3 are built.** `rakupp --detect-races
prog.raku` (or `RAKUPP_RACE=1`) reports each data race when the program
ends, with both sites, and exits 66. The corpus and its harness are in
`t/race-detect/`. The thread records, vector clocks, every edge in the
table, the locations (scalars, array elements and lengths, hash entries and
keys, attributes) and the report are in `src/RaceDetector.{h,cpp}`, dormant
unless the flag or `RAKUPP_RACE`/`RAKUPP_RACE_TRACE` is given. P4 has had
its first pass: all of Roast S17 and four dists, with every report read
(docs/dev/findings/RACE-SWEEP-2026-10-25.md). What was built and what
turned up is in "P0 and P1, as built", "P2a, as built", "P2b, as built",
"P3, as built" and "P4, first pass" below.*

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

### P0 — the corpus, before any detector code (built 2026-10-24)

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

### P1 — clocks and edges (built 2026-10-24)

Thread records, vector clocks, and every edge in the table, but no location
tracking yet. `RAKUPP_RACE_TRACE=edges` dumps each edge as it fires. A
must-stay-silent program has to show the edge it exists to test.

### P2 — locations (built 2026-10-25: "P2a, as built" and "P2b, as built")

Scalar slots and attributes first: the counter and `ub-object-attrs`. Then
array and hash elements and shapes. Hooks go beside the `ParStripe`/`SlotStripe`
sites, and the private-slot skip comes in at the same time. Then the audit:
the list of unhooked container access paths, written into this file and
burned down against the must-report corpus.

### P3 — reports and the flag (built 2026-10-25: "P3, as built")

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

## P0 and P1, as built

### The corpus (`t/race-detect/`)

The programs are labeled in their header comments:

- `# race-detect: silent` or `report`;
- `# edges:` names the edges a silent program exists to test;
- `# expect:` gives its last line of output;
- `# location:` names what a report program races on;
- `# gil: skip (why)` leaves out the `RAKUPP_GIL=1` leg.

The t/stress programs carry the same labels by reference, in
`references.txt`. Every program was labeled by reading it against ASYNC.md's
model, and every one runs under Rakudo 2026.09. The silent ones print the
same answer there; distinct-keys dies there, as in the table above.

| half | own programs | t/stress, by reference |
|---|---|---|
| must-stay-silent | 27: one or more per edge-table row, the FAQ's Lock and atomicint counters, the pre-sized indexed write | 7: lock-counter, hash-guarded, atomic-counter, channel-pipeline, supply-fanin, promise-chain, parallel-map |
| must-report | 11: the table's rows 1–3, one per location kind, one per missing edge (un-awaited start + sleep, two `.then`s, plain mixed with `⚛`, a write outside the Channel item) | 21: the seven ub-*, the thirteen private-*, and cas-containers (plain reads against `cas`) |

Three labels needed a second look. The plan had listed none of the three:

- **cas-containers is a race.** It swaps each slot with `cas` while another
  worker reads the slot plainly.
- **All thirteen private-* programs race.** Each has the owner and a worker
  storing into `$v` with nothing between them.
- **channel-pipeline's channel edges learn nothing.** It awaits its producers
  before it reads the channel. Its reference names only the edges that each
  run is sure to show, and promise-chain's does the same. Whether
  promise-chain's `.then`s run inline or as a fork depends on whether the
  start block has finished.

`rakupp t/race-detect/run.raku [--runs=N] [--only=…]` runs every silent
program under `RAKUPP_RACE_TRACE=edges`, by default and under `RAKUPP_GIL=1`.
A run passes when it exits 0, prints its expected line, and shows every edge
it names. For an acquire, that means a line showing that it learned another
thread's clock. For a spawn kind (`start`, `then`, `hyper`), it means the
fork line. The report half has its labels checked and nothing run until P2.

On 2026-10-24 that came to 68 checks at `--runs=20`, every run passing. Two
of the 68 are skips: semaphore and lock-condition on the GIL leg (see below).
The whole set takes 46 s.

### The edges, and where each hooks in

`RAKUPP_RACE_TRACE=edges` prints every fork and release, and every acquire
that learned something:

```
race-trace: fork start T0 -> T1 (line 10)
race-trace: release promise T0@2
race-trace: acquire promise T1 <- T0@2
race-trace: fork then T1 -> T2 (line 0)
race-trace: acquire promise T0 <- T1@2 T2@1
```

The trace prints an acquire only when it orders something new. The list
after `<-` is the components that grew, as `thread@clock`.

| edge | hook |
|---|---|
| fork | `BigStackThread`'s constructor and `run_`: one choke point for every worker (`start`, `Thread.start`, supply tickers and readers, timers, `.then` continuations, hyper batches, `cue`). `RaceForkTag` names the fork in the trace: `then` in Promise `.then`, `hyper` in `runParallel` |
| promise | `PromiseState::done` became `RaceDone`. Assigning true releases, and reading it while true acquires, so every keep, break and await in the engine is covered by one type: `await`, `.result`, `.status`, `Thread.finish`, a combinator folding its members, `whenever` on a promise, a `.then` registered after the settle. Nine engine-internal looks read the raw `.v` instead (listed under "Found on the way") |
| lock | the Lock arm (`MethodCallPart2.cpp`): `protect` (a `RaceHold`), `lock`, `unlock`, and condition `wait` (release before, acquire after). The clock is keyed by the `LockState`, or by the Lock's hash under the GIL, where there is no mutex but the edges are the same |
| lock-async | a `.lock` that finds the lock free acquires the lock's clock, and `.unlock` releases into it. A queued waiter's edge is its Promise |
| semaphore | `.release` releases; `.acquire`, and a `.try_acquire` that succeeds, acquire |
| channel | a shadow queue of clocks per channel (`raceChanSend`/`raceChanTake`), with its push and pull sites: `send`, `poll`, `receive`, `.list`, the two supply readers of a channel, and the two `Supply.Channel` feeds. The take checks that the queue and its shadow agree in length. If they do not, it acquires every send so far, which can miss a report but cannot invent one. `.close`/`.fail` release into a close clock, which a pull that finds the channel closed acquires |
| supply | `supplierMutex` is now a `RaceMutex`: lock acquires and unlock releases, which covers emit, done, quit and tap registration. A supply activation (`SupplyTapCtx`) acquires when taken and releases when handed back, and a queued delivery carries a snapshot of its deliverer's clock |
| react | `ReactCtx::cv` is a `RaceCv`. Every notify releases (the thirty-odd places a source or the react ends all notify it), and `runReactLoop` acquires as it returns |
| atomic | a `RaceHold` over `atomicOn`, where `cas` and every `atomic-*` op meet |

`Proc::Async` needed no hook of its own: proc-async.raku's taps are ordered
by the process promise. `full-barrier` has none, by the table.

A thread's clock lives in its record, which hangs off `t_hot` (a pointer
added to `ThreadHot`, not a new `thread_local`). When a worker's body ends,
its clock is freed, since what it released lives on in the sync objects.
Sync objects with no state struct of their own keep their clock in a side
table keyed by identity:

- a Lock or Semaphore under the GIL;
- a Lock::Async;
- an atomic's container;
- a channel's shadow queue.

P1 never reclaims these entries. A freed object's address that comes back
inherits the old clock, which can add an edge, a missed report, but never
invent one. P2's destructor hook is where that ends.

### Gates, with the detector off

These ran on 2026-10-24 on macOS arm64, nice'd, with load averages of 3–6.
Roast and the stress suite ran one build before the last change, which moved
the channel and snapshot hooks' `g_race` test inline. That build behaves the
same with the detector off. `t/run.raku` and the corpus (`--runs=20`) ran on
the final build.

- `t/run.raku`: 1795/1795.
- Roast: 1425/1425 files and 221,033 assertions, the same as before.
- `t/stress/run.raku`: 55 of 56 pass, the one left being the known-flaky
  ub-indexed-grow/parallel. No new failures.
- `budget.raku`: OK. The preprocessed headers, measured by hand with this
  box's clang:

  | header | lines | ceiling |
  |---|---|---|
  | Interpreter.h | 84,074 | 84,300 |
  | BuiltinsParts.h | 92,587 | 92,835 |
  | InterpreterParts.h | 100,576 | 100,815 |

- The binary grew by 23 KB, from 25,369,592 to 25,392,968 bytes; the
  detector's object file is 17 KB of text. `t/slim/run.raku`, the `--slim`
  hello budget included, passed with P2a in (2026-10-25).

`perf-guard --check` read INCONCLUSIVE, with its runs spanning 6–50% against
a 5% gate. The interleaved A/B used 11 pairs against a build of 8c570a98,
with a base-against-base null control, all under `RAKUPP_NO_KERNELS=1` so
that the interpreter paths are what gets timed:

| benchmark | median change | pairs slower |
|---|---|---|
| loopsum, fib, hash, parmap | −3.0% to −0.2% | 2–4 of 11 |
| spawn (`await start`), Lock `protect`, Supplier emit | −0.4% to +0.2% | 4–6 of 11 |
| `⚛++` | −2.5% | 1 of 11 |
| Channel send + receive | +1.6% to +1.8% | 19–20 of 21 |

Each benchmark's null control split evenly. The channel result needed a
check, so the channel hooks were compiled out of `MethodCallPart1c.cpp` and
the two new builds were measured against each other: +0.1%, slower in 11 of
21, a null. The 1.6% is code layout from the header changes, not the hooks.
At that size it is inside what microbenchmarks overstate
(raku-pp-benchmarking).

### Found on the way

- **An engine-internal look at a promise was an edge.** The sunk-`start`
  check, which queues the scheduler's `uncaught_handler`, read `done`.
  Under the GIL that made the mainline acquire a finished worker before it
  had received a single channel item, and channel.raku's trace showed no
  channel edge. Nine such looks now read `.v`:
  - that check;
  - allof's reflection of member status;
  - the channel's closed-promise guards (two in `MethodCallPart1c.cpp`,
    two in the `Supply.Channel` settles);
  - the Supply-to-Promise settle guard;
  - keep's and break's double-settle guards.

  This is the plan's "None may be settled silently by an engine lock
  happening to sit there", in the promise flag instead of a lock. Any new
  reader of `PromiseState::done` has to choose between the two.
- **Mode-independent verdicts hold only where the engine's semantics are
  mode-independent.** Under `RAKUPP_GIL=1`, a Semaphore `.acquire` and a
  Lock condition `.wait` return without blocking, as
  [faq/threads.md](../../guide/faq/threads.md) documents. So semaphore.raku
  and lock-condition.raku print 0 there, which is a real race in that run.
  Both skip the GIL leg, with the reason in their labels.
- **rakupp's `Channel.list` says it is lazy** (`.is-lazy` is True; Rakudo
  says False). channel-close.raku consumes with `for` instead. **Fixed
  2026-10-26.** Re-measured, `my @all = $c.list` already waited for the
  close; what differed was `.is-lazy`, on the list and on an Array assigned
  from it. The whole class shared it: a source read live but finite (a
  handle's `.lines`, standard input, a process's output, a conditioned
  `Seq.from-loop`) answered True. Now such a source answers False, list
  assignment reads it to its end, and `lazy` wraps it in a declared view.
  A `for` still walks it live, so #145 stands.
  `t/regression/live-finite-sources-not-lazy.raku` checks this against
  Rakudo 2026.09.
- **A `.then` fork's line is 0.** That is the settler thread's statement line
  at the settle. The report wording in P3 should name the `.then` instead.

## P2a, as built: scalars

### Shadow state and lifetimes

Each location has one FastTrack cell:

- the epoch of its last plain write;
- the epoch of its last plain read, which becomes one epoch per thread once
  two threads' reads are concurrent;
- one epoch per thread for its atomic accesses.

The cells live in one ordered map keyed by address, under one mutex. That
mutex is the detector's own: it orders nothing in the program's clocks.
Recording starts at the first spawn.

A key is an address, so a cell has to die with its container, or whatever
is made at that address next inherits it and reports a race that never
happened. Two hooks cover this:

- `Body<T>::destroySelf` (and `Body<Value>`, a cell) erases the dying body's
  address range. It is the payload's one destroy path, out of line, and
  taken only on a last release.
- `Env::cycleNoteSlots()` erases a frame's pad and variables. Every death and
  every reuse of a frame passes through it first: `~Env`, `clearBindings`,
  `clearBindingsKeeping`, and `FramePool::release`.

### What a scalar location is

`Interpreter::raceSlot` classifies the pointer an access hands it:

- a pad slot of a frame on the current scope chain;
- the inside of a cell that one of those frames holds;
- one of those frames' variables.

A pad slot `slotPrivate` proves private is skipped, as the plan intends: no
other thread can reach it. Anything else (an element, an attribute, a
temporary) is not a scalar location, and is P2b's.

### The hooks

| path | hook |
|---|---|
| the 23 `SlotStripe` sites | a third argument, `RaceAcc::Read` or `Write`, added in place |
| the stores outside a stripe (`*lv = v` in the assignment arms, `++`, compound ops, `applyArithInto`) | `raceW(lv) = v`, in place; 56 sites |
| the fast read paths (`evalVarExpr`'s two, `eval`'s inline copy, the binary fast handler, unary `-`, the typed subtree, the fused int leaf, `~`'s string leaf, the int compare) | `raceR(p)`, or `raceRd(p)` in a static helper (through `liveTarget()`) |
| dynamic variables, the stash, the rw sync, the env-slot proxy | the `ParStripe` slot reads and stores in `InterpreterBinding.cpp` and `InterpreterCalls.cpp` |
| list assignment | `storeThrough` |
| `⚛`, `cas`, `atomic-*` | `atomicOn`, as an atomic access, inside the stripe |

Each hook is one predicted branch when the detector is off, marked unlikely
(`RAKUPP_RACE_ON()`). `raceSlot` and `raceReadAt` are declared cold.
InterpreterCore.cpp, two lines under its ceiling, took every hook without
growing.

`cas` and the `atomic-*` calls evaluate their whole argument list before
taking the container by reference. That read of the container is the
engine's, not the program's, since both take their target `is rw`.
`RaceQuiet`, a count on the thread's record, hides that one argument pass
from the location hooks. Without it, the FAQ's `atomicint` counter reported
a race.

`RAKUPP_RACE` and `RAKUPP_RACE_TRACE` switch the loop kernels off, because
a kernel's native loop has no hooks. `g_noKernels` reads the variables
itself, since another file's static is not initialized yet. `--exe`, `--jit`
and `--target=js` are still P3's to refuse.

`RAKUPP_RACE_TRACE=accesses` prints each access the hooks see, or skip and
why. That is how the uncovered paths below were found.

### The report

The report goes to stderr at the program's end (`raceFinish`, after
`testRunEnd`):

```
rakupp: data race on $n
  write at line 2 (thread T2, a start from line 2)
  previous write at line 2 (thread T3, a start from line 2)
  nothing orders them: no Lock, await, Channel, Supply or ⚛ between the two threads
rakupp: 1 data race (251,063 occurrences)
```

Each site pair is reported once, with a count. A run with a race that would
have exited 0 exits 66; a failing run keeps its own status. A pair with an
atomic side says so: "⚛ orders atomic accesses only with each other". The
file name, the current access's backtrace and folding the two directions of
one pair into one entry are P3.

### Coverage

The harness now checks verdicts as well as edges:

- silent programs must report nothing;
- a report program on a scalar location must report a race on the variable
  its label names and exit 66, by default and under `RAKUPP_GIL=1`.

| | own | t/stress, by reference |
|---|---|---|
| silent | 27 of 27 silent | 7 of 7 silent |
| report, scalar | 5 of 5 (counter, unawaited-start, two-thens, atomic-mixed, channel-outside-item) | 16 of 16 (ub-env-sharing, ub-opassign, ub-torn-values, the twelve private-* that run in parallel, cas-containers) |
| report, P2b | 6 of 6 since P2b: array-element, array-push, attribute, distinct-keys, hash-entry, indexed-grow (GIL leg) | 5 of 5 since P2b: ub-array-push, ub-hash-write, ub-indexed-grow (GIL leg), ub-iterate-during-push, ub-object-attrs |

Two references changed:

- **private-hyper-for left the gate.** rakupp runs that `hyper for` on one
  thread, kept serial by an array literal in its body. A happens-before
  detector judges the run that happened, and that run has no race.
- **The private-* references skip the GIL leg (`gil:skip`).** Their owner
  stores only while `.status` says Planned. Under the GIL the worker can
  finish first; the owner's first `.status` then reads Kept, an acquire, and
  it stores nothing. Two of 240 GIL runs had no race to report.

A shapes probe races one access shape against another and asks whether the
detector sees it. It sees 32 of 33:

- every read shape: `my $c = $x`, `$x + 1`, `$x < 5`, `-$x`, `"$x"`, `f($x)`,
  `$x.defined`, `1 if $x`, `$x eq "a"`, `$x ~ "a"`, `[$x]`, `$x ?? 1 !! 2`,
  `$x == 1`, `$x * 2`, `$x.Str`, `so $x`;
- every write shape: `=`, `++`, `--`, `+=`, `-=`, `*=`, `~=`, `||=`, `max=`,
  `= [...]`, `= %(...)`, `.=succ`, `($x, $y) = …`.

The one it does not count, `//=` on a defined variable, writes nothing, so
there is no race to see. (`~=` crashed rakupp, detector or not, until the
fix below.)

**Regression sweep.** The 215 t/regression files that use threads ran under
`RAKUPP_RACE=1`. Nine reported races; each report was read against its
program:

- **Seven are races in the test programs.** Each one polls a flag, reads
  after a `sleep`, or bumps a shared counter from several workers:
  assign-lane-cell-bound-names (`our $par += 1`), head-tap-single-done,
  hyper-for-over-threads, our-is-one-container, supplier-preserving-nothing-after-done,
  top100-refresh-2026-09-03 and whenever-promise-emit-preserving.
- **Two are races in the programs these tests run as child processes:**
  kernel-shared-flag and our-on-worker-threads. The children inherit
  `RAKUPP_RACE`, exit 66, and the parent test reads that as a failure.

Three more reports in the sweep were false positives. Each was a missing
edge, fixed below.

### Found on the way

- **Two edges P1 had not modelled, both false reports until they were:**
  - **Registering a `.then` is an edge.** The continuation runs on a thread
    forked from the settler's, which may have been spawned before the
    registrar's latest writes (proc-stream-tap-fidelity:
    `my $start = $a.start; $start.then({ … $start.result … })`). The
    registrar now takes a snapshot, and the continuation acquires it. A
    combinator's `.then` does the same.
  - **A react is a monitor.** Its body and each whenever body push the react
    on `reactStack_` and pop it after. That stack is now a `ReactStack`
    whose push acquires the react's clock and whose pop releases into it.
    The bodies are then ordered with each other and before what follows the
    react (whenever-done-semantics: `$ticks++` in an interval whenever, read
    after the react ends). The react body's writes are ordered before the
    whenevers it set up (signal-supply: a handler on a dispatcher thread
    spawned earlier).
- **`$x ~= "a"` from two workers crashes rakupp** (SIGTRAP, 4 runs of 20),
  with the detector off as well: the PARALLEL-PLAN P3 no-crash contract
  broken by a string append to a shared scalar. Fixed since (2026-10-25):
  every OP= arm copies the slot out and stores back under its stripe
  (PARALLEL-PLAN P3, third delivery). Its contract program,
  `t/stress/ub-opassign.raku`, is a reference here.
- **One access, several races, one name.** An access can race with the
  previous write and with another thread's read at once. `check` recorded
  a site pair for each, but kept only the last for `raceNameLast` to name,
  so the others printed as "data race on a scalar". Each access now keeps
  every pair it found. ub-opassign showed it.
- **Off-path cost.** The A/B ran against a build of 8c570a98 (no detector),
  interleaved, with a base-against-base null control, under
  `RAKUPP_NO_KERNELS=1`, at load averages of 6–18. Before the guards were
  marked unlikely: fib +2.7% (slower in 10 of 11 pairs), vars +1.9%, loopsum
  +1.3%. After:

  | benchmark | change | pairs slower |
  |---|---|---|
  | fib, 21 pairs, two runs | +0.3%, +1.2% | 14, 16 of 21 |
  | vars, 21 pairs, two runs | +1.3%, −0.5% | 17, 8 of 21 |
  | loopsum, strcat, methods, hash, parmap, spawn | −4.2% to +0.8% | 4–6 of 11 |
  | Channel send + receive | +1.0% | 8 of 11 (null control: +1.0%, 10 of 11) |

  Which benchmark reads slower flips between runs, so this is this box's
  noise floor and not a measured cost. `t/run.raku` (1795/1795), Roast
  (1425/1425 files, 221,033 assertions), the stress suite (0 new failures)
  and the budget are unchanged, and `t/slim/run.raku` passes.

## P2b, as built: elements, lengths, entries, keys, attributes

### Keys and lifetimes

The plan's table holds as written. A second shadow map, beside the scalars'
address-keyed one, is keyed (container, sub):

| location | container | sub |
|---|---|---|
| array element | the `ValueList*` | the index |
| array length | the `ValueList*` | `kWhole` (bit 63) |
| hash entry | the `ValueMap*` | the key's hash, bit 63 clear |
| hash keys | the `ValueMap*` | `kWhole` |
| attribute | the `ObjectData*` | the attribute's slot address |

Each container is the payload inside a `Body<T>`. The forget hook
`destroySelf` already calls (`raceForgetRange` over the body) therefore drops
a dead container's cells too, in one range erase over the ordered map. An
attribute's slot address is stable for the object's life, because `ValueHash`
keeps its entries in chunks that never move. Hash entries are keyed by the
key, not the entry's address, because a key deleted and inserted again gets
a new entry. An element access also reads the length, and an entry access
also reads the keys. Recording starts at the first fork (`g_forked`).

### Stores: noted where they resolve, checked where they write

A store's location is found in `lvalue()`, but the write happens after the
right-hand side runs: `@a[0] = await $p` must order its write after the
await. So `lvalue()` and `plainIntStepTarget` (the `@a[$i]++`, `%h{$k}++`,
`$!n++` fast path, which bypasses `lvalue()`) *note* the location: the
element's address, its container and its index, key or slot. The note goes
in a ring of four on the thread record. The store hooks already in place
(`SlotStripe`, `raceW`, `slotStore`) pass `raceSlot` only the address they
write. When the address is not a scalar, `raceSlot` matches it against the
notes and checks that location. A note is used once. A right-hand side that
resolves lvalues of its own can push the outer note out of the ring, which
loses that write: a miss, the safe direction. The note functions return the
address they are given, so `return &slot;` became
`return raceNoteElem(&slot, …);` without a new line in a function at its
ceiling.

### The hooks

| location | read | write |
|---|---|---|
| element | `evalIndex`'s fast and general paths; a `for` loop's element copies | noted in `lvalue()`'s array arm and `plainIntStepTarget`; `:delete` |
| length | every element access; `.elems`; any method on an Array (`raceMethodOn` at `methodCallInner`'s entry, past `methodCall`'s fast paths, whose `.elems`, `.kv` and push arms carry their own); a `for` loop's snapshot | `growToIndex` (a store past the end); the mutator stripe, for push, pop, shift, unshift, splice, append, prepend and grab (`raceResizes`); the plain-push fast path; list assignment |
| entry | `evalIndex`'s single-key lookup | noted in `lvalue()`'s hash arm and `plainIntStepTarget` |
| keys | every entry access; a lookup that misses; any method on a Hash (as for an Array; `.values` has its own) | an insert (`raceKeysLv` in the find-or-insert); `:delete`; list assignment |
| attribute | `$!x` (`evalVarExpr`); the accessor's fast path in `evalMethodCallExpr` and in `methodCall`, and methodCallPart2's accessor arm | noted in `lvalue()`'s `$!x` and rw-accessor arms and `plainIntStepTarget`; the attribute-assign helper |

Every hook is folded onto an existing line. `InterpreterCore.cpp`,
`Builtins.cpp`, `lvalue`, `evalIndex`, `methodCallTail` and
`methodCallPart1c` are at or near their ceilings. `Builtins.cpp` had no
spare line for `methodCall`'s own hook, so a two-line comment there became
one. The mutator stripe in methodCallTail is taken for every method that
reaches that arm, not only the eight resizers. Its hook names a write only
for those, and tests the name only under `RAKUPP_RACE_ON()`.

### The report

Names are built only when a race is new. The accessing thread's scope chain
is walked for the variable that holds the container, then the `self`
object's attributes:

- an element: `@a[0]`, or "element 3 of an array" when no variable holds it;
- an entry: `%h<k>`, or `%h{"a b"}` for a key that is not a word;
- a length: "the length of @out";
- a hash's keys: "the keys of %seen";
- an attribute: "$!count of Counter".

A race on a length or a hash's keys adds a line saying what writes and what
reads it, since "write at line 7" on a `push` is not obvious. The corpus
harness maps a `shape` label to those words.

### Not hooked yet (misses, not false reports)

- slices (`@a[1, 2]`, `%h<a b>`), multidimensional subscripts, and adverbs
  other than `:delete`;
- a `for` loop over a snapshot records the length, not each element;
- a `for %h` loop with no method call;
- native arrays (`my int @a`): their word paths (`packedStep`, the packed
  reads) never reach `arr()`;
- a class's own `AT-POS`/`AT-KEY`/`ASSIGN-POS`, Buf elements, the
  shaped-array lanes (`@m[$i; $j]`);
- a store the fused assignment lanes resolve without `lvalue()`;
- compiled code (`--exe`, `-O`): its `rtMPush` and element stores.

### The corpus

All six programs that were waiting report, 20/20 runs, in both modes
(`indexed-grow` in the GIL leg only, below).
`array-element` reports `@a[0]`, `array-push` the length of `@out`,
`attribute` `$!count of Counter`, `distinct-keys` the keys of `%seen`,
`hash-entry` `%h<k>`, `indexed-grow` the length of `@r`. The five t/stress
references that were waiting report too. `presized-indexed`, distinct
elements of a pre-sized array, stays silent, as do the other 26 silent
programs.

The two programs that grow an array by storing past its end skip their
parallel leg (`# par: skip`, `par:skip` in references.txt): `indexed-grow`
and `ub-indexed-grow`. The stale element pointer
(docs/dev/experiments/STALE-ELEMENT-POINTER.md) crashed `ub-indexed-grow` in
8 of 30 runs under the detector, and `indexed-grow` in 1 of 20. Without the
detector the rate is about 1 in 50. The detector's work at a store runs
inside `SlotStripe`, after `lvalue()` handed out the element's address and
before the write, which is the window that bug needs. Their GIL leg, where
nothing grows the array inside that window, is the check.

### Gates and off-path cost

With the detector off: the corpus 132/132 at `--runs=20`, `t/run.raku`
1795/1795, Roast 1425/1425 files (221,033 assertions), the stress suite 0
new failures, the budget, and `t/slim/run.raku` all pass. The shapes probe
still sees 32 of 33.

The cost, against a build of the same tree without P2b or the OP= fix
(PARALLEL-PLAN P3, third delivery), under `RAKUPP_NO_KERNELS=1`. Wall time
was too noisy to read here, so these are instructions retired and the
fewest cycles of three runs, from `/usr/bin/time -l`:

| benchmark | instructions | cycles |
|---|---|---|
| element reads, `@a[$i % 100]` | −0.00% | +0.3% |
| hash reads, `%h{$k}` | +0.5% | +0.6% |
| `.elems` on an array and a hash | +0.2% | −0.4% |
| push, with a list assignment every 1,000 | +0.3% | −0.5% |
| `for @a` | +0.1% | −0.2% |
| accessor read and `$p.v += 1` | +0.2% | +0.2% |
| vars | −0.0% | −0.2% |

A hash read carries two hooks, the keys and the entry, which is its +0.5%.

### Found on the way

- **The hooks have to be macros.** P2a's hooks pass a pointer the caller
  already holds. P2b's often pass a call, `raceElem(bp->arr(), i, …)`, and
  an inline function evaluates its arguments before its own
  `RAKUPP_RACE_ON()` test. The compiler cannot drop the call, because
  `arr()` may unpack a packed array. With the detector off that cost +1.7%
  on a loop of element reads and +1.5% on hash reads (17 and 16 of 21
  pairs slower). As macros, the arguments are evaluated only behind the
  test, and the implementations are `[[gnu::cold, gnu::noinline]]`.
- **Where a hook sits can cost more than what it does.** `raceMethodOn` at
  the top of `methodCall` cost +2.5% wall on a loop of `.elems` calls (20
  of 31 pairs slower than the same build without it). In cycles, the
  minimum of three runs, it was 4% over the baseline, with the same
  instructions retired (within 0.3%). Below `methodCall`'s fast paths it
  was still +3.2%. At the top of `methodCallInner`, the general chain,
  whose entry is already heavy, it measures at or under the baseline.
  Wall time on this box was too noisy to settle it (a null-control range of
  ±30%); cycles and instructions retired from `/usr/bin/time -l` did.
- **A Supply edge P1 had not modelled: `throttle`.** It delivers to its tap
  from two threads: the emitter's, while the tick has tokens left, and its
  ticker's, for the values it queued. Both call the tap's block directly,
  past the delivery path P1 hooked. A tap's deliveries are serial by the
  Supply contract, so `$s.throttle(2, 0.05).tap({ @o.push($_) })` is
  correct code, and it was reported (supply-combinators). Both delivery
  sites, and the hand-off through the queue, now hold the tap
  (`RaceHold` on its handle, edge `supply`).
- **Regression sweep.** The 215 thread-using t/regression files ran under
  `RAKUPP_RACE=1` again. Three report containers now, and in each the test
  itself reads a container that a worker or a supply's thread writes, after
  a `sleep` and with nothing ordering them:
  - async-socket-eof-forgets-fd: the length of `@conns`. The second
    accept's push and main's `@conns == 2` are ordered by nothing.
  - head-tap-single-done: the length of `@events`, read after `sleep 0.5`.
  - supply-combinators: the length of `@o`, polled while the throttle's
    ticker pushes to it.

## P3, as built: the flag and the report

### `--detect-races`

The name is the plan's recommendation (open question 5, decided). One
option, in `kFlagDocs`, so `--help`'s table and the shell completions carry
it. It is legal wherever code runs: a program, `-e`, the REPL, `--mcp`,
Jupyter. The other modes refuse it, as they refuse `--seed`, with
`Illegal option --detect-races`. With `--jit` or `--cnp` it is refused by
name, because the loops they compile have no hooks.

**It re-runs the process.** The detector's switch, `g_race`, is a
`const bool` set while the process starts. So is `g_noKernels`, which the
detector turns on. Every hook tests that constant, and making it writable
would change the code the hooks compile to. The P2b measurements showed how
little that takes. So the flag sets `RAKUPP_RACE=1` and `RAKUPP_RACE_FLAG=1`
and `execv`s the same command line (on Windows it runs it as a child and
answers its status). The new process's `main` clears both variables first
thing, so the program sees neither in `%*ENV` and its child processes run
without the detector. `RAKUPP_RACE=1` given by hand still reaches children,
which is what the regression sweep's two child-process reports were.

**`jit::configure` keeps the JIT off under the detector**, as the kernels
are kept off. A bundle that bakes in `--cnp`, or an `RAKUPP_CNP`, otherwise
compiled hot loops the detector could not see.

### The report

- **Both sites name their file.** A site is one int: the line in the low 20
  bits and, above them, an index into a table of the files seen, where index
  0 is the program and goes unnamed. `raceSite()` takes the file from the
  innermost call frame's routine (`declFile`), else the file whose top level
  is running, as `--trace` does. Each thread caches the last file, so a loop
  in one file takes no lock. A thread's spawn site is a site too:
  "a start from line 5 of lib/Pool.rakumod".
- **The later access has a backtrace:** the named routines it is in, each
  with the site that called it. It is captured only when a race is new. The
  earlier access has only its site, as planned (Go keeps a history ring for
  it; that is P5).
- **The two orders of a pair are one race.** Two threads taking turns
  produced one record for "write at 7, previous read at 9" and another for
  the reverse. Records are keyed by the sorted pair of sites and kinds, and
  print in the order first seen. ub-opassign went from 42 records to 33,
  with element, entry and attribute races in that count that were not
  reported at P2a.
- **A variable reached by two names says both.** The name comes from the
  scope of the thread that found the race. private-rw-param's owner knows
  its variable as `$v`, and the worker stores into it through an `is rw`
  parameter `$x`. Before folding, each order had its own record and own
  name. Folded, a run in which the worker found every race said only `$x`
  (1 run in 30, and once in the corpus at 20 runs). A record now also asks
  the other thread for its name the first time that thread finds the race
  too, at most once per record: "data race on $x (also reached as $v)". The
  harness reads every name in the header.

```
$ rakupp --detect-races -Ilib words.raku
8
rakupp: data race on the keys of %counts
  write at line 4 of lib/Tally.rakumod (thread T1, a start from line 5)
    in sub note-word, called at line 3
    in sub count-all, called at line 5
  previous read at line 4 of lib/Tally.rakumod (thread T2, a start from line 5)
  nothing orders them: no Lock, await, Channel, Supply or ⚛ between the two threads
  (inserting a key, deleting one or assigning the whole hash changes its keys,
  and every lookup reads them)
```

### Tests

- `t/run.raku`'s CLI section has five new checks:
  - a race is reported on stderr, the run exits 66, and stdout is untouched;
  - a `Lock` makes the same program silent;
  - `%*ENV` has no `RAKUPP_RACE`;
  - `--lint --detect-races` is illegal;
  - `--detect-races --jit` is refused.
- The corpus harness runs its report programs through the flag, as a user
  would, so every gate run takes the re-exec path. The edge trace still
  uses `RAKUPP_RACE_TRACE`, which has no flag.

Gates on the final build:

| gate | result |
|---|---|
| corpus, through the flag | 132/132 at `--runs=20` |
| `t/run.raku` | 1800/1800 |
| Roast | 1425/1425 files, 221,033 assertions |
| stress | 0 new failures |
| budget | OK |
| `t/slim/run.raku` | passes |

The budget caught `main()` 3 lines over its ceiling, and the refusal was
folded onto fewer lines rather than the ceiling raised. One earlier
`t/run.raku` run failed one check and passed on the re-run. The gate script
kept only the summary, so the check is not known. The five new checks
passed 10 runs of 10 on their own. The script keeps the whole log now.

## P4, first pass: Roast S17 and four dists

`tools/race-sweep.raku` runs each file twice, plain and under
`--detect-races`, and keeps the reports. It takes Roast files fudged as
`tools/run-roast.raku` fudges them, a directory, or a distribution's `t/`
from its root (`--root`). The details, every report and the fix for each,
are in [RACE-SWEEP-2026-10-25.md](../findings/RACE-SWEEP-2026-10-25.md).
In short:

| | files | clean | races | changed |
|---|---|---|---|---|
| Roast S17 | 99 | 91 | 8 | 0 |
| Log::Async, OO::Monitors, Cache::Async, Cro::Core | 41 | 38 | 3 | 0 |

Every one of the 11 reporting files was read against the model, and each is
a race in the test:
- flags polled or values read after a `sleep`;
- two locks guarding one flag (OO::Monitors' clone test);
- `cas` loops whose first read is plain (cas.t, cas-int.t). docs.raku.org
  shows the code-object form for those, which does an atomic fetch.

No report was the detector's mistake. Nothing ran differently under it.

What the pass fixed:

- **An engine bug:** the sub forms `push @a, …`, `pop @a` and `shift @a`
  mutated in place with no stripe, and two threads doing them crashed
  rakupp (3 runs in 6). They take the array's stripe now, and
  `t/stress/ub-array-push.raku` covers them.
- **Detector gaps:**
  - The sub forms and `+@a` had no hooks. `raceOn`, one hook for methods,
    builtin subs and `+`, covers them, and the must-report program
    `array-push-sub` pins them.
  - `RAKUPP_OPT=--detect-races` re-ran with an argv that had no null at
    its end.
  - A thread the engine forks outside any statement printed without a
    spawn site; it now says "started by the engine".

Next: more dists, by the selection the payoff section describes. The
findings stay in the findings file: nothing is filed with Raku/roast or the
dists' authors.

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
a finding is written as the program's race, with the two sites and the fix
(`Lock.protect`, collect through `await`, route through a `Channel`), never
as "fails on rakupp". Findings are recorded in `docs/dev/findings/` and stay
there: nothing is filed with third parties (Raku/roast, module authors).

- **Roast S17.** Every report in a Roast file is either a detector false
  positive (ours to fix) or a race in the spec suite. The second is recorded
  once it has been read against the model: Rakudo passing the file is not
  evidence either way.
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
  the triage (real or false positive) and the fix.

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
   lands. **Decided 2026-10-25: `--detect-races`.**
