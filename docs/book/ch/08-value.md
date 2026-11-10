\part{The Value Model}

# `Value`: the Fat Tagged Struct

Raku is dynamically typed. A variable holds an `Int` now and a `Hash` later,
routines dispatch on the runtime types of their arguments, lists can be
infinite, and an object can grow a role while the program runs. Raku++ is
written in statically-typed C++17 with no tracing garbage collector. One data structure
carries the entire distance between those two facts.

```cpp
// src/Value.h — as it stands on 64-bit macOS and Linux, abridged
enum class VT : uint8_t { Nil, Any, Bool, Int, Num, Str, Array, Hash, Code,
                          Range, Pair, Type, Whatever, Object, Rat, Regex,
                          Match, Complex };
enum class PK : uint8_t { None, List, Hash, Code, PairV, Obj, Match, Cell,
                          Packed, Rat };

struct Value {                 // 32 bytes: four words
    union {                    // ONE word for the two
        long long i = 0;       // Int; a Type's definiteness; a Pair's Int key
        double n;              // Num; the real part of a Complex
    };
    CowStr s;                  // Str; also the Type name and the Pair key
    IStr hashKind;             // "" Hash, else Set/Bag/Mix/Buf… — interned
    VT t = VT::Any;            // the discriminator
    PK pk_ = PK::None;         // what the payload slot holds
    bool b : 1;                // eleven one-bit flags:
    bool isList : 1;           //   b, isList, itemized, objKeyed, readonly,
    bool itemized : 1;         //   immutableBind, pairValRO, namedArg,
    /* … */                    //   natSigned, natFloat, keyInt
    unsigned natBytes_ : 4;    // native width in bytes; 0 = not native
    PayRef p_;                 // THE payload slot, a 4-byte handle: a list,
                               // a hash, a Callable, a Pair's value, an
                               // object, a Rat's parts, a packed array, a
                               // cell — or a MatchData carrying three at once
    HRef<ValueExt> x_;         // the cold block, a 4-byte handle: enum names,
                               // Complex imag, Range ends, BigInt, shape,
                               // ofType… null on almost every value
};
```

Four words: the scalar, the string, the interned tag with the tag block beside
it, and two 32-bit handles to counted blocks. That is not what this chapter was
written against, and the original is worth seeing, because every argument below
is easier to follow when the fields are all in the open:

```cpp
// src/Value.h — the 392-byte original, for the arguments that follow
struct Value {
    VT t = VT::Any;              // the discriminator
    bool b; long long i; double n, im;    // Bool / Int / Num / Complex imag
    CowStr s;                    // Str; also the Type name, the Pair key
    IStr hashKind, enumName, enumType;    // interned secondary tags
    bool isList, itemized, readonly, namedArg, fatRat, objKeyed;
    std::shared_ptr<ValueList> arr;                    // Array / List / Seq
    std::shared_ptr<std::map<std::string, Value>> hash;
    std::shared_ptr<Callable> code;
    std::shared_ptr<Value> pairVal, pairKey;
    std::shared_ptr<ObjectData> obj;
    std::shared_ptr<void> ext;   // opaque: Promise, Channel, lazy-seq state
    std::shared_ptr<BigInt> big, ratN, ratD;
    std::shared_ptr<std::vector<long long>> shape;
    long long rFrom, rTo; bool rExFrom, rExTo, rNum;   // Range
    std::string ofType; int natBits; bool natSigned, natFloat;
};
```

Eighteen tags, eleven `shared_ptr`s, and a pile of flags. It looks
indefensible. It is not, and the case for it is the most important argument in
this book.

**Read the field names below as the original's, because they still work.** The
campaigns that took 392 to 128 (Chapter 41), and the later ones that took 128
to 32, moved fields; they did not rename anything a reader or a caller uses.
`arr`, `hash`, `code`, `pairVal` and `obj` became one kind-tagged slot, and
`big`, `ratN`, `ratD`, `pairKey`, `ext`, `shape`, `ofType`, `im` and the
`Range` ends went behind a lazily-allocated cold block — but every one of them
is still spelled `v.arr()`, `v.ratN()`, `v.rFrom()`, and returns what it always
did. Roughly 3,900 call sites depended on that, which is why the accessors
were introduced *before* the fields moved. The struct got smaller; the model
did not change.

Four names have since gained the parentheses the others have. `enumName` and
`enumType` moved into the cold block and are read as `v.enumName()` and
`v.enumType()`. `natBits` is held as a 4-bit byte count and read as
`v.natBits()`. `fatRat` is a flag on a `Rat`'s body and read as `v.fatRat()`.
A `Rat`'s parts left the cold block for a payload body of their own (`PK::Rat`),
and `v.ratN()` and `v.ratD()` answer from it.

The one place the change is visible is the `Match`, and it is visible because
the design below forced it: a `Match` genuinely needs `arr`, `hash` and
`pairVal` at once, so it is the single licensed co-occupant of the payload
slot, riding in a `MatchData` body that holds all three. The next section is
why that mattered.

## Why not a union, a variant, or a class hierarchy

The instinct is: a value is one of *N* types, therefore a sum type. All three
standard spellings of that instinct are wrong here, each for a different
reason.

**A `Value` is frequently several fields at once.** A union models mutual
exclusivity — one active member, selected by the tag — and that is simply not
what a Raku value is:

- a **`Match`** sets the subject string `s`, the span `rFrom`/`rTo`, the
  positional captures in `arr`, *and* the named captures in `hash`, all live
  together;
- a **`Pair`** is the key in `s` plus `pairVal`;
- an **enum value** is the ordinal in `i` plus `enumName` and `enumType`;
- a **`Rat`** is `ratN` plus `ratD` plus the `fatRat` flag;
- and the cross-cutting flags — `isList`, `itemized`, `readonly`, `namedArg`,
  `ofType`, `natBits` — are set *regardless of the tag*. A `readonly` `Int` and
  an `itemized` `Array` are both ordinary.

So `VT t` does not mean "which one field is valid". It means "how to read a bag
of fields, several of them set". That is a struct.

**A raw union of these members is undefined behaviour.** They are non-trivial
C++ types — a string, eleven `shared_ptr`s. Making it legal means hand-written
placement-`new`, a tag-switched copy constructor, move constructor, both
assignment operators and destructor. At that point you have rebuilt
`std::variant`, which re-imposes the one-active-member model *and* forces
`std::get`/`std::visit` on the roughly sixteen thousand lines of interpreter
and built-ins that today just read `v.i`, `v.s`, `v.arr`.

**The memory a union would save is smaller than it looks.** The overlappable
members are mostly `shared_ptr`, which is null and therefore allocation-free
when unused, and by the first point many of them cannot be overlapped at all.

**A class hierarchy** would put a virtual call on every field access and a heap
allocation on every integer. For a tree-walker whose values are overwhelmingly
transient scalars, that is the worst of the three.

## What the fat struct buys

**No virtual dispatch and no allocation for scalars.** An `Int` is a `Value`
with `t == VT::Int` and `i` set. Building one is a stack operation:

```cpp
// src/Value.h
static Value integer(long long x) { Value v; v.t = VT::Int; v.i = x; return v; }
```

**Uniform copy and move.** Passing a `Value` by value, returning it from
`eval`, storing it in a vector — all use the compiler-generated operations. The
counted payload slot makes copies of arrays, hashes and objects a refcount bump,
and give them **shared identity**, which is exactly what Raku's container
semantics need. Mutating an object attribute through a copied `self` handle
works for this reason and no other.

**Coercion is a method, not a cast.**

```cpp
// src/Value.h
bool truthy() const;      long long toInt() const;   double toNum() const;
std::string toStr() const;  std::string gist() const;
std::string typeName() const;
```

`~$x` calls `toStr`, `+$x` calls `toNum`, boolean context calls `truthy`. There
is no C++ inheritance making `Int` "be a" `Cool`; these six functions encode the
numeric and string towers directly, each a `switch` on `t` in `Value.cpp`.

## Clever reuse of fields

Several Raku types have no `VT` of their own. They are an existing tag plus a
tag-like marker, and knowing which is which explains a lot of otherwise puzzling
code.

**A `Junction` is an `Array` tagged by `enumName`.** `any(1,2,3)` is a
`VT::Array` whose elements are the eigenstates, with `enumName` set to one of
`any`, `all`, `one`, `none`:

```cpp
// src/Value.cpp — typeName(), the VT::Array case
if (enumName() == "any" || enumName() == "all" ||
    enumName() == "one" || enumName() == "none") return "Junction";
```

The name is in the cold block since the 48-byte step, so a Junction carries
one; an enum value and a `Blob` subtype are the other two values that do.

**Set, Bag and Mix are Hashes tagged by `hashKind`.** So are `Proxy`, `Failure`,
`Date`, `DateTime`, `Scalar` and several more; `hashKind` is a secondary type
tag drawn from a closed vocabulary.

**An allomorph is a number that is also its own string.** `<42>` is
`VT::Int` with `s` holding `"42"` and `hashKind` naming `IntStr`:

```cpp
// src/Value.h
bool isAllomorph() const {
    return (t == VT::Int || t == VT::Rat || t == VT::Num || t == VT::Complex) &&
           (hashKind == "IntStr" || hashKind == "RatStr" ||
            hashKind == "NumStr" || hashKind == "ComplexStr");
}
```

**An `Int` grows a bignum only when it must.**

```cpp
// src/Value.h
static Value bigint(const BigInt& b) {
    Value v; v.t = VT::Int;
    if (b.fitsLL()) v.i = b.toLL();
    else v.bigM() = std::make_shared<BigInt>(b);
    return v;
}
```

**A `FatRat` is a `Rat` with a flag.** Same storage, but the flag carries the
type identity — contagious through arithmetic — and exempts the value from the
denominator spill described in Chapter 11. The flag is a bit on the `Rat`'s
body (`kPayFatRat`), beside the count.

**A native integer container is a `Value` with a width.** `my uint8 $b` records
a width of 8 bits and `natSigned = false`, and every assignment masks to that
width.

## `ext`: the opaque slot

One member is deliberately untyped. It sits in the container part of the cold
block, is read as `v.ext()` and written through `v.extM()`:

```cpp
// src/Value.h — ValueContExt
std::shared_ptr<void> ext;
```

It carries whatever state a value needs that has no business in the struct: a
`PromiseState`, a `Channel`, a `TapHandle`, a `LazySeqState`, a `CueState`, or
the endpoint objects of a `Range`. Each consumer knows what it parked there and
static-casts it back.

That is not as loose as it sounds, because the tag plus `hashKind` always
determine which kind of state can be present. But it is the one place in the
value model where the type system has been switched off on purpose, and it is
worth being aware of when adding a case.

## The identity problem, and how `ext` solves it

Some Raku types are *reference* types: two of them are the same one only when
they are the same object. `Buf`, `Instant` and `Duration` are three. Here they
are plain tagged scalars — a `Buf` is a `Str` with `hashKind = "Buf"`, an
`Instant` a `Num` — and with no address to compare, `===` fell through to
comparing the *rendering* and declared two independently built buffers
identical.

That is not academic. It made `@!outstanding-writes .= grep({ $_ !=== $p })`
unemptiable for Promises, and it was worse for `Buf`, which is mutable:
dropping one buffer from a list by `!=== $buf` threw away every buffer that
happened to hold the same bytes.

```cpp
// src/Value.h
inline bool identityScalar(const Value& v) {
    return (v.t == VT::Str && v.hashKind == "Buf") ||
           ((v.t == VT::Num || v.t == VT::Int || v.t == VT::Rat) &&
            (v.hashKind == "Instant" || v.hashKind == "Duration"));
}
struct IdentityToken { std::uint64_t id; };
inline std::shared_ptr<void> identityToken() {
    return std::make_shared<IdentityToken>(IdentityToken{nextWhichSerial()});
}
inline Value& identify(Value& v) { v.extM() = identityToken(); return v; }
```

Each freshly built one stamps a unique token into `ext`. A plain `Value` copy
carries the token along — which is precisely what "the same object" means for
these types — and `===` compares it. `Blob` stays out: it is immutable and
compares by value in Rakudo too.

The token was a bare `make_shared<char>()` at first, compared by address. It
holds a serial number now, because an address is handed to the next token once
this one is gone, and two temporaries would then share a `WHICH`.

The same trick answers a smaller question. `$*INIT-INSTANT` is one `Instant`
read many times, and `$*INIT-INSTANT === $*INIT-INSTANT` must be `True`, so both
reader sites go through one function that hands back the same stored token.

## Ranges remember what they were written with

A `Range` iterates over integers in `rFrom`/`rTo`, or over doubles in `n`/`im`
when it is fractional. But `1/2 .. 1/3` must keep its `Rat`s and
`True .. False` its `Bool`s for `.min`, `.max`, `.bounds` and rendering.

```cpp
// src/Value.h
struct RangeEnds { Value from, to; };
inline void setRangeEnds(Value& r, const Value& from, const Value& to) {
    auto keep = [](const Value& v) {
        if (v.t == VT::Int && v.big()) return true;   // `i` cannot hold it
        return v.t == VT::Rat || v.t == VT::Num || v.t == VT::Bool ||
               v.t == VT::Nil || v.t == VT::Any || v.t == VT::Type;
    };
    auto renders = [&](const Value& v) { return keep(v) || v.t == VT::Int; };
    if (!keep(from) && !keep(to)) return;
    if (!renders(from) || !renders(to)) return;
    attachRangeEnds(r, from, to);
}
```

The endpoints are parked in `ext`, and only when they would actually render
differently. `..` numifies a `Str` endpoint (`"2"` becomes `2`) and a list
endpoint (its element count), so those must *not* be carried; and a
machine-width `Int` renders identically either way, so carrying it would only
cost an allocation on the very hot `1..n` path. A bignum endpoint is carried:
`0..10**42` used to saturate to `0..Inf`, and a bignum has an allocation
already.

That last clause is the pattern to notice. Almost every field in `Value` is
guarded by a test that keeps the common case free.

## The rendering hook

`Value.h` cannot call into `Builtins.cpp`, but a `Range`'s endpoints must
render with `.raku`, which lives there. The solution is a function pointer:

```cpp
// src/Value.h
using RakuReprFn = std::string (*)(const Value&);
extern RakuReprFn g_rakuRepr;
```

A raw pointer is zero-initialised before any dynamic initialisation runs, so
installing it from another translation unit is order-safe — unlike a
`std::function`, which would have its own construction order. There are a
handful of these hooks in the runtime (`g_forceLazy`, `g_endlessLazy`), each
solving the same layering problem the same way. Not every hook is one: `g_objListItems` and `g_deproxy` are
`std::function`s, which is the alternative this paragraph argues against — they
are installed early enough that it has not mattered, which is the sort of thing
that is true until it is not.

## What it costs

| | bytes |
|---|---:|
| `long long` | 8 |
| `std::string` (libc++) | 24 |
| `CowStr` | 40 |
| **`Value`** | **392**, on the build these numbers were taken from |

Every `Value` carries every field, live or not — or, since the cold block, every
field it might plausibly need with the rest one pointer away. A `ValueList` of
integers is about fifty times the size of a `vector<int64_t>` on the build these
numbers come from. That is the price of branch-free field access and trivial
copyability, and it is a real price:
the profile of a method-call-heavy loop puts 31% of the time in heap allocation
and 11% in `Value` copy and destruction.

The number moves, which is why this chapter shows two listings. It has been
392, 376, 344, 208, 128, 80, 64, 56, 48, 40 and 32 bytes, and the next section
walks the whole lineage — including the steps that made it smaller and slower,
and had to be reverted.

The table above prices the 392-byte original because that is the version the
argument was made against, and because the ratio it produces — fifty times a
`vector<int64_t>` — is the number that made the case for shrinking. At 128
bytes the same ratio is sixteen, and at 32 it is four. The design did not
change; the tax did. The two profile shares above come from the profile of
Chapter 10, taken before the struct was first shrunk, and have not been taken
again on this layout.

The sizes of the present layout, printed by a probe compiled against the
headers on 2026-11-10 (arm64 macOS, libc++):

| | bytes |
|---|---:|
| `IStr` | 4 |
| `PayRef`, `HRef<ValueExt>` (a handle each) | 4 |
| `CowStr` | 8 |
| **`Value`** | **32** |
| `ValueExt`, the cold block | 176 |

That instability is also the single most important input to the extension ABI
in Chapter 37, which is why an extension module never sees this struct at all.

## The struct, version by version

`Value` is the most-rewritten data structure in the project, and every rewrite
was measured. The lineage is worth having in one place, because the individual
steps are scattered across four later chapters and because three of them are
lessons rather than wins.

| | bytes | what changed |
|---|---:|---|
| original | 392 | four `std::string`s, eleven `shared_ptr`s, all inline |
| ordinary work | 376 | field-level tidying, no design change |
| *attempted* | *360* | **flags packed to remove padding — reverted** |
| batch 1 | 344 | `hashKind`, `enumName`, `enumType` interned (Chapter 10) |
| batch 2 | 208 | the cold block: rarely-used fields behind one lazy pointer |
| batch 4 | 128 | five payload pointers become one kind-tagged slot |
| *attempted* | *120* | **`i` and `n` in one word, the byte given back — held at 128** |
| design A | 80 | one scalar word, 32-bit `IStr`, bit-field flags, intrusive counts |
| *attempted* | *72* | **the cold block riding in another field's word — reverted** |
| design C | 64 | `CowStr` is 16 bytes, a text of up to 15 inline (Chapter 9) |
| | 56 | the tag block is 4 bytes: `natBits` held as a 4-bit byte count |
| | 48 | `enumName` and `enumType` move into the cold block |
| | 40 | the payload slot and the cold block are 32-bit handles |
| | 32 | `CowStr` is 8 bytes, a text of up to 7 inline |

Design A landed on 2026-10-01, design C on 2026-11-07, and the last four rows
on one day, 2026-11-10. The size is pinned by a `static_assert` in `Value.h`:
32 where handles are on, 48 where they are off.

**The reverted step is the most instructive.** In July 2026 the flags —
`fatRat`, the three `Range` booleans, `natBits` and its two companions — sat
between 8- and 16-byte members and forced 23 bytes of pure padding. Grouping
them was free: no behaviour change, no risk, 376 down to 360. It measured
**2.5% slower**, consistently, over six alternating rounds.

The explanation is that the hot field offsets did not move — `t`, `b`, `i`,
`n`, `im`, `s`, `hashKind` stayed exactly where they were — while `arr` and
every pointer after it shifted by eight bytes. Smaller is not automatically
faster when the thing you shrink is padding a cache line was absorbing anyway,
and a *free* change can still cost. The size campaign that eventually
succeeded did not repack fields; it moved them out of the struct.

**Batch 1 replaced types, not layout.** Three of the four `std::string`s were
secondary type tags drawn from a closed vocabulary — container kinds, type
names — so they became interned 8-byte handles (Chapter 10). Fifteen
non-trivial members became twelve, `sizeof` went 392 to 344, and a JSON parse
got 4 to 6 per cent faster at every document size while a twelve-thousand-entry
hash build went from 302 to 265 nanoseconds per entry. The win is not the
bytes; it is that a copy stopped running three string constructors.

**Batch 2 asked what a typical value actually carries.** A census instrumented
every `Value` destruction and counted which pointers were live: of thirty
million destructions, **25.6 million carried none** of `big`, `ratN`, `ratD`,
`pairKey`, `ext`, `shape` or the `Range` fields. So those moved behind one
lazily-allocated, copy-on-write block, and an `Int` stopped paying for a `Rat`
it does not have. Reads never allocate; writes clone a shared block first.

**Batch 4 collapsed the payload.** The same census said `arr`, `hash`, `code`,
`pairVal` and `obj` are mutually exclusive on every live value — with exactly
one exception, the `Match`, which needs three at once — so five pointers became
one `shared_ptr<void>` plus a one-byte kind tag, and the `Match` got a combined
body. That is where the `MatchData` mentioned at the top of this chapter comes
from: it exists because the union argument this chapter opens with is *true*,
and the one place it is true had to be given somewhere to live.

Batch 4 also produced the campaign's sharpest reminder that a census bounds
what typical programs do and not what the language allows. A container declared
`is default(9)` carries its element default *alongside* its payload — a live
co-occurrence the thirty-million-destruction census never sampled, because it
is rare per value and unremarkable per program. Three regression files
segfaulted within minutes of the change. The default now lives in the cold
block, where rare-per-value belongs.

**Design A stopped at 80, not at the 56 it was drawn for.** It ran as five
batches, each measured against the commit before it, and while they ran every
freed byte was held as a reserved field, so that each change was judged on its
own and not on the stride it happened to produce.

- *`i` and `n` share one word.* An audit build kept the two apart, recorded
  which was written last, and logged every read a shared word would change.
  Over Roast, the regression suite and the module battery it found two writers
  that "cleared" `n` after storing an `Int`, which in one word would have
  zeroed the result. With
  those fixed the union is the only one in the struct, and it holds two
  trivial scalars. At 120 bytes it measured 0.9% slower in the mean; padded
  back to 128 it was level. The cost was the stride and not the union.
- *`IStr` became a 32-bit index* (Chapter 10). With the fields shifted by the
  smaller names, `fib` ran 7% slower; with every live field kept at its old
  offset the mean was level. Field offsets alone move kernels by several
  percent, so a new field order has to be measured, not just packed.
- *The flags became bit-fields.* `t` and `pk_` stay whole bytes: `pk_` as a
  3-bit field cost the method kernels 3 to 4%, because every payload accessor
  tests it.
- *The counts moved into the blocks.* A `shared_ptr` is 16 bytes and its count
  sits in a control block. `Ref<T>` in `src/Ref.h` is 8 bytes and the count is
  in the object. The payload slot became one untyped `PayRef`, and each payload
  class is wrapped in a `Body<T>` whose 16-byte header holds a virtual destroy,
  the count and a word of flags.
- *The cold block out of the struct* is the third reverted step. The block's
  pointer rode in the payload slot of a value with no payload and in the scalar
  word of one that had. It passed Roast at 72 bytes and cost 1.8% in the mean
  for 4 to 10% less memory. The hand-written copy, move and destroy and an
  extra branch in every cold-block lookup were the cost, not the size.

With the held bytes dropped, 80 measured 2.6% faster in the mean than 128, and
a million-element array went from 258 to 164 MB.

**Design C took the string from 32 bytes to 16, and the struct to 64.** Chapter
9 has the string's side of it. For `Value` the points are that nothing in the
struct is a `std::string` any more, and that the choice between a string
always in a body and a short string inline was made by a census, not by
taste: always-in-a-body would have added 7 to 17% more allocations over whole
corpora, and the inline form half a percent.

**From 64 to 32 took four steps on 2026-11-10,** each measured against the one
before on the same machine.

- *56.* `natBits` is only ever 0, 8, 16, 32 or 64, so it is held in bytes in
  four bits. That made the tag block 4 bytes, and the struct lost that one
  byte and the 7 bytes of padding it had forced. Every kernel retired the same
  instructions or fewer.
- *48.* `enumName` and `enumType` moved into the cold block. An enum value, a
  Junction and a `Blob` subtype now carry a cold block; nothing else changed.
  `hashKind`, with about 1,900 uses, stayed inline.
- *40.* The payload slot and the cold block became 32-bit handles, described
  in the next section. This is the first step that costs time: the call
  kernels retired 2.9 to 3.0% more instructions, for the decode of a handle on
  each use.
- *32.* `CowStr` went from 16 bytes to 8, with the string body reached by a
  handle as well. Against 48, most kernels are within 0.6% in instructions,
  the call kernels are up to 1.8% higher, and a wide Junction is 4.7% higher.
  A copy of a `Value` is 8 bytes of string shorter, which pays for most of the
  decode.

Peak memory for a million `Int`s from a `.map`, measured through the day:
134.6 MB at 64 bytes, 118.9 at 56, 103.4 at 48, 88.0 at 40 and 73.8 at 32. The
choice was between 48 and 32. 40 costs more instructions than 32 and saves half
the memory, so it was never a candidate.

**What the shrinking bought that nobody planned.** Two consequences arrived
from outside the campaign's own goals. The first is in Chapter 42: the cost of
parking a `Value` in long-lived addressable storage, which had made a partially
lowered bytecode IR structurally impossible, is a function of how much struct
there is to construct and destroy — 11.2 nanoseconds per node at 376 bytes,
zero at 128. The second is in Chapter 12: at 128 bytes with two `shared_ptr`s
and no self-referential member, a `Value` is *trivially relocatable*, which is
what lets a list of them grow by `memcpy`. Neither was a design goal. Both
follow from the same reduction. Since design C the second holds on every
standard library, because the one member that could point at itself, a
libstdc++ `std::string`, is gone.

**What is left.** At 32 bytes each field is one word. Taking another out means
putting the string over the scalar word, which an allomorph and a type object
forbid because they keep both live, or putting the cold block behind the
payload handle together with a narrower `hashKind`. Neither is started.
Sixteen is not a layout change at all: it needs values to stop being
refcounted and the container to stop living inside the value, which is a
rewrite of assignment and binding rather than a batch. Reference counting was
kept through every step above, and it stays.

## Bodies, counts and handles

Everything a `Value` does not hold inline is a counted block, and there are
three kinds: a payload body, the cold block, and a string body. The count is
in the block. A copy of the `Value` adds one with a relaxed atomic increment,
and the release that takes the count to zero destroys the block out of line.

As pointers the payload slot and the cold block were 16 bytes of the 48. They
are 8 now, because each is a **handle**: an offset, in 16-byte units, into one
reserved region of address space.

```cpp
// src/BodyPool.h
static constexpr unsigned kGrain = 4;        // a handle counts 16-byte units
static constexpr std::size_t kMaxBody = 4096;
inline static char* base_ = nullptr;
static std::uint32_t enc(const void* p) noexcept {
    return (std::uint32_t)(
        (std::size_t)(static_cast<const char*>(p) - base_) >> kGrain);
}
static void* dec(std::uint32_t h) noexcept {
    return base_ + ((std::size_t)h << kGrain);
}
```

Handle zero is null. Nothing about sharing or lifetime changed with the
handles; only how the address is spelled did. `PayRef` and `HRef<ValueExt>`
hold a handle, and a `CowStr` keeps its body's handle in its first four bytes.
`PRef<T>`, the typed reference every other holder of a payload uses, is still
a pointer, so no site outside `Ref.h`, `Value.h` and `BodyPool.h` changed.

A handle needs every body inside the one region, so the bodies needed an
allocator of their own. `SlabPool`, which served them before, makes each block
with `operator new` so that a block can go back to the system. `BodyPool`
keeps that property inside the region:

- It reserves 64 GB of address space once, which is what 32 bits of 16-byte
  units reach, and less where the system grants less. Only address space is
  reserved; pages are made usable 4 MB at a time.
- The region is cut into 64 KB **spans**. A span serves one size class at a
  time: every 16 bytes up to 512, then six more classes up to 4,096.
- A span whose blocks have all come back is given up. Its pages go back to
  the system, past the first 16 such spans kept warm, and it may next serve
  another class. A class keeps its last free span, so a program that
  allocates and frees at a span's edge does not give the pages up each time.
- The hot path is a thread-private free list per size class. A list that runs
  dry takes a batch from the spans under the class's lock, and one that grows
  past its bound hands half back the same way.

A `static_assert` in `makePayload` holds every payload body to what the pool
serves. A program that fills the region gets `std::bad_alloc`.

The decode is the cost. Each use of a payload or its count loads the region's
base, shifts and adds. With the base a compile-time constant the call kernels
measured about half the extra instructions, but that holds only if the system
grants the exact address asked for, and the first address tried was refused.
That variant is not in the tree.

## Two layouts

The 32-byte struct is the layout on 64-bit macOS and Linux. Where handles are
off, `BodyPool` is `SlabPool`, a handle is the pointer, `CowStr` is 16 bytes
and a `Value` is 48: on Windows, on a 32-bit target, and in a build with the
pools off, which is what an AddressSanitizer build is. Building with
`-DRAKUPP_BODY_HANDLES=0` turns them off anywhere. It is one source text for
both, and both have to keep building.

## The pool that was not built

Handles for bodies raise an obvious next question: keep every `Value` in a
pool too, and let a variable or an array element hold a 32-bit index. A probe
on the real 32-byte struct priced it on 2026-11-10 and the idea was not
pursued. A pooled value is 40 bytes where an inline one is 32, and once the
pool has aged, reading an array in order is 4 to 6 times slower, because the
values are wherever the free list put them. The one large win in the probe, a
small `Int` held in the index itself, does not need the pool, and the engine's
form of it is the packed array of Chapter 12.

## Honest limitations

- **Memory per value**, as above. It is the accepted cost of the design, not an
  oversight, but it is the thing to attack first if the design is ever revisited.
- **Nothing in a `Value` may point at itself.** This one has no compiler
  enforcement and no test, and breaking it is silent. `ValueList` relocates its
  buffer with a `memcpy` when the standard library allows it (Chapter 12),
  which is only sound because moving a `Value`'s bytes to a new address and not
  destroying the source is equivalent to move-constructing and destroying: true
  of the scalars, of `IStr`'s index, of the two handles, and of `CowStr`'s
  inline text or body handle. The near-miss was in the struct until design C —
  libstdc++'s short `std::string` stores a pointer to its own inline buffer,
  which is why the relocation path is decided by a run-time probe rather than
  assumed. Any future field with an
  interior pointer, a self-registering handle, or an intrusive list node breaks
  that path, and it will break it by producing wrong data rather than by
  failing to compile.
- **`ext` is untyped.** Nothing but convention stops two subsystems from parking
  incompatible state in it for the same tag.
- **Reference semantics are emulated.** The identity-token trick covers the
  three scalar types that needed it; a fourth would need the same treatment
  added by hand.
- **A handle costs a decode,** a load of the region's base, a shift and an add
  on every use of a payload, and the region is 64 GB of address space that a
  program cannot grow past.
- **Two layouts.** The 48-byte one is the path Windows and an AddressSanitizer
  build take, so a change to the representation has two builds to pass.
- **The tag set is closed.** Adding a `VT` means auditing every `switch` over
  it in the interpreter, the code generator, the serialiser and the dumper —
  which is exactly why new Raku types are added as an existing tag plus a
  marker instead.
