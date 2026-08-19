# Concurrency

Raku has a full concurrency surface: `start`, `await`, `Promise`, `Channel`,
`Supply`, `react`/`whenever`, `Lock`, atomics, and a scheduler. Raku++
implements it on real OS threads with a **global interpreter lock**, plus an
opt-in mode that switches the lock off.

This chapter is the story of getting there in stages, because the stages are
still visible in the code and each solved a specific class of bug.

## Stage 1: per-thread execution registers

The state that belongs to one thread of Raku execution — its current lexical
scope, the dynamic-variable chain, the recursion depth, the gather and supply
collectors — was originally interpreter members. It is now a struct:

```cpp
// src/Interpreter.h
struct ExecContext { /* Chapter 13 */ };
static thread_local ExecContext tctx_;
void saveCtx(ExecContext& c);
void loadCtx(ExecContext& c);
```

Being `static thread_local`, each real worker thread owns its own set, so
interpreter execution needs no per-handover register swap. The save and load
functions remain because the design allows a parked thread's registers to be
stashed and restored; today they merely shuffle within a thread's own copy.

The same treatment was applied, thread by thread, to everything ThreadSanitizer
reported:

```cpp
// src/Interpreter.h
static thread_local Value* topicWriteback_;
static thread_local bool noAutothread_;
static thread_local int loopPhaserCtl_;
static thread_local std::vector<RedispatchCtx> redispatchStack_;
static thread_local std::vector<std::shared_ptr<ReactCtx>> reactStack_;
static thread_local bool t_isWorker;
static thread_local Value t_threadSelf;
```

The comment on the call registers records the scale: as plain members they were
written by every call on every thread, and were TSan's top report — **2,761
lines on a program with no sharing in it at all**.

One member deliberately stayed shared, and the reasoning is a good example of
measuring rather than assuming:

```cpp
// src/Interpreter.h — the current source line, for test diagnostics
// Written on EVERY statement, so a thread_local costs a TLV lookup per
// statement on macOS — measured +6% on loopsum. A relaxed atomic member is
// a plain store, defined under concurrency, and TSan-clean; the value being
// process-wide is the same arbitrariness diagnostics always had.
struct RelaxedLine {
    std::atomic<int> v{0};
    void operator=(int x) { v.store(x, std::memory_order_relaxed); }
    operator int() const { return v.load(std::memory_order_relaxed); }
} curLine_;
```

Thread-local storage is not free on every platform, and a diagnostic field does
not need to be exactly right.

## Stage 2: the symbol-table freeze

The shared symbol tables — the class registry, the global environment, the named
regex table, the loaded-module set, each class's method map — are mutated freely
while the program is single-threaded. Once concurrency engages they must be
treated as immutable, so worker threads can read them without a lock.

```cpp
// src/Interpreter.h
std::atomic<bool> symbolsFrozen_{false};
void noteSymbolMutation(const char* what);
```

The flag flips when concurrency engages, and `noteSymbolMutation` is a tripwire
wired into every structural writer. Under `RAKUPP_FREEZE_TRACE` it reports any
post-freeze mutation and which thread did it.

That is the interesting part: the tripwire is **empirical evidence** for whether
lock-free reads are safe, rather than an argument that they are. Behaviour is
otherwise unchanged, so the instrumentation costs nothing in a normal build.

## Stage 3: the GIL

```cpp
// src/Interpreter.h
std::mutex gil_;          // held while running Raku
bool gilHeld_ = false;    // engaged once any thread is spawned
void engageGil();         // lazily lock on first async use
```

Only the holder may touch interpreter state. A thread drops it while blocked —
in `await`, in a sleep, around a blocking syscall — so another can run.

The lock is **lazy**: a program that never spawns a thread never engages it and
pays nothing.

Three operations manage the handover:

```cpp
// src/Interpreter.h
void gilYieldNotify();              // unlock + bump a counter + notify
void yieldToWorker();               // drop the GIL until a worker progresses
bool yieldToWorkerFor(double secs); // …bounded
void sleepYield(double secs);       // sleep with the GIL released
bool gilPark();                     // release around a blocking syscall
void gilUnpark(bool wasParked);
```

`gilPark` has a strict contract, stated in the header: the parked window must
touch no interpreter state — only thread-local buffers and syscalls. That is
what lets a child-process wait release the lock so sibling workers can spawn
their own children concurrently.

### Safe points

A background worker doing pure compute has no I/O to yield at, so the
interpreter weaves a cheap check into its hot loop:

```cpp
// src/Interpreter.h
inline void safePoint() {
    if (!t_isWorker) return;                 // main-thread loops never park
    if (workerAbort_.load(std::memory_order_relaxed)) throw WorkerAbortEx{};
    if (++t_safePtCtr >= 4096) { t_safePtCtr = 0; workerYield(); }
}
```

Two jobs in four lines. It periodically hands the GIL back, so a compute-bound
worker cannot starve the main thread. And it unwinds a worker whose result is no
longer wanted, at shutdown, by throwing an exception that is deliberately **not**
a `RakuError` — so a user's `CATCH` cannot swallow a shutdown.

The main-thread early return means the check is one predicted branch on a
thread-local bool for every loop that is not in a worker.

## Stage 3a: true parallelism, opt in

```cpp
// src/Interpreter.h
bool parallelMode_ = false;   // RAKUPP_PARALLEL
std::mutex sharedMut_;
std::atomic<int> liveWorkers_{0};
```

With `RAKUPP_PARALLEL`, worker threads run interpreter compute concurrently
instead of serialising on the GIL — safe now that the registers are thread-local
and the symbol tables freeze. The default is off, so **the GIL path is
byte-for-byte unchanged**.

The few genuinely shared internals a parallel worker can still touch — the test
counters and TAP output, the worker vectors — are guarded by one mutex. User
data mutated without a `Lock` is the user's race, as it is in Rakudo.

For user containers there is a striped lock, and its design is the interesting
part:

```cpp
// src/Interpreter.h
struct ParStripe {
    std::unique_lock<std::recursive_mutex> l;
    ParStripe(const Interpreter& I, const void* p) {
        if (I.parallelMode_ &&
            I.liveWorkers_.load(std::memory_order_relaxed) > 0)
            l = std::unique_lock<std::recursive_mutex>(atomicStripe(p));
    }
};
```

Two conditions, not one. Parallel mode **and** live workers — because before the
first spawn and after the last join, a single thread cannot race itself. A
single-threaded program under `RAKUPP_PARALLEL` therefore pays two predicted
branches and nothing else.

That second condition was not an optimisation for its own sake: the unconditional
stripe tax was pushing compute-heavy Roast files past the timeout, in files with
zero threads in them. The rule it encodes — **the machinery must be free when one
thread runs** — is the gate every stage of this work had to pass.

## Threads need big stacks

The tree walker recurses deeply, and the default stack for a non-main thread on
macOS is 512 KB — a recursive Raku sub inside `start {…}` overflows it within
about a hundred frames, producing a bus error and then a process that will not
die at exit.

```cpp
// src/Interpreter.h — BigStackThread
const size_t kStack = (size_t)256 << 20;   // 256 MiB, virtual
pthread_attr_setstacksize(&attr, kStack);
if (pthread_create(&h_, &attr, entry, fn) == 0) joinable_ = true;
else { std::unique_ptr<Fn> g(fn); g->f(); }   // creation failed: run inline
```

The reservation is virtual and committed only as used. Windows gets the same
through a small shim kept in `Runtime.cpp` so `<windows.h>` stays out of the
widely-included header. If thread creation fails, the work runs **inline** rather
than being lost.

## Worker bookkeeping, and two real crashes

```cpp
// src/Interpreter.h
struct WorkerSlot {
    BigStackThread th; std::shared_ptr<std::atomic<bool>> done;
};
std::vector<WorkerSlot> workers_;
void addWorker(BigStackThread&& th, std::shared_ptr<std::atomic<bool>> fin);
```

`addWorker` is the **only** safe way to register a worker, and the header
explains why in terms of a crash report. In parallel mode spawns happen from
worker threads too — a `start` block tapping a supply spawns the interval ticker
— and the old per-site "reap, then push" pattern mutated the vector from several
threads at once. `vector::erase` corrupted it and the process segfaulted, with
the report naming `erase` inside a supply-interval spawn on thread 5 of 292.

The second crash is inside the fix:

```cpp
// Collect finished slots under the lock, JOIN THEM OUTSIDE IT. Joining
// under sharedMut_ deadlocked: the joinee had set its done flag but was
// still unwinding through code that itself wants sharedMut_ (a worker
// spawning a nested worker) — the reaper held the lock waiting for the
// joinee, the joinee waited for the lock.
```

That is the classic shape — hold a lock across a join — and it is worth
remembering that the *first* fix for a concurrency bug introduced it.

There is also backpressure, parallel mode only:

```cpp
void throttleSpawn() {
    if (!parallelMode_) return;
    while (liveWorkers_.load(std::memory_order_relaxed) >= 384)
        { /* reap finished workers, wait for the herd to thin */ }
}
```

A tap-and-close loop over interval supplies — four spawners times a thousand
activations, each a real thread with a 256 MiB virtual stack — outran teardown
and exhausted the address space. Above the cap a spawner reaps and waits.

**Under the GIL this must stay off**, and the reason is a nice illustration of
how the two modes differ: a spawner spinning while *holding* the lock would keep
the very workers it waits for from ever finishing.

## The user-visible layer

| Type | Backing |
|---|---|
| `Promise` | `PromiseState` in `ext`: mutex, condition variable, result or cause, and a list of `.then` continuations fired once on settle |
| `Channel` | shared state plus a stripe from the atomic pool |
| `Supply` | on-demand blocks run through a `SupplyTapCtx`; live suppliers register a tap record |
| `react`/`whenever` | a `ReactCtx` with an event queue, a live-source count, and its own mutex and condition variable |
| `Lock` | a `std::recursive_mutex`, recursive because `protect` re-enters |
| `Thread` | a `BigStackThread` plus a per-thread identity value |
| `$*SCHEDULER.cue` | a worker with a `CueState` cancellation flag |

Two details in there earn a mention.

**A tap's teardown lives on the context that owns it.** An earlier design kept a
close-callback stack as an interpreter member, which was shared across worker
threads — two concurrent `react` blocks corrupted it. Making it thread-local
fixed the crash and cost 6% on an unrelated benchmark by reshuffling
thread-local storage layout on macOS. The right answer was neither: the context
object already travels with the block, is already thread-correct, and is free.

**`whenever` activations are deferred.** Rakudo runs the `react` body first and
only then activates subscriptions, so a `say` after a `whenever` prints before
the first emitted value. The synchronous drains queue into
`ReactCtx::deferred`, which the `react` implementation runs after the body.

## Signals, and a thing that must be turned off

Signal supplies use the self-pipe trick: a handler writes a byte, a lazily
spawned dispatcher thread reads it and runs the `whenever` block under the GIL.
When the `react` block ends, externally wired taps must be closed explicitly, or
the dispatcher keeps firing the handler after the block is gone — the symptom
was a second Ctrl-C re-invoking a stop method on an already-stopped service.

Separately, and simply: **`SIGPIPE` is ignored process-wide.** Without that, a
TCP server dies when a client disconnects mid-write. It is set once on the
big-stack entry thread.

## Testing it

Concurrency bugs do not appear in unit tests. What found the ones in this
chapter:

- **ThreadSanitizer** over the stress suite, which drove the thread-local work;
- **AddressSanitizer** for the lifetime bugs;
- **a stress suite designed to exhaust something** — a thousand tap-and-close
  activations across four spawners is not a realistic program, and that is the
  point;
- **crash reports read carefully.** "erase inside a supply-interval spawn on
  thread 5 of 292" named the bug precisely;
- **`sample` on a hung process.** One apparent hang in an async test turned out
  to be an unrelated pre-existing bug in a supply transform chain, found by
  sampling the stuck process rather than by reasoning about the test.

## Honest limitations

- **The GIL is the default.** Parallel mode is opt-in and, while the stress
  suite passes under it, it is newer.
- **User data races are the user's.** As in Rakudo, mutating shared data without
  a `Lock` is undefined; the striped locks protect the interpreter's own
  structural invariants, not the user's semantics.
- **The symbol freeze is enforced by a tripwire, not by the type system.** A new
  structural writer that forgets to call `noteSymbolMutation` is invisible.
- **Worker stacks are large.** 256 MiB of virtual address space each is cheap
  but not free, and it is why the spawn throttle exists.
