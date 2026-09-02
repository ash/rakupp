// RVec — the growable array behind `ValueList`. It differs from
// `std::vector<Value>` in exactly two places, both of them measured:
//
//   * it GROWS in one pass — and, where the standard library allows a bitwise
//     move, that pass is a memcpy — where `std::vector` fills a second buffer
//     and then walks the old one again to destroy it (below);
//   * it takes SMALL blocks off a thread-local free list instead of the
//     allocator, because the tree's most frequent list by far is the one- or
//     two-element argument list built on every interpreted call (see alloc()).
//
// Why this exists (docs/dev/plans/VALUE32-PLAN.md, section F). A `Value` is
// 128 bytes with a non-trivial move constructor, so every `std::vector<Value>`
// reallocation walks the buffer element by element — move-construct into the
// new storage, then walk it AGAIN to destroy the sources — and building a
// 1M-element array spends more time on those two passes than on the pushes.
// The probe (tools/value32-probe.cpp) prices the bitwise end of it directly:
//
//     vector<Value> build 1000000 Ints  grow 19.71 ms | reserve 7.73 | reloc-grow 8.58
//     vector<Value> build 1000000 Strs  grow 21.48 ms |               reloc-grow 14.19
//
// A relocating grow lands within ~11% of a perfectly pre-`reserve`d build.
//
// The licence for it: a `Value` is *trivially relocatable*. Moving its bytes
// to a new address and NOT destroying the source is equivalent to
// move-constructing then destroying — which is exactly what a reallocation
// does. Nothing in a `Value` points at itself: `i`/`n` are scalars, `IStr` is
// an interned pointer, `p_`/`x_` and `CowStr::p_` are `shared_ptr` pairs, and
// `CowStr::s_` is a `std::string`…
//
// …which is the one member that is NOT relocatable everywhere. libstdc++'s
// `std::string` points `_M_p` at its own inline buffer for short strings, so a
// bitwise move leaves it dangling. libc++ and MSVC's STL keep no such
// self-pointer. `bitwiseRelocOk()` decides this by asking the library at run
// time rather than by trusting a macro, and the slow path is exactly what
// `std::vector` does today — so a stdlib that fails the probe keeps the
// behaviour it has, and only loses the speedup.
//
// Everything else is `std::vector`: the API is the subset the tree actually
// uses on `ValueList`, and the semantics are the standard's — raw-pointer
// iterators (so `std::sort` and iterator arithmetic work unchanged), growth
// invalidating all iterators, and `push_back(v[0])` staying legal.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

// The grow path is deliberately kept OUT of the caller. `push_back` on a
// `ValueList` is one of the hottest lines in the tree — every call builds an
// argument list — and letting the allocator machinery inline into all ~1,400
// of those sites costs more in instruction cache than the branch saves.
// `std::vector` marks its own `__push_back_slow_path` the same way.
#if defined(_MSC_VER)
#define RAKUPP_NOINLINE __declspec(noinline)
#else
#define RAKUPP_NOINLINE __attribute__((noinline))
#endif

namespace rakupp {

// Is a bitwise move of a `std::string` a valid relocation on this standard
// library?
//
// The question is NOT "does a short string keep its characters inside the
// object" — every implementation does, that is what the small-buffer
// optimisation is. It is "does the object hold a POINTER to those characters".
// libc++ and MSVC compute the address from `this`, so a copy of the bytes at a
// new address reads its own buffer. libstdc++ stores `_M_p` aimed at its own
// `_M_local_buf`, so a copy of the bytes reads the ORIGINAL object's buffer
// and dangles the moment that storage is reused.
//
// So the probe relocates one and asks where the copy's characters came from.
// The first version of this asked whether `s.data()` was inside `s`, which is
// true on every library and answered "not relocatable" everywhere — a bug that
// cost nothing but the speedup, and is exactly why the fallback below has to
// stay correct rather than merely present.
//
// Answered once, in a function-local static, so no namespace-scope dynamic
// initialiser can read it before it runs.
inline bool bitwiseRelocOk() {
    static const bool ok = [] {
        std::string src("ab");   // short: owns no heap block, so nothing leaks
        alignas(std::string) unsigned char buf[sizeof(std::string)];
        std::memcpy(static_cast<void*>(buf), static_cast<const void*>(&src), sizeof(std::string));
        const std::string* dst = reinterpret_cast<const std::string*>(buf);
        const char* p = dst->data();
        const char* lo = reinterpret_cast<const char*>(buf);
        return dst->size() == 2 && p >= lo && p < lo + sizeof(std::string) &&
               p[0] == 'a' && p[1] == 'b';
    }();
    return ok;
}

template <class T>
class RVec {
    T* d_ = nullptr;
    std::size_t n_ = 0, c_ = 0;

    // The FIRST allocation is exactly what was asked for, and only then does
    // capacity double. Rounding the first block up instead (to four, say, so a
    // short list takes one allocation rather than three) measured -2.4% on
    // `fib` and -2.1% on `strpass`: the tree's most frequent `ValueList` by far
    // is a one- or two-element ARGUMENT list, and at 128 bytes an element,
    // over-allocating it four-fold costs more in allocator traffic than it
    // saves in reallocations. So the allocation *pattern* stays exactly
    // `std::vector`'s, and this container's win is purely that each
    // reallocation relocates instead of walking.

    // Small blocks come off a thread-local free list instead of the allocator.
    //
    // The tree's most frequent `ValueList` by far is a one- or two-element
    // ARGUMENT list: one is built, filled, passed and destroyed on every
    // interpreted call, and its heap block is the whole of its cost. The IR
    // experiment (docs/dev/experiments/IR-EXPERIMENT.md) priced that block at
    // ~32 ns against a ~265 ns interpreted call; taking it off a free list
    // measures ~9 ns for the same shape — pop instead of malloc, push instead
    // of free.
    //
    // The lists are kept per EXACT capacity, not per rounded-up size class,
    // and that is the whole design decision. Rounding every small request up
    // to one four-element block is simpler and marginally faster, but a
    // `ValueList` is also the payload of every Array VALUE, and a program
    // holding a million one-element arrays then holds a million four-element
    // blocks: measured at 424 MB -> 664 MB, with the extra memory traffic
    // costing 22% of that program's cycles. Per-capacity lists keep the
    // allocation win and change the footprint by nothing at all.
    //
    // A block allocated on one thread and released on another simply joins the
    // releasing thread's list; blocks within a list are plain memory of one
    // fixed size.
    static constexpr std::size_t kPooled = 4;    // capacities 1..4 are pooled
    static constexpr std::size_t kPoolMax = 64;  // …up to this many blocks each

    struct FreeBlock { FreeBlock* next; };
    // Trivial members, but a real destructor so a thread that exits gives its
    // blocks back rather than leaking — the same discipline the interpreter's
    // call-frame pool already keeps.
    struct BlockPool {
        FreeBlock* head[kPooled] = {};
        std::size_t n[kPooled] = {};
        ~BlockPool() {
            for (std::size_t i = 0; i < kPooled; i++)
                while (head[i]) {
                    FreeBlock* b = head[i];
                    head[i] = b->next;
                    ::operator delete(static_cast<void*>(b));
                }
        }
    };
    static BlockPool& pool() {
        static thread_local BlockPool p;
        return p;
    }

    static T* alloc(std::size_t k) {
        if (k <= kPooled) {
            BlockPool& p = pool();
            FreeBlock*& h = p.head[k - 1];
            if (h) {
                FreeBlock* b = h;
                h = b->next;
                p.n[k - 1]--;
                return reinterpret_cast<T*>(b);
            }
        }
        return static_cast<T*>(::operator new(k * sizeof(T)));
    }
    static void dealloc(T* p, std::size_t cap) {
        if (!p) return;
        if (cap && cap <= kPooled) {
            BlockPool& bp = pool();
            if (bp.n[cap - 1] < kPoolMax) {
                FreeBlock* b = reinterpret_cast<FreeBlock*>(p);
                b->next = bp.head[cap - 1];
                bp.head[cap - 1] = b;
                bp.n[cap - 1]++;
                return;
            }
        }
        ::operator delete(static_cast<void*>(p));
    }

    // Move `k` elements from `src` to uninitialised `dst`, leaving `src`'s
    // storage dead (NOT destroyed) — the relocation contract.
    static void relocate(T* dst, T* src, std::size_t k) {
        if (k == 0) return;
        if (bitwiseRelocOk()) {
            std::memcpy(static_cast<void*>(dst), static_cast<const void*>(src), k * sizeof(T));
        }
        else {
            for (std::size_t j = 0; j < k; j++) {
                ::new (static_cast<void*>(dst + j)) T(std::move(src[j]));
                src[j].~T();
            }
        }
    }

    void destroyAll() {
        for (std::size_t j = 0; j < n_; j++) d_[j].~T();
    }

    // Re-seat into a buffer of exactly `k` slots (k >= n_).
    RAKUPP_NOINLINE void reseat(std::size_t k) {
        T* nd = k ? alloc(k) : nullptr;
        relocate(nd, d_, n_);
        dealloc(d_, c_);
        d_ = nd;
        c_ = k;
    }

    std::size_t nextCap(std::size_t need) const {
        std::size_t k = c_ * 2;
        return k < need ? need : k;
    }
    // (capacities 1..4 come off the free list above, so the small steps of
    // this ramp cost a pop rather than a malloc.)
    void ensure(std::size_t need) {
        if (need > c_) reseat(nextCap(need));
    }

    // Open a `k`-slot uninitialised hole at index `at`, growing if needed.
    // The tail relocates rather than move-assigning, which is the same licence
    // the grow path uses.
    RAKUPP_NOINLINE void openHole(std::size_t at, std::size_t k) {
        if (k == 0) return;
        if (n_ + k > c_) {
            std::size_t nc = nextCap(n_ + k);
            T* nd = alloc(nc);
            relocate(nd, d_, at);
            relocate(nd + at + k, d_ + at, n_ - at);
            dealloc(d_, c_);
            d_ = nd;
            c_ = nc;
        }
        else if (bitwiseRelocOk()) {
            std::memmove(static_cast<void*>(d_ + at + k), static_cast<const void*>(d_ + at),
                         (n_ - at) * sizeof(T));
        }
        else {
            for (std::size_t j = n_; j > at; j--) {
                ::new (static_cast<void*>(d_ + j + k - 1)) T(std::move(d_[j - 1]));
                d_[j - 1].~T();
            }
        }
    }

public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<T*>;
    using const_reverse_iterator = std::reverse_iterator<const T*>;

    RVec() = default;
    explicit RVec(size_type k) {
        if (k) {
            d_ = alloc(k);
            c_ = k;
            for (; n_ < k; n_++) ::new (static_cast<void*>(d_ + n_)) T();
        }
    }
    RVec(size_type k, const T& v) {
        if (k) {
            d_ = alloc(k);
            c_ = k;
            for (; n_ < k; n_++) ::new (static_cast<void*>(d_ + n_)) T(v);
        }
    }
    template <class It, class = typename std::iterator_traits<It>::iterator_category>
    RVec(It first, It last) { append(first, last); }
    RVec(std::initializer_list<T> il) { append(il.begin(), il.end()); }

    RVec(const RVec& o) {
        if (o.n_) {
            d_ = alloc(o.n_);
            c_ = o.n_;
            for (; n_ < o.n_; n_++) ::new (static_cast<void*>(d_ + n_)) T(o.d_[n_]);
        }
    }
    RVec(RVec&& o) noexcept : d_(o.d_), n_(o.n_), c_(o.c_) {
        o.d_ = nullptr;
        o.n_ = o.c_ = 0;
    }
    ~RVec() {
        destroyAll();
        dealloc(d_, c_);
    }

    RVec& operator=(const RVec& o) {
        if (this != &o) assign(o.d_, o.d_ + o.n_);
        return *this;
    }
    RVec& operator=(RVec&& o) noexcept {
        if (this != &o) {
            destroyAll();
            dealloc(d_, c_);
            d_ = o.d_;
            n_ = o.n_;
            c_ = o.c_;
            o.d_ = nullptr;
            o.n_ = o.c_ = 0;
        }
        return *this;
    }
    RVec& operator=(std::initializer_list<T> il) {
        assign(il.begin(), il.end());
        return *this;
    }

    void assign(size_type k, const T& v) {
        T tmp(v);   // v may live inside our own storage
        clear();
        ensure(k);
        for (; n_ < k; n_++) ::new (static_cast<void*>(d_ + n_)) T(tmp);
    }
    template <class It, class = typename std::iterator_traits<It>::iterator_category>
    void assign(It first, It last) {
        clear();
        append(first, last);
    }
    void assign(std::initializer_list<T> il) { assign(il.begin(), il.end()); }

    // element access
    T& operator[](size_type k) { return d_[k]; }
    const T& operator[](size_type k) const { return d_[k]; }
    T& at(size_type k) {
        if (k >= n_) throw std::out_of_range("RVec::at");
        return d_[k];
    }
    const T& at(size_type k) const {
        if (k >= n_) throw std::out_of_range("RVec::at");
        return d_[k];
    }
    T& front() { return d_[0]; }
    const T& front() const { return d_[0]; }
    T& back() { return d_[n_ - 1]; }
    const T& back() const { return d_[n_ - 1]; }
    T* data() { return d_; }
    const T* data() const { return d_; }

    // iterators
    iterator begin() { return d_; }
    iterator end() { return d_ + n_; }
    const_iterator begin() const { return d_; }
    const_iterator end() const { return d_ + n_; }
    const_iterator cbegin() const { return d_; }
    const_iterator cend() const { return d_ + n_; }
    reverse_iterator rbegin() { return reverse_iterator(d_ + n_); }
    reverse_iterator rend() { return reverse_iterator(d_); }
    const_reverse_iterator rbegin() const { return const_reverse_iterator(d_ + n_); }
    const_reverse_iterator rend() const { return const_reverse_iterator(d_); }
    const_reverse_iterator crbegin() const { return const_reverse_iterator(d_ + n_); }
    const_reverse_iterator crend() const { return const_reverse_iterator(d_); }

    // capacity
    size_type size() const { return n_; }
    bool empty() const { return n_ == 0; }
    size_type capacity() const { return c_; }
    static size_type max_size() { return static_cast<size_type>(-1) / sizeof(T); }
    void reserve(size_type k) {
        if (k > c_) reseat(k);
    }
    void shrink_to_fit() {
        if (c_ > n_) reseat(n_);
    }

    // modifiers
    void clear() {
        destroyAll();
        n_ = 0;
    }
    void push_back(const T& v) {
        if (n_ == c_) { growAndBuild(v); return; }
        ::new (static_cast<void*>(d_ + n_)) T(v);
        n_++;
    }
    void push_back(T&& v) {
        if (n_ == c_) { growAndBuild(std::move(v)); return; }
        ::new (static_cast<void*>(d_ + n_)) T(std::move(v));
        n_++;
    }
    template <class... A>
    T& emplace_back(A&&... a) {
        if (n_ == c_) {
            growAndBuild(std::forward<A>(a)...);
            return d_[n_ - 1];
        }
        ::new (static_cast<void*>(d_ + n_)) T(std::forward<A>(a)...);
        return d_[n_++];
    }
    void pop_back() {
        n_--;
        d_[n_].~T();
    }
    void resize(size_type k) {
        if (k < n_) {
            while (n_ > k) pop_back();
        }
        else if (k > n_) {
            ensure(k);
            for (; n_ < k; n_++) ::new (static_cast<void*>(d_ + n_)) T();
        }
    }
    void resize(size_type k, const T& v) {
        if (k < n_) {
            while (n_ > k) pop_back();
        }
        else if (k > n_) {
            T tmp(v);
            ensure(k);
            for (; n_ < k; n_++) ::new (static_cast<void*>(d_ + n_)) T(tmp);
        }
    }

    iterator insert(const_iterator pos, const T& v) {
        std::size_t at = static_cast<std::size_t>(pos - d_);
        T tmp(v);
        openHole(at, 1);
        ::new (static_cast<void*>(d_ + at)) T(std::move(tmp));
        n_++;
        return d_ + at;
    }
    iterator insert(const_iterator pos, T&& v) {
        std::size_t at = static_cast<std::size_t>(pos - d_);
        T tmp(std::move(v));
        openHole(at, 1);
        ::new (static_cast<void*>(d_ + at)) T(std::move(tmp));
        n_++;
        return d_ + at;
    }
    iterator insert(const_iterator pos, size_type k, const T& v) {
        std::size_t at = static_cast<std::size_t>(pos - d_);
        if (k == 0) return d_ + at;
        T tmp(v);
        openHole(at, k);
        for (std::size_t j = 0; j < k; j++) ::new (static_cast<void*>(d_ + at + j)) T(tmp);
        n_ += k;
        return d_ + at;
    }
    template <class It, class = typename std::iterator_traits<It>::iterator_category>
    iterator insert(const_iterator pos, It first, It last) {
        std::size_t at = static_cast<std::size_t>(pos - d_);
        return insertRange(at, first, last,
                           typename std::iterator_traits<It>::iterator_category{});
    }
    iterator insert(const_iterator pos, std::initializer_list<T> il) {
        return insert(pos, il.begin(), il.end());
    }
    template <class... A>
    iterator emplace(const_iterator pos, A&&... a) {
        std::size_t at = static_cast<std::size_t>(pos - d_);
        T tmp(std::forward<A>(a)...);
        openHole(at, 1);
        ::new (static_cast<void*>(d_ + at)) T(std::move(tmp));
        n_++;
        return d_ + at;
    }

    iterator erase(const_iterator pos) { return erase(pos, pos + 1); }
    iterator erase(const_iterator first, const_iterator last) {
        std::size_t a = static_cast<std::size_t>(first - d_);
        std::size_t b = static_cast<std::size_t>(last - d_);
        if (b <= a) return d_ + a;
        std::size_t k = b - a;
        for (std::size_t j = a; j < b; j++) d_[j].~T();
        if (bitwiseRelocOk()) {
            std::memmove(static_cast<void*>(d_ + a), static_cast<const void*>(d_ + b),
                         (n_ - b) * sizeof(T));
        }
        else {
            for (std::size_t j = b; j < n_; j++) {
                ::new (static_cast<void*>(d_ + j - k)) T(std::move(d_[j]));
                d_[j].~T();
            }
        }
        n_ -= k;
        return d_ + a;
    }

    void swap(RVec& o) noexcept {
        std::swap(d_, o.d_);
        std::swap(n_, o.n_);
        std::swap(c_, o.c_);
    }

private:
    // Grow, then build the new last element — in that order, and INTO THE NEW
    // BUFFER, so an argument that points into the old one (`v.push_back(v[0])`,
    // which `std::vector` also has to allow) is still alive when it is read.
    // That is why this takes the arguments rather than the finished value: a
    // copy into a temporary would be an extra 128-byte `Value` move on the one
    // path that runs for every empty list's first push.
    template <class... A>
    RAKUPP_NOINLINE void growAndBuild(A&&... a) {
        std::size_t nc = nextCap(n_ + 1);
        T* nd = alloc(nc);
        try {
            ::new (static_cast<void*>(nd + n_)) T(std::forward<A>(a)...);
        }
        catch (...) {
            dealloc(nd, nc);
            throw;
        }
        relocate(nd, d_, n_);
        dealloc(d_, c_);
        d_ = nd;
        c_ = nc;
        n_++;
    }

    template <class It>
    void append(It first, It last) {
        for (; first != last; ++first) push_back(*first);
    }
    template <class It>
    iterator insertRange(std::size_t at, It first, It last, std::input_iterator_tag) {
        RVec tail(first, last);
        return insertRange(at, tail.begin(), tail.end(), std::forward_iterator_tag{});
    }
    template <class It>
    iterator insertRange(std::size_t at, It first, It last, std::forward_iterator_tag) {
        std::size_t k = static_cast<std::size_t>(std::distance(first, last));
        if (k == 0) return d_ + at;
        // A range taken from THIS container would dangle the moment the hole
        // reallocates. `std::vector` calls that undefined; a hand-written
        // container is worth more than the standard's minimum here, and the
        // check is one pointer comparison on a path that is already the slow
        // one. `if constexpr` so it only compiles where the two pointer types
        // are comparable at all.
        if constexpr (std::is_convertible_v<It, const T*>) {
            const T* p = first;
            if (p >= d_ && p < d_ + n_) {
                RVec tmp(first, last);
                return insertRange(at, tmp.begin(), tmp.end(), std::forward_iterator_tag{});
            }
        }
        openHole(at, k);
        std::size_t j = 0;
        for (It it = first; it != last; ++it, ++j)
            ::new (static_cast<void*>(d_ + at + j)) T(*it);
        n_ += k;
        return d_ + at;
    }
};

template <class T>
inline bool operator==(const RVec<T>& a, const RVec<T>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}
template <class T>
inline bool operator!=(const RVec<T>& a, const RVec<T>& b) { return !(a == b); }
template <class T>
inline bool operator<(const RVec<T>& a, const RVec<T>& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}
template <class T>
inline bool operator>(const RVec<T>& a, const RVec<T>& b) { return b < a; }
template <class T>
inline bool operator<=(const RVec<T>& a, const RVec<T>& b) { return !(b < a); }
template <class T>
inline bool operator>=(const RVec<T>& a, const RVec<T>& b) { return !(a < b); }
template <class T>
inline void swap(RVec<T>& a, RVec<T>& b) noexcept { a.swap(b); }

}  // namespace rakupp
