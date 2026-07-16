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

## The two modes: GIL (default) and true parallelism

There is exactly one knob — the **`RAKUPP_PARALLEL` environment variable** — and it
is read once at startup. You don't change anything in your Raku code; the same
program runs under either mode.

| | **GIL mode** (default) | **Parallel mode** (`RAKUPP_PARALLEL=1`) |
|---|---|---|
| How to select | *(nothing — this is the default)* | set the env var |
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
#   GIL mode: ~5.6 s   |   RAKUPP_PARALLEL=1: ~2.2 s  (2.5× — 4-perf-core Mac)
```

`start EXPR` thunks `EXPR` and runs it *on the worker* (it is not evaluated eagerly
on the spawning thread), so `start work($n)` parallelises just like `start { work($n) }`.

**Match the fan-out to the physical *performance* cores.** The speed-up tops out
at the number of full-speed cores, not the logical-CPU count. On the 4P+4E Apple
Silicon machine above, four `start` blocks scale ~2.5×; spawning eight does *not*
reach ~5× — the extra work spills onto the efficiency cores (~⅓ the speed) and
GIL-handoff contention grows, so eight threads land around 1.4×, *slower* per
task than four. `$*KERNEL.cpu-cores` reports the logical count; size the fan-out
to the performance cores you actually have.

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
scales with the number of **full-speed cores** — roughly 2.5× on a 4-performance-core
machine, and correspondingly more on a box with more true cores. Two caveats
decide whether you see it: keep the fan-out at or below the performance-core
count (oversubscribing onto efficiency cores or hyperthreads gives diminishing,
then negative, returns), and make sure the parallel unit is a real `start` thunk
rather than a single serialised bottleneck. Work dominated by external processes
or I/O already overlaps in the default GIL mode (the waits release the lock), so
parallel mode adds less there.

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
say Promise.allof(Promise.kept(1), Promise.kept(2)).status;   # → Kept
say Promise.anyof(Promise.new, Promise.kept(1)).status;       # → Kept

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

await Promise.allof(@jobs.grep(*.status eq 'Kept'));

say "progress:";
.say for @log.map({ "  $_" });

my @collected;
loop {
    my $v = $results.poll;
    last if $v === Nil;
    @collected.push($v);
}
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
interleave. `sleep` is capped (so a runaway `sleep 10000` can't wedge things),
which preserves small relative delays but not real wall-clock durations.

Under `RAKUPP_PARALLEL=1` sleep-sort still sorts — the workers now run on
independent threads outright rather than being handed off one at a time — but the
observable result is the same. See [The two modes](#the-two-modes-gil-default-and-true-parallelism).
