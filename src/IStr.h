// IStr — an interned string field: 8 bytes, trivially copyable, trivially
// destructible.
//
// Phase 1 of docs/dev/plans/REPRESENTATION-PLAN.md. `Value` carries four
// std::string members (hashKind/enumName/enumType/ofType) that are EMPTY on
// almost every value — but an empty std::string is not free. It is 24 bytes,
// and it is constructed, copied and destroyed on every one of the ~2M `Value`
// copies a 278 KB JSON parse makes. Measured there:
//
//   * `Value`'s implicit ctor/dtor/copy is 12.8% of the parse's self time, the
//     top line of the profile;
//   * `std::string == const char*` (which calls strlen, then memcmp) plus its
//     helpers is ~10% more — and `hashKind` is compared against a literal on
//     paths that run per parameter per call (see setupRwLinks).
//
// The strings that ever reach these fields are a small closed set in practice —
// container kinds and type names — so interning them makes a copy a pointer
// copy and a comparison a length test plus one 8-byte integer compare. That is
// exactly what MName (MethodName.h) already does for method names; this is the
// storable counterpart, and the operator surface is deliberately the same shape
// so the ~1,300 existing `.hashKind == "Buf"` / `.ofType.empty()` sites compile
// unchanged.
//
// The intern table is append-only and never freed. That is deliberate: entries
// are type names and kind tags, bounded by the program's own vocabulary, and a
// stable address is what makes the handle comparable by pointer.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <ostream>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace rakupp {

// Same definition MethodName.h uses; guarded so either header may come first.
#ifndef RAKUPP_ALWAYS_INLINE
#if defined(_MSC_VER)
#define RAKUPP_ALWAYS_INLINE __forceinline
#else
#define RAKUPP_ALWAYS_INLINE __attribute__((always_inline)) inline
#endif
#endif

struct IStr {
    struct Entry {
        std::string s;
        std::size_t n = 0;
        std::uint64_t pre = 0; // first 8 bytes packed, so short tags compare as one integer
    };

    // A 32-bit INDEX into the intern table (VALUE32-PLAN design A, batch 2):
    // half the pointer it replaced, and every Value carries three of them.
    // Zero IS the empty string — a default-constructed IStr is a zeroed word.
    std::uint32_t id = 0;

    IStr() = default;
    // EXPLICIT on purpose. Implicit converting constructors make `k == "Buf"`
    // ambiguous: the literal can convert to IStr just as readily as IStr can
    // convert to std::string, and both give a viable operator==. MName has the
    // same explicit constructor for the same reason.
    explicit IStr(const char* v) { assign(v, std::strlen(v)); }
    explicit IStr(const std::string& v) { assign(v.data(), v.size()); }

    static std::uint64_t pack(const char* p, std::size_t k) {
        std::uint64_t w = 0;
        for (std::size_t i = 0; i < k && i < 8; i++)
            w |= (std::uint64_t)(unsigned char)p[i] << (8 * i);
        return w;
    }

    static const std::string& empties() {
        static const std::string kEmpty;
        return kEmpty;
    }

    // The table: fixed-size chunks that never move once made, found through a
    // fixed array of chunk pointers, so an entry's address is stable and a
    // lookup is two dependent loads with no lock. An id is only ever handed
    // out after its entry is written, under the intern lock; an IStr reaches
    // another thread inside a Value, whose transfer already synchronizes.
    static constexpr unsigned kChunkBits = 10;
    static constexpr std::uint32_t kChunk = 1u << kChunkBits;
    static constexpr std::uint32_t kMaxChunks = 1u << 12;   // 4M names: far past any vocabulary
    static Entry** chunks() {
        static Entry* tbl[kMaxChunks] = {};
        return tbl;
    }
    RAKUPP_ALWAYS_INLINE static const Entry& entryOf(std::uint32_t i) {
        return chunks()[i >> kChunkBits][i & (kChunk - 1)];
    }
    const Entry& ent() const { return entryOf(id); }

    // Interning takes a lock, but only on ASSIGNMENT from text — copies and
    // comparisons, which are the operations `Value` does millions of times,
    // never reach it. Readers share the lock; only a miss serializes.
    static std::uint32_t intern(const char* p, std::size_t k) {
        if (!k) return 0;
        static std::unordered_map<std::string, std::uint32_t> index;
        static std::uint32_t next = 1;   // 0 is the empty string
        static std::shared_mutex mu;
        std::string key(p, k);
        {
            std::shared_lock<std::shared_mutex> rd(mu);
            auto it = index.find(key);
            if (it != index.end()) return it->second;
        }
        std::unique_lock<std::shared_mutex> wr(mu);
        auto it = index.find(key);
        if (it != index.end()) return it->second;   // raced; someone else won
        const std::uint32_t i = next;
        Entry*& chunk = chunks()[i >> kChunkBits];
        if (!chunk) {
            if ((i >> kChunkBits) >= kMaxChunks) std::abort();
            chunk = new Entry[kChunk];
        }
        chunk[i & (kChunk - 1)] = Entry{key, k, pack(p, k)};
        next = i + 1;
        index.emplace(std::move(key), i);
        return i;
    }

    void assign(const char* p, std::size_t k) { id = intern(p, k); }

    IStr& operator=(const char* v) { assign(v, std::strlen(v)); return *this; }
    IStr& operator=(const std::string& v) { assign(v.data(), v.size()); return *this; }

    // ALWAYS inlined for the same reason MName is: out of line the call costs
    // more than the comparison it replaces.
    template <std::size_t N>
    RAKUPP_ALWAYS_INLINE bool operator==(const char (&lit)[N]) const {
        if (!id) return N == 1;                      // empty == ""
        const Entry& e = ent();
        if (e.n != N - 1) return false;
        if (N - 1 <= 8) return e.pre == pack(lit, N - 1);
        return std::memcmp(e.s.data(), lit, N - 1) == 0;
    }
    template <std::size_t N>
    RAKUPP_ALWAYS_INLINE bool operator!=(const char (&lit)[N]) const { return !(*this == lit); }

    // Interned, so identity IS equality — no comparison at all.
    bool operator==(const IStr& o) const { return id == o.id; }
    bool operator!=(const IStr& o) const { return id != o.id; }

    bool operator==(const std::string& o) const { return str() == o; }
    bool operator!=(const std::string& o) const { return str() != o; }

    const std::string& str() const { return id ? ent().s : empties(); }
    operator const std::string&() const { return str(); }

    bool empty() const { return !id; }
    void clear() { id = 0; }
    std::size_t size() const { return id ? ent().n : 0; }
    const char* c_str() const { return str().c_str(); }
    const char* data() const { return str().data(); }
    char operator[](std::size_t i) const { return str()[i]; }
    std::size_t find(const char* n, std::size_t p = 0) const { return str().find(n, p); }
    std::size_t find(char c, std::size_t p = 0) const { return str().find(c, p); }
    std::size_t rfind(const char* n, std::size_t p = std::string::npos) const { return str().rfind(n, p); }
    std::size_t rfind(char c, std::size_t p = std::string::npos) const { return str().rfind(c, p); }
    std::string substr(std::size_t p = 0, std::size_t n = std::string::npos) const { return str().substr(p, n); }
    int compare(std::size_t p, std::size_t n, const char* o) const { return str().compare(p, n, o); }
};

inline bool operator==(const std::string& a, const IStr& b) { return a == b.str(); }
inline bool operator!=(const std::string& a, const IStr& b) { return a != b.str(); }
inline std::string operator+(const char* a, const IStr& b) { return a + b.str(); }
inline std::string operator+(const std::string& a, const IStr& b) { return a + b.str(); }
inline std::string operator+(const IStr& a, const char* b) { return a.str() + b; }
inline std::string operator+(const IStr& a, const std::string& b) { return a.str() + b; }
inline std::ostream& operator<<(std::ostream& o, const IStr& v) { return o << v.str(); }

} // namespace rakupp
