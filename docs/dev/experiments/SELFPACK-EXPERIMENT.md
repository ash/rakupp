# Self-packing arrays: an untyped `@a` of Nums kept as doubles

*2026-11-07, a prototype on branch `claude/selfpack-arrays`, from
5.6.3-46-gf402eeb2. Apple M4, macOS 26.6.2, Release build, load average 3–4
for the timings. It follows
[JSC-VALUES-2026-11-07.md](../findings/JSC-VALUES-2026-11-07.md), which read
JavaScriptCore's sources against the
[perf review's](../findings/PERF-REVIEW-2026-11-02.md) kernels. That note found
that Raku++'s gap to Bun is in arrays and objects, not in scalar code, and
that JSC keeps an array whose every element is a double as raw doubles
(`ArrayWithDouble`), recorded in the array itself, so compiled code checks the
kind once and then runs on machine words. This is that idea for an untyped
Raku Array of Nums.*

---

## The short answer

- **An untyped `@a` of Nums now runs at the speed of a `my num @a`, and
  compiled, at Bun's.** On the review's three array kernels, untyped:

  | ns per operation | C | Bun (FTL) | `--exe -O` before → after | interpreted before → after |
  |---|--:|--:|--:|--:|
  | dot | 0.44 | 0.69 | 25.7 → **0.48** (54×) | 100 → **4.9** (21×) |
  | matmul | 0.45 | 0.81 | 34.8 → **1.10** (32×) | 216 → **9.3** (23×) |
  | stencil | 0.11 | 0.79 | 39.2 → **2.56** (15×) | 272 → **7.1** (38×) |

  - Compiled, untyped `dot` runs faster than Bun's top tier, and faster than
    the same kernel written with `num` types (1.0 ns).
  - `matmul` runs at 1.4× Bun's time, and `stencil` at 3.2× (see
    [Why stencil lags](#why-stencil-lags)).
  - Interpreted, all three now run 1.6–2.1× faster than JSC's own
    interpreter, the LLInt (9.3, 15.1 and 14.8 ns).
- **Packing alone buys little. The lanes that read the words buy the rest.**
  With only the representation changed (the first step below), compiled `dot`
  went 0.140 → 0.081 s and interpreted `stencil` 0.448 → 0.326 s. Compiled
  `stencil` got slower (0.072 → 0.105 s): its stores unpacked the array at
  once, and an unpacked packed body costs an extra check on every access.
  JSC agrees: `ArrayWithDouble` pays because the FTL compiles against it, not
  because doubles are smaller.
- **No answer changed that the checks below can see.**
  - `t/run.raku`: 1,994 of 1,994 checks pass, and 1,995 of 1,995 with
    `RAKUPP_SELFPACK_MIN=1`, which packs every Num array however short.
  - `t/exe/fuzz.raku`: 326 of 326 programs agree. That includes a new matrix
    of 70 array programs whose oracle is the interpreter with packing off; 60
    of them carry a lane.
  - A new regression case, `t/regression/selfpack-arrays.raku`, passes
    interpreted and compiled, with packing on and off, and under Rakudo
    2026.09.
- **The costs.**
  - Without a loop kernel, the plain tree-walker runs the matmul kernel 16%
    slower (0.411 → 0.477 s). dot is level and stencil 28% faster.
  - Peak memory is not lower. A million-Num array peaks at 142 MB against 135:
    packing happens after the Values are built, so the words exist beside them
    for a moment.
  - An array that unpacks keeps both forms, 72 bytes an element.
  - `src/Codegen.cpp`'s line ceiling went from 10,500 to 10,620.
  - perf-guard is level. Best of two rounds per kernel, most are within ±3%.
    fib and asg read +6% and +8%, but they retire the same instructions as on
    main (1,137M against 1,139M, and 1,720M both), so that is layout or load.
- **The prototype found a bug that predates it.** A C-style
  `loop (my $a = 0; …)` inside an expression `try { … }` or `do { … }` did not
  build under `--exe`, with or without `-O`: the generated C++ named `v_sa`
  and never declared it. The same missing scope made
  `my $j = 7; my $r = do { my $j = 100; $j }` leave `$j` at 100, compiled.
  Both are fixed on their own branch, `claude/exe-do-try-block-scope`.

## What the prototype does

**The representation is the one `my num @a` already has.** PACKED-ARRAY-PLAN
gave native arrays a packed body, `PackedArr`: the elements as 64-bit words.
The first time anything asks for them as Values (`Value::arr()`), the body
unpacks once into a `ValueList`, and every holder sees the boxed list from
then on. The prototype adds one flag, `PackedArr::selfPacked`, and lets an
untyped Array use the same body when every element is a plain `Num`.

**When an Array packs itself** (`selfPack`, `src/Value.cpp`):

- **`@a = LIST`** (the interpreter's `arrayStore`) **and `my @a = LIST` under
  `--exe`** (`rtSelfPack`). All of these must hold:
  - the Array has no type, `is default`, shape, laziness, bound element or tag;
  - nothing else holds its storage;
  - every element is a plain Num (`plainWordValue`), and there are at least
    `RAKUPP_SELFPACK_MIN` of them, 8 by default.
- **`my @v = @u` from a packed Num array** (`selfPackCopy`). The words are
  copied. The general copy read @u's Values, which unpacked @u for good.

**What keeps it packed.** The lanes `my num @a` already had now serve a
self-packed array too:

- an element read with an integer subscript in range;
- a store of a plain Num in range, or exactly at the end;
- `.push` of plain Nums;
- `.elems`, `.sum`, `.min` and `.max`;
- `for @a`;
- `--cnp`'s element ops.

Two of these needed a change for an untyped array:

- A native array grows past its end with 0e0 in the gap. An untyped one has
  holes there, so a self-packed array grows only at its end.
- The interpreter's store lane took `@a[$i] = EXPR` only when the reads inside
  EXPR had a literal or a plain variable as their subscript. It now takes any
  subscript with no effects, such as `@u[$i - 1]`, which a stencil needs.

**What unpacks it.** Everything else unpacks it, by construction:

- a store of anything but a plain Num: an Int, a Rat, an allomorph, a Num
  with a role mixed in, Nil;
- a bind (`@a[0] := $x`);
- an element handed out as a container (an `is rw` argument);
- a store past the end with a gap;
- every path that does not know about words.

The answer after an unpack is the Array the boxed path would have built.

**The new fast paths over the words:**

1. **The interpreter's loop kernels** (`src/IntKernel.cpp`). A packed Num
   array is now a container a kernel may use:
   - `@a[I]` reads the word (`PNGet`);
   - `@a[I] = E`, with `E` a Num, stores the word (`PNSet`);
   - out of range, the kernel bails.

   A bail puts back every word the kernel stored, through `RtWordUndo`: a log
   of words that becomes a copy of the arrays once it outgrows them, as the
   kernels' own undo for boxed containers does. The kernel refuses `++`,
   `op=`, `.push` and stores of any other type on such an array.
2. **`--exe -O`'s unboxed loop lanes** (UNBOX-PLAN, `src/Codegen.cpp`). A lane
   may now read `@a[I]` and store `@a[I] = E`.
   - **On entry** it checks each array once (`rtNumWords`): a live packed
     body of Nums, no `is default`, no worker thread running. It keeps the
     words' address and length in locals.
   - **On each access** it compares the subscript with the length, and bails
     past it.
   - **A bail** undoes the lane's stores, as the kernels' does.

   `dot`'s whole loop nest becomes one lane: two entry checks, then
   `s += rtWordN(a, i) * rtWordN(b, i)` on doubles.
3. **`--exe` outside a lane.**
   - `rtIndexGet` reads a packed array's word.
   - `@a[$i] = v;` in statement position stores a Num into a self-packed
     array's word (`rtSelfPackedStore`). Before, `rtIndexRef` would have
     unpacked the array.

`RAKUPP_NO_SELFPACK=1` switches all of it off at run time, in the interpreter
and in a compiled binary. The "before" columns were measured that way, with
the same binaries.

## The numbers

From [selfpack/perop.sh](selfpack/perop.sh): best of 3 wall-clock times, less
start-up (9–13 ms), divided by the kernel's operations as the review counts
them. The packed runs are 8–125× longer, so start-up does not swamp them.

**Self-packing, ns per operation** (rows in the order the script prints them):

| kernel | interpreted, off | interpreted, on | `--exe -O`, off | `--exe -O`, on |
|---|--:|--:|--:|--:|
| dot | 100.4 | 4.85 | 25.7 | 0.48 |
| matmul | 215.6 | 9.27 | 34.8 | 1.10 |
| stencil | 271.8 | 7.13 | 39.2 | 2.56 |

**The references, ns per operation,** from the same machine on the same day:

| kernel | C `-O2` | Bun FTL | Bun LLInt | Raku++ `--exe -O`, `num` typed |
|---|--:|--:|--:|--:|
| dot | 0.44 | 0.69 | 9.3 | 1.0 |
| matmul | 0.45 | 0.81 | 15.1 | 3.7 |
| stencil | 0.11 | 0.79 | 14.8 | 1.0 |

The C, Bun and typed figures are from
[JSC-VALUES-2026-11-07.md](../findings/JSC-VALUES-2026-11-07.md).

**Each of the three steps on its own,** in seconds for the review's kernels
as written:

| step | dot interp | dot `-O` | matmul interp | matmul `-O` | stencil interp | stencil `-O` |
|---|--:|--:|--:|--:|--:|--:|
| off | 0.536 | 0.140 | 0.385 | 0.067 | 0.448 | 0.072 |
| packing and the existing lanes | 0.502 | 0.081 | 0.454 | 0.047 | 0.326 | 0.105 |
| + the `-O` lane | — | 0.0115 | — | 0.0129 | — | 0.0135 |
| + the loop kernels | 0.034 | — | 0.028 | — | 0.021 | — |

The last two rows are mostly start-up, which is why the per-operation table
above uses longer runs.

## Why stencil lags

Compiled, `stencil` runs at 2.56 ns an update, against Bun's 0.79 and the
`num`-typed version's 1.0. Per element, its lane does all of this:

- two overflow checks on `$i - 1` and `$i + 1`;
- three bounds checks;
- a call to the undo log before the store. After the log compacts into a copy
  of the arrays, that call is one branch.

None of these vectorizes the way C's loop does. The undo log is the real
difference from JSC:

- **A lane here bails by running the whole loop again, boxed, from its
  start.** That is UNBOX-PLAN's rule, chosen because resuming mid-loop is
  where such passes get their wrong answers. So every store the lane made
  must be taken back first.
- **JSC's OSR exit resumes at the exact bytecode, with the state rebuilt.**
  Its stores never need undoing.

To close this, hoist the bounds checks out of the loop where the range and
the length are known at entry, as the DFG's integer-range phase does.
`dot` and `matmul` have no stores in their inner loop and are already level
with Bun.

## What it does not do yet

- **Ints.** JSC's `ArrayWithInt32` has the same shape. The int words exist
  (`my int @a`). The difference is that a big Int store must unpack.
- **Arrays that start empty.** An array grown by `.push` from empty never
  packs, because only an assignment packs. The interpreter's `typedDefault`
  already makes an empty packed body for a native array; an untyped one would
  need the first push to decide.
- **Construction straight into words.** Filling the words from the Seq, not
  from a built list, would make the peak 8 bytes an element, not 72.
- **More in the lanes.** `@a[$i] op= E`, `.elems` in a loop bound, and `for @a`
  in an `-O` lane.
- **Objects**, the other half of JSC-VALUES' finding: OBJECT-SLOTS-PLAN.

## Reproduce

```bash
cmake --build build -j10
docs/dev/experiments/selfpack/perop.sh               # the per-operation table
build/rakupp t/run.raku                              # the suite
RAKUPP_SELFPACK_MIN=1 build/rakupp t/run.raku        # …with every Num array packed
build/rakupp t/exe/fuzz.raku --arrays                # the array lanes against the boxed interpreter
```
