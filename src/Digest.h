// Digest.h — MD5, SHA-1 and SHA-2, and HMAC over them (DATA-PLAN P3).
//
// TWIN: Digest::Native's src/digest.c, which is a SEPARATE implementation on
// purpose (NATIVE-MODULES-PLAN, "The architecture: independent C") — not a port,
// unlike DataCsv.cpp. What holds the two together is t/vectors/digest.vec, the
// openssl-generated vector file both sides read; a fix on either is not finished
// until the other has been checked against it. There are no deliberate
// differences today.
//
// This file is the ALGORITHMS only, with no Value and no interpreter in it, so
// that the two callers who need bytes-to-bytes hashing can share one
// implementation: the `digest` tag's primitives (src/DataDigest.cpp) and the
// Jupyter kernel, whose every message carries an HMAC-SHA256 signature. It also
// backs sha1hex(), which resolves module names against a Rakudo CURI index.
//
// Specifications: RFC 1321 (MD5), FIPS 180-4 (SHA-1, SHA-2), RFC 2104 (HMAC).
// No secret-dependent branches and no table lookups indexed by key material —
// control flow here depends only on the LENGTH of the input. That is a property
// to keep, not to rediscover.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rakupp {

enum class DigestAlgo { MD5, SHA1, SHA224, SHA256, SHA384, SHA512 };

// The tag's six names, in the order the primitives are registered. Returns
// false for anything else — SHA-3 and RIPEMD are Digest sub-modules the tag
// deliberately does not cover.
bool digestAlgoByName(const std::string& name, DigestAlgo& out);
const char* digestAlgoName(DigestAlgo a);
size_t digestLength(DigestAlgo a);   // 16, 20, 28, 32, 48 or 64
size_t digestBlockLength(DigestAlgo a);  // HMAC's B: 64 below SHA-384, 128 at and above

// Streaming, so a file is hashed as it is read rather than slurped whole.
class Digester {
public:
    explicit Digester(DigestAlgo a) { reset(a); }
    void reset(DigestAlgo a);
    void update(const void* p, size_t n);
    void update(const std::string& s) { update(s.data(), s.size()); }
    std::string finish();            // the raw digest, digestLength(algo) bytes

private:
    void block(const uint8_t* p);

    DigestAlgo algo_;
    uint32_t h32_[8];
    uint64_t h64_[8];
    uint64_t lenLo_ = 0, lenHi_ = 0;   // message length in BYTES; hi is for SHA-512
    uint8_t buf_[128];
    size_t n_ = 0;
};

std::string digestBytes(DigestAlgo a, const std::string& data);   // raw digest

// RFC 2104. `blockLen` of 0 means "the algorithm's own", which is what the RFC
// wants; a caller may pass a different B and it is honoured as written, because
// Digest::HMAC defaults every hash to 64 and a program reproducing its output
// has to be able to ask for that.
std::string digestHmac(DigestAlgo a, const std::string& key,
                       const std::string& msg, size_t blockLen = 0);

std::string digestToHex(const std::string& raw);        // lower case
std::string digestToHexUpper(const std::string& raw);

// SHA-1 as UPPERCASE hex — the spelling a Rakudo CURI `short/` index uses.
std::string sha1hex(const std::string& msg);

} // namespace rakupp
