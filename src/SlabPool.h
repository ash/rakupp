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

// `constinit` is C++20 and this project builds at C++17 (CMakeLists.txt sets
// CMAKE_CXX_STANDARD 17). The keyword only ASSERTS what the initialisers below
// already are — constant — so dropping it costs the compile-time check and
// nothing else: a thread_local with a constant initialiser has no dynamic
// initialisation, and therefore no per-access guard, in either standard. Kept
// behind the feature test so the assertion comes back for free if the project
// moves to C++20.
#if defined(__cpp_constinit)
#define RAKUPP_CONSTINIT constinit
#else
#define RAKUPP_CONSTINIT
#endif

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

    // The hot state is `constinit thread_local` at namespace scope (below),
    // NOT a function-local `static thread_local`. The difference is an ABI
    // INITIALISATION GUARD on every single access: a function-local
    // thread_local whose type has a non-trivial destructor must check, on each
    // call, whether this thread has run the constructor and registered the
    // destructor with __cxa_thread_atexit. That guard is not hypothetical
    // overhead here — `emptyValueExt` in Value.h exists precisely because the
    // same guard, run 256x per byteset build, "was most of a 28% regex
    // regression". Putting an allocator behind one would charge every payload
    // allocation for it.
    //
    // So: plain arrays, constant-initialised, reached with no guard at all —
    // and the thread-exit cleanup that RVec's pool does is kept by paying the
    // guard exactly ONCE per thread, on the first allocation, behind a
    // constinit bool that the hot path only has to read.
    static constexpr std::size_t cls(std::size_t n) { return (n + kStep - 1) / kStep; }

    static void drain() noexcept;
    static void ensureReaper() {
        // The one guarded object in the design. Referenced only from the
        // first-allocation slow path, so the guard is paid once per thread
        // rather than once per allocation.
        struct Reaper {
            ~Reaper() { drain(); }
        };
        static thread_local Reaper r;
        (void)r;
    }

public:
    static void* alloc(std::size_t n) {
        if (n == 0 || n > kMaxSize) return ::operator new(n ? n : 1);
        if (!tlArmed) { tlArmed = true; ensureReaper(); }
        std::size_t c = cls(n);
        if (FreeBlock* b = tlHead[c]) {
            tlHead[c] = b->next;
            tlCount[c]--;
            return static_cast<void*>(b);
        }
        // Allocate the CLASS size, not the request: a block freed as class c
        // must be reusable for any later request in class c.
        return ::operator new(c * kStep);
    }
    static void dealloc(void* v, std::size_t n) noexcept {
        if (!v) return;
        if (n == 0 || n > kMaxSize) { ::operator delete(v); return; }
        std::size_t c = cls(n);
        if (tlCount[c] < kPoolMax) {
            FreeBlock* b = static_cast<FreeBlock*>(v);
            b->next = tlHead[c];
            tlHead[c] = b;
            tlCount[c]++;
            return;
        }
        ::operator delete(v);
    }

private:
    inline static RAKUPP_CONSTINIT thread_local FreeBlock* tlHead[kClasses]  = {};
    inline static RAKUPP_CONSTINIT thread_local std::size_t tlCount[kClasses] = {};
    inline static RAKUPP_CONSTINIT thread_local bool        tlArmed           = false;
};

inline void SlabPool::drain() noexcept {
    for (std::size_t i = 0; i < kClasses; i++) {
        while (tlHead[i]) {
            FreeBlock* b = tlHead[i];
            tlHead[i] = b->next;
            ::operator delete(static_cast<void*>(b));
        }
        tlCount[i] = 0;
    }
    // A later allocation on this thread (from another thread_local destructor
    // running after ours) must re-register, or its blocks are never drained.
    tlArmed = false;
}

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
