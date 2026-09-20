// payload-slab-probe.cpp — what is left on the `malloc family 21.5%` line, and
// how much of it a thread-local slab allocator can take back.
//
// VALUE32-PLAN.md's profile of an array/hash-heavy 1.8 s workload reads:
//
//     malloc family 21.5%   std::vector<Value> members 7.6%
//     Value copy/move 3.7%  thread-local access 6.1%
//
// `Value` copy is the line that campaign is about, and it is the SMALL one.
// The big one is the allocator, and every call into it comes from a PAYLOAD:
// `make_shared<ValueList>` (24 sites), `<ObjectData>` (47), `<ValueExt>`,
// `<MatchData>`, `<ValueHash>`, `<StrBody>`. All are fixed-size and all are
// high-churn, which is the textbook shape for a slab.
//
// Part of this is ALREADY DONE and the probe must not take credit for it:
// `RVec::alloc`/`dealloc` (src/ValueVec.h) keep thread-local free lists for
// capacities 1..4, which covers the DATA block of the argument list that every
// interpreted call builds. What that pool does NOT cover is the `shared_ptr`
// CONTROL BLOCK: `make_shared<ValueList>` is a second, separate allocation
// holding the refcounts plus the 24-byte RVec header, and it goes to malloc
// every time. Section A measures that split directly instead of assuming it.
//
// Sections:
//   A  allocation census — how many blocks, and of what sizes, each Value shape
//      actually asks malloc for (recorded by a global operator new)
//   B  the drop-in: make_shared<T> vs allocate_shared<T, Slab>, per payload
//   C  the real op: construct + destroy each Value shape, today vs slab
//   D  churn shape: LIFO (stack discipline) vs a live set freed in random order
//   E  threads 1/2/4/8 — where a contended malloc and a thread-local list part
//   F  the floor: a bump arena that never frees, to say how much of the gap is
//      "malloc is slow" and how much is "pooling is fundamentally cheaper"
//
// The probe compares shapes inside ONE binary, so machine drift moves both
// sides together; every number is best-of-R.
//
//   cmake -S . -B build-probe -DCMAKE_BUILD_TYPE=Release
//   cmake --build build-probe --target rakupp_rt rakupp_parse rakupp_ucd_names \
//         rakupp_ucd_coll rakupp_ucd_props rakupp_stubs -j 8
//   c++ -std=c++20 -O2 -DNDEBUG -Isrc -Iinclude tools/payload-slab-probe.cpp \
//       build-probe/librakupp_{rt,parse,ucd_names,ucd_coll,ucd_props,stubs}.a \
//       -o /tmp/payload-slab-probe && /tmp/payload-slab-probe
#include "Value.h"
#include "ValueHash.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

using namespace rakupp;
using clk = std::chrono::steady_clock;
static double ms(clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// ---- A's instrument: a global operator new that can be armed ---------------
//
// A fixed array, not a map: the recorder runs INSIDE operator new, so anything
// that allocates here recurses. Sizes above kCensusMax land in one overflow
// bucket. Single-threaded by construction — section A arms it, nothing else.
static constexpr size_t kCensusMax = 512;
static bool      g_armed = false;
static long long g_hist[kCensusMax + 2];
static long long g_count = 0, g_bytes = 0;

static void censusReset() {
    memset(g_hist, 0, sizeof(g_hist));
    g_count = g_bytes = 0;
}
static void censusNote(size_t n) {
    if (!g_armed) return;
    g_count++;
    g_bytes += (long long)n;
    g_hist[n <= kCensusMax ? n : kCensusMax + 1]++;
}

void* operator new(size_t n) {
    censusNote(n);
    void* p = malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](size_t n) { return ::operator new(n); }
void* operator new(size_t n, std::align_val_t a) {
    censusNote(n);
    size_t al = (size_t)a;
    void* p = nullptr;
    if (posix_memalign(&p, al < sizeof(void*) ? sizeof(void*) : al, n ? n : 1) != 0)
        throw std::bad_alloc();
    return p;
}
void* operator new[](size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }
void operator delete(void* p, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { free(p); }

// ---- the slab --------------------------------------------------------------
//
// Thread-local, size-classed in 16-byte steps, intrusive free list. The same
// design RVec's BlockPool already uses, generalised past `ValueList` data
// blocks to any fixed-size payload — including the `shared_ptr` control block,
// which is the part RVec cannot reach.
//
// Chunks are deliberately NEVER returned to the OS. A probe that freed them
// from a thread_local destructor would race the destruction of shared_ptrs
// that still point into them; a real implementation needs RVec's discipline
// (give blocks back at thread exit) and that is not what is being priced here.
struct SlabPool {
    // The cap has to clear the LARGEST payload block section A finds, or that
    // payload silently falls through to malloc and reads as "pooling does not
    // help it". `ObjectData` is 280 + a 24-byte control block = 304, and
    // `ValueHash`'s deque chunk is 513, so 256 was too low: it scored
    // ObjectData at 1.05x purely because the slab never saw it.
    static constexpr size_t kMaxSize = 1024;             // above this: straight to malloc
    static constexpr size_t kStep    = 16;
    static constexpr size_t kClasses = kMaxSize / kStep + 1;
    static constexpr size_t kChunk   = 64 * 1024;

    struct Node { Node* next; };
    Node*  free_[kClasses] = {};
    size_t chunks = 0;

    static size_t cls(size_t n) { return (n + kStep - 1) / kStep; }

    void* alloc(size_t n) {
        if (n == 0 || n > kMaxSize) return malloc(n ? n : 1);
        size_t c = cls(n);
        Node* h = free_[c];
        if (h) { free_[c] = h->next; return h; }
        return carve(c);
    }
    // Thread one whole chunk onto the class's list and hand back the first slot.
    // The free list is built back-to-front so the slots come out in ascending
    // address order, which is what a fresh bump allocator would have given.
    __attribute__((noinline)) void* carve(size_t c) {
        size_t slot = c * kStep;
        if (slot < sizeof(Node)) slot = sizeof(Node);
        size_t cnt = kChunk / slot;
        char* mem = (char*)malloc(kChunk);
        if (!mem) throw std::bad_alloc();
        chunks++;
        for (size_t i = cnt; i-- > 1;) {
            Node* nd = (Node*)(mem + i * slot);
            nd->next = free_[c];
            free_[c] = nd;
        }
        return mem;
    }
    void dealloc(void* p, size_t n) {
        if (n == 0 || n > kMaxSize) { free(p); return; }
        size_t c = cls(n);
        Node* nd = (Node*)p;
        nd->next = free_[c];
        free_[c] = nd;
    }
};
static SlabPool& slab() {
    static thread_local SlabPool p;
    return p;
}

// An Allocator that routes through the slab. `allocate_shared` rebinds this to
// an internal type whose size is NOT sizeof(T) — the control block and the
// object in one — so the size class must be taken at rebind time, which is
// exactly what keying on sizeof(U) inside allocate() does.
template <class T>
struct SlabAlloc {
    using value_type = T;
    SlabAlloc() = default;
    template <class U> SlabAlloc(const SlabAlloc<U>&) noexcept {}
    T* allocate(size_t n) { return (T*)slab().alloc(n * sizeof(T)); }
    void deallocate(T* p, size_t n) noexcept { slab().dealloc(p, n * sizeof(T)); }
    template <class U> bool operator==(const SlabAlloc<U>&) const noexcept { return true; }
    template <class U> bool operator!=(const SlabAlloc<U>&) const noexcept { return false; }
};

// The floor: a bump arena that never frees. Not a candidate design — it is the
// lower bound any reclaiming allocator is measured against.
struct Arena {
    char*  cur = nullptr;
    size_t left = 0;
    static constexpr size_t kChunk = 1u << 20;
    void* alloc(size_t n) {
        n = (n + 15) & ~size_t(15);
        if (n > left) { cur = (char*)malloc(kChunk); left = kChunk; }
        void* p = cur;
        cur += n;
        left -= n;
        return p;
    }
};
static Arena& arena() {
    static thread_local Arena a;
    return a;
}

static const int R = 7;

// ---------------------------------------------------------------------------
// A. what each Value shape actually asks malloc for
// ---------------------------------------------------------------------------
template <class F>
static void census(const char* label, F&& build) {
    censusReset();
    g_armed = true;
    build();
    g_armed = false;
    long long n = g_count, by = g_bytes;
    char sizes[256];
    int off = 0;
    sizes[0] = 0;
    for (size_t s = 0; s <= kCensusMax + 1 && off < 200; s++) {
        if (!g_hist[s]) continue;
        off += snprintf(sizes + off, sizeof(sizes) - off, "%s%zu", off ? " " : "", s);
        if (g_hist[s] > 1) off += snprintf(sizes + off, sizeof(sizes) - off, "x%lld", g_hist[s]);
    }
    printf("   %-26s %2lld block%s %5lld bytes   sizes: %s\n",
           label, n, n == 1 ? " " : "s", by, sizes);
}

int main() {
    printf("A. sizeof: Value %zu  ValueList %zu  ValueExt %zu  MatchData %zu"
           "  ValueHash %zu  ObjectData %zu\n",
           sizeof(Value), sizeof(ValueList), sizeof(ValueExt), sizeof(MatchData),
           sizeof(ValueHash), sizeof(ObjectData));
    printf("\n   allocation census — what ONE value of each shape costs the allocator\n");
    // Warm anything with a lazy one-time table before arming, so the census
    // records the shape's own blocks and not a first-call side effect — and in
    // particular warm RVec's capacity-1..4 free lists by building and dropping
    // a few small arrays. Without this the "Array (2 elems)" line charges the
    // shape for two data blocks (128 and 256) that a running program takes off
    // that pool, and the census overstates a warm interpreter by 2 of 3 blocks.
    { Value w = Value::array(); Value w2 = Value::makeHash(); Value w3 = Value::matchVal("x"); (void)w; (void)w2; (void)w3; }
    for (int w = 0; w < 8; w++) {
        Value v = Value::array();
        for (int k = 0; k < 4; k++) v.arr()->push_back(Value::integer(k));
    }
    census("Int",             [] { Value v = Value::integer(7); (void)v; });
    census("Str (short)",     [] { Value v = Value::str("id"); (void)v; });
    census("Str (promoted)",  [] { Value v = Value::str(std::string(80, 'x')); (void)v; });
    census("Array (empty)",   [] { Value v = Value::array(); (void)v; });
    census("Array (2 elems)", [] { Value v = Value::array(); v.arr()->push_back(Value::integer(1)); v.arr()->push_back(Value::integer(2)); });
    census("Hash (empty)",    [] { Value v = Value::makeHash(); (void)v; });
    census("Hash (3 keys)",   [] { Value v = Value::makeHash(); for (int k = 0; k < 3; k++) (*v.hash())[std::string(1, 'a' + k)] = Value::integer(k); });
    census("Match",           [] { Value v = Value::matchVal("hello"); (void)v; });
    census("ValueExt only",   [] { Value v = Value::integer(1); v.ofTypeM() = "Int"; });
    census("Object",          [] { Value v = Value::object(std::make_shared<ObjectData>()); (void)v; });

    // -----------------------------------------------------------------------
    // B. the drop-in: same ownership, different allocator
    // -----------------------------------------------------------------------
    {
        const int BN = 3000000;
        printf("\nB. make_shared<T> vs allocate_shared<T,Slab>, ctor+dtor x%d\n", BN);
        printf("   %-12s %10s %12s %8s\n", "payload", "make(ms)", "slab(ms)", "ratio");
#define BPAIR(T, LBL)                                                              \
        {                                                                          \
            double mk = 1e18, sl = 1e18;                                           \
            for (int r = 0; r < R; r++) {                                          \
                auto t0 = clk::now(); size_t sink = 0;                             \
                for (int k = 0; k < BN; k++) { auto p = std::make_shared<T>(); sink += (size_t)p.get(); } \
                auto t1 = clk::now(); mk = std::min(mk, ms(t0, t1)); if (!sink) abort(); \
            }                                                                      \
            for (int r = 0; r < R; r++) {                                          \
                auto t0 = clk::now(); size_t sink = 0;                             \
                for (int k = 0; k < BN; k++) { auto p = std::allocate_shared<T>(SlabAlloc<T>{}); sink += (size_t)p.get(); } \
                auto t1 = clk::now(); sl = std::min(sl, ms(t0, t1)); if (!sink) abort(); \
            }                                                                      \
            printf("   %-12s %10.2f %12.2f %7.2fx\n", LBL, mk, sl, mk / sl);       \
        }
        BPAIR(ValueList,  "ValueList")
        BPAIR(ValueExt,   "ValueExt")
        BPAIR(MatchData,  "MatchData")
        BPAIR(ValueHash,  "ValueHash")
        BPAIR(ObjectData, "ObjectData")
#undef BPAIR
    }

    // -----------------------------------------------------------------------
    // C. the real op — a whole Value of each shape, built and destroyed
    // -----------------------------------------------------------------------
    {
        const int CN = 2000000;
        printf("\nC. build + destroy a whole Value, x%d  (slab column = payloads\n"
               "   allocate_shared'd; RVec's own 1..4 data pool is active in BOTH)\n", CN);
        printf("   %-16s %10s %12s %8s\n", "shape", "today(ms)", "slab(ms)", "ratio");

        double a = 1e18, b = 1e18;
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); size_t sink = 0;
            for (int k = 0; k < CN; k++) { Value v = Value::array(); sink += (size_t)v.arr(); }
            auto t1 = clk::now(); a = std::min(a, ms(t0, t1)); if (!sink) abort();
        }
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); size_t sink = 0;
            for (int k = 0; k < CN; k++) {
                Value v; v.t = VT::Array;
                v.setArr(std::allocate_shared<ValueList>(SlabAlloc<ValueList>{}));
                sink += (size_t)v.arr();
            }
            auto t1 = clk::now(); b = std::min(b, ms(t0, t1)); if (!sink) abort();
        }
        printf("   %-16s %10.2f %12.2f %7.2fx\n", "Array (empty)", a, b, a / b);

        // The argument list every interpreted call builds: one ValueList with
        // two elements. RVec pools the DATA block; only the control block is
        // in play here.
        a = b = 1e18;
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); size_t sink = 0;
            for (int k = 0; k < CN; k++) {
                Value v = Value::array();
                v.arr()->push_back(Value::integer(1));
                v.arr()->push_back(Value::integer(2));
                sink += v.arr()->size();
            }
            auto t1 = clk::now(); a = std::min(a, ms(t0, t1)); if (!sink) abort();
        }
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); size_t sink = 0;
            for (int k = 0; k < CN; k++) {
                Value v; v.t = VT::Array;
                v.setArr(std::allocate_shared<ValueList>(SlabAlloc<ValueList>{}));
                v.arr()->push_back(Value::integer(1));
                v.arr()->push_back(Value::integer(2));
                sink += v.arr()->size();
            }
            auto t1 = clk::now(); b = std::min(b, ms(t0, t1)); if (!sink) abort();
        }
        printf("   %-16s %10.2f %12.2f %7.2fx\n", "arg list (2)", a, b, a / b);

        // A Match: four payload allocations in one value (ValueExt, MatchData,
        // ValueList, ValueMap) — the densest allocator shape the engine has.
        a = b = 1e18;
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); size_t sink = 0;
            for (int k = 0; k < CN; k++) { Value v = Value::matchVal("hello"); sink += (size_t)v.md(); }
            auto t1 = clk::now(); a = std::min(a, ms(t0, t1)); if (!sink) abort();
        }
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); size_t sink = 0;
            for (int k = 0; k < CN; k++) {
                Value v; v.t = VT::Match; v.s = CowStr("hello");
                v.x_ = std::allocate_shared<ValueExt>(SlabAlloc<ValueExt>{});
                auto m = std::allocate_shared<MatchData>(SlabAlloc<MatchData>{});
                m->pos   = std::allocate_shared<ValueList>(SlabAlloc<ValueList>{});
                m->named = std::allocate_shared<ValueMap>(SlabAlloc<ValueMap>{});
                v.setMatch(std::move(m));
                sink += (size_t)v.md();
            }
            auto t1 = clk::now(); b = std::min(b, ms(t0, t1)); if (!sink) abort();
        }
        printf("   %-16s %10.2f %12.2f %7.2fx\n", "Match", a, b, a / b);

        a = b = 1e18;
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); size_t sink = 0;
            for (int k = 0; k < CN; k++) { Value v = Value::object(std::make_shared<ObjectData>()); sink += (size_t)v.obj(); }
            auto t1 = clk::now(); a = std::min(a, ms(t0, t1)); if (!sink) abort();
        }
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); size_t sink = 0;
            for (int k = 0; k < CN; k++) { Value v = Value::object(std::allocate_shared<ObjectData>(SlabAlloc<ObjectData>{})); sink += (size_t)v.obj(); }
            auto t1 = clk::now(); b = std::min(b, ms(t0, t1)); if (!sink) abort();
        }
        printf("   %-16s %10.2f %12.2f %7.2fx\n", "Object", a, b, a / b);
    }

    // -----------------------------------------------------------------------
    // D. churn shape. Sections B and C are pure LIFO, which is a slab's best
    //    case AND a tree-walking interpreter's normal case (a temporary dies
    //    before the one built before it). A long-lived structure churns in a
    //    different order, so price that too rather than quoting only the win.
    // -----------------------------------------------------------------------
    {
        const int LIVE = 200000, OPS = 3000000;
        printf("\nD. live set of %d, %d replacements in RANDOM order\n", LIVE, OPS);
        uint64_t seed = 0x9E3779B97F4A7C15ull;
        auto nextIdx = [&]() {
            seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
            return (size_t)(seed % LIVE);
        };
        double a = 1e18, b = 1e18;
        for (int r = 0; r < R; r++) {
            std::vector<std::shared_ptr<ValueList>> live(LIVE);
            for (int k = 0; k < LIVE; k++) live[k] = std::make_shared<ValueList>();
            seed = 0x9E3779B97F4A7C15ull;
            auto t0 = clk::now();
            for (int k = 0; k < OPS; k++) live[nextIdx()] = std::make_shared<ValueList>();
            auto t1 = clk::now(); a = std::min(a, ms(t0, t1));
        }
        for (int r = 0; r < R; r++) {
            std::vector<std::shared_ptr<ValueList>> live(LIVE);
            for (int k = 0; k < LIVE; k++) live[k] = std::allocate_shared<ValueList>(SlabAlloc<ValueList>{});
            seed = 0x9E3779B97F4A7C15ull;
            auto t0 = clk::now();
            for (int k = 0; k < OPS; k++) live[nextIdx()] = std::allocate_shared<ValueList>(SlabAlloc<ValueList>{});
            auto t1 = clk::now(); b = std::min(b, ms(t0, t1));
        }
        printf("   ValueList        make %8.2f ms   slab %8.2f (%.2fx)\n", a, b, a / b);
    }

    // -----------------------------------------------------------------------
    // E. threads. The engine runs work in parallel, so the question is not only
    //    "is a free list faster than malloc" but "does it still scale".
    // -----------------------------------------------------------------------
    {
        const int TN = 1500000;
        printf("\nE. %d ValueList ctor+dtor per thread, wall clock\n", TN);
        printf("   %8s %10s %12s %8s\n", "threads", "make(ms)", "slab(ms)", "ratio");
        for (int T : {1, 2, 4, 8}) {
            double a = 1e18, b = 1e18;
            for (int r = 0; r < R; r++) {
                std::vector<std::thread> th;
                auto t0 = clk::now();
                for (int t = 0; t < T; t++) th.emplace_back([&] {
                    size_t sink = 0;
                    for (int k = 0; k < TN; k++) { auto p = std::make_shared<ValueList>(); sink += (size_t)p.get(); }
                    if (!sink) abort();
                });
                for (auto& x : th) x.join();
                auto t1 = clk::now(); a = std::min(a, ms(t0, t1));
            }
            for (int r = 0; r < R; r++) {
                std::vector<std::thread> th;
                auto t0 = clk::now();
                for (int t = 0; t < T; t++) th.emplace_back([&] {
                    size_t sink = 0;
                    for (int k = 0; k < TN; k++) { auto p = std::allocate_shared<ValueList>(SlabAlloc<ValueList>{}); sink += (size_t)p.get(); }
                    if (!sink) abort();
                });
                for (auto& x : th) x.join();
                auto t1 = clk::now(); b = std::min(b, ms(t0, t1));
            }
            printf("   %8d %10.2f %12.2f %7.2fx\n", T, a, b, a / b);
        }
    }

    // -----------------------------------------------------------------------
    // F. the floor — what is left after the free list, if reclamation itself
    //    were free. A big gap here means the remaining cost is the refcount and
    //    the constructor, not the allocator, and no allocator work will get it.
    // -----------------------------------------------------------------------
    {
        const int FN = 3000000;
        double mk = 1e18, sl = 1e18, ar = 1e18, raw = 1e18;
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); size_t sink = 0;
            for (int k = 0; k < FN; k++) { auto p = std::make_shared<ValueList>(); sink += (size_t)p.get(); }
            auto t1 = clk::now(); mk = std::min(mk, ms(t0, t1)); if (!sink) abort();
        }
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); size_t sink = 0;
            for (int k = 0; k < FN; k++) { auto p = std::allocate_shared<ValueList>(SlabAlloc<ValueList>{}); sink += (size_t)p.get(); }
            auto t1 = clk::now(); sl = std::min(sl, ms(t0, t1)); if (!sink) abort();
        }
        for (int r = 0; r < R; r++) {
            auto t0 = clk::now(); size_t sink = 0;
            for (int k = 0; k < FN; k++) {
                void* m = arena().alloc(sizeof(ValueList));
                ValueList* p = new (m) ValueList();
                sink += (size_t)p;
                p->~ValueList();
            }
            auto t1 = clk::now(); ar = std::min(ar, ms(t0, t1)); if (!sink) abort();
        }
        for (int r = 0; r < R; r++) {          // no shared_ptr at all: slab + placement new
            auto t0 = clk::now(); size_t sink = 0;
            for (int k = 0; k < FN; k++) {
                void* m = slab().alloc(sizeof(ValueList));
                ValueList* p = new (m) ValueList();
                sink += (size_t)p;
                p->~ValueList();
                slab().dealloc(m, sizeof(ValueList));
            }
            auto t1 = clk::now(); raw = std::min(raw, ms(t0, t1)); if (!sink) abort();
        }
        printf("\nF. ValueList ctor+dtor x%d\n", FN);
        printf("   make_shared      %8.2f ms   (1.00x)\n", mk);
        printf("   allocate_shared  %8.2f ms   (%.2fx)   drop-in, refcount kept\n", sl, mk / sl);
        printf("   slab, no shared  %8.2f ms   (%.2fx)   free list, no control block\n", raw, mk / raw);
        printf("   bump arena       %8.2f ms   (%.2fx)   never frees — the floor\n", ar, mk / ar);
    }
    return 0;
}
