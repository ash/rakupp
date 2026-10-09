# Raku++ — Measuring a parallel speed-up

> **Note! This document is work in progress.**

How to show, with numbers you can defend, that a program runs faster *because*
of `start`. This is the measurement companion to
[ASYNC.md](ASYNC.md#the-two-modes-true-parallelism-default-and-the-gil), which
describes the two execution modes; here we only care about proving the
difference.

The two runnable programs referenced below live in
[`tools/bench/parallel/`](../../tools/bench/parallel).

All numbers on this page: **Apple M3 (4 performance + 4 efficiency cores),
Raku++ 5.2.1 (main), best wall-clock of 9 interleaved runs under `nice -n 10`,
measured 2026-10-07** (see [the method](#the-method) for why interleaved).
Absolute times will differ on your machine; the ratios are the point — and
they move with the engine, so the date matters as much as the machine.

---

## Three things have to be true before there is anything to measure

**1. You have to be in parallel mode — which is the default.** `start` blocks
and worker threads run on all cores with no setting at all. `RAKUPP_GIL=1`
selects the cooperative mode instead, where one thread runs Raku at a time:

```sh
rakupp myprogram.raku              # parallel (the default)
RAKUPP_GIL=1 rakupp myprogram.raku # one thread at a time
```

Under `RAKUPP_GIL=1`, a CPU-bound fan-out measures **0.98×–1.00×** at every
thread count — the thread setup you paid for and did not get back. No amount
of tuning changes that; the mode is the whole difference.

**2. The workload has to be able to scale.** Threads that spend their time
fighting over one shared word do not go four times faster on four cores. This
is not a Raku++ limitation, and it is the single most common reason a
"parallel" benchmark refuses to show a speed-up — see
[the shared-counter example](#example-2--when-the-workers-all-have-to-end-up-in-one-number).

**3. Both sides have to run the same way.** Raku++ compiles some loops into
kernels that work on machine integers directly, and a loop compiled on its own
runs that way only while no other thread is live. A loop that is compiled in the plain run and interpreted inside `start`
compares two different engines, and `start` can lose by a factor of ten — see
[example 3](#example-3--when-the-plain-loop-is-compiled).

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
  laptop a batched sweep has reported the same configuration at 0.241s early on
  and 0.313s a minute later — a 30% swing that had nothing to do with the code.
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
rakupp tools/bench/parallel/cpu-fanout.raku 4 300000 serial
rakupp tools/bench/parallel/cpu-fanout.raku 4 300000 parallel
```

**How to read these tables.** A row does N units of work, M=300 000
iterations each, so the work doubles from one row to the next. The plain loop
runs the N units one after another on one thread, and its time doubles with
the work. The `start` column runs them as N `start` blocks at once; a fan-out
that scales keeps that column *flat* while the work grows, and the speed-up is
the plain time divided by the `start` time.

Parallel mode (the default):

| N | work (iterations) | plain loop, one thread | N `start` blocks | speed-up |
|---|---|---|---|---|
| 1 | 300 000 | 0.061s | 0.072s | 0.85× |
| 2 | 600 000 | 0.123s | 0.074s | 1.66× |
| 4 | 1 200 000 | 0.250s | 0.077s | **3.25×** |
| 8 | 2 400 000 | 0.491s | 0.137s | 3.58× |

`RAKUPP_GIL=1`, the same runs — here the `start` column grows with the work,
because the N blocks take turns:

| N | work (iterations) | plain loop, one thread | N `start` blocks | speed-up |
|---|---|---|---|---|
| 1 | 300 000 | 0.062s | 0.062s | 1.00× |
| 2 | 600 000 | 0.123s | 0.125s | 0.98× |
| 4 | 1 200 000 | 0.246s | 0.252s | 0.98× |
| 8 | 2 400 000 | 0.492s | 0.501s | 0.98× |

Reading the tables:

- **The whole GIL table sits at 0.98×–1.00×.** Four threads, eight threads, it
  makes no difference: the mode is the difference, not the fan-out.
- **N=1 at 0.85× is the control, and it is not 1.00×.** One `start` block with
  nothing to contend with is *slower* than no `start` block. It is not thread
  setup — under `RAKUPP_GIL=1` the same single `start` runs at serial speed. It
  is a cost interpreted code pays on *every* thread while any worker is live,
  which is when the interpreter switches its cross-thread guards on. Run `work` on the
  main thread while an idle `start { sleep 3 }` is alive and it slows down just
  as much (M=2 000 000: 0.416s with no worker, 0.481s beside an idle one,
  0.483s on the worker itself). Every ratio below is quoted against that cost.
- **Identical `sum=3595657` at N=4 in every row**, serial and parallel, both
  modes — and the same holds for each N.
- **N=4 → 3.25× on 4 performance cores.** The missing 0.75 is the cost above
  plus thread setup and the `await` join.
- **N=8 → 3.58×, a tenth more than N=4.** This machine has four full-speed
  cores and four efficiency cores at roughly a third of the speed; a unit that
  lands on an efficiency core takes longer, and the batch is done only when its
  slowest unit is. Size the fan-out to the performance cores;
  `$*KERNEL.cpu-cores` reports the logical count (8 here), which is the wrong
  number to fan out to.
- **Neither side is compiled.** A loop kernel declines a variable declared with
  a native type, so this loop is interpreted on both sides, which is what a
  control should be. `RAKUPP_KERNEL_TRACE=1` says so:
  `kernel: loop at line 26 declined: variable $x (a declaration or a special form)`.
  The same loop over `my $x` is compiled, and that is example 3.

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
rakupp tools/bench/parallel/atomic-counter.raku 4 300000 contended parallel
rakupp tools/bench/parallel/atomic-counter.raku 4 300000 sharded   parallel
rakupp tools/bench/parallel/atomic-counter.raku 4 300000 counters  parallel
```

N=4, M=300 000:

| strategy | mode | plain loop, one thread | 4 `start` blocks | speed-up |
|---|---|---|---|---|
| contended | GIL | 0.373s | 0.378s | 0.99× |
| contended | parallel | 0.373s | 0.490s | **0.76×** |
| sharded | GIL | 0.174s | 0.179s | 0.97× |
| sharded | parallel | 0.176s | 0.053s | **3.32×** |
| counters | GIL | 0.243s | 0.246s | 0.99× |
| counters | parallel | 0.241s | 0.077s | 3.13× |

Every row `PASS`es its `total == N*M` check.

**The contended row is a loss, not a small win.** Four threads updating one
`atomicint` finish 31% *slower* than one thread doing all the work — the cache
line is passed between cores faster than any of them can make progress, and
each update also takes a stripe mutex
([`Interpreter::atomicStripe`](../../src/InterpreterOperators.cpp), a striped
`std::recursive_mutex` pool hashed by container address). Contention does not
merely fail to scale, it costs. Shard the counter, or keep one per worker.

The serial column carries a separate cost: 0.373s contended against 0.176s
sharded, with no thread anywhere. That 0.20s is what 1.2 million atomic
operations cost over the same number of native `int` increments — paid even
single-threaded, before parallelism is in the picture.

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

Both forms are exact in the default parallel mode — verified at N=8, M=400 000
for the array form and at M=400 000 for the four scalars, over repeated runs,
every slot landing on exactly M. The array form is the one to write, because it
takes N as a parameter instead of as a source edit.

`counters` at 3.13× and `sharded` at 3.32× are close enough that the honest
summary is: **once the shared write is out of the inner loop, how you spell the
per-worker tally barely matters.** Getting it out of the inner loop is the whole
move — M shared writes become N, or zero.

`sharded` is the faster of the two serially too (0.176s against 0.241s),
because a native `int` local beats an `Array` element access. Prefer it when
the accumulator is a plain number; prefer `counters` when each worker
accumulates something bigger than a counter — a list, a hash, a partial result
— since a per-worker slot holds anything and an atomic op does not.

### The extreme case: a counter loop with nothing else in it

The narrower the loop body, the larger the share of it that is synchronisation.
A bare `$a⚛++` with no other work per iteration — the shortest possible
atomic-counter loop — at N=4, M=500 000:

```sh
rakupp -e 'my atomicint $a = 0; my $t = now; await (^4).map: { start { $a⚛++ for ^500_000 } }; say "{ (now - $t).round(0.001) }s $a";'
rakupp -e 'my atomicint $a = 0; my $t = now; for ^4 { $a⚛++ for ^500_000 }; say "{ (now - $t).round(0.001) }s $a";'
```

| mode | plain loop, one thread | 4 `start` blocks | speed-up |
|---|---|---|---|
| GIL | 0.248s | 0.371s | 0.67× |
| parallel | 0.248s | 0.747s | **0.33×** |

0.33×, from a program that is nominally four-way parallel: fanning it out makes
it three times slower. A loop like this is a *correctness* test for
`atomicint` — a good one, and both lines above print the exact 2000000 — but it
is not a scaling benchmark. Do not expect one to demonstrate the other.

---

## Example 3 — when the plain loop is compiled

Raku++ runs some routines and loops as **kernels**: a compiled form, decided on
first entry, that works on machine integers, strings and numbers directly
instead of on Raku values. Which of the two kinds a piece of code gets decides
what happens to it under `start`:

- **A routine compiled whole** — `Int` arguments, its own locals, nothing read
  from outside — runs as a kernel on any thread, with or without workers live.
- **A loop compiled on its own** — inside a routine that is not compiled
  whole, typically because the loop reads a variable from outside — holds the
  variables it uses in its own frame and writes them back when it ends.
  Another thread reading or writing those variables meanwhile would see stale
  values, so such a loop runs as a kernel only while no other thread is live:
  not inside a `start` block, and not on the main thread while one runs. Once
  the workers are joined, its next entry is compiled again. Loops under
  `--cnp` follow the same rule.

Example 1's `work` with `my $s` and `my $x` in place of `my int` shows both,
depending only on where `$M` comes from. M=3 000 000:

```raku
sub work($seed) {                 # the loop reads $M from outside: a loop kernel
    my $s = 0;
    my $x = $seed;
    for ^$M {
        $x = ($x * 1103515245 + 12345) % 2147483647;
        $s = $s + ($x % 7);
    }
    $s
}

sub work($seed, $m) {             # everything passed in: a whole-routine kernel
    my $s = 0;
    my $x = $seed;
    for ^$m {
        $x = ($x * 1103515245 + 12345) % 2147483647;
        $s = $s + ($x % 7);
    }
    $s
}
```

As in example 1, a row does N units of M iterations, as a plain loop on one
thread or as N `start` blocks:

| N | `work` | plain loop, one thread | N `start` blocks | speed-up |
|---|---|---|---|---|
| 1 | reads `$M` from outside | 0.036s | 0.355s | 0.10× |
| 4 | reads `$M` from outside | 0.143s | 0.372s | **0.38×** |
| 1 | takes `$m` as a parameter | 0.036s | 0.036s | 1.00× |
| 4 | takes `$m` as a parameter | 0.143s | 0.038s | **3.76×** |

- **The first shape loses to its own plain loop.** The plain run is compiled;
  the four workers are interpreted. With `RAKUPP_NO_KERNELS=1`, which turns
  kernels off, its plain loop takes 0.280s at N=1 and 1.109s at N=4: the four
  workers are 2.98× faster than the interpreted loop and 2.6× slower than the
  compiled one. Both statements are true, and only the second is the one a
  user of the program sees.
- **The second shape scales and keeps the kernel.** 3.76× at N=4, and no cost
  at N=1: a compiled routine pays none of the guards that slow interpreted code
  while a worker is live.
- **The fix is in the source.** Pass what the loop needs as parameters, and
  keep what it computes in its own locals; the routine is then compiled whole
  and runs compiled on every thread.
- **Compile it before the fan-out.** A routine is compiled on its first call,
  and a worker that calls it while another thread is still compiling it runs
  that call interpreted, start to finish. At N=4 that happened in 1 run of 10
  here (0.44s instead of about 0.05s); the 3.76× above is the best run, as the
  method asks. One plain call before the `start`s — `work(0, 1);` — compiles
  it on the main thread, and 12 runs of 12 then landed between 0.048s and
  0.054s.

`RAKUPP_KERNEL_TRACE=1` prints each decision on stderr. The plain run of the
first shape says `kernel: loop at line … compiled (2 variable(s))`; the run
with `start` says only `kernel: work declined`, and the loop runs interpreted.
`--cnp=verbose` names the refusal outright:
`[cnp] loop at line … not entered while another thread is live — its variables are shared`.

---

## Without `start`: `hyper for`, `.hyper` and the hyper operators

Three forms spread work over the cores without a `start` in sight, and the
three conditions at the top of this page hold for them too:

- `hyper for` / `race for` hand the iterations out in batches of 64 to one
  worker per core less one;
- `.hyper` / `.race` do the same for the block of a `.map` or `.grep`
  (`:batch` and `:degree` set both);
- a hyper operator over long lists of plain Ints and Nums — `@a »*« @b`,
  `4 «*» @a`, `@a».sqrt` — runs as a native loop, split over the cores. It is
  not interpreted code at all, so the worker costs above do not apply to it.

Measure each against the serial form that gives the same list — `for`
against `hyper for`, `.map` against `.hyper.map` — with the method above.
Three things move those numbers more than the form does:

- **A short list loses.** Handing a batch to a worker costs tens of
  microseconds, so a list of a few hundred elements is faster serial.
- **`:degree` past the performance cores adds little.** The workers past them
  land on the efficiency cores, as the extra `start` blocks of example 1 do.
- **A block can keep to the calling thread.** One with a `state` variable or
  a loop phaser runs serially, giving the same answer at serial speed.
  `$*THREAD.id` inside the block shows where it ran; `RAKUPP_KERNEL_TRACE=1`
  prints a line for each hyper operator that ran natively, and on how many
  threads.

[faq/hyper.md](faq/hyper.md) has the forms side by side over a million Nums,
and a `:degree` table.

---

## Checklist

Before believing a parallel speed-up number:

- [ ] you are in parallel mode — the default; check that `RAKUPP_GIL=1` (or
      its synonym `RAKUPP_PARALLEL=0`) is *not* set. The mode is read once at
      startup
- [ ] both sides do the same total work, and print the same checksum
- [ ] both sides run the same way: `RAKUPP_KERNEL_TRACE=1` shows no loop
      compiled in the plain run that the threaded run has to interpret, and a
      compiled routine the workers share is called once before they start
- [ ] N=1 is in the table, so the cost of one worker is visible before any
      parallelism pays for it
- [ ] best of several runs, **interleaved** across configurations, and N is
      stated alongside the ratio
- [ ] the fan-out is sized to the performance cores, not `$*KERNEL.cpu-cores`
- [ ] there is no shared write inside the inner loop — each worker tallies into
      something it owns, and the combine happens after the `await`
- [ ] the binary is the one you think it is (`rakupp --version`)

## See also

- [ASYNC.md](ASYNC.md) — the concurrency model, the two modes, and what `Lock`,
  `Semaphore`, `Channel` and `Supply` do under each
- [faq/threads.md](faq/threads.md) — the short answers: is there a GIL, what
  scales, what the runtime protects for you
- [faq/hyper.md](faq/hyper.md) — the hyper operators, `hyper for` and `.hyper`:
  which to reach for, with measured numbers
- [../status/BENCHMARKS.md](../status/BENCHMARKS.md) — single-threaded Raku++
  against Rakudo
