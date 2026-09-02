# Plan: `Value` below 128 bytes — what the last two halvings actually cost

*Written 2026-09-02, before any code. Continues phase 1 of
[REPRESENTATION-PLAN.md](REPRESENTATION-PLAN.md), whose stated target was
`sizeof(Value) <= 64` and which stopped at 128 after batch 4.*

The question this file answers, put by the user: drastically minimise `Value` —
to 32 bytes, "basically a pointer and an int". It is reachable. The finding is
that **the last 24 bytes are all string**, and that two cheaper changes the
probe turned up on the way may be worth more than the halving they were
measured beside.

**The number a stranger can re-measure:**

```bash
c++ -std=c++20 -O2 -DNDEBUG -Isrc -Iinclude tools/value32-probe.cpp \
    build-arm64/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props,stubs}.a \
    -o /tmp/value32-probe && /tmp/value32-probe
```

Measurements below: 2026-09-02, Darwin 24.6 / arm64 (the benchmarks machine —
see the load note under *Methodology*), Apple clang, `build-arm64` static libs,
`Value` at 128 bytes. All of them are C++ **probe** timings that price
representation directly; none of them is an engine kernel number, and none
belongs in BENCHMARKS.md.

---

## Where the 128 bytes are

[src/Value.h:450](../../../src/Value.h). `ValueExt` is 168 and allocated only
when one of its fields is set; `CowStr` is 40; `MatchData` 48; `ValueHash` 88.

| bytes | field(s) | note |
|---:|---|---|
| 16 | `i`, `n` | never both live |
| 40 | `s` (`CowStr`) | `std::string` 24 + `shared_ptr<StrBody>` 16 |
| 24 | `hashKind`, `enumName`, `enumType` | three 8-byte `IStr` pointers |
| 16 | `t` + 9 bools + `pk_` + `natBits` | ~25 bits of information |
| 16 | `p_` | the payload slot (batch 4) |
| 16 | `x_` | the cold block (batch 2) |

Three non-trivial members, so a copy is three branches, up to three atomic
increments and a 24-byte string copy; a destruction is three branches.

## What is left to win — measured

```
B. build vector<T> of 1000000 Ints   Value  28.99 ms   Slim32   5.78 (5.01x)   Slim16   1.20 (24.19x)
   copy  vector<T> of 1000000 Ints   Value  10.01 ms   Slim32   2.31 (4.33x)
   scan  vector<T> of 1000000 Ints   Value   2.52 ms   Slim32   0.41 (6.14x)
C. build 800 ValueHash of 15 (Int)     0.37 ms  = 31 ns/entry
E. copy an Array Value 20000000x   shared_ptr  75.01 ms   intrusive  69.52 (1.08x)
```

Two of those matter more than the headline.

**The hash path is already fixed.** 31 ns/entry, against the 302 that opened
this campaign and the 265 after batch 1 — `ValueHash`
([src/ValueHash.h](../../../src/ValueHash.h)) did that, not `sizeof`. The
original plan priced `sizeof(Value)` at 1.83x on structure building *through a
`std::map`*; through the current container the same lever is much smaller.
Whatever else is true, the hash path is not the argument for this work any
more. The array path is.

**The intrusive refcount buys size, not speed.** Replacing a 16-byte
`shared_ptr` with an 8-byte intrusive `Ref` measures 1.08x on payload copying.
The atomic increment is the cost and it does not change; the second word is
nearly free to copy. So `Ref` earns its place only as part of reaching a size
target — it is not independently worth the ~190 ownership sites it would move.

**Where the time is in a real program.** A `sample` of an array/hash-heavy
1.8 s workload (2M `push`, 2M-element sum, a `map`, 200k hash stores), 1285
main-thread samples: malloc family 21.5%, `std::vector<Value>` member functions
7.6%, `Value` copy/move 3.7%, thread-local access 6.1%. The directly
attributable share is ~11%, plus whatever is inlined into `execBlock` and the
`methodCall*` chain. **The microbenchmark ratios above apply to that share and
to memory bandwidth, not to wall clock.** Expect single-digit to low-double-digit
percent on real programs from a halving — which is the shape batches 2 and 4
already measured (-3% to -26% per kernel, most in the low teens).

The other thing a halving buys is footprint, which those batches also measured
(`hashfill` peak RSS 244.9 -> 148.8 -> 101.5 MB). A 1M-element array holds
128 MB of `Value` today; 56 MB at design A below, 32 MB at design C.

## The 32-byte layout, concretely

```c++
struct Value {                      // 32
    union { long long i; double n; char inl[16]; };   // 16
    Ref<Body> p;                                      //  8  intrusive, kind-tagged
    uint32_t tag;                                     //  4  VT(5) pk(3) flags(9) natBits(3) len(5)
    uint32_t aux;                                     //  4  IStr32: hashKind | enumId
};
```

Four things have to be true. Three are cheap.

**1. `i` and `n` are never both live.** Verified by reading every write, not by
census: the only `n` writes outside `VT::Num`/`VT::Complex` are fractional
Ranges, which keep their real endpoints in `n`/`im` by design
([Interpreter.cpp:1856](../../../src/Interpreter.cpp), 19335, 29693), and the
only `i` writes outside `VT::Int` are a `VT::Type`'s definiteness constraint
([Interpreter.cpp:28763](../../../src/Interpreter.cpp)) and a Seq's position
counter. Both of those are on values whose `n` is dead. Confirm with an
assertion build before relying on it; this is exactly the shape of thing batch
4's `is default(v)` lesson says a census will not see.

**2. `hashKind`, `enumName` and `enumType` collapse into one 32-bit `aux`.**
`IStr`'s table is append-only ([src/IStr.h](../../../src/IStr.h)), so a 32-bit
index into a stable vector replaces the 8-byte pointer with no loss. An enum
value needs two names, so intern the *pair*: one `enumId` naming
(name, type), which also turns `orderVal` into three preinterned constants
instead of two interning calls on every `cmp`/`<=>`. `hashKind` and enums never
co-occur — `hashKind` lives on Hash/Set/Bag/Buf and Regex flags, enums on
Int and Type — and the accessors gate on `VT`, so an Int still reports an empty
`hashKind` rather than reading an enum id as a container kind.

**3. `x_` folds into the body — but only as kind-specific bodies.** It cannot
become a generic side-block behind the same pointer: two Values sharing a
payload would then share their cold block, and `arrS()`-style aliasing exists
precisely so that replacing one copy's pointer does not touch another's. What
works is giving each body kind the fields it needs. `MatchBody` carries
`pos`/`named`/`made` and from/to; `RatBody` carries two `BigInt`s **by value**,
which is the one-allocation Rat batch 2 deferred; an integer `Range` needs no
body at all, since both endpoints fit the 16-byte immediate. Every existing
accessor (`v.big()`, `v.rFrom()`, `v.shape()`) keeps its exact signature and
returns from the right body or a static empty, so batch 2's ~800 converted
sites do not move again. Releasing needs to know the kind: either a virtual
destructor on `Body` (8 bytes per body, which are heap objects anyway) or a
switch on the kind byte.

**4. The string.** This is the whole problem, and it is the next section.

## The last 24 bytes are all string

Everything above reaches **56 bytes** with `CowStr` left exactly as it is. Going
56 -> 32 means the bytes leave the `Value`, and the probe says the two ways of
doing that differ by 7x in opposite directions:

```
D. Str value, ctor + 3 copies, x2000000
   "name"            CowStr  25.35 ms   heap-body  42.37 (0.60x)   inline   5.73 (4.42x)
   "created_at_iso"  CowStr  27.27 ms   heap-body  44.02 (0.62x)   inline   7.35 (3.71x)
```

**Always-heap is 1.6x slower than today** — one malloc where libc++'s small
string optimisation gives you none. **Inline-in-the-value is 3.7-4.4x faster.**
So a 32-byte layout only pays if short strings stay inline.

And inline storage is the expensive decision, because there is then no
`std::string` object for `v.s` to return a `const std::string&` to. That is
~1,549 sites moving to `std::string_view`. The compiler enumerates them, but it
is a larger sweep than batch 4's ~3,900, because the *type* changes rather than
the spelling: every site that hands `v.s` to a function taking
`const std::string&`, and every `v.s.mut()`, has to be looked at. String_view
is arguably the right read type and would remove copying elsewhere, but that is
a claim to test, not to assume.

The source-compatible variant — string always in the body, `str()` returning
`const std::string&` from it (or from a static empty when null) — keeps all
1,549 sites compiling and eats the 1.6x. That trade is only acceptable if
something else pays it back; see design B in the ladder.

## Two cheaper things the probe found on the way

Both are independent of the size question and can be tested this week.

### Growth is 56% of the array build cost, and it is not a `sizeof` problem

```
F. vector<Value> build 1000000 Ints   grow  27.04 ms | reserve  12.03 | reloc-grow  18.56 (1.46x vs grow)
   vector<Value> build 1000000 Strs   grow  30.19 ms |                 reloc-grow  22.79 (1.32x)
```

`Value` has a non-trivial move constructor, so every `std::vector<Value>`
reallocation move-constructs a million elements one at a time. A `Value` is
nonetheless **trivially relocatable** — moving the bits is sound as long as the
source is not then destroyed, which is exactly what a reallocation does — so a
vector that grows by `memcpy` is correct and measures 1.5x on the whole build
(1.46x this sitting, 1.74x in a separate one the day's earlier run produced;
quote the band, not a point).

`ValueList` is a typedef ([Value.h:243](../../../src/Value.h)), and the
precedent for swapping a container behind an API subset is `ValueHash` and
`FlatMap`. This is the cheapest measured item in this file. It also **compounds
with** the size work rather than competing with it: the residue between
reloc-grow (18.56) and reserve (12.03) is memcpy volume, which a 32-byte
`Value` cuts by four.

### `CowStr`'s promote threshold is in the wrong place

`kPromote = 64` ([Value.h:73](../../../src/Value.h)) means a 23..63-byte string
is a heap `std::string` that mallocs **on every copy**, while a 64-byte one is
a shared body that copies by refcount. Swept by length, ctor + 2 copies,
1M iterations, "as built" vs the same value forced to a shared body:

| len | copies | as-is | promoted | |
|---:|---:|---:|---:|---:|
| 8 | 2 | 9.08 ms | 25.95 | 0.35x |
| 22 | 2 | 9.08 | 25.96 | 0.35x |
| 24 | 0 | 21.80 | 51.67 | 0.42x |
| **24** | **2** | **95.14** | **56.54** | **1.68x** |
| 32 | 2 | 103.67 | 58.53 | 1.77x |
| 48 | 2 | 96.89 | 56.67 | 1.71x |
| 63 | 2 | 92.11 | 57.67 | 1.60x |
| 80 | 2 | 54.39 | 55.37 | 0.98x |
| 200 | 2 | 58.22 | 59.35 | 0.98x |

Read the two ends first: below 23 bytes promoting is 3x **worse** (it adds a
malloc where SSO had none), and above 64 the two columns are the same value
measured twice, because it is already promoted. The band in between is the
finding — a 33-byte string costs 1.7x more than a 68-byte one for no reason but
the threshold.

Break-even is about one copy: at len 24, as-is goes 21.80 -> 95.14 for two
copies (~37 ns per copy) while promoted goes 51.67 -> 56.54 (~2.4 ns per copy),
against a construction penalty of ~30 ns. `CowStr`'s own header states the case
for why values are copied more than once. So lowering the threshold to 23 —
libc++'s SSO boundary, one line — is likely a win and must be shown to be one
on `streq`, `strcat`, `textsplit` and `json-parse` rather than argued from this
table. The counter-case to look for is construct-once-never-copied strings, and
the thing to watch is `mut()`, which un-promotes: a repeatedly appended string
in that band would move between representations more often than it does today.

## The ladder

| | size | what it costs | measured |
|---|---:|---|---|
| today | 128 | | |
| **A** intrusive `Ref`, kind-specific bodies, packed tag word, 32-bit `IStr`, `i`/`n` union | **56** | ~190 ownership sites + a mechanical accessor sweep; no string change; no new allocations | most of the 5.0x on the array path |
| **B** A + string in the body, `str()` -> `const std::string&` | **32** | source-compatible reads; +1 malloc per short Str | 5.0x array, **0.60x** short strings |
| **C** A + <=15-byte inline string, `str()` -> `string_view` | **32** | ~1,549-site type change | 5.0x array, 3.7-4.4x short strings |
| **D** "a pointer and an int" | 16 | not a layout change — see below | 24x on the probe |

**A is the whole of the safe win.** It has no semantic edge, it is the batch
1/2/4 recipe again, and it banks the array-path improvement without touching
the string surface. 56 bytes is also the number where the remaining question is
clean: everything left in the struct is either 8 bytes of immediate, 8 of
pointer, 8 of tags — or 40 bytes of `CowStr`.

**B versus C is one measurement, not a preference.** They differ by 7x on
section D and by ~1,500 sites of blast radius, in opposite directions. Decide
it with a real workload after A lands, when the rest of the struct is no longer
confounding the string measurement.

## Why 16 bytes is an architecture change, not a layout change

Slim16 measures 24x on the array build. It is worth saying plainly what that
stand-in is: 16 bytes with **no refcount**, hence trivially copyable and
trivially destructible, so vector growth is a pure `memcpy` and destruction is
free. Repacking fields cannot produce it. Two things would have to be true
first:

- **values stop being refcounted** (tracing or deferred collection), and
- **the container stops living inside the value.** `readonly`, `itemized`,
  `namedArg`, `objKeyed`, `natBits`, `natSigned`, `natFloat`, `ofType` and
  `elemDefault` are attributes of a *container*, not of a value; Rakudo keeps
  them in `Scalar`, not in the thing stored. `Value` today plays value,
  container and type annotation at once, and that — not the field packing — is
  the reason it cannot go below ~32 bytes.

That split is the direction that would make 16 conceivable, and it is a
rewrite of assignment and binding, not a batch. Recorded here so the 24x is not
mistaken for something the ladder above can reach.

## Risk register

- **Codegen emits `Value` field names into generated C++.** 14 string literals
  in [Codegen.cpp](../../../src/Codegen.cpp) contain `.i`, `.t`, `.s`,
  `.itemized`, `.isList`. No compiler pass of rakupp checks those; only the
  `t/` `--exe` goldens do. Batch 2 hit exactly this seam with `.rFrom`/`.ofType`
  and batch 4 with `e.code`.
- **The census bounds what typical programs do, not what the language allows.**
  Batch 4's `is default(v)` co-occurrence was invisible to a 30M-destruction
  census and showed up in the `t/` suite in minutes. Every merge in design A
  needs the semantic corners, not Roast bulk.
- **Parallelism.** `Ref`'s refcount must stay atomic; the ThreadSanitizer build
  and `t/stress/parallel-map` gate it, as they did for the batch-4 slot.
- **The `-DRAKUPP_PTR_CENSUS` build must keep compiling** through whatever
  `ptrMask()` becomes.
- **`ValueList` is `std::vector<Value>` in ~everything.** A relocating
  replacement must cover the `std::vector` surface actually used (`std::sort`,
  `insert`, `erase`, iterator arithmetic), which is a wider API than
  `ValueHash` needed.

## Gates

The standing ones per [RELEASING.md](../RELEASING.md) — zero Roast
regressions read as a **per-file diff** rather than a total, `t/run.raku`,
`perf-guard --check` on **every batch** rather than at the end — plus, for this
file specifically:

- `tools/value32-probe.cpp` re-run after each batch, so the section that
  motivated the batch is still saying what it said;
- peak RSS on `hashfill`, which is the number that has moved most reliably
  across this campaign;
- the module battery, per
  [STRING-SCAN-QUADRATICS](../findings/STRING-SCAN-QUADRATICS.md) §4a: a
  representation change is a concurrency change.

## What would falsify this plan

- If design A lands at 56 bytes and the array-path kernels do not move at all,
  then the 5.0x in section B is bounded by something the probe isolates away
  (most likely allocation, at 21.5% of the sample) and B/C are not worth
  starting.
- If the relocating `ValueList` gets most of the array-path win on its own,
  the size work is competing with a change an order of magnitude smaller and
  should be re-priced against phase 3's per-call allocations and TLS lookups,
  which the same sample puts at 21.5% and 6.1% and which nothing has touched.
- If lowering `kPromote` regresses `strcat` or `textsplit`, the copy-count
  assumption behind the whole `CowStr` design is wrong for real programs and
  section G's band should be left alone.

## Methodology

Best-of-7 per shape, one sitting, single-threaded, `-O2 -DNDEBUG`, arm64 binary
(`file` checked). Load average 2.16 at the start of the run — above the
strict-idle bar, and deliberately not waited out: **every comparison in this
file is between two shapes inside one binary in one run**, so machine drift
moves both sides together, which is the property that matters here and is not
the property an A/B against another build has. The one number quoted as a band
rather than a point (F, 1.46x/1.74x) is the one that moved between sittings.
Nothing here is comparable with BENCHMARKS.md and nothing here belongs in it.
