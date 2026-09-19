# `--jit` — compiling while the program runs

Off by default. With `--jit`, a running program compiles its own hot loops to
native code and starts using them partway through the loop, without you asking
for a build and without a separate binary:

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

| | interpreted | `--jit`, warm cache | `--exe -O` |
|---|---:|---:|---:|
| 5M-iteration integer `while` | 0.93 s | **0.03 s** | 0.03 s |
| Mandelbrot, 150×130, floats | 0.41 s | **0.06 s** | 0.06 s |

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

## Requirements and limits

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
- [OPTIMIZATION.md](../internals/OPTIMIZATION.md) — the `-O` passes the kernel
  is emitted with.
- [CLI.md](CLI.md) — the flag among the rest of the command line.
