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

// count trailing zeros of a nonzero 64-bit value.
inline int ctzll(unsigned long long x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(x);
#elif defined(_MSC_VER)
    unsigned long idx; _BitScanForward64(&idx, x); return (int)idx;
#else
    int n = 0; while (!(x & 1)) { x >>= 1; ++n; } return n;
#endif
}

} // namespace rakupp
