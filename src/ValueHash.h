// ValueHash — the hash payload behind `Value.hash` (and the method/attr
// tables): a compact, insertion-ordered hash map in the perl 5 mold
// (docs/dev/findings/PERL5-TECHNIQUES.md, item 6).
//
// What it fixes: the payload used to be std::map<std::string, Value> — a
// red-black tree, O(log n) per touch with a full string comparison at every
// level and a 344-byte Value in every node. Perl computes a key's hash once,
// stores it beside the key, and compares hashes before bytes; lookups are
// O(1) probes that usually decide on a single integer compare.
//
// Layout (Python-dict style): entries live densely in insertion order; a
// separate power-of-two index of slot numbers does the probing. Two contracts
// carried over from std::map that the interpreter genuinely relies on:
//
//   * REFERENCE STABILITY. rtIndexRef and the lvalue paths hold `Value&`
//     into the payload across further inserts (autovivification). Entries
//     therefore live in a deque (push_back never moves existing elements)
//     and erase only marks — an entry is never moved or destroyed until
//     clear(). A long-lived hash that churns keys carries its tombstones;
//     that is the price of stable references, same as perl's lazy deletes.
//
//   * ITERATION over a `pair<const std::string, Value>` shape — `kv.first`,
//     `it->second` — skipping dead entries, in insertion order. (Sorted
//     output — gist/raku — sorts explicitly at the printing site, which is
//     what Rakudo does; iteration order itself is spec-unordered.)
//
// The converting constructor from std::map preserves the map's sorted order
// as insertion order, so the deliberate sorted locals (Capture nameds and
// friends) keep their semantics when copied into a payload.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace rakupp {

class ValueHash {
public:
    using value_type = std::pair<const std::string, Value>;

private:
    // Entry: the exposed pair (const key — nobody may rewrite a key in
    // place, the index holds its hash) plus the bookkeeping beside it.
    struct Entry {
        std::pair<const std::string, Value> kv;
        uint64_t h;
        bool dead = false;
        Entry(std::string k, Value v, uint64_t hh)
            : kv(std::move(k), std::move(v)), h(hh) {}
    };

    static constexpr int32_t EMPTY = -1;
    static constexpr int32_t TOMB  = -2;

    std::deque<Entry> entries_;    // insertion order; holes stay (dead flag)
    std::vector<int32_t> index_;   // power-of-two probe table of entry numbers
    size_t live_ = 0;              // entries not dead
    size_t used_ = 0;              // index slots not EMPTY (live + tombstones)

    static uint64_t hashKey(const std::string& k) { return std::hash<std::string>{}(k); }

    size_t mask() const { return index_.size() - 1; }

    void rehash(size_t want) {
        size_t cap = 8;
        while (cap < want * 2) cap <<= 1;
        index_.assign(cap, EMPTY);
        used_ = 0;
        for (size_t i = 0; i < entries_.size(); i++) {
            if (entries_[i].dead) continue;
            size_t s = entries_[i].h & mask();
            while (index_[s] != EMPTY) s = (s + 1) & mask();
            index_[s] = (int32_t)i;
            used_++;
        }
    }

    // The probe slot holding `key`, or the first insertable slot (EMPTY or
    // the earliest tombstone on the path) when absent.
    int32_t findSlot(const std::string& key, uint64_t h, size_t* insertAt = nullptr) const {
        if (index_.empty()) { if (insertAt) *insertAt = SIZE_MAX; return -1; }
        size_t s = h & mask();
        size_t firstTomb = SIZE_MAX;
        for (;;) {
            int32_t e = index_[s];
            if (e == EMPTY) {
                if (insertAt) *insertAt = firstTomb != SIZE_MAX ? firstTomb : s;
                return -1;
            }
            if (e == TOMB) {
                if (firstTomb == SIZE_MAX) firstTomb = s;
            }
            else if (entries_[e].h == h && entries_[e].kv.first == key)
                return (int32_t)s;
            s = (s + 1) & mask();
        }
    }

    Value& insertNew(const std::string& key, uint64_t h, Value v, size_t slot) {
        if (index_.empty() || (used_ + 1) * 4 > index_.size() * 3) {
            rehash(live_ + 1);
            size_t s2;
            findSlot(key, h, &s2);
            slot = s2;
        }
        entries_.emplace_back(key, std::move(v), h);
        if (index_[slot] == EMPTY) used_++;   // a tombstone reused does not grow `used_`
        index_[slot] = (int32_t)(entries_.size() - 1);
        live_++;
        return entries_.back().kv.second;
    }

public:
    ValueHash() = default;
    ValueHash(const ValueHash& o) { *this = o; }
    ValueHash& operator=(const ValueHash& o) {
        if (this == &o) return *this;
        entries_.clear(); index_.clear(); live_ = used_ = 0;
        for (const auto& e : o.entries_)
            if (!e.dead) (*this)[e.kv.first] = e.kv.second;
        return *this;
    }
    ValueHash(ValueHash&&) = default;
    ValueHash& operator=(ValueHash&&) = default;
    // Bridge from the deliberate sorted locals: sorted order becomes
    // insertion order, so downstream iteration keeps what the caller built.
    ValueHash(const std::map<std::string, Value>& m) {
        for (const auto& kv : m) (*this)[kv.first] = kv.second;
    }

    template <bool Const>
    class iter {
        using Owner = std::conditional_t<Const, const ValueHash, ValueHash>;
        Owner* m_ = nullptr;
        size_t i_ = 0;
        void skip() { while (m_ && i_ < m_->entries_.size() && m_->entries_[i_].dead) i_++; }
        friend class ValueHash;
    public:
        iter() = default;
        iter(Owner* m, size_t i) : m_(m), i_(i) { skip(); }
        // const_iterator constructible from iterator, as with std::map
        template <bool C2, typename = std::enable_if_t<Const && !C2>>
        iter(const iter<C2>& o) : m_(o.m_), i_(o.i_) {}
        template <bool C2> friend class iter;

        using ref = std::conditional_t<Const, const std::pair<const std::string, Value>&,
                                              std::pair<const std::string, Value>&>;
        using ptr = std::conditional_t<Const, const std::pair<const std::string, Value>*,
                                              std::pair<const std::string, Value>*>;
        // std::iterator_traits (std::advance in ExtCtx.h walks these)
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<const std::string, Value>;
        using difference_type = std::ptrdiff_t;
        using pointer = ptr;
        using reference = ref;
        ref operator*() const { return m_->entries_[i_].kv; }
        ptr operator->() const { return &m_->entries_[i_].kv; }
        iter& operator++() { i_++; skip(); return *this; }
        iter operator++(int) { iter t = *this; ++*this; return t; }
        bool operator==(const iter& o) const { return m_ == o.m_ && i_ == o.i_; }
        bool operator!=(const iter& o) const { return !(*this == o); }
    };
    using iterator = iter<false>;
    using const_iterator = iter<true>;

    iterator begin() { return iterator(this, 0); }
    iterator end() { return iterator(this, entries_.size()); }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, entries_.size()); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    size_t size() const { return live_; }
    bool empty() const { return live_ == 0; }
    void clear() { entries_.clear(); index_.clear(); live_ = used_ = 0; }

    iterator find(const std::string& key) {
        int32_t s = findSlot(key, hashKey(key));
        return s < 0 ? end() : iterator(this, (size_t)index_[s]);
    }
    const_iterator find(const std::string& key) const {
        int32_t s = findSlot(key, hashKey(key));
        return s < 0 ? end() : const_iterator(this, (size_t)index_[s]);
    }
    size_t count(const std::string& key) const { return findSlot(key, hashKey(key)) >= 0 ? 1 : 0; }

    Value& operator[](const std::string& key) {
        uint64_t h = hashKey(key);
        size_t slot;
        int32_t s = findSlot(key, h, &slot);
        if (s >= 0) return entries_[index_[s]].kv.second;
        return insertNew(key, h, Value{}, slot);
    }

    Value& at(const std::string& key) {
        int32_t s = findSlot(key, hashKey(key));
        if (s < 0) throw std::out_of_range("ValueHash::at: " + key);
        return entries_[index_[s]].kv.second;
    }
    const Value& at(const std::string& key) const {
        int32_t s = findSlot(key, hashKey(key));
        if (s < 0) throw std::out_of_range("ValueHash::at: " + key);
        return entries_[index_[s]].kv.second;
    }

    size_t erase(const std::string& key) {
        int32_t s = findSlot(key, hashKey(key));
        if (s < 0) return 0;
        entries_[index_[s]].dead = true;
        index_[s] = TOMB;   // stays `used_` — the probe path must not break
        live_--;
        return 1;
    }
    iterator erase(iterator it) {
        size_t i = it.i_;
        if (i < entries_.size() && !entries_[i].dead) erase(entries_[i].kv.first);
        return iterator(this, i + 1);
    }

    std::pair<iterator, bool> insert(const value_type& kv) {
        uint64_t h = hashKey(kv.first);
        size_t slot;
        int32_t s = findSlot(kv.first, h, &slot);
        if (s >= 0) return {iterator(this, (size_t)index_[s]), false};
        insertNew(kv.first, h, kv.second, slot);
        return {iterator(this, entries_.size() - 1), true};
    }
    template <typename... A>
    std::pair<iterator, bool> emplace(const std::string& k, A&&... a) {
        return insert(value_type(k, Value(std::forward<A>(a)...)));
    }
};

using ValueMap = ValueHash;

} // namespace rakupp
