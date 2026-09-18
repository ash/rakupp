# per-eigenstate cost of autothreading a smartmatch over a junction
# junction is built ONCE (n-ary ctor), then matched REPS times.
sub bench(int $w, int $reps) {
    my @e = (1 .. $w);
    my $j = any(@e);
    my $miss = $w + 1;           # worst case: no eigenstate matches
    my $t0 = now;
    my int $c = 0;
    for ^$reps { $c++ if $miss ~~ $j }
    my $dt = now - $t0;
    my $per = $dt / $reps;
    printf "w=%-6d reps=%-7d total=%8.4fs  per-match=%9.3f us  per-eigenstate=%7.1f ns  (hits=%d)\n",
        $w, $reps, $dt, $per * 1e6, $per * 1e9 / $w, $c;
}
bench(3, 200000);
bench(10, 100000);
bench(100, 20000);
bench(1000, 2000);
bench(10000, 200);
