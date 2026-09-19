# `--jit` and `--cnp` — compiling while the program runs

> **Both flags are work in progress, and both spellings are provisional.**
> They are one feature with two backends behind it, kept apart for now so each
> can be measured against the interpreter on its own. The expectation is that
> **`--cnp` becomes the default and `--jit` is removed** — at which point tiering
> up stops being something you ask for. Until then, treat the flag names, the
> spec words and the defaults as unsettled: use them to measure and to
> experiment, not as something a script or a build depends on.

Off by default. With either flag, a running program compiles its own hot loops to
native code and starts using them partway through the loop, without you asking
for a build and without a separate binary. `--jit` does it with a C++ compiler
([below](#what-happens)); `--cnp` does it with machine-code snippets already
inside the binary, so it needs no compiler at all
([below](#--cnp-the-same-thing-without-a-compiler)).

```bash
rakupp --jit prog.raku
```

This is not a new compiler. It is the code generator `--exe` already uses,
pointed at one loop instead of a whole program.

## What happens

1. **Counting.** Every `while`, `until`, C-style `loop`, and `for` over a
   **Range of integers** counts its iterations. Nothing else does — a `for` over
   anything else, a `.map` and a `repeat` have no counter, so they are never
   candidates however hot they get. How much that leaves out is measured in
   [How much of a program it reaches](#how-much-of-a-program-it-reaches) below.
   The default threshold is 1000.
2. **Checking.** A loop that goes hot is checked against a whitelist (below).
   A loop that fails it is marked and never looked at again.
3. **Emitting.** The loop becomes a small C++ function, the same one `rakupp
   --cpp -O` would print for it, with the variables it did not declare bound as
   references into the live frame.
4. **Compiling.** A C++ compiler runs in the background, on another core. The
   interpreter keeps iterating meanwhile and never waits for it.
5. **Entering.** Once the compiled kernel is loaded, the loop enters it at its
   next iteration boundary and runs the rest natively.

Step 5 works because a `while` or `loop` keeps its whole state in variables, so
entering partway through is the same as entering at the top. Nothing has to be
guessed or replayed.

The kernel calls the **same runtime the interpreter calls** — the same
arithmetic, the same bignum promotion, the same string comparison — because it
is linked with its runtime references undefined and bound to the running
`rakupp` when it loads. A tiered loop and an interpreted one cannot disagree
about what your program means; they run the same machine code for everything
but the loop's own control flow.

## What it is worth

Measured on an arm64 Mac, Release build, against the same programs run plainly.
The machine was busy, so read these as factors:

| | interpreted | `--jit`, warm cache | `--cnp` | `--exe -O` |
|---|---:|---:|---:|---:|
| 5M-iteration integer `while` | 0.96 s | **0.04 s** | **0.05 s** | 0.03 s |
| `examples/mandel.raku` | 0.12 s | 0.10 s | **0.08 s** | 0.07 s |

(The Mandelbrot row is mostly startup and output: `--exe -O` compiles the whole
program and still takes 0.07 s of it. The loop itself goes from about 50 ms to
about 7 ms.)

A tiered loop lands on the `--exe -O` row, which is the honest ceiling for this
version: it is the same emission, so it is not faster than compiling the whole
program ahead of time. What it buys is that you did not have to.

**It does nothing for code that is not a hot arithmetic loop.** Method
dispatch, object construction, hash and array work, regexes and anything that
calls a routine are untouched — they are not eligible, and the loop around them
is not either.

## How much of a program it reaches

The factors above are what a tiered loop is worth. This is how often a program
has one. `--cnp=stats` over the 40 programs in `examples/` and `tools/bench/`
that run without an argument:

| | before `for` was counted | now |
|---|---:|---:|
| programs with no countable loop at all | 33 of 40 | **20 of 40** |
| loops examined | 12 | 33 |
| loops that passed the whitelist | 3 | 7 |
| programs that entered a kernel | 2 | **6** |

The left column is what this looked like when only `while`, `until` and the
C-style `loop` were counted, and it is why `for` over a Range was the first
widening taken: a `for` was not *refused*, it was never examined, and refusing
things better would not have moved a single row of it.

What is still never counted is the rest of how Raku iterates. Counting the
keywords across the Raku in this repo — `examples/`, `tools/`, `rakulib/`,
`lib/` and `t/`, 904 files — by grep over source text, so proportions rather
than exact loop counts:

| form | | counted? |
|---|---:|---|
| `for` | 2256 | **only over a Range of integers** |
| `.map` / `.grep` | 966 | no |
| `while` | 378 | yes, unless it is a statement modifier |
| `until` | 72 | yes |
| `loop (…)` | 43 | yes |
| `repeat` | 26 | no |

A `for` over an array, over `.kv`, over a lazy sequence or written as a
statement modifier (`$f *= $_ for 1 .. 10000`) is still not a candidate, and
neither is any `.map`. So the honest summary is that the most common *shape* is
now reachable and the most common *sources* are not.

Both spellings of a counted `for` tier up, and the same arithmetic written three
ways now lands in the same place:

| a 5M-iteration sum, same answer | plain | `--cnp` |
|---|---:|---:|
| `for 1 .. 5_000_000 -> $i { … }` | 0.95 s | **0.03 s** |
| `for 1 .. 5_000_000 { … $_ … }` | 0.95 s | **0.03 s** |
| `while $i <= 5_000_000 { … }` | 1.33 s | **0.03 s** |

**The topic form is `--cnp`'s alone.** `for 1 .. N { … $_ … }` tiers up under
`--cnp` and stays interpreted under `--jit`, which is the one place the two
backends' eligibility lists differ. The reason is in the C++ backend rather
than in the loop: it emits kernels through the same code generator `--exe`
uses, and that generator does not resolve `$_` to a lexical — it emits the
enclosing topic, and in a kernel there is none. `--jit=verbose` says so by
name. The pointy form `-> $i` tiers up under both.

Where a kernel is built it is worth the factors in the table above, and on
`tools/optbench/nummath.raku` — a `Num` kernel, which the `-O` lanes do not
reach — `--cnp` runs the whole program in 57 ms against `--exe -O`'s 178 ms.

`tools/run-engines.raku` prints the per-kernel version of this across every
engine, with a column saying whether each kernel was refused, never counted, or
built.

## What tiers up

A `while`, `until` or C-style `loop`, unlabelled, not in expression position —
or a `for` over a Range of integers, unlabelled, not in expression position, not
a statement modifier, with one loop variable and no destructuring — whose body
contains only:

- integer, number and plain string literals;
- plain `$` scalars with no twigil (not `$_`, not `$*dyn`, not `$!attr`);
- `my $x` declarations without a type, trait, coercion or default;
- arithmetic, comparison, string and logical operators, `++`/`--`, ternaries,
  assignment and its compound forms;
- `if`/`elsif`/`else`, bare blocks, nested loops, unlabelled `last` and `next`.

Anything else refuses the loop: a call of any kind, a method, an index, a
regex, a `for`, `return`, `die`, a phaser, `state`, a closure. `--jit=verbose`
names the construct that refused each loop, which is also the work queue for
widening the list.

A `for` **around** an eligible loop, where the outer `for` is not itself a
candidate, costs nothing: it is not refused, so the inner loop still tiers up
and is re-entered once per outer iteration.

A counted `for` has one extra rule of its own. Its loop variable is a
**read-only** binding, and a kernel assigns its slots directly, so a body that
writes the loop variable refuses the loop. That is not tidiness: the
interpreter rebinds the variable from the counter on every iteration, so a write
to it is forgotten at the next one, while a kernel holds one slot for the whole
loop and would carry the write into the counter and change the trip count.

There is a second gate at the moment of entry. A variable the kernel would
**write** is refused if its container has behaviour a direct assignment would
skip: a native width (`my int8`), a readonly binding, a coercion, a default, an
`is rw` write-through, `is dynamic`. A variable the kernel only **reads** is
exempt, which is what lets a loop in a routine use its own parameters.

## The cache

A compiled kernel is written to `~/.cache/rakupp/jit`, keyed on the kernel's
source, this build of rakupp and the compiler that built it. Any rebuild of
rakupp invalidates every entry.

This matters more than it sounds. A compile takes a few tenths of a second, and
a program that finishes faster than that gets no benefit from its own first
run — the cache is what makes the *second* run fast. Programs get faster the
more often you run them.

Nothing is written to your disk unless you pass the flag. A kernel is about
50 KB; the 710-program test corpus leaves 40 of them, just under 2 MB in total.

```bash
rakupp --jit-info      # what is in there
rakupp --jit-clean     # empty it; always safe, it is rebuilt on demand
```

`--jit=nocache` bypasses the cache at both ends.

### The precompiled header is opt-in, and why

`--jit=pch` builds a compiled snapshot of the runtime headers and reuses it for
every later compile, which takes a kernel from 0.83 s to 0.55 s. It costs
**about 30 MB, per build of rakupp**, which is a poor trade for a program with
one hot loop: the saving is taken once, in the background, where nobody is
waiting for it. So it is off unless you ask.

Ask for it when you are compiling *many* kernels at once — a test sweep, a
batch of programs — where 0.28 s each adds up to minutes.

A header belongs to one build of rakupp and is never read again after you
rebuild. Building a new one removes the old, so the directory holds at most one,
and `--jit-clean` removes that.

## The spec words

| | |
|---|---|
| `--jit` | on: background compile, threshold 1000 |
| `--jit=off` | explicitly off (the default) |
| `--jit=sync` | compile before the loop continues, rather than in the background |
| `--jit=verbose` | narrate every decision to stderr |
| `--jit=stats` | one summary line at exit |
| `--jit=nocache` | never read or write the kernel cache |
| `--jit=pch` | build a precompiled header: each compile 0.83 s → 0.55 s, at ~30 MB per build of rakupp |
| `--jit=threshold=N` | iterations before a loop counts as hot |

They combine: `--jit=sync,verbose,threshold=0` compiles every eligible loop on
its first iteration and says so, which is how the test gate runs.

## One thing that reads differently

A kernel does not track which statement it is on — not tracking it per
statement is most of why it is fast. So an **uncaught** error raised inside a
tiered loop is reported at the loop's own line rather than at the exact
statement inside it:

```
# interpreted
  in block <unit> at prog.raku line 5      # the statement that failed
# --jit
  in block <unit> at prog.raku line 3      # the loop containing it
```

Everything else is the same, including the message, the exception type and the
exit status. A **caught** exception is unaffected: it unwinds out of the kernel
and reaches its `CATCH` exactly as it would have, with every variable reading
what it should. `--exe` prints no line at all for the same program, so this is
the compiled backends' shared limit, answered a little more usefully here.

## `--cnp`: the same thing without a compiler

`--jit` needs a C++ compiler on the machine and a cache on the disk, and the run
that benefits is the second one. `--cnp` is a second backend behind the same
machinery that needs neither — and it is the one expected to survive: the plan
is for it to become the default and for `--jit` to go, once it has been run on
more than the one platform it has been run on so far.

```bash
rakupp --cnp prog.raku
```

It works by **copy and patch**. Snippets of machine code — one per operation a
kernel can perform — were compiled when rakupp itself was built and are carried
inside the binary. To compile a loop, rakupp copies the snippets it needs into a
page of memory, one after another, and fills in the blanks: which register, which
constant, where to go next. There is no compiler to find and nothing to write
down.

| | `--jit` | `--cnp` |
|---|---|---|
| needs a C++ compiler | yes | **no** |
| writes to your disk | ~50 KB per kernel | **nothing** |
| time to build one kernel | ~0.55–0.83 s | **~15 µs** (measured over 4,000 of them) |
| the run that gets faster | the second, from the cache | **the first** |
| iterations before a loop is hot | 1000 | **100** |
| what it can compile | whatever `--exe -O` emits | what its snippets cover |

The last row is the trade. Both backends start from the same eligibility list
above, but `--cnp` can only build what it has snippets for, and a loop it cannot
build stays interpreted — which costs nothing. `--cnp=verbose` says which loops
it took and which it turned down.

`--cnp=SPEC` takes `off`, `on`, `verbose`, `stats` and `threshold=N`. There is no
`sync`, because there is no background compile to wait for, and no `nocache`,
because there is no cache.

### The one thing it gives up

A `--cnp` kernel keeps the loop's variables in machine registers for the whole
loop and writes them back when it leaves. That is where its speed comes from, and
it is invisible — *unless something outside the loop writes one of those
variables while it runs*. Nothing inside the loop can (a loop that calls anything
is not eligible), so the only way is another thread:

```raku
my $stop = False;
start { until $stop { ... } }     # would never see the write
$stop = True;
```

So a `--cnp` kernel is **not entered at all while another Raku thread is live**.
That loop stays interpreted and behaves exactly as it always did. Single-threaded
programs — which is most programs, and all of the ones above — are unaffected.

### Which snippets your binary has

```bash
rakupp -V
```

reports the instruction set the snippets were built for, or says there are none.
Today that is arm64; the x86-64 support is written and has not been run. A binary
with no snippets says so once and runs interpreted.

### In a bundled binary

`--bundle` can carry it, and it is the only backend that can — `--jit` would need
a C++ compiler on whatever machine runs the bundle, which is the thing a
single-file deliverable exists not to need. A bundled binary has no options of
its own, so the choice is made when you build it:

```bash
rakupp --bundle --cnp prog.raku -o prog
```

`RAKUPP_CNP=1` turns it on for one run of a bundle built without it, and
`RAKUPP_CNP=0` turns it off again. Anything else in that variable is read as a
spec, so `RAKUPP_CNP=verbose,stats` works too.

## Requirements and limits

These are `--jit`'s; `--cnp` answers the first two differently, above.

- **A C++ compiler has to be on the machine** — the same one `--exe` needs, and
  found the same way (`$CXX`, else `c++`/`clang++`/`g++`). Without one, rakupp
  says so once and runs interpreted.
- **macOS and Linux.** Windows needs its own loader path and is not done.
- One compile runs at a time.
- A kernel takes about 0.83 s to compile, or 0.55 s with `--jit=pch`.

## Seeing what it did

```bash
rakupp --jit=verbose,stats prog.raku
```

```
[jit] on — compiler c++ (clang, PCH lane on), threshold 1000
[jit] loop at line 2 is eligible, 2 slot(s)
[jit] loop at line 9 not eligible: an expression the kernel whitelist does not cover
[jit] examined 2, eligible 1, compiled 0, cache hits 1, failed 0, kernels entered 1
```

`rakupp --cpp -O prog.raku` prints the same C++ the kernel is built from, for
the whole program rather than one loop, if you want to read what runs.

## See also

- [JIT-PLAN.md](../dev/plans/JIT-PLAN.md) — the design, the measurements it was
  chosen on, and what the next steps are.
- [CNP-PLAN.md](../dev/plans/CNP-PLAN.md) — the copy-and-patch backend: how a
  snippet is patched, and the four things that made it the harder of the two.
- [OPTIMIZATION.md](../internals/OPTIMIZATION.md) — the `-O` passes the kernel
  is emitted with.
- [CLI.md](CLI.md) — the flag among the rest of the command line.
