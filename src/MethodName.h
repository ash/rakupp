#pragma once
// MName — the method name as the dispatch chain sees it.
//
// methodCallInner compares one name against several hundred string literals, so
// the comparison itself is hot: this caches the length and the first eight bytes,
// which turns most of those comparisons into an integer compare. Lifted out of
// Builtins.cpp when that dispatch chain was split across several translation
// units — every part needs the same type.
#include <cstdint>
#include <cstring>
#include <string>
#include <ostream>

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
inline std::string operator+(const char* a, const MName& b) { return a + b.s; }
inline std::string operator+(const std::string& a, const MName& b) { return a + b.s; }
inline std::string operator+(const MName& a, const char* b) { return a.s + b; }
inline std::string operator+(const MName& a, const std::string& b) { return a.s + b; }
inline std::ostream& operator<<(std::ostream& o, const MName& n) { return o << n.s; }
inline bool operator==(const std::string& a, const MName& b) { return a == b.s; }
inline bool operator!=(const std::string& a, const MName& b) { return a != b.s; }

} // namespace rakupp
