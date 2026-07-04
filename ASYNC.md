# Raku++ — Concurrency & Async

A focused companion to [EXAMPLES.md](EXAMPLES.md) for the concurrency features.
**Every snippet below has been run on `rakupp` and produces the output shown**
(`# → …`). Run any of them with:

```sh
./build/rakupp -e 'CODE'
```

## The model

Raku++ runs concurrency on **real `std::thread`s coordinated by a global
interpreter lock (GIL)** — only one thread executes Raku at a time, so semantics
are correct but there is no CPU parallelism (which the test suite doesn't
require). Blocking operations (`sleep`, `await`) *release* the GIL, so tasks
genuinely interleave in time — enough for [sleep-sort](#concurrent-timing-sleep-sort)
to actually sort. Promises, Supplies, Channels, and `react` loops all behave as
specified; the work is *coordinated in time* rather than *parallel*.

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
