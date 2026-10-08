# Example 3 of docs/guide/PARALLEL-SPEEDUP.md: the fan-out of a loop Raku++
# compiles, in the two shapes that decide where it is compiled.
#
#   outer — `work` reads `$M` from outside, so the routine is not compiled
#           whole and the LOOP is compiled on its own (a loop kernel).
#   param — `work` takes `$m` as a parameter and keeps its work in its own
#           locals, so the whole ROUTINE is compiled (a routine kernel).
#
#     rakupp tools/bench/parallel/kernel-fanout.raku 4 3000000 outer serial
#     rakupp tools/bench/parallel/kernel-fanout.raku 4 3000000 outer parallel
#
# `RAKUPP_KERNEL_TRACE=1` shows which of the two each run compiled.

my $N     = (@*ARGS[0] // 4).Int;
my $M     = (@*ARGS[1] // 3_000_000).Int;
my $shape = @*ARGS[2] // 'outer';      # outer | param
my $mode  = @*ARGS[3] // 'parallel';   # serial | parallel

sub work-outer($seed) {
    my $s = 0;
    my $x = $seed;
    for ^$M {
        $x = ($x * 1103515245 + 12345) % 2147483647;
        $s = $s + ($x % 7);
    }
    $s
}

sub work-param($seed, $m) {
    my $s = 0;
    my $x = $seed;
    for ^$m {
        $x = ($x * 1103515245 + 12345) % 2147483647;
        $s = $s + ($x % 7);
    }
    $s
}

my &unit = $shape eq 'param' ?? -> $i { work-param($i, $M) } !! -> $i { work-outer($i) };

my $t0 = now;
my @r = $mode eq 'parallel'
    ?? await (^$N).map({ start unit($_) })
    !! (^$N).map({ unit($_) }).list;
my $dt = now - $t0;

say sprintf '%-8s %-5s N=%d M=%d  %.3fs  sum=%d', $mode, $shape, $N, $M, $dt, @r.sum;
