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

BigInt BigInt::operator*(const BigInt& o) const {
    if (sign == 0 || o.sign == 0) return BigInt();
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
    // long division on magnitudes, base 1e9, via binary search per limb
    BigInt cur;          // running remainder (magnitude, positive)
    BigInt babs = b.abs();
    q.mag.assign(a.mag.size(), 0);
    for (int i = (int)a.mag.size() - 1; i >= 0; i--) {
        // cur = cur*BASE + a.mag[i]
        cur.mag.insert(cur.mag.begin(), a.mag[i]);
        cur.sign = 1; cur.trim();
        // find largest x in [0,BASE) with babs*x <= cur
        uint32_t lo = 0, hi = BASE - 1, x = 0;
        while (lo <= hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            BigInt t = babs * BigInt((long long)mid);
            if (cmpMag(t, cur) <= 0) { x = mid; lo = mid + 1; }
            else { if (mid == 0) break; hi = mid - 1; }
        }
        q.mag[i] = x;
        cur = subMag(cur, babs * BigInt((long long)x));
    }
    q.sign = a.sign * b.sign;
    q.trim();
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
    a = a.abs(); b = b.abs();
    while (!b.isZero()) { BigInt q, r; divmod(a, b, q, r); a = b; b = r; }
    return a;
}

bool BigInt::fitsLL() const {
    if (mag.size() > 3) return false;
    // compare against 9.2e18; cheap: rebuild as long double-ish
    static const BigInt maxLL(9223372036854775807ll);
    if (sign >= 0) return cmpMag(*this, maxLL) <= 0;
    BigInt minAbs(0); minAbs = maxLL; // |min| = max+1, but max is safe enough
    return cmpMag(*this, maxLL) <= 0;
}

long long BigInt::toLL() const {
    long long r = 0;
    for (int i = (int)mag.size() - 1; i >= 0; i--) r = r * (long long)BASE + mag[i];
    return sign < 0 ? -r : r;
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
