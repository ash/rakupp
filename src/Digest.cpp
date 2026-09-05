// Digest.cpp — see Digest.h for the contract and for why this is a separate
// implementation from Digest::Native's C rather than a port of it.
//
// The three block functions are the textbook ones, written against the
// specifications and gated against t/vectors/digest.vec, which is generated
// from the system openssl and never from this code. SHA-224 is the SHA-256 core
// with a different IV and a truncated output, and SHA-384 stands in the same
// relation to SHA-512; that is why one `block` covers six algorithms.
//
// Explicit byte loads and stores throughout. The specifications are written in
// bytes, and a host that happens to share an algorithm's endianness is a
// portability accident rather than a licence to memcpy a word.

#include "Digest.h"

#include <cstring>

namespace rakupp {

namespace {

inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

inline uint32_t ld32be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
inline uint32_t ld32le(const uint8_t* p) {
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}
inline uint64_t ld64be(const uint8_t* p) {
    return ((uint64_t)ld32be(p) << 32) | ld32be(p + 4);
}
inline void st32be(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
inline void st32le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
inline void st64be(uint8_t* p, uint64_t v) {
    st32be(p, (uint32_t)(v >> 32)); st32be(p + 4, (uint32_t)v);
}

// ---- MD5 (RFC 1321) -------------------------------------------------------

const uint32_t MD5_K[64] = {
    0xd76aa478u,0xe8c7b756u,0x242070dbu,0xc1bdceeeu,0xf57c0fafu,0x4787c62au,0xa8304613u,0xfd469501u,
    0x698098d8u,0x8b44f7afu,0xffff5bb1u,0x895cd7beu,0x6b901122u,0xfd987193u,0xa679438eu,0x49b40821u,
    0xf61e2562u,0xc040b340u,0x265e5a51u,0xe9b6c7aau,0xd62f105du,0x02441453u,0xd8a1e681u,0xe7d3fbc8u,
    0x21e1cde6u,0xc33707d6u,0xf4d50d87u,0x455a14edu,0xa9e3e905u,0xfcefa3f8u,0x676f02d9u,0x8d2a4c8au,
    0xfffa3942u,0x8771f681u,0x6d9d6122u,0xfde5380cu,0xa4beea44u,0x4bdecfa9u,0xf6bb4b60u,0xbebfbc70u,
    0x289b7ec6u,0xeaa127fau,0xd4ef3085u,0x04881d05u,0xd9d4d039u,0xe6db99e5u,0x1fa27cf8u,0xc4ac5665u,
    0xf4292244u,0x432aff97u,0xab9423a7u,0xfc93a039u,0x655b59c3u,0x8f0ccc92u,0xffeff47du,0x85845dd1u,
    0x6fa87e4fu,0xfe2ce6e0u,0xa3014314u,0x4e0811a1u,0xf7537e82u,0xbd3af235u,0x2ad7d2bbu,0xeb86d391u};
const unsigned MD5_S[64] = {
    7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
    5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
    4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
    6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21};

// Four loops of sixteen rather than one of sixty-four with a branch chain
// choosing f and g. Same rounds in the same order; the round function is a
// constant inside each loop, so it inlines instead of being selected.
void md5Block(uint32_t h[4], const uint8_t* p) {
    uint32_t m[16];
    for (int i = 0; i < 16; i++) m[i] = ld32le(p + i * 4);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
// f and g are computed BEFORE the shuffle. Expanding `f` after `d = c; c = b`
// reads the round's own new state and produces a plausible, entirely wrong
// digest — which is what the first version of this macro did.
#define MD5_STEP(f, g, i)                                                     \
    do {                                                                      \
        uint32_t f_ = (f);                                                    \
        int g_ = (g);                                                         \
        uint32_t t_ = d;                                                      \
        d = c; c = b;                                                         \
        b = b + rotl32(a + f_ + MD5_K[i] + m[g_], (int)MD5_S[i]);              \
        a = t_;                                                               \
    } while (0)
    for (int i = 0; i < 16; i++) MD5_STEP((b & c) | (~b & d), i, i);
    for (int i = 16; i < 32; i++) MD5_STEP((d & b) | (~d & c), (5 * i + 1) & 15, i);
    for (int i = 32; i < 48; i++) MD5_STEP(b ^ c ^ d, (3 * i + 5) & 15, i);
    for (int i = 48; i < 64; i++) MD5_STEP(c ^ (b | ~d), (7 * i) & 15, i);
#undef MD5_STEP
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
}

// ---- SHA-1 (FIPS 180-4) ---------------------------------------------------

// A ROLLING sixteen-word window, not the eighty-word schedule the standard is
// written with: w[i] depends only on w[i-3], w[i-8], w[i-14] and w[i-16], so
// sixteen live words are all there ever are.
//
// And unrolled five rounds at a time, which is not decoration. A SHA-1 round
// ends by shuffling five state words along by one; written as a loop that is
// five moves per round for eight bytes of work. Unrolled by five the shuffle
// disappears entirely — the five variables come back to their own roles after
// five rounds, so each round merely names them in a different order and the
// compiler renames rather than moves. Sixteen groups is the whole block, and
// the schedule words are written out rather than looped because the group
// boundary does not line up with round 16, where the message words stop and
// the mixed ones begin.
//
// 651 MB/s rolled, 1,105 unrolled, on 16 MB (arm64, 2026-09-05) — against 868
// for the distribution extension this is measured beside.
#define SHA1_F0(x, y, z) (((x) & ((y) ^ (z))) ^ (z))            // choose
#define SHA1_F1(x, y, z) ((x) ^ (y) ^ (z))                      // parity
#define SHA1_F2(x, y, z) ((((x) | (y)) & (z)) | ((x) & (y)))    // majority
#define SHA1_MIX(i) (w[(i)&15] = rotl32(w[((i)+13)&15] ^ w[((i)+8)&15] ^ \
                                        w[((i)+2)&15]  ^ w[(i)&15], 1))
// One group of five, with the state named in the five rotations it takes.
#define SHA1_5(F, K, W0, W1, W2, W3, W4)                                      \
    do {                                                                      \
        e += rotl32(a, 5) + F(b, c, d) + (K) + (W0); b = rotl32(b, 30);        \
        d += rotl32(e, 5) + F(a, b, c) + (K) + (W1); a = rotl32(a, 30);        \
        c += rotl32(d, 5) + F(e, a, b) + (K) + (W2); e = rotl32(e, 30);        \
        b += rotl32(c, 5) + F(d, e, a) + (K) + (W3); d = rotl32(d, 30);        \
        a += rotl32(b, 5) + F(c, d, e) + (K) + (W4); c = rotl32(c, 30);        \
    } while (0)

void sha1Block(uint32_t h[5], const uint8_t* p) {
    uint32_t w[16];
    for (int i = 0; i < 16; i++) w[i] = ld32be(p + i * 4);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    const uint32_t K0 = 0x5A827999u, K1 = 0x6ED9EBA1u,
                   K2 = 0x8F1BBCDCu, K3 = 0xCA62C1D6u;
    SHA1_5(SHA1_F0, K0, w[0],  w[1],  w[2],  w[3],  w[4]);
    SHA1_5(SHA1_F0, K0, w[5],  w[6],  w[7],  w[8],  w[9]);
    SHA1_5(SHA1_F0, K0, w[10], w[11], w[12], w[13], w[14]);
    SHA1_5(SHA1_F0, K0, w[15], SHA1_MIX(16), SHA1_MIX(17), SHA1_MIX(18), SHA1_MIX(19));
    SHA1_5(SHA1_F1, K1, SHA1_MIX(20), SHA1_MIX(21), SHA1_MIX(22), SHA1_MIX(23), SHA1_MIX(24));
    SHA1_5(SHA1_F1, K1, SHA1_MIX(25), SHA1_MIX(26), SHA1_MIX(27), SHA1_MIX(28), SHA1_MIX(29));
    SHA1_5(SHA1_F1, K1, SHA1_MIX(30), SHA1_MIX(31), SHA1_MIX(32), SHA1_MIX(33), SHA1_MIX(34));
    SHA1_5(SHA1_F1, K1, SHA1_MIX(35), SHA1_MIX(36), SHA1_MIX(37), SHA1_MIX(38), SHA1_MIX(39));
    SHA1_5(SHA1_F2, K2, SHA1_MIX(40), SHA1_MIX(41), SHA1_MIX(42), SHA1_MIX(43), SHA1_MIX(44));
    SHA1_5(SHA1_F2, K2, SHA1_MIX(45), SHA1_MIX(46), SHA1_MIX(47), SHA1_MIX(48), SHA1_MIX(49));
    SHA1_5(SHA1_F2, K2, SHA1_MIX(50), SHA1_MIX(51), SHA1_MIX(52), SHA1_MIX(53), SHA1_MIX(54));
    SHA1_5(SHA1_F2, K2, SHA1_MIX(55), SHA1_MIX(56), SHA1_MIX(57), SHA1_MIX(58), SHA1_MIX(59));
    SHA1_5(SHA1_F1, K3, SHA1_MIX(60), SHA1_MIX(61), SHA1_MIX(62), SHA1_MIX(63), SHA1_MIX(64));
    SHA1_5(SHA1_F1, K3, SHA1_MIX(65), SHA1_MIX(66), SHA1_MIX(67), SHA1_MIX(68), SHA1_MIX(69));
    SHA1_5(SHA1_F1, K3, SHA1_MIX(70), SHA1_MIX(71), SHA1_MIX(72), SHA1_MIX(73), SHA1_MIX(74));
    SHA1_5(SHA1_F1, K3, SHA1_MIX(75), SHA1_MIX(76), SHA1_MIX(77), SHA1_MIX(78), SHA1_MIX(79));
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}
#undef SHA1_5
#undef SHA1_MIX
#undef SHA1_F2
#undef SHA1_F1
#undef SHA1_F0

// ---- SHA-224 / SHA-256 (FIPS 180-4) ---------------------------------------

const uint32_t K256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};

// The same rolling sixteen-word window as SHA-1 above, for the same reason:
// w[i] needs only w[i-16], w[i-15], w[i-7] and w[i-2], so the schedule is
// accumulated in place rather than written out to sixty-four words of stack.
void sha256Block(uint32_t h[8], const uint8_t* p) {
    uint32_t w[16];
    for (int i = 0; i < 16; i++) w[i] = ld32be(p + i * 4);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
#define S256_MIX(i) (w[(i)&15] += (rotr32(w[((i)+1)&15], 7) ^ rotr32(w[((i)+1)&15], 18) ^ \
                                   (w[((i)+1)&15] >> 3)) + w[((i)+9)&15] +                \
                                  (rotr32(w[((i)+14)&15], 17) ^ rotr32(w[((i)+14)&15], 19) ^ \
                                   (w[((i)+14)&15] >> 10)))
#define S256_STEP(i, wv)                                                      \
    do {                                                                      \
        uint32_t S1_ = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);           \
        uint32_t ch_ = (e & f) ^ (~e & g);                                     \
        uint32_t t1_ = hh + S1_ + ch_ + K256[i] + (wv);                        \
        uint32_t S0_ = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);           \
        uint32_t mj_ = (a & b) ^ (a & c) ^ (b & c);                            \
        hh = g; g = f; f = e; e = d + t1_;                                     \
        d = c; c = b; b = a; a = t1_ + S0_ + mj_;                              \
    } while (0)
    for (int i = 0; i < 16; i++) S256_STEP(i, w[i]);
    for (int i = 16; i < 64; i++) S256_STEP(i, S256_MIX(i));
#undef S256_STEP
#undef S256_MIX
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

// ---- SHA-384 / SHA-512 (FIPS 180-4) ---------------------------------------

const uint64_t K512[80] = {
    0x428a2f98d728ae22ull,0x7137449123ef65cdull,0xb5c0fbcfec4d3b2full,0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull,0x59f111f1b605d019ull,0x923f82a4af194f9bull,0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull,0x12835b0145706fbeull,0x243185be4ee4b28cull,0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full,0x80deb1fe3b1696b1ull,0x9bdc06a725c71235ull,0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull,0xefbe4786384f25e3ull,0x0fc19dc68b8cd5b5ull,0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull,0x4a7484aa6ea6e483ull,0x5cb0a9dcbd41fbd4ull,0x76f988da831153b5ull,
    0x983e5152ee66dfabull,0xa831c66d2db43210ull,0xb00327c898fb213full,0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull,0xd5a79147930aa725ull,0x06ca6351e003826full,0x142929670a0e6e70ull,
    0x27b70a8546d22ffcull,0x2e1b21385c26c926ull,0x4d2c6dfc5ac42aedull,0x53380d139d95b3dfull,
    0x650a73548baf63deull,0x766a0abb3c77b2a8ull,0x81c2c92e47edaee6ull,0x92722c851482353bull,
    0xa2bfe8a14cf10364ull,0xa81a664bbc423001ull,0xc24b8b70d0f89791ull,0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull,0xd69906245565a910ull,0xf40e35855771202aull,0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull,0x1e376c085141ab53ull,0x2748774cdf8eeb99ull,0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull,0x4ed8aa4ae3418acbull,0x5b9cca4f7763e373ull,0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull,0x78a5636f43172f60ull,0x84c87814a1f0ab72ull,0x8cc702081a6439ecull,
    0x90befffa23631e28ull,0xa4506cebde82bde9ull,0xbef9a3f7b2c67915ull,0xc67178f2e372532bull,
    0xca273eceea26619cull,0xd186b8c721c0c207ull,0xeada7dd6cde0eb1eull,0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull,0x0a637dc5a2c898a6ull,0x113f9804bef90daeull,0x1b710b35131c471bull,
    0x28db77f523047d84ull,0x32caab7b40c72493ull,0x3c9ebe0a15c9bebcull,0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull,0x597f299cfc657e2aull,0x5fcb6fab3ad6faecull,0x6c44198c4a475817ull};

void sha512Block(uint64_t h[8], const uint8_t* p) {
    uint64_t w[16];
    for (int i = 0; i < 16; i++) w[i] = ld64be(p + i * 8);
    uint64_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint64_t e = h[4], f = h[5], g = h[6], hh = h[7];
#define S512_MIX(i) (w[(i)&15] += (rotr64(w[((i)+1)&15], 1) ^ rotr64(w[((i)+1)&15], 8) ^ \
                                   (w[((i)+1)&15] >> 7)) + w[((i)+9)&15] +               \
                                  (rotr64(w[((i)+14)&15], 19) ^ rotr64(w[((i)+14)&15], 61) ^ \
                                   (w[((i)+14)&15] >> 6)))
#define S512_STEP(i, wv)                                                      \
    do {                                                                      \
        uint64_t S1_ = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);          \
        uint64_t ch_ = (e & f) ^ (~e & g);                                     \
        uint64_t t1_ = hh + S1_ + ch_ + K512[i] + (wv);                        \
        uint64_t S0_ = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);          \
        uint64_t mj_ = (a & b) ^ (a & c) ^ (b & c);                            \
        hh = g; g = f; f = e; e = d + t1_;                                     \
        d = c; c = b; b = a; a = t1_ + S0_ + mj_;                              \
    } while (0)
    for (int i = 0; i < 16; i++) S512_STEP(i, w[i]);
    for (int i = 16; i < 80; i++) S512_STEP(i, S512_MIX(i));
#undef S512_STEP
#undef S512_MIX
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

inline bool is512(DigestAlgo a) { return a == DigestAlgo::SHA384 || a == DigestAlgo::SHA512; }
inline size_t blockSize(DigestAlgo a) { return is512(a) ? 128u : 64u; }

struct AlgoRow { const char* name; DigestAlgo algo; size_t len; };
const AlgoRow kAlgos[] = {
    { "md5",    DigestAlgo::MD5,    16 },
    { "sha1",   DigestAlgo::SHA1,   20 },
    { "sha224", DigestAlgo::SHA224, 28 },
    { "sha256", DigestAlgo::SHA256, 32 },
    { "sha384", DigestAlgo::SHA384, 48 },
    { "sha512", DigestAlgo::SHA512, 64 },
};

} // namespace

bool digestAlgoByName(const std::string& name, DigestAlgo& out) {
    for (auto& r : kAlgos) if (name == r.name) { out = r.algo; return true; }
    return false;
}
const char* digestAlgoName(DigestAlgo a) {
    for (auto& r : kAlgos) if (r.algo == a) return r.name;
    return "";
}
size_t digestLength(DigestAlgo a) {
    for (auto& r : kAlgos) if (r.algo == a) return r.len;
    return 0;
}
size_t digestBlockLength(DigestAlgo a) { return blockSize(a); }

void Digester::reset(DigestAlgo a) {
    algo_ = a;
    lenLo_ = lenHi_ = 0;
    n_ = 0;
    switch (a) {
    case DigestAlgo::MD5: {
        static const uint32_t iv[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
        std::memcpy(h32_, iv, sizeof iv);
        break;
    }
    case DigestAlgo::SHA1: {
        static const uint32_t iv[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
        std::memcpy(h32_, iv, sizeof iv);
        break;
    }
    case DigestAlgo::SHA224: {
        static const uint32_t iv[8] = {0xc1059ed8u, 0x367cd507u, 0x3070dd17u, 0xf70e5939u,
                                       0xffc00b31u, 0x68581511u, 0x64f98fa7u, 0xbefa4fa4u};
        std::memcpy(h32_, iv, sizeof iv);
        break;
    }
    case DigestAlgo::SHA256: {
        static const uint32_t iv[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                       0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        std::memcpy(h32_, iv, sizeof iv);
        break;
    }
    case DigestAlgo::SHA384: {
        static const uint64_t iv[8] = {0xcbbb9d5dc1059ed8ull, 0x629a292a367cd507ull,
                                       0x9159015a3070dd17ull, 0x152fecd8f70e5939ull,
                                       0x67332667ffc00b31ull, 0x8eb44a8768581511ull,
                                       0xdb0c2e0d64f98fa7ull, 0x47b5481dbefa4fa4ull};
        std::memcpy(h64_, iv, sizeof iv);
        break;
    }
    case DigestAlgo::SHA512: {
        static const uint64_t iv[8] = {0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull,
                                       0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
                                       0x510e527fade682d1ull, 0x9b05688c2b3e6c1full,
                                       0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};
        std::memcpy(h64_, iv, sizeof iv);
        break;
    }
    }
}

void Digester::block(const uint8_t* p) {
    switch (algo_) {
    case DigestAlgo::MD5:    md5Block(h32_, p); break;
    case DigestAlgo::SHA1:   sha1Block(h32_, p); break;
    case DigestAlgo::SHA224:
    case DigestAlgo::SHA256: sha256Block(h32_, p); break;
    case DigestAlgo::SHA384:
    case DigestAlgo::SHA512: sha512Block(h64_, p); break;
    }
}

void Digester::update(const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    const size_t B = blockSize(algo_);
    uint64_t before = lenLo_;
    lenLo_ += n;
    if (lenLo_ < before) lenHi_++;      // only SHA-512 can reach the carry
    while (n) {
        size_t take = B - n_;
        if (take > n) take = n;
        // A full block straight out of the caller's buffer, with no copy — the
        // common case for anything larger than one block.
        if (n_ == 0 && n >= B) {
            while (n >= B) { block(p); p += B; n -= B; }
            continue;                  // NOT break: n may still hold a tail
        }
        std::memcpy(buf_ + n_, p, take);
        n_ += take; p += take; n -= take;
        if (n_ == B) { block(buf_); n_ = 0; }
    }
}

std::string Digester::finish() {
    const size_t B = blockSize(algo_);
    const size_t lenField = is512(algo_) ? 16u : 8u;
    // MD5 counts its length little-endian; every SHA counts big-endian. That is
    // the only place the two families disagree about framing.
    const bool littleLen = (algo_ == DigestAlgo::MD5);
    uint64_t bitsLo = lenLo_ << 3;
    uint64_t bitsHi = (lenHi_ << 3) | (lenLo_ >> 61);

    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (n_ != B - lenField) update(&zero, 1);

    uint8_t tail[16];
    if (littleLen) { st32le(tail, (uint32_t)bitsLo); st32le(tail + 4, (uint32_t)(bitsLo >> 32)); }
    else if (lenField == 8) st64be(tail, bitsLo);
    else { st64be(tail, bitsHi); st64be(tail + 8, bitsLo); }
    update(tail, lenField);

    const size_t out = digestLength(algo_);
    std::string r(out, '\0');
    uint8_t* o = (uint8_t*)&r[0];
    if (is512(algo_)) for (size_t i = 0; i < (out + 7) / 8; i++) st64be(o + i * 8, h64_[i]);
    else if (littleLen) for (size_t i = 0; i < 4; i++) st32le(o + i * 4, h32_[i]);
    else for (size_t i = 0; i < (out + 3) / 4; i++) st32be(o + i * 4, h32_[i]);
    return r;
}

std::string digestBytes(DigestAlgo a, const std::string& data) {
    Digester d(a);
    d.update(data);
    return d.finish();
}

std::string digestHmac(DigestAlgo a, const std::string& key,
                       const std::string& msg, size_t blockLen) {
    size_t B = blockLen ? blockLen : blockSize(a);
    std::string k(B, '\0');
    if (key.size() > B) {
        std::string kh = digestBytes(a, key);
        std::memcpy(&k[0], kh.data(), kh.size() < B ? kh.size() : B);
    }
    else std::memcpy(&k[0], key.data(), key.size());

    std::string ipad(B, '\0'), opad(B, '\0');
    for (size_t i = 0; i < B; i++) {
        ipad[i] = (char)((uint8_t)k[i] ^ 0x36);
        opad[i] = (char)((uint8_t)k[i] ^ 0x5c);
    }
    Digester inner(a);
    inner.update(ipad);
    inner.update(msg);
    std::string id = inner.finish();

    Digester outer(a);
    outer.update(opad);
    outer.update(id);
    return outer.finish();
}

static std::string hexOf(const std::string& raw, const char* digits) {
    std::string s;
    s.reserve(raw.size() * 2);
    for (unsigned char c : raw) { s += digits[c >> 4]; s += digits[c & 0xF]; }
    return s;
}
std::string digestToHex(const std::string& raw) { return hexOf(raw, "0123456789abcdef"); }
std::string digestToHexUpper(const std::string& raw) { return hexOf(raw, "0123456789ABCDEF"); }

std::string sha1hex(const std::string& msg) {
    return digestToHexUpper(digestBytes(DigestAlgo::SHA1, msg));
}

} // namespace rakupp
