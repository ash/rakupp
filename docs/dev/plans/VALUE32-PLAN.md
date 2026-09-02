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

## Landed: both cheap items, 2026-09-02

Both of the section above's items are implemented, on a **different machine
from the one that wrote this file** — Darwin 25.5, Apple M1, load average
~2.3 throughout. That machine reads interpreter kernels slower than the
benchmarks box, so none of the numbers below is comparable with
BENCHMARKS.md and none of them belongs in it. What makes them trustworthy
anyway is the metric: every engine ratio here is **instructions retired**
and **cycles elapsed** (`/usr/bin/time -l`), measured against a CONTROL
BINARY built from this exact tree with the one line under test reverted.
Instructions retired does not move with machine load, which is what made
measuring on a busy box legitimate at all — and it is also what settled the
one apparent regression, below.

### Item F — `ValueList` grows by relocating

[src/ValueVec.h](../../../src/ValueVec.h) is `RVec<T>`, and
`using ValueList = RVec<Value>` replaces `std::vector<Value>` everywhere
(the 53 sites that spelled the container out were renamed to the typedef
first, so the swap is one line). It is the `std::vector` API subset the tree
uses, with `std::vector`'s semantics — raw-pointer iterators, growth
invalidating everything, `push_back(v[0])` still legal — and two differences:
a reallocation is ONE pass rather than `std::vector`'s two, and where the
standard library allows it the pass is a `memcpy`.

The licence for the `memcpy` is that `Value` is trivially relocatable. The
exception is the `std::string` inside `CowStr`: libstdc++ stores a pointer
aimed at its own inline buffer, so a bitwise move leaves the copy reading the
original's storage. `bitwiseRelocOk()` asks the library at run time rather
than trusting a macro, and the answer picks between `memcpy` and a
move-and-destroy loop — so a standard library that fails the probe keeps
correct behaviour and loses only that half of the speedup.

Probe, this machine:

```
F. vector<Value> build 1000000 Ints   grow 19.71 ms | reserve  7.73 | reloc-grow  8.58 (2.30x vs grow)
   vector<Value> build 1000000 Strs   grow 21.48 ms |               reloc-grow 14.19 (1.51x)
```

A relocating grow lands within 11% of a perfectly pre-`reserve`d build. The
plan predicted 1.46x/1.74x from the other machine; 2.30x here.

Engine A/B against the control, **cycles**:

| kernel | ratio |
|---|---:|
| listbuild (1M `map`, `grep`, `reverse`) | 1.117 |
| sortnums | 1.101 |
| arrayops | 1.070 |
| bigarr (2M push + sum + map) | 1.054 |
| sortby | 1.050 |
| textsplit | 1.030 |
| strarr (500k Str push + sort) | 1.030 |
| objects | 1.009 |
| hashfill | 1.001 |
| arraypush | 0.978 |

Instructions move by at most 2% on any of these, in either direction: the
gain is not fewer operations, it is that a bulk `memcpy` retires far more
bytes per cycle than a move loop. `arraypush`'s 0.978 is the one below
parity and its instruction count went the other way (1.008) — noise.

The fourteen `perf-guard` kernels are **flat on instructions** (0.998 to
1.007, every one), with cycles scattering ±2.5% in both directions. That
scatter is worth a note, because wall clock first said `fib` had regressed
2.4% and it took instruction counting to disprove it: `fib` retires **+0.14%**
instructions on the new container and a standalone probe of the exact
call-path shape — build a one-element list, hand it to a function by value,
destroy it — has `RVec` **ahead** of `std::vector` by 1.088x. The remaining
1% of cycles is code layout in a 30,000-line translation unit whose object
code shrank by 311 KB, not work the container does.

**The relocation was measured twice, and the first time it was not running.**
`bitwiseRelocOk()`'s first version asked whether a short string's `data()`
pointer lay inside the string object. It does — on every implementation, that
is what the small-buffer optimisation *is* — so the probe answered "not
relocatable" everywhere and every number above was produced by the FALLBACK
move-and-destroy loop. The question it should have asked is whether the
object holds a *stored pointer* to those characters, which is what libstdc++
has and libc++ and MSVC do not; the probe now relocates a string and checks
where the copy's characters come from.

That mistake is worth keeping in the file, because of what it says about the
two halves of the win:

- **Most of the gain above is not `memcpy` at all.** It is that `RVec` grows
  in ONE pass — construct the new element and destroy the old one per element
  — where `std::vector` builds a `__split_buffer`, fills it, and then destroys
  the old buffer in a second pass over the same memory. Two passes over
  128 MB is the cost, and it is paid whether or not the bytes can be moved
  bitwise.
- **Turning `memcpy` on adds the rest**: against the same build with the probe
  still answering false, `listbuild` +6.1% cycles, `arrayops` +4.2%,
  `sortnums` +1.1%, everything else inside noise, with instruction counts flat
  throughout (0.995-1.013). `memcpy` is not fewer instructions than a
  vectorised move loop; it is the same work at better throughput.

So the fallback is not a formality for libstdc++ to limp along on — it was
the shipping path for the whole first measurement, and it is most of the win.
[tools/reloc-probe.cpp](../../../tools/reloc-probe.cpp) exists so the
question "which path is live here" has an answer that does not require
reading the header: it prints the path, names what this standard library
should be taking, and exercises the corner a wrong bitwise move destroys.

Two design choices worth recording:

- **The allocation pattern stays exactly `std::vector`'s** — the first block is
  the size asked for, and only then does capacity double. Rounding the first
  block up instead (to four elements, so a short argument list takes one
  allocation rather than three) first looked like a 2.4% cost on `fib` by wall
  clock; instruction counting then showed that reading to be the same layout
  noise as the paragraph above, so it is NOT the reason. The reason arrived
  later and is memory: batch 3 below makes small blocks nearly free to
  allocate, which makes rounding up attractive again — and a program holding a
  million one-element arrays then holds a million four-element blocks, 560-663
  MB against the 292-296 MB it holds today. A `ValueList` is the payload of
  every Array VALUE, not only an argument list, and that is what forbids the
  round-up.
- **The grow path builds the new element into the NEW buffer** before the old
  one is freed, rather than copying it to a temporary first. That is what makes
  `push_back(v[0])` legal — `std::vector` guarantees it — and it is also one
  `Value` move cheaper on the path that runs for every empty list's first
  push.

### Item G — `CowStr::kPromote` 64 -> 23

One line. A 23..63-byte string was a heap `std::string` that mallocs on every
copy; it is now a shared body that copies by refcount. 23 is libc++'s
small-string boundary, so the change moves exactly the band where the inline
representation had already stopped being free.

Instruction ratios against the item-F build, **minimum of five runs each**:

| workload | ratio |
|---|---:|
| mid-band strings, each copied twice | 1.198 |
| mid-band strings, built and read once | 1.049 |
| textsplit | 1.030 |
| grammar JSON parse (api / strings / numbers / deep) | 1.001 / 1.005 / 1.001 / 0.999 |
| hashfill, streq, strcat, `-c` parse-check | 1.000 |
| **mid-band strings as hash keys** | **0.945** |

That table is the second one this section had. **The first was wrong, and the
way it was wrong is the reason the "minimum of five runs" is in bold.** It
was built from single runs, and it claimed the grammar JSON parses at 1.089
and 1.084 — which would have made them the headline evidence. They are flat.
A single run of that parse lands anywhere in a ~2% band, and two consecutive
readings had happened to agree at a value ~12% above the floor. `fib`, by
contrast, repeats to ±0.02%. So the honest procedure is per-kernel: measure
the spread before trusting a ratio, and quote a minimum of several runs.

What the corrected table says is narrower than the first one, and still
positive. The plan asked for `streq`, `strcat`, `textsplit` and a JSON parse.
`streq` and `strcat` are outside the band entirely — a five-byte literal, and
a string that grows past 63 in its first sixty iterations — and measure
exactly flat, which is the right answer for a change that cannot touch them.
`textsplit` is the one real workload that moves, at 3.0%. The grammar parses
do not move at all: a `Match` keeps its captures as substrings that are
mostly short or long, not mid-band.

The plan's predicted counter-case did **not** appear: a mid-band string built
and read once is 1.049, not a loss, because a Raku string value is copied at
least once before anything reads it — the probe's zero-copy column has no
program behind it.

A different counter-case did appear, and it is worth recording because it
explains the whole trade. **Mid-band strings used as hash keys retire 5.9%
more instructions.** A promoted string costs two allocations, not one — the
`make_shared` block, and then the `std::string` inside `StrBody` allocating
its own buffer — where an unpromoted one costs the single `std::string`
malloc. Copies are free afterwards, so promotion pays from roughly the second
copy onward; a key is copied *out* into the hash's own key storage and the
Value is then dropped, which is the one common shape that never gets there.
`hashfill` does not show it because its keys (`"key$i"`) are under 23 bytes.
Storing `StrBody`'s text inline instead of as a `std::string` member would
collapse promotion to one allocation and remove this loss — that is the same
change design C in the ladder needs, and it is the reason to expect C's
string numbers to be better than this band's.

On this evidence item G is the weakest of the three changes: one real
workload at 3%, a synthetic best case at 20%, a synthetic worst case at
-5.6%, and flat everywhere else. It is kept because the shapes it helps are
commoner than the shape it hurts, not because the measurement is emphatic.

### Batch 3 — small `ValueList` blocks come off a free list

This one is not from the size question at all. It arrived from the other
half of the session's work (see *The AST-flattening question*, below): the
IR experiment of 2026-08-08 measured the per-call argument `ValueList` at
51 ns against a ~451 ns interpreted call and named removing it as one of the
two things worth doing. Re-priced today, in the container this batch already
owns, the shape is:

```
one-argument list, built + passed + destroyed, x5000000
  RVec (batch F)          161.75 ms   32.35 ns/call
  free-list block          47.40 ms    9.48 ns/call  (3.41x)
  inline 2-elem buffer     22.03 ms    4.41 ns/call  (7.34x)
  reused list (ceiling)    39.80 ms    7.96 ns/call  (4.06x)
```

So `RVec` keeps a thread-local free list of blocks, per EXACT capacity, for
capacities 1 through 4, up to 64 blocks each. Allocation is a pop, release is
a push, and a thread that exits gives its blocks back.

Per exact capacity, not per rounded-up size class, and the difference is the
whole design. One four-element block for every small request is simpler and
slightly faster, but a `ValueList` is also the payload of every Array Value:
a million one-element arrays then hold a million four-element blocks. That
variant measured 560-663 MB of peak RSS against 292-296 MB, and paid 22% of
that program's cycles in the extra memory traffic. Per-capacity lists keep
the speed and leave the footprint where it was (298-424 MB across runs, a
band that overlaps the unpooled one — this workload's RSS is noisy).

Measured against the batch-G build:

| kernel | instructions | cycles |
|---|---:|---:|
| listbuild | 1.249 | 1.059 |
| call (400k two-argument sub calls) | 1.130 | 1.061 |
| fib | 1.088 | 1.080 |
| method | 1.077 | 1.044 |
| subcall | 1.057 | 1.021 |
| hashfill | 1.051 | 1.019 |
| a million one-element arrays | 1.047 | 1.046 |
| objects | 1.035 | 1.030 |
| objnew | 1.035 | 1.045 |
| asg, loopsum (no calls) | flat | flat |

This is perl's item 8 (`sv.c`'s arenas and free lists, PERL5-TECHNIQUES),
applied to the one allocation two separate investigations had already
named.

### The whole sitting, against HEAD

Four changes — relocating one-pass growth, the promote threshold, the
small-block free list, and the probe fix that finally turned `memcpy` on —
as one A/B against the binary this session started from. Instructions retired
and cycles elapsed, **minimum of five runs each**:

| kernel | instructions | cycles |
|---|---:|---:|
| arrayops | 1.303 | 1.201 |
| listbuild | 1.259 | 1.223 |
| sortnums | 1.188 | 1.201 |
| sortby | 1.148 | 1.141 |
| multimeth | 1.139 | 1.136 |
| bigarr | 1.134 | 1.133 |
| call (400k two-argument calls) | 1.130 | 1.088 |
| strcat | 1.123 | 1.082 |
| textsplit | 1.116 | 1.111 |
| privmeth | 1.093 | 1.065 |
| strscan | 1.087 | 1.065 |
| fib | 1.086 | 1.043 |
| method | 1.084 | 1.050 |
| attrread | 1.084 | 1.063 |
| strarr | 1.067 | 1.064 |
| strpass | 1.058 | 1.017 |
| subcall | 1.056 | 1.021 |
| hashfill | 1.054 | 1.051 |
| a million one-element arrays | 1.042 | 1.082 |
| arraypush | 1.037 | 1.046 |
| objects | 1.029 | 1.033 |
| regex | 1.028 | 1.040 |
| objnew | 1.024 | 1.043 |
| bigint, rats, asg, regexloop, hash, streq | 0.995-1.001 | flat |

Nothing is below parity. The six flat kernels are the ones the changes cannot
reach — a scalar assignment loop, a `Rat` loop, a five-byte string compare —
and they are flat to within their own repeat spread.

### Gates

- `t/run.raku` 631/631, including a new case,
  [t/regression/valuelist-relocating-growth.raku](../../../t/regression/valuelist-relocating-growth.raku),
  which drives the corners a hand-written container gets wrong where
  `std::vector` does not: self-referential push at every doubling boundary,
  splice/unshift holes across a reallocation, Str elements (the one member
  that is not bitwise-relocatable everywhere), and relocated bytes carrying
  live refcounts.
- Roast per-file diff: **jitter only**, on a full run after each change —
  four runs, including one after the probe fix put the `memcpy` path into
  service for the first time. 648, 648, 647 and 647 fully-passing files
  against the 647 of `v3.24.0-union.list`, with three files the baseline runs
  had flapped (`S04-statements/loop.t`, `S09-typed-arrays/native-decl.t`,
  `integration/advent2012-day03.t`) passing in all four. Every file that
  appeared to be lost in any run was re-run SERIALLY under both the new
  binary and HEAD and behaved identically — `S15-nfg/concatenation.t` 15/15,
  `S15-nfg/emoji-test.t` 3825/3825, `S17-scheduler/basic.t` 34/34, and
  `S17-promise/stress.t`, `S29-context/exit.t`, `S17-channel/stress.t`,
  `S17-lowlevel/cas-loop-int.t`, `S17-scheduler/times.t`,
  `S03-operators/scalar-assign.t`, the two `APPENDICES/A02` files. No file
  was lost twice, and the S17 family are the usual four-worker timeout
  flappers.
- `t/stress/run.raku` 26/26 in the release build, after each change.
- ThreadSanitizer: the same five pre-existing `-parallel` failures under the
  new binary and under the control, verified by building the control under
  TSan too — so they are the tree's, not this work's. Re-run after the
  thread-local pool landed and again after the `memcpy` path went live:
  unchanged both times. No new race.
- The probe (`tools/value32-probe.cpp`) re-run after the batch.
- `perf-guard --check` was NOT run: this box fails its load detector uniformly,
  which is what the interleaved instruction-count A/B above replaces.

### What this means for the rest of the file

The falsifier "if the relocating `ValueList` gets most of the array-path win
on its own" is **not** triggered. The probe's 4.5x on the array path was a
`sizeof` measurement and relocation collects a tenth of it on real kernels;
the remaining nine-tenths is still allocation and memory bandwidth, which is
what design A addresses. But the same measurement re-prices A honestly: the
array-path kernels moved 3-12% for a change that touches one typedef, so a
56-byte `Value` should be expected in the same band rather than in the
probe's, and A's ~190 ownership sites should be weighed against that.


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
