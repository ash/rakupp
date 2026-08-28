# Raku++ — Concurrency & Async

A focused companion to [COOKBOOK.md](COOKBOOK.md) for the concurrency features.
**Every snippet below has been run on `rakupp` and produces the output shown**
(`# → …`). Run any of them with:

```sh
./build/rakupp -e 'CODE'
```

## The model

Raku++ runs concurrency on **real `std::thread`s coordinated by a global
interpreter lock (GIL)**, CPython-style. By default only one thread executes Raku
at a time, so semantics are correct and single-threaded code needs no locks.
Each worker gets a 256 MiB stack (a quarter of the mainline's recursion budget —
[MEMORY.md](MEMORY.md) has the measured depths).
Blocking operations *release* the GIL, so tasks genuinely interleave in time:
`sleep`/`await` let workers overlap (enough for [sleep-sort](#concurrent-timing-sleep-sort)
to actually sort), and external-process waits (`run`/`shell`) run in real parallel
wall-clock — N concurrent `run('sleep','1')` finish in ~1 s, not N:

```raku
# Sequential: four child sleeps back to back.
my $t0 = now;
run('sleep', '1', :out).out.slurp(:close) for ^4;
say "sequential:  {(now - $t0).round(0.1)} s";      # → sequential:  4 s

# Concurrent: four workers, each blocked on its own child process. Each waiting
# `run` releases the GIL, so the four sleeps elapse at the same time.
my $t1 = now;
await (^4).map: { start { run('sleep', '1', :out).out.slurp(:close) } };
say "concurrent:  {(now - $t1).round(0.1)} s";       # → concurrent:  1 s
```

This holds in the **default GIL mode** — no `RAKUPP_PARALLEL` needed — because the
work being overlapped is *waiting on subprocesses*, not running Raku. It's also why
a subprocess-heavy pipeline (e.g. shelling out to `pandoc` per page) is already
near-parallel under the GIL, and gains little from `RAKUPP_PARALLEL`; that flag
helps when the bottleneck is Raku-level CPU (see below).

Promises, Supplies, Channels, and `react` loops all behave as specified. For work
that is genuinely CPU-bound, an **opt-in mode drops the GIL entirely** so worker
threads run interpreter code in parallel — see the next section.

---

## The two modes: true parallelism (default) and the GIL

There is exactly one knob — the **`RAKUPP_GIL` environment variable** — and it
is read once at startup. You don't change anything in your Raku code; the same
program runs under either mode. **Since v3, parallel is the default**: `start`
and worker threads run on all cores. `RAKUPP_GIL=1` selects the cooperative
global-interpreter-lock mode — the pre-v3 default, kept as the escape hatch.
(`RAKUPP_PARALLEL=0` is honored as a synonym.)

| | **GIL mode** (`RAKUPP_GIL=1`) | **Parallel mode** (default) |
|---|---|---|
| How to select | set the env var | *(nothing — this is the default)* |
| Pure-Raku CPU work | one thread at a time | runs on all cores |
| `sleep`/`await`/subprocess waits | overlap (GIL released) | overlap |
| `Lock` / `Semaphore` | no-ops (the GIL already serialises) | real mutual exclusion |
| Unsynchronised shared mutation | safe (serialised) | **your race** — guard it with a `Lock`, as in Rakudo |
| Roast suite | 280 pass | 280 pass (0 regressions) |

Select the mode from the shell:

```sh
rakupp myprogram.raku              # GIL mode (default)
RAKUPP_PARALLEL=1 rakupp myprogram.raku   # true CPU parallelism
```

In parallel mode the runtime is safe because per-thread state (the current scope,
dynamic-variable chain, gather/react/redispatch stacks) is thread-local, and the
shared symbol tables (classes, subs, globals) are frozen once concurrency engages —
so worker threads read them without locking. What is *not* protected for you is
**your own** shared data: two `start` blocks writing the same array/hash/object
without a `Lock` is a data race, exactly as it is under Rakudo.

```raku
# CPU-parallel fan-out. Under RAKUPP_PARALLEL the workers run concurrently.
sub work($n) { my $s = 0; $s += $_ for 1 .. 4_000_000; $s + $n }
my @p = (^4).map(-> $n { start work($n) });
say (await @p).elems;                     # → 4
#   On an M3 (4P+4E): 3.5 s under RAKUPP_PARALLEL=1, against 7.4 s for the same
#   four calls in a plain loop — 2.1×. Under the GIL: 8.3 s with `start`, 7.7 s
#   without it — the thread setup, bought and not paid back.
```

The number that means something is **7.4 s → 3.5 s**: the same work, same mode,
with and without `start`. Comparing parallel mode against GIL mode instead
folds two different changes into one ratio.

`start EXPR` thunks `EXPR` and runs it *on the worker* (it is not evaluated eagerly
on the spawning thread), so `start work($n)` parallelises just like `start { work($n) }`.

**Match the fan-out to the physical *performance* cores.** The speed-up tops out
at the number of full-speed cores, not the logical-CPU count. On the 4P+4E
machine above, eight `start` blocks do *not* reach ~5×: the extra work spills
onto the efficiency cores (~⅓ the speed) and scheduling contention grows, so
eight land at 1.6× (15.6 s → 9.9 s) against four at 2.1× — more total threads,
*less* speed-up. `$*KERNEL.cpu-cores` reports the logical count (8 on this
machine); size the fan-out to the performance cores you actually have.

### Sharing state safely

```raku
# A Lock actually enforces mutual exclusion in parallel mode (a no-op under the GIL).
my $lock = Lock.new;
my $total = 0;
await (^8).map: { start { for ^10000 { $lock.protect({ $total++ }) } } };
say $total;                               # → 80000   (no lost updates in either mode)
```

`Semaphore` likewise becomes a real counting semaphore under `RAKUPP_PARALLEL`.

### When it helps

CPU-bound fan-out (parsing, transforms, number crunching across `start` blocks)
scales with the number of **full-speed cores**, but *how close to that ceiling
you get is a property of the loop, not of the machine*. Four `start` blocks on
the four performance cores above measure anywhere from **2.1× to 3.7×** depending
on what is inside them — 2.1× for the `$s += $_ for 1 .. 4_000_000` above, 3.7×
for the same fan-out over native `int` arithmetic. Quote a speed-up for your
workload, not a number from a page like this one.

Three things decide whether you see it: keep the fan-out at or below the
performance-core count (oversubscribing onto efficiency cores or hyperthreads
gives diminishing, then negative, returns); make sure the parallel unit is a
real `start` thunk rather than a single serialised bottleneck; and keep shared
mutable state out of the inner loop — N workers incrementing one shared counter
lose most of the gain to contention, where N workers each incrementing their own
and summing after the `await` keep it. Work dominated by external processes or
I/O already overlaps in the default GIL mode (the waits release the lock), so
parallel mode adds less there.

[PARALLEL-SPEEDUP.md](PARALLEL-SPEEDUP.md) turns this into a measurement: how to
set the comparison up so `start` is the only variable, two runnable benchmarks
in [tools/bench/parallel/](../../tools/bench/parallel), and the numbers they
produce (3.72× at N=4 contention-free, 3.51× for N per-worker counters summed at
the end, 2.93× for one shared `atomicint`, and 1.45× for a shared-counter loop
with nothing else in it — all on this machine).

## The memory model — what is guaranteed, what is yours to guard

This is the contract the runtime aims at; the v3 parallelism campaign
([dev/plans/PARALLEL-PLAN.md](../dev/plans/PARALLEL-PLAN.md)) is the work of
making every line of it true in parallel mode. It is the same stance Raku
itself (and Rakudo) takes.

**Guaranteed, in both modes:**

- The **synchronization primitives are correct**: `Lock.protect`/`.lock`,
  `Lock::Async`, `Semaphore`, `Channel`, `Supply` (emissions are serialized
  per tap), `Promise`, and `atomicint` with the `⚛` operators. Data handed
  between threads through any of these arrives whole and in order.
- **Independent data is safe.** `start` blocks that work on values they
  don't share — or share only read-only — parallelize without ceremony.
- The **runtime's own structures** (symbol tables, classes, the scheduler)
  are protected: symbol tables freeze when concurrency engages, execution
  registers are thread-local.

**Undefined — for your data:**

- **Unsynchronized mutation of shared plain data** — two `start` blocks
  pushing to one `@array`, writing one `%hash`, or mutating the same
  object's attribute without a `Lock` — has no defined result. Values may
  be lost or duplicated. This is not a Raku++ limitation: the same program
  is a race under Rakudo. Guard it (`Lock.protect`), route it (`Channel`),
  or count it atomically (`atomicint`).

**The line the campaign is drawing:** a race in *your* data may garbage
*your* values, but it must never crash or corrupt the *runtime*. In GIL
mode that holds trivially (everything is serialized). In parallel mode,
today, it does not yet fully hold — a sufficiently unlucky unguarded
structural race can still abort the process — and closing exactly that gap
(then flipping parallel on by default, with `RAKUPP_GIL=1` as the escape
hatch) is what the campaign's phases deliver. The stress suite in
`t/stress/` is the measurable edge of this contract: what it exercises is
guaranteed; what sits on its known-bad list is the remaining work, and
that list only shrinks.

Practical guidance, in preference order: **don't share** (partition the
work, join results with `await`); **route** shared data through a
`Channel` or `Supply`; **count** with `atomicint`; **guard** the rest with
`Lock.protect`. Reach for raw shared mutation last, and only guarded.

---

## Promises

```raku
my $p = start { [+] 1..100 };
say await $p;                             # → 5050        (start + await)

my $q = Promise.new;
say $q.status;                            # → Planned
$q.keep(42);
say $q.status;                            # → Kept        (manual vow: keep/break)
say $q.result;                            # → 42
```

```raku
# A block that dies makes the Promise Broken; await rethrows the cause.
my $p = start { die "boom" };
say (try await $p) // "caught: {$!.message}";   # → caught: boom
```

```raku
# Combinators and chaining
my $all = Promise.allof(Promise.kept(1), Promise.kept(2));
my $any = Promise.anyof(Promise.new, Promise.kept(1));
await $all, $any;                       # without the await, Rakudo still reports
say $all.status;                        # Planned here — Raku++ keeps eagerly
say $any.status;                        # → Kept (both)

my $p = start { 10 };
my $done = $p.then({ .result + 5 });      # runs once $p settles
say $done.result;                         # → 15
```

## Supplies

```raku
# react / whenever over a from-list Supply
my @seen;
react {
    whenever Supply.from-list(1, 2, 3) { @seen.push($_ * 10) }
}
say @seen;                                # → [10 20 30]
```

```raku
# Supply combinators return tappable Supplies
say Supply.from-list(1..10).grep(* %% 2).map(* * 10).list;
                                          # → (20 40 60 80 100)

say Supply.from-list(3, 1, 4, 1, 5).unique.list;   # → (3 1 4 5)
say Supply.from-list(1..5).max.list;               # → (1 2 3 4 5)  (running maximum)
```

## Supplier — live push

```raku
my $s = Supplier.new;
$s.Supply.tap({ say "got $_" });
$s.emit(1);                               # → got 1
$s.emit(2);                               # → got 2
```

## Channel — a thread-safe queue

```raku
my $c = Channel.new;
$c.send(1);
$c.send(2);
$c.close;
say $c.receive;                           # → 1
say $c.poll;                              # → 2
say $c.closed.status;                     # → Kept        (kept once closed + drained)
```

## Thread

```raku
say Thread.is-initial-thread;             # → True
Thread.start({ say Thread.is-initial-thread }).join;   # → False  (inside a spawned block)
```

---

## Putting it together — a job pipeline

Fan out jobs as Promises (one fails), stream progress through a `Supplier`, and
collect the successful results in a `Channel`. Runs identically under the
interpreter and when native-compiled with `--exe`.

```raku
# Fan out 5 jobs as Promises; a job squares its input; job 4 fails.
my @jobs = (1..5).map: -> $n {
    start {
        die "job $n exploded" if $n == 4;   # → a Broken promise, handled below
        $n * $n
    }
}

my $progress = Supplier.new;
my @log;
$progress.Supply.tap(-> $msg { @log.push($msg) });

my $results = Channel.new;

# Wait for every job to settle (Kept or Broken) before inspecting statuses —
# `allof` keeps once all complete, and never rethrows the broken one.
await Promise.allof(@jobs);

for @jobs.kv -> $i, $p {
    my $n = $i + 1;
    if $p.status eq 'Kept' {
        $progress.emit("job $n → " ~ $p.result);
        $results.send($p.result);
    }
    else {
        $progress.emit("job $n FAILED: {$p.cause.message}");
    }
}
$results.close;

say "progress:";
.say for @log.map({ "  $_" });

# `.list` drains a *closed* Channel: it yields every queued value and then ends.
# (Don't loop on `.poll` until Nil — `.poll` returns an *undefined* value, not
#  `Nil`, once the channel is empty, so `=== Nil` never trips.)
my @collected = $results.list;
say "collected results: ", @collected;
say "sum of squares:    ", @collected.sum;
say "closed promise:    ", $results.closed.status;
```

Output:

```
progress:
  job 1 → 1
  job 2 → 4
  job 3 → 9
  job 4 FAILED: job 4 exploded
  job 5 → 25
collected results: [1 4 9 25]
sum of squares:    39
closed promise:    Kept
```

---

## Concurrent timing: sleep-sort

The classic (joke) [sleep-sort](https://rosettacode.org/wiki/Sorting_algorithms/Sleep_sort)
spawns one task per value, each sleeping in proportion to its value, so the
values *print in sorted order* — the smallest sleeps least and wakes first:

```raku
my @nums = 3, 1, 4, 1, 5, 9, 2, 6;
await @nums.map: -> $n { start { sleep $n / 10; say $n } };   # → 1 1 2 3 4 5 6 9
```

This genuinely sorts on Raku++. Each `start` spawns a real worker thread; the
spawner then yields the GIL only until the worker reaches its first blocking
point. A `sleep` **releases the GIL while it waits**, so all eight workers get
into their sleeps concurrently and wake in duration order — the tasks are
*coordinated in time*, even though only one runs Raku at a time.

The same handoff keeps ordinary code predictable: a `start` block that just
computes runs to completion the moment it's spawned (its effects are visible
immediately, as in a synchronous model), so only blocks that actually *wait*
interleave. `sleep` honors the full requested duration — `sleep 333` sleeps
333 real seconds — as do `Promise.in`/`.at` timers; a worker still parked in
one when the program ends is woken and unwound at teardown, so a pending
timer never delays exit.

Under `RAKUPP_PARALLEL=1` sleep-sort still sorts — the workers now run on
independent threads outright rather than being handed off one at a time — but the
observable result is the same. See [The two modes](#the-two-modes-true-parallelism-default-and-the-gil).
