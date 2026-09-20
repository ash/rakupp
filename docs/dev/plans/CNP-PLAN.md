# Plan: `--cnp` — copy-and-patch, the compiler-free tier-up

*Written 2026-09-19, the day after [JIT-PLAN.md](JIT-PLAN.md) landed, and in
answer to the two items it left open. That file ranked this work second and
named the four things it would cost; this file is what they actually cost.*

**Status: landed 2026-09-19, as WORK IN PROGRESS.** `--cnp[=SPEC]`, 53 stencils,
the build-time extractor, the patcher, the lowering, the bundled-binary lane, and
ten cases of its own on top of the fourteen it inherits from `--jit`.

**Both flags are provisional, and the intended end state is that neither
survives as a flag.** `--jit` and `--cnp` are one feature with two backends,
kept apart only so each can be measured against the interpreter on its own. The
expectation is that **`--cnp` becomes the default and `--jit` is removed** — see
P4 below for what has to be true first. Nothing outside the engine should grow a
dependency on either spelling, and the documentation says so wherever the flags
appear.

**The number a stranger can re-measure:**

```bash
cmake --build build -j
build/rakupp                      t/jit/cases/basic-while.raku   # interpreted
build/rakupp --cnp                t/jit/cases/basic-while.raku   # once is enough
build/rakupp --cnp=verbose,stats  t/jit/cases/basic-while.raku
```

No C++ compiler is consulted, nothing is written to disk, and the **first** run
is the fast one.

---

## Why this, and why not just delete `--jit`

Two items were on the table. Deleting `--jit` was defensible — it costs nothing
when off, measured at 22.91 s against 22.94 s on a 50-million-iteration loop —
but it is also the harness any stencil work would have to rebuild: the candidate
walk, the eligibility whitelist, the counter, the outermost-loop attribution,
the slot binding and its container guards. So `--cnp` is not a second engine. It
is a **second backend behind the same harness**: 168 added lines in `Jit.cpp`,
22 in `Jit.h` and 46 in `main.cpp`, against about 2,500 lines of backend.

What the copy-and-patch backend removes is everything `--jit` needs *around* the
code generator:

| | `--jit` | `--cnp` |
|---|---|---|
| needs a C++ compiler on the machine | yes | **no** |
| needs the runtime headers beside the binary | yes | **no** |
| writes to disk | `~/.cache/rakupp/jit`, ~50 KB per kernel | **nothing** |
| time to produce one kernel | ~0.83 s, or 0.55 s with the precompiled header | **~15 µs** |
| the run that gets faster | the second, from the cache | **the first** |
| what it can compile | whatever `--exe -O` emits | what the 53 stencils cover |

The last row is the trade, and it is the honest one: this backend is narrower.
A loop it cannot lower is refused and stays interpreted, which costs nothing.

## What copy-and-patch is

A stencil is a snippet of machine code with holes in it, compiled **when rakupp
was built** and carried in the binary as bytes. To compile a loop, the emitter
copies the snippets one after another into a page of memory and fills the holes
with this run's values — a register number, an immediate, the address of the
next snippet. No compiler runs, and the result is native code rather than a
dispatch loop.

The holes are relocations. `src/cnp/stencils.c` declares undefined symbols
`_JIT_OP0`…`_JIT_OP3`, `_JIT_CONT`, `_JIT_TARGET` and `_JIT_SLOW` and reads
their *addresses*; the C compiler, having no idea what they are, emits its
ordinary address-of-a-global sequence and one relocation per reference. The
build-time extractor reads those relocations out of the object file and hands
them on as a table of `(offset, kind, which hole)`. At run time the patcher
rewrites the instructions.

Three programs, meeting at one header:

```
src/cnp/stencils.c   →  tools/cnp-extract  →  generated/CnpStencils.cpp
  the templates          knows object            the table that ships
                         formats                       ↓
                                              src/cnp/CnpEmit.cpp
                                              knows instruction encodings
                                                       ↓
                                                  src/Cnp.cpp
                                                  knows Raku
```

`src/cnp/CnpTable.h` is the whole of what passes between them: an abstract patch
kind (`Call`, `Adrp21`, `GotOff12`, `Rel32`, …) and which hole it wants. Nobody
else has to know both halves.

## The four costs JIT-PLAN named, and what they came to

### 1. Two instruction sets

Real, and only half paid. The patcher implements arm64 and x86-64; **arm64 is
the one that has been run**. The x86-64 encodings were written from the manual
and had not executed a single instruction. That is stated rather than hedged
because it is the first thing a reader on an Intel Mac or a Linux box needs to
know.

CI has now run them, and the answer is worse than "untested": on linux-x86_64 a
kernel **is** entered and returns the **wrong answer**. The gate saw it as one
failing check and one passing one — the tiered loop disagreed with the
interpreter while `--cnp=stats` cheerfully reported a kernel had run — which is
the single outcome worse than not compiling at all, because nothing about it
looks like a failure from outside.

So `--cnp` now refuses x86-64 at startup (`checkTable()` in CnpEmit.cpp), names
this plan in the reason, and runs the loop interpreted: the answer the machine
would have given anyway, a little slower. `RAKUPP_CNP_X86=1` lifts the gate,
because P1 cannot be done by anyone who cannot run the thing being fixed. The
gate comes out when P1 lands, and not before.

### 2. Two object formats

Smaller than it looks, for one reason worth stating: **the extractor runs on the
machine that builds rakupp**, so it only ever has to read the host's own format.
There is no cross-format case at all. Mach-O and ELF are both implemented, both
64-bit little-endian; Mach-O/arm64 is the tested one.

### 3. W^X, MAP_JIT and the notarization entitlement

Smaller still, and the default route needs none of it. A kernel is mapped
read/write, patched, and flipped to read/execute with `mprotect`, **per region**,
which is what W^X asks for and what Apple silicon enforces. Measured on this box:
it works with no entitlement and no special mapping.

`MAP_JIT` plus `pthread_jit_write_protect_np` is the fallback rather than the
default, and deliberately so: that call toggles write protection for *every*
`MAP_JIT` mapping in the process and for the **calling thread only**, so a
second thread executing one kernel while this thread patches another is a race
by construction. rakupp runs work on several threads. `mprotect` has no such
coupling.

The notarization entitlement only arises for a hardened-runtime build. Nothing
here needs it today; if release binaries ever adopt the hardened runtime, the
`mprotect` route needs `com.apple.security.cs.allow-unsigned-executable-memory`
and the `MAP_JIT` route needs `com.apple.security.cs.allow-jit`. That is a
packaging decision, recorded here so it is not rediscovered.

### 4. `die` is a C++ throw, and a code buffer has no unwind tables

**This was the genuinely hard one, and it is answered by construction rather
than by registering unwind info.**

The rule is one sentence: **no helper a stencil calls may throw.** Every
`rk_cnp_*` function in `src/Cnp.cpp` wraps its body in a catch-all, puts the
exception in the frame as a `std::exception_ptr`, and returns a status. The
stencil returns `RK_CNP_ERR`. Because every stencil tail-calls the next one
(`musttail`), **the whole kernel is a single machine frame**, so that return goes
straight back to the trampoline — a perfectly ordinary C++ frame, with perfectly
ordinary unwind tables — which rethrows. No exception ever crosses the buffer.

That is a constraint on the whole stencil-callable runtime interface, exactly as
predicted. It is affordable *here* because the eligibility whitelist is small:
seven helpers cover everything 53 stencils can reach. The cost is real and it is
the thing to weigh before every widening of the whitelist, because the seventh
helper was free and the seventieth would not be.

Two consequences worth keeping:

- The registers are **hoisted**, so a kernel that dies half way has to write them
  back before the throw propagates. It does, on both exits. `t/cnp/cases/hoisted-throw.raku`
  pins it: a `div` by zero at iteration five leaves `i=5 n=22 d=0` for the
  `CATCH`, byte-identical to the interpreter.
- `-fno-asynchronous-unwind-tables` is not a size optimisation in the stencil
  build, it is the statement that there is nothing to unwind.

## Design

### The register file, and the three rules that make it sound

A kernel does **not** run on `Value` objects. It runs on a flat register file:
`r[k]` holds an int64, the bits of a double, or 0/1 for a Bool, and `t[k]` says
which. Anything else — a bignum, a Str, a Rat, an object, an enum value — lives
in `boxes[k]`, a real `Value`, with the tag `RK_T_BOX`.

Slots are unboxed into registers at kernel entry and written back at exit. That
is what makes the arithmetic one instruction instead of a call, and it is what
the rest of this section has to justify, because `--jit` does not do it: its
kernel binds a `Value&` and re-reads the container on every use, so none of what
follows was ever its problem.

**On this thread**, hoisting is sound for one reason, the same one `--jit` rests
on: **the whitelist admits no calls.** With no call in the subtree, nothing
running inside a kernel can observe a loop variable except through the kernel's
own registers. Every widening has to answer that again.

**On another thread, it is not sound, and this is where the corpus gate earned
its keep.** `t/regression/top100-refresh-2026-09-03.raku` hung:

```raku
my $stop = False;
my $noise = start { my $n = 0; until $stop { … } };   # reads $stop
…
$stop = True;                                         # the main thread writes it
```

The kernel read `$stop` once and the write it was waiting for landed in the
container it had stopped looking at. Nothing about that is exotic — a flag set
by another thread is how you stop a worker — and no amount of reasoning about
the whitelist would have found it, because the whitelist is about what is
*inside* the loop.

So a kernel is **not entered at all while another Raku thread is live**
(`liveWorkers_` or `cuedLoads_` above zero, which is what the rest of the engine
means by concurrent work). That loop stays interpreted and behaves as it always
did. The test comes before the slot binding, because the interpreter asks again
on every iteration and a refusal has to cost one relaxed load; the site is not
retired, so a program that joins its workers gets its kernel back. It is
conservative in one direction — a kernel inside a `start` block never runs,
because that block's own thread is counted — and that is the right way round.

There is a third case, and the harness's own guards do not see it either: two
names bound to a **single container** would be two registers here and one cell
there, and the write-back would drop whichever went last. `cnp::run` refuses on
the spot when two slot pointers coincide — a pairwise scan over a handful of
slots, cheaper than a set.

The unboxing rules are three exclusions, each with a reason:

- a **bignum** Int cannot be an int64 at all;
- **`enumName`** is the value's identity — `Less` prints as `Less`, and a register
  that kept only its `.i` would print `-1` the moment it was copied into another
  container (`t/cnp/cases/enum-in-register.raku`);
- everything else is a Str, a Rat, an object, and stays a `Value`.

`natBits`/`natFloat` are deliberately *not* excluded: they describe what happens
when something is **stored** into that container, and the harness already refuses
a native container a kernel would write. Reading one is fine.

### The fast/slow split, and the six stack operations that forced it

Every stencil that can fall back to the interpreter's operator dispatch is
written in **two pieces**. The fast one handles the machine-number lanes and is a
**leaf** — it calls nothing, so the compiler gives it no prologue and no
epilogue. Everything it cannot do it tail-calls to `_JIT_SLOW`, a block of
ordinary stencils the emitter lays out past the end of the loop.

The first draft had the call inline, and the hot path paid for a frame only the
cold half needed:

```
_rk_st_jlti:                          _rk_st_jlti:   (after the split)
    stp x22, x21, [sp, #-0x30]!
    stp x20, x19, [sp, #0x10]
    stp x29, x30, [sp, #0x20]
    …the compare…                         …the compare…
    ldp x29, x30, [sp, #0x20]
    ldp x20, x19, [sp, #0x10]
    ldp x22, x21, [sp], #0x30
    b   <next>                            b   <next>
```

Six stack-memory operations per op, on every iteration, for a lane that almost
never runs. The split is why the loop condition is nine instructions.

### Folding the hole into the instruction stream

On arm64 the compiler reaches an undefined symbol's address with `ADRP` + `LDR`
through the GOT — two instructions and a load. The patcher knows the value by
then, and when it fits in 32 bits the same two slots hold `MOVZ` + `MOVK`: no GOT
slot and **no load at all** on the hot path. Register numbers and loop bounds
almost always qualify.

It is refused unless the pair is adjacent and all three register fields agree
(`ADRP Xd` / `LDR Xt, [Xn]` with `d == n == t`), so that nothing else in the
stencil can be relying on the scratch register still holding a page address.
Anything wider than 32 bits — a `double`'s bit pattern, a large literal — falls
back to a GOT slot in the same mapping.

### Layout, and why nothing is ever out of range

One mapping holds the lot:

```
[ stencil code, one block per op ] [ veneers ] [ GOT slots ]
```

A branch between stencils is therefore always inside the ±128 MB a `B` can
express, and an `ADRP` always reaches the GOT area, which is kilobytes away
rather than wherever the heap landed relative to the executable.

The **veneers** handle the other direction. A stencil calling `rk_cnp_binop` is
calling into the host executable, and nothing says the heap and the executable
are within branch range. So a helper call branches to a three-word thunk in the
buffer — load the address from the word after it, jump — which is cold and costs
nothing on a path that never calls a helper.

### Fused compare-and-branch, and the NaN question

`while $i < $n` is **one** stencil holding the test and the back edge; no Bool is
ever built. There are negated forms of all twelve (`rk_st_jnlt` and friends)
because `while` and `if` want "branch out when the condition is **false**", and
those are written `!(a < b)` rather than `a >= b` **on purpose**: those are
different questions once an operand is NaN, and inverting the operator to save
twelve stencils would have quietly changed what a `Num` loop does at its edges.
`t/cnp/cases/nan-compare.raku` is that case.

### What the lowering covers

The eligibility whitelist is `--jit`'s, unchanged — `Scan` in
[src/Jit.cpp](../../../src/Jit.cpp). The lowering is narrower still, and refuses
rather than guessing:

- **Statements:** `ExprStmt`, `Block`, `IfStmt` with `elsif`/`else`, nested
  `while`/`until`/C-style `loop`, unlabelled `last`/`next`.
- **Expressions:** Int/Num/Rat/Complex/Str/Bool literals, plain `$` scalars,
  assignment and compound assignment, `+ - *` with fast lanes, every other
  operator through `applyArith`, the six comparisons, `&&`/`and`/`||`/`or`/`//`
  as real short-circuits yielding an **operand** rather than a Bool, prefix and
  postfix `++`/`--`, unary `- + ! not ? ~ +^`, and the ternary.
- **Nothing else**, and a `ListExpr` only in a C-style loop's header, where a
  comma is a sequence of side effects.

Every operator that is not one of the fast lanes lands in `applyArith` — the
interpreter's own dispatcher, the same function `Codegen` emits calls to. So an
overflow into a bignum, a Rat division, a coercion and a type error are all
decided by the code an interpreted program would have reached. That is what makes
the differential gate below meaningful rather than circular.

### The outermost loop's init is not emitted

A kernel is entered at an iteration boundary, which for `loop (my $i = 0; …)` is
**after** the interpreter has run the init. The first draft walked the init
anyway, and re-ran `$i = 0` on entry — restarting the loop. The gate caught it as
exactly one extra iteration, which is what a re-zeroed counter looks like from
outside. `t/jit/cases/floats.raku` and `comma-header.raku` were the two that
noticed.

### The threshold is a hundredth of `--jit`'s

`--jit` waits 1000 iterations because a kernel costs the better part of a
second. `--cnp` costs about 15 µs, so waiting that long throws away most of
what there is to win. It waits
**100**, compiles on the interpreter thread, and is done before the next
iteration starts. There is no `sync` word because there is nothing to do on
another thread, and no `nocache` because there is nothing on disk.

## Where it stands

Measured 2026-09-19 on the benchmark machine (Darwin 25.5, arm64, Release
`-O3 -DNDEBUG`, Apple clang). `--jit` is shown with its cache **warm**, which is
its second and every later run; `--cnp` has no such distinction — every run is
the same.

| | interp | `--cnp` | `--jit`, warm | `--exe -O` | Rakudo |
|---|---:|---:|---:|---:|---:|
| 5M-iteration integer `while` | 959 ms | **45 ms** | 42 ms | 27 ms | 797 ms |
| `examples/mandel.raku` | 121 ms | **78 ms** | 95 ms | 71 ms | 379 ms |
| a kernel entered 200,000 times, ten iterations each | 784 ms | **140 ms** | 116 ms | 34 ms | 360 ms |

Minimum of seven, on a machine carrying a load average of 5–8, so these are
factors rather than percentages and none of them belongs in BENCHMARKS.md until
re-taken under the quiet-machine protocol.

Row by row, because the interesting thing is different in each:

- **The 5M loop** is the headline: 21× over the interpreter, landing just above
  `--jit`'s warm cache and above `--exe -O`. The residue over `-O` is the
  interpreted prefix before the kernel is entered.
- **Mandelbrot** reads as only 1.6×, and that is honest but needs reading:
  `--exe -O` compiles the *whole* program and still takes 71 ms, so most of that
  121 ms is startup and output, not the loop. What the loop itself costs falls
  from roughly 50 ms to roughly 7 ms. `--cnp` is faster than `--jit` here, which
  is the register file paying for itself on `Num` arithmetic.
- **The third row is the one that could have gone the other way**, and it was
  written to find out. An outer `for` that is not eligible around a ten-iteration
  inner `while` enters a kernel 200,000 times, and every entry binds the slots,
  allocates a register file and boxes the results back. If that entry were
  expensive the row would read *slower* than interpreting. It reads 5.6× faster.
  `--jit` is ahead of it, which is exactly the entry cost: its kernel binds a
  `Value&` and allocates nothing.

**What a kernel costs to produce**, measured directly rather than asserted: a
program of 4,000 distinct one-iteration loops, which at `threshold=0` lowers and
patches 4,000 kernels and enters each one once (`--cnp=stats`: *examined 4000,
eligible 4000, compiled 4000, kernels entered 4000*). Plain 71 ms, `--cnp` 134 ms
— **63 ms for 4,000 kernels, about 15 µs each**, and that figure includes the
entry, so production alone is less. For comparison `--jit` is 0.55–0.83 s per
kernel, four to five orders of magnitude more, and that difference is the whole
reason this backend needs no cache.

**In a bundled binary** (`rakupp --bundle --cnp`), the same 5M loop, minimum of
seven:

| | |
|---|---:|
| the bundle, `--cnp` baked in | **58 ms** |
| the same bundle, `RAKUPP_CNP=0` | 1030 ms |

One binary, no compiler and no rakupp on the machine running it.

## Gates

1. **`t/jit/run.raku --cnp`** — the same differential gate `--jit` uses, with the
   copy-and-patch lane substituted. Every program in `t/jit/cases`, `t/cnp/cases`,
   `t/regression` and `examples` is run twice by the same binary, once plainly and
   once at `--cnp=threshold=0`, and stdout, stderr and exit status are compared
   byte for byte. The interpreter is the oracle, as it is for the JS backend and
   for `--jit`. **722 agreeing, 0 differing**, 6 skipped as non-deterministic.
2. **The tier-up coverage check**, inherited: a case that agrees only because
   nothing compiled is not evidence, so every case that is not marked
   `# JIT: refused` must be shown to have entered a kernel.
3. **Roast at `--cnp=threshold=0`**, so every eligible loop in the suite lowers
   on entry and the suite becomes this backend's differential test too. Run with
   `RAKUPP_OPT=--cnp=threshold=0` (the harness spawns each file as `$BIN $file`
   and has nowhere to put a flag), 12 workers, **interleaved** so that both lanes
   meet the same machine:

   | round | lane | files fully passing | assertions, of all declared | files timed out |
   |---|---|---:|---:|---:|
   | 1 | plain | 736 / 1464 | 202194 / 219547 | 12 |
   | 1 | `--cnp` | **737 / 1464** | **202249 / 219602** | **11** |
   | 2 | plain | 735 / 1464 | 202194 / 219547 | 12 |
   | 2 | `--cnp` | **736 / 1464** | **202195 / 219547** | 12 |
   | 3 | plain | 736 / 1464 | 202195 / 219547 | 12 |
   | 3 | `--cnp` | 736 / 1464 | **202215 / 219571** | **11** |

   Wall time 45.1–45.6 s plain against 43.6–46.6 s at `threshold=0`: the two
   bands overlap, so lowering every eligible loop in the suite costs nothing
   measurable at this resolution.

   **Interleaving is what made this readable, and it is worth recording why.**
   A single non-interleaved pair had read 202193 against 202170 — 23 assertions
   fewer under `--cnp` — and every one of those 23 came from two files
   (`S17-procasync/stress.t`, `no-runaway-file-limit.t`) hitting the harness's
   10-second timeout in that one run. It looked like a finding. It was not: this
   suite times **11 or 12 files out in the PLAIN lane, every round**, so a
   timeout carries no information about which lane it happened in. In the three
   controlled rounds the per-file tables differ by exactly one file each time,
   and all three differences go the OTHER way — `S15-nfg/concat-stable.t`
   TIME → PASS 55/55, `S17-supply/syntax-nonblocking-await.t` part 8/9 → PASS 9/9,
   `S17-lowlevel/cas.t` TIME → part 20/24. Neither of the two files from the
   original pair times out again in six further runs.

   The lesson is not about this backend. **A suite with a double-digit timeout
   baseline cannot be read one run at a time**, in either direction, and the
   first reading of it here was published as a characterisation ("not the
   backend's doing") on the strength of one sample before the replication existed
   to support it.
4. **The off-path still costs nothing.** `--cnp` shares `--jit`'s single gate, so
   a run with neither flag reaches none of this code and the existing perf guard
   already covers it.

## What it does NOT claim

- **It reaches the most common loop SHAPE and not the most common loop
  SOURCES.** Only `while`/`until`, the C-style `loop` and — since `for` over a
  Range of integers landed — that `for` are counted; everything else is not
  refused but never examined. A `for` over an array, over `.kv`, over a lazy
  sequence or written as a statement modifier is still invisible, and so is
  every `.map`. Measured with `--cnp=stats` over the 40 runnable programs in
  `examples/` and `tools/bench/`: 20 have no countable loop at all (33 before
  the `for` work), 33 sites are examined (12), 7 pass the whitelist (3), and 6
  programs enter a kernel (2). One of those (`parmap.raku`) builds a kernel it
  never enters, because another thread is live.
- **The topic form is this backend's alone, and that is the first crack in "one
  eligibility list".** `for 1 .. N { … $_ … }` tiers up here and is refused for
  `--jit`. The reason is in the C++ backend: Codegen does not resolve `$_` to a
  lexical, it emits the enclosing topic or `RT.dynVarRef("$_")`, and a kernel has
  no enclosing topic — so the slot bound under that name is written by the
  synthetic `$_++` and read by nothing, while the body reads the interpreter's
  live topic. It answered 211 where the interpreter answered 210, which the
  differential gate caught and nothing else would have. Copy-and-patch binds its
  slots by name and has the question nowhere. Closing the split means teaching
  Codegen to bind a topic to a name.
- **It is not more general than `--jit`.** The 53 stencils are the ceiling. Where
  `--jit` can in principle compile whatever `--exe -O` emits, this refuses and
  leaves the loop interpreted.
- **It does not tier up a loop whose operators the program overloads**, and
  neither does `--jit`. A user `multi sub infix:<+>` shadows the built-in for
  the operand shapes it has candidates for; a kernel emits the built-in and may
  not call anything, so there is nowhere to put the call. Both backends look the
  routine up in the live frame at the first entry and retire the site. `--exe`
  is not limited this way — it emits the call (`rtUserInfix`), which it could do
  all along and did not: until 2026-09-20 every compiled backend quietly
  answered differently from the interpreter here, `--exe` included.
- **It has run on one platform.** arm64 macOS. The x86-64 patcher is written,
  is now known to be *wrong* rather than merely unexercised, and is refused at
  startup until P1 (see above). ELF is exercised on aarch64 and works.
- **It does not tier up threaded code.** Any loop reached while another Raku
  thread is live stays interpreted, for the reason above. That is a real
  functional gap against `--jit`, which has no such restriction because it does
  not hoist.
- **It is not a speculating JIT.** There is no type feedback and no deopt path;
  the register tags are checked per operation, which is what lets a guard miss
  fall back *within* the kernel instead of leaving it. What that buys over
  `--exe -O` is the unboxed register file, which `--exe -O` does not yet have
  ([UNBOX-PLAN.md](UNBOX-PLAN.md), JIT-PLAN's P3).
- **A kernel costs a page.** Each mmap is at least one page (16 KB on this
  machine), so forty kernels is about 640 KB of address space. Pooling them into
  one arena is obvious and not done.

## Phases

| | | |
|---|---|---|
| **P0** | the ABI, the stencils, the extractor, the patcher, the lowering, `--cnp`, the gate | **DONE** |
| **P1** | x86-64: find and fix what makes a patched kernel return the wrong answer, on both object formats, then drop the startup refusal | **next, and now a known defect rather than a gap** |
| **P2** | arena allocation, so kernels share pages instead of taking one each | |
| **P3** | widen the lowering toward the whitelist's edges (`ListExpr`, `min=`/`max=`), then past it — every step reopening the no-calls question | per-item gates |
| | *and*: re-read read-only slots per iteration instead of refusing a threaded program outright, if the measured cost of one inline reload turns out to be worth the generality | |
| **P4** | make it the default and retire `--jit` | |

### What P4 needs before the flags go

The end state is no flag at all: hot loops tier up because that is what the
interpreter does. Three things stand between here and there, and the first is
the only large one.

1. **P1, a platform matrix.** Making a backend the default that has run on one
   instruction set would be making it the default on trust. ELF is exercised on
   aarch64 now and passes; x86-64 is exercised and *fails*, which is why it is
   refused rather than shipped.
2. **The threaded gap closed or accepted.** Today a loop reached while another
   Raku thread is live stays interpreted (see the register file above). As a
   flag, that is a documented limitation; as the default, it is a silent cliff
   in threaded programs, and it wants either the per-iteration reload in P3 or
   an explicit decision to live with it.
3. **`--jit`'s removal costs nothing to measure.** It is already the case that
   the harness is shared, so retiring `--jit` deletes the C++-emission lane, the
   PCH, the on-disk cache, `--jit-info`/`--jit-clean` and their goldens — and
   nothing else. That is the easy half, and it should happen only after (1),
   because until then `--jit` is the fallback on every platform `--cnp` has not
   been run on.

### What a later phase inherits

- **The no-throw rule is the constraint to protect.** It is what lets the code
  buffer be raw bytes. A widening that wants a helper which can throw has to
  route it through the same status-return boundary, or else register unwind info
  per region and accept that on two platforms.
- **The stencil file is checked by the extractor, not by review.** An object with
  any section but the text one — a literal pool, a jump table, a call into libc —
  fails the build and names what it found. That check is what keeps
  "no constants in a stencil" honest as the file grows.
- **`--bundle --cnp` bakes the backend in.** A bundled binary has no option
  surface — every argument belongs to the embedded program — so the choice is
  made when the bundle is built, and `RAKUPP_CNP` steers one run either way. It
  is the only backend a bundle can carry, because `--jit` would need a compiler
  on the machine running it, which is what a single-file deliverable exists not
  to need.
