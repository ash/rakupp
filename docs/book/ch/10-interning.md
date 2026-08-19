# Interning and Comparison: `IStr` and `MName`

Two small headers, ninety lines each, both solving the same shape of problem:
**a string that is compared far more often than it is created**. They arrived
from different directions and ended up with almost the same interface, which is
itself the point.

## `MName`: the method name as the dispatcher sees it

Method dispatch on a built-in value is a long ladder of name comparisons in
`Builtins.cpp` and its three continuation files — roughly 1,640 of them. So the
comparison itself is hot.

The path to this fix is worth following, because two plausible diagnoses were
ruled out by measurement before the real one was found.

A profile of `for ^3000000 { "ab".chars }` put **60% of the time in string
comparison**: `_platform_strlen` 19%, an out-of-line
`std::operator==(const string&, const char*)` 19%, `DYLD-STUB$$strlen` 14%,
`memcmp` 8%.

That looks like the ladder's length. Earlier measurement had seemed to exonerate
it: `.chars` sits 177 comparisons into the file and `.uc` 812, yet they cost the
same. **Both facts are true.** Position *in the file* is not the number of
comparisons *executed*, because the arms are guarded by invocant type — a `Str`
invocant only ever runs the `Str` arms, and `.chars`, `.uc` and `.uniname` are
all in that same group.

The real problem was that `strlen` was being called **at run time on a string
literal**. Clang normally folds that away, and in a small function it does — a
1,700-branch toy reproduction compiles to zero `strlen` calls. But
`methodCallInner` was about 8,900 lines with 1,640 comparisons, far past the
optimizer's inlining budget: `std::operator==` stayed out of line, and out of
line it cannot see the literal, so it measured it with `strlen` on every call.

The fix does not depend on the optimizer's mood. Wrap the name in a type whose
`operator==` takes the literal **by reference to array**, so the length is part
of the template argument:

```cpp
// src/MethodName.h
struct MName {
    const std::string& s;
    std::size_t n;        // cached length: the first test on every comparison
    std::uint64_t pre;    // first 8 bytes packed, so short names compare as one

    explicit MName(const std::string& v)
        : s(v), n(v.size()), pre(pack(v.data(), v.size())) {}

    template <std::size_t N>
    RAKUPP_ALWAYS_INLINE bool operator==(const char (&lit)[N]) const {
        if (n != N - 1) return false;
        if (N - 1 <= 8) return pre == pack(lit, N - 1);
        return std::memcmp(s.data(), lit, N - 1) == 0;
    }
};
```

One `const MName m{mName};` at the top of the function, and every `m == "chars"`
site becomes a size check — which rejects nearly every candidate outright —
followed by an inlined comparison. Four compile errors resulted, all of them
stream or concatenation overloads, which is why the struct also forwards `+`,
`<<` and the handful of `std::string` members the chain uses.

Two details earn their keep.

**`always_inline` is not optional.** Left to itself the optimizer emits
`MName::operator==<N>` out of line in this function too, and an out-of-line
call costs more than the comparison it performs.

**The first eight bytes are packed into a `uint64_t` at construction**, so any
name of eight characters or fewer — which is most of them — compares as a single
integer against a literal packed at compile time, with no `memcmp` at all.

Measured, one million iterations:

| | before | wrapper | + always_inline & packing |
|---|---:|---:|---:|
| `'ab'.chars` | 1.63 s | 1.19 s | **1.17 s** |
| `'ab'.uc` | 1.78 s | 1.15 s | **0.95 s** |
| `'ab'.uniname` | 1.61 s | 1.08 s | **0.71 s** |

`.chars` barely moves because it is reached early. `.uniname` is deep in the
`Str` arm and gains the most.

### Where that leaves dispatch

Name comparison went from 60% of the profile to **8.5%**, which retires it as a
target. A perfect-hash dispatch table would only be chasing that 8.5%, and it is
not a drop-in: the ladder is not a pure dispatch on the name. Arms are guarded
by invocant type and argument shape, and later generic arms deliberately catch
what earlier specific ones decline. Turning 1,640 of those into table entries
means giving each its own guard *and* preserving the fall-through order between
them — a large, risky rewrite for a single-digit percentage.

The 42% now sitting in allocation and `Value` churn is the real remaining
target, and it is a different problem (Chapter 16).

## `IStr`: the storable counterpart

`Value` carried four `std::string` members — `hashKind`, `enumName`, `enumType`,
`ofType` — that are **empty on almost every value**. An empty `std::string` is
not free: it is 24 bytes, and it is constructed, copied and destroyed on every
one of the roughly two million `Value` copies a 278 KB JSON parse makes.

Measured on that parse:

- `Value`'s implicit constructor, destructor and copy were **12.8% of self
  time**, the top line of the profile;
- `std::string == const char*` — which calls `strlen`, then `memcmp` — plus its
  helpers was about **10% more**, and `hashKind` is compared against a literal
  on paths that run per parameter per call.

The strings that ever reach these fields are a small closed set in practice:
container kinds and type names. So intern them.

```cpp
// src/IStr.h
struct IStr {
    struct Entry { std::string s; std::size_t n; std::uint64_t pre; };
    const Entry* e = nullptr;      // null IS the empty string

    static const Entry* intern(const char* p, std::size_t k) {
        if (!k) return nullptr;
        static std::deque<Entry> storage;   // stable addresses
        static std::unordered_map<std::string, const Entry*> index;
        static std::shared_mutex mu;
        std::string key(p, k);
        { std::shared_lock<std::shared_mutex> rd(mu);
          auto it = index.find(key);
          if (it != index.end()) return it->second; }
        std::unique_lock<std::shared_mutex> wr(mu);
        // … re-check, then append and index …
    }

    // interned, so identity IS equality
    bool operator==(const IStr& o) const { return e == o.e; }
};
```

Eight bytes, trivially copyable, trivially destructible. A default-constructed
`IStr` costs a zeroed pointer where a `std::string` cost a constructor call.
Comparing two `IStr`s is a pointer comparison, because interning makes identity
equality. Comparing against a literal uses the same packed-prefix trick as
`MName`.

**Interning takes a lock, but only on assignment from text.** Copies and
comparisons — the operations `Value` does millions of times — never reach it.
Readers share the lock; only a miss serialises.

The operator surface is deliberately the same shape as `MName`'s, so the
roughly 1,300 existing `.hashKind == "Buf"` and `.ofType.empty()` sites compiled
unchanged.

### The intern table is never freed

That is deliberate. Entries are type names and kind tags, bounded by the
program's own vocabulary, and a stable address is what makes the handle
comparable by pointer.

It also imposes a rule, and there is one field that breaks it. `ofType` is
**not** interned, and must stay that way until one site moves:

```cpp
// src/Value.h
// `IO::Path`'s `:CWD` rides in `ofType` because a path value has no other
// use for it, so this field can hold a runtime DIRECTORY NAME rather than
// a type name. The intern table is append-only by design, so a program
// walking many directories would add an entry per directory and never
// release it.
std::string ofType;
```

This is the interesting failure mode of interning, and it is worth stating as a
general rule: **intern a closed vocabulary, never an open one.** A field that
can hold arbitrary runtime data — a path, a user string, a key — turns an
append-only table into an unbounded leak. Everything else in `Value` is drawn
from the program's own vocabulary of type names and is safe.

## Why they are separate types

`MName` borrows a `const std::string&` and caches its length and prefix. It is a
**view**, built at the top of a function and discarded at the bottom; it cannot
be stored.

`IStr` **owns** a handle into a global table. It can live in a struct, be copied
freely, and outlive the text it was built from.

They could in principle be unified — `IStr` could serve both roles — but a
method name is compared once per call and never stored, so paying the intern
lock for it would be strictly worse. Two types, two lifetimes, one idea.
