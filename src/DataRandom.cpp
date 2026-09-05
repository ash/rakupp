// DataRandom.cpp — see DataRandom.h.

#include "DataRandom.h"
#include "Interpreter.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#  include <bcrypt.h>
#elif defined(__linux__)
#  include <sys/syscall.h>
#  include <unistd.h>
#else
#  include <sys/random.h>
#  include <unistd.h>
#endif

namespace rakupp {

namespace {

[[noreturn]] void rDie(const std::string& msg) {
    throw RakuError{Value::typeObj("X::AdHoc"), msg};
}

// The OS, and only the OS. Every arm here either fills the buffer or reports
// that it did not; there is no software fallback and there must not be one — a
// CSPRNG that quietly degrades to something predictable is worse than one that
// refuses, because the caller cannot tell.
bool osRandom(unsigned char* p, size_t n) {
    if (!n) return true;
#if defined(_WIN32)
    return BCryptGenRandom(nullptr, p, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
#  if defined(__linux__)
    // getrandom(2) may return a short read, and may not exist on a kernel older
    // than 3.17 — in which case /dev/urandom below is the answer.
    size_t got = 0;
    while (got < n) {
        long r = syscall(SYS_getrandom, p + got, n - got, 0);
        if (r > 0) { got += (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        break;
    }
    if (got == n) return true;
#  else
    // getentropy(2) is the macOS and BSD spelling, and takes at most 256 bytes
    // a call — so the loop is the interface, not a retry.
    size_t got = 0;
    bool ok = true;
    while (got < n && ok) {
        size_t take = n - got;
        if (take > 256) take = 256;
        ok = getentropy(p + got, take) == 0;
        if (ok) got += take;
    }
    if (got == n) return true;
#  endif
    // The portable floor. Opened per call rather than kept: a long-lived
    // descriptor is one a fork, an exec or a `close(2)` in user code can take
    // away, and this is not a hot path.
    FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) return false;
    size_t rd = std::fread(p, 1, n, f);
    std::fclose(f);
    return rd == n;
#endif
}

std::vector<unsigned char> randomBytes(size_t n, const char* who) {
    std::vector<unsigned char> b(n);
    if (!osRandom(b.data(), n))
        rDie(std::string(who) + ": the operating system would not supply random bytes");
    return b;
}

const Value* positional(ValueList& a, size_t which) {
    size_t seen = 0;
    for (auto& x : a) {
        if (x.t == VT::Pair && x.namedArg) continue;
        if (seen++ == which) return &x;
    }
    return nullptr;
}
size_t positionals(ValueList& a) {
    size_t n = 0;
    for (auto& x : a) if (!(x.t == VT::Pair && x.namedArg)) n++;
    return n;
}
void noAdverbs(ValueList& a, const char* who) {
    for (auto& x : a)
        if (x.t == VT::Pair && x.namedArg)
            rDie(std::string(who) + ": no such adverb :" + x.s.str());
}

// A non-negative integer from bytes, big-endian. Built through BigInt because
// eight bytes do not fit a long long once the top bit is set, and the reference
// answers a plain Raku Int of whatever size.
Value intFromBytes(const std::vector<unsigned char>& b) {
    BigInt v(0);
    for (unsigned char c : b) {
        v.mulLimbInPlace(256);
        v.sign = v.isZero() && c == 0 ? 0 : 1;
        v = v + BigInt((long long)c);
    }
    return Value::bigint(v);
}

// The value of a positional as a BigInt, whatever width it arrived in.
BigInt asBig(const Value& v) {
    if (v.t == VT::Int && v.big()) return *v.big();
    return BigInt(v.toInt());
}

// How many bytes it takes to represent every value below `upper`.
size_t bytesFor(const BigInt& upper) {
    BigInt n = upper - BigInt(1);
    size_t bytes = 0;
    BigInt q, r;
    while (!n.isZero()) {
        BigInt::divmod(n, BigInt(256), q, r);
        n = q;
        bytes++;
    }
    return bytes ? bytes : 1;
}

} // namespace

Value dataRandomBuf(Interpreter&, ValueList& a) {
    if (positionals(a) != 1) rDie("crypt_random_buf: expected exactly one argument, the length");
    noAdverbs(a, "crypt_random_buf");
    long long n = positional(a, 0)->toInt();
    if (n < 0) rDie("crypt_random_buf: length must not be negative, got " + std::to_string(n));
    // 16 MB is not the OS's limit, it is a sanity one: a `crypt_random_buf($n)`
    // with an $n that came from somewhere else should say so rather than try to
    // allocate whatever it was.
    if (n > 16 * 1024 * 1024)
        rDie("crypt_random_buf: " + std::to_string(n) + " bytes is more than this asks the OS for at once (16 MB)");
    auto b = randomBytes((size_t)n, "crypt_random_buf");
    Value v = Value::str(std::string((const char*)b.data(), b.size()));
    v.hashKind = "Buf";
    return v;
}

Value dataRandomInt(Interpreter&, ValueList& a) {
    size_t np = positionals(a);
    if (np > 1) rDie("crypt_random: expected ($size?)");
    noAdverbs(a, "crypt_random");
    long long size = 4;
    if (np == 1) {
        const Value* sv = positional(a, 0);
        if (rtIsDefined(*sv)) size = sv->toInt();
    }
    if (size < 1) rDie("crypt_random: $size must be at least one byte, got " + std::to_string(size));
    if (size > 4096) rDie("crypt_random: $size of " + std::to_string(size) + " bytes is beyond what this answers (4096)");
    return intFromBytes(randomBytes((size_t)size, "crypt_random"));
}

Value dataRandomUniform(Interpreter&, ValueList& a) {
    size_t np = positionals(a);
    if (np < 1 || np > 2) rDie("crypt_random_uniform: expected ($upper_bound, $size?)");
    noAdverbs(a, "crypt_random_uniform");
    BigInt upper = asBig(*positional(a, 0));
    if (upper.sign <= 0)
        rDie("crypt_random_uniform: $upper_bound must be positive");

    // The reference defaults $size to 4 and REJECTION-SAMPLES, so an upper
    // bound above 2**32 there never terminates — `crypt_random_uniform(2**40)`
    // hangs, probed 2026-09-05. Sizing the draw to the bound is what the caller
    // meant, and is the documented difference; an explicit $size is still
    // honoured as written, and refused only when it cannot represent the bound
    // at all, which is the same non-termination said out loud.
    size_t need = bytesFor(upper);
    size_t size = need;
    if (np == 2) {
        const Value* sv = positional(a, 1);
        if (rtIsDefined(*sv)) {
            long long s = sv->toInt();
            if (s < 1) rDie("crypt_random_uniform: $size must be at least one byte");
            if (s > 4096) rDie("crypt_random_uniform: $size of " + std::to_string(s) + " bytes is beyond what this answers (4096)");
            size = (size_t)s;
            if (size < need)
                rDie("crypt_random_uniform: $size of " + std::to_string(size) +
                     " byte(s) cannot reach an upper bound needing " + std::to_string(need) +
                     " — the draw would never be accepted");
        }
    }

    // Rejection sampling, not a modulus: `draw % $upper` is biased toward the
    // low values whenever $upper does not divide the range, and the whole point
    // of this sub is that it does not do that. `limit` is the largest multiple
    // of $upper that fits, and a draw at or above it is thrown away.
    BigInt range = BigInt(1);
    for (size_t i = 0; i < size; i++) range.mulLimbInPlace(256);
    range.sign = 1;
    BigInt q, r;
    BigInt::divmod(range, upper, q, r);
    BigInt limit = q * upper;                 // range - (range mod upper)

    for (;;) {
        Value drawn = intFromBytes(randomBytes(size, "crypt_random_uniform"));
        BigInt d = asBig(drawn);
        if (BigInt::cmp(d, limit) >= 0) continue;
        BigInt qq, rr;
        BigInt::divmod(d, upper, qq, rr);
        return Value::bigint(rr);
    }
}

} // namespace rakupp
