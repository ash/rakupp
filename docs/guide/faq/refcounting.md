# FAQ — how reference counting works

Raku++ frees memory by counting references. Anything a value keeps on the
heap (a long string, an array's elements, an object's attributes, a closure's
variables) carries a count of how many places point at it. Each new place adds
one and each place that lets go takes one away. The drop that takes the count
to zero frees it, right there, before the next statement runs.

This page is about the mechanism: what is counted, which Raku operations
move a count, what is shared and what is copied, and the one thing counting
cannot see, which is a cycle. [garbage-collection.md](garbage-collection.md) is
the companion page about consequences: the memory floor, the absence of pauses,
the cost of a big free, and what `DESTROY` does and does not promise.

Rakudo counts nothing. MoarVM has a tracing, generational collector that finds
unreachable objects by walking from the roots. So the programs below print the
same values on both engines, because that is language semantics. Where they
differ, it is in *when* something is freed and how much memory it takes, and
the page shows both.

Memory figures are **peak memory footprint** from `/usr/bin/time -l`, macOS
arm64, 2026-09-26: rakupp 4.0.1 (main) against Rakudo v2026.08 / MoarVM
2026.08. Rakudo starts from a floor of about 92 MB, so its column cannot go
below that.

## What exactly is counted?

A variable, an array element, a hash value, an attribute, a parameter: each is
a **slot** holding one fixed-size `Value` (128 bytes). Small things live inside
the slot and are copied outright. Everything else lives on the heap behind a
counted pointer, and copying the slot copies the pointer and adds one to the
count.

| inside the slot, copied | on the heap, counted |
|---|---|
| an `Int` that fits in 64 bits, a `Num`, a `Bool` | a larger `Int`; a `Rat`'s numerator and denominator |
| a `Str` shorter than 23 bytes | a `Str` of 23 bytes or more |
| type objects, enum values | an `Array`/`List`'s elements, a `Hash`'s table (also `Set`, `Bag`, `Mix`) |
| | an object's attributes |
| | a routine or closure, and the frame it closes over |
| | a `Pair`'s value, a `Match` |

A few rarely used extras (a `Complex`'s imaginary part, a `Range`'s ends, a
shaped array's dimensions) sit in one side block per slot, which is shared
between copies and duplicated on the first write.

In the source, `p_` and `x_` in `src/Value.h` are the two counted pointers on
every `Value`. The counts come from `std::shared_ptr`, with no hand-written
bookkeeping around them.

## Which operations raise a count, and which lower it?

A count goes **up** whenever a second slot starts pointing at the same thing:
assigning an object to another variable, putting it in an array or hash,
passing it as an argument, returning it, or capturing the variable that holds it
in a closure.

```raku
class Point { has $.x is rw; has $.y is rw }
my $p = Point.new(x => 1, y => 2);
my $q = $p;                 # a second reference to the same object
my @list = $p, $p;          # two more
$q.x = 10;
say $p.x;                   # 10
say @list[1].x;             # 10
say $p === $q === @list[0]; # True
```

Four slots, one object, count four. Every slot sees the change because there
is only one object to change.

A count goes **down** when a slot stops pointing at it: the slot's scope ends
(`my` variables at the end of a block, parameters when the routine returns), the
slot is assigned something else or `Nil`, or the element is removed (`.shift`,
`.pop`, `.splice`, `@a[$i]:delete`).

**One exception today.** Deleting a key from a hash (`%h<k>:delete`,
`.DELETE-KEY`) takes the entry out of the hash, but the value stays alive until
the hash itself is emptied or freed:

```raku
my %h;
for ^300 { %h<k> = 'x' x 1_000_000; %h<k>:delete }
```

| | rakupp | Rakudo |
|---|---:|---:|
| `%h<k>:delete` | 294 MB | 97 MB |
| `%h<k> = Nil; %h<k>:delete` | 3 MB | |
| `@a[0]:delete` (an array, same loop) | 3 MB | |

If a long-lived hash churns through large values, assign `Nil` before deleting,
as in the second row. That releases the value at once.

## When exactly is memory freed?

When the count reaches zero, which happens inside the statement that dropped
the last reference and on the thread that dropped it. No collector runs later
to find it. Freeing then cascades: when an array goes, it lets go of each
element, and any element whose count reaches zero goes too.

So a loop that builds a large temporary on every pass never holds more than one
of them:

```raku
for ^1000 { my @a = ^100_000 }
```

| | peak |
|---|---:|
| rakupp | 32 MB |
| Rakudo | 137 MB |

Each `@a` is freed at the end of its own iteration, before the next one
allocates. A tracing collector frees the same arrays in batches, whenever it
next runs, so several of them are alive at the peak.

The flip side is that the work of freeing lands at the drop.
[garbage-collection.md](garbage-collection.md) measures what that costs for a
two-million-element structure.

## Does `my @b = @a` copy the array?

It copies the **slots**, not what is in them. `@b` is a new array with its own
list of slots, so adding to it leaves `@a` alone. But a slot that holds
something counted (another array, an object, a long string) is copied as a
pointer, so both arrays share that thing:

```raku
my @a = [1, 2], [3, 4];
my @b = @a;          # a new array ...
@b.push([5, 6]);     # ... so this does not touch @a
@b[0].push(99);      # but its elements are the same arrays
say @a;              # [[1 2 99] [3 4]]
say @b;              # [[1 2 99] [3 4] [5 6]]
```

This is ordinary Raku semantics (a shallow copy), and both engines print the
same. What refcounting adds is the price: copying costs 128 bytes per slot plus
one count increment per counted element. Binding with `:=` copies nothing. It
gives the same array a second name and raises its count by one:

```raku
my @a = ^1_000_000;
my @b = @a;          # or: my @b := @a
say @b.elems;        # 1000000
```

| | rakupp | Rakudo |
|---|---:|---:|
| `@a` alone | 252 MB | 241 MB |
| `my @b = @a` | 374 MB | 306 MB |
| `my @b := @a` | 252 MB | 247 MB |

## Is an array copied when I pass it to a routine?

No. The parameter is one more reference to the caller's array: its count goes
up for the duration of the call and back down when the routine returns. That
is why a routine can change the caller's array. `is copy` asks for a copy, which
works the same way as `my @b = @a`:

```raku
sub add-one(@x)          { @x.push(1) }
sub add-two(@x is copy)  { @x.push(2); @x.elems }
my @a = 7, 8;
add-one(@a);
say @a;              # [7 8 1]
say add-two(@a);     # 4
say @a;              # [7 8 1]
```

The size of the array does not matter: 100,000 calls of a routine that returns
`@x.elems` took 75 ms with a three-element array and 70 ms with a
million-element one.

## Why is copying a long string cheap?

A string of 23 bytes or more is stored once, in an immutable shared body.
Copying it copies a pointer and raises the count. Changing one copy (`~=`,
`s///`, `.=`) gives that copy a body of its own, and the other copies keep
sharing the original:

```raku
my $s = "x" x 10_000_000;           # one 10 MB string
my @many = $s xx 1000;              # a thousand references to it
@many[0] ~= "!";                    # this one gets its own copy
say @many[0].chars, " ", @many[1].chars;   # 10000001 10000000
```

| | rakupp | Rakudo |
|---|---:|---:|
| without the `~=` line | 13 MB | 96 MB |
| with it | 31 MB | 96 MB |

A thousand 10 MB strings take one string's worth of memory. Strings under 23
bytes are copied whole instead. 23 is where the C++ library on macOS stops
keeping a string inside its own object, and
[Chapter 9](../../book/ch/09-strings.md) of the Internals book explains the
choice.

## What keeps a closure's variables alive?

A routine's variables live in a **frame**. A closure holds a counted reference
to the frame it was created in, so the frame and everything in it live as long
as the closure does, after the routine has returned:

```raku
sub make-counter {
    my $n = 0;
    -> { ++$n }
}
my $c1 = make-counter();
my $c2 = make-counter();
$c1(); $c1();
say $c1();           # 3
say $c2();           # 1
```

Each call made a new frame with its own `$n`. When the last reference to a
closure goes, the frame's count drops too, and it is freed along with whatever
it held. With an object in the frame and a `DESTROY` to show it:

```raku
class Res { has $.name; submethod DESTROY { say "DESTROY $!name" } }
sub make-reader {
    my $r = Res.new(name => 'held by a closure');
    -> { $r.name }
}
my $read = make-reader();
say $read();
$*VM.request-garbage-collection;
say "closure still held";
$read = Nil;
$*VM.request-garbage-collection;
say "closure dropped";
```

| rakupp | Rakudo |
|---|---|
| `held by a closure` | `held by a closure` |
| `closure still held` | `closure still held` |
| `DESTROY held by a closure` | |
| `closure dropped` | `closure dropped` |

Rakudo printed no `DESTROY` line at all for this program, not even at exit.
That is within the language's rules, which promise nothing about when
`DESTROY` runs.

## Why doesn't `DESTROY` run the moment the count reaches zero?

Because the count never reaches zero while the object is waiting for it.

When a class declares a `DESTROY` submethod, construction also puts one
reference to each new instance in a registry. The program's own references come
and go as usual, but the registry's stays. A **sweep** looks for entries whose
count is exactly one (the registry alone), which means the program has dropped
every reference. For each one it runs `DESTROY`, child class first, then lets
go of the registry's reference, and the object is freed.

A sweep runs:

- when the program calls `$*VM.request-garbage-collection`;
- when registrations reach twice the number that survived the previous sweep
  (at least 1024), so a construct-and-drop loop cannot pile up instances;
- at program end.

Classes without a `DESTROY` never enter the registry, and their instances are
freed at the drop like everything else.

This design means `DESTROY` always runs at one of those points and sees a live
`self`, never inside whatever statement happened to drop the last reference.
It has one visible consequence: **a chain of objects that each have a
`DESTROY` is released one link per sweep.** A sweep only takes the entries
whose count is one when it looks. The next link down is still referenced by the
object in front of it, so it waits for the following sweep:

```raku
class R { has $.name; has $.next; submethod DESTROY { say "DESTROY $!name" } }
{ my $x = R.new(name => 'x', next => R.new(name => 'y', next => R.new(name => 'z'))) }
$*VM.request-garbage-collection; say "sweep 1";
$*VM.request-garbage-collection; say "sweep 2";
$*VM.request-garbage-collection; say "sweep 3";
```

| rakupp | Rakudo |
|---|---|
| `DESTROY x` | `DESTROY x` |
| `sweep 1` | `DESTROY y` |
| `DESTROY y` | `DESTROY z` |
| `sweep 2` | `sweep 1` |
| `DESTROY z` | `sweep 2` |
| `sweep 3` | `sweep 3` |

The memory itself is not held up the same way: only the objects that have a
`DESTROY` wait for sweeps.
[garbage-collection.md](garbage-collection.md) has the rest of the `DESTROY`
story, including why it should never be what closes a file.

## What happens with a reference cycle?

It is never freed. If two things point at each other, each keeps the other's
count at one or more after the program has let go of both. Nothing goes looking
for such pairs: there is no cycle collector.

```raku
class Node {
    has $.name;
    has $.peer is rw;
    submethod DESTROY { say "DESTROY $!name" }
}
{
    my $plain = Node.new(name => 'plain');
}
{
    my $a = Node.new(name => 'a');
    my $b = Node.new(name => 'b');
    $a.peer = $b;
    $b.peer = $a;
}
$*VM.request-garbage-collection;
say "after the sweep";
```

| rakupp | Rakudo |
|---|---|
| | `DESTROY b` |
| | `DESTROY a` |
| `DESTROY plain` | `DESTROY plain` |
| `after the sweep` | `after the sweep` |

On rakupp, `a` and `b` stay in memory until the process exits, and their
`DESTROY` never runs because their count never gets down to the registry's one.
Rakudo's collector finds them, because tracing from the roots never reaches
them.

The fix is to cut a link when you are done with the pair, in a `LEAVE` if the
end of a scope is what "done" means:

```raku
class Node {
    has $.name;
    has $.peer is rw;
    submethod DESTROY { say "DESTROY $!name" }
}
sub pair-up {
    my $a = Node.new(name => 'a');
    my $b = Node.new(name => 'b');
    $a.peer = $b;
    $b.peer = $a;
    LEAVE { $a.peer = Nil; $b.peer = Nil }
    "$a.name() ↔ $b.name()";
}
say pair-up();
$*VM.request-garbage-collection;
say "after the sweep";
```

| rakupp | Rakudo |
|---|---|
| `a ↔ b` | `a ↔ b` |
| `DESTROY a` | |
| `DESTROY b` | |
| `after the sweep` | `after the sweep` |

Raku has no weak reference, in either engine, so there is no keyword that does
this for you. Other ways to break the cycle (point in one direction only, look
the parent up by id instead of storing it) are in
[garbage-collection.md](garbage-collection.md).

## Which cycles do I make without noticing?

The obvious one is two objects that point at each other. The rest come from
closures, because a closure points at its frame:

- **An object that stores a callback which uses that same object.** In
  `$obj.on-change = -> { $obj.refresh }`, the object holds the block and the
  block's frame holds `$obj`.
- **A closure kept in a variable of its own frame.** `my $cb = -> { … }` puts
  the block in the frame and the block points back at the frame. So does a
  named `my sub` declared inside a routine, because the name is a variable in
  that frame too.

The second shape is one the language creates for you, so the interpreter tries
to break it: when a routine returns and the closure has not escaped, it cuts the
closure's link back to the frame (`breakSelfClosures` in `src/Interpreter.cpp`).
**In 4.0.0 and 4.0.1 that cut is skipped once the inner sub or block has been
called.** Each call then retains its frame and everything the frame holds.

Measured with 300 calls of a routine whose frame holds a 1 MB string:

```raku
sub outer($x) {
    my $big = 'x' x 1_000_000;
    my sub helper($y) { $y * 2 }
    helper($x) + 1;
}
for ^300 { outer($_) }
```

| shape of the routine | rakupp |
|---|---:|
| `my sub helper` declared, never called | 3 MB |
| `my sub helper` called (above) | 294 MB |
| `my $h = -> $y { … }; $h($x)` | 294 MB |
| `helper` declared at unit scope instead | 4 MB |
| a block passed straight to `.map`, not stored | 3 MB |
| `my $cb = -> { $big.chars }; $cb` returned | 294 MB |
| `-> { $big.chars }` returned directly | 4 MB |
| `my $cb = …; LEAVE $cb = Nil; $cb` returned | 4 MB |

Rakudo stays at its 96–98 MB floor for every row. The frame does not need
anything big in it to add up: an empty one is about 1.5 KB, so a million calls
of the routine above without `$big` reach 1.5 GB. Until the regression is
fixed, declare helpers at unit scope, or pass blocks where they are used
instead of storing them first.

The last three rows have nothing to do with the regression. A closure that
escapes while it is also stored in its own frame is a real cycle. Return the
block directly, or clear the variable in a `LEAVE` as in the last row.

## Can I see a reference count from Raku?

No. Nothing in the language exposes it, and Rakudo has no count to expose.
Two things can show you the effect of a count:

- **A `DESTROY` plus `$*VM.request-garbage-collection`**, as in the examples
  above. It tells you whether an object with a `DESTROY` has been let go.
- **Peak memory footprint.** On macOS that is
  `/usr/bin/time -l rakupp prog.raku 2>&1 | grep "peak memory footprint"`.
  Run the program at two sizes. A leak grows with the iteration count, and a
  structure you are merely holding grows with the data.
  [garbage-collection.md](garbage-collection.md) explains why resident set size
  misleads here.

## Is the counting thread-safe?

The counts are. Increments and decrements are atomic, and whichever thread
drops the last reference frees the value, with no global pause and no
collector thread. What you store is a different matter: two `start` blocks
writing the same array without a `Lock` is a data race here, as it is in
Rakudo. See [ASYNC.md](../ASYNC.md).

---

Further reading, from here inwards:

- [garbage-collection.md](garbage-collection.md): what refcounting buys and
  costs, measured; `DESTROY` timing on both engines; filehandles.
- [implementations.md](implementations.md): refcounting here, refcounting plus a
  cycle collector in mutsu, a tracing collector in Rakudo.
- [RUNTIME.md](../../internals/RUNTIME.md): what a `Value` is, and where
  assignment deliberately breaks the sharing.
- The Internals book: [Chapter 8](../../book/ch/08-value.md) on the `Value`
  layout, [Chapter 9](../../book/ch/09-strings.md) on shared string bodies,
  [Chapter 12](../../book/ch/12-containers.md) on containers, and
  [Chapter 14](../../book/ch/14-calls.md) on frames.

Back to the [FAQ index](README.md).
