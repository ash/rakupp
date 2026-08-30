# FAQ — the garbage collector

There isn't one. Raku++ has no tracing, generational or incremental collector,
no heap to size, no GC knobs and no GC log. Lifetime is `shared_ptr` reference
counting: a value is freed the moment the last reference to it goes away.

That answers most of the questions on this page in one line, but the
consequences are worth spelling out, because two of them will eventually bite
you: **reference cycles are never reclaimed**, and **`DESTROY` still is not a
destructor you can schedule**.

Figures below were measured on macOS arm64 (M-series), 2026-08-30, rakupp
3.23.0 against Rakudo v2026.08 / MoarVM 2026.08, with `/usr/bin/time -l`.

## What refcounting buys

**A small floor.** Nothing is reserved for a heap, so the baseline is the
program, not the collector:

| `say 1` | peak footprint | max RSS |
|---|---:|---:|
| rakupp | 1.5 MB | 4.2 MB |
| Rakudo | 91.1 MB | 113.4 MB |

**No stop-the-world phase.** Freeing happens inline, at the drop, on the thread
that dropped it. There is no moment when the program is paused so a collector
can walk the heap. The same allocation-heavy loop (200,000 iterations, each
building a hash and an array from it), counting iterations that took longer
than a millisecond:

| run | rakupp: >1 ms / >5 ms / worst | Rakudo: >1 ms / >5 ms / worst |
|---|---|---|
| 1 | 0 / 0 / 0.9 ms | 23 / 14 / 13.4 ms |
| 2 | 6 / 0 / 1.7 ms | 28 / 17 / 45.1 ms |
| 3 | 0 / 0 / 0.4 ms | 23 / 15 / 129.0 ms |

If you are writing something latency-sensitive — a request handler, an audio
callback, a game loop — that is the property you are buying.

## What it costs

**The free is on your clock.** A collector defers the work; refcounting bills
it at the drop, and a big drop is a big bill. Building a 2,000,000-element
array of two-element arrays, then releasing it with `@big = ()`:

| | build | free |
|---|---:|---:|
| rakupp | 4514 ms | **544 ms** |
| Rakudo | 16526 ms | **2 ms** |

Rakudo's 2 ms is not free memory, it is postponed work — but if a half-second
lands in the wrong place, move the drop somewhere it does not matter, or let
the structure die with the process.

Deep structures are safe to drop: a 500,000-node linked list released by
setting its head to `Nil` tears down without exhausting the stack.

**Cycles are never reclaimed.** This is the real limitation. Two objects
pointing at each other keep each other's count above zero, and nothing ever
goes looking for them:

```raku
class Node { has $.name is rw; has $.peer is rw; }
sub cyc() {
    my $a = Node.new(name => 'a');
    my $b = Node.new(name => 'b');
    $a.peer = $b;
    $b.peer = $a;      # ← the cycle: neither is ever freed
}
```

Calling that in a loop, against the same routine with the last line removed:

| iterations | no cycle | cycle | Rakudo, cycle |
|---:|---:|---:|---:|
| 100,000 | 2 MB | 857 MB | 164 MB |
| 400,000 | 2 MB | 3423 MB | 189 MB |

About 8.6 KB per cycle, growing forever. Rakudo's collector finds them, which
is exactly what a collector is for.

The same applies to a container that contains itself:

| shape | leaked per iteration |
|---|---:|
| `%h<self> = %h` | ~4 KB |
| `@a[0] = @a` | ~180 B |
| `$x = [$x]` (not a cycle — the old value is captured, not referenced back) | nothing |

**One cycle is broken for you.** A named sub nested in another routine closes
over the enclosing frame and is stored back into it — a cycle the language
makes, not you. When a frame exits without that closure having escaped, the
interpreter drops the back-edge (`breakSelfClosures`, src/Interpreter.cpp), so
this stays flat at 1.6 MB at any iteration count:

```raku
sub outer($x) {
    my sub helper($y) { $y * 2 }    # would otherwise retain outer's frame
    helper($x) + 1;
}
```

That is the only cycle handled automatically. Cycles in *your* data are yours.

## How do I avoid a cycle?

Raku has no weak reference in either implementation, so the fix is structural,
not a keyword:

- **Prefer one direction.** Parent → children, and look the parent up by id or
  index instead of storing a back-pointer.
- **Break the link when you are done with the graph**, in a `LEAVE` if the
  scope is what defines "done":

```raku
class Node { has $.name is rw; has $.peer is rw; }
sub build() {
    my $a = Node.new(name => 'a');
    my $b = Node.new(name => 'b');
    LEAVE { $a.peer = Nil; $b.peer = Nil }   # frees both
    $a.peer = $b;
    $b.peer = $a;
    "$a.name() ↔ $b.name()";
}
say build();       # a ↔ b
```

  In a 400,000-iteration loop that routine stays flat at 1.7 MB, against the
  3423 MB it takes with the links left in place.

- **Let the process end.** A short-lived script that leaks a bounded number of
  cycles is fine; the OS reclaims everything at exit. It is long-lived
  processes — servers, daemons, watchers — where a per-request cycle matters.

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

3. **Then look for a cycle** — a back-pointer, a parent link, a callback an
   object stores that closes over that same object.

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
enough instances have piled up since the last sweep, and at program end.

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
call. That is the advice on the Rakudo side too, and it is the same advice here
for a stronger reason — nothing looks for cycles, so a leaked object never even
reaches the registry sweep.

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

`$*VM.request-garbage-collection` runs the pending-`DESTROY` sweep, as in the
snippet above. It does not collect cycles — there is nothing that does.

`nqp::force_gc()` is not implemented; it works on Rakudo. There are no other
knobs: no heap size, no nursery, no generation count, no GC environment
variables, because there is nothing to tune.

## Do threads change any of this?

No collector means no global pause and no collector-side synchronisation.
Reference counts are atomic, and a value is freed by whichever thread drops the
last reference. Your own shared data is a different question — two `start`
blocks writing the same array without a `Lock` is a data race here exactly as
it is under Rakudo. See [ASYNC.md](../ASYNC.md).

## Does `--exe` behave differently?

No. Compiled binaries link the same runtime and use the same reference
counting, so the floor, the cycles and the `DESTROY` sweep all behave as they
do in the interpreter. The same holds for the WebAssembly build.

---

Deeper detail: [MEMORY.md](../MEMORY.md) for stacks, recursion depth and the
data-side guardrails; [RUNTIME.md](../../internals/RUNTIME.md) for what a
`Value` is and why its payloads are behind `shared_ptr`.

Back to the [FAQ index](README.md).
