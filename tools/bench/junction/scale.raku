# What does the engine's real parallelism actually deliver on this box, for a
# chunk of work the size of a junction leg? This is the ceiling any parallel
# junction could reach.
sub work(int $n --> int) { my int $s = 0; loop (my int $i = 0; $i < $n; $i++) { $s = $s + $i }; $s }
for 4000, 40000 -> $unit {
    for 1, 2, 4, 8 -> $n {
        my $reps = 20;
        my ($ser, $par);
        my $t0 = now; for ^$reps { my int $x = 0; for ^$n { $x = $x + work($unit) } }; $ser = (now - $t0)/$reps;
        $t0 = now; for ^$reps { await (^$n).map({ start work($unit) }) }; $par = (now - $t0)/$reps;
        printf "unit=%-6d n=%-2d serial=%8.1f us  parallel=%8.1f us  speedup=%.2fx\n",
            $unit, $n, $ser*1e6, $par*1e6, $ser/$par;
    }
}
