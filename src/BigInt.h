#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rakupp {

// Arbitrary-precision signed integer. Magnitude stored base 1e9, little-endian.
struct BigInt {
    int sign = 0;                 // -1, 0, +1
    std::vector<uint32_t> mag;    // little-endian limbs, base 1e9, no leading zeros
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
    // truncated division (toward zero) + remainder with sign of dividend
    static void divmod(const BigInt& a, const BigInt& b, BigInt& q, BigInt& r);

    BigInt abs() const { BigInt c = *this; if (c.sign < 0) c.sign = 1; return c; }
    BigInt pow(long long e) const;
    static BigInt gcd(BigInt a, BigInt b);

    bool fitsLL() const;
    long long toLL() const;
    double toDouble() const;
    std::string toString() const;
};

} // namespace rakupp
