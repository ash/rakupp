# Raku++ — Measuring a parallel speed-up

How to show, with numbers you can defend, that a program runs faster *because*
of `start`. This is the measurement companion to
[ASYNC.md](ASYNC.md#the-two-modes-true-parallelism-default-and-the-gil), which
describes the two execution modes; here we only care about proving the
difference.

The two runnable programs referenced below live in
[`tools/bench/parallel/`](../../tools/bench/parallel).

All numbers on this page: **Apple M3 (4 performance + 4 efficiency cores),
`build-arm64/rakupp` 3.25.0, best wall-clock of 15 interleaved runs, measured
2026-09-07** (see [the method](#the-method) for why interleaved). Absolute times
will differ on your machine; the ratios are the point — and they move with the
engine, so the date matters as much as the machine. Between the first sitting
and this one the *serial* baseline got about four times faster while the
threading did not, and every ratio here shrank accordingly.

---

## Two things have to be true before there is anything to measure

**1. You have to be in parallel mode — which since v3 is the default.**
Before v3, Raku++ ran Raku under a global interpreter lock unless
`RAKUPP_PARALLEL=1` was set; the tables below carry that spelling because the
numbers were measured then. Today the same runs need no env var at all, and
`RAKUPP_GIL=1` selects the old cooperative mode:

```sh
./build/rakupp myprogram.raku              # parallel (the default)
RAKUPP_GIL=1 ./build/rakupp myprogram.raku # the pre-v3 GIL mode
```

Under the GIL (`RAKUPP_GIL=1`), a CPU-bound fan-out measures at **0.98×–0.99×**
at every thread count — very slightly *slower* with `start` than without, which is the
thread setup you paid for and did not get back. No amount of tuning changes
that; the flag is the whole difference.

**2. The workload has to be able to scale.** Threads that spend their time
fighting over one shared word do not go four times faster on four cores. This
is not a Raku++ limitation, and it is the single most common reason a
"parallel" benchmark refuses to show a speed-up — see
[the shared-counter example](#example-2--when-the-workers-all-have-to-end-up-in-one-number).

## The method

The comparison is only meaningful if `start` is the *only* thing that changed:

- **Same total work on both sides.** N units of M iterations, run either as N
  `start` blocks or as a plain loop over the same N units. Not "N×M in threads"
  versus "M in one thread".
- **One variable.** Same binary, same arguments, same machine, same mode. Flip
  `serial` / `parallel` and nothing else.
- **Print a checksum and compare it.** If the two sides do not produce the
  identical answer, they are not doing the identical computation and the timing
  is worthless. Both example programs below print one.
- **Best of N runs, not one run, not the mean.** The minimum is the run least
  disturbed by everything else on the box.
- **Interleave the configurations, do not batch them.** Loop rounds on the
  outside and configurations on the inside, so every cell is measured at every
  point in the session. Running all the runs of one configuration back to back
  and then the next lets the machine's own state drift into the ratio: on this
  laptop a batched sweep reported the same configuration at 0.241s early on and
  0.313s a minute later — a 30% swing that had nothing to do with the code, and
  enough to move a 3.4× to a 2.8×.
- **Report the thread count.** A speed-up without an N next to it does not mean
  anything.

Time from inside the program with `now`, so startup and compile time do not
dilute the ratio. `/usr/bin/time` on the whole process is a fine cross-check but
it charges you for the interpreter boot on both sides.

---

## Example 1 — the contention-free control

[`tools/bench/parallel/cpu-fanout.raku`](../../tools/bench/parallel/cpu-fanout.raku).
Integer arithmetic in thread-local `int` natives: no allocation, no shared
mutable state, nothing between the workers and the cores.

```raku
my $N    = (@*ARGS[0] // 4).Int;          # units of work / worker threads
my $M    = (@*ARGS[1] // 300_000).Int;    # iterations per unit
my $mode = @*ARGS[2] // 'parallel';       # serial | parallel

sub work($seed) {
    my int $s = 0;
    my int $x = $seed;
    for ^$M {
        $x = ($x * 1103515245 + 12345) % 2147483647;
        $s = $s + ($x % 7);
    }
    $s
}

my $t0 = now;
my @r = $mode eq 'parallel'
    ?? await (^$N).map({ start work($_) })
    !! (^$N).map({ work($_) }).list;
my $dt = now - $t0;

say sprintf '%-8s N=%d M=%d  %.3fs  sum=%d', $mode, $N, $M, $dt, @r.sum;
```

```sh
./build/rakupp tools/bench/parallel/cpu-fanout.raku 4 300000 serial
./build/rakupp tools/bench/parallel/cpu-fanout.raku 4 300000 parallel
```

M=300 000, both modes:

| N | mode | plain loop | with `start` | speed-up |
|---|---|---|---|---|
| 1 | parallel (default) | 0.059s | 0.069s | 0.85× |
| 2 | parallel (default) | 0.114s | 0.071s | 1.62× |
| 4 | parallel (default) | 0.227s | 0.078s | **2.93×** |
| 8 | parallel (default) | 0.450s | 0.163s | 2.76× |
| 1 | GIL (`RAKUPP_GIL=1`) | 0.059s | 0.059s | 0.99× |
| 2 | GIL (`RAKUPP_GIL=1`) | 0.115s | 0.116s | 0.99× |
| 4 | GIL (`RAKUPP_GIL=1`) | 0.227s | 0.231s | 0.98× |
| 8 | GIL (`RAKUPP_GIL=1`) | 0.452s | 0.457s | 0.99× |

Reading the table:

- **The whole GIL half sits at 0.98×–0.99×.** Four threads, eight threads, it
  makes no difference: the mode is the difference, not the fan-out.
- **N=1 at 0.85× is the control, and it is not 1.00×.** One `start` block with
  nothing to contend with is *slower* than no `start` block. That is not thread
  setup: the gap grows with the work (at M=2,000,000 it is 0.374s against
  0.439s), and under `RAKUPP_GIL=1` the same single `start` runs at serial
  speed. A worker thread's own loop costs about 15% more than the main
  thread's, and every ratio below is quoted against that tax.
- **Identical `sum=3595657` in every row**, serial and parallel, both modes.
- **N=4 → 2.93× on 4 performance cores.** The missing 1.07 is the worker tax
  above plus thread setup and the `await` join.
- **N=8 → 2.76×, less than N=4.** This machine has four full-speed cores and
  four efficiency cores at roughly a third of the speed, so the extra four
  workers slow the batch down rather than speeding it up. Size the fan-out to
  the performance cores; `$*KERNEL.cpu-cores` reports the logical count (8
  here), which is the wrong number to fan out to.

---

## Example 2 — when the workers all have to end up in one number

[`tools/bench/parallel/atomic-counter.raku`](../../tools/bench/parallel/atomic-counter.raku).
Every worker counts its M iterations and the program wants one total at the end.
The per-iteration arithmetic is identical to example 1 and identical across all
three strategies below; the only variable is **where the increment lands**.

```raku
my atomicint $total = 0;              # used by contended + sharded
my @count = 0 xx $N;                  # used by counters: one slot per worker

my &unit = do given $strategy {
    when 'contended' {
        sub ($i) {
            my int $x = $i;
            for ^$M {
                $x = ($x * 1103515245 + 12345) % 2147483647;
                $total⚛++;                        # M writes to the shared counter
            }
        }
    }
    when 'sharded' {
        sub ($i) {
            my int $x = $i;
            my int $local = 0;
            for ^$M {
                $x = ($x * 1103515245 + 12345) % 2147483647;
                $local = $local + 1;              # thread-local native, uncontended
            }
            atomic-fetch-add($total, $local);     # one write to the shared counter
        }
    }
    when 'counters' {
        sub ($i) {
            my int $x = $i;
            for ^$M {
                $x = ($x * 1103515245 + 12345) % 2147483647;
                @count[$i]++;                     # this worker's own slot — no atomics
            }
        }
    }
};

my $t0 = now;
if $mode eq 'parallel' {
    await (^$N).map({ start unit($_) });
}
else {
    unit($_) for ^$N;
}
my $dt = now - $t0;

# The counters strategy adds its N per-worker tallies up here, after the join —
# single-threaded, so a plain sum is all it takes.
$total = @count.sum if $strategy eq 'counters';
```

All three compute the same total and the program checks it in both modes — a
speed-up that loses increments is not a speed-up.

```sh
./build/rakupp tools/bench/parallel/atomic-counter.raku 4 300000 contended parallel
./build/rakupp tools/bench/parallel/atomic-counter.raku 4 300000 sharded   parallel
./build/rakupp tools/bench/parallel/atomic-counter.raku 4 300000 counters  parallel
```

N=4, M=300 000:

| strategy | mode | plain loop | with `start` | speed-up |
|---|---|---|---|---|
| contended | GIL | 0.394s | 0.397s | 0.99× |
| contended | parallel | 0.393s | 0.500s | **0.79×** |
| sharded | GIL | 0.194s | 0.198s | 0.98× |
| sharded | parallel | 0.194s | 0.116s | 1.68× |
| counters | GIL | 0.284s | 0.288s | 0.98× |
| counters | parallel | 0.285s | 0.125s | **2.27×** |

Every row `PASS`es its `total == N*M` check.

**The contended row is now a loss, not a small win.** Four threads updating one
`atomicint` finish 27% *slower* than one thread doing all the work — the cache
line is passed between cores faster than any of them can make progress. That is
the strongest form of this page's point: contention does not merely fail to
scale, it costs. Shard the counter, or keep one per worker.

Two separate costs are visible, and it is worth keeping them apart:

- **The serial column**, where no thread exists at all. 1.167s contended against
  0.863s sharded: that 0.30s is what M atomic operations cost over M native
  `int` increments. You pay it even single-threaded, before parallelism is in
  the picture.
- **The parallel column.** Contended reaches 2.93×; both of the others clear
  3.4×. Under contention the workers serialise on one cache line and one stripe
  mutex ([`Interpreter::atomicStripe`](../../src/Interpreter.cpp), a striped
  `std::recursive_mutex` pool hashed by container address), so part of every
  iteration is spent in a queue.

### N counters, added up at the end

The `counters` strategy is worth looking at on its own, because it is the shape
to reach for first and it needs no atomics whatsoever:

```raku
my @count = 0 xx $N;                                   # one slot per worker
await (^$N).map: -> $i { start { @count[$i]++ for ^$M } };
say @count.sum;                                        # combine after the join
```

No two workers ever write the same variable, so there is nothing to
synchronise and a plain non-atomic `++` is correct. The sum happens after
`await`, back on one thread. With four literal counters it is the same idea
spelled out:

```raku
my ($x1, $x2, $x3, $x4) = 0, 0, 0, 0;
await (start { $x1++ for ^$M }, start { $x2++ for ^$M },
       start { $x3++ for ^$M }, start { $x4++ for ^$M });
say $x1 + $x2 + $x3 + $x4;
```

Both forms are exact under `RAKUPP_PARALLEL=1` — verified at N=8, M=400 000
across repeated runs, every slot landing on exactly M. The array form is the one
to write, because it takes N as a parameter instead of as a source edit.

At 3.51× this edges out `sharded`'s 3.40×, and the two are close enough that on
another run they swap. Which is the honest summary: **once the shared write is
out of the inner loop, how you spell the per-worker tally barely matters.**
Getting it out of the inner loop is the whole move — M shared writes become N,
or zero.

`sharded` is the faster of the two *serially* (0.863s against 0.948s), because a
native `int` local beats an `Array` element access. Prefer it when the
accumulator is a plain number; prefer `counters` when each worker accumulates
something bigger than a counter — a list, a hash, a partial result — since a
per-worker slot holds anything and an atomic op does not.

### The extreme case: a counter loop with nothing else in it

The narrower the loop body, the larger the share of it that is synchronisation.
A bare `$a⚛++` with no other work per iteration — the shortest possible
atomic-counter loop — at N=4, M=500 000:

```sh
./build/rakupp -e 'my atomicint $a = 0; my $t = now; await (^4).map: { start { $a⚛++ for ^500_000 } }; say "{ (now - $t).round(0.001) }s $a";'
./build/rakupp -e 'my atomicint $a = 0; my $t = now; for ^4 { $a⚛++ for ^500_000 }; say "{ (now - $t).round(0.001) }s $a";'
```

| mode | plain loop | with `start` | speed-up |
|---|---|---|---|
| GIL | 0.340s | 0.501s | 0.68× |
| parallel | 0.337s | 0.802s | **0.42×** |

0.42×, from a program that is nominally four-way parallel: fanning it out makes
it two and a half times slower. `sys` time triples, which is the tell — the
process is in the kernel arbitrating, not computing. A loop like this is a *correctness* test for `atomicint` — a good
one, and both lines above print the exact 2000000 — but it is not a scaling
benchmark. Do not expect one to demonstrate the other.

---

## Checklist

Before believing a parallel speed-up number:

- [ ] you are in parallel mode — the default; check that `RAKUPP_GIL=1` is
      *not* set, and note `RAKUPP_PARALLEL=1` is not a switch (only
      `RAKUPP_PARALLEL=0` is read, as a synonym for the GIL). The mode is read
      once at startup
- [ ] both sides do the same total work, and print the same checksum
- [ ] N=1 measures 1.00× — the harness is not measuring itself
- [ ] best of several runs, **interleaved** across configurations, and N is
      stated alongside the ratio
- [ ] the fan-out is sized to the performance cores, not `$*KERNEL.cpu-cores`
- [ ] there is no shared write inside the inner loop — each worker tallies into
      something it owns, and the combine happens after the `await`
- [ ] the binary is current (`cmake --build build -j8`) — a stale `build/rakupp`
      silently benchmarks whatever the tree used to be

## See also

- [ASYNC.md](ASYNC.md) — the concurrency model, the two modes, and what `Lock`,
  `Semaphore`, `Channel` and `Supply` do under each
- [../status/BENCHMARKS.md](../status/BENCHMARKS.md) — single-threaded Raku++
  against Rakudo
