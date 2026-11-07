/* kernels.c KERNEL [PASSES] : the kernels of kernels.f90 over double*, the same
   loops in the same order. The arrays are built once and the kernel runs
   PASSES times (5); prints the checksum, then the milliseconds of the fastest
   pass. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(int argc, char **argv) {
    const char *k = argc > 1 ? argv[1] : "";
    const int passes = argc > 2 ? atoi(argv[2]) : 5;
    const long n = 10000000, m = 200;
    double s = 0, best = 1e300;
    if (!strcmp(k, "matmul")) {
        double *x = malloc(8 * m * m), *y = malloc(8 * m * m), *z = malloc(8 * m * m);
        for (long i = 0; i < m * m; i++) { x[i] = (i % 7) * 0.25; y[i] = (i % 5) * 0.5; }
        for (int r = 0; r < passes; r++) {
            double t0 = now();
            for (long i = 0; i < m; i++)
                for (long j = 0; j < m; j++) {
                    double t = 0;
                    for (long q = 0; q < m; q++) t += x[i * m + q] * y[q * m + j];
                    z[i * m + j] = t;
                }
            double ms = (now() - t0) * 1000;
            if (ms < best) best = ms;
        }
        for (long i = 0; i < m * m; i++) s += z[i];
    } else {
        double *a = malloc(8 * n), *b = malloc(8 * n), *c = malloc(8 * n);
        for (long i = 0; i < n; i++) { a[i] = i * 1e-7; b[i] = -(i * 1e-7); }
        for (int r = 0; r < passes; r++) {
            double t0 = now();
            if (!strcmp(k, "sum")) { s = 0; for (long i = 0; i < n; i++) s += a[i]; }
            else if (!strcmp(k, "dot")) { s = 0; for (long i = 0; i < n; i++) s += a[i] * b[i]; }
            else if (!strcmp(k, "axpy")) for (long i = 0; i < n; i++) c[i] = 2.0 * a[i] + b[i];
            else { fprintf(stderr, "kernel?\n"); return 1; }
            double ms = (now() - t0) * 1000;
            if (ms < best) best = ms;
        }
        if (!strcmp(k, "axpy")) { s = 0; for (long i = 0; i < n; i++) s += c[i]; }
    }
    printf("%.16e\n%.3f\n", s, best);
    return 0;
}
