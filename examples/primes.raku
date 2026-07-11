#!/usr/bin/env raku
# A short tour of number theory, leaning on Raku's arbitrary-precision
# integers (there is no 64-bit ceiling — factorials and Mersenne numbers
# grow to whatever size they need) and the lazy sequence operator `...`.

# Sieve of Eratosthenes as a lazy, self-referential sequence: each new
# prime is the next number not divisible by any prime already found.
my @primes = lazy gather {
    my @seen;
    my $n = 2;
    loop {
        unless @seen.first: $n %% * {
            @seen.push: $n;
            take $n;
        }
        $n++;
    }
}

say 'First 20 primes:';
say @primes[^20];

say '';
say 'Primes between 100 and 130:';
say @primes[^40].grep({ 100 <= $_ <= 130 });

# Arbitrary precision: factorials well past what a machine int could hold.
say '';
say 'Factorials:';
for 10, 25, 50 -> $n {
    say "  $n! = ", [*] 1 .. $n;
}

# Mersenne primes: numbers of the form 2^p - 1 that are themselves prime.
say '';
say 'Mersenne primes 2^p - 1 (p prime, p < 32):';
for @primes[^11] -> $p {
    my $m = 2 ** $p - 1;
    say "  p=$p  2^{$p}-1 = $m" if $m.is-prime;
}

# The Collatz (3n+1) sequence: the stopping time for a few seeds.
sub collatz-steps($start) {
    my $n = $start;
    my $steps = 0;
    while $n != 1 {
        $n = $n %% 2 ?? $n div 2 !! 3 * $n + 1;
        $steps++;
    }
    $steps;
}

say '';
say 'Collatz stopping times:';
for 6, 27, 97 -> $n {
    say "  $n reaches 1 in {collatz-steps($n)} steps";
}
