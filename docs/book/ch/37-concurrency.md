# Concurrency

Raku has a full concurrency surface: `start`, `await`, `Promise`, `Channel`,
`Supply`, `react`/`whenever`, `Lock`, atomics, and a scheduler. Raku++
implements it on real OS threads, running interpreter compute on all cores by
default, with a **global interpreter lock** still there as an opt-in mode.

This chapter is the story of getting there in stages, because the stages are
still visible in the code and each solved a specific class of bug.

## Stage 1: per-thread execution registers

The state that belongs to one thread of Raku execution — its current lexical
scope, the dynamic-variable chain, the recursion depth, the gather and supply
collectors — was originally interpreter members. It is now a struct:

```cpp
// src/Interpreter.h
struct ExecContext { /* Chapter 13 */ };
static ExecContext& tctx();   // this thread's context, through t_hot.ctx
void saveCtx(ExecContext& c);
void loadCtx(ExecContext& c);
```

Being per thread, each real worker thread owns its own set, so
interpreter execution needs no per-handover register swap. The save and load
functions remain because the design allows a parked thread's registers to be
stashed and restored; today they merely shuffle within a thread's own copy.
(The embedding API's `own_stack` option is the one place a set does change
threads: it carries the host thread's registers to the big-stack thread with
the same pair.)

The same treatment was applied, thread by thread, to everything ThreadSanitizer
reported:

```cpp
// src/Interpreter.h
struct ThreadHot {
    LoopPoll poll;
    ExecContext* ctx = nullptr;   // see Interpreter::tctx()
    RaceThread* race = nullptr;   // the race detector's record of this thread
};
RAKUPP_CONSTINIT extern thread_local ThreadHot t_hot;
extern thread_local Value t_threadSelf;

RAKUPP_CONSTINIT static thread_local Value* builtinTopicWB_;
RAKUPP_CONSTINIT static thread_local bool deferGather_;
RAKUPP_CONSTINIT static thread_local
    std::vector<RedispatchCtx>* redispatchPtr_;
static thread_local ReactStack reactStack_;
RAKUPP_CONSTINIT static thread_local int threadDepth_;
```

The one-shot registers a call consumes — the topic write-back, the
no-autothread flag, the loop-phaser control — began as thread-locals of their
own and are now members of `ExecContext`. What every loop iteration asks of its
thread sits in one struct, `LoopPoll`, beside the context pointer, because on
macOS each thread-local a function names is a call of its own: four variables
cost a loop body up to four calls where one struct costs one.

(More live outside this header and are worth knowing about: the call-frame
pool, `RVec`'s free list of small `ValueList` blocks, and the free lists of
`SlabPool` and `BodyPool`, one per size class — all per-thread caches of
released memory rather than per-thread state. A block allocated on one thread
and released on another simply joins the releasing thread's list, so the hot
path takes no lock; `BodyPool` takes one only when a list runs dry and refills
from the shared spans, or grows past its bound and hands half back.)

The comment on the call registers records the scale: as plain members they were
written by every call on every thread, and were TSan's top report — **2,761
lines on a program with no sharing in it at all**.

One member deliberately stayed shared, and the reasoning is a good example of
measuring rather than assuming:

```cpp
// src/Interpreter.h — the current source line, for test diagnostics
// Written on EVERY statement, so a thread_local costs a TLV lookup per
// statement on macOS — measured +6% on loopsum. A relaxed atomic global is
// a plain store, defined under concurrency, and TSan-clean. It stays that
// one shared value until a worker thread is created, at which point the
// sharing would put another thread's line in this one's frames and every
// thread moves to its own (stmtLinesGoThreaded, above).
struct RelaxedLine {
    void operator=(int x) {
        if (g_stmtLineThreaded.load(std::memory_order_relaxed))
            t_stmtLine = x;
        else g_stmtLine.store(x, std::memory_order_relaxed);
    }
    operator int() const {
        return g_stmtLineThreaded.load(std::memory_order_relaxed)
                   ? t_stmtLine
                   : g_stmtLine.load(std::memory_order_relaxed);
    }
} curLine_;
```

Thread-local storage is not free on every platform. The first version of this
struct went further and kept the one shared value for good, on the argument
that a diagnostic field does not need to be exactly right. It does:
`callframe(1)` reads the line, and once a worker existed a frame taken on the
mainline could carry the worker's. Log::Async stamps every message that way,
and its `t/14-frame` failed 7 runs in 20. So creating a worker thread latches a
flag for the rest of the run, and from then on every thread keeps its own
line. The thread-local cost is paid only by programs that spawn threads.

That lesson was learned again, expensively, on a field that *does* need to be
right. Comparing this tree against the shipped v3.24.0 release found the plain
assignment loop and the range-sum loop running 1.4 to 2 per cent *slower* than
the release, and a bisect narrowed it to one line added to the `ExprStmt` arm
of `exec`:

```cpp
tctx().curStmtExpr = e;   // which expression this statement is evaluating
```

`tctx()` is thread-local, so on macOS that store is a real call to
`_tlv_get_addr` — the second-heaviest symbol in an interpreter profile — and
placing it at the top of the arm forces a resolution the rest of the arm would
otherwise have folded into one. It costs about 34 instructions **per statement
executed**: an empty loop body is unchanged, and every non-empty body moves by
the same amount, which is how the cost was pinned to the statement rather than
to the operation.

The difference from `curLine_` is that this one cannot simply be made
approximate. Cooperative `next`/`last`/`redo` read `curStmtExpr` to tell a bare
loop control from one nested inside an expression, so the field has to be
exact. The fix is to pay for it less often — set it only for statements whose
expression *can* contain a bare loop control, which is a parse-time property of
the node — or to hoist the thread-local resolution to the top of `exec`, where
the function needs it anyway.

The second is what was done. The arm now resolves the context once and stores
through the reference:

```cpp
// src/InterpreterCore.cpp — exec, the ExprStmt arm
ExecContext& tc = tctx();   // one thread_local read for the arm
tc.curStmtExpr = e;
```

And `tctx()` itself got cheaper on 20 October 2026. The context had been a
`thread_local` with a destructor, which on Darwin is reached through an
initialization guard on every mention: two `_tlv_get_addr` calls, the guard
and then the variable. It is now reached through `t_hot.ctx`, a
constant-initialized pointer bound on a thread's first use, whose address
Clang computes once per function. Measured then, as the median of 5
interleaved runs: 4.3 per cent fewer instructions on the benchmark set with
the integer kernels off, and `_tlv_get_addr`'s share of busy samples on
`loopsum`, kernels off, down from 17.7 to 10.1 per cent.

The counterweight is worth stating in the same breath, because it stops this
reading as "thread-locals are bad". The `ValueList` free list of Chapter 12 is
a *new* `thread_local` on the hottest path in the tree — every interpreted call
touches it — and it is one of the largest wins in this book. A thread-local
access costs what it costs; what matters is how much work rides on the access.
One pointer store per statement is a bad trade. Removing a `malloc` per call is
a good one.

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
    if (!t_hot.poll.isWorker) return;        // main-thread loops never park
    if (workerAbort_.load(std::memory_order_relaxed)) throw WorkerAbortEx{};
    if (++t_hot.poll.safePtCtr >= 4096) {
        t_hot.poll.safePtCtr = 0; workerYield();
    }
}
```

Two jobs in a few lines. It periodically hands the GIL back, so a compute-bound
worker cannot starve the main thread. And it unwinds a worker whose result is no
longer wanted, at shutdown, by throwing an exception that is deliberately **not**
a `RakuError` — so a user's `CATCH` cannot swallow a shutdown.

The main-thread early return means the check is one predicted branch on a
thread-local bool for every loop that is not in a worker.

## Stage 3a: true parallelism, and then the flip

Worker threads run interpreter compute concurrently instead of serialising on
the GIL — safe once the registers are thread-local and the symbol tables freeze.
That arrived as an opt-in, and v3.0.0 made it the default:

```cpp
// src/Interpreter.cpp — the constructor decides the mode
const char* g = std::getenv("RAKUPP_GIL");
const char* p = std::getenv("RAKUPP_PARALLEL");
bool gilWanted = (g && *g && std::string(g) != "0") ||
                 (p && std::string(p) == "0");
parallelMode_ = !gilWanted;
```

`RAKUPP_GIL=1` selects the cooperative GIL — the escape hatch, the bisection
tool and a CI leg — and `RAKUPP_PARALLEL=0` is honoured as a synonym for
symmetry with the old opt-in spelling. `RAKUPP_PARALLEL=1` does nothing: it asks
for what is already true.

The member's own initialiser still reads `bool parallelMode_ = false;`, which is
a default the constructor overwrites on the next line. It is worth knowing
because it is exactly the sort of line a reader — or a chapter — quotes as if it
settled the question.

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
            I.liveWorkers_.load(std::memory_order_acquire) > 0)
            l = lockStripe(p);
    }
};
```

The pool behind it is 256 recursive mutexes, picked by a hash of the
container's address; `cas`, the atomic operators and `Channel` share it.

Two conditions, not one. Parallel mode **and** live workers — because before the
first spawn and after the last join, a single thread cannot race itself. A
single-threaded program therefore pays two predicted branches and nothing else,
default mode or not.

That second condition was not an optimisation for its own sake: the unconditional
stripe tax was pushing compute-heavy Roast files past the timeout, in files with
zero threads in them. The rule it encodes — **the machinery must be free when one
thread runs** — is the gate every stage of this work had to pass.

The load of `liveWorkers_` was once relaxed. It is an acquire now, because a
worker decrements the count before anyone joins it: reading zero is the only
edge from the worker's last store to the unlocked access that follows, and
under the relaxed load the owner could copy a value the worker had already
released.

### The torn-copy contract

A stripe around growth keeps the *container* whole. It does not keep a
*value* whole. A `Value` is several words and one of them is a counted
reference; the count is atomic, but that protects the count, not the word that
points at it. A thread copying a value out of a slot while another stores into
the same slot can read the old reference after the store has released it, and
then count a body that is already freed.

So the rule, which the plan calls the torn-copy contract: **a `Value` copied
out of a shared slot is copied under that slot's stripe, paired with the
striped store.** It arrived in three deliveries — plain lexicals in August
2026, then array elements, hash elements and attributes in September (`cas`
on those had built a cyclic linked list about one run in three), then
compound assignment in October, after a probe of 48 `OP=` shapes run by two
workers on one container crashed on 33.

```cpp
// src/Interpreter.h
struct SlotStripe {
    std::unique_lock<std::recursive_mutex> l;
    SlotStripe(const Interpreter& I, const void* p, RaceAcc acc) {
        if (I.parallelMode_ &&
            I.liveWorkers_.load(std::memory_order_acquire) > 0)
            l = I.lockSlotStripe(p);
        if (RAKUPP_RACE_ON()) I.raceSlot(p, acc);
    }
};
const Value& slotView(const Value* p, Value& copy) const {
    SlotStripe rs(*this, p, RaceAcc::None);
    return rs.l.owns_lock() ? (copy = *p) : *p;
}
Value slotRead(const Value* p) const {
    SlotStripe rs(*this, p, RaceAcc::None); return *p;
}
Value slotOut(const Value* p, bool sink) const {
    return sink ? Value::any() : slotRead(p);
}
void slotStore(Value* p, const Value& v) const {
    SlotStripe ws(*this, p, RaceAcc::Write); *p = v;
}
```

`slotView` is what a read-modify-write computes from: the slot itself while no
other thread can reach it, else a copy taken under the stripe. `slotStore`
puts the result back under the stripe. No lock is held in between, because the
operator in the middle may run user code. That does not make `$n += 1` from
two threads add up — a lost update is still the user's race — but it can no
longer free a string twice, which two threads' `$s ~= "a"` did on every run.

`slotOut` is the newest of the four, from 10 November 2026, and it closed a
gap in the oldest path. An assignment is an expression, and what it answers
is the slot's value. Thirteen return sites in the assignment paths stored
under the stripe and then copied `*lv` for the answer after the stripe was
gone, so a second thread's store in that window freed what the copy was
counting. ThreadSanitizer reported it on `private-eval`, one of the stress
programs, about 1 run in 8, and one run died in the allocator. The fault was
old; what made it show was the 32-byte `Value` of the same day. A `Str` of 8
to 15 bytes, which the 64-byte `Value` kept inline, has a counted body now,
so the stress program's `$v = "o-" ~ $n` began to allocate where it had not.

### Private slots

A `SlotStripe` differs from a `ParStripe` in one step, `lockSlotStripe`: a
lexical slot that no other thread can reach takes no lock even while workers
are live. A slot is private when three things hold: the static mask the pad
resolver computed for its layout says nothing shares it, the frame's run-time
mask says it has not escaped since, and it is not a shared cell. (A program
that reaches frames through dynamic scope switches the proof off for the rest
of the run.) On the four-way fan-out benchmark `RAKUPP_STRIPE_STATS=1` counted
8 stripe acquisitions in the whole run, against 3,600,024 with
`RAKUPP_PRIVATE_SLOTS=0`, which stripes every slot as before; each worker ran
at 1.09 to 1.13 times its solo time, where it had been 1.21 to 1.45 (measured
2026-10-08).

## Threads need big stacks

The tree walker recurses deeply, and the default stack for a non-main thread on
macOS is 512 KB — a recursive Raku sub inside `start {…}` overflows it within
about a hundred frames, producing a bus error and then a process that will not
die at exit.

```cpp
// src/Interpreter.h — BigStackThread
const size_t kStack = stackBytes ? stackBytes
                                 : (size_t)256 << 20; // 256 MiB (virtual)
pthread_attr_setstacksize(&attr, kStack);
if (pthread_create(&h_, &attr, entry, fn) == 0) joinable_ = true;
else if (inlineOnFailure) runInline_(fn);   // creation failed: run inline
else { std::unique_ptr<Fn> g(fn); }         // …or tell the caller
```

The reservation is virtual and committed only as used. Windows gets the same
through a small shim kept in `Runtime.cpp` so `<windows.h>` stays out of the
widely-included header. If thread creation fails, the work runs **inline** rather
than being lost. A body that loops — a pool worker's — asks for the other
branch, since running a worker loop on the caller would never return; its
creator checks `joinable()` instead.

## Worker bookkeeping, and two real crashes

```cpp
// src/Interpreter.h
struct WorkerSlot {
    BigStackThread th;
    std::shared_ptr<std::atomic<bool>> done;
    bool pooled = false;               // runPooled's: endPool joins it
};
std::vector<WorkerSlot> workers_;
void addWorker(BigStackThread&& th,
               std::shared_ptr<std::atomic<bool>> fin, bool pooled = false);
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

Two counters stand behind `liveWorkers_`'s name, and the split is a fix.
`liveWorkers_` falls when a worker *thread* exits; `runningBodies_` falls the
moment its *body* returns. The stripes and the cycle collector read the first,
since a thread that is tearing down is still freeing things. A check that asks
whether another thread can be running Raku code reads the second: right after
`await start {…}` the thread may not have exited yet, and reading the wrong
counter there made a bind refuse its cell and a later `my $*X` invisible to a
callee, now and then.

A `start` block still gets a thread of its own; a pool for those was measured
and deferred. Since 30 October 2026 a promise's *continuations* do run on a
pool: a `.then`, or a `whenever $promise`, goes to an idle pooled worker if
there is one, and otherwise to a new thread that joins the pool after its job.
At most 8 wait idle, and an idle one is not counted in `liveWorkers_`, so the
stripes and the collector go on as if it were not there. Parallel mode only;
under the GIL each continuation keeps a thread.

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
- **the stress matrix with a ratchet.** `t/stress/run.raku` runs 30 programs
  in both modes. A case on its known-bad list that fails is tolerated; one
  that passes is an error too, so the list shrinks in the commit that fixes
  the case and can never go stale. The list is empty today. One case is
  listed as flaky: four workers storing by index into one growing array still
  crash about 1 run in 50, because an element pointer outlives the stripe
  while a sibling's growth moves the buffer;
- **the same matrix under TSan**, as the `linux-tsan` CI job. It is not clean
  in parallel mode, and the harness says exactly how: 9 programs are skipped
  (8 stage a deliberate race, so a report proves nothing), 2 run with a known
  report tolerated, and 12 of the private-slot contract programs are
  tolerated for reads of a slot's flags ahead of the stripe — with one thing
  kept strict, which is that a report in which a *store* held no lock fails
  the case;
- **`rakupp --detect-races`**, since v5.6.0: the same question asked of the
  user's program. A happens-before detector with vector clocks, dormant
  unless asked for, that reports each pair of unordered accesses with both
  lines when the program ends and exits 66. It watches what the interpreter
  runs; compiled code has no hooks;
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

- **A worker thread's own loop is slower than the main thread's.** Measured on
  an Apple M3 (4 performance and 4 efficiency cores) on 2026-10-08, after the
  private-slot work, one `start` block with nothing to contend with runs at
  0.94× — and under `RAKUPP_GIL=1` the same single `start` runs at serial
  speed, so it is a per-operation cost while a worker is live rather than
  thread setup. It caps every ratio below: a four-way CPU fan-out is 3.65×
  where C threads running the same loop reach 3.81×, eight-way is 5.32×
  against 6.13×, and four threads updating one `atomicint` are 0.93× — a net
  loss, because the cache line moves between cores faster than any of them
  makes progress. Before that work the same sitting read 0.86×, 3.26×, 3.16×
  and 0.82×.
- **ThreadSanitizer is not clean in parallel mode**, as the list above says.
  The reports left are reads of a slot's flags before the stripe is taken, and
  one class in the promise and `await` handshake.
- **User data races are the user's.** As in Rakudo, mutating shared data without
  a `Lock` is undefined; the striped locks protect the interpreter's own
  structural invariants, not the user's semantics.
- **The symbol freeze is enforced by a tripwire, not by the type system.** A new
  structural writer that forgets to call `noteSymbolMutation` is invisible.
- **Worker stacks are large.** 256 MiB of virtual address space each is cheap
  but not free, and it is why the spawn throttle exists.
