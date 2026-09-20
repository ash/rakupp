// SlabPool — a thread-local free list for the fixed-size PAYLOAD blocks.
//
// [docs/dev/plans/PAYLOAD-SLAB-PLAN.md]. A `Value`'s payloads all reach the
// allocator through `make_shared`, and the census in that plan says what one
// costs: a `Match` is four malloc calls (ValueExt 192, MatchData 88, ValueList
// 48, ValueMap 136), and 82.5% of every allocation `regex.raku` makes is one of
// those four blocks.
//
// This is RVec's `BlockPool` (ValueVec.h) with the key changed. That one is
// indexed by ValueList CAPACITY and therefore only ever holds `ValueList` data
// blocks; this one is indexed by SIZE CLASS, so it holds anything — and in
// particular the `shared_ptr` CONTROL BLOCK, which is the allocation RVec's
// pool structurally cannot reach (it is made by `make_shared`, before any
// `RVec` exists).
//
// The retention discipline is copied deliberately rather than improved on.
// Blocks are allocated ONE AT A TIME with `::operator new` and merely RETAINED
// on free, up to kPoolMax per class — not carved out of large chunks. Chunk
// carving measures faster in a probe and is the wrong trade here for the reason
// ValueVec.h already records: a pool that cannot give individual blocks back
// turns a transient peak into a permanent footprint, and this codebase has
// already paid that bill once (424 MB -> 664 MB, 22% of a program's cycles,
// from rounding small ValueList blocks up). Bounded retention keeps the reuse
// win and caps the footprint at kClasses * kPoolMax * kMaxSize per thread.
//
// Threading: each list is thread-private, exactly as RVec's is. A block
// allocated on one thread and released on another simply joins the releasing
// thread's list; the blocks are plain memory of one fixed size, so nothing is
// shared and nothing needs a lock.
#pragma once

#include <cstddef>
#include <memory>
#include <new>

namespace rakupp {

class SlabPool {
    // 512 covers every payload the census names — ValueList control 48,
    // MatchData 88, ValueHash 136, ValueExt 192, ObjectData 304 — with room
    // for the block to grow a field without silently falling out of the pool.
    // Anything larger goes straight to the allocator, which is the right answer
    // for ValueHash's 4 KB deque chunk: pooling that is a different plan.
    static constexpr std::size_t kMaxSize = 512;
    static constexpr std::size_t kStep    = 16;   // every class is 16-byte aligned
    static constexpr std::size_t kClasses = kMaxSize / kStep + 1;
    static constexpr std::size_t kPoolMax = 64;   // blocks retained per class — RVec's number

    struct FreeBlock { FreeBlock* next; };

    // Trivial members, but a real destructor so a thread that exits gives its
    // blocks back rather than leaking — RVec's discipline, and the reason this
    // is a class with a thread_local instance rather than bare statics.
    struct Pool {
        FreeBlock*  head[kClasses] = {};
        std::size_t n[kClasses]    = {};
        ~Pool() {
            for (std::size_t i = 0; i < kClasses; i++)
                while (head[i]) {
                    FreeBlock* b = head[i];
                    head[i] = b->next;
                    ::operator delete(static_cast<void*>(b));
                }
        }
    };
    static Pool& pool() {
        static thread_local Pool p;
        return p;
    }
    static constexpr std::size_t cls(std::size_t n) { return (n + kStep - 1) / kStep; }

public:
    static void* alloc(std::size_t n) {
        if (n == 0 || n > kMaxSize) return ::operator new(n ? n : 1);
        Pool& p = pool();
        std::size_t c = cls(n);
        if (FreeBlock* b = p.head[c]) {
            p.head[c] = b->next;
            p.n[c]--;
            return static_cast<void*>(b);
        }
        // Allocate the CLASS size, not the request: a block freed as class c
        // must be reusable for any later request in class c.
        return ::operator new(c * kStep);
    }
    static void dealloc(void* v, std::size_t n) noexcept {
        if (!v) return;
        if (n == 0 || n > kMaxSize) { ::operator delete(v); return; }
        Pool& p = pool();
        std::size_t c = cls(n);
        if (p.n[c] < kPoolMax) {
            FreeBlock* b = static_cast<FreeBlock*>(v);
            b->next = p.head[c];
            p.head[c] = b;
            p.n[c]++;
            return;
        }
        ::operator delete(v);
    }
};

// The Allocator surface `std::allocate_shared` wants.
//
// allocate_shared REBINDS this to an internal type that is the control block
// and T together — so the size that reaches allocate() is NOT sizeof(T), and
// keying the class on `n * sizeof(U)` at that point is what makes the control
// block poolable at all.
template <class T>
struct SlabAlloc {
    using value_type = T;
    // The pool hands out 16-byte-aligned blocks from `::operator new`. An
    // over-aligned payload would need the aligned new/delete pair instead, so
    // refuse it at compile time rather than mis-align it at runtime.
    static_assert(alignof(T) <= alignof(std::max_align_t),
                  "SlabAlloc does not serve over-aligned types");

    SlabAlloc() = default;
    template <class U> SlabAlloc(const SlabAlloc<U>&) noexcept {}

    T* allocate(std::size_t n) {
        return static_cast<T*>(SlabPool::alloc(n * sizeof(T)));
    }
    void deallocate(T* p, std::size_t n) noexcept {
        SlabPool::dealloc(static_cast<void*>(p), n * sizeof(T));
    }
    template <class U> bool operator==(const SlabAlloc<U>&) const noexcept { return true; }
    template <class U> bool operator!=(const SlabAlloc<U>&) const noexcept { return false; }
};

// The one spelling every payload site uses, so the swap is a rename and the
// whole experiment can be reverted by changing this function alone.
template <class T, class... A>
inline std::shared_ptr<T> makePayload(A&&... a) {
    return std::allocate_shared<T>(SlabAlloc<T>{}, static_cast<A&&>(a)...);
}

} // namespace rakupp
