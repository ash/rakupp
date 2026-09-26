# Plan: v6.0.0 — Raku that is fast

*Written 2026-09-26, before any code, alongside [V5-PLAN.md](V5-PLAN.md): v5 is
about errors, v6 is about speed. "Speed" here has four parts: time on the
workloads where Rakudo still leads, memory per value, time to compile and load,
and how much of Raku the native compiler handles without falling back. Work
starts after v5.0.0 is tagged.*

*Every figure below comes from the 2026-09-26 survey
([../findings/survey-2026-09-26/](../findings/survey-2026-09-26/)). The box was
carrying a load average of about 3 while it was taken, so the times are ratios
of minimum-of-five runs and the profiles are sample shares. Memory figures are
peak RSS and do not depend on load. P0 re-takes the time ratios on a quiet
machine before any batch is judged against them.*

---

## The numbers a stranger can re-measure

1. **Every perf-guard kernel at or below Rakudo's time, at steady state.** The
   shipped kernels include Rakudo's ~80 ms of startup and warm-up. That is why
   BENCHMARKS.md reads `objects` at 1.4× and `multiwhere` at 2.5×; at ten times
   the work they read **2.93×** and **4.8×**.
2. **Issue #47 at parity.** A Math::NumberTheory driver (11 functions, n ≤
   3000) reads **2.35×** Rakudo's time, and Graph is still to be measured.
3. **Memory per element at or below Rakudo's,** on every row of the table
   below. Seven rows are above it today, one of them by 26×.
4. **`--exe` compiles ≥ 90% of the fully passing Roast files natively.** On
   2026-09-26 it compiled **784 of 1,201 (65%)**
   ([exe-fallback-roast.tsv](../findings/survey-2026-09-26/exe-fallback-roast.tsv)).
   The last such count was 389 of 416, at v1.0.
5. **Compile and load time, halved:**
   - an `--exe` hello's translation unit: 0.93 s
   - a warm `use Cro::HTTP::Router`: about 58 ms
   - `-c` on large sources: 76–190k lines/s

---

## What the survey measured

### Time (ratio = rakupp ÷ Rakudo; above 1 means rakupp is slower)

| workload | ratio | where the time goes |
|---|---:|---|
| `objects` ×10 | 2.93 | the "does it do `Rational`" check on every default `.new` (11.8%); class-name resolution at each use (~6%); a 4,032-byte hash block per object (~6%); string compares of operator names (10%) |
| `multiwhere` ×7.5 | 4.8 | `where` evaluation: 33%. Every call builds an Env, defines `$x` and `$_` in a hash map and re-curries `* > 0` |
| multi method without `where` | 2.8 | the dispatch lambda: 64%, ~650 ns per call |
| 1M `.new` + accessor | 3.1 | `strlen`/`memcmp` through the dylib stub: 26% of self time |
| Math::NumberTheory driver | 2.35 | `scoreCandidate`: 44% |
| — `euler-phi` alone | 4.4 | ~1.1M calls to the module's `multi infix:<gcd>`. All six candidates fail every time, then the builtin answers |
| builtin `gcd` loop, module loaded | 2.3 | 0.26 s without the module, 0.81 s with it; Rakudo 0.31 → 0.35 |
| sort with a comparator and a key | 1.22 | `Value::orderVal` interns twice per compare under a `shared_mutex` (6.9% + 2.5% locks); `exprYieldsContainer` walks the AST per statement (~5%); `Env::define` of placeholders (4.8%) |
| split / map / uc / join | 1.10 | ~1,300 `m == "…"` checks before a builtin Str method is found: ~175 ns per call against Rakudo's 15–25 |
| return / next / last / CATCH | 1.08 | control-flow exceptions: ≤0.5% |
| grammar `jg.raku` api / deep | 0.57 / 0.79 | a 4,032-byte hash block: 11.5%; the memo hash: ~15%; the replay `std::set<tuple>`: 4.6% |
| lazy Seq pipelines, hash-of-arrays, word frequency | 0.74, 0.73, 0.31 | — |
| JSON::Fast `from-json` | 0.13 | — |

Present in every profile: `tctx_` thread-local access (3–5%), shared_ptr
release (4–12%), and malloc/free (4–24%). The last two fall mostly as P2 and
P3 land.

### Memory (bytes per element; empty-program RSS is 7 MB for rakupp, 147 MB for Rakudo)

| workload | rakupp | Rakudo | ratio |
|---|---:|---:|---:|
| 200k two-attribute objects | 4,780 | 312–526 | **11×** |
| 200k two-key hashes `%(a => …, b => …)` | 4,510–4,620 | 547–734 | **7×** |
| 1M-element `my int @a` | 262 | ~10 | **26×** |
| 1M Int-keyed Pairs | 779 | 206–241 | 3.5× |
| 1M Rats | 617 | 157–222 | 3.2× |
| 1M Ints in an array | 263 | 122–157 | 1.9× |
| 1M Nums in an array | 262 | 110–167 | 1.9× |
| 1M short Strs | 262 | 140–209 | 1.5× |
| 1M Str-keyed Pairs | 424 | 269–289 | 1.5× |
| 1M `[$_, $_+1]` | 584 | 438–467 | 1.3× |
| hash, 1M Int → Str | 188 | 272–283 | 0.7× |
| a 50k-line program under `-c` | 3.3 KB/line | 25 KB/line | 0.13× |

- About 128 bytes of each array row is garbage from growing the array: macOS
  keeps the old blocks resident.
- `objects.raku` makes 10 mallocs per object and churns 996 MB. A 4,032-byte
  `std::deque` block for the attributes is 81% of those bytes.
- `%h.elems` builds every Pair: 613 MB peak, against 194 MB for `+%h`.

**Type sizes (libc++):**

| type | bytes |
|---|---:|
| `Value` | 128; its `CowStr` alone is 40 |
| `ObjectData` | 280, 128 of them an inline `Value boxed` |
| `ValueHash` entry | 168 |
| `Callable` | 664 |
| `VarExpr` | 360 |

### Compile and load

| where | measured | mechanism |
|---|---|---|
| lexer | 24.5% of `-c` on a 188k-line file | `kLexOps` scan builds a `std::string` per table entry, per operator token (Lexer.cpp ~3725) |
| warm module load | `precompRead` 34% of a warm Cro load | files are copied through `ss << rdbuf()` (22%); every dependency of every entry is re-hashed (455 reads for 68 modules); `buildInstalledDistribution` re-parses a dist's META for each of its modules (10%) |
| `$*DISTRO` | +10 ms per process (3.3 → 13 ms) | any method spawns `sw_vers` and reads a licence file (MethodCallPart2.cpp ~890); 89 battery files use it, 252 of the uses are `.is-win`; 16% of a warm Cro load |
| operator pre-scan | `-c` on `use Cro::HTTP::Router` 14.1 ms, JSON::Fast 3.6 ms | Parser.cpp ~936–953 follows every transitive `use` |
| precomp cache | 23,823 entries, 77 MB; 1,037 copies of Test::Util | `precompPath` absolutizes `.` and `lib` into the key, so each working directory is a cold load |
| `--exe` build | hello TU 0.93 s, 0.61 s of it headers; `range-int.t` 52 s at -O2, 18 s at -O1, 1.2 s at -O0 | the whole mainline is one C++ function; optimisation time on it grows faster than its size |

### What `--exe` falls back on (first blocker per program)

| construct | Roast | `t/` | total |
|---|---:|---:|---:|
| multi dispatch: nested multi, parent-class candidate, `:D`/`:U`, `where` | 61 | 28 | 89 |
| role / package | 28 | 51 | 79 |
| allomorph literal `<42>`, `<1 2 3>` (reported as "NK 24") | 52 | 18 | 70 |
| regex / `s///` with a code block or a variable | 38 | 22 | 60 |
| phasers, CATCH | 14 | 44 | 58 |
| assignment to an unsupported target | 40 | 18 | 58 |
| a declaration as a sub-expression (`if (my $x = …)`) | 32 | 10 | 42 |
| misc: do-loop values, `xx *`, `redo`, `subset`, non-literal enum | 20 | 20 | 40 |
| parameterized types: `Buf[uint8]`, `array[int]` | 24 | 10 | 34 |
| `\( )` Capture literal | 20 | 10 | 30 |
| other prefix operators | 18 | 11 | 29 |
| `::("…")` symbolic reference | 11 | 16 | 27 |
| `$=pod` / `$=finish` | 18 | 1 | 19 |
| other | 53 | 34 | 87 |

`--cnp` covers 7 of the 34 loops in `examples/` and `tools/bench`
([cnp-refusals.tsv](../findings/survey-2026-09-26/cnp-refusals.tsv)). The
refusals:

| reason | loops |
|---|---:|
| calls | 13 |
| array index | 6 |
| hash element | 4 |
| given/when or a nested `for` | 2 |
| regex | 1 |

None of the 9 loops in the real workloads was eligible.

---

## Phases

### P0 — measurement first

- **Steady-state variants of `objects` and `multiwhere`,** and the #47 driver,
  as kernels. A new kernel has to be named in four lists; three of them fail
  silently.
- **Install Graph's dependencies** (BinaryHeap, Data::TypeSystem) under both
  engines and measure it.
- **`tools/bench/memory.raku`:** the bytes-per-element table for both engines,
  so row 3 of the numbers can be re-measured.
- **The malloc census as a tool:** allocations per operation for `objects` and
  a hash loop.
- **Profile a `die`/CATCH round trip** (~70 µs here, ~4.5 µs on Rakudo).
- **Measure `--exe`'s exception share.** The claim that exceptions dominate
  `--exe`'s distance from hand-written native code was measured on the WASM
  build only ([rakujs/INTERNALS.md](../../../rakujs/INTERNALS.md)).
- **Re-take the time table** on a quiet machine.

### P1 — small and independent

Each item is gated alone, so its effect has a number:

- **`Value::orderVal`:** three prebuilt statics instead of two interns per
  compare.
- **The lexer:** a switch on the first byte, with precomputed lengths. Reserve
  the token vector.
- **`$*DISTRO`:** load it lazily, only for `version`, `release`, `desc` and
  `gist`, and read the version with `sysctlbyname("kern.osproductversion")`
  instead of spawning.
- **`precompRead`:**
  - read each file in one call
  - remember each file's hash for the life of the process, so content still
    validates and timestamps are never trusted
  - parse each distribution's META once
- **Stop building temporary lists:**
  - `%h.elems`
  - `EXPR for ^$n` (Interpreter.cpp ~15205), which flattens the Range where the
    block form does not
  - reducers that call no user code — `elems`, `sum`, `min`, `max`, `join` —
    which should borrow as `hashDirect` does (MethodCallTail.cpp ~1778) instead
    of snapshotting through `toList`
- **Grow arrays by `realloc`** when `bitwiseRelocOk()` holds (ValueVec.h
  ~172–197; `growAndBuild` builds the new element first). A C probe halves
  large arrays on macOS. Linux is not measured.
- **Cache the "does it do Rational" answer** as a tri-state on `ClassInfo`
  (MethodCallPart2.cpp ~6174).
- **Cache a class name's resolution** on its node, keyed by the symbol
  generation.
- **Store `exprYieldsContainer`** on the node instead of walking the AST per
  statement.
- **Precompute placeholder names** and bind them to pad slots; build `@_` only
  when the block reads it.
- **Grammar replay:** flag tree membership on the node instead of building a
  `std::set<tuple<string,long,long>>`.
- **`execBlock` and `runLoopBody`** take `shared_ptr<Env>` by `const&`
  (Interpreter.h ~1146).
- **`providedArgs.reserve`** (MethodCallPart2.cpp ~626).
- **Free the parser's token vector** after the parse: 48 MB of the 136 MB live
  heap on a 50k-line file.
- **Stop `Array.shift` being O(n).**

### P2 — dispatch

- **A multi-dispatch cache.**
  - Key: the dispatcher, the argument count, each argument's type, `ClassInfo*`
    and definedness, and the named-argument names.
  - Value: the winning candidate, or "the builtin answers".
  - Used only when every candidate is purely nominal; invalidated through
    `noteSymbolMutation`.
  - The redispatch context (the `visited` set, a heap `std::function` with 5–9
    captures, `RedispatchCtx`) is built lazily, only for a body that uses
    `callsame`/`nextsame`.
  - `scoreCandidate`'s per-candidate vectors go.
  - This is most of issue #47. [DISPATCH-PERF-PLAN.md](DISPATCH-PERF-PLAN.md)
    left `scoreCandidate` "to its own plan", and this is that plan.
- **`where` constraints:** cache the constraint's Code on the `Param` when it
  reads no earlier parameter and no `self`, as `SubsetInfo::whereBlock` already
  does, and call it directly.
- **Operator names as integers in hot paths.**
  - `applyArith` has 172 `op ==` sites, and `typeMatchesArg`, `coreEnumValue`
    and `isKnownTypeName` compare strings the same way. Wrap the name in an
    `MName`-style view at function entry, as MethodName.h does for methods.
  - Extend the Int fast path past two-character operators, to `<=>`, `cmp`,
    `%%`, `div`, `mod`, `gcd` and `**`.
- **A dispatch table for builtin methods,** replacing the chain of about 1,300
  string compares. [DISPATCH-PERF-PLAN.md](DISPATCH-PERF-PLAN.md) §5 named it
  as a precondition.
- **Inline caches keyed on type:** dispatch on the invocant type first, then
  on the name within that type. This is the conclusion of
  [METHOD-DISPATCH-EXPERIMENT.md](../experiments/METHOD-DISPATCH-EXPERIMENT.md):
  the 198 type-guard arms are what make the chain long.
- **The thread-local execution context** (DISPATCH-PERF-PLAN phase 2): about
  1,200 direct `tctx_.` sites.
- **The cost of a `die`,** from P0's profile.

### P3 — representation and memory

In order of evidence per cost:

1. **[VALUEHASH-SMALL-PLAN.md](VALUEHASH-SMALL-PLAN.md), still unimplemented.**
   A small first chunk instead of the 4,032-byte `std::deque` block
   (ValueHash.h:60). It is the largest row in the memory table and 6–11.5% of
   two profiles.
2. **Inline storage for `Env::vars`.** Four stable slots with spill, and the
   86 `make_shared<Env>` sites moved onto the slab. It accounts for 99% of the
   mallocs in `for %h.kv -> $k, $v` and 30% of those in `objects`.
3. **Small Rats inline.** The numerator lives in `i` and the denominator in the
   `n` word, promoted to BigInt on overflow; there are 154 `ratN`/`ratD` sites.
   A first step is two inline BigInt limbs, allocated from the slab, which
   takes four mallocs per Rat operation to none.
4. **Pairs with an inline Int/Bool key,** and `pairVal` allocated through
   `makePayload`. There are 83 `make_shared<Value>` sites that bypass the slab.
5. **`ObjectData::boxed` behind a pointer.** It is 128 of the 280 bytes and is
   used only for mixins over non-objects.
6. **Packed native arrays:** a payload of contiguous `int64`/`double` that boxes
   on read. It resolves the 26× row and SPEC-DIVERGENCES #14.
7. **Split `Callable`** (664 bytes, plus a per-closure copy of the `declFile`
   path) into an immutable per-AST CodeInfo and a closure of about 64 bytes.
   A closure costs ~1.6 KB here and ~0.6 KB on Rakudo.
8. **Slim `VarExpr`** (384 bytes on the heap; 23% of a large program's AST) by
   moving its declaration-only fields behind a pointer.
9. **Native math phases 2–3** ([NATIVE-MATH-PLAN.md](NATIVE-MATH-PLAN.md)):
   unboxed typed slots in the pads. Re-price them first; the UNBOX lanes landed
   on 2026-09-19 and took phase 4.
10. **The endgame** ([VALUE32-PLAN.md](VALUE32-PLAN.md)):
    - design A, a 56-byte `Value`
    - then the question the plan leaves open, whether values stop being
      refcounted; that is also the answer to reference cycles

    Together with the `realloc` growth, arrays would come to about 60 B per
    element and hash entries 168 → 96.

### P4 — compile and load

- **The operator pre-scan** follows only modules that re-export, or stores
  each module's scan summary in its precomp entry.
- **The precomp cache and the working directory: a decision, not a fix.**
  An entry is keyed by *(source, search path)* on purpose
  ([CACHING.md](../../guide/CACHING.md)). A different `-I` can select a
  different file for a `use`, and recording dependency contents cannot catch
  that. But `.` and `lib` are made absolute before they enter the key, so every
  new working directory is a cold load (JSON::Fast 11 ms against 6; Cro 125
  against 70) and writes another entry. That is how Test::Util came to have
  1,037 copies.
  - The candidate: record the file each `use` resolved to, and re-resolve at
    load. That catches a different `-I` selecting a different file, and drops
    the working directory from the key.
  - It changes a documented design, so it waits for the user's call.
  - The narrower alternative keeps the key, and makes only the path entries
    that actually decided a resolution part of it.

### P5 — the native compiler

- **Build time:**
  - Ship a precompiled runtime header. With `-fpch-instantiate-templates` the
    hello TU drops from 0.93 s to 0.36 s.
  - Split the mainline into non-inlined chunks, or compile straight-line code
    without optimisation.
- **Coverage, by the fallback table:**
  - allomorph literals: 5 lines in the interpreter; the JS backend already has
    `R.allo`
  - CATCH, `\( )` and a declaration inside a condition: 140 programs together
  - multi dispatch: the guard learns required nameds, `where`, `:D`/`:U` and
    ancestor candidates
  - roles and packages
  - parameterized types
  - symbolic references
  - `$=pod`
  - regex with code blocks
- **Diagnostics:** the fallback note names the construct (no more "NK 24") and
  gives its line.
- **Size:** the `--slim` hello went from 4.9 MB to 10.24 MB between v3.14 and
  2026-09-26, and `Interpreter.o` grew by 1.84 MB in six days. P3's type splits
  and a carve-out census are where it comes back ([SLIM-PLAN.md](SLIM-PLAN.md)).

### P6 — tier-up and the other backends

- **`--cnp` becomes the default and `--jit` goes** ([CNP-PLAN.md](CNP-PLAN.md)):
  - stencils that call a runtime helper that cannot throw, covering index
    reads and writes, hash elements and calls
  - correct answers on x86-64
  - threaded programs

  Taken last because the real workloads spend their time inside calls. It
  moves numeric kernels, not them.
- **`--target=js` refusals.** Port `Test` to the JS runtime (44 refusals), then
  `LtmNfa`, `:nth`/`:x`/`:P5`/`:m`, `sleep` in a Worker, and `use`d modules
  (issue #83). The gate `t/js/run.raku` is also blind to `MAIN` arguments.
- **The worker tax.** A worker's loop runs ~15% slower than the main thread's,
  and the P4 thread pool is deferred ([PARALLEL-PLAN.md](PARALLEL-PLAN.md)).

---

## Gates, every batch

- **`perf-guard --check`,** with the ratio method against the previous release
  built locally and interleaved. Never eyeball it.
- **Zero Roast regressions:** after v5, that means 100% stays 100%.
- **The local suite.**
- **v5's battery scan and use smoke.**
- **The memory table** from P0 on: no row gets worse.
- **The native compiler:** the `--cpp` build check, and the fallback census
  never loses a program.

## Measured, and not pursued

Measured and not worth doing; not to be re-proposed without numbers that
overturn these:

| idea | why not |
|---|---|
| control flow without C++ exceptions, in the interpreter | ≤0.5% even in a return/next/last/CATCH workload (`--exe` and WASM are P0's question) |
| a non-atomic refcount | 1.35× on a probe, before the usual ~3× probe discount |
| widening `--cnp` to move real workloads | their time is inside calls |
| compiling grammars to low-level code | caps at about 2× |
| a register IR or flat threaded loop | ~4% ceiling |
| hashing the method-dispatch chain | slower than the chain |
| repacking `Value`'s fields | 2.5% slower |
| constant folding | 0.7 foldable sites per 1k nodes |

The last five are from earlier sittings, with their files under
[../experiments/](../experiments/).
