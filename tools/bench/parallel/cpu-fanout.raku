# Does `start` actually make this faster? — the contention-free control.
#
# The same total work either way: N units of M iterations each. The only
# difference between the two modes is whether the units run in `start` blocks.
# The work is CPU-bound and touches no shared mutable state, so nothing but the
# scheduler stands between the workers and the cores.
#
#     ./build/rakupp tools/bench/parallel/cpu-fanout.raku 4 300000 serial
#     RAKUPP_PARALLEL=1 ./build/rakupp tools/bench/parallel/cpu-fanout.raku 4 300000 parallel
#
# Both modes print the same `sum=` — that is the honesty check. If the numbers
# differ, the two sides are not doing the same computation and the timing is
# meaningless.
#
# See docs/guide/PARALLEL-SPEEDUP.md for the measured table.

my $N    = (@*ARGS[0] // 4).Int;          # units of work / worker threads
my $M    = (@*ARGS[1] // 300_000).Int;    # iterations per unit
my $mode = @*ARGS[2] // 'parallel';       # serial | parallel

# A linear congruential step plus an accumulate: pure integer arithmetic in
# thread-local `int` natives, no allocation, no shared state.
sub work($seed) {
    my int $s = 0;
    my int $x = $seed;
    for ^$M {
        $x = ($x * 1103515245 + 12345) % 2147483647;
        $s = $s + ($x % 7);
    }
    $s
}

my $t0 = now;
my @r = $mode eq 'parallel'
    ?? await (^$N).map({ start work($_) })
    !! (^$N).map({ work($_) }).list;
my $dt = now - $t0;

say sprintf '%-8s N=%d M=%d  %.3fs  sum=%d', $mode, $N, $M, $dt, @r.sum;
