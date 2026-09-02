# Raku++ — the `-O` optimizer

Raku++ runs a program three ways (see [BENCHMARKS.md](../status/BENCHMARKS.md) for the
speed picture):

- **interp** — tree-walk the AST (the default).
- **`--aot` / `--bundle`** — standalone binaries that still *tree-walk* an
  embedded program, so they run at interpreter speed.
- **`--exe`** — transpile the program to C++ and compile it to a native binary,
  with no interpreter inside. This is the only mode whose runtime performance
  differs, and the only mode the optimizer touches.

For how `--exe` codegen fits the pipeline see [ARCHITECTURE.md](ARCHITECTURE.md)
(§4); it emits C++ that calls the *same* runtime the interpreter uses (`Value`,
`applyArith`, …), documented in [RUNTIME.md](RUNTIME.md). [NATIVE.md](../guide/NATIVE.md)
compares compiled vs. interpreted on the example programs.

`--exe` (and its inspection twin `--cpp`, which prints the generated C++ instead
of compiling it) accept **`-O`**. This document is about what `-O` does.

Everything under `-O` is **semantics-preserving** — it changes *how* the compiled
program computes, never *what* it computes. It is opt-in and off by default.

```sh
rakupp --exe    prog.raku -o prog     # default: no optimizer
rakupp --exe -O prog.raku -o prog     # optimizer on
rakupp --cpp -O prog.raku             # print the optimized C++ to stdout
```

## What the generated code looks like without `-O`

By default the transpiler is faithful but generic: every value is a boxed
`Value`, operators in **value position** go through the runtime's string-keyed
dispatcher (`applyArith`), and every user-sub call packs its arguments into a
`ValueList` (a list built and torn down per call; since 2026-09-02 its block,
for the short lists a call actually builds, comes off a thread-local free list
rather than the allocator — see [RUNTIME.md](RUNTIME.md)). (Three
dispatch cuts apply even without `-O`, because they are plumbing rather than
speculation: comparisons in **conditions** use the inline `rtLtB`/`rtEqSB`-family
helpers, builtin calls go through pointers cached once at startup — see
[DISPATCH.md](DISPATCH.md) — and `applyArith` itself takes the operator as a
`const char*`, described below.)

```cpp
// sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }
static Value u_fib(ValueList __a) {
    Value v_sn = rtPos(__a, 0);
    return rtLtB(v_sn, Value::integer(2LL))     // condition: inline int compare (always on)
         ? v_sn
         : applyArith("+", u_fib(ValueList{applyArith("-", v_sn, Value::integer(1LL))}),
                           u_fib(ValueList{applyArith("-", v_sn, Value::integer(2LL))}));
}
```

For a hot recursive function this is two costs on every one of ~1.6M calls: a
`ValueList` `malloc`, and the value-position `applyArith` calls that dispatch on
the op *string* before touching the operands.

## The passes

### 1. Direct-arity calls (skip the per-call `ValueList`)

A sub whose signature is entirely **plain required positional scalars** (no
named, slurpy, optional, defaulted, or destructured params) gets a direct-`Value`
overload — its parameters *are* the C++ arguments — plus a boxed adapter so any
unusual call site still resolves. C++ overload resolution picks the right one.

```cpp
static Value u_fib(Value v_sn) { … }                                 // fast overload
static Value u_fib(ValueList __a) { return u_fib(rtPos(__a, 0)); }   // adapter
```

A call site takes the fast overload when it passes exactly the right number of
**plain positional** arguments (no `:name(…)` pairs, no `|@slurp`); otherwise it
calls through the adapter unchanged. Multi subs, indirect calls (`&fib`), and
method calls are untouched.

The same idea covers **37 named builtins** — `abs chr ord`, `say print put
note` (1-arg forms), the numeric family
(`sign floor ceiling round truncate sqrt exp log log10 log2 is-prime`), the
string family (`uc lc chars flip trim chomp chop`), and the trig/hyperbolic
functions: each has a real C++ function (`rtBAbs`, `rtBUc`, `rtBSin`, …), and a
single-plain-arg call site emits the direct call — no `ValueList`, no
`std::function`, and for `abs`/`sign` an inline plain-Int hot path at the call
site. An `abs`-heavy loop went 1112.9 → 198.3 ms (5.6×), a `sqrt`+`sin`+`floor`
loop 731.5 → 363.6 ms (2.0×), a buffered `say` loop 124.9 → 69.6 ms (1.8×);
details and the general recipe in
[DISPATCH.md](DISPATCH.md).

### 2. Inline int64 arithmetic (skip the string dispatch and boxing)

For the common operators the codegen emits inline helpers (declared `inline` in
`Interpreter.h`) instead of `applyArith("…", …)`:

| ops | helper | fast path |
|---|---|---|
| `+` `-` `*` | `rtAdd`/`rtSub`/`rtMul` | native `int64`, overflow → bignum |
| `**` | `rtPow` | integer power by squaring, overflow → bignum |
| `<` `<=` `>` `>=` `==` `!=` | `rtLt`/… | `int64` compare |
| `%` `%%` `div` | `rtMod`/`rtDivides`/`rtDiv` | `int64` mod / divisibility / floor division |
| `~` | `rtConcat` | direct `std::string` concat when both are `Str` |
| `eq` `ne` `lt` `gt` `le` `ge` | `rtEqS`/… | byte-wise compare when both are **plain** `Str`s (no Version/IO/Buf tag, no enum identity) — tagged values fall back to the full chain. These matter more than the int ops: string comparisons sit *late* in `applyArith`'s dispatch chain (~118 ns vs ~10 ns direct — see [DISPATCH.md](DISPATCH.md)) |

Each inlines its fast case and falls back to `applyArith` for everything else:

```cpp
inline Value rtAdd(const Value& l, const Value& r) {
    long long z;
    if (rtBothInt(l, r) && !__builtin_add_overflow(l.i, r.i, &z)) return Value::integer(z);
    return applyArith("+", l, r);   // Rats, bignums, mixed types, coercions
}
```

`rtBothInt` is `l.t == VT::Int && r.t == VT::Int && !l.big && !r.big`. Integer
overflow is detected with `__builtin_*_overflow` and promotes to bignum, exactly
as `applyArith` does. This pass covers both binary operators and compound
assignment (`$s += …`).

With both passes (plus pass 3's condition lane), `fib` transpiles to:

```cpp
static Value u_fib(Value v_sn) {
    return ([&]() -> bool {                        // pass-3 condition lane:
        do { if (!(rtIntBox(v_sn))) break;         //  guard the box…
             return (v_sn.i < 2LL); } while (0);   //  …compare as raw int64
        return rtLtB(v_sn, Value::integer(2LL));   //  guard failed: boxed compare
    }())
         ? v_sn
         : rtAdd(u_fib(rtSub(v_sn, Value::integer(1LL))),
                 u_fib(rtSub(v_sn, Value::integer(2LL))));
}
```

No heap allocation, no string dispatch — pure inlinable code. Under a real C++
optimizer this collapses to tight native-int recursion.

### 3. Guarded native-int expression lanes (skip the `Value` box entirely)

Passes 1–2 still build a boxed `Value` for every intermediate result — `$sum +=
$_ * 2 - 1` constructs four `Value`s per evaluation even with `rtAdd`/`rtSub`/
`rtMul`. Pass 3 removes them: a straight-line integer expression whose leaves
are **int literals and plain scalar variables** is computed in raw `int64`.
Each `Value` leaf is tag-guarded at runtime (`rtIntBox`: an `Int`, not a
bignum), each op is overflow-checked, and the result is stored **into the
target's existing box** (`.i`) with no `Value` construction at all. Any guard,
overflow, or domain failure falls through to the untouched boxed emission —
lane leaves are pure (literals and variable reads), so re-evaluating them on
the slow path is safe.

```cpp
// $sum += $_ * 2 - 1        (inside a native range loop; __i3 is the counter)
{ bool __lok = false; do { // -O int lane
    if (!(rtIntBox(v__t0) && rtIntSlot(v_ssum))) break;
    long long __t1; if (rakupp::mul_ovf(v__t0.i, 2LL, &__t1)) break;
    long long __t2; if (rakupp::sub_ovf(__t1, 1LL, &__t2)) break;
    long long __t3; if (rakupp::add_ovf(v_ssum.i, __t2, &__t3)) break;
    v_ssum.i = __t3; __lok = true;
} while (0);
if (!__lok) { v_ssum = rtAdd(v_ssum, rtSub(rtMul(v__t0, Value::integer(2LL)), Value::integer(1LL))); } }
```

The lane applies to:

- **statement-position assignment** to a plain scalar — `$x = <int expr>` and
  `$x += -= *= %= <int expr>` (in-place stores additionally require
  `rtIntSlot`: not an enum-typed box, whose stringification is its name);
- **statement-position `++`/`--`** on a plain scalar;
- **conditions** (`if`/`while`/`until`/ternary) that are int comparisons or
  `%%` — the whole comparison evaluates unboxed inside a `bool` lambda.

Ops covered: `+ - *` (overflow-checked, promoting via the boxed path), unary
minus, `%` (floored, mirroring `rtMod`'s int case bit-for-bit; a zero divisor
falls to the boxed path, which throws), `%%`, and the six comparisons.
Everything else — `Num`s, strings, `Rat`s, bignums, array elements, method
calls — fails the lane at compile time or its guards at runtime and takes the
boxed route unchanged.

## A related default: in-place `~=` (not gated by `-O`)

`$s ~= …` naively rebuilds the whole string each step — `$s = $s ~ "x"` copies
the growing buffer every iteration, so *n* appends do O(n²) work. That is a
correctness wart, not a missing optimization (the interpreter and Rakudo both
build strings in O(n)), so both the tree-walker and the `--exe` codegen now
append into the accumulator's existing buffer **by default** via `rtCatAssign`:

```cpp
inline void rtCatAssign(Value& l, const Value& r) {
    if (l.t == VT::Str && r.t == VT::Str) { l.s += r.s; return; }  // O(1) amortized
    l = applyArith("~", l, r);                                     // anything else
}
```

It applies to scalar and element (`@a[i] ~=`, `%h{k} ~=`) targets; non-`Str`
operands fall back to `applyArith`. The interpreter pairs it with *sink context*:
a loop body's value is discarded, so the assignment doesn't copy its (growing)
result either. Because this is now the default in every mode, `-O` adds nothing
on top of it — `strcat` looks flat between `--exe` and `--exe -O`.

**Native-bool conditions** are the same idea. An `if`/`while`/ternary condition
that is a comparison (`$n < 2`) used to compile to `RT.boolify(rtLt(…))` — build a
`Bool` `Value`, then read it back. The codegen now emits a `bool`-returning helper
(`rtLtB`/`rtLeB`/…, joined by the string forms `rtEqSB`/`rtLtSB`/… — see
[DISPATCH.md](DISPATCH.md)) straight into the condition, skipping the
round-trip. It is default (not `-O`-gated); on `fib`, whose ternary runs 1.6M
times, it took `--exe` from 186 → 165 ms and `--exe -O` from 84 → 66 ms; on
`streq` the string form is most of a 15× cut in plain `--exe`.

## Another default: `.sort($key)` extracts the key once per element

`.sort` takes either a **2-ary comparator** (`{ $^a <=> $^b }`) or a **1-ary key
extractor** (`*.chars`). They are different contracts, and the difference is
asymptotic: a comparator is *supposed* to run per comparison, but a key extractor
runs **once per element**, and the sort then compares the extracted keys. Raku++
used to call the 1-ary block inside the comparator, evaluating the key O(n log n)
times where the contract asks for O(n).

```cpp
// before — the key is recomputed for both sides of every comparison
std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
    Value kx = callCallable(blk, {items[x]}), ky = callCallable(blk, {items[y]});
    return valueCmp(kx, ky) < 0;
});

// after — a Schwartzian transform: n calls, then compare the keys
ValueList keys(items.size());
for (size_t i = 0; i < items.size(); i++) keys[i] = callCallable(blk, {items[i]});
std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) {
    return valueCmp(keys[x], keys[y]) < 0;
});
```

Like in-place `~=` above, this is a **correctness wart rather than a missing
optimization** — Rakudo has always evaluated the key once per element — so it is
the default in every mode, not gated by `-O`.

The cost was only visible when the key itself was expensive. Sorting codepoints
by the length of their Unicode name (`.uniname` is a table lookup) on this
machine, `say (0..N).sort(*.uniname.chars)[*-1]`:

| N | before | after | Rakudo |
|---|---:|---:|---:|
| `0x1FFF` (8 K) | 5.70 s | 0.29 s | 0.34 s |
| `0xFFFF` (64 K) | 18.09 s | 0.68 s | 0.63 s |
| `0x1FFFF` (128 K) | still running at 95 s | 1.33 s | 1.09 s |

The ratio grows with `log n`, which is the shape the change predicts. The last
row is the example as the documentation writes it; before the fix it was the
slowest thing in the conformance corpus by two orders of magnitude.

A 2-ary comparator is unaffected — it keeps calling per comparison, because that
is what a comparator means, so it still does O(n log n) key evaluations by
construction. `(0..0xFFF).sort({ $^a.uniname.chars <=> $^b.uniname.chars })` takes
1.78 s against Rakudo's 0.27 s, and profiling that gap turns up something with
nothing to do with sorting:

> **`std::stod` threw a C++ exception on every method call.** See below.

## A third default: never numify a string by throwing

Chasing the comparator gap above turned up something with nothing to do with
sorting. `for ^300000 { 'ab'.METHOD }` minus the empty-loop baseline used to cost
about **7 µs per method call**, against roughly 0.17 µs for the same loop under
Rakudo — while Raku++'s bare loop is *faster* than Rakudo's (0.10 s vs 0.41 s for
300 K iterations). So it was the call, not the interpreter.

Two plausible causes were ruled out by measurement first. Not the length of the
`m == "…"` dispatch ladder in `methodCallInner`: `.chars` sits 177 comparisons in
and `.uc` 812, and they cost the same. Not the method bodies: `.uniname` is a
binary search plus a hash lookup and cost what `.chars` cost.

A breakpoint on `__cxa_throw` gave the answer — **one throw per method call**, and
none at all for an empty loop:

```
__cxa_throw
  std::__throw_invalid_argument
    std::stod(std::string const&, unsigned long*)
      rakupp::Value::toNum() const
        rakupp::Interpreter::methodCallInner(…)
```

`Value::toNum()` numified a `Str` with `std::stod`, which **throws
`std::invalid_argument`** when the string does not start with a number. The
result was caught and turned into `0.0` — the right answer, arrived at by
raising, unwinding and catching a C++ exception. Speculatively numifying a string
that turns out not to be one is completely routine in an interpreter (the
invocant of every `"ab".method` call reaches here), so the language's slowest
control-transfer mechanism sat on its hottest path.

`std::strtod` reports the same failure by leaving its end pointer at the start of
the input, and costs nothing:

```cpp
static double strToNumOr0(const std::string& s) {
    if (s.empty()) return 0.0;
    const char* p = s.c_str(); char* end = nullptr;
    double d = std::strtod(p, &end);
    return end == p ? 0.0 : d;   // stod would have thrown here
}
```

Measured on this machine, 300 K iterations:

| | before | after | Rakudo |
|---|---:|---:|---:|
| `'ab'.chars` | 2.23 s | **0.73 s** | 0.34 s |
| `'ab'.uniname` | 2.51 s | **0.47 s** | 0.36 s |
| `"hello world".uc.chars`, 200 K | 2.9 s | **0.94 s** | 0.27 s |
| `(0..0xFFF).sort({…uniname…})` | 1.78 s | **0.68 s** | 0.28 s |

Roast is unchanged across the switch, which is the point: `strtod` and a caught
`stod` agree on every input that reaches this path.

Two smaller per-call costs went with it: `methodCallInner` called
`std::getenv("RAKUPP_TRACE")` on entry — once per method call, about 10% on its
own — and is now read once into a `static`.

### …and then the dispatch ladder, for a reason that is not its length

With the throw gone, a fresh profile of `for ^3000000 { "ab".chars }` put **60% of
the time in string comparison**: `_platform_strlen` 19%, an out-of-line
`std::operator==(const string&, const char*)` 19%, `DYLD-STUB$$strlen` 14%,
`memcmp` 8%.

That looks like the `m == "…"` ladder, and earlier measurement had seemed to
exonerate it — `.chars` sits 177 comparisons into the file and `.uc` 812, yet they
cost the same. Both facts are true. Position *in the file* is not the number of
comparisons *executed*, because the ladder is guarded by invocant-type tests: a
`Str` invocant only ever runs the `Str` arms, and `.chars`, `.uc` and `.uniname`
are all in that same group.

The real problem was that `strlen` was being called **at run time on a string
literal**. Clang normally folds that away, and in a small function it does — a
1,700-branch toy reproduction compiles to zero `strlen` calls. `methodCallInner`
is ~8,900 lines with ~1,640 comparisons, which is far past the optimizer's
inlining budget: `std::operator==` stays out of line, and out of line it cannot
see the literal, so it measures it with `strlen` on every call.

The fix does not depend on the optimizer's mood. Wrap the name in a type whose
`operator==` takes the literal **by reference to array**, so its length is part of
the template argument:

```cpp
struct MName {
    const std::string& s;
    template <std::size_t N> bool operator==(const char (&lit)[N]) const {
        return s.size() == N - 1 && std::memcmp(s.data(), lit, N - 1) == 0;
    }
    // …plus the reversed forms, `+`, `<<`, and the handful of std::string
    // members the function uses, so all ~1,640 sites compile unchanged
};
```

`const MName m{mName};` at the top of the function, and every `m == "chars"` site
is now a size check (which rejects nearly every candidate outright) followed by an
inlined `memcmp`. Four compile errors, all of them stream or concatenation
overloads. Worth about 30%:

Two details earn their keep. The comparison is `__attribute__((always_inline))`:
left to itself the optimizer emits `MName::operator==<N>` **out of line** in this
function too, and an out-of-line call costs more than the comparison it performs.
And the first eight bytes of the name are packed into a `uint64_t` at
construction, so any name of eight characters or fewer — which is most of them —
compares as a single integer against a literal packed at compile time, with no
`memcmp` at all.

| 1 M iterations | before | ladder wrapper | + always_inline & packing |
|---|---:|---:|---:|
| `'ab'.chars` | 1.63 s | 1.19 s | **1.17 s** |
| `'ab'.uc` | 1.78 s | 1.15 s | **0.95 s** |
| `'ab'.uniname` | 1.61 s | 1.08 s | **0.71 s** |

(`.chars` barely moves because it is reached early: few comparisons run before it.
`.uniname` is deep in the `Str` arm, so it gains the most.)

### Where that leaves method dispatch

Taken together the two fixes are about **7× on a method call** — `'ab'.chars`
× 300 K went from 2.47 s to 0.36 s, against Rakudo's 0.11 s. From roughly 20×
slower than Rakudo per call to roughly 3×.

A profile of `for ^6000000 { "ab".uc }` now looks completely different from where
this started:

| | share |
|---|---:|
| heap allocate / free | 31% |
| `Value` copy / destroy | 11% |
| **method-name comparison** | **8.5%** |
| `methodCallInner` body | 6.1% |

Name comparison went from 60% to 8.5%, which retires it as a target. **A dispatch
table would only be chasing that 8.5%** — and it is not a drop-in, because the
ladder is not a pure dispatch on the name: arms are guarded by invocant type and
argument shape, and later generic arms deliberately catch what earlier specific
ones decline. Turning ~1,640 of those into map entries means giving each one its
own guard and preserving the fall-through order between them: a large, risky
rewrite for a single-digit percentage.

The 42% now sitting in allocation and `Value` churn is the real remaining target,
and it is the by-value question from earlier: both `methodCall` and
`methodCallInner` take their invocant and argument list **by value**, and a
`Value` carries ten `shared_ptr` members plus four `std::string`s, so every call
copies those and heap-allocates a `ValueList`. Switching both to `const&` is
mechanically small — 18 compile errors, each either "make a local copy here" or
"let this helper take const&" — but it trades away the accidental safety copying
provides against a callee mutating the container its own invocant lives in, so it
wants an aliasing audit of the 178 call sites rather than a quick pass.

## A fourth default: `applyArith` reads the operator without building a string

`applyArith`'s parameter is a `const std::string&`, and non-`-O` codegen calls it
with a literal: `applyArith("+", …)`. Every such call therefore **constructed a
`std::string`** — a short one, so no allocation, but still an object built and
destroyed — before the dispatcher could look at its first byte, and the first
thing it looks at is whether the first byte is `+`.

An overload taking the operator as it is (`Interpreter.h`) answers the small-Int
case the string version answers first anyway, and hands everything else to that
same function, so the result is identical either way:

```cpp
inline Value applyArith(const char* op, const Value& l, const Value& r) {
    if (rtBothInt(l, r) && op[0] != '\0' && op[1] == '\0') {
        long long a = l.i, b = r.i, z;
        switch (op[0]) {
            case '+': if (!rakupp::add_ovf(a, b, &z)) return Value::integer(z); break;
            /* -  *  <  > … */
        }
    }
    return applyArith(std::string(op), l, r);   // unchanged for everything else
}
```

`op` is a literal at every call site, so the switch folds to one comparison and
nothing survives into the binary but the taken branch. Like the two cuts above
it is plumbing rather than speculation, so it is on in every mode; what `-O`
adds on top is emitting `rtAdd` directly and skipping the call altogether.

On this machine (Apple M1, best of 9): compiled `loopsum` 16.5 → 15.1 ms,
compiled `fib` 95.5 → 92.5 ms. The interpreter reaches `applyArith` from the AST
with a `std::string` already in hand, so it is unaffected.

## A fifth default: `.sort` on native Ints orders a flat key array

The generic `.sort` decides its order by calling `valueCmp` from inside
`std::stable_sort`, and its comparator does two things per comparison that are
both worse than they look: an out-of-line call, and two **random probes** into
the element array. `Value` is about a hundred bytes, so sorting 50 000 of them
walks a 5 MB working set in shuffled order. On compiled `sortnums` —
`(1 .. 50_000).map({ … }).sort` — the comparator was **35% of the run**.

When every element is a native (non-bignum) `Int`, the whole order is decided by
one `int64` apiece. So: check that in one linear pass, pull the keys into a flat
array of `(key, index)` pairs, and sort *that* with an inlined compare. The
working set drops from `sizeof(Value)` per element to sixteen bytes, and the
index tiebreak is what keeps a plain `std::sort` stable. Anything the scan does
not describe — a mixed list, a bignum, a list too long for a 32-bit index —
falls through to the `valueCmp` path untouched.

The same specialisation applies to the *keys* of the 1-ary key-extractor path
above, which is how `.sort(*.chars)` reaches it.

Compiled `sortnums` 26.4 → 22.6 ms on this machine; interpreted 41.7 → 38.5 ms.

## A sixth default: a bignum times one limb keeps its carry in a register

`BigInt` stores magnitudes base 1e9, and multiplication was one schoolbook loop
for every shape. That loop routes each limb's carry through `r.mag[i+1]` — a
store the next iteration must load back — and needs a second inner pass per limb
to place it, so a big-by-small product ran at two iterations and two
store-to-load round trips per limb. A **running product** is entirely that shape:
`$f *= $_`, factorials, radix scaling.

Two changes, both inside `BigInt::operator*`:

1. When one side is a single limb, take a dedicated one-pass loop that keeps the
   carry in a register.
2. In that loop, split the limb product **before** folding the carry in.
   `(src[i]*m + carry) / BASE` puts a division on the carry chain, and division
   by 1e9 is a multiply-high plus a shift — five-odd cycles every limb must wait
   for. `src[i]*m` does not depend on the carry, so its own split runs ahead of
   the chain; folding the carry into the low half is then an add and a
   conditional subtract, and the chain is three cycles.

```cpp
uint32_t carry = 0;
for (std::size_t i = 0; i < n; i++) {
    uint64_t p  = (uint64_t)src[i] * m;
    uint32_t ph = (uint32_t)(p / BigInt::BASE);          // off the carry chain
    uint32_t pl = (uint32_t)(p - (uint64_t)ph * BigInt::BASE);
    uint32_t low = pl + carry;                           // both < BASE, so < 2^32
    if (low >= BigInt::BASE) { low -= BigInt::BASE; ph++; }
    dst[i] = low;
    carry  = ph;
}
```

`bigint` (`$f *= $_ for 1 .. 5000`, then `.chars`) is 90% inside this loop by
profile, and it moved on this machine from 45.2 to 11.4 ms compiled and 60.4 to
12.6 ms interpreted — a 4.0× and 4.8×. It is a runtime change, not a codegen
one, so `--exe`, `-O` and the interpreter all get it.

## A seventh default: eight carry chains, and the accumulator multiplied in place

That loop got `bigint` level with mutsu, and stopping there left two things on
the table — one in the loop and one around it.

**In the loop: the carry chain was the whole cost.** Every limb's carry feeds
the next, so however wide the core is, the loop can only retire one limb per
round trip through that dependency. Measured, about five cycles a limb where the
instruction count says one and a half. The fix is to stop having one chain: cut
the magnitude into eight contiguous segments, give each its own carry starting at
zero, and interleave their steps in a single loop body. The chains are
independent, so they issue in parallel and the loop becomes throughput-bound.

What that owes afterwards is seven carries — segment *j*'s carry-out belongs at
the first limb of segment *j+1*, which was computed as if nothing came in from
below. Paying it back is one add with a ripple that stops at the first limb which
does not overflow, and the payments commute, so the order does not matter.

Once no chain's latency is on the critical path, the *shape* of the step inverts.
The split above exists to keep the division off the carry chain, and it costs
twelve instructions a limb to do it. With eight chains there is nothing to keep
off, so the step folds the carry in first and pays one division — six
instructions: load, `umaddl`, `umulh`, shift, `msub`, store. All three forms
measured in one harness on the factorial kernel at K=8: fold-first **2.46 ms**,
a reciprocal-of-`m` form 2.80 (`M = ceil(2^64·m/1e9)`, two multiply-highs instead
of the divide — the same three multiply-class ops, which is the real floor here,
and four more scalar ones), the split form above 3.48.

```cpp
static inline void mulStep(uint32_t* d, const uint32_t* s, std::size_t i,
                           uint32_t m, uint32_t& carry) {
    uint64_t p = (uint64_t)s[i] * m + carry;
    uint64_t q = p / BigInt::BASE;              // a multiply-high and a shift
    d[i] = (uint32_t)(p - q * BigInt::BASE);    // one fused multiply-subtract
    carry = (uint32_t)q;
}
```

Eight is measured, not chosen: 4 and 6 are within 3%, 2 is 1.5× worse, 16 gives
the fold-back more segments than the loop saves. Below 32 limbs a single chain
wins outright and that is what runs.

**Around the loop: three copies of the magnitude per step, for one multiply.**
`$f *= $_` reached `applyArith`, which copied the accumulator in by value
(`toBig()` returns a `BigInt`), built a new magnitude for the product, and copied
*that* into the result box (`make_shared<BigInt>(const BigInt&)`). Three O(limbs)
passes around one O(limbs) multiply, and a running product is thousands of limbs
long by the end.

Two of those go away with a reference (`toBigRef`) and a move overload
(`Value::bigint(BigInt&&)`). The third needs the destination and the left operand
to be named once rather than twice, which is what `applyArithInto` is: the
compound-assign form of `applyArith`, emitted by the code generator in place of
`lhs = applyArith(op, lhs, rhs)` and called by the interpreter's op= paths. When
the box provably owns its magnitude alone it multiplies over it and allocates
nothing.

*Provably* is two conditions, not one, and each catches a shape the other misses.
`ValueExt` is copy-on-write, so a plain copy of a `Value` shares the cold block
and leaves the *magnitude's* use count at one while two Values plainly reach it.
And a `Range` built over a big endpoint splices the same `shared_ptr<BigInt>`
into a *different* cold block, so the block can be unshared while the magnitude is
not. Both counts must be one. The rest of the guard is the fields that
`dst = Value::bigint(...)` would have reset and an in-place write would instead
preserve — an enum identity, a native width, a readonly binding, an itemized tag.
It also refuses outright while worker threads are live: `dst = applyArith(...)`
already races there, but growing a magnitude *reallocates*, so a racing reader
that has already loaded `mag.data()` reads freed memory rather than a
stale-but-valid limb.

Counted with a `malloc` shim over the whole benchmark, against a `+=` control
that does the same loop without the bignum:

| | allocations | bytes copied |
|---|---:|---:|
| before | 32,163 | 34,019,701 |
| after | **2,297** (control 2,223) | **131,852** (control 59,986) |
| after, `--exe -O` | **2,057** | **119,998** |

Seventy-four allocations for 4,966 bignum steps — one `std::vector` growing
geometrically — and 33.9 MB of copying gone.

Together the two passes take `bigint` from 13.0 ms to **7.4** interpreted and
11.1 to **6.2** compiled, measured through `tools/run-bench.raku` against a
purpose-built binary of the commit before them, in one interleaved sitting at
load average 2.3. mutsu reads 11.2 in the same sitting. Five other kernels were
measured as a control and all landed within ±2%.

**What this does not do is make the general n×n product fast.** That is still a
plain schoolbook loop, and `num-bigint`'s base-2^64 limbs are measured about 10×
ahead of our base-1e9 ones on it. Base 1e9 is not an accident: it makes decimal
output O(n), where every power-of-two radix makes it O(n²) — measured, a
16,326-digit number converts in 0.087 ms from base 1e9 and 0.73 ms from base 2^64
with a divide-and-conquer split over Knuth algorithm D, rising to 1.3 ms against
129 ms at 261,211 digits. A base change would also silently alter Rat→Num
rounding at `Value.cpp`'s `dblExact`, whose `mag.size() <= 1` means "below 1e9,
hence exact in a double" and would come to mean "below 2^64". If base 2^64 is
ever worth it, the reason is that 10× on general multiplication, and the
divide-and-conquer conversion stack has to be built first so `.Str` never
regresses. Base 1e18 was measured too and is the worst of the three: it needs the
same rewrite for 1.16× on this loop, because dividing a 128-bit product by 1e18
exactly costs three multiplies where base 2^64 costs none.

## Forwarding the C++ optimization level

`--exe` compiles the generated C++ at **`-O2`** by default. A level on the `-O`
flag is passed straight through to the C++ compiler:

| flag | codegen passes | C++ compile |
|---|---|---|
| *(none)* | off | `-O2` |
| `-O` | on | `-O2` |
| `-O3` / `-Os` / `-Ofast` / `-O0` / … | on | that level |

```sh
rakupp --exe -O3    prog.raku      # codegen opt + cc -O3
rakupp --exe -Ofast prog.raku      # codegen opt + cc -Ofast
```

**The int passes only pay off *with* C++ inlining.** The `rt*` helpers and the
direct-arity split are wins because the C++ compiler inlines them. At `-O0` they
become real function calls with no fast path, so `-O0` is *slower* than the
default — it's for inspecting/debugging the generated C++, not for speed.

### Speed or size?

`-O` is a **speed** switch, and the C++ level is not a size lever: the binary
is dominated by the statically linked runtime (`librakupp_rt.a`, mostly the
Unicode tables), not by the program's own generated code. Measured (fib /
mandel, this machine):

| | binary | fib time |
|---|---:|---:|
| `--exe` (default `-O2`) | 6,300 KB | 166 ms |
| `--exe -O`  | 6,300 KB | **47 ms** |
| `--exe -Os` | 6,307 KB | 72 ms |
| `--exe -O3` | 6,300 KB | 47 ms |
| `--exe -O` + `strip` | **5,380 KB** | 47 ms |

`-Os` shrinks nothing that matters (it can even come out a few KB *larger*)
and costs 20–50% of the lane speed-up, so there is no size/speed trade to
make at this flag — use `-O` for speed and **`strip`** on the output (~15%
smaller) if size matters. A genuinely smaller binary would need a slimmed
runtime build (e.g. without the full Unicode tables) — a build-system project,
not a codegen flag.

## Measured impact

`--exe`, best of 6 runs after a discarded warm-up (startup-inclusive, as in
[BENCHMARKS.md](../status/BENCHMARKS.md)); measured 2026-07-17 on a lightly loaded machine.

| Benchmark | `--exe` | `--exe -O` | speed-up | what `-O` reached |
|---|---:|---:|---:|---|
| fib      | 166.5 ms | **47.0 ms** | 3.5× | direct-arity calls + int-lane condition |
| loopsum  | 27.1 ms  | **8.6 ms**  | 3.2× | `+=` lane over the native counter |
| streq    | 46.5 ms  | **17.5 ms** | 2.7× | int-lane counters atop the inline `eq`/`lt` |
| arrayops | 102.0 ms | **77.8 ms** | 1.3× | map/grep-body boxing |
| regex    | 61.9 ms  | 60.3 ms | 1.0× | (regex engine dominates) |
| hash     | 15.3 ms  | 14.9 ms | 1.0× | (hash slots, not scalars) |
| sortnums | 49.9 ms  | 48.6 ms | 1.0× | (`.sort` dominates) |
| bigint   | 29.2 ms  | 29.2 ms | 1.0× | (`BigInt` multiply) |
| strcat   | 3.9 ms   | 4.8 ms  | 0.8× | (`~=` already O(n) by default) |

These `--exe` baselines are much lower than they once were: the runtime's
`applyArith` now hot-paths `Int`/`Int` `+ - * < <= > >= == != %` with a char
switch instead of a chain of `op == "…"` string compares, and `--exe` (which
links that runtime) inherits it; plain `--exe` also emits inline string
comparisons in *conditions* and calls builtins through pointers cached at
startup (see [DISPATCH.md](DISPATCH.md) — that is what makes the
`streq` baseline low). So `-O`'s remaining edge is the boxing/allocation it
removes *entirely* — the per-call `ValueList` (`fib` direct calls), and with
pass 3 every `Value` temporary in laneable int statements and conditions
(`loopsum`, and the showcase kernels below).

Every kernel here is already ahead of Rakudo at plain `--exe` (fib included, now
that the runtime hot-paths integer arithmetic). Where the time is **inside a
runtime method** — the regex engine, `BigInt` multiply, `.sort` — `-O` can't reach
it, so those are unmoved. `fib` is the standout: a tiny body called millions of
times, where removing the per-call `ValueList` allocation still halves the time.

## Correctness

`-O` is validated to produce output byte-for-byte identical to the interpreter:

- all benchmark programs match with `-O` on;
- every deterministic example in `examples/` compiles with `--exe -O` and
  matches its golden output in `t/expected/`;
- the arithmetic fast-path fallbacks are checked directly — int64 overflow →
  bignum, `Int`/`Num` mixes, string coercion (`"3" + 4`), `<=>` (left on the
  general path), sorting, and `+=` at the int64 boundary;
- the lane fallbacks likewise: `+=`/`++`/`*=` crossing int64 promote to bignum,
  floored `%` with negative operands, `%= 0`, and comparisons whose
  intermediate overflows all match the interpreter exactly.

The design leans on the fallback: anything the fast path doesn't recognize (a
`Rat`, a bignum operand, a non-`Int` type, a named/slurpy call) routes to the
same runtime code the non-`-O` build uses.

## The value model: the box stays, only its per-op cost goes

A common question about `--exe`: does a native-compiled `my int $x` become a
bare C++ `long long`? **No.** The generated code keeps one uniform
representation — everything is a `Value`, at every optimization level. A
`Value` is a tagged union carrying the int64 inline in its `.i` field, next to
slots for `Num`/`Complex`/`Str`/`Array`/`Hash`/`BigInt`/`Rat` and their
`shared_ptr`s; on this build **`sizeof(Value)` is 376 bytes** (versus 8 for a
`long long`). `my int $s` compiles to `Value v_ss`, not `long long s`.

What the passes remove is per-*operation* overhead, not the box:

- **Default `--exe`:** `$s = $s + $i` is `v_ss = rtAdd(v_ss, v_si)` — a runtime
  dispatch that also **constructs a fresh 376-byte `Value`** for the result.
- **`-O` (pass 3):** the arithmetic runs as raw `int64` in registers and the
  result is written **into `v_ss`'s existing `.i` slot in place** — no `Value`
  is constructed per operation (this is the "zero boxing" behind `intsum`'s
  7.9×). But `v_ss` is *still* a 376-byte `Value`; the storage is unchanged, and
  a guard miss (non-int operand, overflow to bignum) falls back to the boxed
  path.

So `-O` makes hot integer *work* native without allocation, but it does **not**
shrink the variables, and loops still copy full `Value`s (e.g. the loop topic
each iteration — cheap when the `shared_ptr`s are null, but 376 bytes moved).

Why keep the box? Because the transpile is uniform: any expression must be able
to flow anywhere — into a runtime function, an array element, `say()`, an
untyped assignment — and Raku's `my int` is readable in `Any` context while
non-native `Int` can hold `Nil` or promote to bignum. Emitting a bare
`long long` local is only sound once analysis proves the variable never escapes
into a `Value` context; that's the **native int locals** pass below, not a small
tweak.

## Limits and what's next

`-O` is deliberately conservative — it removes per-operation overhead without
changing the value model (see the section just above). Pass 3 delivered the
first slice of **leaving the `Value` box entirely** for statements and
conditions; the remaining levers, in rough order of expected payoff:

- **value-position lanes** — laneable int expressions inside larger
  expressions (call arguments, list elements) still box;
- **`Num` lanes** — the same trick for `double` arithmetic (no overflow
  checks needed, just tag guards) — measuring stick: `nummath`;
- **array-element lanes** — `@a[$i]` reads/writes inside the lane (a bounds +
  tag guard against the underlying vector) — measuring stick: `arrayidx`;
- **native int locals** — the big one for both memory and speed: prove a typed
  local (`my int $x`) never escapes into a `Value` context and never leaves
  `Int`, then emit a raw `long long` with **no box at all** — eliminating the
  376-byte storage and the per-iteration `Value` copies, not just the per-op
  construction. This is a genuine escape-analysis / type-flow pass, the natural
  successor to pass 3;
- devirtualizing monomorphic method calls (measuring stick: `methodcalls`),
  constant-folding literal arithmetic, and specializing `.map`/`.grep`/`.sort`
  on native element types.

`-O` today is the three passes above.

Three of those levers now have a kernel in the showcase suite that measures the
gap rather than a win: `nummath`, `arrayidx` and `methodcalls` were added on
2026-08-22 for exactly that. A showcase whose every row is a speed-up cannot
tell you where the optimizer stops, and a lever with no kernel behind it is a
claim about future work with no number attached. These three report ~1× today;
when a lever lands, its row is the evidence.

## Showcase suite

[`tools/optbench/`](../../tools/optbench) holds programs each written to lean on one
pass, and [`tools/run-optbench.raku`](../../tools/run-optbench.raku) compiles every one
twice — `--exe` and `--exe -O` — checks that both builds, the interpreter, and
Rakudo all emit byte-identical output (a divergent row is flagged and the run
exits non-zero), then times them and reports the `-O` speed-up (Rakudo also shown
for reference):

```sh
./build/rakupp tools/run-optbench.raku
```

Best of 5 runs each and the minimum across three passes, on the benchmarks
machine (macOS/Darwin 24.6, Apple M3, Rakudo v2026.07, measured 2026-08-22 at
`v3.6.0-36-g9dfc982` with all three passes). The last three rows were added on
2026-08-22 to measure where `-O` does **not** yet reach — one per lever named
in "Limits and what's next" above — and are now in the same sitting as the
rest, so every row here is comparable:

| benchmark | `--exe` | `--exe -O` | `-O` speed-up | rakudo | showcases |
|---|---:|---:|---:|---:|---|
| sieve       | 971.4 ms | **20.2 ms**  | **48.1×** | 1002.6 ms | primes <200k — `* <= %%` |
| powmod      | 530.8 ms | **21.1 ms**  | **25.2×** | 728.6 ms  | 1M `** 3` then `% 1000` |
| intsum      | 127.6 ms | **16.5 ms**  | **7.7×**  | 648.0 ms  | 5M `+= $_ * 2 - 1` |
| fibcalls    | 349.1 ms | **62.2 ms**  | **5.6×**  | 1381.2 ms | fib(32) — calls + `< + -` |
| arrayidx    | 90.0 ms  | 47.1 ms      | 1.9×      | 563.3 ms  | array-element lanes — **not built yet** |
| nummath     | 120.5 ms | 92.2 ms      | 1.3×      | 430.7 ms  | `Num` lanes — **not built yet** |
| methodcalls | 284.4 ms | 274.6 ms     | 1.0×      | 309.3 ms  | devirtualizing monomorphic calls — **not built yet** |
| stringbuild | 5.7 ms   | 5.6 ms       | 1.0×      | 209.2 ms  | 400k `~=` — already O(n) by default |

A ninth kernel, **`bigmul`** (`$f *= $_ for 1 .. 10000`), was added on
2026-08-31 and is not in the sitting above, so its numbers are from the M1 box
and belong beside the seventh default's, not beside these: `--exe` 15.6 ms,
`--exe -O` 15.7 ms — **1.0×** — against Rakudo's 1048.9. It reads like `nummath`
on purpose. It is here for the *agreement* check rather than the timing: nothing
else in this directory leaves `int64` (`powmod` tops out at 1e18), so the
compound-assign lane the code generator emits for `*=` had no four-lane
coverage at all — and `-O` reaches that lane by a different route than plain
`--exe`, with a bignum accumulator the one shape where the two could disagree.

What little the three "not built yet" rows gain comes from the parts of the
loop that ARE laneable — the int counter and its comparison — not from the work
the kernel is named for, which is exactly the point. `methodcalls` is the one
to watch: at 1.0× from `-O` and only 1.1× ahead of Rakudo even compiled, it
agrees with the `objects` row in [BENCHMARKS.md](../status/BENCHMARKS.md),
where the interpreter is 1.8× *behind* Rakudo on the same machine. Two
independently added kernels landing on the same conclusion is the reason to
believe it: method dispatch is the hot path with the least done to it.

The int lanes (pass 3) are what moved the top of the table: `sieve`'s whole
inner loop — `while $d * $d <= $n`, `if $n %% $d`, `$d++` — now runs as raw
`int64`, taking it from a tie with Rakudo at plain `--exe` (971 vs 1003 ms) to
50× ahead;
`intsum` went from a small edge (arithmetic already fast, boxing dominant) to
7.9× once the four per-iteration `Value` constructions disappeared.
`stringbuild` is flat because in-place `~=` is default in both builds. As
always this is only the subset of Raku both engines run identically — not a
coverage claim (see [BENCHMARKS.md](../status/BENCHMARKS.md)).

## See also

- [BENCHMARKS.md](../status/BENCHMARKS.md) — the full speed comparison across all modes.
- [`tools/optbench/`](../../tools/optbench) + `tools/run-optbench.raku` — the showcase above.
- `rakupp --cpp [-O] SRC` — inspect exactly what the transpiler emits.
