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
    LimbVec mag;                   // little-endian limbs, base 1e9
    static const uint32_t BASE = 1000000000u;
};
```

`LimbVec` is the part of `std::vector<uint32_t>` the arithmetic uses, with the
first four limbs inline. It is the same 24 bytes as the vector, and storage
moves to the heap only past four limbs. A `Rat` is two `BigInt`s, and nearly
every `Rat` a program makes has parts under 10^36, so each one used to cost two
heap blocks for a limb apiece.

Base 10^9 rather than a power of two. That choice trades some arithmetic
density for the thing a language runtime does constantly: **decimal conversion
is free**. `toString` walks the limbs and prints nine digits each;
`fromString` parses in nine-digit chunks from the right. A binary base would
make printing a repeated division by 10, which for a language where every
integer is eventually stringified is the wrong trade.

Multiplication is schoolbook below 96 limbs and Karatsuba above, with the
longer operand taken in slices when the two differ in length. Where the
compiler has a 128-bit integer, two magnitudes that each fit 64 bits skip both
and multiply in one hardware product.
It was schoolbook alone for a long time, on the argument that the sizes that
appear in practice are small. They are, until a program writes
`2 ** 1_000_000`: that went from 1.46 s to 0.04 s, measured on an M3 when the
Karatsuba path was added. There is no FFT.

Division is base-10^9 long division — Knuth's algorithm D. The per-limb quotient
digit is **estimated** from the leading limbs and then corrected, which is what
normalising the divisor so its top limb is at least `BASE/2` buys: it bounds the
estimate's error to 2, where without it a small leading limb can make the
estimate wrong by a factor of `BASE`.

It did not always work that way, and the reason it changed is a good example of
a wart that was only theoretical until it was not. The quotient digit used to be
found by **binary search** over `[0, BASE)`, at about thirty `BigInt`
multiplications per limb. That made `gcd` — then Euclid over `divmod`, which
every `Rat` construction calls — take 865 ms on a 1,437-digit over 812-digit pair
where Rakudo takes 3. And it was not a corner case: `Math::NumberTheory`'s
`FatRat` digit expansions reduce `Rat`s with ~600-digit parts on every step, so
that file simply never finished.

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
// Fast path: one hardware divide instead of the base-1e9 long division
// below.
if (a.fitsU64() && b.fitsU64()) { /* two 64-bit divides, rebuild */ }
```

```cpp
// src/BigInt.cpp — gcd
// Euclid entirely in registers when both fit — Value::rat() calls this on
// EVERY Rat it constructs.
```

Both have grown a second band since. Where the compiler has a 128-bit integer,
`divmod` of two magnitudes that fit 128 bits is two hardware divides as well:
`($x * $x + $c) % $n` with a modulus near 10^17 has a dividend near 10^34 on
every step of a Pollard rho. And past 64 bits `gcd` is no longer Euclid over
`divmod`. It converts both magnitudes to binary words, takes out the factors
of two, and runs Lehmer's algorithm on the rest.

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
    v.setRatParts(std::move(n), std::move(d));
    return v;
}
```

The pair is a payload body of its own, `PK::Rat`:

```cpp
// src/Value.h
struct RatBody {
    BigInt num, den;
    RatBody(BigInt n, BigInt d) : num(std::move(n)), den(std::move(d)) {}
    RatBody(const RatBody&) = default;
};
```

Every copy of the `Value` shares the body, and only its sole holder changes it
in place. Until 2026-11-05 the parts were two `shared_ptr<BigInt>` in the cold
block: three allocations a `Rat`. They are one 80-byte block now, with the two
`BigInt`s by value and the `FatRat` flag in the block's header. A million kept
`Rat`s went from 437 to 163 bytes an element with that change, against
Rakudo's 211. `v.ratN()` and `v.ratD()` still answer a pointer to each part,
null on anything that is not a `Rat`.

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
// src/InterpreterCore.cpp — applyArith, the Rat result
bool fat = (l.t == VT::Rat && l.fatRat()) ||
           (r.t == VT::Rat && r.fatRat());
auto mkRat = [&](BigInt n, BigInt d) {
    Value v = Value::rat(std::move(n), std::move(d)); v.setFatRat(fat);
    if (!fat && v.ratD() && !v.ratD()->fitsU64())
        return Value::number(v.toNum());
    return v;
};
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
// src/Value.h — abridged
bool natSigned : 1;
bool natFloat : 1;     // num32: truncates to float32 on assignment
unsigned natBytes_ : 4;
int natBits() const { return (int)natBytes_ * 8; }   // 0 = not native
void setNatBits(int bits) { natBytes_ = (unsigned)bits >> 3; }
```

The width is only ever 0, 8, 16, 32 or 64 bits, so it is kept in bytes and
fits four bits of the tag block.

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
`im` is a field of the cold block, read as `v.im()`, so a `Complex` carries one.
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
// src/Value.cpp — abridged
static double strToNumOr0(const char* p, size_t n) {
    if (n == 0) return 0.0;
    // … a leading U+2212 MINUS SIGN is turned into '-' first …
    char* end = nullptr;
    errno = 0;
    double d = cnum::strtod(p, &end);
    if (end == p) return 0.0;   // nothing numeric at the front
    return d;
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
