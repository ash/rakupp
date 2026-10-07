#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace rakupp {

// A BigInt's limbs: the part of std::vector<uint32_t> the arithmetic uses, with
// the first four limbs INLINE. A Rat is two BigInts, and nearly every Rat a
// program makes (0.01 * 7, 1/3) has a numerator and denominator under 10^36 —
// so each one used to cost two heap blocks for a limb apiece. Same 24 bytes as
// the vector; storage moves to the heap only past four limbs.
class LimbVec {
    static constexpr uint32_t kInline = 4;
    union { uint32_t inl_[kInline]; uint32_t* heap_; };
    uint32_t n_ = 0, cap_ = kInline;
    bool onHeap() const { return cap_ > kInline; }
    void grow(size_t want) {
        size_t nc = cap_ * 2;
        if (nc < want) nc = want;
        uint32_t* p = static_cast<uint32_t*>(::operator new(nc * sizeof(uint32_t)));
        std::memcpy(p, data(), n_ * sizeof(uint32_t));
        if (onHeap()) ::operator delete(heap_);
        heap_ = p;
        cap_ = (uint32_t)nc;
    }
public:
    LimbVec() {}
    LimbVec(const LimbVec& o) { assignFrom(o.data(), o.n_); }
    LimbVec(LimbVec&& o) noexcept : n_(o.n_), cap_(o.cap_) {
        if (o.onHeap()) { heap_ = o.heap_; o.cap_ = kInline; }
        else std::memcpy(inl_, o.inl_, sizeof inl_);
        o.n_ = 0;
    }
    LimbVec& operator=(const LimbVec& o) { if (this != &o) assignFrom(o.data(), o.n_); return *this; }
    LimbVec& operator=(LimbVec&& o) noexcept {
        if (this == &o) return *this;
        if (onHeap()) ::operator delete(heap_);
        n_ = o.n_; cap_ = o.cap_;
        if (o.onHeap()) { heap_ = o.heap_; o.cap_ = kInline; }
        else std::memcpy(inl_, o.inl_, sizeof inl_);
        o.n_ = 0;
        return *this;
    }
    ~LimbVec() { if (onHeap()) ::operator delete(heap_); }

    void assignFrom(const uint32_t* p, size_t n) {
        if (n > cap_) grow(n);
        std::memmove(data(), p, n * sizeof(uint32_t));
        n_ = (uint32_t)n;
    }
    uint32_t* data() { return onHeap() ? heap_ : inl_; }
    const uint32_t* data() const { return onHeap() ? heap_ : inl_; }
    size_t size() const { return n_; }
    bool empty() const { return n_ == 0; }
    uint32_t& operator[](size_t i) { return data()[i]; }
    const uint32_t& operator[](size_t i) const { return data()[i]; }
    uint32_t& back() { return data()[n_ - 1]; }
    const uint32_t& back() const { return data()[n_ - 1]; }
    uint32_t* begin() { return data(); }
    uint32_t* end() { return data() + n_; }
    const uint32_t* begin() const { return data(); }
    const uint32_t* end() const { return data() + n_; }
    void push_back(uint32_t v) { if (n_ == cap_) grow(n_ + 1); data()[n_++] = v; }
    void pop_back() { n_--; }
    void clear() { n_ = 0; }
    void resize(size_t n, uint32_t v = 0) {
        if (n > cap_) grow(n);
        for (size_t i = n_; i < n; i++) data()[i] = v;
        n_ = (uint32_t)n;
    }
    void assign(size_t n, uint32_t v) { n_ = 0; resize(n, v); }
    // the two positional edits the arithmetic makes: a limb in FRONT (long
    // division's running remainder) and a run off the front (a shift)
    void insert(uint32_t* pos, uint32_t v) {
        const size_t at = (size_t)(pos - data());
        if (n_ == cap_) grow(n_ + 1);
        std::memmove(data() + at + 1, data() + at, (n_ - at) * sizeof(uint32_t));
        data()[at] = v;
        n_++;
    }
    void erase(uint32_t* first, uint32_t* last) {
        const size_t a = (size_t)(first - data()), b = (size_t)(last - data());
        std::memmove(data() + a, data() + b, (n_ - b) * sizeof(uint32_t));
        n_ -= (uint32_t)(b - a);
    }
    bool operator==(const LimbVec& o) const {
        return n_ == o.n_ && std::memcmp(data(), o.data(), n_ * sizeof(uint32_t)) == 0;
    }
    bool operator!=(const LimbVec& o) const { return !(*this == o); }
};

// Arbitrary-precision signed integer. Magnitude stored base 1e9, little-endian.
struct BigInt {
    int sign = 0;                 // -1, 0, +1
    LimbVec mag;                  // little-endian limbs, base 1e9, no leading zeros
    static const uint32_t BASE = 1000000000u;

    BigInt() {}
    BigInt(long long v);
    static BigInt fromString(const std::string& s); // decimal, optional leading sign

    bool isZero() const { return sign == 0; }
    void trim();                  // drop leading-zero limbs, fix sign

    static int cmpMag(const BigInt& a, const BigInt& b);
    static int cmp(const BigInt& a, const BigInt& b);

    static BigInt addMag(const BigInt& a, const BigInt& b);
    static BigInt subMag(const BigInt& a, const BigInt& b); // |a| >= |b|

    BigInt operator-() const;
    BigInt operator+(const BigInt& o) const;
    BigInt operator-(const BigInt& o) const;
    BigInt operator*(const BigInt& o) const;
    // `*this` times a SINGLE limb (m < BASE), written back over the existing
    // magnitude. `$f *= $_` is a running product whose accumulator is thousands
    // of limbs long, and building the answer somewhere else means allocating that
    // many limbs, zero-filling them, and freeing the old ones once per step — all
    // of it the same order as the multiply. Sign is left alone: the caller owns
    // it, because it also owns the multiplier's.
    void mulLimbInPlace(uint32_t m);
    // truncated division (toward zero) + remainder with sign of dividend
    static void divmod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r);

    BigInt abs() const { BigInt c = *this; if (c.sign < 0) c.sign = 1; return c; }
    void makeAbs() { if (sign < 0) sign = 1; }  // abs() without copying the magnitude
    BigInt pow(long long e) const;
    static BigInt gcd(BigInt a, BigInt b);
    // Position of the highest set bit, 1-based (0 for zero) — what Rakudo's
    // "Cannot unbox N bit wide bigint" message counts, and S02-types/declare.t
    // asserts the N; `.msb` is this minus one. Read off the top limbs when that
    // is provably exact, which is every magnitude not within a hair of a power
    // of two; the rest are converted to binary.
    long long bitLength() const;
    // Position of the lowest set bit of the magnitude, counting from 0 — the
    // number of trailing zero bits, which is `.lsb`; -1 for zero. Read off the
    // low limbs unless the magnitude is divisible by a large power of two.
    long long lowestSetBit() const;
    // The magnitude in binary: 64-bit words, least significant first, no
    // leading zero word, empty for zero. With `words`, only the low that many
    // (the magnitude mod 2**(64·words)), converted from only the limbs that
    // reach them. Both directions are divide and conquer with Karatsuba
    // joins, so a million bits converts in tens of milliseconds.
    std::vector<uint64_t> toBinary(size_t words = SIZE_MAX) const;
    // The inverse: n binary words (least significant first; leading zero
    // words are fine) as a BigInt, negative when `sign` is. Zero words are
    // zero whatever the sign.
    static BigInt fromBinary(const uint64_t* words, size_t n, int sign);

    bool fitsLL() const;
    // magnitude fits in a uint64 (Raku caps Rat denominators at uint64;
    // arithmetic producing a larger one spills to Num)
    bool fitsU64() const {
        if (mag.size() > 3) return false;
        if (mag.size() < 3) return true;
        // limbs are base 1e9: value = m2·1e18 + m1·1e9 + m0 vs 18446744073709551615
        if (mag[2] != 18u) return mag[2] < 18u;
        if (mag[1] != 446744073u) return mag[1] < 446744073u;
        return mag[0] <= 709551615u;
    }
#if defined(__SIZEOF_INT128__)
    // magnitude fits in an unsigned __int128. Four base-1e9 limbs top out at
    // 1e36-1, comfortably under 2^128; only a five-limb value needs the compare
    // against 2^128-1 = 340282366920938463463374607431768211455.
    bool fitsU128() const {
        if (mag.size() > 5) return false;
        if (mag.size() < 5) return true;
        if (mag[4] != 340u) return mag[4] < 340u;
        if (mag[3] != 282366920u) return mag[3] < 282366920u;
        if (mag[2] != 938463463u) return mag[2] < 938463463u;
        if (mag[1] != 374607431u) return mag[1] < 374607431u;
        return mag[0] <= 768211455u;
    }
#endif
    long long toLL() const;
    // The low 64 bits, two's complement, WRAPPING rather than saturating. toLL()
    // deliberately saturates because its callers are indices and codepoints, where
    // a silent wrap is garbage; a native uint64/int64 container is the opposite
    // case — it is defined to keep the low bits, and saturating there turned every
    // 64-bit digest word into 0x7FFF_FFFF_FFFF_FFFF.
    unsigned long long toU64Wrap() const;
    double toDouble() const;
    std::string toString() const;
};

// A WORK BUDGET for one operation that could otherwise run for hours — the
// interpreter sets it around `**`: the limb products multiplication may still
// spend (all of them are made in its schoolbook kernel, Karatsuba's leaves
// included), or null for no limit. Spending past it throws
// BigIntBudgetExceeded, from the middle of a multiplication if need be.
extern thread_local unsigned long long* g_bigIntBudget;
struct BigIntBudgetExceeded {};

// (n/d)**e as the nearest double, never building the exact powers: each factor
// keeps its top six limbs (54 digits) through the squarings, which leaves the
// ratio good to some 45 significant digits whatever the exponent — far past
// the 17 a double holds. Overflow is Inf and underflow 0, signed. d is nonzero.
double bigRatioPowToDouble(const BigInt& n, const BigInt& d, unsigned long long e);

} // namespace rakupp
