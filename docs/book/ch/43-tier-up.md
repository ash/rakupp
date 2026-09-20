# Compiling While It Runs

The campaign's two measured episodes attacked *constant factors*: what it
costs to read a variable, to copy a value, to carry one limb of a multiply. Neither
changed what the machine does with a program. A tree walk stayed a tree walk,
and the native back end stayed a translation made before the program ran.

This chapter is the other kind of change. For one shape of code — a hot
arithmetic loop — the interpreter stops walking the tree partway through and
runs machine code instead, and it picks the loop because of what the program
was *observed* to do rather than because anyone asked. Two back ends do it,
landed a day apart, behind one harness. `--jit` hands the loop to the C++
compiler on the machine; `--cnp` stitches it out of machine-code snippets
carried inside rakupp itself.

**Both are off by default, and both are work in progress.** They are one
feature with two back ends, kept apart so each can be measured against the
interpreter on its own, and the expectation is that `--cnp` becomes the default
and `--jit` is removed. Treat the two spellings as scaffolding rather than as
interface; this chapter describes mechanisms, and `docs/guide/JIT.md` is the
flag reference that moves with them.

## What tiering up is

Five steps, of which only the last is unusual.

1. **Counting.** Every `while`, `until`, C-style `loop` and `for` over a Range
   of integers counts its iterations. Nothing else does.
2. **Checking.** A loop that goes hot is checked against a whitelist. One that
   fails is marked and never examined again.
3. **Emitting.** The loop becomes a kernel — a small compiled function with the
   variables it did not declare bound into the live frame.
4. **Compiling.** By whichever back end is on.
5. **Entering.** At the loop's next iteration boundary, control moves into the
   kernel and the rest of the loop runs natively.

Step five is the one that usually needs machinery. Entering compiled code
partway through a running loop is *on-stack replacement*, and in a bytecode VM
it means reconstructing a native frame from an interpreter frame: which
register held what, where the stack pointer should be, which values were
spilled. Here it needs nothing at all. A `while` or `loop` keeps its entire
state in named variables, which live in the interpreter's environment either
way, so entering at an iteration boundary *is* entering at the top. Nothing has
to be guessed, replayed or reconstructed.

That is not a trick; it is a consequence of the tree walk (Chapter 13) keeping
no hidden execution state between statements. The design decision that makes
the interpreter simple is the one that makes this cheap.

## The harness, and the whitelist that makes it sound

A candidate loop must be unlabelled and not in expression position, and its
body may contain only:

| | |
|---|---|
| literals | integer, number, Rat, string, Bool |
| variables | plain `$` scalars with no twigil: not the topic, not a dynamic, not an attribute |
| declarations | `my $x`, with no type, trait, coercion or default |
| operators | arithmetic, comparison, string and logical, `++` and `--`, the ternary, assignment and its compound forms |
| statements | `if` with `elsif` and `else`, bare blocks, nested loops, unlabelled `last` and `next` |

Anything else refuses the loop: a call of any kind, a method, an index, a
regex, an inner `for`, `return`, `die`, a phaser, `state`, a closure.

The list looks like a list of things not yet implemented. It is really **one
invariant, spelled out**: *nothing in the loop may call anything.* Everything
that follows in this chapter — binding variables by pointer, holding them in
machine registers for the whole loop, never checking a statement boundary —
is sound because no code inside a kernel can observe the loop's variables
except through the kernel itself. A widening that admits a call has to answer
that question again, and the answer is not "probably fine".

### The second gate, at the moment of entry

The whitelist is about the loop's *text*. The second gate is about the
containers it will touch, and it runs every time the kernel is entered.

A slot the kernel would **write** is refused if its container has behaviour
that a direct assignment would skip: a native width (`my int8` wraps at eight
bits), a readonly binding, a coercion, a default, an `is rw` write-through,
`is dynamic` (Chapter 12). A kernel assigns the container, so it would honour
none of them. A slot the kernel only **reads** is exempt, which is what lets a
loop inside a routine use its own parameters.

The wrapped answer is the proof the guard fired:

```raku
my int8 $n = 0;
my $i = 0;
while $i < 300 { $n = $n + 1; $i = $i + 1 }
say "$n $i";        # 44 300 — the int8 wrapped, so no kernel ran
```

A counted `for` has a rule of its own. Its loop variable is a read-only
binding, so a body that writes it refuses the loop — and that refusal is
load-bearing rather than tidy. The interpreter rebinds the variable from the
counter on every iteration, so a write to it is forgotten at the next one,
while a kernel holds one slot for the whole loop and would carry the write into
the counter and change the trip count.

## The first back end: hand the loop to the machine's compiler

`--jit` emits the loop as a small C++ function — the same one the code
generator would print for it with `-O` (Chapters 26 and 27), with the
undeclared variables bound as references into the live frame — runs a C++
compiler on it in a background thread, and `dlopen`s the result. The
interpreter keeps iterating meanwhile and never waits.

What makes it a JIT rather than a build step is that it happens during the run,
on code chosen by how the program behaves, and takes over mid-loop. What makes
it an unusual one is the back end: there is **no instruction encoder and no
executable-memory buffer**, because the compiler on the machine is the
assembler. The kernel is linked with its runtime references undefined and bound
to the running rakupp when it loads, so a tiered loop and an interpreted one
cannot disagree about semantics — they run the same machine code for everything
but the loop's own control flow.

The price is in the numbers. A kernel takes about 0.83 s to compile, or 0.55 s
with a precompiled header, and is about 50 KB on disk. That is why the
threshold is 1000 iterations and why there is a cache, keyed on the kernel's
source, this build of rakupp and the compiler that built it: a program that
finishes in under a second gets nothing from its own first run, and the cache
is what makes the *second* one fast.

## The second back end: copy and patch

`--cnp` removes the compiler and the cache. A **stencil** is a snippet of
machine code with holes in it, compiled when rakupp itself was built and
carried in the binary as bytes. To compile a loop, the emitter copies the
snippets it needs into a page of memory, one after another, and fills the holes
with this run's values: which register, which constant, where to go next.

Three programs, meeting at one header:

```
src/cnp/stencils.c  ->  tools/cnp-extract  ->  CnpStencils.cpp
  the templates           knows object            the table that ships
                          formats                        |
                                                         v
                                                 src/cnp/CnpEmit.cpp
                                                 knows encodings
                                                         |
                                                         v
                                                    src/Cnp.cpp
                                                    knows Raku
```

`src/cnp/CnpTable.h` is the whole of what passes between them: an abstract
patch kind and which hole it wants. Nobody has to know both halves. The
extractor runs on the machine that *builds* rakupp, which is why there is no
cross-object-format case to handle — it only ever reads the host's own.

This build carries **53 stencils, 5,324 bytes of template code**, sitting in
read-only data and costing nothing in a run that never turns the flag on.

### A stencil, and the holes in it

The holes are relocations, and that is the trick the whole back end turns on.
`stencils.c` declares undefined symbols — `_JIT_OP0` to `_JIT_OP3`, `_JIT_CONT`,
`_JIT_TARGET`, `_JIT_SLOW` — and reads their *addresses*. The C compiler, with
no idea what they are, emits its ordinary address-of-a-global sequence and one
relocation per reference. The extractor lifts those relocations out of the
object file; the patcher rewrites the instructions.

Here is the stencil for a loop condition — "branch out when `$i < k` is false"
— as the compiler left it, with the holes marked:

```
_rk_st_jnlti:
   adrp x8, 0          ; hole: OP0, the register number
   ldr  x8, [x8]       ;   an ADRP/LDR pair through the GOT
   ldrb w9, [x2, x8]   ; the register's tag
   cmp  w9, #0x1
   b.eq <the Num lane>
   cbnz w9, <cold>     ; neither Int nor Num
   ldr  x8, [x1, x8, lsl #3]
   adrp x9, 0          ; hole: OP1, the loop bound
   ldr  x9, [x9]
   cmp  x8, x9
   b.ge <target>       ; hole: _JIT_TARGET
   b    <next>         ; hole: _JIT_CONT
```

The two `ADRP`/`LDR` pairs are where most of the win is. The compiler had to
assume an arbitrary symbol, so it reached it through the global offset table —
two instructions and a **load**. By patch time the value is known, and when it
fits in 32 bits the same two instruction slots hold `MOVZ` and `MOVK` instead:
no table slot, and no memory access at all on the hot path. Register numbers
and loop bounds almost always qualify; a `double`'s bit pattern does not, and
falls back to a table slot in the same mapping.

The rewrite is refused unless the two instructions are adjacent and all three
register fields agree. Anything looser and the scratch register might be one
another instruction in the stencil is still reading, which would be a wrong
answer rather than a slow one.

Every stencil that can fall back to the interpreter's own operator dispatch is
written in **two pieces**. The fast one handles the machine-number lanes and
calls nothing, so the compiler gives it no prologue and no epilogue; everything
it cannot do it tail-calls to a cold block the emitter lays out past the end of
the loop. Written as one function with the call inline, the first draft paid
six stack-memory operations per operation for a frame only the cold half
needed.

### The layout, and why nothing is ever out of range

One mapping holds the lot:

```
[ stencil code, one block per op ] [ veneers ] [ GOT slots ]
```

so a branch between stencils is always inside the range a single instruction
can express, and an address-of-page instruction always reaches the table area,
which is kilobytes away rather than wherever the heap landed relative to the
executable. The **veneers** handle the other direction: a stencil calling a
runtime helper is calling into the host executable, which may be anywhere, so
it branches to a three-word thunk in the buffer that loads the real address and
jumps. Cold, and free on a path that never calls a helper.

The buffer is mapped read/write, patched, then flipped to read/execute with
`mprotect`, per region and never both at once. On Apple silicon the documented
alternative is `MAP_JIT` with `pthread_jit_write_protect_np`, and it is
deliberately the fallback: that call toggles write protection for every such
mapping in the process and for the calling thread only, so patching one kernel
while another thread runs a second one is a race by construction. rakupp runs
work on several threads (Chapter 38). `mprotect` has no such coupling.

### What it costs to make one

Measured rather than asserted: a program of 4,000 distinct one-iteration loops,
lowered and patched at a threshold of zero, costs 63 ms more than running it
plainly — about **15 µs per kernel**, and that figure includes entering each
one. Against `--jit`'s 0.55–0.83 s that is four to five orders of magnitude,
which is the whole reason this back end needs no cache: there is nothing worth
storing. It also lets the threshold drop from 1000 iterations to 100, and it
makes the **first** run of a program the fast one.

## The register file, and the one IR in this engine

Chapter 3 classifies the back end by what is missing: no bytecode, no
three-address code, no SSA, no control-flow graph — the AST is the sole
representation the whole way through. This back end is the exception, and it is
worth stating exactly what kind.

A kernel does not run on `Value` objects. It runs on a flat register file:
`r[k]` holds an int64, the bit pattern of a double, or 0/1 for a Bool, and
`t[k]` says which. Anything else — a bignum, a `Str`, a `Rat`, an object, an
enum value — lives in a parallel array of real `Value`s with the tag `RK_T_BOX`.
Slots are unboxed into registers at kernel entry and written back at exit,
which is what makes the arithmetic one instruction instead of a call.

To emit that, the lowering walks the loop subtree once and produces a flat list
of ops — a stencil index, up to four operands, and three continuations: the
next op, a branch target, and the cold block a guard bails to. The `while` loop

```raku
my $s = 0; my $i = 0;
while $i < 200000 { $s = $s + $i; $i = $i + 1 }
```

becomes sixteen of them: seven on the hot path — the fused compare-and-branch,
an add, a move, an add-immediate, a move, the back edge, the return — and nine
more in cold blocks, one per guard that can fail, each ending where the fast op
would have gone. Then the patcher copies 1,224 bytes of stencils into a page
and fills in the holes.

That list is an intermediate representation by any honest definition. What it
is not is a middle end: **nothing runs over it.** No folding, no scheduling, no
register allocation past a bump counter and a high-water mark for temporaries,
no serialisation, and nothing outside the function that builds it ever sees it.
It exists for about 15 µs and is then thrown away. Chapter 3's claim survives
as written for the language pipeline, and this is the footnote to it.

Three kinds of value are deliberately kept out of registers, and the first two
are there because the alternative is a wrong answer nothing would have
reported:

- a **bignum** `Int` cannot be an int64 at all (Chapter 11);
- an **enum value** is an `Int` with a name, and the name is its identity —
  `Less` unboxed to its ordinal prints as `-1` the moment it is copied into
  another container;
- everything else — `Str`, `Rat`, `Complex`, objects — stays a `Value`.

## The four costs, and what each came to

The plan that proposed this back end named four things it would cost before any
of it was written. Recording what they actually came to is more useful than the
estimate was.

**Two instruction sets.** Real, and half paid. The patcher implements arm64 and
x86-64; arm64 is the one that has been run. The x86-64 encodings were written
from the manual, and when CI ran them the result was worse than untested: a
kernel *is* entered and returns the **wrong answer**, while the statistics line
cheerfully reports success. That is the one outcome worse than not compiling at
all, because nothing about it looks like a failure from outside. `--cnp` now
refuses x86-64 at startup, names the plan in the reason, and runs the loop
interpreted.

**Two object formats.** Smaller than it looked, for the reason given earlier:
the extractor runs on the build machine, so there is no cross-format case. Mach-O
and ELF are both implemented.

**W^X and the executable-memory rules.** Smaller still, and the default route
needs no entitlement and no special mapping — `mprotect` per region, as above.

**`die` is a C++ throw, and a code buffer has no unwind tables.** This was the
hard one, and it is answered by construction rather than by registering unwind
information. The rule is one sentence: **no helper a stencil calls may throw.**
Every helper wraps its body in a catch-all, puts the exception in the frame as
an `exception_ptr` and returns a status; the stencil returns an error code.
Because every stencil tail-calls the next one, the whole kernel is a **single
machine frame**, so that return goes straight back to the trampoline — an
ordinary C++ frame, with ordinary unwind tables — which rethrows.

That is a constraint on the entire stencil-callable interface, and it is
affordable only because the whitelist is small: six helpers cover everything 53
stencils can reach. The cost is real and it is the thing to weigh before every
widening, because the sixth helper was free and the sixtieth would not be.

It has a visible consequence. The registers are hoisted, so a kernel that dies
half way has to write them back before the throw propagates:

```raku
my $i = 0; my $n = 0; my $d = 5;
{
    while $i < 100 { $n = $n + 10 div $d; $d = $d - 1; $i = $i + 1 }
    CATCH { default { say "caught " ~ .^name ~ ": i=$i n=$n d=$d" } }
}
```

The division by zero happens at iteration five, inside a cold block, inside
machine code with no unwind tables — and the `CATCH` must still read
`i=5 n=22 d=0`, byte-identical to the interpreted run. It does.

## Hoisting, and the bug that priced it

Holding the loop's variables in registers is where the speed comes from, and it
is unobservable for exactly one reason: on this thread, the whitelist admits no
calls. On *another* thread it is not unobservable at all, and no amount of
reasoning about the whitelist would have found it, because the whitelist is
about what is inside the loop. The corpus gate found it as a hang:

```raku
my $stop = False;
my $noise = start { my $n = 0; until $stop { $n = $n + 1 } };
$stop = True;                  # the kernel never sees this
```

The kernel read `$stop` once, and the write it was waiting for landed in the
container it had stopped looking at. Nothing about that program is exotic — a
flag set by another thread is how you stop a worker.

So a `--cnp` kernel is **not entered at all while another Raku thread is live**,
which is what the concurrency runtime's live-worker and cued-load counters
already say (Chapter 38). That loop stays interpreted and behaves as it always
did. The test comes before the slot binding, because the interpreter asks again
on every iteration and a refusal has to cost one relaxed load; the site is not
retired, so a program that joins its workers gets its kernel back.

There is a third case that none of the harness's guards can see: two names
bound to a **single container** would be two registers here and one cell there,
and the write-back would drop whichever went last. That is checked on the spot,
with a pairwise scan over the handful of slots.

`--jit` has none of these questions, because it does not hoist: its kernel binds
a reference and re-reads the container on every use. This is the one place
where the faster back end is the more restricted one, and closing it — by
reloading read-only slots per iteration, if the measured cost of one inline load
is worth the generality — is the open item.

## Measured

Minimum of seven, arm64 Release build, on a machine carrying a load average of
5–8, so these are factors rather than percentages and none of them belongs in a
benchmark table until it is re-taken under Chapter 40's quiet-machine protocol.
`--jit` is shown with a warm cache, which is its second and every later run;
`--cnp` has no such distinction.

| | interp | `--cnp` | `--jit`, warm | `--exe -O` |
|---|---:|---:|---:|---:|
| 5M-iteration integer `while` | 959 ms | **45 ms** | 42 ms | 27 ms |
| the Mandelbrot example | 121 ms | **78 ms** | 95 ms | 71 ms |
| a kernel entered 200,000 times | 784 ms | **140 ms** | 116 ms | 34 ms |

Each row says something different. The first is the headline: 21× over the
interpreter, landing just above `--jit`'s warm cache. The second — the
Mandelbrot program in the examples directory — reads as only 1.6× and needs
reading: `--exe -O` compiles the *whole* program and still takes 71 ms, so most
of that 121 ms is startup and output rather than the loop. What the loop itself
costs falls from roughly 50 ms to roughly 7 ms.

**The third row was written to find out whether it would go the other way.** An
ineligible outer loop around a ten-iteration inner one enters a kernel 200,000
times, and every entry binds slots, allocates a register file and boxes the
results back. If entry were expensive the row would read *slower* than
interpreting. It reads 5.6× faster. `--jit` is ahead of it here, and that gap
*is* the entry cost: its kernel binds a reference and allocates nothing, which
is the other side of the trade the register file makes.

That is what a tiered loop is worth. How often a program *has* one is a
different measurement, and a more sobering one. Over the 40 programs in
`examples/` and `tools/bench/` that run without an argument: 20 have no
countable loop at all, 33 loop sites are examined, 7 pass the whitelist, and 6
programs enter a kernel. Counting loop keywords across the 904 Raku files in
the repository says why — `for` outnumbers `while` by six to one, and `.map`
and `.grep` together outnumber it by two and a half to one, and none of those
is counted unless it is a `for` over a Range of integers.

**The most common loop shape is now reachable; the most common loop sources are
not.**

## How it is proved

Three gates, and the second exists because the first can be satisfied by doing
nothing.

**The differential gate.** Every program in the tier-up cases, the regression
corpus and `examples/` is run twice by the same binary — once plainly, once at
a threshold of zero, so every eligible loop lowers on its first iteration — and
stdout, stderr and exit status are compared byte for byte. The interpreter is
the oracle, as it is for the JavaScript back end (Chapter 31). Twenty-five
cases are written for this specifically: the fifteen `--jit` brought and ten
of `--cnp`'s own, each pinning one thing a register file could get plausibly
wrong — a Bool that must print as `True` and not `1`, an enum that must keep
its name, a `Rat` that must stay a `Rat`, NaN at the edge of a comparison, a
string appended sixty times, a kernel that dies half way.

**The coverage check.** A case that agrees only because nothing compiled is not
evidence. So every case that is not explicitly marked as testing a refusal must
be shown to have entered a kernel.

Together they read, on the build this chapter describes: **725 programs
agreeing, 0 differing**, 6 skipped as non-deterministic, and 23 of the 23
cases that are not marked as refusals shown to have entered one.

**Roast at a threshold of zero**, which turns the specification suite into this
back end's differential test as well: every eligible loop in it lowers on entry.
The two lanes are run interleaved, so that both meet the same machine.

The Roast rounds taught a lesson that is not about this back end at all. A
single non-interleaved pair had read 23 assertions fewer under `--cnp`, and
every one of those 23 came from two files hitting the harness's ten-second
timeout in that one run. It looked like a finding. It was not: this suite times
eleven or twelve files out in the *plain* lane every round, so a timeout
carries no information about which lane it happened in. **A suite with a
double-digit timeout baseline cannot be read one run at a time**, in either
direction — and the first reading of it here was published as a
characterisation on the strength of one sample, before the replication existed
to support it.

## What went wrong on the way

Four. Three were caught by a gate; the fourth by reading the machine code the
first draft produced.

**The outermost loop's init was emitted, and re-ran.** A kernel is entered at an
iteration boundary, which for `loop (my $i = 0; …)` is *after* the interpreter
has run the init. The first draft walked it anyway and re-ran `$i = 0` on
entry, restarting the loop. The gate caught it as exactly one extra iteration,
which is what a re-zeroed counter looks like from outside.

**The topic form answered 211 where the interpreter answered 210.** `for 1 .. N
{ … $_ … }` tiers up under `--cnp` and is refused under `--jit`, and the reason
is in the C++ back end: the code generator does not resolve the topic to a
lexical, so the slot bound under that name was written by the synthetic
increment and read by nothing, while the body read the interpreter's live
topic. Wrong, not failed — the outcome the whitelist exists to make impossible,
caught by the differential gate and by nothing else.

**An append loop was right and 570× slower.** `$s ~= …` in a kernel went
through the general operator path, which copies the accumulator out of its box,
builds a whole new string beside it and copies that back — three passes per
append, so *n* appends move O(n²) bytes. 400,000 appends took 34.4 s against
the interpreter's 0.06 s, and the answer was correct the whole time, which is
why no test caught it. The fix was to call the same in-place append the
interpreter and `--exe` already share.

**The fast path paid for the slow path's frame**, which is the measurement that
produced the two-piece stencil described above.

## What it does not claim

- **It is not more general than `--jit`.** The 53 stencils are the ceiling;
  where `--jit` can in principle compile whatever `--exe -O` emits, this refuses
  and leaves the loop interpreted, which costs nothing.
- **It is not a speculating JIT.** There is no type feedback and no deopt path.
  The register tags are checked per operation, which is what lets a guard miss
  fall back *within* the kernel instead of leaving it.
- **It has run on one instruction set.**
- **It does not tier up threaded code.**
- **A kernel costs a page.** Each mapping is at least one page, so forty
  kernels is about 640 KB of address space. Pooling them into an arena is
  obvious and not done.
- **Neither back end helps anything that is not a hot arithmetic loop.** Method
  dispatch, object construction, hash and array work, regexes, and anything that
  calls a routine are untouched — and so is the loop around them.

## Where it goes

The end state is no flag at all: hot loops tier up because that is what the
interpreter does. Three things stand between here and there. The x86-64 patcher
has to be made correct, because making a back end the default that has run on
one instruction set would be making it the default on trust. The threaded gap
has to be closed or consciously accepted — as a flag it is a documented
limitation, as a default it is a silent cliff in threaded programs. And then
`--jit` can go, which deletes the C++-emission lane, the precompiled header,
the on-disk cache and two flags, and nothing else, because the harness was
shared from the start.

What this chapter adds to the campaign is a different kind of entry in the
ledger. Chapters 41 and 43 made the same work cost less. This one skips the
work: for the narrow shape it reaches, the dispatch, the environment lookup and
the `Value` copies are not made cheaper, they are **not performed at all**. The
narrowness is the honest part of the result, and widening it is the same
question every time — what can a kernel be allowed to do and still be unable to
observe the difference?
