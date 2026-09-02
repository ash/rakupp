# Measuring, and Proving

Almost every decision in this book was made against a number. This closing
chapter is about how those numbers are produced, why several of them were wrong
the first time, and what a release has to pass.

## The benchmark policy

Stated once, applied everywhere:

- **interleave the variants.** Run A, B, A, B — not all of A then all of B — so
  machine drift and thermal state hit both equally.
- **discard a warm-up**, then report the median or the minimum of several
  repetitions. Which one depends on the noise shape; the important thing is to
  say which.
- **keep a control kernel** that the change under test cannot possibly affect.
- **state the conditions**: machine, architecture, compiler, date.

- **prefer a load-independent metric when the machine is not yours to quiet.**
  `/usr/bin/time -l` reports **instructions retired** and **cycles elapsed** on
  macOS 12 and later. Instructions retired does not move when a browser or a
  build is running alongside, which is what makes a developer box measurable at
  all — and it is what `perf-guard --check` cannot offer, since it refuses to
  report under load rather than reporting something misleading.

Read the two counters together, because the pair says more than either:

| reading | meaning |
|---|---|
| instructions down | work was removed; the size of the drop is the size of the win |
| instructions flat, cycles down | the same work done more efficiently — better locality, a bulk `memcpy` instead of a loop |
| instructions up, wall clock down | suspect code layout, and say so rather than quoting the wall clock |

One addition to the control rule, learned the hard way: the control should be a
**control binary**, not only a control kernel. Build it from the same tree with
just the line under test reverted. A rename, a new header, an inlining decision
that shifts a 30,000-line translation unit — any of these moves code layout by
itself, and comparing against the last release charges that to the change.

The control is the part people skip and the part that carries the argument.
Chapter 19's table has a row for a pure method-dispatch loop, containing nothing
node specialisation can touch:

| kernel | base | specialised | delta |
|---|---:|---:|---:|
| vars | 840.8 ms | 686.5 | −18.3% |
| fib | 478.6 | 422.4 | −11.7% |
| **ctl — method dispatch only** | 327.9 | 327.0 | **−0.3%** |

Without that last row the others are only evidence that the machine was quieter
the second time.

## The performance gate

```sh
build/rakupp tools/perf-guard.raku --check
```

`perf-guard` compares the current binary against a recorded baseline and
**fails** on a regression. The release checklist gates on it.

That it exists at all is a lesson learned twice: a release has shipped with a
performance regression because someone eyeballed the numbers and thought they
looked fine. Eyeballing does not work — the noise band is a few per cent, the
regressions that matter are often a few per cent, and human judgement about
which is which is unreliable at exactly that scale.

The rule that follows: **a performance change is an interleaved A/B against
`perf-guard`, not an opinion.**

`perf-guard` has one honest limitation: it refuses to report on a machine under
load, by its own detector, which on a developer's daily box means most of the
time. That refusal is correct — a gate that reports noise is worse than one
that reports nothing — but it leaves a gap, and the instruction-count A/B above
is what fills it. The gate still owns the release; the counters own the day-to-day.

## Three kernels, three resolutions

The number that makes the previous section usable is not a ratio at all. It is
the **spread of a kernel measured against itself**: the same unchanged binary,
run eight times.

| kernel | spread over eight runs |
|---|---|
| `fib` | 6.4093–6.4119 G instructions, ±0.02% |
| grammar JSON parse | 1.8493–1.8839 G, ±1.9% |
| `streq` | 4.90–5.00 G, ±2% |

Two orders of magnitude between the tightest and the loosest, on the same
machine, in the same minute. The mechanism is the parallel runtime: its worker
threads do variable amounts of work, so the variance lands on kernels that
allocate and thread and not on tight arithmetic loops. A 1% ratio measured on
`fib` is a finding. A 1% ratio measured on the grammar parse is nothing at all.

**Measure a kernel's spread on one binary before trusting any ratio from it.**
The next section is what happens when you do not.

## The correctness gates

| Gate | What it is |
|---|---|
| **Roast** | the Raku specification suite, run by a Raku harness |
| **the local suite** | examples and showcases, byte-compared against golden output |
| **regression tests** | one file per fixed bug |
| **stress tests** | concurrency and memory, also under TSan and ASan |
| **compiler agreement** | every deterministic example must produce identical output interpreted, `--exe`, and `--exe -O` |
| **the second FFI leg** | the whole suite run again with `RAKUPP_FFI=0` |

Compiler agreement is the one that catches the most. Three execution paths must
produce **byte-identical** output for the same program; a divergence is a bug in
one of them, and it is found automatically rather than by someone noticing.

Roast is reported two ways, and the difference matters. **Files fully passing**
is an all-or-nothing bar; **assertions passing** gives partial credit. Roughly
ninety per cent of declared assertions pass. Both numbers are published, because
either alone is misleading — a file can fail on one obscure assertion out of two
hundred, and a file can "pass" by skipping everything.

## Oracles

A test needs something to be right *against*. Four are used, in decreasing order
of authority.

**Roast.** The specification. If Roast asserts it, it is the answer.

**Rakudo.** For anything Roast does not cover, the reference implementation is
run side by side. The documentation harness does this in bulk: every documented
example, on both engines, three-way classified.

**The previous rakupp binary.** For a change that is supposed to be
*semantically invisible* — an optimisation — the baseline binary is the oracle,
not another implementation. That habit came directly from a bug: the first
`evalIndex` fast path wrapped negative subscripts from the end, and
`my $i = -1; @a[$i]` must throw. Rakudo could not have caught it, because Rakudo
rejects that spelling at compile time. Diffing against the previous binary did.

**The language being implemented.** The showcase interpreters compare their
output against `node`, `perl`, `python3` and the real Lisp — which is an oracle
for thousands of lines of Raku that nobody wrote assertions for.

## Error messages are not behaviour

A rule with a sharp edge: **do not copy Rakudo's message prose unless Roast or
the documentation asserts it.**

Behaviour must match. Message wording need not, and chasing it produces churn
that no test protects. Where a diagnostic *is* asserted, it is asserted by
exception class and attributes rather than by text — which is why typed
exceptions are built with attributes rather than formatted strings
(Chapter 15).

## What has and has not paid

The pattern across every optimisation in this book is consistent enough to state
as a finding.

**What paid** — all of it removing work or allocation:

| Change | Effect |
|---|---|
| copy-on-write strings | removed a quadratic; 142 ms to 24 ms on a copy benchmark |
| the property cache on the string body | removed the *other* quadratic |
| interned tag fields | removed a constructor, destructor and copy from every `Value` |
| the packed-prefix name comparison | 60% of a profile to 8.5% |
| `strtod` instead of a caught `stod` | removed a C++ throw from the hottest path |
| node specialisation | removed a `Value` copy and a literal rebuild per evaluation |
| direct-arity calls | removed the per-call `ValueList` — in compiled code |
| the small-block free list | removed the same allocation in *interpreted* code: 32.35 ns to 9.48 |
| one-pass relocating list growth | removed a whole second walk over the buffer |
| native integer lanes | removed the per-operation `Value` |
| the key-once sort | removed an asymptotic factor |

**What did not pay:**

- **constant folding** — measured first, with a corpus of 51,353 nodes; 0.07% of
  nodes, none of it in a loop. Not built.
- **link-time optimisation and `-mcpu=native`** — measured, no effect.
- **`-Os`** — smaller by nothing that matters, slower by 20 to 50% of the lane
  speed-up.
- **a dispatch table for the method ladder** — would chase 8.5% of a profile, and
  is not a drop-in because the ladder is guarded and order-sensitive.
- **a flat threaded execution loop** — perl's `run.c` in one line, and the most
  frequently proposed change to any tree-walker. Measured twice, a month apart:
  the opcode `switch` is worth 0.32 ns against a node visit costing 46 to 85.
  Under one per cent. Chapter 41.

The generalisation: **on this codebase, removing an allocation has always paid
and removing a branch almost never has.** That is a property of a tree-walker
over a fat value type, and it is worth re-deriving before assuming it holds
somewhere else. The two cleanest witnesses sit at opposite ends of the two
lists above and were measured within days of each other: the free list is pure
allocation removal and paid; the threaded loop is pure branch removal and did
not.

## Four ways the measurement itself was wrong

Worth more than the successes.

**Benchmarking a rename instead of the change it enables.** An early analysis
concluded that turning a builtin into a named C++ function was worth about 1
nanosecond per call. That was true for an out-of-line function still taking a
`ValueList` — and completely missed what a real named function unlocks: direct
`Value` arguments, no allocation, and an inlinable body. The corrected
measurement was 5.6 times on an `abs` loop (Chapter 28).

**Restructuring the shared path while adding a fast one.** The first node
specialisation made the *control* 5.7% slower, because binding an operand
through an optional cost the general path its copy elision. The rule extracted:
add an early exit, never restructure the code underneath it.

**Assuming where the cost was.** "I/O dominates, so call plumbing does not
matter" was false: a buffered one-argument `say` costs about 125 nanoseconds, of
which the plumbing was 45%. And the method dispatch ladder was exonerated twice
by a correct observation — that a method 177 comparisons in cost the same as one
812 in — before a profiler showed the cost was `strlen` on a literal, not the
ladder's length.

**Two agreeing single runs, agreeing on a wrong number.** A string-representation
change was published as an 8.9% win on grammar JSON parsing. It is 0.1%. The
measurement was one run per binary; it was taken twice, on different corpora,
and both times said 8 to 9 per cent, which felt like confirmation. It was not.
The grammar parse spreads ±1.9% on an unchanged binary, and the two readings
had agreed on a value about 12% above the kernel's own floor — an agreement
between two draws from the same wide distribution, which is exactly what
independent confirmation is supposed to rule out and does not, when the draws
share a bias. Best of five put the real figure at 0.1%, and the change's honest
case turned out to rest on a different kernel entirely.

The rule that came out of it is the previous section: the spread is a property
of the kernel, it varies by two orders of magnitude across the suite, and it
has to be measured before any ratio from that kernel is quoted. Repeating a
measurement is not the same as repeating it enough.

## Where the remaining time is

For an interpreted method-heavy loop, after everything above:

| | share |
|---|---:|
| heap allocate and free | 31% |
| `Value` copy and destroy | 11% |
| method-name comparison | 8.5% |
| the dispatch function's own body | 6.1% |

Forty-two per cent in allocation and value churn. The shape of the fix looked
known when this was written: pass the invocant and argument list by reference
rather than by value, and shrink `Value` — both to be approached carefully
rather than quickly, because the first trades away an accidental safety
property and the second is a representation change that the extension ABI was
specifically designed to survive (Chapter 36).

Half of that came true and half did not, which is the interesting part.
`Value` was shrunk twice, 344 to 208 to 128, and the ABI did survive it exactly
as designed. The argument list was never passed by reference — the 178-call-site
aliasing audit is still undone — and the cost was taken anyway, from a third
direction nobody had written down: making the allocation itself nearly free
(Chapter 12), which needed no audit at all.

That is the more useful lesson to carry out of this chapter. A cost can be
correctly identified, and the *shape of the fix* can still be wrong. "Pass it
by reference" and "make the allocation cheap" address the same 31% and have
nothing else in common: one is a 178-site audit of what may alias, the other is
sixty lines in one header. Naming a cost is worth more than naming its cure.

## Honesty as a practice

Every chapter in this book has a "honest limitations" section, and that is a
deliberate convention rather than a stylistic tic.

The list for the project as a whole: about ninety per cent of Roast; depth-first
method resolution rather than C3; no ambiguity error in multiple dispatch;
role composition that is last-writer-wins; modules that publish their whole
environment; a flat class registry; no macros, no `RakuAST`, no slangs; laziness
capped at a million elements; a `gather` block that can run more than once; a
reference cycle that leaks.

Every one of those is a real difference from the reference implementation, and
every one is written down where someone hitting it would look. That is the
difference between a document and a brochure, and it is the reason the divergence
lists in `docs/dev/findings/` are maintained as running logs rather than
occasionally cleaned up.

A last observation, which is the closest thing this book has to a thesis. The
mechanisms in it are mostly ordinary — a Pratt parser, a tagged struct, a
backtracking matcher, a transpiler. What made them work was not cleverness. It
was measuring before building, keeping a control, writing down the reason next
to the code, and being specific in public about what does not work yet.
