# Strings — `CowStr`, and why the standard library has nothing to offer

`Value` holds its `Str` payload in a `CowStr` ([src/Value.h](../../src/Value.h)),
not a `std::string`. This file explains what that class is, why it had to be
written rather than reached for, how it compares to the copy-on-write strings
that exist in the wild, and the rules for working with it.

The measurements that forced it are in
[dev/findings/STRING-SCAN-QUADRATICS.md](../dev/findings/STRING-SCAN-QUADRATICS.md);
this file is the design side of the same story. `Value` itself is described in
[RUNTIME.md](RUNTIME.md#value--the-fat-tagged-struct).

Numbers below: 2026-08-09, arm64 macOS, Apple clang 17 (libc++) unless a
toolchain is named.

## The problem it solves

`Value` is copied by value everywhere — every argument pass, every operand
evaluation, every list element. With a `std::string` member, a long string was
`memcpy`'d on every one of those. That is O(length) **per operation**, which
makes any tokenizer written in Raku O(n²): JSON::Fast needed 13.9 s on a
421 KB document that Rakudo parses in 51 ms, and the profile was copying, not
parsing.

Measured, 20k operations on a 200 KB string:

| | `std::string` | `CowStr` |
|---|---:|---:|
| pass the string to a sub, no work done | 142 ms | 24 ms |
| `nqp::ordat` on a global | 79 ms | 11 ms |

## What it is

Two arms, exactly one live:

```cpp
class CowStr {
    std::string s_;                          // short: inline
    std::shared_ptr<const StrBody> p_;       // long: shared, immutable
    static constexpr size_t kPromote = 23;
};
```

- **Short strings stay inline.** Below the threshold, `std::string`'s own
  small-buffer optimisation already makes a copy free, and sharing would cost
  an allocation per string to save nothing.
- **Long strings are promoted into a shared immutable body.** Copying is then
  a refcount bump.

Three decisions in there are worth their own paragraphs.

### Promotion is eager

At construction, not lazily on first copy. rakupp runs work in parallel, and a
lazy promotion would have to mutate the source from a `const` copy
constructor — two threads copying the same `Value` would race on it. Eager
promotion means **a `const CowStr` is never written to at all**, which is what
makes the type safe to share without a lock.

### The threshold is 23 (it was 64 until 2026-09-02)

The relevant numbers are the SSO capacities, measured:

| standard library | `sizeof(std::string)` | SSO capacity |
|---|---:|---:|
| libc++ (clang) | 24 | 22 |
| libstdc++ (g++ 14, 16) | 32 | 15 |

The original 64 sat above every mainstream capacity: it left a band where a
copy was a couple of words and no allocation happened either way, and promoted
only when copying was clearly the thing being paid for. Deliberately safe
rather than tuned.

It was also wrong at one end. Between libc++'s 22 and the threshold's 64 sat a
band where the inline arm had already stopped being free — a 30-byte string is
a heap `std::string` that mallocs on **every copy**, while a 70-byte one copies
by refcount — so a 33-byte string cost more to pass around than a 68-byte one,
for no reason but the constant. The threshold is now libc++'s boundary, and the
policy with it: promote wherever `std::string` would allocate anyway.

Measured at best of five runs: mid-band strings copied twice **+19.8%**
instructions, built and read once **+4.9%**, `textsplit` **+3.0%**; grammar
parsing, `streq` and `strcat` flat because their strings are not in the band.
One counter-case at **−5.6%**: a mid-band string used as a hash key, which is
constructed, hashed, copied *out* into the hash's key storage and dropped —
never reaching the second copy at which promotion starts paying, while paying
promotion's two allocations up front (`make_shared`'s block, then `StrBody`'s
own `std::string` buffer). Storing `StrBody`'s text inline would remove that
loss; it is the same change the 32-byte `Value` design needs.

### The body caches string properties

This is the part no general-purpose string can do, and the reason `CowStr`
exists rather than a third-party COW string:

```cpp
struct StrBody {
    std::string text;
    mutable std::atomic<signed char> allAscii{-1};   // byte index == codepoint index
    mutable std::atomic<signed char> crFree{-1};     // with allAscii: byte index == GRAPHEME index
    mutable std::atomic<long long>   nGraphemes{-1}; // .chars
};
```

Raku string positions are *grapheme* positions and rakupp stores UTF-8 bytes,
so every indexing op must first establish that a byte index is a grapheme
index. Doing that by scanning is O(position) per character examined — the
other half of the quadratic. Because the promoted body is immutable, the
answer can be computed once and cached on it, reached through
`cowAllAscii` / `cowByteIsGraphemeIndex` / `cowGraphemeCount`
([BuiltinsShared.h:43](../../src/BuiltinsShared.h)).

The caches need no lock. Two threads may each compute one, but the text is
immutable so they compute the same answer; the store is idempotent.

They live on the body, so they exist only for promoted strings — which is
exactly the case that needs them. Short strings are cheap to rescan.

---

## Does C++ have this already? No — and it used to

**The standard library had a copy-on-write string and removed it.**
libstdc++'s pre-C++11 `basic_string` was refcounted COW. C++11 made that
non-conforming: the string requirements added contiguous storage, O(1)
non-`const` `operator[]` and `data()`, and iterator/reference invalidation
rules under which one copy's mutation must not disturb another's references.
COW cannot satisfy all of them at once — non-`const` element access would have
to detach, and detaching is O(n). GCC 5's dual ABI
(`_GLIBCXX_USE_CXX11_ABI`, `std::__cxx11::basic_string`) exists to carry that
break. libc++ was never COW.

C++11's replacement answer was **move semantics** — which solves "stop copying
temporaries" and not our problem. Every copy in the list at the top of this
file is a genuine copy, not a move: an argument pass to a function that keeps
its argument, an element stored into a list. Moves do not apply.

**None of the reasons COW was banned apply to `CowStr`**, because it is not
trying to be a `std::string`. Its `operator[]`, `data()`, `begin()` and
`substr` are `const`-only; the single mutation door is `mut()`, which detaches
explicitly and returns a real `std::string&`. What C++11 outlawed was COW
*hiding behind* an interface that promises O(1) non-`const` element access.
`CowStr` promises no such thing.

### What the standard does offer, and why each is not it

| | why not |
|---|---|
| `std::string_view` (C++17) | non-owning. `Value` owns its string; a view cannot be the storage. Useful at call boundaries, which is a separate change |
| SSO | already the short arm — that is precisely what `s_` relies on |
| `std::shared_ptr<const std::string>` | already the long arm. `CowStr` is mostly the *policy* for choosing between the two |
| `std::atomic<std::shared_ptr<T>>` (C++20) | only needed if the pointer were rebound concurrently. Eager promotion plus an immutable body means it never is |

There is no `std::cow_string` and no live proposal for one.

### What exists outside the standard

| | shape |
|---|---|
| **`folly::fbstring`** | the closest thing by far: a drop-in `basic_string` with three tiers — SSO ≤23, eager-copy ≤255, and above that **COW with an atomic refcount**. `CowStr`'s design with one extra tier and a much higher threshold |
| **Qt `QString`, `QByteArray`** | "implicit sharing" — COW with atomic refcounts across the whole container library, for about 25 years |
| **`absl::Cord`** | a tree of refcounted immutable chunks. Same motivation, different structure; it wins when *concatenating* large strings is the hot operation rather than copying them |
| **Rust `Cow<'_, str>`** | the same concept made explicit in the type system instead of hidden behind a value type |
| **LLVM** | went the other way entirely: `StringRef` (view) plus `Twine` (deferred concatenation), no COW anywhere |

The pattern is thoroughly proven. It simply lives outside the standard, and
every mature implementation of it is attached to a framework.

### Why we did not adopt one

`folly` and `Qt` are large dependencies for a subset of what is needed, and
rakupp deliberately vendors nothing it can write in a page. But the deciding
reason is the property cache: `fbstring`, `QString` and `Cord` would all give
the copy elision and none of the `allAscii`/`crFree`/`nGraphemes` caching,
which is the half of the fix that removed the *scanning* quadratic. Bolting
that onto a third-party string means a side table keyed on the buffer address,
with its own lifetime and thread-safety problem — strictly worse than owning
40 lines.

The threshold difference is the same argument from the other end: `fbstring`
promotes at 255 because copy avoidance is all it buys. `CowStr` promotes at 23
because promotion also buys the cache.

---

## What it costs

| | bytes |
|---|---:|
| `std::string` (libc++) | 24 |
| `std::shared_ptr` | 16 |
| **`CowStr`** | **40** |
| `StrBody` | 40 |
| **`Value`** | **392** (was 376) |

Both arms are stored side by side even though only one is ever live, so
`CowStr` is the sum rather than the maximum. That is the 16-byte, 4% growth of
`Value` — paid on every `Value`, everywhere. The perf gate passed and Roast
came out marginally up, so it is bought and paid for, but it is a real tax and
should be named as one. On libstdc++ the same layout is 48 bytes, since
`sizeof(std::string)` is 32 there.

A promoted string also costs **two** allocations: `make_shared` fuses the
control block with `StrBody`, but `StrBody::text` heap-allocates its own
buffer for anything past SSO.

---

## Rules for working with it

The type forwards ~1,100 read sites unchanged, which is the point — but the
places where it does not behave like a `std::string` are worth knowing.

- **`mut()` is the only write door, and it detaches.** After `mut()` the
  string is inline again and stays inline until it is next *assigned*, which
  is where promotion happens. A long string mutated in a loop is therefore
  unpromoted for the whole loop; if that ever shows up in a profile, the fix
  is to build into a local `std::string` and assign once at the end.
- **References from `str()` do not survive an assignment.** `const auto& r =
  v.s.str();` then `v.s = something;` leaves `r` dangling — the same rule as
  `std::string`, but easier to forget through the implicit conversion.
- **Both converting constructors are `explicit` on purpose.** With an implicit
  `std::string` → `CowStr` alongside the `CowStr` → `const std::string&`
  conversion, every `cond ? value.s : someString` became ambiguous in both
  directions. Assignment covers the cases that genuinely want to convert.
- **Comparisons and `+` are spelled out by hand**
  ([Value.h:128](../../src/Value.h)). Two reasons, both real: no conversion is
  considered for a built-in operator when neither operand is a class type with
  a candidate, so `CowStr == CowStr` needs its own overload; and
  `std::string`'s comparisons are templates, and template deduction never
  considers a user-defined conversion, so `value.s == "Int"` needs a real
  candidate too. That is what the `RAKUPP_COWSTR_CMP` macro generates.
- **Do not add a non-`const` `operator[]`, `begin()` or `data()`.** That is
  exactly the interface that made COW non-conforming for `std::string`, and it
  would reintroduce every problem described above. Reach for `mut()`.
- **Do not cache `body()` across an assignment.** It is the raw
  `StrBody*`; the `shared_ptr` that owns it can be replaced.

## Deliberately left on the table

Both of these are real, both are unmeasured, and neither should be done on
faith — the project's rule is that a perf change is an A/B against
`perf-guard`, interleaved, same conditions.

- **A union instead of two side-by-side members.** `fbstring` is a union and
  stays at `sizeof(std::string)`; doing the same here returns 16 bytes per
  `CowStr` and takes `Value` from 392 back to 376. It costs the very readable
  `p_ ? p_->text : s_` and needs hand-written copy/move/destroy. Worth it only
  if `Value` size shows up in a measurement — and [one attempt to blame
  `Value`'s size](../dev/findings/STRING-SCAN-QUADRATICS.md) has already been
  measured and rejected.
- **One allocation instead of two** for a promoted string, by storing the
  refcount and the caches inline ahead of the characters the way `fbstring`
  does. Only matters if the promotion *rate* is high; the workload to measure
  it on is JSON::Fast over the 421 KB document.

## Checking that it is working

The failure mode is silent: a change that puts a hot string back on the
copying path costs time and nothing else. Two cheap checks.

- **Scaling, not speed.** Time the operation over inputs of n, 2n, 4n, 8n. ×2
  per doubling is fine; ×4 is the bug this class exists to prevent. This is
  the test that found `findnotcclass` after four other sites had been fixed.
- **Promotion actually happening.** A string that never crosses 23 bytes is
  never promoted and never caches anything — which is correct, but means a
  benchmark built from short strings proves nothing about either mechanism.
