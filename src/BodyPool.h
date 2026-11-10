// BodyPool — where a Value's out-of-line blocks live: the payload bodies
// (Ref.h's Body<T>) and the cold block (ValueExt).
//
// VALUE32-PLAN, the step to 40 bytes. A Value holds its payload and its cold
// block by a counted reference each. As pointers those are 16 bytes; here they
// are two 32-bit HANDLES, and a handle is an offset into one reserved region:
// the block's address is `base_ + (handle << 4)`. The count is where it was, in
// the block, and nothing about sharing or lifetime changes — only how the
// address is spelled.
//
// So every such block must come from that region, which is why this is an
// allocator and not SlabPool with another key. The region is reserved once
// (address space only) and cut into 64 KB SPANS. A span serves one size class
// at a time; a span whose blocks have all come back is given up — its pages go
// back to the system, and the span may next serve another class. That is the
// property SlabPool keeps by allocating its blocks one at a time, and the one
// a pool that only carves does not have (SlabPool.h says what that cost once).
//
// The hot path is SlabPool's: a thread-private free list per size class. A
// list that runs dry takes a batch from the spans under the class's lock, and
// one that grows past its bound hands half back the same way.
//
// Where handles are off — Windows, a 32-bit target, a build with the pools off
// (AddressSanitizer) — BodyPool is SlabPool, a handle is the pointer, and a
// Value is 8 bytes larger.
#pragma once

#include "ConstInit.h"
#include "SlabPool.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>

#ifndef RAKUPP_BODY_HANDLES
#if RAKUPP_POOLS && (defined(__APPLE__) || defined(__linux__)) && (defined(__aarch64__) || defined(__x86_64__))
#define RAKUPP_BODY_HANDLES 1
#else
#define RAKUPP_BODY_HANDLES 0
#endif
#endif

#if RAKUPP_BODY_HANDLES
#include <sys/mman.h>
#endif

namespace rakupp {

#if !RAKUPP_BODY_HANDLES

struct BodyPool {
    static void* alloc(std::size_t n) { return SlabPool::alloc(n); }
    static void dealloc(void* p, std::size_t n) noexcept { SlabPool::dealloc(p, n); }
};

// A block's address as a Value keeps it: here, the pointer.
template <class T>
struct BodyHandle {
    T* p = nullptr;
    T* get() const noexcept { return p; }
    T* raw() const noexcept { return p; }   // (not null)
    void set(T* x) noexcept { p = x; }
    explicit operator bool() const noexcept { return p != nullptr; }
    friend bool operator==(BodyHandle a, BodyHandle b) noexcept { return a.p == b.p; }
    friend bool operator!=(BodyHandle a, BodyHandle b) noexcept { return a.p != b.p; }
};

#else

class BodyPool {
public:
    static constexpr unsigned kGrain = 4;                 // a handle counts 16-byte units
    static constexpr std::size_t kMaxBody = 4096;         // the largest block served
    // Set once, by the first allocation, before any handle exists.
    inline static char* base_ = nullptr;
    static std::uint32_t enc(const void* p) noexcept {
        return (std::uint32_t)((std::size_t)(static_cast<const char*>(p) - base_) >> kGrain);
    }
    static void* dec(std::uint32_t h) noexcept { return base_ + ((std::size_t)h << kGrain); }

private:
    // Size classes: every 16 bytes up to 512, as SlabPool's, then six more up
    // to kMaxBody for the bodies it sent to the allocator (a Callable's).
    static constexpr std::size_t kSmallMax = 512;
    static constexpr std::size_t kStep = 16;
    static constexpr std::size_t kSmall = kSmallMax / kStep;   // classes 1..32
    static constexpr std::size_t kLarge[6] = {768, 1024, 1536, 2048, 3072, 4096};
    static constexpr std::size_t kClasses = 1 + kSmall + 6;    // class 0: no such size
    static constexpr std::size_t cls(std::size_t n) {
        if (n <= kSmallMax) return n ? (n + kStep - 1) / kStep : 1;
        for (std::size_t i = 0; i < 6; i++) if (n <= kLarge[i]) return kSmall + 1 + i;
        return 0;
    }
    static constexpr std::size_t clsSize(std::size_t c) { return c <= kSmall ? c * kStep : kLarge[c - kSmall - 1]; }
    // Blocks a thread keeps per class: SlabPool's 64, fewer of the large ones.
    static constexpr std::uint32_t keep(std::size_t c) {
        const std::size_t k = 16384 / clsSize(c);
        return (std::uint32_t)(k > 64 ? 64 : k < 4 ? 4 : k);
    }

    static constexpr unsigned kSpanShift = 16;
    static constexpr std::size_t kSpan = (std::size_t)1 << kSpanShift;
    static constexpr std::size_t kCommitSpans = 64;     // the region is made usable 4 MB at a time
    static constexpr std::uint32_t kWarmSpans = 16;     // given-up spans kept with their pages

    struct FreeBlock { FreeBlock* next; };
    struct Lists {
        FreeBlock* head[kClasses];
        std::uint32_t count[kClasses];
        bool armed;
    };
    RAKUPP_CONSTINIT inline static thread_local Lists tl = {};

    // One span's record. A free block holds the handle of the next in its
    // first four bytes; `carved` blocks of the span have ever been handed out.
    struct Span {
        std::uint32_t freeHead, freeCount, carved, total;
        std::uint32_t next, prev;       // the class's list of spans with a block to give, or the given-up list
        std::uint16_t cls;
        bool avail, cold;               // cold: its pages went back to the system
    };
    struct Global {
        std::mutex spanMu;
        std::mutex clsMu[kClasses];
        Span* meta = nullptr;
        std::size_t nSpans = 0;
        std::size_t hiSpan = 1;         // span 0 is never used: handle 0 is null
        std::size_t committed = 0;
        std::uint32_t emptyHead = 0, warm = 0;
        std::uint32_t availHead[kClasses] = {};
    };
    [[gnu::noinline]] static Global& g() {
        static Global* G = init();
        return *G;
    }
    static Global* init() {
        Global* G = new Global;
        // Address space only: 64 GB is what 32 bits of 16-byte units reach. A
        // machine that will not reserve that much gets what it will, and a
        // program that fills it gets std::bad_alloc.
        std::size_t bytes = (std::size_t)1 << (32 + kGrain);
        void* r = MAP_FAILED;
        for (; bytes >= ((std::size_t)1 << 28); bytes >>= 1) {
            r = ::mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
            if (r != MAP_FAILED) break;
        }
        if (r == MAP_FAILED) throw std::bad_alloc();
        G->nSpans = bytes >> kSpanShift;
        void* m = ::mmap(nullptr, G->nSpans * sizeof(Span), PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
        if (m == MAP_FAILED) throw std::bad_alloc();
        G->meta = static_cast<Span*>(m);   // zero pages, touched as spans come into use
        base_ = static_cast<char*>(r);
        return G;
    }

    static void linkAvail(Global& G, std::size_t c, std::uint32_t si) {
        Span& s = G.meta[si];
        s.prev = 0; s.next = G.availHead[c];
        if (s.next) G.meta[s.next].prev = si;
        G.availHead[c] = si;
        s.avail = true;
    }
    static void unlinkAvail(Global& G, std::size_t c, std::uint32_t si) {
        Span& s = G.meta[si];
        if (s.prev) G.meta[s.prev].next = s.next; else G.availHead[c] = s.next;
        if (s.next) G.meta[s.next].prev = s.prev;
        s.next = s.prev = 0;
        s.avail = false;
    }
    // A span for class c, linked as available. (clsMu[c] is held.)
    static std::uint32_t newSpan(Global& G, std::size_t c) {
        std::uint32_t si;
        {
            std::lock_guard<std::mutex> lk(G.spanMu);
            if (G.emptyHead) {
                si = G.emptyHead;
                G.emptyHead = G.meta[si].next;
                if (!G.meta[si].cold) G.warm--;
            }
            else {
                if (G.hiSpan >= G.nSpans) throw std::bad_alloc();
                if (G.hiSpan >= G.committed) {
                    std::size_t upTo = G.hiSpan + kCommitSpans;
                    if (upTo > G.nSpans) upTo = G.nSpans;
                    if (::mprotect(base_ + (G.committed << kSpanShift), (upTo - G.committed) << kSpanShift,
                                   PROT_READ | PROT_WRITE) != 0)
                        throw std::bad_alloc();
                    G.committed = upTo;
                }
                si = (std::uint32_t)G.hiSpan++;
            }
        }
        Span& s = G.meta[si];
        s = Span{};
        s.cls = (std::uint16_t)c;
        s.total = (std::uint32_t)(kSpan / clsSize(c));
        linkAvail(G, c, si);
        return si;
    }
    // Every block of the span is back: it leaves its class. (clsMu[c] is held.)
    static void retire(Global& G, std::uint32_t si) noexcept {
        Span& s = G.meta[si];
        std::lock_guard<std::mutex> lk(G.spanMu);
        s.cold = G.warm >= kWarmSpans;
        if (s.cold) {
#if defined(__APPLE__)
            ::madvise(base_ + ((std::size_t)si << kSpanShift), kSpan, MADV_FREE);
#else
            ::madvise(base_ + ((std::size_t)si << kSpanShift), kSpan, MADV_DONTNEED);
#endif
        }
        else G.warm++;
        s.next = G.emptyHead;
        G.emptyHead = si;
    }

    static void drain() noexcept {
        Lists& l = tl;
        for (std::size_t c = 1; c < kClasses; c++)
            if (l.count[c]) flush(c, l.count[c]);
        l.armed = false;   // (as SlabPool::drain)
    }
    static void arm() {
        struct Reaper {
            ~Reaper() { drain(); }
        };
        static thread_local Reaper r;
        (void)r;
    }

    // The thread's list for class c is empty: take a batch from the spans.
    [[gnu::noinline]] static void* refill(std::size_t c) {
        if (!c) throw std::bad_alloc();   // larger than kMaxBody
        Global& G = g();
        Lists& l = tl;
        if (!l.armed) { l.armed = true; arm(); }
        const std::size_t sz = clsSize(c);
        std::uint32_t want = keep(c) / 2;
        FreeBlock* chain = nullptr;
        std::uint32_t got = 0;
        {
            std::lock_guard<std::mutex> lk(G.clsMu[c]);
            while (got < want) {
                std::uint32_t si = G.availHead[c];
                if (!si) {
                    if (got) break;
                    si = newSpan(G, c);
                }
                Span& s = G.meta[si];
                char* const at = base_ + ((std::size_t)si << kSpanShift);
                while (got < want) {
                    char* blk;
                    if (s.freeHead) {
                        blk = static_cast<char*>(dec(s.freeHead));
                        s.freeHead = *reinterpret_cast<std::uint32_t*>(blk);
                        s.freeCount--;
                    }
                    else if (s.carved < s.total) blk = at + (std::size_t)s.carved++ * sz;
                    else break;
                    FreeBlock* b = reinterpret_cast<FreeBlock*>(blk);
                    b->next = chain;
                    chain = b;
                    got++;
                }
                if (!s.freeHead && s.carved == s.total) unlinkAvail(G, c, si);
            }
        }
        FreeBlock* out = chain;
        chain = chain->next;
        l.head[c] = chain;          // (the list was empty)
        l.count[c] = got - 1;
        return static_cast<void*>(out);
    }
    // Hand k blocks of the thread's list for class c back to their spans.
    [[gnu::noinline]] static void flush(std::size_t c, std::uint32_t k) noexcept {
        Global& G = g();
        Lists& l = tl;
        std::lock_guard<std::mutex> lk(G.clsMu[c]);
        while (k-- && l.head[c]) {
            FreeBlock* b = l.head[c];
            l.head[c] = b->next;
            l.count[c]--;
            char* blk = reinterpret_cast<char*>(b);
            const std::uint32_t si = (std::uint32_t)((std::size_t)(blk - base_) >> kSpanShift);
            Span& s = G.meta[si];
            *reinterpret_cast<std::uint32_t*>(blk) = s.freeHead;
            s.freeHead = enc(blk);
            s.freeCount++;
            if (!s.avail) linkAvail(G, c, si);
            // All of it is back. The class keeps one such span, so a program
            // that frees and allocates at a span's edge does not give the
            // pages up and ask for them again each time.
            if (s.freeCount == s.carved && !(G.availHead[c] == si && !s.next)) {
                unlinkAvail(G, c, si);
                retire(G, si);
            }
        }
    }

public:
    static void* alloc(std::size_t n) {
        const std::size_t c = cls(n);
        Lists& l = tl;
        if (FreeBlock* b = l.head[c]) {
            l.head[c] = b->next;
            l.count[c]--;
            return static_cast<void*>(b);
        }
        return refill(c);
    }
    static void dealloc(void* v, std::size_t n) noexcept {
        if (!v) return;
        const std::size_t c = cls(n);
        Lists& l = tl;
        if (!l.armed) { l.armed = true; arm(); }
        FreeBlock* b = static_cast<FreeBlock*>(v);
        b->next = l.head[c];
        l.head[c] = b;
        if (++l.count[c] > keep(c)) flush(c, keep(c) / 2);
    }
};

// A block's address as a Value keeps it: 32 bits, zero for none.
template <class T>
struct BodyHandle {
    std::uint32_t h = 0;
    T* get() const noexcept { return h ? raw() : nullptr; }
    T* raw() const noexcept { return static_cast<T*>(BodyPool::dec(h)); }   // (not null)
    void set(T* x) noexcept { h = x ? BodyPool::enc(static_cast<const void*>(x)) : 0; }
    explicit operator bool() const noexcept { return h != 0; }
    friend bool operator==(BodyHandle a, BodyHandle b) noexcept { return a.h == b.h; }
    friend bool operator!=(BodyHandle a, BodyHandle b) noexcept { return a.h != b.h; }
};

#endif

} // namespace rakupp
