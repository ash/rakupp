# FAQ — the garbage collector

Not a tracing one. Raku++ has no tracing, generational or incremental
collector, no heap to size and no GC log. Lifetime is reference counting: a
value is freed the moment the last reference to it goes away. How the counting
itself works, in Raku terms, is [refcounting.md](refcounting.md).

Reference counting alone cannot free two things that point at each other, so
beside it runs a **backup cycle collector** that looks only for such groups and
frees them (since 2026-10, issue #151). It never pauses the program to walk the
heap: it looks at what was let go of recently, a bounded amount at a time. The
section [What about cycles?](#what-about-cycles) below has the details.

That answers most of the questions on this page in one line, but the
consequences are worth spelling out. The one that will eventually bite you:
**`DESTROY` is not a destructor you can schedule**.

Figures below were measured on macOS arm64 (M-series), 2026-08-30, rakupp
3.23.0 against Rakudo v2026.08 / MoarVM 2026.08, with `/usr/bin/time -l`.

## What refcounting buys

**A small floor.** Nothing is reserved for a heap, so the baseline is the
program, not the collector. The program here is a file containing one
statement, `say 1`, measured whole-process from launch to exit:

```bash
/usr/bin/time -l rakupp floor.raku          # and the same line for `rakudo`
```

| running `say 1` | peak footprint | max RSS |
|---|---:|---:|
| rakupp | 1.2 MB | 4.2 MB |
| Rakudo | 92.1 MB | 130.7 MB |

Both columns because the two metrics count differently: *peak memory footprint*
is what macOS charges the process, *maximum resident set size* also counts the
clean pages mapped in from the binary and the shared libraries. Each wobbles a
few MB between runs — Rakudo's RSS came out anywhere from 113 to 131 MB — and
the gap does not.

**No stop-the-world phase.** Freeing happens inline, at the drop, on the thread
that dropped it. There is no moment when the program is paused so a collector
can walk the heap. This program allocates steadily, times every iteration, and
counts the slow ones:

```raku
my ($over1, $over5, $worst) = 0, 0, 0;
my $t = now;
for ^200_000 -> $i {
    my %h = a => $i, b => $i * 2, c => "s$i";
    my @a = %h.values;
    my $dt = now - $t;
    if $i > 1_000 {                        # past warm-up
        $over1++ if $dt > 0.001;
        $over5++ if $dt > 0.005;
        $worst  = $dt if $dt > $worst;
    }
    $t = now;
}
printf "over 1 ms: %d, over 5 ms: %d, worst %.1f ms\n", $over1, $over5, $worst * 1000;
```

Six consecutive runs of it per engine, on an otherwise idle machine:

| run | rakupp — >1 ms / >5 ms / worst | Rakudo — >1 ms / >5 ms / worst |
|---|---|---|
| 1 | 0 / 0 / 0.1 ms | 22 / 3 / 7.4 ms |
| 2 | 0 / 0 / 0.1 ms | 20 / 0 / 4.9 ms |
| 3 | 0 / 0 / 0.0 ms | 24 / 0 / 4.9 ms |
| 4 | 0 / 0 / 0.1 ms | 24 / 1 / 5.9 ms |
| 5 | 0 / 0 / 0.1 ms | 21 / 3 / 7.7 ms |
| 6 | 0 / 0 / 0.1 ms | 18 / 2 / 7.9 ms |

The middle column is the one to read, and machine load is the reason to be
careful with the other two: run the same program while a compile is going and
rakupp's row moves too — one such run gave 30 / 8 / 30.1 ms. What does not move
is Rakudo's twenty-odd iterations over 1 ms in *every* run, idle or not, against
rakupp's zero. Those are the collector; rakupp's occasional millisecond, when it
appears at all, is the operating system. (An earlier sitting of this table put
Rakudo's over-5-ms column at twelve to eighteen; on this Rakudo it is nought to
three, so the over-1-ms column is the one to read.)

If you are writing something latency-sensitive — a request handler, an audio
callback, a game loop — that is the property you are buying.

## What it costs

**The free is on your clock.** A collector defers the work; refcounting bills
it at the drop, and a big drop is a big bill:

```raku
my $t0 = now;
my @big = (^2_000_000).map({ [$_, $_ * 2] }).Array;
my $built = now - $t0;
my $t1 = now;
@big = ();                                  # the last reference goes here
my $freed = now - $t1;
printf "build %.0f ms, free %.0f ms\n", $built * 1000, $freed * 1000;
```

| | build | free |
|---|---:|---:|
| rakupp | 1005 ms | **218 ms** |
| Rakudo | 2685 ms | **0 ms** |

(Three runs per engine, 2026-09-07; both columns repeated to within 25 ms.)
Rakudo's free column is zero because the work has not happened yet, not because
it was cheap.
Rakudo's 2 ms is not free memory, it is postponed work — but if a half-second
lands in the wrong place, move the drop somewhere it does not matter, or let
the structure die with the process.

Deep structures are safe to drop: a 500,000-node linked list released by
setting its head to `Nil` tears down without exhausting the stack.

## What about cycles?

Two objects pointing at each other keep each other's count above zero after
the program has let go of both. Reference counting cannot free them; the cycle
collector does:

```raku
class Node { has $.name is rw; has $.peer is rw; }
sub cyc() {
    my $a = Node.new(name => 'a');
    my $b = Node.new(name => 'b');
    $a.peer = $b;
    $b.peer = $a;      # ← the cycle
}
```

Peak footprint of `for ^N { cyc() }` (the block form of `for`, so the loop
itself allocates nothing), against the same routine with the last line
removed, and with the collector switched off (`RAKUPP_CYCLE=0`):

| iterations | no cycle | cycle | cycle, `RAKUPP_CYCLE=0` | Rakudo, cycle |
|---:|---:|---:|---:|---:|
| 100,000 | 2.1 MB | 3.2 MB | 94 MB | 146 MB |
| 400,000 | 2.0 MB | 3.2 MB | 371 MB | 146 MB |

(rakupp after 5.3.0, Rakudo 2026.09, macOS arm64, 2026-10-12, peak memory
footprint.) Without the collector each
cycle leaks about 0.9 KB, growing for as long as the program runs; with it,
memory stays flat.

The same holds for a container that contains itself:

| shape, 200,000 iterations | collector on | `RAKUPP_CYCLE=0` |
|---|---:|---:|
| `%h<self> = %h` | 2.8 MB | 115 MB |
| `@a[0] = @a` | 2.4 MB | 26 MB |

and for the cycles closures make: a block kept in a variable of the frame it
was made in, or an object that stores a callback mentioning that object. Those
are measured in [refcounting.md](refcounting.md#which-cycles-do-i-make-without-noticing).

### How it works

A group of objects becomes garbage at the moment its last outside reference
goes away. The collector keeps a list of **candidates**, the containers that
may just have lost such a reference:

- an object, every time a reference to it is released and the object stays
  alive;
- an array, hash, Pair or container cell, when a scope that held it exits;
- the variables of a scope a closure keeps alive, when the routine or loop
  that made the scope lets go of it.

The list holds no reference of its own: a candidate that is freed in the
ordinary way simply leaves it. When enough candidates have gathered, at the
end of a scope, the collector walks what they reference and counts how many
of each object's references come from inside the walk. An object referenced
more often than that is held from outside, and so is everything it reaches.
What is left is referenced only by itself, so it is garbage. Its `DESTROY`s
run first, with every link intact; then its contents are cleared, and
reference counting frees the rest.
[RUNTIME.md](../../internals/RUNTIME.md#memory-reference-counts-and-the-cycle-collector)
has the engineering, and the Internals book a chapter of its own
([Chapter 12](../../book/ch/12-containers.md#freeing-cycles)).

### What it costs

The cost is paid where candidates are made, mostly on objects. On the
benchmark kernels (instructions retired, 2026-10-12) twenty of twenty-two
moved by under 0.6%; the kernel that builds 200,000 objects and calls
methods on them costs 1.3–1.5% more. A class whose attributes are all typed
with a value type (`has Int $.x`, `has str $.s`, …) cannot be part of a
cycle, and its objects are not tracked at all; on the same kernel with typed
attributes the cost is 0.7%.

A collection walks at most a bounded amount from each candidate (1,024 new
objects, 16,384 values read), so a large structure that is still in use is
looked at in slices, never re-read whole. The price of that bound is the
first item in the next list.

### What it does not free

- **A cycle larger than one walk.** A ring of 30,000 objects is still leaked.
  It is reported nowhere, and nothing else slows down.
- **A cycle through a promise, a channel, a lazy sequence's generator or a
  native callback.** The collector cannot see inside those, so a cycle that
  passes through one is treated as held from outside.
- **A cycle built while worker threads run, until they stop.** A collection
  needs the program to be quiet: the main thread, with no `start` block or
  supply worker running. A server that always has a worker live collects only
  at the moments it has none.

When a cycle is still a problem, the structural fixes below free it at once
and need no collector at all.

## How do I avoid a cycle?

Usually you do not need to: the collector frees them. Avoid one when you want
the memory back at a precise moment, or when the cycle is one of the shapes
above that the collector cannot free. Raku has no weak reference in either
implementation, so the fix is structural, not a keyword:

- **Prefer one direction.** Parent → children, and look the parent up by id or
  index instead of storing a back-pointer.
- **Break the link when you are done with the graph**, in a `LEAVE` if the
  scope is what defines "done":

```raku
class Node { has $.name is rw; has $.peer is rw; }
sub build() {
    my $a = Node.new(name => 'a');
    my $b = Node.new(name => 'b');
    LEAVE { $a.peer = Nil; $b.peer = Nil }   # frees both, right here
    $a.peer = $b;
    $b.peer = $a;
    "$a.name() ↔ $b.name()";
}
say build();       # a ↔ b
```

- **Let the process end.** A short-lived script that leaks a bounded number of
  cycles is fine; the OS reclaims everything at exit.

## My long-running program grows. Is it a cycle?

Check three things, in this order, because the first two are far more common:

1. **Are you still holding it?** A cache, a `state` variable, an array you
   append to and never trim, a class attribute. Nothing here is a collector's
   job — a tracing GC would keep it alive too.

2. **Are you materialising a list?** Memory that is proportional to the *data*
   is not a leak. The statement-modifier form of `for` builds the list first,
   in both engines; the block form streams it:

   ```raku
   $s += $_ for ^300_000;      # rakupp 102 MB, Rakudo 180 MB
   for ^300_000 { $s += $_ }   # rakupp 1.6 MB
   ```

   The modifier's cost is per element of the list, not per iteration: three
   million iterations of `$s += $_ for ^10` stay at 1.6 MB.

3. **Then look for a cycle the collector cannot free** (see
   [What it does not free](#what-it-does-not-free)): a ring of tens of
   thousands of objects, a cycle through a promise or a channel, or a program
   that always has a worker thread running. `RAKUPP_CYCLE_STATS=1` prints what
   the collector did at exit: how many collections ran, and how many
   containers they freed.

### Measuring it

On macOS use **peak memory footprint**, not resident set size:

```bash
/usr/bin/time -l rakupp prog.raku 2>&1 | grep "peak memory footprint"
```

RSS will actively mislead you here. Leaked pages are never touched again, so
macOS compresses them and RSS *falls* — the cycle loop above wobbled between
231 MB and 24 MB resident while its footprint climbed linearly past 3 GB. On
Linux, `/usr/bin/time -v` and "Maximum resident set size" are honest.

Then bisect by shape: delete the suspected back-pointer, re-run, compare. A
leak that is a cycle changes slope; one that is a cache does not.

## When does `DESTROY` run?

Not at the drop. Raku does not promise timely destruction, and Raku++ does not
provide it: instances of classes that declare a `DESTROY` submethod are parked
in a registry, and a sweep runs the destructor of every entry the registry
alone still holds. Sweeps happen at three moments — when the program asks, when
enough instances have piled up since the last sweep, and at program end. A
cycle collection, which starts on its own at the end of a scope once enough
candidates are waiting, also runs `DESTROY` for the dropped instances it finds,
whether or not they are in a cycle.

Asking explicitly works the same in both engines:

```raku
class Res {
    has $.name;
    submethod DESTROY { say "DESTROY $!name" }
}
sub work { my $r = Res.new(name => 'a'); say "in work" }
work();
say "after work";
$*VM.request-garbage-collection;
say "after gc request";
```

```
in work
after work
DESTROY a
after gc request
```

Anything beyond "eventually, maybe" differs between the engines. Here is the
same program with one instance held in an outer variable:

```raku
class R { has $.n; submethod DESTROY { note "bye $!n" } }
my $keep;
sub make($n) { my $r = R.new(:$n); $keep = $r if $n == 2 }
make($_) for 1..3;
say "made";
$*VM.request-garbage-collection;
say "swept";
$keep = Nil;
$*VM.request-garbage-collection;
say "swept again";
say "end of program";
```

| rakupp | Rakudo |
|---|---|
| `made` | `made` |
| `bye 1` | |
| `bye 3` | `bye 3` |
| `swept` | `swept` |
| `bye 2` | |
| `swept again` | `swept again` |
| `end of program` | `end of program` |
| | `bye 1` |
| | `bye 2` |

Both are within spec, and neither is a contract. Two rules survive the
difference:

- **An object still reachable at exit is never destroyed.** `my $r = R.new` in
  the mainline prints nothing at all, on either engine.
- **A dropped one is destroyed at some point you did not choose.** At program
  end rakupp sweeps before the `END` blocks run; Rakudo, in the same program,
  may not run `DESTROY` at all.

So: **use `DESTROY` for a diagnostic, never for a release.** Close the file,
release the lock, return the connection in a `LEAVE` block, or with an explicit
call. That is the advice on the Rakudo side too.

An object in a cycle gets its `DESTROY` when the cycle collector finds the
cycle, before anything in the cycle is cleared, so it still sees its
attributes. If `DESTROY` stores `self` somewhere, the object stays alive, and
its `DESTROY` does not run a second time when it is dropped again.

## Does dropping a filehandle close it?

**No — and this is a difference from Rakudo.** Writes accumulate in the handle,
and an open write handle is registered so that its contents are flushed if the
program ends without a `close`. That registration is itself a reference, so
letting the variable go does not flush anything:

```raku
sub writer() {
    my $fh = open "out.txt", :w;
    $fh.print("hello");          # no close
}
writer();
say "size right after: ", "out.txt".IO.s;
```

| rakupp | Rakudo |
|---|---|
| `size right after: 0` | `size right after: 5` |

The bytes do arrive — at `.close`, or at program exit — but not when the
handle goes out of scope. Close it, and both engines agree:

```raku
sub writer() {
    my $fh = open "out.txt", :w;
    LEAVE { $fh.close }
    $fh.print("hello");
}
writer();
say "size right after: ", "out.txt".IO.s;      # 5, on both
```

Note also that opening write handles in a long-running loop grows memory even
when you close them: the exit-flush registry keeps every handle it was given
(~4 KB each, measured over 40,000 open/close pairs). For a program that opens
tens of thousands of files, that is worth knowing about.

## Can I force a collection?

`$*VM.request-garbage-collection` runs a cycle collection over whatever
candidates are waiting, then the pending-`DESTROY` sweep, as in the snippet
above.

`nqp::force_gc()` is not implemented; it works on Rakudo. There is no heap
size, nursery or generation count to tune. The environment variables are
switches for checking the collector, not tuning knobs:

| variable | effect |
|---|---|
| `RAKUPP_CYCLE=0` | no cycle collection; reference counting alone |
| `RAKUPP_CYCLE=dry` | find cyclic garbage and count it, free nothing |
| `RAKUPP_CYCLE_STATS=1` | at exit, print what the collector did |
| `RAKUPP_CYCLE_POISON=1` | instead of clearing a collected object, give it a class whose every method dies with `CYCLE-COLLECTED[…]` in the message; for checking that nothing collected is used again |
| `RAKUPP_CYCLE_TRIGGER=N` | collect whenever N candidates wait (default: 1,024, rising while collections find nothing); `1` collects at nearly every scope exit |

## Do threads change any of this?

There is no global pause. Reference counts are atomic, and a value is freed by
whichever thread drops the last reference. The cycle collector runs only on
the main thread while no worker thread is live; while workers run, candidates
wait, and the next quiet moment collects them. Your own shared data is a different question — two `start`
blocks writing the same array without a `Lock` is a data race here exactly as
it is under Rakudo. See [ASYNC.md](../ASYNC.md).

## Does `--exe` behave differently?

No. Compiled binaries link the same runtime and use the same reference
counting and the same cycle collector, so the floor, the cycles and the
`DESTROY` sweep all behave as they do in the interpreter; a compiled program
collects at the top of each loop iteration that calls user code. The same
holds for the WebAssembly build.

---

Deeper detail: [MEMORY.md](../MEMORY.md) for stacks, recursion depth and the
data-side guardrails; [RUNTIME.md](../../internals/RUNTIME.md) for what a
`Value` is and how its payloads are counted;
[CYCLE-GC-PLAN.md](../../dev/plans/CYCLE-GC-PLAN.md) for the collector's
design and measurements.

Back to the [FAQ index](README.md).
