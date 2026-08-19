# Strings: `CowStr`

`Value` holds its `Str` payload in a `CowStr`, not a `std::string`. This
chapter is about why that class had to be written rather than reached for, and
it is the clearest example in the project of a design forced entirely by
measurement.

## The problem

A `Value` is copied by value everywhere: every argument pass, every operand
evaluation, every list element, every return from `eval`. With a `std::string`
member, a long string was `memcpy`'d on every one of those.

That is O(length) **per operation**, which makes any tokenizer written in Raku
quadratic. `JSON::Fast` needed 13.9 seconds on a 421 KB document that Rakudo
parses in 51 milliseconds, and the profile was copying, not parsing.

Measured, 20,000 operations on a 200 KB string:

| | `std::string` | `CowStr` |
|---|---:|---:|
| pass the string to a sub, no work done | 142 ms | 24 ms |
| `nqp::ordat` on a global | 79 ms | 11 ms |

## What it is

Two arms, exactly one live:

```cpp
// src/Value.h
class CowStr {
    std::string s_;                        // short: inline
    std::shared_ptr<const StrBody> p_;     // long: shared, immutable
    static constexpr size_t kPromote = 64;

    void take(std::string x) {
        if (x.size() >= kPromote) {
            p_ = std::make_shared<const StrBody>(std::move(x)); s_.clear();
        } else { s_ = std::move(x); p_.reset(); }
    }
public:
    const std::string& str() const { return p_ ? p_->text : s_; }
    std::string& mut() { if (p_) { s_ = p_->text; p_.reset(); } return s_; }
};
```

Short strings stay inline, where `std::string`'s own small-buffer optimisation
already makes a copy free and sharing would cost an allocation to save nothing.
Long strings are promoted once into a shared immutable body, after which
copying is a refcount bump.

Three decisions inside that deserve their own paragraphs.

### Promotion is eager

At construction, not lazily on first copy. Raku++ runs work in parallel, and a
lazy promotion would have to mutate the source from a `const` copy constructor
— two threads copying the same `Value` would race on it.

Eager promotion means **a `const CowStr` is never written to at all**, which is
what makes the type safe to share without a lock. That single property is what
lets everything else here be lock-free.

### The threshold is 64

Above every mainstream small-buffer capacity, measured:

| standard library | `sizeof(std::string)` | SSO capacity |
|---|---:|---:|
| libc++ (clang) | 24 | 22 |
| libstdc++ (g++ 14, 16) | 32 | 15 |

So 64 leaves a band in which a copy is a couple of words and no allocation
happens either way, and only promotes when copying is the thing being paid for.
It is not a tuned number; it is a deliberately safe one.

### The body caches string properties

This is the part no general-purpose string type can do, and the real reason
`CowStr` exists rather than a third-party copy-on-write string.

```cpp
// src/Value.h
struct StrBody {
    std::string text;
    mutable std::atomic<signed char> allAscii{-1};
    mutable std::atomic<signed char> crFree{-1};
    mutable std::atomic<long long>   nGraphemes{-1};
};
```

Raku string positions are **grapheme** positions, and Raku++ stores UTF-8
bytes. So every indexing operation must first establish whether a byte index is
also a grapheme index. Establishing that by scanning is O(position) per
character examined — which is the *other* half of the quadratic, independent of
copying.

Because the promoted body is immutable, the answer can be computed once and
cached on it. Three flags suffice:

- `allAscii`: every byte below 0x80, so a byte index is a codepoint index;
- `crFree`: no carriage return which, together with `allAscii`, means a byte
  index is a **grapheme** index — CR LF is the one ASCII sequence that clusters
  under UAX #29;
- `nGraphemes`: the answer to `.chars`.

They are reached through `cowAllAscii`, `cowByteIsGraphemeIndex` and
`cowGraphemeCount` in `BuiltinsShared.h`.

The caches need no lock. Two threads may each compute one, but the text is
immutable, so they compute the same answer and the store is idempotent. They
live on the body, so they exist only for promoted strings — which is exactly
the case that needs them; short strings are cheap to rescan.

## Does C++ have this already? It did, and removed it

**libstdc++'s pre-C++11 `basic_string` was refcounted copy-on-write.** C++11
made that non-conforming. The string requirements added contiguous storage,
O(1) non-`const` `operator[]` and `data()`, and iterator-invalidation rules
under which one copy's mutation must not disturb another copy's references.
Copy-on-write cannot satisfy all of them at once: non-`const` element access
would have to detach, and detaching is O(n). GCC 5's dual ABI
(`_GLIBCXX_USE_CXX11_ABI`, `std::__cxx11::basic_string`) exists to carry that
break. libc++ was never copy-on-write.

C++11's replacement answer was **move semantics**, which solves "stop copying
temporaries" and not this problem. Every copy in the list at the top of this
chapter is a genuine copy, not a move: an argument passed to a function that
keeps it, an element stored into a list.

**None of the reasons copy-on-write was banned apply to `CowStr`,** because it
is not trying to be a `std::string`. Its `operator[]`, `data()`, `begin()` and
`substr` are `const`-only; the single mutation door is `mut()`, which detaches
explicitly and hands back a real `std::string&`. What C++11 outlawed was
copy-on-write *hiding behind* an interface that promises O(1) non-`const`
element access. `CowStr` promises no such thing.

### The alternatives, and why each is not it

| | why not |
|---|---|
| `std::string_view` | non-owning; `Value` owns its string |
| small-buffer optimisation | already the short arm — that is what `s_` relies on |
| `shared_ptr<const std::string>` | already the long arm; `CowStr` is mostly the *policy* for choosing between the two |
| `std::atomic<std::shared_ptr<T>>` | only needed if the pointer were rebound concurrently, which eager promotion guarantees it never is |

Outside the standard the pattern is thoroughly proven and lives attached to
frameworks: `folly::fbstring` is the closest relative — a drop-in
`basic_string` with three tiers, the top one copy-on-write with an atomic
refcount; Qt has done "implicit sharing" across its whole container library for
about twenty-five years; `absl::Cord` is a tree of refcounted immutable chunks,
which wins when *concatenating* large strings rather than copying them; Rust's
`Cow<'_, str>` makes the same idea explicit in the type system. LLVM went the
other way entirely, with `StringRef` and `Twine` and no copy-on-write anywhere.

**Why not adopt one?** Folly and Qt are large dependencies for a subset of what
is needed, and this project vendors nothing it can write in a page. But the
deciding reason is the property cache: `fbstring`, `QString` and `Cord` would
all give the copy elision and none of the `allAscii`/`crFree`/`nGraphemes`
caching, which is the half of the fix that removed the *scanning* quadratic.
Bolting that onto a third-party string means a side table keyed on the buffer
address, with its own lifetime and thread-safety problem — strictly worse than
owning forty lines.

The threshold difference makes the same argument from the other end.
`fbstring` promotes at 255 because copy avoidance is all it buys. `CowStr`
promotes at 64 because promotion also buys the cache.

## What it costs

| | bytes |
|---|---:|
| `std::string` (libc++) | 24 |
| `std::shared_ptr` | 16 |
| **`CowStr`** | **40** |
| `StrBody` | 40 |

Both arms are stored side by side even though only one is ever live, so
`CowStr` is the sum rather than the maximum. That is a 16-byte, 4% growth of
`Value`, paid on every `Value` everywhere. The performance gate passed and
Roast came out marginally up, so it is bought and paid for — but it is a real
tax and should be named as one. On libstdc++ the same layout is 48 bytes,
because `sizeof(std::string)` is 32 there.

A promoted string also costs **two** allocations: `make_shared` fuses the
control block with `StrBody`, but `StrBody::text` heap-allocates its own buffer
for anything past the small-buffer capacity.

## Rules for working with it

The type forwards about 1,100 read sites unchanged, which is the point. The
places where it does not behave like a `std::string` are worth knowing.

- **`mut()` is the only write door, and it detaches.** After `mut()` the string
  is inline again and stays inline until it is next *assigned*, which is where
  promotion happens. A long string mutated in a loop is therefore unpromoted for
  the whole loop; if that ever shows up in a profile, build into a local
  `std::string` and assign once at the end.
- **References from `str()` do not survive an assignment.** The same rule as
  `std::string`, but easier to forget through the implicit conversion.
- **Both converting constructors are `explicit` on purpose.** With an implicit
  `std::string` to `CowStr` conversion alongside the `CowStr` to
  `const std::string&` one, every `cond ? value.s : someString` became ambiguous
  in both directions.
- **Comparisons and `+` are spelled out by hand.** Two real reasons: no
  conversion is considered for a built-in operator when neither operand is a
  class type with a candidate, so `CowStr == CowStr` needs its own overload; and
  `std::string`'s comparisons are templates, and template deduction never
  considers a user-defined conversion, so `value.s == "Int"` needs a real
  candidate too. A macro generates the twenty-four resulting overloads.
- **Do not add a non-`const` `operator[]`, `begin()` or `data()`.** That is
  exactly the interface that made copy-on-write non-conforming for
  `std::string`. Reach for `mut()`.
- **Do not cache `body()` across an assignment.** It is the raw `StrBody*`, and
  the `shared_ptr` that owns it can be replaced.

## Deliberately left on the table

Both are real, both unmeasured, and neither should be done on faith — the
project's rule is that a performance change is an interleaved A/B against
`perf-guard` under the same conditions.

- **A union instead of two side-by-side members.** `fbstring` is a union and
  stays at `sizeof(std::string)`; the same here returns 16 bytes per `CowStr`.
  It costs the very readable `p_ ? p_->text : s_` and needs hand-written
  copy, move and destroy. Worth it only if `Value`'s size shows up in a
  measurement — and one attempt to blame `Value`'s size has already been
  measured and rejected.
- **One allocation instead of two** for a promoted string, by storing the
  refcount and the caches inline ahead of the characters. Only matters if the
  promotion *rate* is high.

## Checking that it is working

The failure mode is silent: a change that puts a hot string back on the copying
path costs time and nothing else. Two cheap checks.

**Measure scaling, not speed.** Time the operation over inputs of *n*, 2*n*,
4*n*, 8*n*. Doubling per doubling is fine; quadrupling is the bug this class
exists to prevent. That is the test which found `findnotcclass` after four
other sites had already been fixed.

**Confirm promotion is actually happening.** A string that never crosses 64
bytes is never promoted and caches nothing — which is correct, but means a
benchmark built from short strings proves nothing about either mechanism.
