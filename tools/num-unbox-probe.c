// NUM-UNBOX-PLAN.md's probe, the C reference for tools/num-unbox-probe.raku,
// row for row. -ffp-contract=off is the contract generated code keeps
// (KERNEL-PLAN.md, item 4): no multiply fused into an add.
//   cc -O2 -ffp-contract=off tools/num-unbox-probe.c -o /tmp/nupc && /tmp/nupc
// Every input arrives through a volatile or a global the clock call might
// write, so nothing folds or hoists out of the timing loop.
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static volatile double X = 1.5, F27 = 27, HALF = 0.5, QUARTER = 0.25;
static volatile long N27 = 27;
static volatile double B15 = 1.5;
double* BIG; double TEN[10];   // not static: opaque calls may write them

__attribute__((noinline)) static double scal(double x) {
    double s = 0; for (long i = 0; i < 10000000; i++) s = s + x * 1.0000001; return s;
}
__attribute__((noinline)) static long fib(long n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
__attribute__((noinline)) static double fibf(double n) { return n < 2 ? n : fibf(n - 1) + fibf(n - 2); }
__attribute__((noinline)) static double lerp(double a, double b, double t) { double d = b - a; return a + d * t; }
__attribute__((noinline)) static double lerps(double x) {
    double s = 0; for (long i = 0; i < 10000000; i++) s = lerp(s, x, 1e-7); return s;
}
__attribute__((noinline)) static double asum(const double* a, long k) {
    double s = 0; for (int p = 0; p < 10; p++) for (long i = 0; i < k; i++) s = s + a[i]; return s;
}
__attribute__((noinline)) static double sum10(const double* a) { double s = 0; for (int i = 0; i < 10; i++) s = s + a[i]; return s; }
__attribute__((noinline)) static double sum10s(const double* a) {
    // (the clobber: sum10 only reads, and would otherwise be called once)
    double s = 0; for (long i = 0; i < 1000000; i++) { __asm__ volatile("" ::: "memory"); s = s + sum10(a); } return s;
}
// step 4, as the heap array a Raku Array is...
__attribute__((noinline)) static double* mk3(double a, double b) {
    double* v = malloc(3 * sizeof(double)); v[0] = a + b; v[1] = a - b; v[2] = a * b; return v;
}
__attribute__((noinline)) static double mk3s(double b) {
    double s = 0;
    for (long i = 0; i < 1000000; i++) { double* v = mk3(s, b); s = v[0] * 1e-9 + v[2] * 1e-12; free(v); }
    return s;
}
// ...and as Fortran writes it: the caller's buffer, nothing allocated
__attribute__((noinline)) static void mk3into(double a, double b, double* v) { v[0] = a + b; v[1] = a - b; v[2] = a * b; }
__attribute__((noinline)) static double mk3s_noalloc(double b) {
    double s = 0, v[3];
    for (long i = 0; i < 1000000; i++) { mk3into(s, b, v); s = v[0] * 1e-9 + v[2] * 1e-12; }
    return s;
}

#define T(name, expr, per) do { \
    double best = 1e300, r = 0; \
    for (int q = 0; q < 5; q++) { double t0 = now_(); r = (double)(expr); double d = (now_() - t0) / (per) * 1e9; if (d < best) best = d; } \
    printf("%-46s %8.2f ns   %.17g\n", name, best, r); } while (0)

int main(void) {
    long k = 1000000;
    BIG = malloc(k * sizeof(double)); for (long i = 0; i < k; i++) BIG[i] = i * HALF;
    for (int i = 0; i < 10; i++) TEN[i] = i * QUARTER;
    T("control: F64 loop lane, per iteration", scal(X), 1e7);
    T("control: fib(27) on Int, per call", fib(N27), 635621);
    T("step 1: fib(27) on Num, per call", fibf(F27), 635621);
    T("step 1: a leaf Num sub with a local, per call", lerps(X), 1e7);
    T("step 3: 1M-Num Array by index, per element", asum(BIG, k), 1e7);
    T("step 3: 10-Num Array summed, per call", sum10s(TEN), 1e6);
    T("step 4: 3-element array per call", mk3s(B15), 1e6);
    T("step 4: the same, caller's buffer (Fortran)", mk3s_noalloc(B15), 1e6);
    return 0;
}
