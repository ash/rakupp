# Plan: `--jit` — the transpiler becomes a tier-up compiler

*Written 2026-09-19, with the feasibility probes taken before any engine code.
Prompted by the user's question: "a magic compiler trick that delivers
something 100x faster than now", followed by "make it safe and only allow this
mode with an explicit CLI switch, so that we can test it independently and no
chance to break the interpreter and the `--exe` parts".*

**Status: P0 and P1 landed 2026-09-19** — `--jit[=SPEC]`, the eligibility walk,
`emitJitKernel`, the background compile, the PCH, the on-disk kernel cache, and
the gate `t/jit/run.raku` with twelve cases of its own. Where the code decided
differently from the draft below it says so there. What did NOT land: Linux
(P2), unboxed slot hoisting (P3), any widening of the whitelist (P4), and
speculation (P5).

**The number a stranger can re-measure:**

```bash
cmake --build build -j
build/rakupp           t/jit/cases/basic-while.raku     # interpreted
build/rakupp --jit     t/jit/cases/basic-while.raku     # twice: the second is the one
build/rakupp --jit=verbose,stats  t/jit/cases/basic-while.raku
```

A hot loop runs at `--exe -O` speed **inside the interpreter process**, on the
first run, without a compile step the user asked for. Everything else in the
engine behaves exactly as it does today, because without `--jit` none of this
code is reachable.

Measurements below: 2026-09-19, Darwin 25.5 / arm64, Apple clang, `build/`
Release (`-O3 -DNDEBUG`) at v4.0.1-34-g32dd94c. The box was **not** idle (load
2.0 rising to 25 during the sitting), so these are factors, not percentages,
and none of them belongs in BENCHMARKS.md until re-taken under the
quiet-machine protocol.

---

## The gap this closes

`--exe -O` already produces the speed. Nothing runs it, because it needs a
compiler on the box, a second command, a whole-program compile, and it refuses
programs the interpreter runs fine.

| kernel | interp | `--exe -O` | hand-written C | interp → `-O` |
|---|---:|---:|---:|---:|
| 5M-iteration `while` over two `my` Ints | 0.92 s | 0.023 s | ~0.001 s | **40×** |
| Mandelbrot 150×130, `Num` throughout | 0.91 s | 0.060 s | <0.01 s | **15×** |
| `tools/bench/fib.raku` | 0.80 s | 0.030 s | | **25×** |
| `tools/bench/objects.raku` | 0.82 s | 0.320 s | | 2.6× |

The last row is the honest one: `objects` is dispatch-bound, and this plan does
nothing for it. [DISPATCH-PERF-PLAN.md](DISPATCH-PERF-PLAN.md) owns that.

This is **not** a new backend. It is the existing `-O` emission, applied to one
loop instead of a whole program, compiled in the background, and entered
mid-run.

## The two feasibility probes, and what they answered

Both were taken before any engine code, because both could have killed the
plan outright.

**1. Can a compiled kernel call the runtime of the process that loaded it?**
Yes, and with no change to the build. `tools/jit-probe.cpp` (a loop over
`rtAdd`/`rtLtB` plus a call to the non-inline `applyArith`) compiled against
`src/` headers, linked `-shared -Wl,-undefined,dynamic_lookup`, loaded into a
running `rakupp` through NativeCall, and returned the right answer:

```
12499997500000
50M boxed-runtime loop inside the rakupp process: 0.773 s
```

`nm -u` shows exactly two unresolved `rakupp::` symbols, both bound at load
time to the host executable's own copies. Mach-O exports executable globals by
default. ELF does not — Linux needs the `--dynamic-list` treatment
`CMakeLists.txt` already applies for the `rk_*` glob, widened or replaced with
`-rdynamic`. That is the one portability item this plan carries.

**2. Is the compile fast enough to be a JIT rather than a build?**

**Not as fast as the first probe said, and the correction changed the design.**
The probe's headline was 10 ms, and it was wrong: the compiles it timed had
FAILED, against a PCH that was stale because `Interpreter.h` had been edited
since. `clang` reports that in about 10 ms and exits 1, and the probe was not
checking exit status. Re-measured with the status checked:

| step | time |
|---|---:|
| `-O2` compile of a kernel TU, no PCH | 0.81 s |
| build a PCH of `Interpreter.h` (once per version+compiler) | 0.80 s |
| `-O2` compile of a kernel TU against that PCH | **0.40 s** |
| the same, measured again at the end of the batch: without / with | 0.83 s / 0.55 s |
| link the object into a `.dylib` | 0.03 s |
| a TRIVIAL TU against the same PCH — the floor | 0.36 s |
| the same kernel at `-O0` | 0.01 s |

The floor row is the one that matters: 0.36 s of the 0.40 is loading a 31 MB
PCH and standing up the optimizer, not compiling the loop. So the PCH halves
the compile and there is no further constant to remove — **a kernel costs about
0.43 s to produce, not 30 ms**.

Two things follow, and both are in the design below rather than bolted on
after it:

- **The compile must be in the background.** At 30 ms a synchronous compile
  would have been defensible; at 0.43 s it is not.
- **The on-disk cache is not an optimisation, it is the feature.** A program
  that runs for less than half a second cannot benefit from its own first run,
  so the cache is what makes the *second* run fast. Measured: with the cache
  cold, the Mandelbrot below is 0.41 s both ways; with it warm, 0.06 s.

The first draft got both of these wrong in the same way and measured 1.25×
instead of 31×, for three separate reasons that are worth recording because
each is a trap the next person will meet:

1. **The kernel's entry-point name contained the AST node's address.** The
   cache key is the kernel source, the name is in the source, and the address
   moves between runs — so the key changed every run, the cache never hit, and
   every run recompiled. Fixed by giving every kernel the same exported name
   and letting the file path distinguish them.
2. **The publish was a `rename()` in C++ after the compiler returned.** A
   program that exits before its own compile finishes takes the compile thread
   with it, and the rename never runs, so the work was thrown away every time.
   Fixed by making `&& mv` part of the shell command: the compiler is a child
   process that outlives us, so the run that paid for the compile still leaves
   the kernel for the next one.
3. **A kernel that could not bind its slots was retried every iteration** —
   the lookups, the guards and, under `--jit=verbose`, a line of output per
   iteration. Slower than simply interpreting. Fixed by retiring the site on
   the first refusal.

## Design

### The switch, and why everything hangs off it

`--jit` is off by default. One file-static `g_jitOn` in `main.cpp`, one
`jit::on()` inline reading it, and **every** new code path in the engine sits
behind that branch. A run without `--jit` executes one predictable,
never-taken branch per loop iteration and reaches no new code at all. The gate
below measures that cost against zero.

`--jit[=SPEC]`, comma-separated, following the `--slim=SPEC` precedent:

| spec | meaning |
|---|---|
| (bare `--jit`) | on, background compile, threshold 1000 |
| `off` | explicitly off (the default) |
| `sync` | compile on the interpreter thread — deterministic, for gates |
| `verbose` | narrate every tier-up decision to stderr |
| `stats` | one summary line at exit |
| `threshold=N` | iterations before a loop is considered hot |
| `nocache` | never read or write the on-disk kernel cache |
| `pch` | build a precompiled header — opt-in, see *The header is opt-in* |

Per [[rakupp-cli-flags]] the flag is accepted by every mode and position-
independently; it is *acted on* only where a program is interpreted. In
`--exe`, `--cpp`, `--target=js` and the check/lint modes it parses and is
ignored, because those modes never tier-walk anything.

### What tiers up, in v1

A loop node is a **candidate** if it is a `while`/`until` with no pointy
signature, or a C-style `loop`, not in expression position, not labelled. Its
subtree must pass a whitelist walk:

- **Statements:** `ExprStmt`, `IfStmt` (with `elsif`/`else`), `Block`,
  nested `WhileStmt`/`LoopStmt`, unlabelled `LastStmt`/`NextStmt`.
- **A comma list in a C-style loop's header only** — `loop ($r = $c, $i = $C,
  $k = 0; …; …)`, where the comma is a sequence of side effects and the list
  value is discarded. Added after the first landing, because it was the single
  thing keeping `examples/mandel.raku` entirely interpreted: with it, that
  file's innermost loop tiers up (90 → 60 ms whole-program, output byte-identical
  to both the interpreter and Rakudo). A `ListExpr` anywhere else is still
  refused — nothing here admits a list into a scalar slot.
- **Expressions:** integer/number/string literals, plain `$`-sigil variable
  references with no twigil, assignment and compound assignment to those,
  binary and unary operators, `++`/`--`, ternary.
- **Nothing else.** No calls, no method calls, no indexing, no regexes, no
  `return`, no `die`, no phasers, no `state`, no closures, no `$_`, no
  dynamics, no `for`.

That whitelist is deliberately far narrower than what `Codegen` accepts. It is
not a statement about what the technique can do — it is the smallest set that
makes the v1 **provably** unable to reach the interpreter's state while a
kernel runs, which is what "no chance to break the interpreter" means. It
covers both numeric kernels above. Widening it is per-node work with a gate
behind each step, and `for` over a Range is the first item.

### Why "no calls" is the load-bearing rule

A kernel binds its outer variables as `Value*` taken once at entry. Those
pointers must stay valid for the whole loop. A pad slot is stable by the
PADS-PLAN contract; a map entry is stable only while nothing inserts into that
`Env`. With no calls in the subtree, nothing in the kernel can insert into any
scope the kernel did not create, so both kinds of slot are pinned for the
duration. Every later widening of the whitelist has to answer this question
again, which is why it is stated here rather than in a comment.

### Slots and locals

A scope-aware walk over the subtree splits the names it mentions:

- declared inside (`my`) → a C++ local in the kernel, exactly as `--exe` emits
  it;
- mentioned but not declared inside → a **slot**, bound at entry from
  `Env::find`, in a stable order.

The walk is scope-aware rather than a flat set, because `while { $a = 1; if
… { my $a = 2 } }` has one of each under one name, and a flat set gets it
wrong in the silent direction.

### The kernel

```cpp
extern "C" int rakupp_jit_N(rakupp::Interpreter* I, rakupp::Value** slots);
```

Body: `Value& v_ssum = *slots[0];` per slot, then the loop statement emitted by
today's `Codegen` with `optimize_ = true`. `last`/`next` compile to
`break`/`continue` inside the kernel's own loops, which is what `loopDepth_`
already does. The kernel always runs to completion, so v1 returns 0 and has no
deopt path: every `-O` lane carries its own boxed fallback inline, so a guard
miss falls back **within** the kernel instead of leaving it.

### Tier-up, and picking the right loop

Each candidate carries a counter and a published kernel pointer. The
interpreter maintains a stack of executing candidates. When a counter trips,
the **outermost** candidate on that stack is the one compiled — in a loop nest,
the inner loop is what gets hot and the outer loop is what is worth compiling.
The outer loop picks up the pointer at its next iteration boundary, which is
on-stack replacement for free: a `while`/`loop`'s entire state lives in the
variables, so entering at any boundary is entering at the top.

### Background compile

Emit and check on the interpreter thread (microseconds). Then a detached
`std::thread` that touches no interpreter state: write the TU, build the PCH if
absent, run the compiler, link, `dlopen`, publish the pointer with a release
store. One compile in flight at a time. The manager is deliberately leaked, so
a compile finishing during shutdown cannot race a destructor.

### On-disk cache

`~/.cache/rakupp/jit/<xx>/<hash>.dylib`, beside the precomp cache, keyed on the
kernel source, the rakupp version and the compiler identity. Written through a
temp file and an atomic rename, so a killed compile leaves no half-file. Second
and later runs load the kernel at first loop entry instead of compiling it.
`--jit=nocache` bypasses both ends.

## What this costs the first run

| | |
|---|---|
| the counter | one relaxed increment per iteration against ~180 ns of interpreted iteration |
| emit + whitelist | microseconds, on the interpreter thread, once per loop |
| compile + link | ~0.43 s of CPU, on another core, never waited for |
| the PCH | 0.80 s, once per version+compiler, in the compile thread |

**Measured, against the pre-JIT binary built from the same commit**, interleaved
best-of-7 on eight kernels including a 5M-iteration loop: **no difference on any
of them**. A 50M-iteration loop — 50 million evaluations of the hook's
never-taken branch — reads 22.91 s with the hook and 22.94 s without, so the
cost is below a ~0.9% noise floor and the sign of the difference is not even
stable.

What a first run gets is another matter, and it is honest to state it plainly:
a program shorter than about half a second finishes before its own kernel is
ready and is exactly as fast as it was. The 5M loop (0.93 s interpreted) does
benefit on its first run; the Mandelbrot (0.41 s) does not. Both benefit on the
second, from the cache.

## What it does NOT claim

- **It is not faster than `--exe -O`.** v1 is the same emission, so a tiered
  loop lands on the `--exe -O` row and not below it. It can be marginally
  *slower* on a call-heavy loop, since a v1 kernel stops at the loop boundary
  while `--exe` also compiles the callee.
- **The 100× needs the next step.** Today's `-O` output keeps the box and
  writes `.i` back per statement behind a tag guard. Guarding the slot tags
  once at kernel entry, running the body on C++ locals and storing back at
  exit is native-math phases 3–4 — "unboxed for the extent of a loop, a `Value`
  built only where it escapes" — expressed in emitted C++, where a local *is*
  the non-escaping slot and clang does the register allocation.
  [IR-EXPERIMENT.md](../experiments/IR-EXPERIMENT.md)'s objection (a register
  file is addressable by construction) does not apply to a C++ local. That step
  lifts `--exe -O` by the same factor on the same day, and it is where the
  5 ms row comes from.
- **Speculation is the part `--exe` can never have**, and it is not in v1. At
  tier-up the engine knows what the slots held for the last thousand
  iterations; an AOT compile does not. A `Num`-only Mandelbrot kernel, a
  hoisted variable static analysis cannot prove numeric, a monomorphic multi
  call turned into a guarded direct call — that is where a JIT passes an AOT
  compiler on a dynamic language, and it needs the deopt path v1 does without.

### The header is opt-in, and the default cache is small

A precompiled header of `Interpreter.h` takes a kernel compile from 0.83 s to
0.55 s. It is also **31 MB, per build of rakupp**, and the first version of this
work had it on by default. One afternoon of rebuilding left three of them —
91 MB — in `~/.cache/rakupp/jit`, which is how the defect was found: the user
saw the directory, not a benchmark.

The trade is bad for the ordinary case and good for the unusual one. A program
with one hot loop pays 31 MB to save 0.28 s, once, in the background, where
nobody is waiting. A sweep that compiles hundreds of kernels saves minutes. So
it is `--jit=pch`, off unless asked, and building one now removes any header an
earlier build left, so the directory holds at most one.

What the default leaves behind is a kernel per hot loop at about 50 KB: the
710-program gate corpus produces 40 of them, just under 2 MB. `--jit-info`
reports it and `--jit-clean` empties it, mirroring `--precomp-info` /
`--precomp-clean`.

The general lesson is worth keeping separate from the specific one. **An
optimisation that spends the user's disk is not free just because it is
measured in time**, and the measurement that justified this one — halve the
compile — never looked at what it cost.

## Where it stands

Measured 2026-09-19 on the benchmark machine (Darwin 25.5, arm64, Release
`-O3 -DNDEBUG`, Apple clang), load 2–3, minimum of five runs. The `--jit`
column is with the kernel cache warm, which is the second and every later run
of a program.

| | interp | `--jit` | `--exe -O` | Rakudo |
|---|---:|---:|---:|---:|
| 5M-iteration integer `while` | 1050 ms | **50 ms** | 30 ms | 760 ms |
| Mandelbrot 150×130, `Num` | 410 ms | **60 ms** | 40 ms | 240 ms |

21× and 6.8× against the interpreter, landing just above `--exe -O` as
predicted — the residue is the interpreted prefix before the kernel is entered
plus the `dlopen`. Against Rakudo the same two kernels read 15× and 4×.

**Gates run.** `t/jit/run.raku`: 708 programs of `t/regression` and `examples`,
each run twice by the same binary, plain against `--jit=sync,threshold=0,nocache`
— **0 disagreements**, plus 11 of 11 of the JIT's own cases confirmed to enter a
kernel rather than passing by not compiling. `t/run.raku`: **1042 checks, the
same 4 failures the pre-JIT binary at this commit has** (`assoc-index-on-scalar-dies`,
`conformance-phase1-repr`, `itemization-and-list-folds`,
`slices-trans-and-exact-sequences` — all four byte-identical between the two
binaries), plus 10 new CLI goldens.

**The whole of Roast, both ways**, 1,464 files at `--jit=threshold=0` against a
plain run of the same binary:

| | plain | `--jit=threshold=0` |
|---|---:|---:|
| files fully passing | 737 / 1464 | **737 / 1464** |
| partial / no-TAP / timeout | 616 / 100 / 11 | **616 / 100 / 11** |
| assertions, of all declared | 202250 / 219602 | 202249 / 219602 |
| wall time, 6 workers | 63.8 s | 76.8 s |

The per-section table is identical but for one line, and the one assertion is
`S32-list/pick.t`, which is **flaky against itself**: five consecutive PLAIN
runs of it scored 318, 318, 318, 318, 316. It is a sampling test over `pick`
and `roll`, and the JIT never touched it.

The 20% wall-time cost is `threshold=0` doing what it is for — compiling every
eligible loop in the suite, most of which run a handful of times — and is not
what `--jit` does at its default threshold. It is the price of the gate, not a
property of the feature.

**One divergence this work DOES introduce**, found by forcing a throw from
inside a kernel: an *uncaught* error is reported at the loop's line rather than
the failing statement's, because a kernel does not advance `curLine_` per
statement and pointing it at the loop once per entry is the honest answer
available at that price. `--exe` prints no line at all for the same program.
A *caught* exception is unaffected — it unwinds out of the dlopen'd image and
reaches its `CATCH` with every variable correct, which `t/jit/cases/throws.raku`
pins against the interpreter.

**One divergence found and NOT caused by this work**, recorded because the gate
surfaced it: `my int $n` does not wrap at 64 bits here (it promotes to a bignum
where Rakudo wraps), so `my int` carries no `natBits` and a kernel is allowed to
write it. The narrower widths do carry it and are correctly refused — `my int8`
over 300 increments answers 44 under the interpreter, under `--jit` and under
Rakudo. The 64-bit gap is the interpreter's, and predates the JIT.

## Gates

Standing gates on every batch, plus three this plan adds:

1. **`t/jit/run.raku`** — every eligible program run twice, `--jit=sync`
   against the plain interpreter, stdout/stderr/exit compared byte for byte.
   The interpreter is the oracle, as it is for the JS backend.
2. **Roast with the threshold at zero** (`RAKUPP_JIT_THRESHOLD=0 --jit`), so
   every eligible loop in the suite tiers up on entry. The suite becomes the
   JIT's differential test.
3. **The off-path costs nothing.** `perf-guard` without `--jit` against the
   pre-JIT binary, on the quiet machine. A regression there is a stop.

## Prior art, and why not the alternatives

**Copy-and-patch** (CPython 3.13) builds stencils at compile time and patches
them at run time, so it needs no compiler on the box and costs microseconds
instead of 30 ms. It is the better *second* tier. It is not the first one here:
it is two architectures and two object formats of new code, it needs
executable-memory handling (MAP_JIT, W^X, cache invalidation, the notarization
entitlement), and `die` is a C++ throw that cannot cross machine code with no
unwind tables — a constraint on every helper a stencil may call. The dylib
route reuses a code generator that already exists and leaves a `.cpp` a human
can read.

**A register IR or a bytecode VM** was measured and not adopted
([IR-EXPERIMENT.md](../experiments/IR-EXPERIMENT.md)): flat dispatch is worth
0.4–0.7% of a node visit. Nothing in this plan contradicts that file — the win
here is not dispatch, it is that a compiled loop skips the per-iteration
machinery *between* nodes (the loop runner, the block re-scan, the
thread-local access) that the native-math revisit priced at roughly half an
iteration, and which a per-node fast path cannot remove.

**Compiling grammars** caps at ~2× (V5-IDEAS §3).

## Phases

| | | |
|---|---|---|
| **P0** | the switch, the counter, the whitelist, emission, sync compile, `dlopen`, the `t/jit` gate | **DONE** |
| **P1** | background compile, the on-disk cache, the PCH | **DONE** |
| **P2** | Linux: the dynamic-list widening, and the gate leg | next |
| **P3** | unboxed slot hoisting — the 100× step, shared with `--exe -O` | its own plan |
| **P4** | whitelist widening: `for` over a Range, then calls to user subs (which reopens the pointer-stability question), then sub kernels | per-item gates |
| **P5** | type feedback and a deopt path | needs P3 |

### What P2–P5 inherit from this batch

- **Linux (P2).** The kernel resolves its runtime symbols against the
  executable. Mach-O exports them by default; ELF does not, and
  `CMakeLists.txt` already narrows rakupp's dynamic list to the `rk_*` glob for
  the extension loader. That list has to grow to cover what a kernel calls, or
  the kernel has to link `librakupp_rt.a` instead of borrowing the host's copy —
  the second is simpler and costs binary size per kernel. Untested either way;
  nothing here has run on Linux.
- **The whitelist is one function** (`Scan` in [src/Jit.cpp](../../../src/Jit.cpp))
  and every widening is a case added to it plus a case in `t/jit/cases`. The
  ordering of value comes from `--jit=verbose` over a real corpus, which nobody
  has collected yet: that histogram is the first thing P4 should produce.
- **The rule P4 must not break.** No kernel may call anything, because that is
  what pins the slot pointers for the duration of the loop. The first widening
  that admits a call has to answer pointer stability again — by re-binding at
  each call boundary, by restricting to calls that provably cannot declare into
  an enclosing scope, or by giving up map-stored slots and taking pad slots
  only, which the PADS-PLAN contract already guarantees never move.
- **P3 is where the headline is.** A kernel today is byte-for-byte the `-O`
  emission, so it inherits both its speed and its ceiling. Guarding the slot
  tags once at kernel entry and running the body on C++ locals is the step that
  takes the 5M loop from 30 ms toward the 1 ms the hand-written C kernel
  measures — and it lifts `--exe -O` by the same factor on the same day, since
  it is the same emitter.

