/* The hardware ceiling for cpu-fanout.raku: the same LCG loop on C threads.
 *
 * N units of M iterations, run one after another (`serial`) or on N pthreads
 * (`parallel`). The ratio of the two is what the machine allows for this
 * loop; a Raku++ fan-out is judged against that ratio, not against N.
 *
 *     cc -O2 -o ceiling tools/bench/parallel/ceiling.c -lpthread
 *     ./ceiling 4 300000000 serial
 *     ./ceiling 4 300000000 parallel
 *
 * M is large because C runs the loop about a thousand times faster than an
 * interpreter does; the ratio, not the time, is the number to compare.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static long M;

struct unit { long seed, sum; };

static void *work(void *arg) {
    struct unit *u = arg;
    long s = 0, x = u->seed;
    for (long i = 0; i < M; i++) {
        x = (x * 1103515245 + 12345) % 2147483647;
        s = s + (x % 7);
    }
    u->sum = s;
    return NULL;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 4;
    M = argc > 2 ? atol(argv[2]) : 300000000L;
    const char *mode = argc > 3 ? argv[3] : "parallel";
    if (n < 1 || n > 256) { fprintf(stderr, "N must be 1..256\n"); return 2; }
    struct unit u[256];
    pthread_t th[256];
    for (int i = 0; i < n; i++) u[i].seed = i;
    double t0 = now();
    if (strcmp(mode, "parallel") == 0) {
        for (int i = 0; i < n; i++) pthread_create(&th[i], NULL, work, &u[i]);
        for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
    }
    else {
        for (int i = 0; i < n; i++) work(&u[i]);
    }
    double dt = now() - t0;
    long sum = 0;
    for (int i = 0; i < n; i++) sum += u[i].sum;
    printf("%-8s N=%d M=%ld  %.3fs  sum=%ld\n", mode, n, M, dt, sum);
    return 0;
}
