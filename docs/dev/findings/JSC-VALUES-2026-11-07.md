# How Bun runs untyped JavaScript near C, read from its sources

*2026-11-07, on an Apple M4, macOS 26.6.2, load average 3–6. Bun 1.4.4
(38afee6c), whose JavaScriptCore is oven-sh/WebKit at d6375c72 (pinned in
Bun's `scripts/build/deps/webkit.ts`). Raku++ 5.6.3-46-gf402eeb2, Release
build. C is Apple clang 21 `-O2 -ffp-contract=off`. Every time is the best of
3 runs. Read the ratios, not the last digit.*

The question: a JavaScript variable can hold an int, a double, a string or an
object, and Bun still runs numeric programs near C. Raku++ is far behind on
the same programs. How does JavaScriptCore (JSC) handle its values, and why
is it fast?

The method:

- **Sources.** Bun's source at the bun-v1.4.4 tag, and the JSC directory of
  the WebKit commit Bun pins. File references below are into that WebKit
  tree.
- **Kernels.** The kernels of
  [PERF-REVIEW-2026-11-02.md](PERF-REVIEW-2026-11-02.md), ported line for
  line to untyped JavaScript in
  [jsc-values-2026-11/ks.js](jsc-values-2026-11/ks.js).
- **Tiers.** Each kernel ran under each of JSC's four tiers, which Bun lets
  you switch off through `BUN_JSC_*` environment variables.
- **Graphs.** JSC's own dumps of the optimized code for three of the loops.
- **Raku++.** The same kernels re-timed in the same sitting, interpreted and
  under `--exe -O`.

---

## The short answer

- **Bun contributes nothing to this speed. JSC does all of it.** Bun's
  `JSValue` (`src/jsc/JSValue.rs`; Bun 1.4.4 is Rust, with no Zig left) is a
  `#[repr(transparent)]` wrapper over JSC's 64-bit encoding.
- **The 8-byte NaN-boxed value is not why JSC is fast.** JSC's interpreter,
  the LLInt, uses that box and runs these kernels at 9–19 ns per operation
  (intloop aside), 17–135× C. On scalar loops and calls, Raku++'s
  interpreter beats it by 2–9×.
- **The speed comes from code in which values are not boxed at all.** JSC's
  top tier, the FTL, compiles a hot function from types the lower tiers
  observed:
  - variables live in registers as raw `int32`, `int52` or `double`;
  - arrays that hold only doubles store raw doubles;
  - an object field is a load at a fixed offset;
  - a method call whose target is proven is inlined;
  - the checks run once, at function entry or hoisted out of the loop.

  If a check fails, the code exits to the interpreter (OSR exit) and is
  recompiled with the wider type.
- **When no machine type fits, JS is slow too.** In `intloop`, `i*i` grows
  past 2³¹. JS has no 64-bit integer, so the FTL computes `i*i` as a double
  and `%` as a call to `fmod`. It runs at 34 ns per iteration, 13× C and 11×
  slower than Raku++ `--exe -O`.
- **Raku++'s gap to Bun is in arrays and objects.** On scalar loops and
  recursion, Raku++ already matches or beats Bun, both interpreted and
  compiled. (A plain call in a loop, `subcall`, is the exception: 12× behind
  compiled, though 6× ahead interpreted.)
  Compiled, untyped Raku++ is 36–48× behind Bun on array loops and 150–280×
  behind on objects. Interpreted, it is 11–23× behind JSC's interpreter on
  the same kernels.
- **Not knowing the type costs JSC 1.35×. It costs Raku++ 9–38×.** JSC's
  general path is cheap because its box is 8 bytes, has no reference count
  and is tested with one compare. Raku++'s general path copies a 64-byte
  `Value` with three atomically counted members.
- **JSC learns types by watching. An ahead-of-time compiler can learn them
  from the data.** A JSC array records its element kind in its own header
  (`IndexingType`), and an object records its layout in its `Structure`. The
  compiled code checks that header once at entry and then runs the
  specialised loop. Raku++ could do the same without a JIT:
  - arrays that choose packed storage for themselves;
  - objects laid out by their class;
  - a guarded fast version of each loop, with the general loop as the
    fallback.

---

## 1. The numbers

**Nanoseconds per operation.** An operation is one iteration for
intloop/numloop, one multiply-add for dot/matmul, one point update for
stencil, one call for fib, one loop iteration for subcall/meth, and one step
for nbody. Bun's times are taken inside the process: start-up is left out,
JIT compilation is included. Raku++ and C are wall-clock times less
start-up, measured on longer runs where a short one would vanish into
start-up (see [Reproduce](#reproduce)).

| kernel | C | Bun (FTL) | Bun, interpreter only (LLInt) | Raku++ interp | Raku++ `--exe -O` untyped | Raku++ `--exe -O` native |
|---|--:|--:|--:|--:|--:|--:|
| intloop | 2.6 | **34.4** | 50.6 | 5.8 | **3.0** | 3.1 |
| numloop | 0.78 | 0.58 | 13.6 | 6.9 | 0.78 | 0.87 |
| fib | 0.52 | 1.16 | 14.8 | 4.0 | 1.27 | 1.23 |
| subcall | | 0.73 | 16.6 | 2.9 | 8.7 | |
| dot | 0.44 | 0.69 | 9.3 | 101 | 24.9 | 1.0 |
| matmul | 0.45 | 0.81 | 15.1 | 217 | 34.0 | 3.7 |
| stencil | 0.11 | 0.79 | 14.8 | 258 | 37.9 | 1.0 |
| meth | | 0.53 | 19.2 | 234 | 81 | |
| nbody | 28 | 59 | 1,328 | 30,000 | 16,600 | |

The gap between the two engines, untyped code throughout. Below 1 means
Raku++ is faster.

| kernel | Raku++ `--exe -O` ÷ Bun FTL | Raku++ interp ÷ LLInt |
|---|--:|--:|
| intloop | **0.09** | 0.11 |
| numloop | 1.3 | 0.51 |
| fib | 1.1 | 0.27 |
| subcall | 12 | 0.17 |
| dot | 36 | 11 |
| matmul | 42 | 14 |
| stencil | 48 | 17 |
| meth | 153 | 12 |
| nbody | 282 | 23 |

- **The scalar rows are not where the gap is.** IntKernel and `--exe -O`
  already run those shapes on `int64` and `double`.
- **Raku++ has something JS lacks: 64-bit integers.** `intloop` with the
  operands kept inside 32 bits (`intloop32` in ks.js) runs at 3.2 ns under
  the FTL, level with C.
- **The array and object rows are the gap, and they show up in both tiers.**
  JSC's interpreter, with boxes and no JIT, does an array multiply-add in 9 ns
  where Raku++'s interpreter takes 100–260 ns. That difference is
  representation, not JIT.
- **numloop native is 0.87 ns now.** PERF-REVIEW measured 39 ns there (its
  "written `num` compiles to an `int` lane" defect), so that defect is gone.

### What each JSC tier buys

The same kernels with the upper tiers switched off
(`BUN_JSC_useJIT=false`, `useDFGJIT=false`, `useFTLJIT=false`), in ns per
operation:

| kernel | LLInt | + Baseline JIT | + DFG | + FTL |
|---|--:|--:|--:|--:|
| intloop | 50.6 | 45.9 | 33.1 | 34.4 |
| numloop | 13.6 | 3.96 | 1.06 | 0.58 |
| dot | 9.3 | 4.40 | 2.19 | 0.69 |
| matmul | 15.1 | 6.40 | 2.13 | 0.81 |
| stencil | 14.8 | 6.13 | 1.66 | 0.79 |
| fib | 14.8 | 3.14 | 1.56 | 1.16 |
| subcall | 16.6 | 4.52 | 0.83 | 0.73 |
| meth | 19.2 | 5.25 | 0.96 | 0.53 |
| nbody | 1,328 | 554 | 68.7 | 58.9 |

- **The Baseline JIT is a template JIT.** It still handles boxed values and
  buys 2–5×.
- **The DFG is where values leave the box.** It speculates and unboxes, and
  buys another 2–8×, the 8× on nbody.
- **The FTL** (the DFG's graph lowered through B3) adds 1.1–3.2×.

## 2. How a JSC value is represented

**The box** (`runtime/JSCJSValue.h:328–415`) is one 64-bit word:

- **A pointer** to a heap cell has its top 16 bits zero.
- **An int32** is `0xFFFE:0000:IIII:IIII`.
- **A double** is its IEEE bits plus 2⁴⁹. That moves every real double out of
  the two ranges above and leaves NaN space for the tags.
- **`true`, `false`, `null` and `undefined`** are small invalid pointers
  (0x02–0x0A) with bit 1 set.

So:

- the type test is one compare against `NumberTag`;
- an int is unboxed by taking the low 32 bits;
- a double is unboxed with one integer subtract;
- nothing is reference-counted, because a GC owns the cells;
- copying a value copies 8 bytes.

**The interpreter uses the box directly and records what it sees.** Here is
`op_add` in the LLInt, condensed from `binaryOpCustomStore`
(`llint/LowLevelInterpreter64.asm:1229`):

```
loadConstantOrVariable(size, t0, t1)        # both operands, 8 bytes each
bqb t0, numberTag, .op1NotInt               # one compare: is it an int32?
bqb t1, numberTag, .op2NotInt
baddio t1, t0, .slow                        # add, branch on 32-bit overflow
orq numberTag, t0                           # re-tag
storeq t0, [cfr, t2, 8]
updateBinaryArithProfile(..., ArithProfileIntInt, ...)
```

That last line feeds the next tier. Every arithmetic op records whether it
saw int/int, int/double or double/double, and whether it overflowed. Every
`get_by_val` and `get_by_id` records the array shapes and object structures
it met. This is the profile the DFG compiles from.

**The optimizing tiers have three representations, not one.**
`dfg/DFGUseKind.h` names them:

- boxed `JSValue` in a general-purpose register;
- `DoubleRep`, a raw double in a floating-point register;
- `Int52Rep`, a 52-bit integer in a general-purpose register.

The fixup phase (`dfg/DFGFixupPhase.cpp`) picks one for each operation from
the profile. It also consults the record of past failures: `hasExitSite(node,
Int52Overflow)` in `modShouldSpeculateInt52` (`dfg/DFGGraph.h:423`) refuses a
speculation that has already failed at that bytecode. Pizlo's rule, as
[JSC-TECHNIQUES.md](engines/JSC-TECHNIQUES.md) §1 quotes it: profile for
counterexamples, not distributions.

**Arrays have shapes too** (`runtime/IndexingType.h`):

| shape | element storage |
|---|---|
| `ArrayWithInt32` | ints |
| `ArrayWithDouble` | raw unboxed doubles |
| `ArrayWithContiguous` | boxed `JSValue`s |
| `ArrayWithArrayStorage` | sparse arrays, holes, accessors |

The shape only widens, and the conversion runs once over the whole buffer:
`JSObject::convertInt32ToDouble` at `runtime/JSObject.cpp:1519` rewrites
every element in place. An array filled by `a.push(i * 0.5)` is a C array of
doubles with a length in front of it.

## 3. What the FTL makes of three loops

These are from JSC's own graph dumps (`BUN_JSC_dumpDFGFTLGraphAtEachPhase=1
BUN_JSC_useConcurrentJIT=false`), after loop-invariant code motion. Nodes
that generate no code are left out: `ValueRep`, `MovHint` and `ZombieHint`
only tell an OSR exit how to rebuild the boxed state.

**dot**, `for (i…) s += a[i] * b[i]`:

| where | nodes |
|---|---|
| function entry, once per call | `CheckStructure(a, [ArrayWithDouble])`, `CheckStructure(b, [ArrayWithDouble])` |
| loop preheader | `GetButterfly` and `GetArrayLength` for both arrays |
| each iteration | `CheckInBounds`, `GetByVal(Double)` ×2, `ArithMul(DoubleRep)`, `ArithAdd(DoubleRep)`, `ArithAdd(Int32, CheckOverflow)` for `i++`, `CompareLess`, `Branch` |

The loop is also unrolled once. Per iteration that is C's loop plus two bounds
checks and an overflow check: 0.69 ns against C's 0.44.

**meth**, `for (…) s += p.sum()` with `sum() { return this.x + this.y }`:

- **Once at entry:** `CheckStructure(p)`.
- **The call:** `FilterCallLinkStatus … Statically Proved`. The target is
  proven by a watchpoint, so no check is emitted at all, and the body is
  inlined.
- **The fields:** `this.x` is `GetByOffset(p, 0)` and `this.y` is
  `GetByOffset(p, 1)`.
- **Per iteration:** two loads, two int32 adds with overflow checks, the
  increment and the branch. That is 0.53 ns. Raku++ `--exe -O` takes 81 ns
  for `$p.sum`.

**intloop**, `s = (s + i*i) % 1000003` with `i` up to 2×10⁷:

- `i*i` overflowed int32 in the lower tiers, so the profile carries
  `MayOverflowInt32InBaseline|InDFG`. The FTL computes it as
  `ArithMul(DoubleRep, DoubleRep)`.
- The add follows as `ArithAdd(DoubleRep)`, and the remainder as
  `ArithMod(DoubleRep)`, which is a call to `fmod`.
- `i` itself stays an unchecked int32, because the loop bound proves it
  cannot overflow.
- JavaScript numbers are doubles. int32 is an optimization over them, and
  `int52` reaches only some operations. A program whose integers grow past
  32 bits leaves the fast path.

**When a guess fails** ([box.js](jsc-values-2026-11/box.js) and a variant
that stores a string into `a[7]` after warm-up):

- `BUN_JSC_printEachOSRExit=1` shows `Speculation failure in dot … exit #8
  (bc#0, BadCache)`: the entry `CheckStructure` failed.
- The function falls back to the lower tiers and is recompiled for
  `ArrayWithContiguous`.
- It runs at 1.29 ns where it ran at 0.92.

The general path, compiled:

| dot over… | LLInt | FTL |
|---|--:|--:|
| `ArrayWithDouble` (unboxed storage) | 10.1 | 0.71 |
| `ArrayWithContiguous` (boxed storage, numbers only) | 9.7 | 0.96 |

**JSC's price for boxed storage, compiled, is 1.35×.** The FTL loads 8 bytes,
compares the tag, subtracts 2⁴⁹, and continues. Raku++'s price for not
knowing the type is the untyped column over the native one:

| kernel | untyped ÷ native, `--exe -O` |
|---|--:|
| dot | 25× |
| matmul | 9× |
| stencil | 38× |

In untyped compiled Raku++, an element read is a runtime call that copies a
64-byte `Value` ([FORTRAN-GAP-2026-10-24.md](FORTRAN-GAP-2026-10-24.md) §3
has the profile). Its `CowStr` body, its `PayRef` payload and its
`Ref<ValueExt>` are each counted with an atomic `fetch_add` when present
(`src/Value.h:264`, `src/Ref.h:214`, `src/Ref.h:39`), and a destructor tests
all three.

## 4. What transfers to Raku++, and what does not

JSC gets its speed from three facts it learns while the program runs:

- what kind of element an array holds;
- what layout an object has;
- which function a call reaches.

It checks each fact once and runs code specialised to it. `--exe` compiles
before the program runs, so it cannot watch. **But in JSC two of the three
facts live in the data itself:**

- an array's `IndexingType` sits in its header;
- an object's `Structure` sits in its first word.

The profile only tells the compiler which fact to expect. An ahead-of-time
compiler can emit the check and both versions of the loop.

1. **Arrays that choose their own storage.** An untyped `my @a` that is only
   ever given `Num`s would keep raw doubles, with `Int` handled the same
   way. The first store of anything else would convert the buffer, once,
   and so would the first operation that needs a container: a bind
   (`@a[0] := $x`) or an rw element handed out. That is
   `convertInt32ToDouble` → `convertDoubleToContiguous` in Raku terms.
   - Raku++ already has the packed form (`my num @a`, PACKED-ARRAY-PLAN).
     `--cnp` already reads its words with the kind checked once when the box
     is loaded (FORTRAN-LESSONS item 2).
   - Native dot, matmul and stencil already run at 1.0–3.7 ns under
     `--exe -O`. This step would let untyped code reach them.
   - The Raku-specific obstacles are the element-container question
     (ELEMENT-CELL-PLAN), `of` constraints and `is default`. JSC puts its
     awkward cases in `ArrayStorage`, a slower general shape, and so could
     Raku++.
   - The rows it moves: dot, matmul and stencil, 25–38 ns today.
2. **Objects laid out by their class, and call sites that remember.** Raku is
   easier here than JS:
   - A Raku class declares its attributes, so the slot offsets are known
     when the class is composed. JSC has to discover a `Structure` one
     property at a time.
   - OBJECT-SLOTS-PLAN (2026-11-05) is the layout half.
   - The other half is a per-site cache, (class → method) and (class → slot).
     It would be flipped back by the rare `augment` or `^add_method`: JSC's
     watchpoint, and the second item of JSC-TECHNIQUES.
   - The rows it moves: meth (81 ns, JSC 0.53) and nbody (16,600 ns, JSC 59).
3. **Check at entry, not per use.** Every guard JSC keeps sits at function
   entry or is hoisted out of the loop. Under `--exe` this means loop
   versioning: test the arrays' kinds and the objects' classes once before
   the loop, run a specialised body, and fall back to the general body when
   a test fails. No JIT is needed.

What does not transfer:

- **The NaN box itself.** Raku's `Int` is arbitrary precision, and it has
  `Rat`, allomorphs and containers. The 16–24-byte endgame in
  [JSC-TECHNIQUES.md](engines/JSC-TECHNIQUES.md) §5 stands.
- **What the box teaches does transfer.** JSC's general path is cheap because
  its value is trivially copyable: no counts, no destructor. Every step
  VALUE32-PLAN takes toward that shrinks the 9–38× that untyped code pays
  over native.
- **The tier stack.** These numbers say why it is not needed. Raku++'s
  compiled native code is already within 1.0–3.7 ns on these kernels. What
  is missing is a way to reach that code when the types are not written
  down, and data that carries its own kind provides that.

## Reproduce

From [jsc-values-2026-11/](jsc-values-2026-11/), with Bun on `PATH`, or
`BUN=` set to it:

```bash
./tiers.sh dot 10                 # ns/op under LLInt, +Baseline, +DFG, +FTL
bun box.js unboxed; bun box.js boxed
BUN_JSC_useConcurrentJIT=false BUN_JSC_dumpDFGFTLGraphAtEachPhase=1 bun ks.js dot 1 2>&1 | less
```

The S used for each kernel (ks.js scales its outer loop by S):

| kernel | S |
|---|--:|
| intloop, intloop32 | 1 |
| numloop | 2 |
| subcall, meth | 5 |
| dot, matmul, stencil, fib, nbody | 10 |

**Raku++ and C** use the review's kernels in
[perf-review-2026-11/](perf-review-2026-11/), with best-of-3 wall times less
start-up. Start-up was 12 ms for the interpreter, 8–10 ms for an `--exe`
binary and 6–8 ms for C. Kernels that would finish inside start-up were
lengthened as the review lengthens them:

| runs | lengthened to |
|---|---|
| C dot, matmul, stencil, and the native Raku++ ones | outer repeat ×100, or matmul `n=600` |
| C fib, and fib under `--exe -O` (both forms) and interpreted | `fib(37)`: 78,176,337 calls |
| C nbody | 2,000,000 steps |

**Sources:**

```bash
git fetch --depth 1 --filter=blob:none https://github.com/oven-sh/bun refs/tags/bun-v1.4.4
# oven-sh/WebKit at d6375c72affe0f70222cdcb45012b10f0bf3a9d5, sparse: Source/JavaScriptCore, Source/WTF/wtf
```
