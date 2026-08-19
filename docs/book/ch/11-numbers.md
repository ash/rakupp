# Numbers

Raku's numeric tower is exact where it can be. `1/3` is a rational, not a
float. `2**200` is an integer, not an overflow. `0.1 + 0.2 == 0.3` is `True`,
because a decimal literal is a `Rat`. Implementing that on a machine with
64-bit registers means three layers, each with a boundary the previous one
falls through.

## Layer 0: native integers, with overflow as a signal

Most integers in most programs fit in a `long long`, so that is the
representation: `Value` with `t == VT::Int` and the value in `i`. Promotion to
bignum happens exactly when an operation overflows, which means overflow must be
*detected* rather than avoided.

```cpp
// src/IntOps.h
inline bool add_ovf(long long a, long long b, long long* r) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_add_overflow(a, b, r);
#else
    unsigned long long u = (unsigned long long)a + (unsigned long long)b;
    long long s = (long long)u; *r = s;
    return ((a ^ s) & (b ^ s)) < 0;   // both operands' sign differs from result
#endif
}
```

`IntOps.h` exists because MSVC has neither `__builtin_*_overflow` nor
`__int128`. It provides `add_ovf`, `sub_ovf`, `mul_ovf`, `ctzll`, `clzll` and a
`RAKUPP_HAS_INT128` flag, mapping each onto intrinsics where they exist and a
hand-written fallback where they do not. The multiply fallback for MSVC on
ARM64 computes the wrapped product and then verifies it by division, which is
slow and correct; the x64 path uses `_mul128` and checks that the high half is
the low half's sign extension.

This header is what makes the arithmetic fast paths in Chapters 27 and 28
possible: `rtAdd` can inline the native case precisely because the overflow test
is one instruction on the compilers that matter.

## Layer 1: `BigInt`

```cpp
// src/BigInt.h
struct BigInt {
    int sign = 0;                  // -1, 0, +1
    std::vector<uint32_t> mag;     // little-endian limbs, base 1e9
    static const uint32_t BASE = 1000000000u;
};
```

Base 10^9 rather than a power of two. That choice trades some arithmetic
density for the thing a language runtime does constantly: **decimal conversion
is free**. `toString` walks the limbs and prints nine digits each;
`fromString` parses in nine-digit chunks from the right. A binary base would
make printing a repeated division by 10, which for a language where every
integer is eventually stringified is the wrong trade.

Multiplication is schoolbook, O(n·m). There is no Karatsuba and no FFT: the
sizes that appear in practice are small, and the workloads where they are not
are dominated by other costs.

Division is base-10^9 long division, and it has one wart worth naming: the
per-limb quotient digit is found by **binary search** over `[0, BASE)`, costing
about thirty `BigInt` multiplications per limb. That is expensive, which is why
the fast path below matters so much.

### The 64-bit fast path

Almost every bignum operation in real Raku code is on values that are not, in
fact, big. They arrive as `BigInt` because a `Rat` stores its numerator and
denominator as `BigInt`s unconditionally — and *every* `Rat` construction calls
`gcd` and then two `divmod`s.

Measured on values that fit in 64 bits, `divmod` cost 2.1 microseconds and
`gcd` — which is Euclid over `divmod` — cost 15.8 microseconds. So every decimal
literal and every `p/q` in a program cost roughly 10 microseconds to build.

```cpp
// src/BigInt.cpp — divmod, the fast path
// One hardware divide instead of the base-1e9 long division below, whose
// per-limb BINARY SEARCH costs ~30 BigInt multiplications.
if (a.fitsU64() && b.fitsU64()) { /* two 64-bit divides, rebuild */ }
```

```cpp
// src/BigInt.cpp — gcd
// Euclid entirely in registers when both fit — the general loop below
// builds two BigInts per step and calls divmod, and Value::rat() calls
// this on EVERY Rat it constructs.
```

The guard is `fitsU64()`, which is a three-limb comparison against the base-10^9
digits of 2^64 − 1:

```cpp
// src/BigInt.h
bool fitsU64() const {
    if (mag.size() > 3) return false;
    if (mag.size() < 3) return true;
    if (mag[2] != 18u)        return mag[2] < 18u;
    if (mag[1] != 446744073u) return mag[1] < 446744073u;
    return mag[0] <= 709551615u;
}
```

`Rat` construction became about nineteen times faster.

### Two conversions to 64 bits, deliberately different

```cpp
// src/BigInt.h
long long toLL() const;                    // SATURATES on overflow
unsigned long long toU64Wrap() const;      // WRAPS, two's complement
```

`toLL` saturates because its callers are indices, codepoints and range bounds,
where a silent wrap produces garbage rather than a wrong-but-defined answer.
`toU64Wrap` wraps because its caller is a native `uint64`/`int64` container,
which is *defined* to keep the low bits — and saturating there turned every
64-bit digest word into `0x7FFF_FFFF_FFFF_FFFF`.

Two functions with the same signature and opposite policies is a smell in
general. Here it is the correct answer, and the comment in the header says so
at length precisely because the next person will want to merge them.

## Layer 2: `Rat`

A `Rat` is a normalised pair of `BigInt`s:

```cpp
// src/Value.h
static Value rat(BigInt n, BigInt d) {
    if (d.sign == 0) return ratZ(std::move(n), std::move(d));
    Value v; v.t = VT::Rat;
    if (d.sign < 0) { n = -n; d = -d; }          // sign lives in the numerator
    BigInt g = BigInt::gcd(n, d);
    if (!g.isZero()) { /* divide both by g */ }
    v.ratN = std::make_shared<BigInt>(n);
    v.ratD = std::make_shared<BigInt>(d);
    return v;
}
```

Normalisation is eager: the denominator is positive and the pair is reduced at
construction. That is what makes `==` on `Rat`s a component-wise comparison
rather than a cross-multiplication, and it is why `gcd`'s speed matters.

A decimal literal is a `Rat`, built by the parser as a numerator and
denominator pair (`NumLit::ratNum`, `ratDen`, with decimal-string fields for
the cases that overflow `long long`). The node caches the constructed `BigInt`
parts, because a literal in a hot loop would otherwise re-allocate and re-reduce
on every evaluation.

### The spill, and the type that is exempt

Raku caps a plain `Rat`'s denominator at 64 bits. Arithmetic that would produce
a larger one degrades to `Num`:

```cpp
// src/Interpreter.cpp — applyArith, the Rat result
bool fat = (l.t == VT::Rat && l.fatRat) || (r.t == VT::Rat && r.fatRat);
Value v = Value::rat(std::move(n), std::move(d)); v.fatRat = fat;
if (!fat && v.ratD && !v.ratD->fitsU64()) return Value::number(v.toNum());
return v;
```

`FatRat` is the same storage with a flag, and the flag does two things: it
carries the type identity, and it exempts the value from the spill so a `FatRat`
stays an arbitrary-precision rational forever. The flag is **contagious** — one
`FatRat` operand makes the result a `FatRat` — which is the semantics Raku
specifies and also the only way a chain of operations can stay exact.

`Rat.new` is subtly different from the `/` operator: a zero denominator must be
*preserved* rather than treated as an error, normalised to ±1/0 with 0/0 kept as
0/0. That is `ratZ`, and taking `Str` or `Num` of such a value throws.

## Native integer containers

`my int8 $x` is not a different type at runtime; it is a `Value` carrying a
width:

```cpp
// src/Value.h
int natBits = 0;       // 0 = not native
bool natSigned = false;
bool natFloat = false; // num32: truncate to float32 on assignment
```

Every assignment masks to the declared width, so `my uint8 $b = 300` stores 44.
The width is derived from the declared type name once and cached, because
deriving it involved a `substr` and therefore an allocation per typed parameter
per call:

```cpp
// src/Value.h
static int natWidthOfType(const std::string& ofType, bool& sign);
```

```cpp
// src/Ast.h — Param
mutable DecidedOnce<int> natSpec{-1};   // bits<<1 | signed, decided once
```

Typed arrays and hashes use the same table through `ofType`, so
`my uint32 @a` stores masked elements.

## Complex

`VT::Complex` reuses `n` for the real part and `im` for the imaginary one — the
only tag that uses `im` at all, apart from a fractional `Range`'s upper bound.
Coercing a `Complex` back to a real checks the imaginary part against
`$*TOLERANCE`, which defaults to 1e-15 and is looked up dynamically first, then
lexically.

The language revision matters here: under `use v6.e.PREVIEW`, `sqrt(-1)` is a
`Complex` rather than `NaN` — and so is `log(-1)`, and `log10`/`log2` of a
negative, all of which come through one `rtLogReal` so the branch is written
once. `Interpreter::langRev_` carries the revision *of the code currently
running*: each compilation unit records the revision it was compiled under, every
routine is stamped with its unit's, and the call path switches to the callee's
for the duration of the call. That is what lets a module compiled under 6.e keep
`Complex` results when a 6.d program calls into it, without the 6.d program's own
`sqrt` changing meaning.

## Where the arithmetic actually happens

One function, `applyArith(op, l, r)`, implements every binary operator for every
combination of numeric types, plus string operators, set operators, junction
autothreading and `Whatever` currying. It is shared by the interpreter and the
compiled backend, which is why the two agree byte for byte.

Its structure is a dispatch chain, and the order is performance-critical.
Integer-integer and number-number cases are tested first, with a character
switch on the operator rather than a chain of string comparisons. String
comparisons were originally *late* in that chain, and paid about 110 nanoseconds
walking past mixin delegation, negated operators, set operations, junction
autothreading, `Whatever` currying and the `Version` branch before reaching the
actual work — which is why `~`, `eq` and friends got their own fast path at the
top too (Chapter 28).

## A bug worth remembering: numifying by throwing

`Value::toNum()` numified a `Str` with `std::stod`, which **throws
`std::invalid_argument`** when the string does not begin with a number. The
result was caught and turned into `0.0` — the right answer, arrived at by
raising, unwinding and catching a C++ exception.

Speculatively numifying a string that turns out not to be one is completely
routine in an interpreter; the invocant of every `"ab".method` call reached that
code. So the language's slowest control-transfer mechanism sat on its hottest
path, at roughly 7 microseconds per method call.

`std::strtod` reports the same failure by leaving its end pointer at the start
of the input, and costs nothing:

```cpp
// src/Value.cpp
static double strToNumOr0(const std::string& s) {
    if (s.empty()) return 0.0;
    const char* p = s.c_str(); char* end = nullptr;
    double d = std::strtod(p, &end);
    return end == p ? 0.0 : d;      // stod would have thrown here
}
```

Measured over 300,000 iterations:

| | before | after | Rakudo |
|---|---:|---:|---:|
| `'ab'.chars` | 2.23 s | **0.73 s** | 0.34 s |
| `'ab'.uniname` | 2.51 s | **0.47 s** | 0.36 s |

Roast was unchanged across the switch, which is the point: `strtod` and a caught
`stod` agree on every input that reaches this path.

The general lesson is not about `stod`. It is that a C++ exception used as a
*value-returning* mechanism — rather than for an error that genuinely aborts
something — is a performance bug waiting for a profiler. The same realisation
drives the cooperative control flow in Chapter 15.
