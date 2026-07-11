#!/usr/bin/env raku
# A little number-theory workshop: prime factorization, divisor counts, and
# the two classic partners gcd and lcm. Shows trial-division factoring, hash
# tallies for exponents, and Raku's built-in `gcd` / `lcm` infix operators
# (which also work as reductions, `[gcd]` / `[lcm]`).

# Trial division: pull out each prime factor and tally its exponent in a hash.
sub prime-factors($n is copy) {
    my %exp;
    my $d = 2;
    while $d * $d <= $n {
        while $n %% $d {
            %exp{$d}++;
            $n = $n div $d;
        }
        $d++;
    }
    %exp{$n}++ if $n > 1;        # whatever is left is a prime
    %exp;
}

# Render 360 as "2^3 * 3^2 * 5" (a lone prime keeps no exponent).
sub factor-string(%exp) {
    return '1' unless %exp;
    %exp.keys.sort({ $^a <=> $^b }).map(-> $p {
        %exp{$p} == 1 ?? "$p" !! "$p^%exp{$p}"
    }).join(' * ');
}

say 'Prime factorizations (with divisor counts):';
for 360, 97, 1024, 5040, 600851475143 -> $n {
    my %e = prime-factors($n);
    # The number of divisors is the product of (exponent + 1) over all primes.
    my $divisors = [*] %e.values.map(* + 1);
    $divisors = 1 unless %e;
    say sprintf('  %-14d = %-22s  %d divisors',
                $n, factor-string(%e), $divisors);
}
say '';

# gcd and lcm as infix operators. gcd via Euclid is a one-liner too, shown
# here as a check that the built-in agrees.
sub my-gcd($a is copy, $b is copy) {
    ($a, $b) = $b, $a % $b while $b;
    $a;
}

say 'gcd and lcm (built-in infix vs. Euclid by hand):';
for (48, 36), (1071, 462), (17, 5) -> ($a, $b) {
    my $g = $a gcd $b;
    my $l = $a lcm $b;
    my $check = my-gcd($a, $b) == $g ?? 'ok' !! 'MISMATCH';
    say "  $a, $b:  gcd = $g  lcm = $l   (Euclid check: $check)";
}
say '';

# Reductions fold the operator across a whole list.
say 'Reductions across a list:';
my @nums = 24, 60, 36, 90;
say "  numbers      = @nums[]";
say "  [gcd] @nums[] = ", [gcd] @nums;
say "  [lcm] @nums[] = ", [lcm] @nums;
