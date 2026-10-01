// ValueHash — the hash payload behind `Value.hash` (and the method/attr
// tables): a compact, insertion-ordered hash map in the perl 5 mold
// (docs/dev/findings/engines/PERL5-TECHNIQUES.md, item 6).
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
//     therefore live in a ChunkList (below: an append never moves existing
//     elements) and erase only marks — an entry is never moved or destroyed until
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

// A list whose elements never move once constructed (VALUEHASH-SMALL-PLAN.md):
// the one guarantee ValueHash took std::deque for. Storage is a run of chunks,
// each twice the one before, starting from a first chunk of `1 << shift_`
// elements — 4 by default, or what reserve() asked for. libc++'s deque block
// is 4 KB whatever the element, which is 24 entries of 168 bytes: every hash
// that received one key paid for 24, and every object's attribute table is a
// hash. The first chunk's pointer lives inline and later ones in `more_`,
// which a hash never allocates until it outgrows its first chunk.
template <class T>
class ChunkList {
    T* first_ = nullptr;
    std::vector<T*> more_;   // chunks 1, 2, … (chunk k holds cap0 << k elements)
    size_t n_ = 0;
    unsigned shift_ = 2;     // log2 of the first chunk's capacity

    // element i lives in chunk k at offset o, where chunk k starts at
    // cap0 * (2^k - 1): k = floor(log2(i / cap0 + 1))
    void locate(size_t i, size_t& k, size_t& o) const {
        const size_t q = (i >> shift_) + 1;
        k = 63 - (size_t)__builtin_clzll((unsigned long long)q);
        o = i - (((size_t(1) << k) - 1) << shift_);
    }
    T* chunk(size_t k) const { return k == 0 ? first_ : more_[k - 1]; }
    static T* allocChunk(size_t cap) { return static_cast<T*>(::operator new(cap * sizeof(T))); }
    void release() {
        for (size_t i = n_; i-- > 0; ) (*this)[i].~T();
        if (first_) ::operator delete(first_);
        for (T* c : more_) ::operator delete(c);
        first_ = nullptr; more_.clear(); n_ = 0;
    }

public:
    ChunkList() = default;
    ChunkList(const ChunkList&) = delete;
    ChunkList& operator=(const ChunkList&) = delete;
    ChunkList(ChunkList&& o) noexcept
        : first_(o.first_), more_(std::move(o.more_)), n_(o.n_), shift_(o.shift_) {
        o.first_ = nullptr; o.more_.clear(); o.n_ = 0;
    }
    ChunkList& operator=(ChunkList&& o) noexcept {
        if (this != &o) {
            release();
            first_ = o.first_; more_ = std::move(o.more_); n_ = o.n_; shift_ = o.shift_;
            o.first_ = nullptr; o.more_.clear(); o.n_ = 0;
        }
        return *this;
    }
    ~ChunkList() { release(); }

    size_t size() const { return n_; }
    T& operator[](size_t i) { size_t k, o; locate(i, k, o); return chunk(k)[o]; }
    const T& operator[](size_t i) const { size_t k, o; locate(i, k, o); return chunk(k)[o]; }
    T& back() { return (*this)[n_ - 1]; }
    void clear() { release(); }
    // Size the FIRST chunk for `n` elements — only before anything is stored.
    void reserve(size_t n) {
        if (n_ || first_) return;
        unsigned s = 0;
        while ((size_t(1) << s) < n && s < 20) s++;
        shift_ = s;
    }
    template <class... A>
    T& emplace_back(A&&... a) {
        size_t k, o;
        locate(n_, k, o);
        if (o == 0) {                       // the first element of a new chunk
            const size_t cap = size_t(1) << (shift_ + k);
            if (k == 0) { if (!first_) first_ = allocChunk(cap); }
            else if (more_.size() < k) more_.push_back(allocChunk(cap));
        }
        T* p = chunk(k) + o;
        ::new (static_cast<void*>(p)) T(std::forward<A>(a)...);
        n_++;
        return *p;
    }
};

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

    // Insertion order; holes stay (dead flag). The entries live in `own_`
    // until something ALIASES one of them — a Pair that a plain Hash hands out
    // holds the hash's own container, as Rakudo's do — and from then on in
    // `gen_`, a generation the aliases co-own (aliasOf). Moving the list keeps
    // every element where it was, so taking the generation moves nothing. A
    // clear or an assignment then starts an empty generation and lets the old
    // one go when its last alias does: a Pair taken before `%h = ()` keeps its
    // container, and nothing piles up for a hash that is refilled in a loop.
    ChunkList<Entry> own_;
    std::shared_ptr<ChunkList<Entry>> gen_;
    ChunkList<Entry>& E() { return gen_ ? *gen_ : own_; }
    const ChunkList<Entry>& E() const { return gen_ ? *gen_ : own_; }
    void retireEntries() { if (gen_) gen_.reset(); own_.clear(); }
    std::vector<int32_t> index_;   // power-of-two probe table of entry numbers
    size_t live_ = 0;              // entries not dead
    size_t used_ = 0;              // index slots not EMPTY (live + tombstones)
    // An OBJECT-KEYED hash (`my %h{Mu}`) subscripts by an object, and the
    // payload is keyed by strings — so `%h.keys` handed back the STRINGIFICATION
    // and the original was gone. `.print(…) for %connections.keys` then called
    // .print on a stringified socket (Log::Timeline), and
    // `keys.first(* eqv [1,2,3])` matched nothing (CBOR::Simple). The originals
    // live HERE rather than on the stored value, because the lvalue path hands
    // back a slot pointer the caller overwrites — and here they travel with the
    // payload's shared_ptr, so every copy of the Value sees them. Empty, and
    // never touched, for an ordinary string-keyed hash.
    std::map<std::string, Value> objKeys_;

    static uint64_t hashKey(const std::string& k) { return std::hash<std::string>{}(k); }

public:
    // The object a key was subscripted with, when this hash is object-keyed.
    void setObjKey(const std::string& k, const Value& v) { objKeys_[k] = v; }
    const Value* objKey(const std::string& k) const {
        auto it = objKeys_.find(k);
        return it == objKeys_.end() ? nullptr : &it->second;
    }
    bool hasObjKeys() const { return !objKeys_.empty(); }

private:

    size_t mask() const { return index_.size() - 1; }

    void rehash(size_t want) {
        size_t cap = 8;
        while (cap < want * 2) cap <<= 1;
        index_.assign(cap, EMPTY);
        used_ = 0;
        ChunkList<Entry>& es = E();
        for (size_t i = 0; i < es.size(); i++) {
            if (es[i].dead) continue;
            size_t s = es[i].h & mask();
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
            else if (E()[e].h == h && E()[e].kv.first == key)
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
        ChunkList<Entry>& es = E();
        es.emplace_back(key, std::move(v), h);
        if (index_[slot] == EMPTY) used_++;   // a tombstone reused does not grow `used_`
        index_[slot] = (int32_t)(es.size() - 1);
        live_++;
        return es.back().kv.second;
    }

public:
    ValueHash() = default;
    ValueHash(const ValueHash& o) { *this = o; }
    ValueHash& operator=(const ValueHash& o) {
        if (this == &o) return *this;
        retireEntries(); index_.clear(); live_ = used_ = 0;
        const ChunkList<Entry>& oe = o.E();
        E().reserve(o.live_);   // one right-sized first chunk for the copy
        for (size_t i = 0; i < oe.size(); i++)
            if (!oe[i].dead) (*this)[oe[i].kv.first] = oe[i].kv.second;
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
        void skip() { while (m_ && i_ < m_->E().size() && m_->E()[i_].dead) i_++; }
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
        ref operator*() const { return m_->E()[i_].kv; }
        ptr operator->() const { return &m_->E()[i_].kv; }
        iter& operator++() { i_++; skip(); return *this; }
        iter operator++(int) { iter t = *this; ++*this; return t; }
        bool operator==(const iter& o) const { return m_ == o.m_ && i_ == o.i_; }
        bool operator!=(const iter& o) const { return !(*this == o); }
    };
    using iterator = iter<false>;
    using const_iterator = iter<true>;

    iterator begin() { return iterator(this, 0); }
    iterator end() { return iterator(this, E().size()); }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, E().size()); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }

    size_t size() const { return live_; }
    bool empty() const { return live_ == 0; }
    // A first chunk for `n` entries, for a caller that knows the count (an
    // object's attributes). Only before anything is stored; a hint, not a cap.
    void reserve(size_t n) { if (E().size() == 0) E().reserve(n); }
    void clear() { retireEntries(); index_.clear(); live_ = used_ = 0; }

    // A shared_ptr to `v`, the Value of one of THIS hash's entries, that keeps
    // the entry alive whatever later happens to the hash (see gen_). The first
    // call moves the entries into their own generation; nothing is relocated.
    PRef<Value> aliasOf(Value& v) {
        if (!gen_) { gen_ = std::make_shared<ChunkList<Entry>>(std::move(own_)); own_.clear(); }
        return makeValueAlias(gen_, &v);
    }

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
        if (s >= 0) return E()[index_[s]].kv.second;
        return insertNew(key, h, Value{}, slot);
    }

    Value& at(const std::string& key) {
        int32_t s = findSlot(key, hashKey(key));
        if (s < 0) throw std::out_of_range("ValueHash::at: " + key);
        return E()[index_[s]].kv.second;
    }
    const Value& at(const std::string& key) const {
        int32_t s = findSlot(key, hashKey(key));
        if (s < 0) throw std::out_of_range("ValueHash::at: " + key);
        return E()[index_[s]].kv.second;
    }

    size_t erase(const std::string& key) {
        int32_t s = findSlot(key, hashKey(key));
        if (s < 0) return 0;
        E()[index_[s]].dead = true;
        index_[s] = TOMB;   // stays `used_` — the probe path must not break
        live_--;
        return 1;
    }
    iterator erase(iterator it) {
        size_t i = it.i_;
        if (i < E().size() && !E()[i].dead) erase(E()[i].kv.first);
        return iterator(this, i + 1);
    }

    std::pair<iterator, bool> insert(const value_type& kv) {
        uint64_t h = hashKey(kv.first);
        size_t slot;
        int32_t s = findSlot(kv.first, h, &slot);
        if (s >= 0) return {iterator(this, (size_t)index_[s]), false};
        insertNew(kv.first, h, kv.second, slot);
        return {iterator(this, E().size() - 1), true};
    }
    template <typename... A>
    std::pair<iterator, bool> emplace(const std::string& k, A&&... a) {
        return insert(value_type(k, Value(std::forward<A>(a)...)));
    }
};

using ValueMap = ValueHash;

} // namespace rakupp
