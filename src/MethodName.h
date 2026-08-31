#pragma once
// MName — the method name as the dispatch chain sees it.
//
// methodCallInner compares one name against several hundred string literals, so
// the comparison itself is hot: this caches the length and the first eight bytes,
// which turns most of those comparisons into an integer compare. Lifted out of
// Builtins.cpp when that dispatch chain was split across several translation
// units — every part needs the same type.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <ostream>
#include <string>
#include <vector>

namespace rakupp {

#if defined(_MSC_VER)
#define RAKUPP_ALWAYS_INLINE __forceinline
#else
#define RAKUPP_ALWAYS_INLINE __attribute__((always_inline)) inline
#endif

struct MName {
    const std::string& s;
    std::size_t n;        // cached length: the first test on every comparison
    std::uint64_t pre;    // first 8 bytes, so short names compare as one integer
    // `self.Mu::Str` — dispatch PAST the invocant's own methods, straight to the
    // built-in behaviour the qualifier names. Without it the call re-entered the
    // very override that made it (Hash::Agnostic's `multi method Str(::?ROLE:U:)
    // { self.Mu::Str }` recursed until the stack ran out).
    bool skipOwn = false;
    explicit MName(const std::string& v) : s(v), n(v.size()), pre(pack(v.data(), v.size())) {}
    static std::uint64_t pack(const char* p, std::size_t k) {
        std::uint64_t w = 0;
        for (std::size_t i = 0; i < k && i < 8; i++)
            w |= (std::uint64_t)(unsigned char)p[i] << (8 * i);
        return w;
    }
    // ALWAYS inlined: out of line, the call itself costs more than the comparison,
    // and the enclosing function is far too large for clang to inline it by choice.
    template <std::size_t N>
    RAKUPP_ALWAYS_INLINE bool operator==(const char (&lit)[N]) const {
        if (n != N - 1) return false;
        if (N - 1 <= 8) return pre == pack(lit, N - 1);
        return std::memcmp(s.data(), lit, N - 1) == 0;
    }
    template <std::size_t N>
    RAKUPP_ALWAYS_INLINE bool operator!=(const char (&lit)[N]) const { return !(*this == lit); }
    bool operator==(const std::string& o) const { return s == o; }
    bool operator!=(const std::string& o) const { return s != o; }
    operator const std::string&() const { return s; }
    std::size_t size() const { return s.size(); }
    bool empty() const { return s.empty(); }
    const char* c_str() const { return s.c_str(); }
    const char* data() const { return s.data(); }
    char operator[](std::size_t i) const { return s[i]; }
    std::size_t find(const char* n, std::size_t p = 0) const { return s.find(n, p); }
    std::size_t find(char c, std::size_t p = 0) const { return s.find(c, p); }
    std::size_t rfind(const char* n, std::size_t p = std::string::npos) const { return s.rfind(n, p); }
    std::size_t rfind(char c, std::size_t p = std::string::npos) const { return s.rfind(c, p); }
    std::string substr(std::size_t p = 0, std::size_t n = std::string::npos) const { return s.substr(p, n); }
    int compare(std::size_t p, std::size_t n, const char* o) const { return s.compare(p, n, o); }
};
// A small set of SHORT method names (<= 8 characters), tested against the
// packed first-eight-bytes form MName already carries. A name that fits in
// eight bytes packs injectively — a name holds no NUL, so no two differ only
// past their length — which turns membership into a binary search over sorted
// integers instead of a std::set<std::string> tree walk with string compares
// at each node. That walk is what made every Str and Array method call ~45 ns
// slower once two such sets were consulted before the common handlers.
struct MNameSet8 {
    std::vector<std::uint64_t> keys;   // sorted packed names
    MNameSet8(std::initializer_list<const char*> names) {
        keys.reserve(names.size());
        for (const char* p : names) {
            std::size_t k = std::strlen(p);
            if (k > 8) continue;       // cannot be a member: the test rejects it below
            keys.push_back(MName::pack(p, k));
        }
        std::sort(keys.begin(), keys.end());
    }
    RAKUPP_ALWAYS_INLINE bool has(const MName& m) const {
        return m.n <= 8 && std::binary_search(keys.begin(), keys.end(), m.pre);
    }
};

inline std::string operator+(const char* a, const MName& b) { return a + b.s; }
inline std::string operator+(const std::string& a, const MName& b) { return a + b.s; }
inline std::string operator+(const MName& a, const char* b) { return a.s + b; }
inline std::string operator+(const MName& a, const std::string& b) { return a.s + b; }
inline std::ostream& operator<<(std::ostream& o, const MName& n) { return o << n.s; }
inline bool operator==(const std::string& a, const MName& b) { return a == b.s; }
inline bool operator!=(const std::string& a, const MName& b) { return a != b.s; }

} // namespace rakupp
