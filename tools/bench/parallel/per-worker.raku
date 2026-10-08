# What does a live worker cost the threads around it? — each worker's own time.
#
# One unit of `work` is timed alone first. Then N workers each run one unit
# and time themselves. A contention-free fan-out leaves every worker at its
# solo time; anything above it is a cost the workers pay for each other.
#
#     rakupp tools/bench/parallel/per-worker.raku 4 300000
#
# `work` is cpu-fanout.raku's: an LCG step and an accumulate in `my int`
# locals, no shared state. The sums are printed so both sides can be checked.

my $N = (@*ARGS[0] // 4).Int;          # worker threads
my $M = (@*ARGS[1] // 300_000).Int;    # iterations per unit

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
my $solo-sum = work(0);
my $solo = now - $t0;

my @r = await (^$N).map: -> $w {
    start {
        my $t = now;
        my $s = work($w);
        ($s, now - $t)
    }
};
my @t = @r.map(*[1]);

say sprintf 'per-worker N=%d M=%d  %.3fs  solo %.3fs  min %.3fs  max/solo %.2f  sum=%d',
    $N, $M, @t.max, $solo, @t.min, @t.max / $solo, $solo-sum + @r.map(*[0]).sum;
