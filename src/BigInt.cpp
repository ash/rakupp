#include "BigInt.h"
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace rakupp {

void BigInt::trim() {
    while (!mag.empty() && mag.back() == 0) mag.pop_back();
    if (mag.empty()) sign = 0;
    else if (sign == 0) sign = 1;
}

BigInt::BigInt(long long v) {
    if (v == 0) { sign = 0; return; }
    sign = v < 0 ? -1 : 1;
    unsigned long long u = v < 0 ? (unsigned long long)(-(v + 1)) + 1ull : (unsigned long long)v;
    while (u) { mag.push_back((uint32_t)(u % BASE)); u /= BASE; }
}

BigInt BigInt::fromString(const std::string& s) {
    BigInt r;
    size_t i = 0;
    int sgn = 1;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) { if (s[i] == '-') sgn = -1; i++; }
    std::string digits;
    for (; i < s.size(); i++) if (s[i] >= '0' && s[i] <= '9') digits += s[i];
    if (digits.empty()) return r;
    // parse from the right in chunks of 9
    for (int p = (int)digits.size(); p > 0; p -= 9) {
        int start = std::max(0, p - 9);
        r.mag.push_back((uint32_t)std::stoul(digits.substr(start, p - start)));
    }
    r.sign = sgn;
    r.trim();
    return r;
}

int BigInt::cmpMag(const BigInt& a, const BigInt& b) {
    if (a.mag.size() != b.mag.size()) return a.mag.size() < b.mag.size() ? -1 : 1;
    for (int i = (int)a.mag.size() - 1; i >= 0; i--)
        if (a.mag[i] != b.mag[i]) return a.mag[i] < b.mag[i] ? -1 : 1;
    return 0;
}

int BigInt::cmp(const BigInt& a, const BigInt& b) {
    if (a.sign != b.sign) return a.sign < b.sign ? -1 : 1;
    if (a.sign == 0) return 0;
    int m = cmpMag(a, b);
    return a.sign > 0 ? m : -m;
}

BigInt BigInt::addMag(const BigInt& a, const BigInt& b) {
    BigInt r;
    uint64_t carry = 0;
    size_t n = std::max(a.mag.size(), b.mag.size());
    for (size_t i = 0; i < n || carry; i++) {
        uint64_t cur = carry;
        if (i < a.mag.size()) cur += a.mag[i];
        if (i < b.mag.size()) cur += b.mag[i];
        r.mag.push_back((uint32_t)(cur % BASE));
        carry = cur / BASE;
    }
    r.sign = 1;
    r.trim();
    return r;
}

BigInt BigInt::subMag(const BigInt& a, const BigInt& b) { // assumes |a| >= |b|
    BigInt r;
    int64_t borrow = 0;
    for (size_t i = 0; i < a.mag.size(); i++) {
        int64_t cur = (int64_t)a.mag[i] - borrow - (i < b.mag.size() ? b.mag[i] : 0);
        if (cur < 0) { cur += BASE; borrow = 1; } else borrow = 0;
        r.mag.push_back((uint32_t)cur);
    }
    r.sign = 1;
    r.trim();
    return r;
}

BigInt BigInt::operator-() const { BigInt c = *this; c.sign = -c.sign; return c; }

BigInt BigInt::operator+(const BigInt& o) const {
    if (sign == 0) return o;
    if (o.sign == 0) return *this;
    if (sign == o.sign) { BigInt r = addMag(*this, o); r.sign = sign; r.trim(); return r; }
    int m = cmpMag(*this, o);
    if (m == 0) return BigInt();
    if (m > 0) { BigInt r = subMag(*this, o); r.sign = sign; r.trim(); return r; }
    BigInt r = subMag(o, *this); r.sign = o.sign; r.trim(); return r;
}

BigInt BigInt::operator-(const BigInt& o) const { return *this + (-o); }

// The magnitude as a uint64. Only valid when fitsU64(), which is what the two
// fast paths below check first; the intermediate never overflows because that
// guarantee bounds the whole value by 2^64-1.
static inline unsigned long long magU64(const BigInt& x) {
    unsigned long long v = 0;
    for (std::size_t i = x.mag.size(); i-- > 0;) v = v * 1000000000ull + x.mag[i];
    return v;
}
// Rebuild a BigInt from a magnitude and a sign; a zero magnitude is sign 0,
// which is the invariant trim() maintains everywhere else.
static inline BigInt fromMagU64(unsigned long long m, int sign) {
    BigInt r;
    while (m) { r.mag.push_back((uint32_t)(m % 1000000000ull)); m /= 1000000000ull; }
    r.sign = r.mag.empty() ? 0 : sign;
    return r;
}

#if defined(__SIZEOF_INT128__)
// The same two helpers one limb-width up. Values in the 64-128 bit band are
// where real arithmetic lives once it stops fitting a machine word — Pollard
// rho squares a ~1e17 modulus into ~1e34 on every step — and the base-1e9 long
// division below pays a heap allocation per quotient limb to get there.
static inline unsigned __int128 magU128(const BigInt& x) {
    unsigned __int128 v = 0;
    for (std::size_t i = x.mag.size(); i-- > 0;) v = v * 1000000000u + x.mag[i];
    return v;
}
static inline BigInt fromMagU128(unsigned __int128 m, int sign) {
    BigInt r;
    while (m) { r.mag.push_back((uint32_t)(m % 1000000000u)); m /= 1000000000u; }
    r.sign = r.mag.empty() ? 0 : sign;
    return r;
}
#endif

// Multiply a magnitude by a SINGLE limb. The general schoolbook loop below
// routes each limb's carry through r.mag[i+1] — a store the next iteration must
// load back — and needs a second inner pass per limb to place it, so a
// big-by-small product runs at two iterations and two store-to-load round trips
// per limb. Keeping the carry in a register does both in one pass, and a running
// product (`$f *= $_`, factorials, radix scaling) is entirely this shape.
//
// One limb of that pass: `d[i] = s[i]*m + carry`, base 1e9.
//
// This folds the carry into the product BEFORE splitting it, which puts the
// division by 1e9 on the carry chain — deliberately. The alternative, splitting
// s[i]*m first so that only an add and a conditional subtract depend on the
// carry, has a three-cycle chain instead of seven, and it WAS the faster form
// while the loop ran one chain. It is not any more: mulRunK below runs eight
// independent chains, so no chain's latency is on the critical path, and what is
// left to pay is instructions. This shape is six of them — load, umaddl, umulh,
// shift, msub, store — against the split form's twelve. Measured on the
// factorial kernel at K=8, all three forms in one harness: fold-first 2.46 ms,
// a reciprocal-of-m form 2.80 (M = ceil(2^64*m/1e9), two multiply-highs in place
// of the divide — same three multiply-class ops, the real floor here, and four
// more scalar ones), split-first 3.48.
//
// p cannot overflow, and carry stays below BASE, by induction: s[i] and m are
// both < BASE and carry < BASE, so p <= (BASE-1)^2 + BASE-1 < 1e18 < 2^63 and
// q = p/BASE <= BASE-1.
static inline void mulStep(uint32_t* d, const uint32_t* s, std::size_t i,
                           uint32_t m, uint32_t& carry) {
    uint64_t p = (uint64_t)s[i] * m + carry;
    uint64_t q = p / BigInt::BASE;              // a multiply-high and a shift
    d[i] = (uint32_t)(p - q * BigInt::BASE);    // one fused multiply-subtract
    carry = (uint32_t)q;
}

// Add one limb into position k and let it ripple. Both addends are < BASE, so
// the sum is < 2^31 and the carry out is 0 or 1; the ripple stops at the first
// limb that does not overflow, which is the first one on all but a vanishing
// fraction of calls. It cannot run past `n`: the caller's array is sized to hold
// the whole product, and a partial sum of a value is bounded by that value —
// the `k <= n` guard is a belt on top of that, not the reason it terminates.
static inline void addLimbAt(uint32_t* d, std::size_t k, std::size_t n, uint32_t c) {
    while (c && k <= n) {
        uint32_t v = d[k] + c;
        if (v >= BigInt::BASE) { d[k] = v - BigInt::BASE; c = 1; }
        else { d[k] = v; c = 0; }
        k++;
    }
}

// dst[0..n] = src[0..n-1] * m, dst == src allowed. n >= 1, m in [2, BASE).
//
// THE CARRY CHAIN WAS THE WHOLE COST. Every limb's carry feeds the next one, so
// a single-chain loop can only retire one limb per round trip through that
// dependency however wide the core is — measured, about five cycles a limb where
// the instruction count says one and a half.
//
// So run K chains. Cut the magnitude into K contiguous segments, give each its
// own carry starting at zero, and interleave their steps in one loop body. The
// chains are independent, so they issue in parallel and the loop becomes
// throughput-bound instead of latency-bound.
//
// What that owes afterwards is K-1 carries: segment j's carry-out belongs at the
// first limb of segment j+1, which was computed as if nothing came in from below.
// Paying it back is addLimbAt, and the payments commute — each is an addition of
// a fixed amount at a fixed position — so the order does not matter and a ripple
// running on into a later segment is still just an addition on the array.
//
// Short magnitudes keep a single chain: K chains need K segments' worth of work
// in flight to pay for the fold-back, and below a couple of dozen limbs there is
// nothing to overlap.
template <std::size_t K>
static inline void mulRunK(uint32_t* d, const uint32_t* s, std::size_t n, uint32_t m) {
    const std::size_t q = n / K;
    uint32_t c[K] = {0};
    for (std::size_t i = 0; i < q; i++)
        for (std::size_t j = 0; j < K; j++) mulStep(d + j * q, s + j * q, i, m, c[j]);
    // n % K leftovers belong to the last segment, which runs past q
    for (std::size_t i = q; i < n - (K - 1) * q; i++)
        mulStep(d + (K - 1) * q, s + (K - 1) * q, i, m, c[K - 1]);
    d[n] = c[K - 1];
    for (std::size_t j = 0; j + 1 < K; j++) addLimbAt(d, (j + 1) * q, n, c[j]);
}

static void mulRun(uint32_t* d, const uint32_t* s, std::size_t n, uint32_t m) {
    // Eight measured best on an M1 over the factorial kernel: 4 and 6 are within
    // 3%, 2 is 1.5x worse, 16 gives the fold-back more segments than the loop
    // saves. Below 32 limbs there are fewer than four limbs per segment and the
    // single chain wins outright.
    if (n >= 32) { mulRunK<8>(d, s, n, m); return; }
    uint32_t carry = 0;
    for (std::size_t i = 0; i < n; i++) mulStep(d, s, i, m, carry);
    d[n] = carry;
}

static BigInt mulLimb(const BigInt& a, uint32_t m) {
    BigInt r;
    if (m == 0 || a.sign == 0) return r;
    const std::size_t n = a.mag.size();
    r.mag.resize(n + 1);
    mulRun(r.mag.data(), a.mag.data(), n, m);
    r.sign = a.sign;
    r.trim();
    return r;
}

// The same kernel over the magnitude it is handed rather than a fresh one. The
// extra limb is pushed FIRST so the carry always has somewhere to land, and
// push_back grows geometrically — so a product that runs for thousands of steps
// pays an amortised O(1) reallocation per step instead of an allocation, a
// zero-fill and a free every step.
void BigInt::mulLimbInPlace(uint32_t m) {
    if (sign == 0) return;
    if (m == 1) return;
    if (m == 0) { mag.clear(); sign = 0; return; }
    const std::size_t n = mag.size();
    mag.push_back(0);
    mulRun(mag.data(), mag.data(), n, m);
    trim();
}

BigInt BigInt::operator*(const BigInt& o) const {
    if (sign == 0 || o.sign == 0) return BigInt();
#if defined(__SIZEOF_INT128__)
    // (2^64-1)^2 < 2^128, so two u64 magnitudes always multiply exactly into a
    // u128 — one hardware multiply in place of the base-1e9 schoolbook loop and
    // its per-limb `% BASE` / `/ BASE`.
    if (fitsU64() && o.fitsU64())
        return fromMagU128((unsigned __int128)magU64(*this) * magU64(o), sign * o.sign);
#endif
    // Exactly one of the two is a single limb: one pass, carry in a register.
    if (o.mag.size() == 1) { BigInt r = mulLimb(*this, o.mag[0]); if (r.sign) r.sign = sign * o.sign; return r; }
    if (mag.size() == 1)   { BigInt r = mulLimb(o, mag[0]);       if (r.sign) r.sign = sign * o.sign; return r; }
    BigInt r;
    r.mag.assign(mag.size() + o.mag.size(), 0);
    for (size_t i = 0; i < mag.size(); i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < o.mag.size() || carry; j++) {
            uint64_t cur = r.mag[i + j] + carry +
                (j < o.mag.size() ? (uint64_t)mag[i] * o.mag[j] : 0);
            r.mag[i + j] = (uint32_t)(cur % BASE);
            carry = cur / BASE;
        }
    }
    r.sign = sign * o.sign;
    r.trim();
    return r;
}

// truncated division: q = trunc(a/b), r = a - q*b (sign of a)
void BigInt::divmod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r) {
    q = BigInt(); r = BigInt();
    if (b.sign == 0) return; // div by zero -> 0,0 (caller guards)
    if (cmpMag(a, b) < 0) { r = a; return; }
    // Fast path: one hardware divide instead of the base-1e9 long division
    // below, whose per-limb BINARY SEARCH costs ~30 BigInt multiplications.
    // Measured on values that fit in 64 bits, divmod was 2.1 us and gcd — which
    // is Euclid over divmod — was 15.8 us, so every Rat construction (gcd plus
    // two divmods, i.e. every decimal literal and every p/q in Raku) cost ~10 us
    // before this. Almost every Rat in real code is small.
    if (a.fitsU64() && b.fitsU64()) {
        unsigned long long am = magU64(a), bm = magU64(b);
        q = fromMagU64(am / bm, a.sign * b.sign);
        r = fromMagU64(am % bm, a.sign);
        return;
    }
#if defined(__SIZEOF_INT128__)
    // …and one 128-bit divide for the next band up. `($x * $x + $c) % $n` with a
    // ~1e17 modulus lands here on every Pollard-rho step: the dividend is ~1e34,
    // so it misses the u64 path and used to run algorithm D, which allocates a
    // BigInt per quotient limb. Two hardware divides instead.
    if (a.fitsU128() && b.fitsU128()) {
        unsigned __int128 am = magU128(a), bm = magU128(b);
        q = fromMagU128(am / bm, a.sign * b.sign);
        r = fromMagU128(am % bm, a.sign);
        return;
    }
#endif
    // Long division on magnitudes, base 1e9 — Knuth's algorithm D. The quotient
    // limb is ESTIMATED from the leading limbs and then corrected, instead of
    // binary-searched: the search cost ~30 full BigInt multiplications per limb,
    // which made gcd (Euclid over divmod, and every Rat construction calls it)
    // 865ms on a 1437-digit/812-digit pair where Rakudo takes 3ms. That was not
    // a corner: Math::NumberTheory's FatRat digit expansions reduce Rats with
    // ~600-digit parts on every step, and the file simply never finished.
    //
    // Normalizing by `f` so the divisor's top limb is at least BASE/2 is what
    // bounds the estimate's error to 2 — without it a small leading limb can
    // make the estimate wrong by a factor of BASE.
    BigInt babs = b.abs(), aabs = a.abs();
    uint32_t f = (uint32_t)((uint64_t)BASE / ((uint64_t)babs.mag.back() + 1));
    if (f > 1) { aabs = aabs * BigInt((long long)f); babs = babs * BigInt((long long)f); }
    const size_t n = babs.mag.size();
    const uint64_t vtop = babs.mag[n - 1];
    BigInt cur;          // running remainder (magnitude, positive)
    q.mag.assign(aabs.mag.size(), 0);
    for (int i = (int)aabs.mag.size() - 1; i >= 0; i--) {
        // cur = cur*BASE + aabs.mag[i]
        cur.mag.insert(cur.mag.begin(), aabs.mag[i]);
        cur.sign = 1; cur.trim();
        uint32_t x = 0;
        if (cmpMag(cur, babs) >= 0) {
            // cur < babs*BASE here, so it is at most one limb longer than babs
            size_t m = cur.mag.size();
            uint64_t top = cur.mag[m - 1];
            if (m > n) top = top * (uint64_t)BASE + cur.mag[m - 2];
            uint64_t est = top / vtop;
            if (est > (uint64_t)BASE - 1) est = (uint64_t)BASE - 1;
            x = (uint32_t)est;
            BigInt t = babs * BigInt((long long)x);
            while (x > 0 && cmpMag(t, cur) > 0) { x--; t = subMag(t, babs); }   // at most 2
            for (;;) { BigInt t2 = t + babs; if (cmpMag(t2, cur) > 0) break; x++; t = t2; }
            cur = subMag(cur, t);
        }
        q.mag[i] = x;
    }
    q.sign = a.sign * b.sign;
    q.trim();
    if (f > 1) { // undo the normalization on the remainder (f divides it exactly)
        uint64_t carry = 0;
        for (int i = (int)cur.mag.size() - 1; i >= 0; i--) {
            uint64_t v = carry * (uint64_t)BASE + cur.mag[i];
            cur.mag[i] = (uint32_t)(v / f);
            carry = v % f;
        }
        cur.sign = 1; cur.trim();
    }
    r = cur; r.sign = (cur.mag.empty() ? 0 : a.sign); r.trim();
}

BigInt BigInt::pow(long long e) const {
    BigInt result(1), base = *this;
    while (e > 0) {
        if (e & 1) result = result * base;
        base = base * base;
        e >>= 1;
    }
    return result;
}

BigInt BigInt::gcd(BigInt a, BigInt b) {
    a.makeAbs(); b.makeAbs();   // by value already: no reason to copy them again
    // Euclid entirely in registers when both fit — the general loop below builds
    // two BigInts per step and calls divmod, and Value::rat() calls this on
    // EVERY Rat it constructs.
    if (a.fitsU64() && b.fitsU64()) {
        unsigned long long x = magU64(a), y = magU64(b);
        while (y) { unsigned long long t = x % y; x = y; y = t; }
        return fromMagU64(x, 1);
    }
    while (!b.isZero()) { BigInt q, r; divmod(a, b, q, r); a = b; b = r; }
    return a;
}

bool BigInt::fitsLL() const {
    if (mag.size() > 3) return false;
    static const BigInt maxLL(9223372036854775807ll); // 2^63 - 1
    if (sign >= 0) return cmpMag(*this, maxLL) <= 0;
    // negatives fit down to LLONG_MIN, whose magnitude is 2^63 = maxLL + 1
    static const BigInt minAbs = maxLL + BigInt(1);
    return cmpMag(*this, minAbs) <= 0;
}

long long BigInt::toLL() const {
    // saturate on overflow instead of wrapping (UB) — callers use this for native-int
    // coercion (indexing, chr, ranges); a silent wrap produced garbage indices.
    if (!fitsLL()) return sign < 0 ? INT64_MIN : INT64_MAX;
    unsigned long long r = 0;
    for (int i = (int)mag.size() - 1; i >= 0; i--) r = r * (unsigned long long)BASE + mag[i];
    return sign < 0 ? -(long long)r : (long long)r; // r <= 2^63 here, so -(ll)r is well-defined at LLONG_MIN
}

unsigned long long BigInt::toU64Wrap() const {
    unsigned long long r = 0;   // unsigned overflow is defined: this is value mod 2^64
    for (int i = (int)mag.size() - 1; i >= 0; i--) r = r * (unsigned long long)BASE + mag[i];
    return sign < 0 ? (unsigned long long)0 - r : r;
}

double BigInt::toDouble() const {
    double r = 0;
    for (int i = (int)mag.size() - 1; i >= 0; i--) r = r * (double)BASE + mag[i];
    return sign < 0 ? -r : r;
}

std::string BigInt::toString() const {
    if (sign == 0) return "0";
    std::string s = sign < 0 ? "-" : "";
    s += std::to_string(mag.back());
    char buf[16];
    for (int i = (int)mag.size() - 2; i >= 0; i--) { snprintf(buf, sizeof(buf), "%09u", mag[i]); s += buf; }
    return s;
}

} // namespace rakupp
