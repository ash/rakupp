# Does the autothread loop short-circuit? If it did, a hit at the FRONT
# would cost a fraction of a hit at the BACK.
my int $w = 2000;
my @e = (1 .. $w);
my $j = any(@e);
for (1, 'first'), ($w div 2, 'middle'), ($w, 'last'), ($w+1, 'none') -> ($needle, $where) {
    my $reps = 500;
    my $t0 = now;
    my int $c = 0;
    for ^$reps { $c++ if $needle ~~ $j }
    my $dt = (now - $t0) / $reps;
    printf "hit at %-7s  %8.1f us per match  (%5.1f ns/eigenstate)  matched=%s\n",
        $where, $dt*1e6, $dt*1e9/$w, $c > 0;
}
