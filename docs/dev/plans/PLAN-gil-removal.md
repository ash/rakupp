# Plan B: True multicore parallelism (GIL removal)

Status: **future work**. This document is the design/roadmap for letting two or
more threads interpret Raku code *simultaneously and correctly* — i.e. dropping
the Global Interpreter Lock (GIL) as the default, so CPU-bound Raku can use
multiple cores.

Plan A (interpreter safe points, so compute-bound `start` workers are
preemptible and shut down cleanly) is a separate, already-landed change. Plan A
keeps the cooperative GIL; Plan B removes it. They are independent.

## Where we are today

- Workers are real OS threads (`std::vector<std::thread> workers_`).
- A single `std::mutex gil_` (the GIL) is held while a thread interprets Raku.
  Only one thread runs interpreter code at a time.
- The GIL is released cooperatively at *safe points*: blocking I/O
  (`accept`/`recv`/`connect`/subprocess waits — `gilPark`/`gilUnpark`), `sleep`
  (`sleepYield`), and `await`. Plan A adds periodic safe points inside the
  interpreter's hot loops so compute-only code also yields.
- There is an **opt-in true-parallel mode**, `RAKUPP_PARALLEL=1`
  (`parallelMode_`): workers run with **no GIL**. It is only *safe* today under
  strict conditions (below) and is not the default.

So the threads and even a no-GIL execution path already exist. What's missing is
making the interpreter's shared data structures safe to touch concurrently, so
the no-GIL path is correct for arbitrary programs.

## Why the GIL exists (what breaks without it)

A Raku `Value` is a bundle of `shared_ptr`s into mutable structures shared across
threads. Concurrent access without a lock is a data race (UB): crashes,
corruption, or silent wrong answers. The unsafe surfaces, roughly in order of
how often they're hit:

1. **Lexical scopes (`Env`)** — `std::unordered_map<std::string, Value> vars`.
   Two closures sharing an outer `Env` (the common case for `start` blocks that
   close over an outer `my`) reading/writing concurrently → race. Rehashing on
   insert while another thread reads → crash.
2. **Containers** — `shared_ptr<ValueList>` (Array/List), `shared_ptr<map>`
   (Hash). Two threads `.push`/`.=`/`{k}=` the same container → race. Iteration
   racing mutation → invalidated iterators.
3. **Objects** — `ObjectData::attrs` map mutated by method calls from two
   threads on the same object.
4. **Refcounts / lifetime** — `std::shared_ptr` control-block refcounts *are*
   atomic, so sharing a `Value` by copy is fine; but the *pointee* mutation is
   not guarded, and `keptPrograms_` / interner side-tables are appended without
   locks.
5. **Symbol tables** — `classes_`, `namedRegex_`, `global_`, `builtins_`. Reads
   are fine once frozen; writes (runtime `EVAL`, dynamic class creation) race.
6. **Interpreter-global mutable bits** — caches, `$/` / capture registers if any
   remain non-thread-local, RNG state, etc.

`RAKUPP_PARALLEL` sidesteps 1/2/3 by *assuming* workers don't share mutable
state, and handles 5 by **freezing** the symbol tables after compile
(`symbolsFrozen_`, `noteSymbolMutation`) and 6 by making the execution registers
**thread-local** (per-thread `ExecContext`). That's the down-payment; the rest
is this plan.

## Design options

### Option 1 — Fine-grained locking (make structures thread-safe)

Give each mutable structure its own lock (or lock-free design):

- `Env`: a per-`Env` mutex, or switch to a concurrent map. Hot path (variable
  lookup) becomes lock/atomic — measurable overhead even single-threaded.
- Containers: a per-container mutex living next to the `shared_ptr`, or
  copy-on-write semantics for List vs Array.
- Objects: per-object attribute lock.

Pros: correct for arbitrary sharing. Cons: pervasive; locking overhead on the
single-threaded hot path (which is 99% of runs); deadlock risk with nested locks
(e.g. `%h{$k}++` touching Env + Hash); a large, error-prone audit.

### Option 2 — Ownership / actor discipline (Rakudo-like `OS thread + data`)

Keep the GIL for shared state, but let genuinely-independent work run without it.
Formalize what `RAKUPP_PARALLEL` assumes: a worker gets **immutable** access to
frozen globals + its **own** fresh registers, and any data it shares with others
must be explicitly synchronized (Raku already gives users `Lock`, `Channel`,
atomic ops, `Supply`). Programs that share plain `my` containers across threads
without synchronization are *already* undefined in Raku's memory model, so we're
not obligated to make them work — only to not crash the runtime.

Pros: much smaller than Option 1; matches Raku's actual concurrency contract;
builds directly on the freeze + thread-local-registers work already done. Cons:
still need to harden the *runtime itself* (allocation, interners, refcount
side-tables, symbol-table reads-during-late-EVAL) so a race in *user* data can't
corrupt the *interpreter*; needs a clear, documented boundary.

### Option 3 — Multiple isolated interpreters (share-nothing)

One `Interpreter` (heap, symbol tables, everything) per OS thread, communicating
only by copying messages (à la Perl ithreads / JS workers). `start` on a
CPU-bound block spins up/reuses an isolated interpreter.

Pros: zero shared mutable state → no locking on the hot path → true linear
speedup; the runtime stays single-threaded internally. Cons: `start` blocks that
close over outer lexicals need those captured-by-copy (semantics differ from
shared Rakudo threads); cross-thread `Promise`/`Channel` must marshal values;
larger memory footprint; `EVAL`/dynamic types per-interpreter.

## Recommended direction

**Option 2, incrementally**, because three of its prerequisites already exist
(freeze, thread-local registers, GIL-park). Concretely:

1. **Define and document the memory model.** State plainly: unsynchronized
   sharing of mutable `my` data across threads is UB *for the user's data* but
   must never corrupt the *runtime*. This scopes the work: harden the runtime,
   not every user structure.
2. **Harden runtime internals** so a user-data race can't crash the interpreter:
   make allocation / interners / `keptPrograms_` / any global side-tables
   thread-safe (locked or lock-free); confirm all execution registers and
   capture/`$/` state are thread-local; audit `shared_ptr` usage for pointee
   mutation on shared control paths.
3. **Guard the still-shared symbol tables.** Reads are lock-free once frozen;
   route the rare post-freeze mutation (runtime `EVAL`, dynamic class/regex) through
   a lock and re-freeze, or forbid it in parallel regions with a clear error.
4. **Make the concurrency primitives real.** `Lock`, `Semaphore`, `Channel`,
   atomic integer ops, and `Supply`/`Promise` must be genuinely thread-safe and
   usable so programs *can* synchronize shared data correctly.
5. **Flip the default carefully.** Keep `RAKUPP_PARALLEL` opt-in until the Roast
   concurrency suites (S17-*, S32-*) pass with zero regressions *and* a stress
   harness (many threads hammering shared Channels/Locks) is clean under TSan.
6. **Ship with ThreadSanitizer in CI** for the parallel configuration — races
   are invisible without it.

## Testing / acceptance

- Full Roast at parity with the GIL build (0 regressions) in parallel mode.
- A dedicated stress suite (producer/consumer over `Channel`, contended `Lock`,
  parallel `map`) clean under `-fsanitize=thread`.
- A measurable multicore speedup on an embarrassingly-parallel Raku benchmark
  (the whole point).

## Non-goals

- Making unsynchronized shared-mutable-data programs *correct* (they're UB in
  Raku too). The bar is: never corrupt the runtime; give users the primitives to
  synchronize.

## Related code

- GIL + parking: `gil_`, `gilPark`/`gilUnpark`, `sleepYield`, `gilYieldNotify`
  (Interpreter.cpp).
- Opt-in parallel: `parallelMode_` / `RAKUPP_PARALLEL` (Interpreter.cpp:352).
- Freeze: `symbolsFrozen_`, `noteSymbolMutation` (Interpreter.h).
- Thread-local registers: `ExecContext`, `saveCtx`/`loadCtx`.
- Worker spawn/drain: `spawnPromise`, `drainWorkers`, `workers_`, `liveWorkers_`.
