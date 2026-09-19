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

1. **Counting.** Every `while` and C-style `loop` counts its iterations. The
   default threshold is 1000.
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

## What tiers up

A `while`, `until` or C-style `loop`, unlabelled, not in expression position,
whose body contains only:

- integer, number and plain string literals;
- plain `$` scalars with no twigil (not `$_`, not `$*dyn`, not `$!attr`);
- `my $x` declarations without a type, trait, coercion or default;
- arithmetic, comparison, string and logical operators, `++`/`--`, ternaries,
  assignment and its compound forms;
- `if`/`elsif`/`else`, bare blocks, nested loops, unlabelled `last` and `next`.

Anything else refuses the loop: a call of any kind, a method, an index, a
regex, `for`, `return`, `die`, a phaser, `state`, a closure. `--jit=verbose`
names the construct that refused each loop, which is also the work queue for
widening the list.

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
