// malloc-census.c — a DYLD interposer that counts every malloc a real rakupp
// run makes, bucketed by exact size.
//
// PAYLOAD-SLAB-PLAN.md prices a slab at ~3x on an allocator operation, but a
// probe cannot say how much of a PROGRAM is allocator operations, and the
// profile line it leans on ("malloc family 21.5%") does not separate poolable
// fixed-size payloads from strings, deque chunks and AST nodes.
//
// This does. The probe's census gives each payload a signature size — 48 for a
// `ValueList` control block, 88 `MatchData`, 136 `ValueHash`, 192 `ValueExt`,
// 304 `ObjectData` — so a histogram of exact sizes from a real run attributes
// the traffic without guessing.
//
//   cc -dynamiclib -O2 tools/malloc-census.c -o /tmp/malloc-census.dylib
//   DYLD_INSERT_LIBRARIES=/tmp/malloc-census.dylib ./rakupp tools/bench/hash.raku
//
// Counting only: the real allocation still comes from the default zone, so
// nothing about the program's behaviour changes except speed. `free` is left
// alone — a default-zone pointer frees normally.
#include <malloc/malloc.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CENSUS_MAX 4096

static _Atomic long long g_hist[CENSUS_MAX + 2];
static _Atomic long long g_calls;
static _Atomic long long g_bytes;

static inline void record(size_t n) {
    atomic_fetch_add_explicit(&g_calls, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_bytes, (long long)n, memory_order_relaxed);
    size_t b = n <= CENSUS_MAX ? n : CENSUS_MAX + 1;
    atomic_fetch_add_explicit(&g_hist[b], 1, memory_order_relaxed);
}

// The default zone directly, NOT malloc(): a call to malloc() from inside the
// interposer is itself interposed, which recurses until the stack ends.
static inline void* raw(size_t n) {
    return malloc_zone_malloc(malloc_default_zone(), n ? n : 1);
}

static void* my_malloc(size_t n) {
    record(n);
    return raw(n);
}
static void* my_calloc(size_t c, size_t n) {
    record(c * n);
    return malloc_zone_calloc(malloc_default_zone(), c, n);
}
static void* my_realloc(void* p, size_t n) {
    record(n);
    return malloc_zone_realloc(malloc_default_zone(), p, n);
}

// Written with write(2) from a stack buffer: printf at exit would allocate,
// and the numbers are already final by then.
__attribute__((destructor)) static void dump(void) {
    char buf[512];
    long long calls = atomic_load(&g_calls), bytes = atomic_load(&g_bytes);
    int n = snprintf(buf, sizeof buf,
                     "\n--- malloc census ---\ncalls %lld  bytes %lld\n"
                     "size  count      share\n", calls, bytes);
    write(2, buf, n);
    if (!calls) return;

    // Every size that is at least 0.5% of all calls, plus the payload
    // signatures whether or not they clear that bar.
    static const size_t sig[] = {48, 88, 136, 192, 304};
    long long pool = 0;
    for (size_t s = 0; s <= CENSUS_MAX + 1; s++) {
        long long c = atomic_load(&g_hist[s]);
        if (!c) continue;
        if (s <= 1024) pool += c;
        int isSig = 0;
        for (size_t k = 0; k < sizeof sig / sizeof *sig; k++)
            if (s == sig[k]) isSig = 1;
        if (c * 200 < calls && !isSig) continue;
        n = snprintf(buf, sizeof buf, "%4zu%s %10lld   %5.1f%%\n",
                     s, s > CENSUS_MAX ? "+" : " ", c, 100.0 * (double)c / (double)calls);
        write(2, buf, n);
    }
    n = snprintf(buf, sizeof buf,
                 "poolable (<=1024 bytes): %lld of %lld calls = %.1f%%\n",
                 pool, calls, 100.0 * (double)pool / (double)calls);
    write(2, buf, n);
}

typedef struct interpose_s {
    const void* replacement;
    const void* replacee;
} interpose_t;

__attribute__((used)) static const interpose_t g_interposers[]
    __attribute__((section("__DATA,__interpose"))) = {
        {(const void*)my_malloc, (const void*)malloc},
        {(const void*)my_calloc, (const void*)calloc},
        {(const void*)my_realloc, (const void*)realloc},
};
