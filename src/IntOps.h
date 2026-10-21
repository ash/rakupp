#pragma once
// Portable 64-bit integer primitives: overflow-checked add/sub/mul, count-
// trailing-zeros, and a 128-bit-availability flag. GCC/Clang have __builtin_*
// intrinsics and __int128; MSVC has neither, so this maps onto <intrin.h>
// (_mul128, _BitScanForward64) or a manual fallback. Kept free of <windows.h>
// so the core headers can include it cheaply.

#include <cstdint>
#include <climits>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(__SIZEOF_INT128__)
#define RAKUPP_HAS_INT128 1
#else
#define RAKUPP_HAS_INT128 0
#endif

namespace rakupp {

// r = a + b; returns true on signed overflow (matches __builtin_add_overflow).
inline bool add_ovf(long long a, long long b, long long* r) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_add_overflow(a, b, r);
#else
    unsigned long long u = (unsigned long long)a + (unsigned long long)b;
    long long s = (long long)u; *r = s;
    return ((a ^ s) & (b ^ s)) < 0; // both operands' sign differs from the result
#endif
}

inline bool sub_ovf(long long a, long long b, long long* r) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_sub_overflow(a, b, r);
#else
    unsigned long long u = (unsigned long long)a - (unsigned long long)b;
    long long s = (long long)u; *r = s;
    return ((a ^ b) & (a ^ s)) < 0;
#endif
}

inline bool mul_ovf(long long a, long long b, long long* r) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_mul_overflow(a, b, r);
#elif defined(_MSC_VER) && defined(_M_X64)
    long long hi; long long lo = _mul128(a, b, &hi);
    *r = lo;
    return hi != (lo >> 63); // no overflow iff the high half is lo's sign extension
#else
    // Generic fallback (e.g. MSVC ARM64): compute wrapped, then verify by division.
    if (a == 0 || b == 0) { *r = 0; return false; }
    if (a == -1 && b == LLONG_MIN) { *r = LLONG_MIN; return true; }
    if (b == -1 && a == LLONG_MIN) { *r = LLONG_MIN; return true; }
    long long res = (long long)((unsigned long long)a * (unsigned long long)b);
    *r = res;
    return res / b != a;
#endif
}

// r = a ** b by squaring; returns true when the answer is not an int64: a
// negative exponent (a Rat in Raku) or a power past int64 (a BigInt).
inline bool pow_ovf(long long a, long long b, long long* r) {
    if (b < 0) return true;
    long long acc = 1, base = a;
    for (long long e = b;;) {
        if ((e & 1) && mul_ovf(acc, base, &acc)) return true;
        if (!(e >>= 1)) break;
        if (mul_ovf(base, base, &base)) return true;
    }
    *r = acc;
    return false;
}

// count trailing zeros of a nonzero 64-bit value.
inline int ctzll(unsigned long long x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(x);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long idx; _BitScanForward64(&idx, x); return (int)idx; // 64-bit MSVC only
#else
    int n = 0; while (!(x & 1)) { x >>= 1; ++n; } return n;
#endif
}

// |v| for any long long, -2**63 included: std::llabs has no answer for that one.
inline unsigned long long magU(long long v) {
    return v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
}

#if RAKUPP_HAS_INT128
// n1/d1 op n2/d2 for `+ - * /` (c), exactly: the small-Rat lane's arithmetic.
// Every part must be of magnitude under 2**62 and both denominators positive,
// so each product fits __int128. The result is reduced by a binary gcd, its
// denominator positive; whether it fits the caller's representation is the
// caller's to decide. False only for a zero divisor.
__attribute__((always_inline)) inline bool ratOp128(char c, long long n1, long long d1, long long n2,
                                                    long long d2, __int128& n, __int128& d) {
    switch (c) {
        case '*': n = (__int128)n1 * n2; d = (__int128)d1 * d2; break;
        case '/': if (n2 == 0) return false; n = (__int128)n1 * d2; d = (__int128)d1 * n2; break;
        case '+': n = (__int128)n1 * d2 + (__int128)n2 * d1; d = (__int128)d1 * d2; break;
        default:  n = (__int128)n1 * d2 - (__int128)n2 * d1; d = (__int128)d1 * d2; break;
    }
    if (d < 0) { n = -n; d = -d; }
    // binary gcd: shifts and subtracts instead of ~15 __int128 divisions
    auto ctz = [](unsigned __int128 x) -> int {
        const unsigned long long lo = (unsigned long long)x;
        return lo ? ctzll(lo) : 64 + ctzll((unsigned long long)(x >> 64));
    };
    unsigned __int128 a = (unsigned __int128)(n < 0 ? -n : n), b = (unsigned __int128)d, g;
    if (a && b) {
        const int sh = ctz(a | b);
        a >>= ctz(a);
        while (b) {
            b >>= ctz(b);
            if (a > b) { const unsigned __int128 t = a; a = b; b = t; }
            b -= a;
        }
        g = a << sh;
    } else g = a | b;
    if (g > 1) { n /= (__int128)g; d /= (__int128)g; }
    return true;
}
#endif

// count leading zeros of a nonzero 64-bit value.
inline int clzll(unsigned long long x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_clzll(x);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long idx; _BitScanReverse64(&idx, x); return 63 - (int)idx; // 64-bit MSVC only
#else
    int n = 0; while (!(x & 0x8000000000000000ULL)) { x <<= 1; ++n; } return n;
#endif
}

} // namespace rakupp
