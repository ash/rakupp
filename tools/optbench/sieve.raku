# Count primes below N by trial division. The inner loop is all `*`, `<=`, `%%`,
# `++` — every one an inline int op under `-O`, none of them boxed.
my $count = 0;
for 2 .. 200_000 -> $n {
    my $prime = True;
    my $d = 2;
    while $d * $d <= $n {
        if $n %% $d { $prime = False; last; }
        $d++;
    }
    $count++ if $prime;
}
say $count;
