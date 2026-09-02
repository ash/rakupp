\part{The Value Model}

# `Value`: the Fat Tagged Struct

Raku is dynamically typed. A variable holds an `Int` now and a `Hash` later,
routines dispatch on the runtime types of their arguments, lists can be
infinite, and an object can grow a role while the program runs. Raku++ is
written in statically-typed C++17 with no garbage collector. One data structure
carries the entire distance between those two facts.

```cpp
// src/Value.h — as it stands
enum class VT : uint8_t { Nil, Any, Bool, Int, Num, Str, Array, Hash, Code,
                          Range, Pair, Type, Whatever, Object, Rat, Regex,
                          Match, Complex };
enum class PK : uint8_t { None, List, Hash, Code, PairV, Obj, Match };

struct Value {                     // 128 bytes
    long long i = 0;               // Int; a Type's definiteness; a Seq's position
    double    n = 0;               // Num; the real part of a Complex
    CowStr    s;                   // Str; also the Type name and the Pair key
    IStr      hashKind;            // "" Hash, else Set/Bag/Mix/Buf… — interned
    VT   t = VT::Any;              // the discriminator
    bool b, isList, itemized, objKeyed, readonly, namedArg,
         natSigned, natFloat;      // packed together so they cost one word
    PK   pk_ = PK::None;           // what the payload slot holds
    int  natBits = 0;              // native width uint8/int16/…; 0 = not native
    std::shared_ptr<void>     p_;  // THE payload slot: a list, a hash, a
                                   // Callable, a Pair's value, an object — or a
                                   // MatchData carrying three of them at once
    std::shared_ptr<ValueExt> x_;  // the cold block: Rat parts, Complex imag,
                                   // Range ends, BigInt, shape, ofType…
                                   // null on almost every value
    IStr enumName, enumType;       // enum key and enum type — interned
};
```

Two `shared_ptr`s, one interned tag, a packed word of flags. That is not what
this chapter was written against, and the original is worth seeing, because
every argument below is easier to follow when the fields are all in the open:

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
three campaigns that took 392 to 128 (Chapter 40) moved fields; they did not
rename anything a reader or a caller uses. `arr`, `hash`, `code`, `pairVal` and
`obj` became one kind-tagged slot, and `big`, `ratN`, `ratD`, `pairKey`, `ext`,
`shape`, `ofType`, `im` and the `Range` ends went behind a lazily-allocated
cold block — but every one of them is still spelled `v.arr()`, `v.ratN()`,
`v.rFrom()`, and returns what it always did. Roughly 3,900 call sites depended
on that, which is why the accessors were introduced *before* the fields moved.
The struct got smaller; the model did not change.

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
`shared_ptr` members make copies of arrays, hashes and objects a refcount bump,
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
if (enumName == "any" || enumName == "all" ||
    enumName == "one" || enumName == "none") return "Junction";
```

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
    else v.big = std::make_shared<BigInt>(b);
    return v;
}
```

**A `FatRat` is a `Rat` with a flag.** Same storage, but the flag carries the
type identity — contagious through arithmetic — and exempts the value from the
denominator spill described in Chapter 11.

**A native integer container is a `Value` with a width.** `my uint8 $b` sets
`natBits = 8`, `natSigned = false`, and every assignment masks to that width.

## `ext`: the opaque slot

One member is deliberately untyped:

```cpp
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
           (v.t == VT::Num && (v.hashKind == "Instant" ||
                               v.hashKind == "Duration"));
}
inline Value& identify(Value& v) { v.ext = std::make_shared<char>(); return v; }
```

Each freshly built one stamps a unique token into `ext`. A plain `Value` copy
carries the token along — which is precisely what "the same object" means for
these types — and `===` compares it. `Blob` stays out: it is immutable and
compares by value in Rakudo too.

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
endpoint (its element count), so those must *not* be carried; and an `Int`
renders identically either way, so carrying it would only cost an allocation on
the very hot `1..n` path.

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
handful of these hooks in the runtime (`g_objListItems`, `g_deproxy`), each
solving the same layering problem the same way.

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
392, 376, 344, 208 and 128 bytes, and the next section walks the whole lineage
— including the step that made it smaller and slower, and had to be reverted.

The table above prices the 392-byte original because that is the version the
argument was made against, and because the ratio it produces — fifty times a
`vector<int64_t>` — is the number that made the case for shrinking. At 128
bytes the same ratio is sixteen. The design did not change; the tax did.

That instability is also the single most important input to the extension ABI
in Chapter 36, which is why an extension module never sees this struct at all.

## The struct, version by version

`Value` is the most-rewritten data structure in the project, and every rewrite
was measured. The lineage is worth having in one place, because the individual
steps are scattered across four later chapters and because two of them are
lessons rather than wins.

| | bytes | what changed |
|---|---:|---|
| original | 392 | four `std::string`s, eleven `shared_ptr`s, all inline |
| ordinary work | 376 | field-level tidying, no design change |
| *attempted* | *360* | **flags packed to remove padding — reverted** |
| batch 1 | 344 | `hashKind`, `enumName`, `enumType` interned (Chapter 10) |
| batch 2 | 208 | the cold block: rarely-used fields behind one lazy pointer |
| batch 4 | 128 | five payload pointers become one kind-tagged slot |
| design A | *56* | proposed: intrusive refcount, kind-specific bodies, packed tags |
| design C | *32* | proposed: the string leaves the struct too |

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

**What the shrinking bought that nobody planned.** Two consequences arrived
from outside the campaign's own goals. The first is in Chapter 41: the cost of
parking a `Value` in long-lived addressable storage, which had made a partially
lowered bytecode IR structurally impossible, is a function of how much struct
there is to construct and destroy — 11.2 nanoseconds per node at 376 bytes,
zero at 128. The second is in Chapter 12: at 128 bytes with two `shared_ptr`s
and no self-referential member, a `Value` is *trivially relocatable*, which is
what lets a list of them grow by `memcpy`. Neither was a design goal. Both
follow from the same reduction.

**What is left.** Fifty-six bytes is reachable without touching the string: an
intrusive refcount instead of `shared_ptr`, kind-specific bodies instead of the
generic cold block, the tag word packed, and `i`/`n` unioned. Thirty-two means
the string leaves the struct, and the probe says the two ways of doing that
differ by seven times in opposite directions — a heap body is 1.6× slower than
today, an inline buffer 3.7 to 4.4× faster, and the second costs a
fifteen-hundred-site type change. Sixteen is not a layout change at all: it needs
values to stop being refcounted and the container to stop living inside the
value, which is a rewrite of assignment and binding rather than a batch.

## Honest limitations

- **Memory per value**, as above. It is the accepted cost of the design, not an
  oversight, but it is the thing to attack first if the design is ever revisited.
- **Nothing in a `Value` may point at itself.** This one has no compiler
  enforcement and no test, and breaking it is silent. `ValueList` relocates its
  buffer with a `memcpy` when the standard library allows it (Chapter 12),
  which is only sound because moving a `Value`'s bytes to a new address and not
  destroying the source is equivalent to move-constructing and destroying: true
  of the scalars, of `IStr`'s interned pointer, and of the `shared_ptr` slots.
  The near-miss is already in the struct — libstdc++'s short `std::string`
  stores a pointer to its own inline buffer, which is why the relocation path
  is decided by a run-time probe rather than assumed. Any future field with an
  interior pointer, a self-registering handle, or an intrusive list node breaks
  that path, and it will break it by producing wrong data rather than by
  failing to compile.
- **`ext` is untyped.** Nothing but convention stops two subsystems from parking
  incompatible state in it for the same tag.
- **Reference semantics are emulated.** The identity-token trick covers the
  three scalar types that needed it; a fourth would need the same treatment
  added by hand.
- **The tag set is closed.** Adding a `VT` means auditing every `switch` over
  it in the interpreter, the code generator, the serialiser and the dumper —
  which is exactly why new Raku types are added as an existing tag plus a
  marker instead.
