// StrMap — a string-keyed hash map looked up by std::string_view.
//
// A Value's text reads as a std::string_view (CowStr::str), and C++17's
// unordered containers take a lookup key only as `const key_type&` —
// heterogeneous lookup is C++20, and rakupp builds, and `--exe` compiles a
// user's program, as C++17. So a std::unordered_map<std::string, V> looked up
// by a Value's text needs a std::string of it: free while CowStr keeps one, a
// copy once it does not (VALUE32-PLAN design C keeps a short text in 16 bytes).
//
// This map is keyed by views instead, of texts it owns: a key is copied once,
// when it is inserted, into a deque whose elements never move, and every
// lookup reads the caller's text where it is. std::hash<std::string_view>
// hashes a text as std::hash<std::string> does ([string.view.hash]), so the
// buckets, and the iteration order, are the ones a std::string key gave.
//
// An erased key's text stays until the map goes: the tables this is for are
// registries that erase rarely, if ever.
#pragma once

#include <cstddef>
#include <deque>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace rakupp {

template <class V>
class StrMap {
    using Map = std::unordered_map<std::string_view, V>;
    Map m_;
    std::deque<std::string> keys_;   // the texts m_'s keys view
    std::string_view own(std::string_view k) { return keys_.emplace_back(k); }

public:
    using key_type = std::string_view;
    using mapped_type = V;
    using value_type = typename Map::value_type;
    using iterator = typename Map::iterator;
    using const_iterator = typename Map::const_iterator;

    StrMap() = default;
    // A deque's elements keep their addresses when it moves. (Not noexcept:
    // libstdc++'s deque allocates in its move constructor, and a defaulted
    // move declared noexcept against that would be deleted under C++17.)
    StrMap(StrMap&&) = default;
    StrMap& operator=(StrMap&&) = default;
    StrMap(const StrMap& o) { for (const auto& kv : o.m_) m_.emplace(own(kv.first), kv.second); }
    StrMap& operator=(const StrMap& o) {
        if (this != &o) { StrMap c(o); *this = std::move(c); }
        return *this;
    }

    iterator begin() { return m_.begin(); }
    iterator end() { return m_.end(); }
    const_iterator begin() const { return m_.begin(); }
    const_iterator end() const { return m_.end(); }
    size_t size() const { return m_.size(); }
    bool empty() const { return m_.empty(); }
    void clear() { m_.clear(); keys_.clear(); }

    iterator find(std::string_view k) { return m_.find(k); }
    const_iterator find(std::string_view k) const { return m_.find(k); }
    size_t count(std::string_view k) const { return m_.count(k); }
    V& operator[](std::string_view k) {
        auto it = m_.find(k);
        return it != m_.end() ? it->second : m_.emplace(own(k), V()).first->second;
    }
    V& at(std::string_view k) {
        auto it = m_.find(k);
        if (it == m_.end()) throw std::out_of_range("StrMap::at");
        return it->second;
    }
    const V& at(std::string_view k) const {
        auto it = m_.find(k);
        if (it == m_.end()) throw std::out_of_range("StrMap::at");
        return it->second;
    }
    template <class... A>
    std::pair<iterator, bool> emplace(std::string_view k, A&&... a) {
        auto it = m_.find(k);
        if (it != m_.end()) return {it, false};
        return m_.emplace(own(k), V(std::forward<A>(a)...));
    }
    template <class... A>
    std::pair<iterator, bool> try_emplace(std::string_view k, A&&... a) { return emplace(k, std::forward<A>(a)...); }
    size_t erase(std::string_view k) { return m_.erase(k); }
    iterator erase(const_iterator it) { return m_.erase(it); }
};

} // namespace rakupp
