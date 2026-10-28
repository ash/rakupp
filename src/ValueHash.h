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

#include "IntOps.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
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
        k = 63 - (size_t)clzll((unsigned long long)q);
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
    // The elements in order as runs of contiguous storage, f(first, count),
    // until f answers false: a scan without locate()'s arithmetic per element.
    template <class F>
    void runs(F&& f) const {
        size_t left = n_;
        for (size_t k = 0; left; k++) {
            const size_t cap = size_t(1) << (shift_ + k);
            const size_t m = left < cap ? left : cap;
            if (!f(static_cast<const T*>(chunk(k)), m)) return;
            left -= m;
        }
    }
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

    // Insertion order; holes stay (dead flag). The entries live in `own_`, and
    // every access goes there. Once something ALIASES one of them — a Pair that
    // a plain Hash hands out holds the hash's own container, as Rakudo's do —
    // `gen_` is a generation the aliases co-own (aliasOf). It stays EMPTY while
    // the hash holds its entries: a clear, an assignment or the hash's own end
    // hands them over to it (retireEntries) and lets it go when its last alias
    // does, so a Pair taken before `%h = ()` keeps its container, and nothing
    // piles up for a hash that is refilled in a loop.
    //
    // Taking the generation therefore moves nothing and leaves `own_` as every
    // reader sees it. It used to move the entries into the generation there and
    // then, which made handing out a Pair a WRITE to the hash: two threads
    // reading one shared hash with `.pairs` both moved it, the second moving an
    // emptied list — the hash read as `{}`, or its Pairs pointed into a list
    // already freed — and a thread iterating a hash (`run` reading %*ENV) while
    // another took its first Pair read the list mid-move (issue #193).
    ChunkList<Entry> own_;
    std::shared_ptr<ChunkList<Entry>> gen_;
    // gen_ is made once, under genMutex(), and published here: a thread that
    // sees the flag set sees gen_ (aliasOf, entriesAliased)
    std::atomic<bool> genPub_{false};
    ChunkList<Entry>& E() { return own_; }
    const ChunkList<Entry>& E() const { return own_; }
    void retireEntries() {
        if (gen_) {   // the entries leave with the generation, which keeps them while a Pair does
            *gen_ = std::move(own_);
            gen_.reset();
            genPub_.store(false, std::memory_order_relaxed);
        }
        own_.clear();
    }
    static std::mutex& genMutex() { static std::mutex m; return m; }
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    void makeGeneration() {
        std::lock_guard<std::mutex> lk(genMutex());
        if (genPub_.load(std::memory_order_relaxed)) return;   // another thread made it while we waited
        gen_ = std::make_shared<ChunkList<Entry>>();
        genPub_.store(true, std::memory_order_release);
    }
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

    // A hash of at most kSmall entries has no index (VALUEHASH-SMALL-PLAN step
    // 4): its lookups scan the entries, comparing the stored hash before the
    // key, which costs about what a probe does at this size and spares most
    // objects' attribute tables — and every small hash — the index's
    // allocation. (At 8 a lookup of the 7th key measured +14%.) The
    // index is built the first time an insert would pass kSmall entries, and
    // stays until clear().
    static constexpr size_t kSmall = 4;

    // The entry holding `key`, or -1; *insertAt as findSlot gives it (SIZE_MAX
    // for a hash with no index). The probe comes first and the scan is out of
    // line, so an indexed hash's lookup costs what it did before.
    int32_t findEntry(const std::string& key, uint64_t h, size_t* insertAt = nullptr) const {
        if (!index_.empty()) {
            int32_t s = findSlot(key, h, insertAt);
            return s < 0 ? -1 : index_[s];
        }
        if (insertAt) *insertAt = SIZE_MAX;
        return E().size() ? scanSmall(key, h) : -1;
    }
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    int32_t scanSmall(const std::string& key, uint64_t h) const {
        int32_t found = -1;
        size_t base = 0;
        E().runs([&](const Entry* p, size_t m) {
            for (size_t j = 0; j < m; j++)
                if (p[j].h == h && !p[j].dead && p[j].kv.first == key) { found = (int32_t)(base + j); return false; }
            base += m;
            return true;
        });
        return found;
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
        if (index_.empty() && E().size() < kSmall) {   // still small: no index to keep
            ChunkList<Entry>& es = E();
            es.emplace_back(key, std::move(v), h);
            live_++;
            return es.back().kv.second;
        }
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
    // A hash that ends while a Pair still holds one of its entries hands the
    // entries to the generation, as a clear does (retireEntries)
    ~ValueHash() { if (gen_) *gen_ = std::move(own_); }
    ValueHash(ValueHash&& o) noexcept
        : own_(std::move(o.own_)), gen_(std::move(o.gen_)),
          genPub_(o.genPub_.load(std::memory_order_relaxed)), index_(std::move(o.index_)),
          live_(o.live_), used_(o.used_), objKeys_(std::move(o.objKeys_)) {
        o.genPub_.store(false, std::memory_order_relaxed);
        o.index_.clear();
        o.live_ = o.used_ = 0;
    }
    ValueHash& operator=(ValueHash&& o) noexcept {
        if (this == &o) return *this;
        retireEntries();   // this hash's own entries go to its aliases, if any hold them
        own_ = std::move(o.own_);
        gen_ = std::move(o.gen_);
        genPub_.store(o.genPub_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        o.genPub_.store(false, std::memory_order_relaxed);
        index_ = std::move(o.index_);
        o.index_.clear();
        live_ = o.live_; used_ = o.used_;
        o.live_ = o.used_ = 0;
        objKeys_ = std::move(o.objKeys_);
        return *this;
    }
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
    // the entry alive whatever later happens to the hash (see gen_). Threads
    // that only READ the hash call this — `.pairs` on a hash several `start`
    // blocks share — so the first makes the generation under the lock and the
    // rest find it published.
    PRef<Value> aliasOf(Value& v) {
        if (!genPub_.load(std::memory_order_acquire)) makeGeneration();
        return makeValueAlias(gen_, &v);
    }

    // The cycle collector's view (Cycle.cpp): every Value this hash holds a
    // count through — a dead entry keeps its Value until clear(), and an
    // object-keyed hash keeps its keys' originals — until `f` answers false;
    // and whether a live Pair over an entry (aliasOf) can still reach the
    // entries from outside.
    template <class F>
    void forEachHeld(F&& f) const {
        const ChunkList<Entry>& es = E();
        for (size_t i = 0; i < es.size(); i++) if (!f(es[i].kv.second)) return;
        for (const auto& kv : objKeys_) if (!f(kv.second)) return;
    }
    bool entriesAliased() const { return genPub_.load(std::memory_order_acquire) && gen_.use_count() > 1; }
    bool holdsNothing() const { return E().size() == 0 && objKeys_.empty(); }

    iterator find(const std::string& key) {
        int32_t e = findEntry(key, hashKey(key));
        return e < 0 ? end() : iterator(this, (size_t)e);
    }
    const_iterator find(const std::string& key) const {
        int32_t e = findEntry(key, hashKey(key));
        return e < 0 ? end() : const_iterator(this, (size_t)e);
    }
    size_t count(const std::string& key) const { return findEntry(key, hashKey(key)) >= 0 ? 1 : 0; }

    // For the engine's fixed-shape hashes (a Proxy's hidden keys, read on every
    // access through a binding): the caller hashes its constant key ONCE
    // (keyHash) and looks it up by that, and builds the hash by appending keys
    // it knows are absent.
    static uint64_t keyHash(const std::string& k) { return hashKey(k); }
    Value* findPre(const std::string& key, uint64_t h) {
        int32_t e = findEntry(key, h);
        return e < 0 ? nullptr : &E()[e].kv.second;
    }
    const Value* findPre(const std::string& key, uint64_t h) const {
        int32_t e = findEntry(key, h);
        return e < 0 ? nullptr : &E()[e].kv.second;
    }
    Value& addFresh(const std::string& key, uint64_t h, Value v) {
        size_t slot = SIZE_MAX;
        if (!index_.empty()) findSlot(key, h, &slot);
        return insertNew(key, h, std::move(v), slot);
    }

    Value& operator[](const std::string& key) {
        uint64_t h = hashKey(key);
        size_t slot;
        int32_t e = findEntry(key, h, &slot);
        if (e >= 0) return E()[e].kv.second;
        return insertNew(key, h, Value{}, slot);
    }

    Value& at(const std::string& key) {
        int32_t e = findEntry(key, hashKey(key));
        if (e < 0) throw std::out_of_range("ValueHash::at: " + key);
        return E()[e].kv.second;
    }
    const Value& at(const std::string& key) const {
        int32_t e = findEntry(key, hashKey(key));
        if (e < 0) throw std::out_of_range("ValueHash::at: " + key);
        return E()[e].kv.second;
    }

    // The entry stays (marked dead) for reference stability, but its VALUE is
    // let go: a hash used as a cache or a work queue held every deleted
    // payload until clear() (`%h<k> = "x" x 1e6; %h<k>:delete` 300 times kept
    // 300 MB). Not while a Pair or a binding aliases an entry (aliasOf): it
    // may be reading this one. `dropped` is released after the bookkeeping,
    // so a DESTROY it runs sees a consistent hash.
    size_t erase(const std::string& key) {
        Value dropped;
        int32_t e;
        if (index_.empty()) {   // a small hash: the entry is only marked
            e = findEntry(key, hashKey(key));
            if (e < 0) return 0;
        }
        else {
            int32_t s = findSlot(key, hashKey(key));
            if (s < 0) return 0;
            e = index_[s];
            index_[s] = TOMB;   // stays `used_` — the probe path must not break
        }
        E()[e].dead = true;
        live_--;
        if (!entriesAliased()) std::swap(dropped, E()[e].kv.second);
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
        int32_t e = findEntry(kv.first, h, &slot);
        if (e >= 0) return {iterator(this, (size_t)e), false};
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
