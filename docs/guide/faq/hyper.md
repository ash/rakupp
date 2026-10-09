# FAQ — hyper: the operators, the loop and the method

Short answers about the three things Raku calls *hyper* — the hyper
operators, the `hyper for` loop and the `.hyper` method — and their `race`
twins: which way the arrows point, which of them use more than one core, and
which to reach for. Threads in general are [threads.md](threads.md); measuring
a speed-up you can defend is [PARALLEL-SPEEDUP.md](../PARALLEL-SPEEDUP.md).

Measured 2026-10-09 on an Apple M3 (4 performance + 4 efficiency cores):
Raku++ 5.3.0 (main) and Rakudo v2026.09.

## What does "hyper" mean in Raku?

Three things share the word:

```raku
my @a = 1, 4, 9;
say @a »+« (10, 20, 30);        # a hyper operator: element by element
say @a».sqrt;                   # a hyper method call: the method on each element
say (hyper for @a { $_ * 2 });  # the hyper loop: iterations on worker threads
say @a.hyper.map(* * 2);        # the .hyper method: .map and .grep on worker threads
```

Both engines print:

```
[11 24 39]
[1 2 3]
(2 8 18)
(2 8 18)
```

The operators say *what* to compute for every element and leave the order
open. The loop and the method say *how*: in batches, on worker threads.
`race for` and `.race` are the same with the order of the answers left open
too. Raku++ gives them back in order anyway. `>>` and `<<` are the ASCII
spellings of `»` and `«`.

## Which way do the arrows point?

At a side that may be stretched — repeated until it is as long as the other
side. `»*«` stretches neither, so both sides must be the same length; `«*»`
stretches either:

```raku
my @a = 1, 2, 3;
say @a »*» 2;                      # [2 4 6]
say 2 «*« @a;                      # [2 4 6]
say @a «*» 2;                      # [2 4 6]
say (try @a »*« 2) // $!.^name;    # X::HyperOp::NonDWIM
say (try 2 »*» @a) // $!.^name;    # X::HyperOp::NonDWIM
```

The last line is the common mistake: the arrows point at the list and away
from the number, so the number is not stretched, and one element cannot meet
three. Both engines die there.

## Do the hyper operators use more than one core?

The language allows it — a hyper operator promises nothing about the order it
computes in. Rakudo computes one element at a time. Raku++ runs arithmetic on
plain numbers as a native loop, split over the cores:

- the operators `+ - * /` and the six comparisons, and the methods `.sqrt`,
  `.abs`, `.exp` and the trigonometric ones,
- over lists whose elements are all plain Int or Num values,
- on more than one core once a list holds 32,768 elements.

Over a million Nums, median of three runs:

| | Raku++ | Rakudo |
|---|---:|---:|
| `my @c = (4 «*» @a) «*» @b` | 28 ms | 866 ms |
| `my @c = (@a »+» 3)».sqrt` | 24 ms | 1149 ms |

Anything else takes the element-by-element path and gives the same answer
it always did: another operator, a Rat or a string among the elements, a
nested list, an operator of your own, an Int that overflows, a division by
zero. Code of your own in a hyper — `@objects».frobnicate`, `@a »my-op« @b`
— runs one element at a time, in order, on both engines. The language does
not promise that order, so do not lean on it.

## `.map`, `.hyper.map`, `hyper for` or a hyper operator?

The same work four ways, over a million Nums:

```raku
my @e = @a.map({ ($_ + 3).sqrt });
my @f = @a.hyper.map({ ($_ + 3).sqrt });
my @g = hyper for @a { ($_ + 3).sqrt };
my @d = (@a »+» 3)».sqrt;
```

| | Raku++ | Rakudo |
|---|---:|---:|
| `.map` | 322 ms | 483 ms |
| `.hyper.map` | 162 ms | 342–786 ms |
| `hyper for` | 90 ms | 400–743 ms |
| `(@a »+» 3)».sqrt` | 24 ms | 1149 ms |

All four give the same list. `.map` runs in order on one thread; that is what
it means, and it stays that way. When the work is arithmetic, the operators
are the fast form. When the block does real work per element, `hyper for` and
`.hyper.map` spread it over the cores. `hyper for` keeps one scope per worker,
where `.hyper.map` makes a call of the block for each element, which is most
of the difference between the two.

## Why isn't it eight times faster on eight cores?

`.hyper.map` over the same million Nums, by number of workers:

| `:degree` | Raku++ | against `.map` |
|---:|---:|---:|
| 1 | 337 ms | 1.0× |
| 2 | 236 ms | 1.4× |
| 4 | 134 ms | 2.4× |
| 7 | 155 ms | 2.1× |

Four of the M3's cores are efficiency cores, at about a third of the speed,
and the workers past four land on them. The rest is per-call work: each call
of the block builds and frees a frame of its own, and that does not get
cheaper with more threads. The native operator loops have a different limit,
memory: a million answers are 80 MB of values to write.

## When is `.hyper` slower?

On a short list. Handing work to the workers costs about 40 µs a call on
Raku++:

```raku
my @a = ^100;
for ^1000 { my @r = @a.map(* + 1) }         # Raku++ 10 ms, Rakudo 19 ms
for ^1000 { my @r = @a.hyper.map(* + 1) }   # Raku++ 50 ms, Rakudo 390 ms
```

A list of a few hundred elements, or less work per element than a call costs,
is `.map`'s job.

## What do `:batch` and `:degree` do?

`:batch` is how many elements a worker takes at a time (64 by default), and
`:degree` how many workers there are (the number of cores less one). Both
carry on through `.map` and `.grep`:

```raku
my $h = (^10).hyper(:batch(2), :degree(3));
say $h.map(* * 10);                                         # (0 10 20 30 40 50 60 70 80 90)
say $h.configuration.batch, ' ', $h.configuration.degree;   # 2 3
```

A larger batch means fewer hand-offs. A smaller one shares the work out more
evenly when some elements cost more than others.

## What stays on the calling thread?

On Raku++, `.hyper.map` and `.hyper.grep` keep to the calling thread — the
same answer, without the speed-up — for:

- a block with a `state` variable: there is one variable, and every worker
  would initialise and bump it at once;
- a block with a `FIRST`, `NEXT` or `LAST` phaser (Rakudo refuses those in a
  hyper: "Phasers in hyper/race not yet implemented");
- a block that `return`s;
- a matcher that is not a block — `.hyper.grep(/re/)`, `.hyper.grep(Int)` —
  and `.grep` with `:k`, `:kv` or `:p`.

`hyper for` keeps to the calling thread with a `state` variable or a loop
phaser in its body.

## What happens when the block dies?

```raku
my $e = do { try (1..10).hyper.map({ die "boom at $_" if $_ == 3; $_ }).eager; $! };
say $e.^name;     # X::AdHoc+{X::HyperRace::Died}
say $e.message;   # boom at 3
```

Both engines print that. The exception is the one the block threw, with
`X::HyperRace::Died` mixed in to say it came from a worker. When several
calls die, Raku++ reports the first in the order of the list.

## Can the block write to `$_`?

Yes, as in `.map`: `$_` is the element.

```raku
my @m = 1..5;
@m.hyper.map({ $_ *= 10 }).eager;
say @m;           # [10 20 30 40 50]
```

Each element belongs to one call, so those writes do not race each other.
Writing to anything the calls share — a counter, a hash — is yours to guard
([threads.md](threads.md#what-does-the-runtime-protect-and-what-is-mine-to-guard)).

## Is a regex in the block safe?

On Raku++ each call has its own `$/`. Over 5,000 strings, the numbers read
back through `$0`:

```raku
my @w = (^5000).map({ "x$_" });
my @n = @w.hyper(:batch(8)).map({ /x(\d+)/; +$0 });
say (@n Z!= ^5000).grep(*.so).elems, ' wrong';
```

Raku++ prints `0 wrong`. Rakudo 2026.09 printed between 11 and 21 wrong in
four runs: there, the calls on different workers share one `$/`. Taking the
match as a value is right on both:

```raku
my @m = @w.hyper(:batch(8)).map({ +.match(/x(\d+)/)[0] });   # 0 wrong on both
```

## How do I see what ran in parallel?

`$*THREAD.id` inside the block says where it ran. `RAKUPP_KERNEL_TRACE=1`
prints a line for each hyper operator that ran as a native loop, with the
number of threads, or that declined:

```
$ RAKUPP_KERNEL_TRACE=1 rakupp -e 'my @a = (^100000).map(*.Num); my @b = @a »*» 2; my @c = (1, 2, 9223372036854775807) »+» 1'
kernel: hyper infix:<*> over 100000 element(s) on 6 threads
kernel: hyper infix:<+> over 3 element(s) declined
```

The second declined because `9223372036854775807 + 1` needs a big integer,
which the element-by-element path makes. `RAKUPP_HYPER_THREADS=1` keeps the
native loops on one thread, `RAKUPP_NO_KERNELS=1` turns them off, and
`RAKUPP_GIL=1` runs everything, workers included, one thread at a time.
