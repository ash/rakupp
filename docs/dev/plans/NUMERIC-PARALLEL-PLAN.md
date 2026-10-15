# Plan: parallel numeric Raku — the hyper forms at the hardware's speed

*Written 2026-10-15, before any code, as a v7 pillar ([VERSIONS.md](VERSIONS.md)).
[PARALLEL-PLAN.md](PARALLEL-PLAN.md) made threads the default and safe;
[PARALLEL-SCALING-PLAN.md](PARALLEL-SCALING-PLAN.md) made `start` scale. This
plan is about data parallelism over numbers. Raku already has the forms for it:
the hyper operators, `[op]` and `[\op]`, `.hyper`/`.race`, `hyper for`/`race
for`, and native `int`/`num` arrays. Their semantics leave the order open, so the
language allows all of them to run in parallel. Raku++ already runs some of them
in parallel. The plan is to make all of them run near what the hardware can do,
without new syntax.*

*What other languages do, and what Raku borrows from them:*

- *Julia's fused broadcasting.* `@. y = a*x + y` is one loop with no
  temporaries.
- *Chapel's reductions and scans as expressions.* `+ reduce A`, `+ scan A`, and
  `forall … with (+ reduce s)`.
- *OpenMP's `reduction(+:s)` and Cilk's reducers.*
- *Rayon's `par_iter`.* Changing one word, and nothing else, turns a loop
  parallel.

*Raku's versions of these are `»op«`, `[op]`/`[\op]`, `race` and `.hyper`.*

**Measured:**

- **When and where:** 2026-10-15, Apple M4 (4 performance + 6 efficiency
  cores).
- **Binary:** `5.4.1-66-g49c98013-modified`, the working tree with another
  session's uncommitted edits. It was copied aside so that it stayed one binary
  for the whole sitting.
- **Load average:** 2 to 4, so the machine was not quiet. Times are best of 3,
  interleaved across forms, and the ratios matter more than the absolute
  times.
- **Data:** every table is over 10⁷-element arrays unless it says otherwise.
- **Re-measuring:** the [appendix](#appendix-the-measurement) has the
  scripts. P0 moves them into `tools/bench/parallel/`.

---

## The numbers a stranger can re-measure

The data is two `num` arrays of 10⁷ elements, `@a` and `@b`. "C" means a plain
C loop over `double*` at `-O2`, measured in the same sitting with the same
number of threads.

1. **Dot product: `[+] (@a »*« @b)` within 2× of C.** Today it takes 317 ms.
   That is 62× C on one thread (5.1 ms) and 210× C on ten threads (1.5 ms).
2. **Element-wise into a native array: `my num @c = @a »*« @b` within 2× of C,
   with no boxed intermediate.** Today it takes 193 ms (C: 2.5 ms), and its
   peak memory is the 800 MB of boxed results.
3. **A reduction that leaves the array packed: `@a.sum` within 2× of C.** Today
   it takes 80 ms (C: 5.2 ms), and it unpacks the array: 86 MB → 849 MB, for
   good.
4. **A worker loop over native arrays: `hyper for ^N -> $i { @c[$i] = f(@a[$i])
   }` at least 3× its serial `for`, with both arrays still packed afterwards.**
   Today it is 1.0–1.2×, and both arrays unpack.
5. **The closure tax: `.hyper.map(&f)` at its default degree uses at most 1.5×
   the CPU of `.map(&f)`.** Today it uses 4.6×. It is also slower at degree 9
   than at degree 3.

---

## Where it stands — measured

### The ceiling: C over `double*`, 10⁷ elements

| | 1 thread | 4 threads | 10 threads |
|---|---:|---:|---:|
| dot `s += a[i]*b[i]` | 5.1 ms | 2.2 ms | 1.5 ms |
| `c[i] = 2*a[i] + b[i]` | 2.6 ms | 2.5 ms | 2.5 ms |
| `c[i] = sqrt(a[i] + 3)` | 2.3 ms | 1.6 ms | 1.7 ms |
| sum `s += a[i]` | 5.2 ms | | |

The element-wise rows are memory-bound by 4 threads. The two reductions are
latency-bound on one thread, because `-O2` keeps the floating-point adds in
order: one dependent add per element. That is also the honest ceiling for a
bit-exact serial `.sum` (see P3).

### Raku++, the same work

| Form | Time | Threads | What happens |
|---|---:|---:|---|
| `@a.sum` | 80 ms | 1 | unpacks the array, then folds the boxed Values |
| `[+] @a` | 333 ms | 1 | |
| `[max] @a` | 738 ms | 1 | |
| `my @c = @a »*« @b` | 62 ms | 10 | kernel; 87 ms on one thread; 800 MB of boxed results |
| `my num @c = @a »*« @b` | 193 ms | 10, then 1 | boxed result, then packed again |
| `my num @c = @a »*» 2e0 »+« @b` | 234 ms | 10 | two kernel passes, both boxed, then packed |
| `my @c = (@a »+» 3e0)».sqrt` | 106 ms | 10 | 194 ms on one thread |
| `[+] (@a »*« @b)` | 317 ms | 10, then 1 | 343 ms on one thread |
| `[+] (@a Z* @b)` | 1,229 ms | 1 | `Z` has no kernel |
| `for ^N -> int $i { $s += @a[$i] * @b[$i] }` | 2,538 ms | 1 | interpreted |
| `loop (my int $i …) { … }`, the same body | 2,087 ms | 1 | interpreted; `--cnp` refuses native arrays |
| the same loop split over `start` ×4 | 752 ms | 4 | 2.8× |
| the same loop split over `start` ×10 | 520 ms | 10 | 4.0× |
| `@a.map({ ($_ + 3e0).sqrt })` | 2,774 ms | 1 | |
| `@a.hyper.map(…)` | 1,455 ms | 9 | 1.9×; `:batch(65536)` the same |
| `hyper for @a { ($_ + 3e0).sqrt }` | 714 ms | 9 | 3.9× |
| `@a.race.map(* * 2e0).sum` | 514 ms | 9, then 1 | builds the 10⁷-element list, then folds it serially |

### What the probes found

**The hyper kernels are real, and they are the best thing here.**

- **Where they live:** `@a »op« @b` over plain Ints and Nums runs as a native
  loop
  ([InterpreterRegex.cpp:3593–4001](../../../src/InterpreterRegex.cpp)) on a
  resident pool (`HyperPool`) once a list reaches 2 × 16,384 elements.
- **What they cover:**
  - the operators `+ - * /` and the six comparisons;
  - the methods `.sqrt`, `.abs`, `.exp` and the trigonometric ones.
- **What they read:** packed words directly.
- **What they write:** boxed 80-byte Values, always.
  - The pool stops scaling at two threads. `@a».sqrt` takes 82 ms on one
    thread, 60 ms on two, 61 on four and 57 on ten: the time goes into building
    800 MB of Values, not into arithmetic.
  - C writes the same answers in about 2 ms.
  - The bottleneck is the representation of the result, not the thread
    count.

**Element-by-element and serial, though the language allows parallel:**

- the operators `**`, `%`, `div`, `mod`, `min`, `max` and `atan2`;
- the prefix `-«` and the methods `.log`, `.floor` and `.round`;
- every `Z` and `X` metaop;
- `[\+]`;
- user code in a hyper: `».&f` and `»my-op«`, even with `is pure`. The trait is
  parsed ([Parser.cpp:13454](../../../src/Parser.cpp)) and nothing reads it;
- junction autothreading.

**Reductions are serial and boxed.**

- **`.sum`, `.min` and `.max` over plain arrays** fold in place
  (`borrowedNumericFold`,
  [MethodCallTail.cpp:525](../../../src/MethodCallTail.cpp)).
- **Over a packed array they call `arr()`, which unpacks it.** A `num @a[10⁷]`
  read 86 MB before `.sum` and 849 MB after. The first call took 123 ms and the
  second 37 ms. Reductions over the words are
  [PACKED-ARRAY-PLAN.md](PACKED-ARRAY-PLAN.md) batch 3, still open.
- **`[+]` and `[max]` take slower general paths than `.sum`.** They were 4× and
  9× slower in the table above.

**Loops over native arrays run interpreted, at about 200 ns an element.**

- The default loop kernel declined every shape tried: "variable $i (a
  declaration or a special form)".
- `--cnp` compiles the scalar loops at 4 ns an iteration. It refuses one that
  reads a native array: "array @a is not a plain Array (typed, native, shaped,
  lazy or a List)" ([Jit.cpp:1647](../../../src/Jit.cpp)).
- So every numeric worker loop, in `start` or `hyper for`, is interpreted.

**Writing results from workers unpacks the arrays.**

- **The rule:** the packed-store lane and packed `.push` turn off while workers
  are live, because a word written there could race an unpack
  ([InterpreterCalls.cpp:3747](../../../src/InterpreterCalls.cpp),
  [Builtins.cpp:6263](../../../src/Builtins.cpp)).
- **The cost, over two 2×10⁶ arrays:** `hyper for ^N -> $i { @p[$i] = @a[$i]
  * 2e0 }` took the process from 193 MB to 522 MB. That is both arrays unpacked.
- **The speed:** 0.209 s against 0.244 s serial.
- **The same loop over boxed arrays scales:** 0.593 → 0.197 s. It takes
  3,000,000 stripe acquisitions per 10⁶ iterations, with 0.01% of them
  waiting.

**Growing a native array by index unpacks it.**

- Filling two empty `num` arrays to 10⁷ with `@a[$i] = …` took 6.5 s and
  peaked at 2.5 GB.
- `my num @a = (^$N) »*» 1e-7` took 0.14 s, but peaked at 890 MB, because it
  goes through the boxed kernel result.

**`race for` with an outer accumulator silently loses updates.**

- **The probe:** 2×10⁵ iterations of `$s += $i.Num`.
- **Num:** 8,512,272,888 instead of 19,999,900,000.
- **`int`:** 6,238,110,399.
- **Why:** this is undefined behaviour under the memory model
  ([guide/ASYNC.md](../../guide/ASYNC.md)), and it is the first thing a numeric
  programmer writes.
- **What works today:** `my atomicint` with `⚛+=` is correct, and so is
  `.race.map(…).sum`. There is no atomic Num.

**Closures pay a large tax on workers.** Over 4×10⁶ elements with a `sqrt`
body:

| | Wall | CPU |
|---|---:|---:|
| `.map(…)` | 0.95 s | 0.99 s |
| `.hyper(:degree(3)).map(…)` | 0.54 s | 1.31 s |
| `.hyper(:degree(9)).map(…)` | 0.63 s | 4.56 s |
| `for … { my $x = … }` | 1.16 s | 1.23 s |
| `hyper for … { my $x = … }` (sink) | 0.49 s | 3.40 s |
| `my @r = hyper for … { … }` | 0.24 s | 1.56 s |

A `sample` profile of `.hyper(:degree(9)).map` over 3×10⁷ elements, ordered by
time at the top of the stack:

1. `callCallableRaw`: high self time, which looks like shared reference counts
   bumped from nine threads.
2. `RVec::growAndBuild`: called from `hyperSeqCall`'s per-element lambda.
3. `__ulock_wait`: waits under the allocator's free lists.
4. `PooledFrame::~PooledFrame`.
5. `bindParams`.

What `runHyperLoop` does per iteration that allocates:

- it builds a `std::function` for `redo`
  ([InterpreterModules.cpp:3398](../../../src/InterpreterModules.cpp));
- it clears the worker's scope;
- it defines `$/` again.

**Each run spawns its threads.**

- **`start`:** each one is a new OS thread
  ([Interpreter.cpp:6198](../../../src/Interpreter.cpp)). `await start { 1 }`
  costs 16 µs.
- **`.hyper` and `hyper for`:** each run spawns `degree` threads.
- **The cost on short lists:** 1,000 runs over 640 elements took 115 ms with
  `.hyper.map` against 45 ms with `.map`, and 106 ms with `hyper for`.
- **`hyper for`'s batch is fixed at 64**
  ([InterpreterModules.cpp:3378](../../../src/InterpreterModules.cpp)).

**Every pool counts the efficiency cores as full ones.**

- **The hyper kernels:** their pool cuts the list into equal static chunks, one
  per hardware thread, so the slowest efficiency core sets the time.
- **`runParallel`:** it already hands out batches dynamically. Its default
  degree is all cores less one, six of them efficiency cores here.

---

## What can run without the interpreter

No user code can run in any of these, so they can be split over threads and
reordered freely. The result has to be bit-identical to the generic path, or
the rule for floats in P3 has to say otherwise:

- an expression tree of built-in numeric operators and methods, whose leaves are
  packed arrays, Int ranges, plain lists of machine Ints and Nums, and scalars;
- `Z op`, with a built-in `op`, over equal-length operands: the same loop as
  `»op«`;
- reductions with a built-in associative operator (`+ * min max minmax`), over
  a leaf or over such a tree;
- the scan `[\+]`, as a two-pass parallel prefix sum.

Anything that calls a closure needs the interpreter on the worker, and pays the
costs above: allocation, shared reference counts, stripes, and thread spawns.

---

## Phases

Ordered by payoff against complexity. P1 and P2 take out the largest factors
without any threads: the kernels are bound by their output, not their thread
count. P3 multiplies what P1 and P2 leave.

| Phase | What | Complexity | The gap it closes |
|---|---|---|---|
| P0 | Benches, the C ceiling, the unpack census | S | — |
| P1 | Packed in, packed out; reductions over the words | M | `.sum` 80 → ~5 ms with no unpack; `my num @c = @a »*« @b` 193 → ~5 ms |
| P2 | Fusing trees of hyper operators, reductions included | L | `[+] (@a »*« @b)` 317 → ~5 ms on one thread |
| P3 | Parallel reductions, and the float decision | M | the dot product ~5 → ~1.5 ms; `[max]` 738 → ~1 ms |
| P4 | `race`/`hyper` reductions that never build the list | M | `.race.map(&f).sum` with no 10⁷-element list |
| P5 | The closure tax: allocation, counts, a resident pool | L | `.hyper.map` 1.9× → ≥3.5×; short lists stop losing |
| P6 | Native arrays in worker loops | L | the dot loop 2.5 s → tens of ms; `hyper for` stores stay packed |
| P7 | `is pure` user code in the hyper operators | M | a decision first |
| P8 | Wider kernel coverage | S–M each | `Z*`, `**`, `%`, `min`/`max`, `.log`, `[\+]` |

### P0 — Measure first (no engine change)

- **The benches:** `tools/bench/parallel/numeric.raku`, the form matrix from
  the appendix, one process per form, with `--n`; and `numeric-ceiling.c`.
  `sweep.raku` from PARALLEL-SCALING P0 drives them interleaved.
- **The baseline:** on a quiet machine, written into this plan beside the
  table above, plus peak RSS for each form.
- **The unpack census:** which of these forms leave a packed array packed. It
  extends PACKED-ARRAY-PLAN's census to the hyper and reduction forms.
- **The bit-identity harness:** random Int and Num arrays, including NaN, ±Inf,
  −0e0, subnormals, and Ints near 2⁶³. Every kernel form runs against
  `RAKUPP_NO_KERNELS=1` and the two `.raku` outputs are compared. It guards
  every later phase.

### P1 — Packed in, packed out

- **Kernel results as words:** a hyper kernel whose answer goes to a `num` or
  `int` target writes words: `my num @c = …`, `@c = …` with a native `@c`, and
  an argument bound to a native array parameter. That covers
  `hyperNumericInfix`/`Method` building a `PackedArr`, and the list-assignment
  arm taking it over the way `my @d = <fresh list>` takes over a buffer
  (4a1edb88). An Int kernel into an `int` target is exact. A Num kernel into a
  `num` target is too.
- **Reductions over the words:** `.sum`, `.min`, `.max`, `[+]`, `[*]`, `[min]`,
  `[max]` and `.minmax` without `arr()`. This is item 7 of PACKED-ARRAY-PLAN
  batch 3, taken up here.
  - An Int `.sum` uses `add_ovf` (IntOps.h) and goes on through `applyArith` on
    overflow, as `borrowedNumericFold` does.
  - A Num `.sum` keeps its left-to-right order (P3).
  - `[+]` and `[max]` reach the same fold as `.sum`/`.max` whenever no lexical
    operator shadows them.
- **Growth by index keeps the words:** `@a[$i] = …` one past the end of a
  packed array appends a word. Any further gap fills with zeros for `int` and
  `num` arrays (PACKED-ARRAY-PLAN item 4 widened).
- **An open question for the maintainer:** a plain `my @c = @a »*« @b` stays an
  Array of boxed Nums, as its type says. Holding untyped Nums as words is a
  representation change ([REPRESENTATION-PLAN.md](REPRESENTATION-PLAN.md)),
  not this plan's.

Exit: numbers 2 and 3. The census shows no unpack in any form P1 names.

### P2 — Fusion

- **One pass per tree:** a tree of hyper operators and hyper method calls over
  P1's leaves is evaluated as one kernel, with no intermediate.
  - It is recognised on the AST: a hyper infix or hyper method call whose
    operands are hyper nodes or leaves.
  - It is lowered once per node into a short postfix program (`DecidedOnce`),
    and run over chunks of about 256 elements with chunk-sized temporaries, so
    the compiler can vectorise each step.
- **A reduction can consume the tree:** `[+]`/`.sum`, `[*]`, `[min]`/`[max]`
  and `.minmax` over such a tree fold each chunk as it is made. `[+] (@a »*«
  @b)` becomes a dot product with no list at all.
- **It declines as the kernels do today:**
  - Element 0 is probed before anything is allocated.
  - Any element the kernel cannot answer stops the kernel, and the generic
    path evaluates the whole tree as it does today.
  - The leaves are variables, literals and ranges, so evaluating them twice has
    no effect a program can see. A tree with any other leaf is not fused.
- **FMA is off:** clang contracts `x * y + z` into a fused multiply-add on
  arm64 by default (`-ffp-contract=on`). That rounds once where the generic
  path rounds twice. The fused kernels are compiled with contraction off
  (`#pragma clang fp contract(off)`, or the GCC and MSVC equivalents), and the
  P0 harness has an `a*b+c` case that fails if one slips in.

Exit: number 1 on one thread (≤ 2× of 5.1 ms). `my num @c = @a »*» 2e0 »+« @b`
within 2× of C.

### P3 — Parallel reductions, and the float decision

- **Ints and the order-free operators run on the pool:** Int `+` and `*`
  (overflow falls back), `min`, `max` and `minmax` give the same answer in any
  order. Their reductions run on the pool whenever the list is long enough.
- **Num `+` and `*` depend on the order.** This is the maintainer's decision.
  The two options:
  - **(a), recommended:** `.sum`, `[+]` and the fused reductions keep the
    left-to-right fold. That is bit-identical to Rakudo and to today, with a
    serial ceiling of about 5 ms per 10⁷.
    - Under `race` or `hyper`, the fold uses a fixed tree: chunks of 16,384
      summed in order, then the partials combined pairwise in index order. The
      forms are `@a.race.sum`, `@a.hyper.sum`, `[+] @a.race` and P4's
      streaming forms.
    - The tree gives the same bits on 1 thread or 10, whatever
      `RAKUPP_HYPER_THREADS` says. It is also more accurate than the left fold,
      which is why Julia and NumPy use pairwise sums.
    - `race` is the word Raku already has for "the order is yours", so nothing
      new is needed.
  - **(b):** every Num reduction uses the tree, as Julia does. It differs from
    Rakudo in the last bits.
- **The pool splits more finely:** about four chunks per thread, handed out
  dynamically (`HyperPool::run` already takes chunks from a shared counter), so
  efficiency cores take fewer chunks instead of setting the pace.

Exit: number 1 on the pool, under option (a)'s `race` spelling. Bit-identical
results for 1 to 10 threads.

### P4 — `race`/`hyper` reductions that never build the list

- **The forms:** `@a.race.map(&f).sum`, `[+] race for ^N -> $i { … }`,
  `(^N).hyper.map(&f).max`, and `.grep(…).elems` the same way.
  - Each worker folds its batches into an accumulator of its own.
  - The per-batch partials combine in batch order: deterministic, as in P3.
  - Today `.race.map(* * 2e0).sum` builds the whole 10⁷-element list and then
    folds it on one thread (514 ms).
- **Why no new syntax:** this is Raku's answer to Chapel's `with (+ reduce s)`
  and OpenMP's `reduction(+:s)`. The reduction wraps the parallel loop.
- **`race for { $s += … }`:**
  - **First:** a warning, from the `ShareSweep` mask that PARALLEL-SCALING P1
    already computes. The trigger is an op-assignment to an outer variable
    the sweep marks shared, inside a `hyper`/`race for` body. The warning names
    the `[+] race for` form.
  - **Later, the maintainer's decision:** rewriting the sole-`+=` shape into
    per-worker accumulators would make an undefined-behaviour program right,
    which Rakudo does not do. The warning comes first either way.

### P5 — The closure tax

Target: number 5. The costs come from the profile above, in the order it ranks
them:

- **No allocation per iteration:**
  - `runHyperLoop` builds its `redo` closure only for a body that contains
    `redo`, and stops re-defining `$/` in a scope that kept it.
  - `hyperSeqCall` reuses one argument vector per worker.
  - `PooledFrame` keeps a pool per thread.
- **No shared count per call:** the Callable and the loop's parent scope are
  referenced once per worker, not once per call. That is the same move
  `runHyperLoop` made for its per-worker scope.
- **Then re-profile.** If allocator waits are still on top, give each worker an
  arena for frames. That is the risk PARALLEL-PLAN named ("allocator contention
  may dominate after the easy locks are fixed").
- **A resident pool for `runParallel`.** Short lists pay about 100 µs a run
  for spawning threads. PARALLEL-PLAN P4 deferred a pool for `start` on the
  scaling numbers of the time. Here the spawn is the measured cost.
- **`hyper for`'s batch** grows from 64 while iterations run short of a target
  time per batch (about 50 µs), as `.hyper(:batch)` lets a user do by hand.
- **The default degree** is measured, not assumed:
  - all cores less one, against the performance cores alone;
  - dynamic batches already let efficiency cores help without holding the
    others back.

Exit: number 5. `:degree(9)` is no slower than `:degree(3)`. 1,000
`.hyper.map` runs over 640 elements are no slower than `.map`.

### P6 — Native arrays in worker loops

This depends on PACKED-ARRAY-PLAN batch 3 (`--cnp` on packed registers) and
reuses PARALLEL-SCALING P5 (kernels under threads for private variables).

- **Word stores while workers run:**
  - **Why stripes don't apply:** an aligned 8-byte store cannot tear on any
    platform Raku++ ships for, so the torn-copy argument that needs stripes for
    80-byte Values does not apply to words.
  - **The real hazard** is a word store racing the one-time unpack in `arr()`.
  - **Two designs to prototype and measure:**
    - a parallel loop pins the payload, and `arr()` waits while it is pinned;
    - stores check an "unpacking" flag under the payload's once-flag, and take
      the general path when it is set.
- **Loops compile on workers:** `--cnp` and the loop kernel compile a worker's
  loop over native arrays. The dot loop split over `start` goes from 752 ms (×4)
  toward the C ceiling.
- **`hyper for` over native arrays:** stores stay words and need no stripe. The
  loop variable already costs no stripe: PARALLEL-SCALING P4 measured that, and
  found its own change unnecessary.

Exit: number 4. The dot loop over `start` ×4 within 3× of C at 4 threads.

### P7 — User code in the hyper operators

The language allows `».&f`, `»my-op«` and junction autothreading to run in
parallel. Both engines run them in order, and programs lean on that: `@a».say`
prints in order on both.

- **The proposal:**
  - fan out only a routine marked `is pure`, or a built-in method known pure
    (`Cnp.cpp`'s `isPureMethod`);
  - only over lists past a threshold;
  - through `runParallel`, so P5's costs apply.
- **Never** for methods that do I/O.
- **Junctions stay serial:** they are rarely large enough to pay.

This is the maintainer's decision. It is the only phase where a program's
observable order could change, and only for code its author declared pure.

### P8 — Wider kernel coverage

Each item is small, with one bit-identity question to settle:

- the operators `**` (Num, and Int with a small exponent), and `%`, `mod` and
  `div` on Ints (a zero divisor declines);
- the operators `min`, `max` and `atan2`, and the prefix `-«`;
- the methods `.log`, `.log10`, `.floor`, `.ceiling`, `.round` and `.Int`
  (in range);
- `Z op` as `»op«` over equal lengths;
- `[\+]` and `[\max]` as a parallel prefix: two passes over chunks.

---

## Gates, every phase

- **Roast:** zero regressions.
- **The rest of the standing suite:** `t/run.raku`, the `--cnp` lane of
  `t/jit/run.raku`, and the battery at P1, P5 and P6.
- **Stress:** `t/stress/run.raku` in both modes, and the TSan job.
  - P5 and P6 add contract programs: a hyper over native arrays with a
    concurrent unpack; and a `race` reduction with a dying element.
- **`perf-guard --check`:** single-threaded numbers unchanged. Every new test
  sits on the hyper and reduction paths. None goes in the per-statement path.
- **Bit identity:** the P0 harness, kernel against no kernel, after every
  kernel change. Num reductions under `race` give the same bits for 1–10
  threads.
- **The P0 matrix,** re-measured after each phase, interleaved against the
  previous binary.
- **`--exe` and `--cpp`:** they share the runtime, so their lanes run each
  phase's forms.

## Non-goals

- New syntax: no reduce intents, no `forall`. Raku's `race`, `hyper` and
  `[op]` already say it.
- GPU offload, distribution over machines, a BLAS dependency, or hand-written
  SIMD intrinsics. The kernels are plain loops written so the compiler can
  vectorise them.
- Making racy programs correct (P4's rewrite is an explicit later decision), or
  any change to the memory model.
- Matrix kernels over shaped arrays (`my num @m[n;n]`). They come after P6, if
  at all.

## Risks

- **Bits.**
  - FMA contraction: P2 turns it off and P0's harness has a case for it.
  - A reduction that reorders without the `race` rule.
  - Any kernel answer that differs from the generic path is a bug, as the
    existing kernel comment already promises.
- **P6's unpack race.** It is the only part of the plan that touches the memory
  model's machinery. It gets its own contract program before its fix, as every
  PARALLEL-PLAN P3 delivery did.
- **Single-threaded speed:** the extra AST recognition for fusion runs once per
  node, and the decline probes run before any allocation. `perf-guard` is the
  gate.
- **The machine:** the M-series mix of performance and efficiency cores hides
  scaling problems behind core speed. The second-machine check from
  PARALLEL-SCALING (4 equal cores) applies here too.

---

## Appendix: the measurement

The form matrix (`bench.raku MODE`, one process per form):

```raku
sub MAIN(Str $mode, Int :$n = 10_000_000) {
    my $N = $n;
    my num @a = (^$N) »*» 1e-7;
    my num @b = (^$N) »*» -1e-7;
    my $t0 = now;
    my $chk;
    given $mode {
        when 'sum'        { $chk = @a.sum }
        when 'reduce'     { $chk = [+] @a }
        when 'max'        { $chk = [max] @a }
        when 'mul-boxed'  { my @c = @a »*« @b; $chk = @c[$N div 2] }
        when 'mul-native' { my num @c = @a »*« @b; $chk = @c[$N div 2] }
        when 'axpy'       { my num @c = @a »*» 2e0 »+« @b; $chk = @c[$N div 2] }
        when 'sqrt-chain' { my @c = (@a »+» 3e0)».sqrt; $chk = @c[$N div 2] }
        when 'dot-hyper'  { $chk = [+] (@a »*« @b) }
        when 'dot-Z'      { $chk = [+] (@a Z* @b) }
        when 'dot-loop'   { my num $s = 0e0; for ^$N -> int $i { $s += @a[$i] * @b[$i] }; $chk = $s }
        when 'map'        { my @c = @a.map({ ($_ + 3e0).sqrt }); $chk = @c[$N div 2] }
        when 'hyper-map'  { my @c = @a.hyper.map({ ($_ + 3e0).sqrt }); $chk = @c[$N div 2] }
        when 'hyper-for'  { my @c = hyper for @a { ($_ + 3e0).sqrt }; $chk = @c[$N div 2] }
        when 'race-sum'   { $chk = @a.race.map(* * 2e0).sum }
        when 'start4'|'start10' {
            my $W = $mode eq 'start4' ?? 4 !! 10;
            my @p = (^$W).map: -> $k {
                start {
                    my int $lo = $k * $N div $W; my int $hi = ($k + 1) * $N div $W;
                    my num $s = 0e0;
                    loop (my int $i = $lo; $i < $hi; $i++) { $s += @a[$i] * @b[$i] }
                    $s
                }
            };
            $chk = [+] await @p;
        }
    }
    printf "%-12s %8.3f s  chk=%s\n", $mode, now - $t0, $chk;
}
```

The C ceiling: the same arrays (`a[i] = i*1e-7`, `b[i] = -i*1e-7`) and the same
four operations. It uses pthreads over equal slices, `clang -O2`, best of 3.
Single-thread rows come from `RAKUPP_HYPER_THREADS=1`. The unpack measurements
read `ps -o rss= -p $*PID` before and after each form.
