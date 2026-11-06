// AttrTable.h — an object's attributes (ObjectData::attrs): the interface of
// the ValueHash it replaces, over storage the object's class can lay out
// (docs/dev/plans/OBJECT-SLOTS-PLAN.md). Batch 2 is its table-only form: every
// key lives in `extra`, a ValueHash. An object made by makePayload has it in
// its own block, right behind the body (objectAdoptTail, Value.h): kept in a
// block of its own, it cost every object one more allocation, and building a
// RakuAST tree +5.7% in retired instructions, one malloc for each node. A
// table made any other way allocates its hash on the first store.
//
// Iteration yields a pair of references, `kv.first` the key and `kv.second`
// the value, not the ValueHash's own pair: in layout mode (batch 3) a key comes
// from the class's layout and its value from a slot, and there is no pair to
// point at. Bind it as `auto&&` or `const auto&` (`auto&` cannot bind it).
//
// Included by Value.h, after the ValueHash it wraps (Value is complete here).
#pragma once

#include "ValueHash.h"

#include <iterator>
#include <type_traits>

namespace rakupp {

class AttrTable {
    ValueHash* x_ = nullptr;   // `extra`: every key, in batch 2
    bool inBody_ = false;      // x_ is the hash behind the object's body, not a block of its own

    template <class... A>
    static ValueHash* fresh(A&&... a) { return ::new (SlabPool::alloc(sizeof(ValueHash))) ValueHash(std::forward<A>(a)...); }
    ValueHash& x() {
        if (!x_) x_ = fresh();
        return *x_;
    }
    void release() noexcept {
        if (!x_) return;
        x_->~ValueHash();
        if (!inBody_) SlabPool::dealloc(x_, sizeof(ValueHash));
        x_ = nullptr;
    }
    friend void objectAdoptTail(ObjectData& od, void* tail) noexcept;
    friend std::size_t objectTailOf(const ObjectData& od) noexcept;

public:
    // one entry, as iteration yields it
    struct Ref  { const std::string& first; Value& second; };
    struct CRef { const std::string& first; const Value& second; };

    template <bool Const>
    class iter {
        using Base = std::conditional_t<Const, ValueHash::const_iterator, ValueHash::iterator>;
        using R = std::conditional_t<Const, CRef, Ref>;
        Base b_;   // default-constructed (no hash) is the end of a table with no `extra`
        friend class AttrTable;
        explicit iter(Base b) : b_(b) {}
    public:
        iter() = default;
        // const_iterator from iterator, as with ValueHash
        template <bool C2, typename = std::enable_if_t<Const && !C2>>
        iter(const iter<C2>& o) : b_(o.b_) {}
        template <bool C2> friend class iter;

        // `it->second`: the entry is a value, so the arrow holds one
        struct Arrow {
            R r;
            const R* operator->() const { return &r; }
        };
        using iterator_category = std::forward_iterator_tag;
        using value_type = R;
        using difference_type = std::ptrdiff_t;
        using pointer = Arrow;
        using reference = R;

        R operator*() const { auto& kv = *b_; return R{kv.first, kv.second}; }
        Arrow operator->() const { return Arrow{**this}; }
        iter& operator++() { ++b_; return *this; }
        iter operator++(int) { iter t = *this; ++*this; return t; }
        bool operator==(const iter& o) const { return b_ == o.b_; }
        bool operator!=(const iter& o) const { return b_ != o.b_; }
    };
    using iterator = iter<false>;
    using const_iterator = iter<true>;

    AttrTable() = default;
    AttrTable(const AttrTable& o) : x_(o.x_ ? fresh(*o.x_) : nullptr) {}
    // a hash in another object's block stays there: its entries move instead
    AttrTable(AttrTable&& o) : x_(o.inBody_ ? fresh(std::move(*o.x_)) : o.x_) {
        if (!o.inBody_) o.x_ = nullptr;
    }
    AttrTable& operator=(const AttrTable& o) {
        if (this == &o) return *this;
        if (o.x_) x() = *o.x_;
        else if (x_) x_->clear();
        return *this;
    }
    AttrTable& operator=(AttrTable&& o) {
        if (this == &o) return *this;
        if (inBody_ || o.inBody_) {
            if (o.x_) x() = std::move(*o.x_);
            else if (x_) x_->clear();
        }
        else { release(); x_ = o.x_; o.x_ = nullptr; }
        return *this;
    }
    ~AttrTable() { release(); }

    iterator begin() { return x_ ? iterator(x_->begin()) : iterator(); }
    iterator end() { return x_ ? iterator(x_->end()) : iterator(); }
    const_iterator begin() const { return x_ ? const_iterator(std::as_const(*x_).begin()) : const_iterator(); }
    const_iterator end() const { return x_ ? const_iterator(std::as_const(*x_).end()) : const_iterator(); }

    size_t size() const { return x_ ? x_->size() : 0; }
    bool empty() const { return !x_ || x_->empty(); }
    // room for `n` entries, before anything is stored (an object's attribute count)
    void reserve(size_t n) { if (n) x().reserve(n); }
    void clear() { if (x_) x_->clear(); }

    iterator find(std::string_view key) { return x_ ? iterator(x_->find(key)) : iterator(); }
    const_iterator find(std::string_view key) const {
        return x_ ? const_iterator(std::as_const(*x_).find(key)) : const_iterator();
    }
    size_t count(std::string_view key) const { return x_ ? x_->count(key) : 0; }
    Value& operator[](std::string_view key) { return x()[key]; }
    Value& at(std::string_view key) {
        if (!x_) throw std::out_of_range(std::string("AttrTable::at: ").append(key));
        return x_->at(key);
    }
    const Value& at(std::string_view key) const {
        if (!x_) throw std::out_of_range(std::string("AttrTable::at: ").append(key));
        return std::as_const(*x_).at(key);
    }
    size_t erase(std::string_view key) { return x_ ? x_->erase(key) : 0; }
    template <typename... A>
    std::pair<iterator, bool> emplace(const std::string& k, A&&... a) {
        auto r = x().emplace(k, std::forward<A>(a)...);
        return {iterator(r.first), r.second};
    }

    // The cycle collector's view (Cycle.cpp hashEdges / breakBody): the
    // Values the table holds a count through, and whether a Pair over an
    // entry can still reach them from outside — ValueHash's, for its keys.
    template <class F>
    void forEachHeld(F&& f) const { if (x_) x_->forEachHeld(std::forward<F>(f)); }
    bool entriesAliased() const { return x_ && x_->entriesAliased(); }
};

} // namespace rakupp
