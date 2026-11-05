// throw-census.c — a DYLD interposer that counts the C++ exceptions a process
// throws and the time each spends between the throw and its catch.
//
// V6-PLAN P0 asks how much of `--exe`'s time goes to exceptions: the claim
// that they dominate was measured on the WASM build, where `-fexceptions`
// routes every call that might throw through a JS trampoline. A native binary
// pays only when it throws, and then for the search and the unwind: the time
// from __cxa_throw (or __cxa_rethrow) to the __cxa_begin_catch that ends it.
// This adds those intervals up, per thread, and prints the total at exit.
//
//   cc -dynamiclib -O2 tools/throw-census.c -lc++ -o /tmp/throw-census.dylib
//   DYLD_INSERT_LIBRARIES=/tmp/throw-census.dylib ./a.out
//
// tools/throw-census.raku runs it over the bench kernels, interpreted and
// under `--exe`. A throw that no handler catches ends the process without a
// catch, and is counted but not timed.
#include <mach/mach_time.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static _Atomic long long g_throws;
static _Atomic long long g_catches;
static _Atomic unsigned long long g_ticks;
static _Thread_local uint64_t t_start;

void __cxa_throw(void*, void*, void (*)(void*));
void __cxa_rethrow(void);
void* __cxa_begin_catch(void*);

// Calls to the replacee from inside this image are not interposed, so these
// reach libc++abi's own functions.
static void my_throw(void* obj, void* tinfo, void (*dest)(void*)) {
    atomic_fetch_add_explicit(&g_throws, 1, memory_order_relaxed);
    t_start = mach_absolute_time();
    __cxa_throw(obj, tinfo, dest);
}
static void my_rethrow(void) {
    atomic_fetch_add_explicit(&g_throws, 1, memory_order_relaxed);
    t_start = mach_absolute_time();
    __cxa_rethrow();
}
static void* my_begin_catch(void* exc) {
    if (t_start) {
        atomic_fetch_add_explicit(&g_ticks, mach_absolute_time() - t_start, memory_order_relaxed);
        atomic_fetch_add_explicit(&g_catches, 1, memory_order_relaxed);
        t_start = 0;
    }
    return __cxa_begin_catch(exc);
}

// Written with write(2) from a stack buffer: printf at exit could allocate.
__attribute__((destructor)) static void dump(void) {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    unsigned long long ns = atomic_load(&g_ticks) * tb.numer / tb.denom;
    char buf[160];
    int n = snprintf(buf, sizeof buf, "\n--- throw census ---\nthrows %lld  caught %lld  ns %llu\n",
                     atomic_load(&g_throws), atomic_load(&g_catches), ns);
    write(2, buf, n);
}

typedef struct interpose_s {
    const void* replacement;
    const void* replacee;
} interpose_t;

__attribute__((used)) static const interpose_t g_interposers[]
    __attribute__((section("__DATA,__interpose"))) = {
        {(const void*)my_throw, (const void*)__cxa_throw},
        {(const void*)my_rethrow, (const void*)__cxa_rethrow},
        {(const void*)my_begin_catch, (const void*)__cxa_begin_catch},
};
