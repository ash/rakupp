# What makes Fortran fast, and what Raku++ can borrow

*2026-10-15, at `5.4.1-77-g0d779b9b-modified` (the working tree, with another
session's uncommitted edits), on an Apple M4 with a load average of about 3.
The engine's times are best of 3, interleaved.*

*Fortran's side is read from the compilers' own sources, in shallow clones taken
the same day:*

- *gcc `f16aa1c2`: gfortran's front end and libgfortran;*
- *llvm-project `80e6d81c`: Flang's FIR and HLFIR passes and its runtime;*
- *lfortran `e645e1ca`: its ASR passes.*

*No Fortran compiler is installed on the benchmark machine, so the ceiling is
clang over `double*`. That is the back end Flang uses, and the same class of
back end as gfortran's.*

*This is a companion to
[NUMERIC-PARALLEL-PLAN.md](../plans/NUMERIC-PARALLEL-PLAN.md), which was
written the same day and covers the hyper forms. This note covers loops,
shaped arrays and the compiled tiers. Where the two overlap, this note says
what Fortran adds to that plan rather than repeating it.*

---

## The short answer

Fortran's speed does not come from a cleverer optimizer. GCC and LLVM are the
back ends of gfortran and Flang, and clang uses the same ones. The difference
is that the language hands the compiler facts other languages make it prove:

- **Types are fixed.** A `real(8)` variable holds a double, so no code checks
  whether it does.
- **A `do` loop's trip count is fixed at entry, and the body cannot assign the
  loop variable.** The count is known before the first iteration, and every
  subscript built from the loop variable has a known range.
- **An array is one block with a descriptor.** The descriptor records the
  extents and strides.
- **A dummy argument that is modified has no other name** for the length of the
  call. So the two arrays of `c = a + b` never overlap.
- **Whole-array statements leave the order open.** So do `elemental` procedures
  and `do concurrent`.

The compilers turn these facts into plain loops that the back end can
vectorize, and they add a run-time check wherever a fact is missing (Flang's
loop versioning, its disjointness tests). The rest is a library: libgfortran's
MATMUL is a blocked kernel compiled once per instruction set and chosen at
first call.

Raku has nearly all the same facts:

| Fortran's fact | Raku's spelling | Raku++ today |
|---|---|---|
| `real(8) :: s`, `integer :: i` | `my num $s`, `my int $i`, `-> int $i` | **slower** than untyped in `--exe -O` (90×) and `--cnp` (20×) |
| trip count fixed, loop variable read-only | `for ^$n -> $i`: the range is evaluated once and `$i` is read-only | `--cnp` compiles it, and checks every subscript on every read |
| arrays as one contiguous block | `my num @a`, `my num @m[$n;$n]` | 1-D packed; 2-D packed per row, and `@m[$i;$j]` is refused by `--cnp` |
| modified dummies have no alias | none: `:=` may bind two names to one array | needs a run-time identity check, which is cheap |
| whole-array statements, `elemental` | the hyper operators, `race`, `hyper` | kernels with boxed output; fusion is planned (NUMERIC-PARALLEL P2) |
| `pure` | `is pure` | parsed, and nothing reads it |
| `do concurrent` (with `reduce` in F2023) | `hyper for`, `race for`, `[+] race for` | NUMERIC-PARALLEL P4 and P6 |

The largest measured gaps are places where Raku++ has the fact and does not
use it. In three tiers, a native declaration makes the code slower.

---

## Measured: the same kernels, Raku++ against C

The arrays hold ten million `num` elements, `a[i] = i·1e-7` and `b[i] = −i·1e-7`.
The matmul is 200×200, the naive triple loop. "C" is clang over `double*`, best
of 5, with contraction off unless the column says otherwise.

### The ceiling

| | `-O2` | `-O3` | `-O3 -ffast-math` |
|---|---:|---:|---:|
| sum `s += a[i]` | 7.95 ms | 8.24 ms | **1.49 ms** |
| dot `s += a[i]*b[i]` | 6.59 ms | 6.36 ms | **2.78 ms** |
| `c[i] = 2*a[i] + b[i]` | 4.54 ms | 3.55 ms | 4.01 ms |
| matmul 200³, naive | 5.91 ms | 4.33 ms | **1.49 ms** |
| Int sum, `int64_t` | | 1.2–1.7 ms | |
| Int sum, `__builtin_add_overflow` on each add | | 3.0–3.4 ms | |
| max over `double`, `m = a[i] > m ? a[i] : m` | | 5.8–6.3 ms | |

The bold column is what reassociation buys. The reductions vectorize only when
the compiler may reorder the adds.

### Raku++

| Form | Time | per element | × C `-O3` | What happens |
|---|---:|---:|---:|---|
| `@a.sum`, `num` | 10 ms | 1 ns | 1.2× | folds the words in order (PACKED-ARRAY batch 3) |
| `@i.sum`, `int` | 2.9 ms | 0.3 ns | 1× the checked C loop | folds the words through `add_ovf` |
| `@a.max`, `num` | **193–398 ms** | 19–40 ns | 30–60× | **unpacks the array for good**, then folds boxed Values |
| `[+] (@a »*« @b)` | 316–396 ms | 35 ns | 55× | boxed hyper result, then the fold |
| `for ^$n -> $i { $s = $s + @a[$i] * @b[$i] }`, `--cnp` | 148 ms | 15 ns | 23× | |
| the same with `-> int $i`, `--cnp` | **2,839 ms** | 284 ns | 450× | **interpreted: a typed loop variable is not a candidate** |
| `loop (my int $i …)`, the same body, `--cnp` | 145 ms | 15 ns | 23× | |
| the same, interpreted | 2,350 ms | 235 ns | 370× | |
| matmul over `my num @x[$M;$M]`, `--cnp` | **2,734 ms** | 342 ns | 630× | **interpreted: `@x[$i;$k]` is refused** |
| matmul over a flat `my num @x`, `@x[$i*$M+$k]`, `--cnp` | 410 ms | 51 ns | 95× | compiled |

### Where the 15 ns of the `--cnp` dot loop goes

| `loop (my int $i …)` body, `--cnp` | per iteration |
|---|---:|
| `$s = $s + $i * 1.5e0` (no array) | 4.6 ns |
| `$s = $s + @a[$i]` | 8.2 ns |
| `$s = $s + @a[$i] * @a[$i]` | 14.5 ns |

Each packed-array read costs about 5 ns, so the two reads are two thirds of
the loop. A read works like this:

1. It calls out of the stencil chain into `rk_cnp_idxget`
   ([Cnp.cpp:408](../../../src/Cnp.cpp)), which opens a `try`.
2. It checks six facts about the array (`packedAt`,
   [Cnp.cpp:397](../../../src/Cnp.cpp)): a box register, an Array, packed and
   live, no `hashKind`, no element default, and the subscript in range.
3. It builds a `Value` for the element (`PackedArr::at`).
4. It unboxes that Value back into the register.

Only the last of the six checks can change while the loop runs, and even it
cannot change when the subscript is the loop counter of a `for ^$n` over an
array the body never resizes.

### Why `-> int $i` is twenty times slower

A typed pointy parameter puts the loop in `ForStmt::params`. Two places then
skip it:

- the interpreter's counted-range fast path, which requires
  `fs->params.empty()` ([InterpreterCore.cpp:3303](../../../src/InterpreterCore.cpp));
- the JIT harness, which refuses any `params` as "a loop shape the JIT does not
  take" ([Jit.cpp:1034](../../../src/Jit.cpp)).

The interpreter's site for `--cnp` is installed only from the counted-range
path, so `--cnp=stats` does not even count the loop as examined. The loop
runs through general binding, 284 ns an iteration.

### Why matmul over a shaped array is not compiled

`--cnp` refuses the multi-dimensional subscript as "an index of this shape (a
slice, an adverb, a hash, or not on a plain @array)". Shaped `int`/`num`
arrays keep each innermost row as its own `PackedArr` (44ed66bc), so there is
no single block to index into. The same matmul over flat 1-D arrays with
computed subscripts compiles, and is 6.7× faster for that alone.

### `--exe -O`: native declarations cost 90×

The same scalar loop inside a sub, compiled with `--exe -O`, 10⁷ iterations:

| Declarations | Time | per iteration |
|---|---:|---:|
| `my $s = 0e0; loop (my $i = 0; …)` | 6 ms | 0.6 ns |
| `my num $s = 0e0; loop (my int $i = 0; …)` | 517 ms | 52 ns |
| `my $s = 0; loop (my $i = 0; …)`, Int | 3 ms | 0.3 ns |
| `my int $s = 0; loop (my int $i = 0; …)` | 395 ms | 40 ns |

**What each loop compiles to:**

- **The untyped loop** gets UNBOX-PLAN's lanes and runs at C speed.
- **The typed loop** is emitted as boxed `rtAdd`/`rtMul` calls, with
  `rtNativeValueLike` around each store. UNBOX-PLAN lists `my int` lanes as
  open (P4).
- **A `my num @a` anywhere in the program** makes `--exe` bundle the whole
  program with the interpreter ("a typed @ variable (my num @a) — not yet
  natively compiled").

---

## What the Fortran compilers do

Each technique below names where it lives, when it fires and when it gives up.
"Verified" means read in the clone. Anything recalled rather than read says so.

### 1. Native types are never guarded

This is the language, not a pass. `real(8) :: s` means every operation on `s`
is a double operation, so the compilers emit no tag test and no box. All the
techniques below assume it.

### 2. A whole-array expression becomes one loop nest

**gfortran's scalarizer** (`trans-array.cc`, verified):

- **The pipeline:**
  - `gfc_walk_expr` flattens `a = b + c*sin(d)` into a chain of `gfc_ss`
    terms.
  - `gfc_conv_loop_setup` picks the bounds.
  - `gfc_trans_scalarizing_loops` emits one loop nest, with the scalar code
    generator writing the body.
- **No intermediate arrays.** A temporary appears only for one of these:
  - an array-valued call that is not elemental;
  - an array constructor;
  - a dependency the analysis cannot rule out (section 3).
- **TRANSPOSE costs nothing.** `walk_inline_intrinsic_transpose`
  (`trans-intrinsic.cc`) swaps the two dimension entries of the term. The
  transpose becomes a strided view.

**Flang:**

- An array expression is an `hlfir.elemental`, a lazy index→value region.
- `InlineElementals.cpp` splices a producer into its single consumer, so
  `a+b+c` becomes one region. It gives up in three cases:
  - more than one use;
  - a temporary that must be finalized;
  - a conflicting write between producer and consumer.

**LFortran:** `array_op.cpp` lowers to `DoLoop`s. Its comment at lines
1188–1196 notes that the first element of a section is bounds-checked once,
before the loops, because every later subscript lies inside the triplet.

**For Raku++:** this is NUMERIC-PARALLEL P2 as planned. Fortran adds two
things:

- **Transposes and sections can be views,** not copies (section 4).
- **The bounds check of a section can be hoisted** to one check before the
  loop.

### 3. Deciding whether `a = f(a)` needs a temporary

**gfortran, `dependency.cc` (verified).**

**Between two different variables,** `gfc_check_dependency` says "no
dependency" unless one of these holds:

- they are EQUIVALENCEd;
- both are POINTER or TARGET of the same type;
- one falls in the TARGET-dummy hole.

**Within one variable,** `gfc_dep_resolver` classifies each dimension of a
section pair:

| Class | Meaning |
|---|---|
| NODEP | disjoint, including a gcd test on the strides |
| EQUAL | the same elements |
| FORWARD | safe in forward loop order |
| BACKWARD | safe only in reverse order |
| OVERLAP | not safe in either order |

The decision follows from the classes:

- Any NODEP dimension means no dependency at all.
- A BACKWARD dimension is repaired by **running that loop in reverse**
  (`reverse[m] = GFC_REVERSE_SET`, line 2370).
- Only an OVERLAP, or the two sides walking their dimensions in a different
  order, costs a temporary (`gfc_conv_resolve_dependencies`, `trans-array.cc`).

**Flang (verified):**

- **In place when safe:** `OptimizedBufferization.cpp` writes an elemental
  straight into its assignment target when both hold:
  - every read of the target inside it is at the elemental's own index
    (`isDesignatingArrayInOrder`, line 372);
  - nothing in between touches what it reads or writes.
- **A run-time check when the static analysis cannot decide:**
  `InlineHLFIRAssign.cpp` emits a disjointness test and two versions of the
  loop, a direct one and one through a temporary. The test comes from one of:
  - `genIndexBasedDisjointnessCheck` (`FIRBuilder.cpp:1843`), which compares
    `ub1 < lb2 || ub2 < lb1` for each dimension of one base;
  - `genAddressBasedDisjointnessCheck` (`:1896`), which compares byte ranges
    built from the descriptors, negative strides included.

**For Raku++:** the static rule does not transfer, because Raku allows `:=`.
The mechanism transfers completely, because at run time the engine knows
exactly which payload each operand is and which range each slice covers. The
run-time ladder:

1. Different payloads: write in place.
2. The same payload, read only at the written index: write in place.
3. Forward overlap: a forward loop.
4. Backward overlap: a reversed loop.
5. Otherwise: a temporary.

### 4. Specializing on stride 1, checked once

**Flang `LoopVersioning.cpp` (verified).** The header comment:

> This pass looks for loops iterating over assumed-shape arrays, that can be
> optimized by "guessing" that the stride is element-sized.

- **What it emits:** a `fir.if` on "every such array's stride equals its
  element size", placed on the outermost loop where all accesses qualify.
  - The then-branch is a clone of the loop with each array flattened to 1-D
    and slices folded into the index.
  - The else-branch is the original loop.
- **The cost:** the header itself warns of 3–4× code growth.
- **When it runs:** on at `-O3`/`-Ofast` (clang's Flang driver,
  `shouldLoopVersion`).
- **Measured:** commit 711419e reports facerec about 12% faster, because an
  inner SUM loop now vectorizes.

**GCC** does the same generically in `gimple-loop-versioning.cc`, whose header
names Fortran's assumed-shape arrays as the motivating case.

**Repacking** a strided array into a contiguous copy is the other answer:

- gfortran `-frepack-arrays`;
- nvfortran at call sites, by default;
- Flang `fir.pack_array`.

Flang's ArrayRepacking notes measure capacita 1.8× faster, and fatigue2 2×
slower. In fatigue2 the cause was not the copy: the inserted calls made the
inliner choose worse.

**For Raku++:** the principle is the one to take, more than the stride case.
Find the facts the fast loop needs. Check them once at loop entry. Run a
specialized copy when they hold and the general loop when they do not. For
`--cnp` the facts are about packed arrays: live, the element kind, the length
against the counter's range, and no resize in the body.

### 5. Reductions and searches inlined as specialized loops

**gfortran, `trans-intrinsic.cc` (verified):**

- **Inline loops:** SUM, PRODUCT, DOT_PRODUCT, COUNT, MINVAL, MAXVAL, MINLOC,
  MAXLOC and FINDLOC become loops in place.
- **Early exit:** ANY and ALL leave the loop at the first deciding element
  (`gfc_conv_intrinsic_anyall`).
- **The two-loop scheme for MIN/MAX under IEEE semantics** (the comments before
  `gfc_conv_intrinsic_minmaxval`, around line 4916):
  - The first loop runs only until it finds an element that is not a NaN
    (`if (a[S] <= limit)`), then jumps into the second.
  - The second loop is a plain `limit = max(a[S], limit)`, which vectorizes.
  - The rare semantic case costs a short prologue. The hot loop carries none
    of it.

**Flang** (`SimplifyHLFIRIntrinsics.cpp`) rewrites the same family, MATMUL and
TRANSPOSE included, into elementals or loop nests. It marks a floating-point
reduction loop unordered, and so vectorizable, only when the operation carries
`fastmath<reassoc>`.

**For Raku++:** the two-loop shape is the general lesson. Put Raku's exact but
irregular semantics in a cold prologue or epilogue. Then the hot loop is one
the C compiler can vectorize, and the answer stays bit-identical.

### 6. MATMUL: inline when small, blocked when large, dispatched by CPU

**gfortran's front end** (`inline_matmul_assign`, `frontend-passes.cc`,
verified):

- `c = matmul(a, b)` becomes a loop nest in j, k, i order, so the innermost
  loop has unit stride in column-major order.
- It is guarded at run time by `size(a,1)*size(a,2)*size(b,2) <= limit**3`,
  with the library call in the else branch.
- The limit is 30 by default (`options.cc:505`).
- `transpose()` arguments are handled without being materialized.

**libgfortran** (`m4/matmul.m4`, `m4/matmul_internal.m4`, verified):

- **One kernel, several instruction sets.** `matmul_internal.m4` is compiled
  several times, with `__attribute__((__target__("avx")))`, `"avx2,fma"` and
  `"avx512f"` (lines 90–128).
- **Chosen at the first call.** The variant comes from
  `__builtin_cpu_supports` (lines 160–197) and is cached in a function pointer
  with an atomic store.
- **When all leading strides are 1:** a blocked kernel "derived from Superscalar
  GEMM-based level 3 BLAS" (Kågström and Ling). It uses 512×256×256 blocks, a
  packed copy of the A block, and a 4×4 inner tile with 16 accumulators.
- **Otherwise:** dot-product or axpy loop orders, chosen from the strides.
- **Measured:** PR 51119 (2016) reports about 5.2 GFLOPS at n = 512–2048
  against 1.2–2.4 for the naive loop, so 2–4×.

**Flang:** MATMUL is inlined by `MatmulConversion` in
`SimplifyHLFIRIntrinsics.cpp`. The runtime (`flang-rt` `matmul.h`) uses a
k-j-i loop order "to avoid the inner sum reduction and to avoid non-unit
strides".

### 7. Floating-point order

The standard lets a processor evaluate any mathematically equivalent
expression, as long as it respects parentheses. The open-source compilers do
not use that freedom by default:

- **gfortran:** sets `flag_associative_math = !flag_trapping_math &&
  !flag_signed_zeros` (`options.cc:300`), which is off unless the user relaxes
  both. It protects parentheses except at `-Ofast`.
- **Flang:** reorders a reduction only under `reassoc`.
- **Intel's default floating-point model** (`fp-model fast`) does allow
  reordering. That is recalled, not read here, and it is part of Intel's
  reputation.

**For Raku++:** NUMERIC-PARALLEL P3's option (a), left-to-right by default with
`race` as the opt-in, is what gfortran and Flang do. The ceiling table above
shows the price: 5.5× on a sum, 2.9× on the naive matmul.

### 8. `pure`, `elemental` and `do concurrent`

- **`elemental` procedures** are scalar functions that the array machinery
  applies element by element, and that inlining folds into the loop.
- **LFortran's `inline_function_calls`** takes single-return functions with no
  SAVE state.
- **`do concurrent` is lowered without parallelism by default:**
  - gfortran attaches only `ivdep` (`trans-stmt.cc`);
  - Flang lowers it to `fir.do_loop … unordered`, and to OpenMP only with
    `-fdo-concurrent-to-openmp`;
  - ifx's `-parallel` turns it into OpenMP.
- **Common function elimination** (`cfe_expr_0`, `frontend-passes.cc`) removes
  repeated calls within one statement, but only to `pure` functions unless
  `-faggressive-function-elimination` is given.

### 9. Temporaries on the stack

Both compilers can move fixed-size temporaries from the heap to the stack:

- gfortran `-fstack-arrays`, on at `-Ofast`;
- Flang `StackArrays.cpp`, a dataflow pass that turns an `allocmem` freed on
  every path into an `alloca`.

NUMERIC-PARALLEL P2's chunk-sized temporaries are the same idea.

### 10. What does not happen

Hand vectorization is not where the speed comes from. LFortran's
`loop_vectorise` pass handles only constant-size copies, with the vector width
hard-coded. What the front ends do is hand the back end contiguity,
distinctness and trip counts, so that it can do the vectorizing.

---

## What to borrow, ranked

Ranked by measured gap against complexity. An estimate is labelled as one.

### 1. A native declaration is a fact: stop paying for it (S–M each)

**The Fortran lesson:** a declared type is never guarded (section 1).

Raku's natives give the same guarantee. A `num` variable cannot hold anything
but a num, because a store to it either converts or refuses (`natchk` in
`--cnp`, `rtNativeValueLike` in `--exe`). Today the engine treats them as an
obstacle instead, in three places:

**a. `for ^$n -> int $i` and `-> num $x`: 284 → about 15 ns an iteration.**

- **Where it is refused:**
  - the interpreter's counted-range path
    ([InterpreterCore.cpp:3303](../../../src/InterpreterCore.cpp));
  - the JIT harness ([Jit.cpp:1034](../../../src/Jit.cpp)).
- **The change:** let one `int` (or untyped scalar) pointy parameter over an
  Int range take the path that `-> $i` takes. An `int` parameter over a range
  of machine integers binds exactly what the counted path mints.
- **Why it ranks first:** this is the loop a Fortran programmer writes first,
  because it is the `do i = 1, n` spelling.
- **Done** (07775af3, 2026-10-24), for a lone `int`, `int64` or `Int`
  parameter over an integer Range: the `--cnp` dot loop 1,860 → 144 ms,
  the same as `-> $i`. A Range with a BigInt end keeps general binding.

**b. `--exe -O` with `my int` and `my num`: 52 → about 0.6 ns an iteration.**

- **What it is:** UNBOX-PLAN P4.
- **Fortran's framing makes it simpler than the untyped lane:** the untyped
  lane guards a box on entry and on every write. A native variable needs no
  guard at all, only `natchk`'s conversion where a non-native value is stored
  into it.
- **The direction is the point:** declaring types should make a compiled
  program faster, never 90× slower.
- **Done** (d0ba733b, 2026-10-24). The lane already took a native declared
  in its body; one declared in the loop header is now taken too, and its
  store-back rebuilds the native. The loop of the table above, in a sub over
  10M, 240 → 5.1 ms against a87a6b73, the same as untyped. `for ^$n -> int
  $i` takes the lane too (c95d9332): 60.6 → 2.2 ms.

**c. `--exe` with a `my num @a`.** Today it bundles the whole program with the
interpreter. The fix is an emitted loop over `double*` with the versioning
check of item 2. That is the case where the C++ compiler vectorizes what
`--cnp` cannot.

### 2. Loop versioning for packed arrays in `--cnp` (M)

**The Fortran lesson:** Flang's `LoopVersioning`, GCC's
`gimple-loop-versioning`, LFortran's hoisted section check, and the `do`
loop's fixed trip count (sections 2 and 4).

**At kernel entry,** where the register file is already bound (783bc53a), for
each array register the loop indexes:

- **The check, once:** packed and live, the element kind, no `hashKind`, no
  element default. The same also requires that the body never resizes, rebinds
  or unpacks the array. The harness's `Scan` already records which arrays and
  variables a body uses and writes (`pre.arrays`, `pre.written`,
  [Jit.cpp:1044](../../../src/Jit.cpp)).
- **Into registers:** the word pointer and the length.
- **When the check fails:** the loop runs as today.

**In the loop:**

- `idxget` becomes a leaf stencil, `r[d] = w[r[k]]`, with one unsigned compare
  against the length that takes the slow path when it fails. There is no call,
  no `try` and no `Value`.
- **When the subscript is the counter of a `for ^$n`,** compare `$n` against
  the length once at entry and drop the per-read compare entirely. That is the
  Fortran trip-count rule doing its job.
- **`idxset`** gets the same treatment. Its entry check also unshares the
  payload once, if a copy shares it.

**Calls:**

- A loop with calls takes the fast version only if it re-validates after each
  call stencil. The kernel already writes back and reloads around calls
  (2374b5c0).
- While workers are live, keep today's path. A word read can race the one-time
  unpack, just as a word store can.

**Estimate:** the dot loop from 15 to about 6 ns an iteration. The basis is
4.6 ns for the same loop with no reads, plus a few instructions per leaf load.

**The ceiling past that:** `--cnp` cannot vectorize, because each operation is
a stencil over a register file in memory. Vector speed comes from two places:

- `--exe`, where the C++ compiler sees a plain loop (item 1c);
- the prebuilt kernels.

Once this item lands, a `--cnp` loop of exactly the shape `$s += @a[$i] *
@b[$i]` or `@c[$i] = @a[$i] op @b[$i]` over the counter could hand the whole
range to the kernel NUMERIC-PARALLEL P2 builds. Fortran users get that by
writing `dot_product`. Raku users get it by writing `[+] (@a »*« @b)`.

### 3. A shaped native array is one block with a descriptor (L)

**The Fortran lesson:** arrays and their descriptors, transpose as a swap of
two dimension entries, and LoopVersioning's flattening (sections 2 and 4).

**Today:**

- each innermost row is its own `PackedArr`;
- `@m[$i;$j]` is refused by `--cnp`;
- the 200³ matmul is 630× C.

**The change:** a full-width `int`/`num` shaped array holds one
`std::vector<int64_t>` plus its extents, row-major because that is Raku's
order. `@m[$i;$j]` is `w[$i*s0 + $j]`.

**Why shaped arrays are the easy case:** they cannot grow. So the word pointer
is fixed for the array's life, and item 2's entry check is trivially
loop-invariant.

**What it opens:**

- `@m[$i;$j]` as a leaf stencil;
- a column or a transpose as a strided view;
- hyper operators over whole shaped arrays;
- item 7.

**Measured so far:** the flat-array matmul already shows what contiguity alone
buys under today's `--cnp`, 2,734 → 410 ms, before item 2's leaf reads.

**The risk:** this is a representation change of what 44ed66bc just landed. The
unpack census and roast's shaped-array files are the gate.

### 4. Reductions the gfortran way: exact semantics outside the hot loop (S–M)

**The Fortran lesson:** inline reductions and the two-loop MIN/MAX scheme
(section 5).

**a. `.min`/`.max` over a `num` array.** This is the measured outlier:
193–398 ms against 6 ms, and the first call unpacks the array for good. The
packed fold takes only `.sum` for `num` ("a `num` array folds `.sum` only",
[Builtins.cpp:6232](../../../src/Builtins.cpp)), presumably because of the
semantics.

Rakudo and Raku++ agree on those semantics, as probed today:

- `.max` is NaN if any element is NaN;
- `.min` skips NaNs, and is NaN only when every element is;
- a tie keeps the first element, so `(0e0, -0e0).max` is `0e0` and `(-0e0,
  0e0).max` is `-0e0`.

All three fit the two-loop shape:

1. A vectorizable pass computes the plain `>` maximum (or `<` minimum) over
   the elements that are not NaN, and counts the NaNs.
2. The count answers the NaN cases at once: any NaN for `.max`, all NaNs for
   `.min`. A best value of zero triggers a rescan for the first ±0, which is
   rare.

This is item 7 of PACKED-ARRAY batch 3 finished for `num`, and P1 of
NUMERIC-PARALLEL.

**b. An Int `.sum` in blocks.** Today every add goes through `add_ovf`. That
measures at the speed of C's checked loop (2.9 ms), but checked adds do not
vectorize: the plain loop is 1.2–1.7 ms. The two-loop version:

1. Per block of, say, 4,096 words, one vectorizable pass finds the maximum
   absolute value.
2. If that maximum times the block length cannot overflow, a plain vectorized
   sum of the block follows.
3. Otherwise the block goes the `add_ovf` way, and on through `applyArith`, as
   today.

The answer is the same, and the gain is about 2× on the common case. Small:
`.sum` is already within 2× of the ceiling.

**c. Early exit.** `so @a.any > $x`, `.first`, `all`: early exit over the words,
as `gfc_conv_intrinsic_anyall` does, once these forms read packed words at
all.

### 5. A run-time overlap test: in place, forward, reversed, or a temporary (M)

**The Fortran lesson:** gfortran's `gfc_dep_resolver` with loop reversal, and
Flang's in-place bufferization and run-time disjointness checks (section 3).

**Where it fits:** NUMERIC-PARALLEL P1 (packed out) and P2 (fusion) have to
decide where a result's words go. Fortran's answer:

- `@a = @a »*» 2e0` and `@c = @a »+« @b` with `@c` bound to `@a` can write
  into the target's words, with no temporary, when its payload is uniquely
  owned. Every read is at the written index, which is Flang's
  `isDesignatingArrayInOrder` case.
- **Slices with offsets:** `@a[1..*] = @a[0..*-2]` and `@d = @a[1..*-1] »-«
  @a[0..*-2]` (the finite-difference idiom, Fortran's `a(2:n) - a(1:n-1)`) can
  read the slices as strided leaves instead of materializing them.
- **Overlap:** decided by comparing two ranges: forward, reversed, or a
  temporary.

**What it costs:** the check is two compares per operand at run time. The
saving is the boxed intermediate list, which is the measured bottleneck of
today's hyper kernels.

### 6. Kernels compiled per instruction set, chosen once (S–M)

**The Fortran lesson:** libgfortran's MATMUL dispatch (section 6).

**Today:** the release workflow builds with no `-march`
([release.yml](../../../.github/workflows/release.yml)). That is correct for
a portable binary, but it means every x86-64 download runs the hyper kernels
and the folds on the baseline x86-64 vector unit (SSE2): half the width of
AVX2.

**The libgfortran recipe:**

1. Compile the few hot kernels a second time with
   `__attribute__((target("avx2")))`. Those are the hyper loops, the packed
   folds, P2's fused chunk loop, and item 7.
2. Pick the variant at first use with `__builtin_cpu_supports`.
3. Cache it in a function pointer.

**The constraints:**

- Contraction stays off, so the answers do not change; P2 already requires
  that.
- MSVC has no `target` attribute, so the AVX2 variant goes in a translation
  unit of its own built with `/arch:AVX2`. That keeps the Windows build
  honest.
- On arm64, NEON is the baseline, so there is nothing to do on Apple silicon.

### 7. Matrix kernels over shaped arrays (M; after item 3; the maintainer's decision)

NUMERIC-PARALLEL lists matrix kernels as a non-goal "after P6, if at all", and
a BLAS dependency as a non-goal outright. Fortran's evidence bears on both:

- **No dependency needed:** libgfortran's speed comes from one 563-line m4
  template (the blocked kernel plus the strided fallbacks) with no external
  library, 2–4× the naive loop at large sizes.
- **Small sizes:** a plain loop in cache-friendly order serves them; gfortran
  inlines below 30³.

**What Raku lacks:** core Raku has no `matmul`, so this is a surface question
first. One option is an engine-answered module, in the way `use JSON::Native`
is answered. Fortran compilers do not recognize hand-written triple loops
either: MATMUL is an intrinsic.

### 8. `is pure` routines as kernel bodies (M–L)

**The Fortran lesson:** `elemental` procedures and LFortran's inlining (section
8).

**What exists today:**

- `--cnp` already compiles a small sub in place instead of calling it
  (07508052).
- `Cnp.cpp` knows which built-in methods are pure (`isPureMethod`).
- The `is pure` trait is parsed
  ([Parser.cpp:13454](../../../src/Parser.cpp)) and unread.

**The change:** `@a».&f` and `@a.map(&f)` over packed words, with `f` a small
numeric sub that `--cnp` can compile, run as one kernel over the words. The
output is packed when it goes to a native target.

**What must hold:**

- Order matters only if `f` has effects. `is pure`, or a body the scan proves
  effect-free, gives the licence.
- That is the same decision as NUMERIC-PARALLEL P7, which is about running such
  code in parallel. This item is about compiling it, and it is useful on one
  thread.

---

## Already in NUMERIC-PARALLEL-PLAN, and Fortran agrees

- **P2's fusion:** the gfortran scalarizer and HLFIR's elemental inlining. The
  same design, recognised on the tree and lowered once.
- **P2's chunk-sized temporaries:** `-fstack-arrays` and `StackArrays.cpp`.
- **P3's left-to-right default with `race` as the opt-in:** gfortran's and
  Flang's defaults (section 7).
- **P4's reductions that never build the list:** Fortran 2023 added a `reduce`
  locality clause to `do concurrent` for exactly the `race for { $s += … }`
  problem. That is recalled, not read here. Raku's `[+] race for` says it
  without a clause.
- **The non-goal of hand-written SIMD:** section 10.

---

## What not to borrow

- **Assuming no alias.** Fortran's rule is a static promise that Raku does not
  make. Check payload identity at run time, once per loop or operation, as
  Flang itself does whenever its analysis is unsure.
- **Column-major order.** What matters is contiguity and unit stride in the
  innermost loop. Raku's `@m[$i;$j]` is row-major by its semantics, so the
  innermost loop should walk `$j`.
- **A global fast-math switch.** Bit identity with the generic path, and with
  Rakudo, is the engine's rule. The opt-in is `race`, per operation, not a
  flag per process.
- **Fused multiply-add by default.** gfortran inherits GCC's default of
  `-ffp-contract=fast`, which is recalled, not read here. So a Fortran
  program's last bits can differ between a machine with FMA and one without.
  NUMERIC-PARALLEL P2's ban on contraction is the right call for an engine
  that promises Rakudo's answers.
- **Whole-program passes:** type-based alias metadata from the dummy-argument
  rule, LFortran's `pass_array_by_data`, and common elimination of user
  functions assumed pure. All need guarantees an interpreter with `EVAL` and
  `augment` does not have.
- **Automatic repacking of strided views.** Fatigue2's 2× slowdown is the
  warning. Leave it until a real program shows the need, and gate it on a
  reuse heuristic.

---

## Appendix: re-measuring

The scripts are below. The engine is `build/rakupp`, the C is `clang -O3`
(plus the flags each table names), and the times are best of 3, interleaved.

`num.raku` (the modes `sum`, `dot-hyper`, `dot-for`, `dot-loop`, `axpy-loop`,
`matmul`; options go before the mode):

```raku
sub MAIN(Str $mode, Int :$n = 10_000_000, Int :$m = 200) {
    my $N = $n;
    my num @a = (^$N) »*» 1e-7;
    my num @b = (^$N) »*» -1e-7;
    my $t0 = now;
    my $chk;
    given $mode {
        when 'sum'       { $chk = @a.sum }
        when 'dot-hyper' { $chk = [+] (@a »*« @b) }
        when 'dot-for'   { my num $s = 0e0; for ^$N -> int $i { $s += @a[$i] * @b[$i] }; $chk = $s }
        when 'dot-loop'  { my num $s = 0e0; my int $nn = $N; loop (my int $i = 0; $i < $nn; $i++) { $s = $s + @a[$i] * @b[$i] }; $chk = $s }
        when 'axpy-loop' { my num @c = @b; my int $nn = $N; loop (my int $i = 0; $i < $nn; $i++) { @c[$i] = 2e0 * @a[$i] + @b[$i] }; $chk = @c[$N div 2] }
        when 'matmul' {
            my int $M = $m;
            my num @x[$M;$M]; my num @y[$M;$M]; my num @z[$M;$M];
            loop (my int $i = 0; $i < $M; $i++) { loop (my int $j = 0; $j < $M; $j++) { @x[$i;$j] = ($i + $j).Num; @y[$i;$j] = ($i - $j).Num } }
            $t0 = now;
            loop (my int $i = 0; $i < $M; $i++) {
                loop (my int $j = 0; $j < $M; $j++) {
                    my num $s = 0e0;
                    loop (my int $k = 0; $k < $M; $k++) { $s = $s + @x[$i;$k] * @y[$k;$j] }
                    @z[$i;$j] = $s;
                }
            }
            $chk = @z[$M div 2; $M div 3];
        }
    }
    printf "%-10s %8.3f s  chk=%s\n", $mode, now - $t0, $chk;
}
```

The `for` binding shapes, under `--cnp`:

```raku
my int $nn = 10_000_000;
my num @a = (^$nn) »*» 1e-7;
my num @b = (^$nn) »*» -1e-7;
my $t0 = now; my num $s = 0e0;
for ^$nn -> $i { $s = $s + @a[$i] * @b[$i] }
printf "for-untyped %.3f s %s\n", now - $t0, $s;
$t0 = now; $s = 0e0;
for ^$nn -> int $i { $s = $s + @a[$i] * @b[$i] }
printf "for-int     %.3f s %s\n", now - $t0, $s;
```

The cost of one read under `--cnp`: the `loop (my int $i …)` bodies `$s = $s
+ $i * 1.5e0`, `$s = $s + @a[$i]` and `$s = $s + @a[$i] * @a[$i]`, each over
10⁷ iterations.

The flat matmul: `num.raku`'s matmul with `my num @x = …` of `$M*$M` elements
and the subscripts `@x[$i * $M + $k]`, `@y[$k * $M + $j]` and `@z[$i * $M +
$j]`.

`--exe -O`, the typed loop (the untyped one drops `num`/`int`, and the Int pair
uses `$s = $s + $i * 3`):

```raku
sub f(int $nn --> num) {
    my num $s = 0e0;
    loop (my int $i = 0; $i < $nn; $i++) { $s = $s + $i * 1.5e0 }
    $s
}
my $t0 = now;
my $s = f(10_000_000);
printf "scalar-in-sub %.3f s %s\n", now - $t0, $s;
```

```bash
build/rakupp --exe -O -o /tmp/typed typed.raku && /tmp/typed
```

The packed folds:

```raku
my int @i = ^10_000_000;
my num @n = (^10_000_000) »*» 1e-7;
my $t0 = now; my $s = @i.sum; printf "int-sum %.2f ms %s\n", (now - $t0) * 1e3, $s;
$t0 = now; $s = @n.max; printf "num-max %.2f ms %s\n", (now - $t0) * 1e3, $s;
```

`RAKUPP_PACKED_CENSUS=1` on the second shows the unpack: one call site, 10⁷
elements.

The C ceiling is the same arrays and the same four loops, in one file, timed
with `clock_gettime(CLOCK_MONOTONIC)`, best of 5. The reductions accumulate
into a `double` declared outside the loop, and the matmul is `z[i*M+j] = Σk
x[i*M+k]*y[k*M+j]`.
