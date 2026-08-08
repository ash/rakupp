# Embarrassingly parallel map — the scaling kernel for the v3 parallel
# campaign (PARALLEL-PLAN.md, P5 gate: ≥3× wall-clock on 8 cores vs the
# same binary's single-thread run, under RAKUPP_PARALLEL=1).
#
#   rakupp tools/bench/parmap.raku            # workers = 8
#   rakupp tools/bench/parmap.raku 1          # single-thread reference
#
# Pure CPU per worker, no sharing: each start block sums squares mod 7 over
# its own range and returns one number; await joins.
my $N = (@*ARGS[0] // 8).Int;
my $M = (@*ARGS[1] // 400_000).Int;
sub work($lo, $hi) {
    my $s = 0;
    loop (my $i = $lo; $i < $hi; $i++) { $s += $i * $i % 7 }
    $s
}
my $t0 = now;
my @r = await (^$N).map: -> $w { start work($w * $M, ($w + 1) * $M) };
my $wall = now - $t0;
say "workers=$N items-each=$M sum={@r.sum} wall={($wall * 1000).Int}ms";
