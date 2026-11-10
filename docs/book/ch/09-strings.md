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

Measured when the class was introduced, 20,000 operations on a 200 KB string:

| | `std::string` | `CowStr` |
|---|---:|---:|
| pass the string to a sub, no work done | 142 ms | 24 ms |
| `nqp::ordat` on a global | 79 ms | 11 ms |

## What it is

One word of bytes, in one of two forms that its last byte tells apart:

```cpp
// src/Value.h — abridged
class CowStr {
    static constexpr size_t kBytes = RAKUPP_BODY_HANDLES ? 8 : 16;
    static constexpr size_t kInline = kBytes - 1;
    static constexpr size_t kTag = kBytes - 1;   // says which form
    static constexpr unsigned char kBody = 0x80;
    alignas(8) char raw_[kBytes];

    unsigned char tag() const { return (unsigned char)raw_[kTag]; }
    bool isBody() const { return tag() == kBody; }
    size_t ilen() const { return kInline - tag(); }   // (inline only)

    void store(std::string x) {
        if (x.size() > kInline)
            setBody(makeRef<const StrBody>(std::move(x)));
        else setInline(x.data(), x.size());
    }
public:
    std::string_view str() const {
        return isBody() ? std::string_view(bodyRaw()->str())
                        : std::string_view(raw_, ilen());
    }
};
```

- **Inline**, a text of up to `kInline` bytes. The text is at the front and the
  bytes after it are zero. The last byte holds `kInline` minus the length, so
  the NUL of a text that fills the form is that byte, and every text is
  NUL-terminated. With the tail kept at zero, two inline texts are equal
  exactly when their bytes are, so comparing two short strings compares one
  word.
- **A body.** The first bytes are the address of a `StrBody` that holds one
  count, the rest are zero, and the last byte is the tag `0x80`. Copying is
  the bytes and one more count.

On 64-bit macOS and Linux the class is 8 bytes: a text of up to 7 bytes
inline, or the body's 32-bit handle in the first four (Chapter 8 has the
handles). Where handles are off it is 16 bytes, 15 inline, and the body's
address is a pointer. It is one source text for both widths.

A text longer than `kInline` is always a body. A shorter one is a body only
when something asked for one: `promote()`, for a `Buf` whose address is handed
to C, or a body with one owner that was rewritten in place and shrank. Nothing
in the bytes points at them, so a `CowStr` may be moved bitwise, which the
list container of Chapter 12 relies on.

Three decisions inside that deserve their own paragraphs.

### Promotion is eager

At construction, not lazily on first copy. Raku++ runs work in parallel, and a
lazy promotion would have to mutate the source from a `const` copy constructor
— two threads copying the same `Value` would race on it.

Eager promotion means **a `const CowStr` is never written to at all**, which is
what makes the type safe to share without a lock. That single property is what
lets everything else here be lock-free.

### The threshold was 64, then 23, and is now what fits

From 2026-08 to 2026-11 the class was two members side by side, a
`std::string` for short text and a shared pointer to a body for long text,
with a constant `kPromote` choosing between them. The relevant numbers were the small-buffer
capacities, measured:

| standard library | `sizeof(std::string)` | SSO capacity |
|---|---:|---:|
| libc++ (clang) | 24 | 22 |
| libstdc++ (g++ 14, 16) | 32 | 15 |

The original threshold was 64, chosen to sit above every mainstream capacity.
That left a band in which a copy was a couple of words and no allocation
happened either way, and it promoted only where copying was clearly the thing
being paid for. It was not a tuned number; it was a deliberately safe one.

It was also, by construction, wrong at one end. Between libc++'s 22 and the
threshold's 64 sat a band where the inline arm had already stopped being free:
a 30-byte string was a heap `std::string` that mallocs **on every copy**, while
a 70-byte one was a shared body that copies by refcount. A 33-byte string cost
more to pass around than a 68-byte one, for no reason but the constant.

So on 2026-09-02 the threshold moved to 23 — libc++'s boundary, the point where
the inline representation stops being free — and the policy inverted with it:
promote wherever `std::string` would allocate anyway, rather than wherever
sharing is obviously worth it.

The honest ledger for that change, measured as instruction counts at best of
five runs: mid-band strings copied twice **+19.8%**, built and read once
**+4.9%**, `textsplit` **+3.0%**, and one measured counter-case at **−5.6%**.
Grammar parsing, string comparison and string appending were all flat, because
their strings are not in the band. The change was kept because the shapes it
helps are commoner than the shape it hurts, not because the measurement was
emphatic — and the counter-case is the next section.

The constant is gone now. When the `std::string` left the class (2026-11-07)
the threshold became the length the inline form can hold: 15 bytes in a
16-byte `CowStr`, and 7 in the 8-byte one that followed on 2026-11-10. Each
step moved a band of short strings into bodies, and each was priced first.

A census of Roast counted the texts a `Value` wrote inline: 86% are 1 to 7
bytes, 11.5% are 8 to 15, and 2.6% are 16 to 22. The same census priced the
alternative of every string in a body, which would have kept the old read
interface: 7 to 17% more allocations over whole corpora, against half a
percent for the inline form.

- **At 15 bytes inline,** the 16-to-22 band became bodies. Over 44 benchmark
  and example programs that was 0.39% more allocations. `textsplit`, which
  keeps 20,000 strings in that band, made 6.25% more and still ran faster,
  91.3 ms against 100.3.
- **At 7 bytes inline,** the 8-to-15 band became bodies. The string programs
  the band should tax measured, against the 48-byte build:
  `textsplit` −0.4%, `sortby` −0.2%, `streq` +0.2%, `strcat` +1.5%, `regex`
  +1.7%. A body no longer comes from `malloc` at this step, so its
  allocations were not counted the way the earlier step's were.

### The counter-case: a mid-band string used as a hash key

This was measured at the threshold of 23, in the two-member layout, and has not
been measured again in the present one.

A promoted string cost **two** allocations, not one: `make_shared` fused the
control block with `StrBody`, but `StrBody::text` heap-allocates its own
buffer. An unpromoted mid-band string cost the single `std::string` malloc.
Copies are free afterwards, so promotion pays from roughly the second copy
onward.

A hash key never gets there. It is built, hashed, and copied *out* into the
hash's own key storage, and the `Value` is then dropped — one construction,
one read, no copy that a shared body could make cheap. Two hundred thousand
mid-band keys measured 5.6% more instructions after the threshold change, and
that is the whole shape of the trade written in one program.

The fix named then was `StrBody` storing its text inline, which would collapse
promotion to a single allocation. That is not what happened. `StrBody::text` is
still a `std::string`. What changed is the block: it has an intrusive count
and comes from a pool, `SlabPool` first and `BodyPool`'s region now, so making
a body is a free-list pop. A text short enough for the `std::string`'s own
small buffer then costs no `malloc` at all, and a longer one costs one, for
the characters.

### The body caches string properties

This is the part no general-purpose string type can do, and the real reason
`CowStr` exists rather than a third-party copy-on-write string.

```cpp
// src/Value.h — abridged
struct StrBody : RefCounted {
    std::string text;           // empty for a view, which reads `view`
    StrView* view = nullptr;    // owned; set for a range of a StrBuf
    mutable std::atomic<signed char> allAscii{-1};
    mutable std::atomic<signed char> crFree{-1};
    mutable std::atomic<signed char> nfc{-1};
    mutable std::atomic<long long>   nGraphemes{-1};
    mutable std::atomic<const std::vector<uint32_t>*> cpIndex{nullptr};
    mutable std::atomic<const std::vector<uint32_t>*> gIndex{nullptr};
};
```

Raku string positions are **grapheme** positions, and Raku++ stores UTF-8
bytes. So every indexing operation must first establish whether a byte index is
also a grapheme index. Establishing that by scanning is O(position) per
character examined — which is the *other* half of the quadratic, independent of
copying.

Because the promoted body is immutable, the answer can be computed once and
cached on it. Four cheap values cover the common questions:

- `allAscii`: every byte below 0x80, so a byte index is a codepoint index;
- `crFree`: no carriage return which, together with `allAscii`, means a byte
  index is a **grapheme** index — CR LF is the one ASCII sequence that clusters
  under UAX #29;
- `nfc`: the text is in NFC, so a join renormalizes only where two texts meet;
- `nGraphemes`: the answer to `.chars`.

They are reached through `cowAllAscii`, `cowByteIsGraphemeIndex` and
`cowGraphemeCount` in `BuiltinsShared.h`.

The caches need no lock. Two threads may each compute one, but the text is
immutable, so they compute the same answer and the store is idempotent. They
live on the body, so they exist only for body strings — which is exactly
the case that needs them; short strings are cheap to rescan.

One writer breaks the immutability on purpose: a body with a single owner is
rewritten in place, and `invalidate()` resets every cached value first.

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
`substr` are `const`-only; the mutation door for text is `edit(f)`, which
detaches explicitly and lends `f` a real `std::string&` for the call. What
C++11 outlawed was copy-on-write *hiding behind* an interface that promises
O(1) non-`const` element access. `CowStr` promises no such thing.

### The alternatives, and why each is not it

| | why not |
|---|---|
| `std::string_view` | non-owning; `Value` owns its string. It is what `str()` answers with |
| small-buffer optimisation | already the inline form, written by hand since the `std::string` member left |
| `shared_ptr<const std::string>` | what the long arm was; the count is in the body now, and the body carries the caches |
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
owning the class.

The threshold difference makes the same argument from the other end.
`fbstring` promotes at 255 because copy avoidance is all it buys. `CowStr`
makes a body past 7 bytes, for a type that gets something out of the body
besides sharing and whose inline form has to fit one word of a 32-byte struct.

## What it costs

Printed by a probe compiled against the headers on 2026-11-10 (arm64 macOS,
libc++):

| | bytes |
|---|---:|
| `std::string` (libc++) | 24 |
| `Ref<const StrBody>` | 8 |
| **`CowStr`** | **8**, and 16 where handles are off |
| `StrBody` | 72 |

The class was the sum of its two arms at first: a 24-byte `std::string` and a
16-byte `shared_ptr`, 40 bytes, of a `Value` that was then 392. It was 48 on
libstdc++, whose `std::string` is 32. An intrusive count made it 32, one form
in shared bytes made it 16, and a handle for the body made it 8. Those are a
quarter of today's `Value`, paid on every `Value` everywhere, most of which
are not strings: in Roast three quarters of all string copies were the empty
string of a `Value` that is not a `Str`.

`StrBody` is 72 bytes for a reason worth knowing, because it is the other
half of the string-scan story. Beside the cheap scanning flags it carries two
lazily built byte-offset tables — `cpIndex`, the byte offset of codepoint *i*,
and `gIndex`, the byte offset of grapheme *g*, each with an end sentinel. They
are what makes a positional operation on a non-ASCII string O(1) instead of a
rescan from the start, which is the non-ASCII half of the quadratic the ASCII
fast path had already closed. They are installed by compare-and-swap rather than
by the idempotent store the flags use: two threads may both build one, and the
loser deletes its own copy.

## A body can be a view

Three ways of building a string need the old text to survive unchanged while a
new one is made from it: `$k = $s; $s ~= "x"`, `$s = "x" ~ $s`, and
`$s = $s.substr(5)`. A flat string can only do that by copying all of it at
every step, so all three were quadratic.

A body may therefore hold a **view** instead of a text: a range of a shared,
growable `StrBuf`. Bytes inside a buffer's written region are never moved or
changed, so any number of views, on any number of threads, may read them. A
view whose end is the region's end may claim the free space after it with one
compare-and-swap and write there, and the same at the front. When the space
runs out, the view's bytes move to a buffer twice the size with room on both
sides.

Views are made only by the paths that build a `Str` and ask for one:
`appendText`, `prependText`, and the static `joinPrepend`, `joinAppend` and
`sub`. Plain `+=` never makes one, because a `Buf`'s storage is handed to C and
must stay one flat `std::string`. A reader that needs contiguous text gets it
from `StrBody::str()`, which flattens a view once and keeps the copy.

Measured when views landed (2026-10-06), 100,000 steps of each perf-guard
kernel: prepend 4.53 s to 0.01 s, shared append 0.56 s to 0.01 s, the `substr`
loop 1.61 s to 0.05 s. Setting `RAKUPP_APPEND=force` makes every body a view,
which is the debug mode that proves the readers.

## A literal keeps its `Value`

A string literal used to build a new `Value` from the AST's `std::string` on
every evaluation, and a literal too long for the inline form allocated a body
each time. The `StrLit` node now builds its `Value` once, into storage inside
the node, and every later evaluation copies it. A long literal's body is
therefore shared by every evaluation of it. That is sound because no writer can
reach it in place: the in-place writers of a `Str` check that the body has one
owner, and `mutInPlace` serves only `Buf` values.

A double-quoted string with nothing interpolated takes the same road. The
parser makes it an `InterpStr` whose one part is the `StrLit`, and the
evaluator answers that part's `Value` itself. Measured on 2026-11-07, a 36-byte
literal pushed onto an array 200,000 times by the interpreter went from 604,514
allocations to 4,512.

## Rules for working with it

The type forwarded about 1,100 read sites unchanged when it was introduced,
which was the point. The move to views as the read type (2026-11-06) broke 816
sites the compiler listed, of about 3,164 textual uses of `.s`; the rest still
compile through the forwarding members. The places where it does not behave
like a `std::string` are worth knowing.

- **`str()` answers a `std::string_view`,** and that is the only implicit
  conversion. The view is valid while the `CowStr` is unchanged *and where it
  is*: an inline text moves with its `Value`. `c_str()` and `data()` are
  NUL-terminated.
- **A copy is `copyText()`, not `std::string(s.str())`.** A string built from
  a view calls `memmove` for its bytes however short it is; `copyText()`
  copies a short one at a fixed length without a call.
- **`edit(f)` is the write door for text, and it detaches.** It lends `f` a
  `std::string&` for the call only. A body that other values share is copied
  into a new body first. A body with one owner is written where it is, which
  is what keeps `$s ~= …` in a loop linear. An inline text has no
  `std::string`, so `f` gets a copy and what it leaves is stored back.
- **`+=`, `pop_back`, `resize` and `erase` write an inline text in place** and
  keep its tail zero. An append that overflows the inline form moves the text
  into a body.
- **`mutInPlace()` is for a `Buf`.** It keeps the shared body, because a `Buf`
  has reference semantics and its storage may be bound into C.
- **Both converting constructors are `explicit` on purpose.** With an implicit
  `std::string` to `CowStr` conversion alongside the one from `CowStr` to a
  view, every `cond ? value.s : someString` became ambiguous in both
  directions.
- **Comparisons and `+` are spelled out by hand.** Two real reasons: no
  conversion is considered for a built-in operator when neither operand is a
  class type with a candidate, so `CowStr == CowStr` needs its own overload; and
  `std::string`'s comparisons are templates, and template deduction never
  considers a user-defined conversion, so `value.s == "Int"` needs a real
  candidate too. A macro generates the ordering overloads. `==` and `!=`
  against a string literal are templates over the array's length, so a literal
  of a different length is rejected by one integer compare and no `strlen` —
  the trick Chapter 10 describes for method names.
- **Do not add a non-`const` `operator[]`, `begin()` or `data()`.** That is
  exactly the interface that made copy-on-write non-conforming for
  `std::string`. Reach for `edit()`.
- **Do not cache `body()` across an assignment.** It is the raw `StrBody*`, and
  the count that owns it can be dropped.

## What was left on the table, and where it went

This section used to list two changes that were real, unmeasured, and not to be
done on faith. Both have since been measured.

- **A union instead of two side-by-side members.** Done, as design C of the
  `Value` size plan: one set of bytes holds either form, with hand-written
  copy, move and destroy. It was worth it once `Value`'s size did show up in a
  measurement, which Chapter 8 walks through.
- **One allocation instead of two** for a promoted string, by storing the
  count and the caches ahead of the characters. Half done: the count is in the
  body and the body comes from a pool, but the characters past the small
  buffer are still a `std::string`'s own allocation.

## Checking that it is working

The failure mode is silent: a change that puts a hot string back on the copying
path costs time and nothing else. Two cheap checks.

**Measure scaling, not speed.** Time the operation over inputs of *n*, 2*n*,
4*n*, 8*n*. Doubling per doubling is fine; quadrupling is the bug this class
exists to prevent. That is the test which found `findnotcclass` after four
other sites had already been fixed.

**Confirm a body is actually being made.** A string of 7 bytes or fewer has no
body and caches nothing — which is correct, but means a benchmark built from
very short strings proves nothing about either mechanism. The bar is low
enough now that most real text clears it; it was 64, and a good deal of
plausible-looking test data used to sit under it.
