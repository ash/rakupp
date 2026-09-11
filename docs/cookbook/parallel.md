# Cookbook — doing several things at once

Four shapes of concurrency, the numbers each one produced on this machine, and
the places where "it ran, so it works" is not the same as "it is correct".

Every program was run under Raku++ 3.27 on macOS 15 (arm64, 8 cores), and
where the comparison matters it was run under Rakudo 2026.08 as well. The box
was not idle, so treat the milliseconds as ratios rather than records.

## Waiting on four things instead of one

The clearest win is work that is not your program's to do: a request goes out,
and the thread that sent it sits there. [parallel/fetch-many.raku](parallel/fetch-many.raku)
asks for the same two-second response four times.

```raku
# One at a time: each get returns before the next one starts.
my @one-by-one = @urls.map({ HTTP::Tiny.new.get($_)<status> });

# start returns immediately with a Promise; the requests overlap, and the
# await collects them in the order they were started.
my @promises = @urls.map(-> $u { start HTTP::Tiny.new.get($u)<status> });
my @together = await @promises;
```

```output
one after another : 200 200 200 200 in 8020 ms
all at once       : 200 200 200 200 in 2021 ms
await in the loop : 200 200 200 200 in 8023 ms
```

That third line is the whole lesson. `await start …` inside the loop is the
sequential version with extra ceremony — it starts one promise and immediately
blocks on it. `start` everything first, `await` the list afterwards.

The requests need the server from the [JSON API recipe](http.md); start
`api-server.raku` in another terminal first.

## Splitting work between threads

For arithmetic rather than waiting, the same two lines apply.
[parallel/cpu.raku](parallel/cpu.raku) counts the primes below 400,000 in one
thread, and then in eight chunks:

```raku
my @promises = (^$parts).map(-> $i {
    start count-primes(max(2, $i * $size), min($limit, ($i + 1) * $size))
});
my $parallel = ([+] await @promises);
```

```output
primes below 400000
  one thread    : 33860 in 145 ms
  8 promises   : 33860 in 32 ms
  ratio         : 4.5x
  race(:degree(8)) : 33860 in 158 ms
```

4.5× out of 8 cores on a machine that was already busy, and both answers agree.
The last line is the same computation written as `race`, which is the idiom you
will see everywhere else — see the note under [things that bite](#four-things-that-bite)
for what it currently does here.

## Results in the order they arrive

`await` gives you everything at once, in the order you started it. When the
point is to act on each result as it lands — and to stop waiting at some
point — that is `react` and `whenever`:

```raku
react {
    for @jobs -> $p {
        whenever $p -> $result {
            say "  { ((now - $t0) * 1000).Int } ms  $result";
        }
    }
    whenever Promise.in($deadline) {
        say "  deadline: not waiting for the rest";
        done;
    }
}
```

[parallel/as-they-finish.raku](parallel/as-they-finish.raku) starts four jobs
of 0.9, 0.3, 0.6 and 2.0 seconds under a 1.5-second deadline:

```output
  300 ms  job 2 (took 0.3s)
  600 ms  job 3 (took 0.6s)
  902 ms  job 1 (took 0.9s)
  1500 ms  deadline: not waiting for the rest
finished: 3 of 4
still running: 1
```

`react` blocks until every `whenever` has finished or something calls `done`.
The deadline is just another source of events — a promise that keeps itself
after a while — which is why it needs no special support.

Note the last line: the fourth job is *still running*. `done` stops waiting; it
does not cancel anything.

## A queue and a pool of workers

When the jobs are not known up front, put them on a `Channel` and let a fixed
number of workers take them. [parallel/pool.raku](parallel/pool.raku):

```raku
my @running = (1 .. $workers).map(-> $id {
    start {
        for $work.list -> $job {
            sleep $each;                    # stand-in for the real work
            $out.send({ :$id, :$job });
        }
    }
});

$work.send($_) for 1 .. $jobs;
$work.close;                                # tells the workers when to stop
await @running;                             # every worker has finished
$out.close;
```

A `Channel` is the thread-safe part: `for $chan.list` blocks until there is
something to take, two workers can never take the same job, and closing the
channel is how the loop ends. Nothing else here locks anything.

```output
9 jobs of 0.3s across 3 workers: 1519 ms
  (one after another would be 2700 ms)
  worker 1: 1
  worker 2: 3 4 5
  worker 3: 2 6 7 8 9
```

Which worker gets which job is never guaranteed, but the split above is
lopsided enough to be worth stating plainly: on Raku++ 3.27 a `Channel` with
several waiting consumers favours one of them, so the pool finishes in 1519 ms
where an even split would take about 900 ms — and a second run of the same
program gave one worker eight of the nine jobs and took 2433 ms. The same
program under Rakudo 2026.08 splits the jobs 3/3/3 and finishes in 965 ms.

Until that evens out: when the work is known in advance, chunk it and start one
promise per chunk, as `cpu.raku` does. The pool shape is for work that arrives
while the program runs.

## Sharing state between threads

[parallel/shared.raku](parallel/shared.raku) has eight threads produce 16,000
numbers three different ways:

```raku
# 1. Everybody pushes to the same Array.
await (1 .. $threads).map(-> $n {
    start { @out.push($_) for ($n - 1) * $each ..^ $n * $each }
});

# 2. The same Array, with a Lock around the push.
$lock.protect({ @locked.push($_) });

# 3. No sharing at all: each thread builds its own list and returns it.
my @parts = await (1 .. $threads).map(-> $n {
    start { (($n - 1) * $each ..^ $n * $each).List }
});
```

Under Raku++:

```output
shared Array   : expected 16000, got 16000
Array + Lock   : expected 16000, got 16000
one list each  : expected 16000, got 16000
```

Under Rakudo, the same program, unchanged:

```output
shared Array   : expected 16000, got 15268
Array + Lock   : expected 16000, got 16000
one list each  : expected 16000, got 16000
```

732 numbers gone, silently. `Array` is not thread-safe by specification, so
whether a given engine loses elements is a question about that engine's
internals and this run's timing — not a licence. The first form is a bug on
both, and it is the kind that shows up in production and never in testing.

Of the two fixes, prefer the third: a thread that shares nothing needs no lock,
and 16,000 lock acquisitions are 16,000 chances to become the bottleneck. Reach
for `Lock` when the state genuinely is shared — a cache, a counter — and keep
what it protects small.

## Four things that bite

**`await` inside the loop.** Covered above, and worth repeating because it
looks concurrent and reads as concurrent: 8023 ms against 2021 ms for the same
four requests.

**`race` and `hyper` do not fan out on Raku++ 3.27.** They give the right
answer, in one thread, at serial speed: 158 ms against 145 ms for the loop
they replace, where `start`/`await` over the same chunks took 32 ms. The same
`race` under Rakudo 2026.08 took 51 ms against 220 ms serial. If a program
here needs parallelism today, express it with `start` and `await`.

**Counting threads will not tell you whether you are parallel.** `$*THREAD.id`
answers `1` inside every `start` block on Raku++ 3.27, including the ones that
demonstrably run at the same time. Rakudo answers the real ids. Measure the
wall clock instead — the ratio in `cpu.raku` is the honest test.

**An exception in a `start` block waits for you.** The promise is broken, not
thrown; nothing appears until something awaits it, and a promise that is never
awaited fails silently:

```raku
my $p = start { die "worker failed" };
my $r = try await $p;
say $! ?? "caught: { $!.message }" !! "no error";   # caught: worker failed
```

`await` on a list of promises rethrows the first failure, so a fan-out of eight
where one dies gives you one exception and seven discarded results. If each
job's failure should be handled on its own, catch inside the block and return
the failure as a value.

## What to reach for next

- `Promise.anyof` and `Promise.allof` build a promise out of promises, which is
  how a deadline is expressed without a `react` block — see
  [talking to a JSON API](http.md).
- `Supply` is the stream form of all this: `supply`/`emit` produces values over
  time, and `whenever` consumes them.
- `Lock::Async` is the lock to use inside a `react` block, where blocking a
  thread would block the loop that feeds it.
- [../guide/ASYNC.md](../guide/ASYNC.md) is the engine side of this page: what
  runs on which thread, what the runtime protects for you and what it does not,
  and the `RAKUPP_GIL=1` mode that serialises the lot.
