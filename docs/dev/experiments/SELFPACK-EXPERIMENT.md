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

- **Ints.** Done on 2026-11-10 for the interpreter: see
  [Ints](#ints-2026-11-10) below. Compiled code still packs Nums only.
- **Arrays that start empty.** An array grown by `.push` from empty never
  packs, because only an assignment packs. The interpreter's `typedDefault`
  already makes an empty packed body for a native array; an untyped one would
  need the first push to decide.
- **Construction straight into words.** Filling the words from the Seq, not
  from a built list, would make the peak 8 bytes an element, not 72.
- **More in the lanes.** `@a[$i] op= E`, `.elems` in a loop bound, and `for @a`
  in an `-O` lane.
- **Objects**, the other half of JSC-VALUES' finding: OBJECT-SLOTS-PLAN.

## Ints (2026-11-10)

*On branch `claude/selfpack-ints`, from 62474b1f, where a `Value` is 32 bytes.
Apple M4, Release build. It follows
[VALUE-POOL-INDEX-2026-11-10.md](../findings/VALUE-POOL-INDEX-2026-11-10.md),
whose verdict was that element size is won by packing more arrays, not by
shrinking `Value` again.*

**An untyped Array of plain machine Ints packs itself too, in the
interpreter.** The words are the ones a `my int @a` has, 8 bytes an element
where a `Value` is 32. None of a native array's rules shows through:

- a Bool stored stays a Bool (a `my int @a` holds `True` as 1);
- a step, an `op=` or a `.sum` that leaves 64 bits is a big Int and unpacks
  the array (a `my int @a` wraps);
- a store past the end with a gap unpacks, and the gap is holes.

**What changed beyond letting `selfPack` take Ints:**

- **`my @a = LO .. HI` fills the words from the Range.** No `Value` is built,
  so the peak is the words alone. A Range that is endless, fractional, of
  Strs or of big Ints goes the old way (`plainIntRange`).
- **One compiled loop kernel takes an Int array in either form**
  (`src/IntKernel.cpp`). The nodes that read, store, `op=` and step an element
  look on each access whether the container is words. A word access bails out
  of range or on a result past 64 bits, with the words put back. A loop that
  pushes takes the array as a list: it unpacks on entry.
  - The first version compiled a kernel for the form it met, as the Num
    kernels do. An array that unpacked between two entries of one loop then
    never entered its kernel again: 60 passes over 100,000 elements with one
    `.join` after the third retired 18,328M instructions where they retire
    1,169M now.
  - With the word forms beside it, the compiler stopped inlining the list
    lookup into the read node, 16 instructions a read. `contSlot` is
    `always_inline` now, and a list read costs less than on `main`.
- **An Array that unpacked is a plain Array again.** A body that had unpacked
  kept its Array off every plain-Array fast path for good: the loop kernels,
  the `.push`, `.elems`, `.kv` and `.join` shortcuts. `Value::noWords()` lets
  those lanes take it, and a kernel settles the variable on the list itself
  (`Value::packedSettle`).
- **An Array keeps its `.WHICH` through every change of form** (`whichCarry`,
  `src/Which.cpp`). The serial is the payload's, and packing or unpacking
  replaces the payload. `my @a = 1e0 xx 4; my $w = @a.WHICH; @a = 2e0 xx 10`
  gave another WHICH on `main` already, for Nums; with Ints packed,
  `t/regression/issue-221-array-list-which-identity.raku` failed under
  `RAKUPP_SELFPACK_MIN=1`.
- **Packing and unpacking are one tight pass each.** Packing compares each
  element with a plain Int's bytes and gives up at the first that differs;
  unpacking builds each `Value` in its slot. They cost 65 and 61 instructions
  an element as first written, and 25 and 28 now.
- **`--cnp`:** a view of an untyped Array's Int words takes no Bool as 1
  (bit 8 of `RkCnpView::tag`, `rk_st_idxsetw`).
- **Compiled code packs Nums only** (`rtSelfPack`). With Ints packed, an
  `--exe -O` counting loop (`@h[$x % 1000]++`) retired 45% more instructions
  and a store loop 21% more: the compiled statements and lanes read and store
  Num words, and sent every Int word the long way.

`RAKUPP_NO_SELFPACK_INT=1` turns the Int kind off and leaves Nums packing.

**Measured against `main` (62474b1f), interpreted:** instructions retired,
wall time and peak memory from `/usr/bin/time -l`, the least of 7 runs each.
The wall times have a 10 ms step, and the machine was loaded.

| program | instructions, M | wall, ms | peak, MB |
|---|--:|--:|--:|
| `my @a = 1..2_000_000`, summed by index | 617 → 378 | 30 → 10 | 72.1 → 24.5 |
| `0 xx 1_000_000`, stored by index, summed by `for` | 2,340 → 2,138 | 100 → 80 | 133.1 → 57.4 |
| a sieve to 2,000,000 | 2,396 → 1,395 | 190 → 70 | 197.8 → 107.2 |
| 3M `@h[$x % 1000]++` | 1,058 → 817 | 40 → 30 | 10.3 → 9.5 |
| 300,000 ten-element arrays, one store each | 6,544 → 5,913 | 260 → 240 | 9.5 → 9.6 |
| a million Ints through `.map`, `.sort`, `.grep`, `.reverse`, then summed by index | 3,974 → 4,075 | 170 → 170 | 192.5 → 207.8 |
| `tools/bench/sortnums.raku` | 234 → 240 | 10 → 10 | 17.5 → 18.4 |
| `tools/bench/arraypush.raku` (never packs) | 215 → 209 | 10 → 10 | 36.7 → 36.9 |

The sixth and seventh rows are the cost. An array that packs and is then
asked for its elements as Values pays for both passes and keeps the words
beside the list: 2.5% more instructions and 5–8% more memory. On an earlier
build of the branch, single runs, the other kernels of `tools/bench` and the
20 programs of `examples/` (Int packing off against on, same binary) were
within about 1% either way.

**Checked:**

- Roast: 1,425 of 1,425 files on the tree as committed.
- `t/run.raku`: 2,175 of 2,175 checks, and 2,174 of 2,175 under
  `RAKUPP_SELFPACK_MIN=1`, on the tree before the `.WHICH` change; the one
  failure was the `.WHICH` case above. The run on the tree as committed had
  not finished when this was written (402 checks in, none failing).
- `t/exe/fuzz.raku --arrays`: no program mismatching, 60 carrying a lane, on
  the tree before the `.WHICH` change.
- `t/regression/selfpack-int-arrays.raku` passes under Rakudo 2026.09,
  interpreted, with `RAKUPP_SELFPACK_MIN=1`, `RAKUPP_NO_SELFPACK=1`,
  `RAKUPP_NO_SELFPACK_INT=1` and `RAKUPP_NO_KERNELS=1`, under `--cnp`, and as
  an `--exe -O` binary.

**Left open:** [TODO-FOLLOW-UPS.md](../plans/TODO-FOLLOW-UPS.md), under
*Performance*: compiled code, the cost of an array that unpacks, lists built
as Values first, arrays that start empty, and the gates not run.

## Reproduce

The quickest look is [selfpack/demo.raku](selfpack/demo.raku). It runs one
untyped matrix product four ways with one binary: interpreted and as an
`--exe -O` binary, each with packing off and on. Bun runs the same program in
JavaScript beside them when it is on `PATH`. The script checks that every
answer agrees. At 5.6.3-62 it printed:

```
untyped Raku: a 200×200 matrix product, 8,000,000 multiply-adds

                          packing off   packing on   speed-up
  interpreted                    1693           60        28×
  --exe -O                        260          6.1        42×
  Bun (JavaScript)                             6.4

every run answers 5999750
```

The times are milliseconds spent in the loops, best of 3.

```bash
cmake --build build -j10
build/rakupp docs/dev/experiments/selfpack/demo.raku   # the demo above (--n=300 for a bigger matrix)
docs/dev/experiments/selfpack/perop.sh               # the per-operation table
build/rakupp t/run.raku                              # the suite
RAKUPP_SELFPACK_MIN=1 build/rakupp t/run.raku        # …with every Num array packed
build/rakupp t/exe/fuzz.raku --arrays                # the array lanes against the boxed interpreter
```
