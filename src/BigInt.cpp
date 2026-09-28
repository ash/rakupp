#include "BigInt.h"
#include "CNumeric.h"
#include "IntOps.h"
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace rakupp {

thread_local unsigned long long* g_bigIntBudget = nullptr;

// A number as m × BASE**ex, m truncated (toward zero) to its top K limbs.
namespace {
struct TopLimbs { BigInt m; long long ex = 0; };
void keepTop(TopLimbs& x, size_t k) {
    const size_t n = x.m.mag.size();
    if (n <= k) return;
    x.m.mag.erase(x.m.mag.begin(), x.m.mag.begin() + (long)(n - k));
    x.ex += (long long)(n - k);
}
TopLimbs topPow(const BigInt& b, unsigned long long e, size_t k) {
    TopLimbs r, base;
    r.m = BigInt(1);
    base.m = b.abs();
    keepTop(base, k);
    while (e) {
        if (e & 1) { r.m = r.m * base.m; r.ex += base.ex; keepTop(r, k); }
        e >>= 1;
        if (e) { base.m = base.m * base.m; base.ex += base.ex; keepTop(base, k); }
    }
    return r;
}
}  // namespace

double bigRatioPowToDouble(const BigInt& n, const BigInt& d, unsigned long long e) {
    const bool neg = ((n.sign < 0) != (d.sign < 0)) && (e & 1);
    if (n.isZero()) return e == 0 ? 1.0 : 0.0;
    const size_t k = 6;
    TopLimbs pn = topPow(n, e, k), pd = topPow(d, e, k);
    // the ratio of the two mantissas to 20 significant digits, placed by the
    // decimal digits the limb counts stand for; strtod rounds it once, and says
    // Inf or 0 when the exponent is past a double's range
    std::string sn = pn.m.toString(), sd = pd.m.toString();
    long long scale = 20 - ((long long)sn.size() - (long long)sd.size());
    BigInt num = pn.m, den = pd.m, q, rem;
    if (scale > 0) num = num * BigInt(10).pow(scale);
    else if (scale < 0) den = den * BigInt(10).pow(-scale);
    BigInt::divmod(num, den, q, rem);
    long long dexp = 9LL * (pn.ex - pd.ex) - scale;
    std::string lit = q.toString() + "e" + std::to_string(dexp);
    double v = cnum::strtod(lit.c_str(), nullptr);
    return neg ? -v : v;
}

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
    unsigned long long* const budget = g_bigIntBudget;   // (one thread-local read, not one per row)
    for (size_t i = 0; i < mag.size(); i++) {
        if (budget) {   // one row of limb products, paid for before it runs
            if (*budget < o.mag.size()) throw BigIntBudgetExceeded{};
            *budget -= o.mag.size();
        }
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
    // below. (That loop was a per-limb BINARY SEARCH costing ~30 BigInt
    // multiplications when these numbers were taken; it is Knuth's algorithm D
    // now — see the comment there — so the ratio below is better than stated.)
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

// ---------------------------------------------------------------------------
// The magnitude in binary.
//
// Base 1e9 answers most bit questions from a few limbs — bitLength and
// lowestSetBit below say which — but not all of them: 2**N and 2**N - 1 agree
// in every digit but the last, and divisibility by 2**N depends on all of
// them. Those convert.
//
// Divide and conquer: split the limbs at a power of two h, convert both
// halves, and join them as hi·1e9^h + lo. Only the powers 1e9^(2^k) ever
// occur, each the square of the one before, so a conversion builds log2(limbs)
// of them. The joins are Karatsuba products, and that is what makes the whole
// subquadratic: each level of the split costs two thirds of the one above, so
// the top join dominates. 2**1_000_000 (33,448 limbs) converts in 23 ms.
// Converting a limb at a time instead (acc = acc·1e9 + next) is quadratic
// however tight the loop — 70 ms for the same number at two limbs a step — and
// so is peeling bits off the bottom by repeated division, which is how `.msb`
// used to do it: about forty minutes for that number.
// ---------------------------------------------------------------------------
namespace {

#if RAKUPP_HAS_INT128
typedef uint64_t Word;
typedef unsigned __int128 DWord;
#else
typedef uint32_t Word;
typedef uint64_t DWord;
#endif
const int WORD_BITS = 8 * (int)sizeof(Word);
typedef std::vector<Word> Words;

// r[0, n) += a[0, n); returns the carry out
Word addWords(Word* r, const Word* a, std::size_t n) {
    Word c = 0;
    for (std::size_t i = 0; i < n; i++) {
        DWord t = (DWord)r[i] + a[i] + c;
        r[i] = (Word)t;
        c = (Word)(t >> WORD_BITS);
    }
    return c;
}

// r[0, n) -= a[0, n); returns the borrow out
Word subWords(Word* r, const Word* a, std::size_t n) {
    Word b = 0;
    for (std::size_t i = 0; i < n; i++) {
        Word x = r[i], y = a[i];
        r[i] = x - y - b;
        b = (x < y || (x == y && b)) ? 1 : 0;
    }
    return b;
}

// c added at r[0] and carried up through r[0, n); returns what carries out
Word rippleWords(Word* r, std::size_t n, Word c) {
    for (std::size_t i = 0; c && i < n; i++) { r[i] += c; c = r[i] < c ? 1 : 0; }
    return c;
}

// r[0, na + nb) = a·b
void mulSchool(Word* r, const Word* a, std::size_t na, const Word* b, std::size_t nb) {
    std::fill(r, r + na + nb, (Word)0);
    for (std::size_t i = 0; i < na; i++) {
        Word c = 0;
        for (std::size_t j = 0; j < nb; j++) {
            DWord t = (DWord)a[i] * b[j] + r[i + j] + c;
            r[i + j] = (Word)t;
            c = (Word)(t >> WORD_BITS);
        }
        r[i + nb] = c;
    }
}

// d = |x - y| over n words; true when x < y
bool absDiff(Word* d, const Word* x, const Word* y, std::size_t n) {
    std::size_t i = n;
    while (i > 0 && x[i - 1] == y[i - 1]) i--;
    const bool neg = i > 0 && x[i - 1] < y[i - 1];
    if (neg) std::swap(x, y);
    std::copy(x, x + n, d);
    subWords(d, y, n);
    return neg;
}

// Below this many words a schoolbook product beats splitting it. Converting
// 33,448 random limbs, anything from 16 to 28 measured the same (23.5 ms on an
// M1, 64-bit words); 40 was 8% slower and 64 was 38% slower.
const std::size_t KARATSUBA_MIN = 24;

std::size_t karatsubaScratch(std::size_t n) {
    if (n < KARATSUBA_MIN) return 0;
    const std::size_t h = (n + 1) / 2;
    return 4 * h + std::max(2 * h + 1, karatsubaScratch(h));
}

// r[0, 2n) = a·b for two n-word operands, s scratch of karatsubaScratch(n)
// words. With a = a1·W^h + a0 and b likewise, the middle term costs ONE more
// half-size product:
//     a0·b1 + a1·b0 = a0·b0 + a1·b1 - (a0 - a1)(b0 - b1)
// The subtractive form, because its factors stay h words where the additive
// (a0 + a1)(b0 + b1) grows a carry word.
void karatsuba(Word* r, const Word* a, const Word* b, std::size_t n, Word* s) {
    if (n < KARATSUBA_MIN) { mulSchool(r, a, n, b, n); return; }
    const std::size_t h = (n + 1) / 2, l = n - h;   // low halves h words, high l <= h
    Word* da = s;
    Word* db = s + h;
    Word* t = s + 2 * h;       // 2h words
    Word* rest = s + 4 * h;    // the recursion's scratch, then the middle term
    std::copy(a + h, a + n, t);
    if (l < h) t[l] = 0;       // a1, zero-extended to h words
    const bool negA = absDiff(da, a, t, h);
    std::copy(b + h, b + n, t);
    if (l < h) t[l] = 0;
    const bool negB = absDiff(db, b, t, h);
    karatsuba(r, a, b, h, rest);                   // a0·b0 -> r[0, 2h)
    karatsuba(r + 2 * h, a + h, b + h, l, rest);   // a1·b1 -> r[2h, 2n)
    karatsuba(t, da, db, h, rest);                 // |a0 - a1|·|b0 - b1|
    Word* mid = rest;                              // 2h + 1 words
    std::copy(r, r + 2 * h, mid);
    mid[2 * h] = 0;
    rippleWords(mid + 2 * l, 2 * h + 1 - 2 * l, addWords(mid, r + 2 * h, 2 * l));
    if (negA == negB) mid[2 * h] -= subWords(mid, t, 2 * h);
    else mid[2 * h] += addWords(mid, t, 2 * h);
    // the middle term lands at W^h; the product fits 2n words, so whatever
    // of it would reach past them is zero
    const std::size_t len = std::min(2 * h + 1, 2 * n - h);
    rippleWords(r + h + len, 2 * n - h - len, addWords(r + h, mid, len));
}

// a·b at any sizes: the longer operand in slices the length of the shorter,
// each slice a balanced product added in at its offset
Words mulWords(const Words& a, const Words& b) {
    if (a.empty() || b.empty()) return Words();
    const Words& x = a.size() >= b.size() ? a : b;
    const Words& y = a.size() >= b.size() ? b : a;
    const std::size_t nx = x.size(), ny = y.size();
    Words r(nx + ny, 0);
    if (ny < KARATSUBA_MIN) {
        mulSchool(r.data(), x.data(), nx, y.data(), ny);
    } else {
        Words scratch(karatsubaScratch(ny)), prod(2 * ny), slice(ny);
        for (std::size_t at = 0; at < nx; at += ny) {
            const std::size_t k = std::min(ny, nx - at);
            const Word* xs = x.data() + at;
            if (k < ny) {
                std::copy(xs, xs + k, slice.begin());
                std::fill(slice.begin() + k, slice.end(), (Word)0);
                xs = slice.data();
            }
            karatsuba(prod.data(), xs, y.data(), ny, scratch.data());
            const std::size_t len = std::min(2 * ny, nx + ny - at);
            rippleWords(r.data() + at + len, nx + ny - at - len,
                        addWords(r.data() + at, prod.data(), len));
        }
    }
    while (!r.empty() && r.back() == 0) r.pop_back();
    return r;
}

// Base-1e9 limbs to binary: of(at, n) is the sum of L[at + i]·1e9^i for i < n,
// with no leading zero words (empty for zero).
class ToBinary {
public:
    explicit ToBinary(const uint32_t* limbs) : L_(limbs) {}

    Words of(std::size_t at, std::size_t n) {
        if (n <= HORNER_MAX) return horner(at, n);
        unsigned k = 0;
        while (((std::size_t)2 << k) < n) k++;     // 2^k < n <= 2^(k+1)
        const std::size_t h = (std::size_t)1 << k;
        Words lo = of(at, h);
        Words hi = of(at + h, n - h);
        return join(hi, power(k), lo);
    }

    // 1e9^(2^k)
    const Words& power(unsigned k) {
        if (pow_.empty()) pow_.push_back(Words(1, (Word)BigInt::BASE));
        while (pow_.size() <= k) pow_.push_back(mulWords(pow_.back(), pow_.back()));
        return pow_[k];
    }

    // hi·p + lo, for lo < p
    static Words join(const Words& hi, const Words& p, const Words& lo) {
        Words r = mulWords(hi, p);
        if (r.size() < lo.size()) r.resize(lo.size(), 0);
        if (rippleWords(r.data() + lo.size(), r.size() - lo.size(),
                        addWords(r.data(), lo.data(), lo.size())))
            r.push_back(1);
        return r;
    }

private:
    // A few dozen limbs are cheaper one at a time than split (32, 64 and 128
    // measured alike).
    static constexpr std::size_t HORNER_MAX = 64;

    Words horner(std::size_t at, std::size_t n) const {
        Words w;
        for (std::size_t i = n; i-- > 0;) {
            Word c = L_[at + i];
            for (Word& x : w) {
                DWord t = (DWord)x * BigInt::BASE + c;
                x = (Word)t;
                c = (Word)(t >> WORD_BITS);
            }
            if (c) w.push_back(c);
        }
        return w;
    }

    const uint32_t* L_;
    std::vector<Words> pow_;
};

// the index of the lowest set bit of a binary magnitude, -1 for zero
long long lowestBit(const Words& w) {
    for (std::size_t i = 0; i < w.size(); i++)
        if (w[i]) return (long long)i * WORD_BITS + ctzll(w[i]);
    return -1;
}

} // namespace

// The 1-based index of the highest set bit of |self|.
//
// The top three limbs T bracket the magnitude: it lies in [T·1e9^j, (T+1)·1e9^j)
// for j the limbs below them, so log2 of it is log2(T) + j·log2(1e9) to within
// log2(1 + 1/T) < 2e-18. The doubles add their own rounding, under 3e-16 of
// the result plus 2e-14, so `slack` below is thirty times what they can cost.
// Unless that window straddles an integer, its floor is the answer — for every
// magnitude but a sliver either side of a power of two (for a million bits,
// the ones within about one part in 10^8).
//
// Those are the ones people ask about, though — 2**N, 2**N - 1, and every
// negative power of two, since `msb` of -2**N measures 2**N - 1 — and no
// number of leading limbs can tell them apart. They convert to binary.
long long BigInt::bitLength() const {
    if (sign == 0) return 0;
    if (fitsU64()) return 64 - clzll(magU64(*this));
    const std::size_t m = mag.size();                // at least three limbs here
    static const double LOG2_1E9 = std::log2(1e9);
    const double top = ((double)mag[m - 1] * 1e9 + mag[m - 2]) * 1e9 + mag[m - 3];
    const double est = std::log2(top) + (double)(m - 3) * LOG2_1E9;
    const double slack = est * 1e-14 + 1e-11;
    const double below = std::floor(est - slack);
    if (below == std::floor(est + slack)) return (long long)below + 1;
    const Words w = ToBinary(mag.data()).of(0, m);
    return (long long)(w.size() - 1) * WORD_BITS + (64 - clzll(w.back()));
}

// The index of the lowest set bit of |self|: its trailing zero bits.
long long BigInt::lowestSetBit() const {
    if (sign == 0) return -1;
    // A zero limb is a factor of 1e9 = 2^9·5^9, nine trailing zero bits that
    // nothing above can disturb.
    std::size_t z = 0;
    while (mag[z] == 0) z++;
    const uint32_t* L = mag.data() + z;
    const std::size_t m = mag.size() - z;
    // The rest, mod 2^64. Limb i is scaled by 1e9^i = 2^9i·5^9i, so from limb
    // 8 on (2^72) nothing reaches the low 64 bits: limbs 0 to 7 decide them.
    uint64_t low = 0;
    for (std::size_t i = std::min<std::size_t>(m, 8); i-- > 0;)
        low = low * 1000000000ull + L[i];
    if (low) return 9 * (long long)z + ctzll(low);
    // Divisible by 2^64 — every power of two from there up is. The low 9t
    // bits are final once t limbs are converted, so convert 16, 32, 64, …
    // until one of them is set, extending the binary by one join each time.
    // That is exactly the divide-and-conquer split, so the worst case, a
    // power of two, costs one conversion in all.
    ToBinary bin(L);
    std::size_t t = 16;
    unsigned k = 4;                                  // 16 = 2^4
    Words lo = bin.of(0, std::min(t, m));
    for (;;) {
        const long long p = lowestBit(lo);
        if (t >= m || (p >= 0 && p < 9 * (long long)t)) return 9 * (long long)z + p;
        const Words hi = bin.of(t, std::min(t, m - t));
        lo = ToBinary::join(hi, bin.power(k), lo);
        t *= 2;
        k++;
    }
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
    return sign < 0 ? (long long)(0 - r) : (long long)r; // negate UNSIGNED, then convert: well-defined at LLONG_MIN (the signed negation was UB there)
}

unsigned long long BigInt::toU64Wrap() const {
    unsigned long long r = 0;   // unsigned overflow is defined: this is value mod 2^64
    for (int i = (int)mag.size() - 1; i >= 0; i--) r = r * (unsigned long long)BASE + mag[i];
    return sign < 0 ? (unsigned long long)0 - r : r;
}

double BigInt::toDouble() const {
    // up to two limbs (< 1e18) accumulate exactly in a double; beyond that a
    // per-limb accumulation rounds at every step and lands an ulp off from
    // 1e27 up ((2**200).Num), so let strtod round the decimal string ONCE —
    // toString is the cheap direction in base 1e9
    if (mag.size() <= 2) {
        double r = 0;
        for (int i = (int)mag.size() - 1; i >= 0; i--) r = r * (double)BASE + mag[i];
        return sign < 0 ? -r : r;
    }
    return cnum::strtod(toString().c_str(), nullptr);
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
