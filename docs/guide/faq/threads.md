# FAQ — threads, and is there a GIL?

Short answers about running Raku on more than one core: whether a global
interpreter lock stands in the way, how to tell that threads really ran at
once, why a `start` sometimes makes a program slower, and what is yours to
guard. The engine side is [ASYNC.md](../ASYNC.md); how to measure a speed-up
you can defend is [PARALLEL-SPEEDUP.md](../PARALLEL-SPEEDUP.md).

Measured 2026-10-07 on an Apple M3 (4 performance + 4 efficiency cores):
Raku++ 5.2.1 (main) and Rakudo v2026.09.

## Is there a global interpreter lock?

Not by default. `start` blocks and the iterations of `hyper for` and
`race for` run Raku on separate cores at the same time. Rakudo works the same
way, and has no lock to turn on.

Raku++ has one switch, read once at startup: `RAKUPP_GIL=1` runs the program
under a global interpreter lock, so one thread runs Raku at a time.
(`RAKUPP_PARALLEL=0` means the same.) Nothing in the program changes; the same
source runs in either mode.

```raku
sub work($n) {
    my int $s = 0;
    for ^2_000_000 -> $i { $s = $s + $i % 7 }
    $s + $n
}
my $t0 = now;
my @plain = (^4).map({ work($_) });
my $plain = now - $t0;
$t0 = now;
my @threads = await (^4).map({ start work($_) });
my $threads = now - $t0;
say @plain eqv @threads;
say sprintf '%.3fs plain, %.3fs with start: %.2f×', $plain, $threads, $plain / $threads;
```

Both engines print `True` and a speed-up; over five runs each:

```
Raku++   0.827s plain, 0.368s with start: 2.25×    (2.20× to 2.60×)
Rakudo   0.692s plain, 0.350s with start: 1.98×    (1.98× to 2.14×)
```

Under `RAKUPP_GIL=1` the same program measures 0.98× to 1.00×: the threads
exist, and take turns.

## How do I know my threads really ran at the same time?

Time it. The same work as a plain loop and as `start` blocks, the same answer
from both, and the wall clock between them, as in the program above. That is
the only test that answers the question.

`$*THREAD.id` says *where* a block ran, not whether it overlapped another one:

```raku
my $main = $*THREAD.id;
my @ids = await (^4).map: { start { sleep 0.1; $*THREAD.id } };
say @ids.grep(* != $main).elems, ' of 4 ran off the main thread';
say @ids.unique.elems, ' distinct threads';
```

```
4 of 4 ran off the main thread
4 distinct threads
```

Both engines print that, and Raku++ prints the same under `RAKUPP_GIL=1`,
where the four took turns.

## Why did `start` make my program slower?

Four reasons cover nearly every case, all measured in
[PARALLEL-SPEEDUP.md](../PARALLEL-SPEEDUP.md):

- **The workers share a write.** Four threads incrementing one `atomicint`
  finish at 0.76× — slower than one thread doing all of it. Give each worker
  its own tally and add them up after the `await`: 3.13× for the same work.
- **More workers than fast cores.** On this 4+4 machine, eight workers reach
  3.58× against four workers' 3.25×. `$*KERNEL.cpu-cores` counts every core
  (8 here); fan out to the performance cores.
- **The plain loop is compiled and the threaded one is not.** Raku++ compiles
  some loops into kernels that work on machine integers directly, and a loop
  compiled on its own runs that way only while no other thread is live. Its
  plain run can then beat four threads: 0.143s against 0.372s. A routine that
  takes its inputs as parameters and keeps its work in its own locals is
  compiled whole, and runs compiled on every thread: 3.76× on the same work.
  `RAKUPP_KERNEL_TRACE=1` shows which loops and routines were compiled.
- **One worker costs something on its own.** While any worker is live,
  interpreted code runs about 15% slower on every thread, so a single `start`
  measures 0.85×. It takes two workers to come out ahead.

## What does the runtime protect, and what is mine to guard?

The runtime protects its own state: every thread has its own scopes and
dynamic variables, the symbol tables are fixed once threads start, and
reference counts are atomic. Your variables are yours. Two threads writing the
same one without a lock is a data race, on both engines:

```raku
my $n = 0;
await (^4).map: { start { $n++ for ^100_000 } };
say $n;
```

This should print 400000, and does not:

```
Raku++   141245, 163593, 161042    (three runs)
Rakudo   248537, 260403, 263266
```

A `Lock` makes it exact, and so does an `atomicint` with `⚛++`; both print
400000 every time, on both engines:

```raku
my $lock = Lock.new;
my $n = 0;
await (^4).map: { start { for ^100_000 { $lock.protect: { $n++ } } } };
say $n;                                   # 400000
```

```raku
my atomicint $n = 0;
await (^4).map: { start { $n⚛++ for ^100_000 } };
say $n;                                   # 400000
```

Both are correct, and both send every increment through one shared point,
which is where the time goes: four threads on one `atomicint` measure 0.76×
against one thread doing all of it. When the workers only need to be added up
at the end, one counter per worker and a sum after the `await` needs no
synchronisation at all.

## Do `hyper` and `race` use more than one core?

The statement forms do; the methods do not on Raku++:

```raku
my $main = $*THREAD.id;
my @a = hyper for ^1000 { $*THREAD.id };
my @b = (^1000).hyper.map({ $*THREAD.id });
say 'hyper for: ', @a.grep(* != $main).elems, ' of 1000 off the main thread';
say '.hyper:    ', @b.grep(* != $main).elems, ' of 1000 off the main thread';
```

```
Raku++                                      Rakudo
hyper for: 1000 of 1000 off the main thread  hyper for: 1000 of 1000 off the main thread
.hyper:    0 of 1000 off the main thread     .hyper:    1000 of 1000 off the main thread
```

`race for` and `.race` behave the same way on each engine. On Raku++,
`.hyper` and `.race` give the right answer serially. To fan a loop out, write
`hyper for` / `race for`, or `start` and `await`.

## When is `RAKUPP_GIL=1` worth setting?

**To tell whether a wrong answer is a race.** The unguarded counter above
printed 400000 in five runs of five under `RAKUPP_GIL=1`. A result that is
wrong by default and right under the lock means two threads are writing the
same thing.

**Not for speed.** A CPU-bound fan-out gains nothing under it, and work that
mostly waits — on `sleep`, a child process, a socket — overlaps in both modes,
because a thread that waits lets the others run.
