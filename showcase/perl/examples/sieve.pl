# Sieve of Eratosthenes: arrays, nested loops, list context.
my $n = 50;
my @is_prime = (1) x ($n + 1);
$is_prime[0] = 0;
$is_prime[1] = 0;
for my $i (2 .. $n) {
    next unless $is_prime[$i];
    for (my $j = $i * $i; $j <= $n; $j += $i) {
        $is_prime[$j] = 0;
    }
}
my @primes = grep { $is_prime[$_] } (2 .. $n);
print "primes up to $n: @primes\n";
print "count: ", scalar(@primes), "\n";
