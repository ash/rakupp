#!/usr/bin/env raku
# Exact rational arithmetic. In Raku a decimal literal like 0.1 is a `Rat`
# (a numerator/denominator pair), not a floating-point approximation, so
# sums that famously go wrong in binary floating point stay exact here.

say 'Floating-point folklore, done exactly:';
say "  0.1 + 0.2        = {0.1 + 0.2}";          # 0.3, not 0.30000000000000004
say "  0.1 + 0.2 - 0.3  = {0.1 + 0.2 - 0.3}";    # exactly 0
say "  (0.1 + 0.2).nude = {(0.1 + 0.2).nude.join('/')}";  # 3/10 — a real Rat
say '';

# The harmonic numbers H_n = 1 + 1/2 + 1/3 + ... + 1/n are rationals whose
# denominators blow up fast. With a real bignum tower they stay exact.
say 'Harmonic numbers H_n (exact):';
my $h = 0;
for 1 .. 10 -> $n {
    $h += 1 / $n;
    say "  H_$n = {$h.nude.join('/')}" if $n == any(1, 5, 10);
}
say '';

# Continued-fraction convergents of pi: each is the best rational
# approximation of pi for its denominator size. 355/113 is famous for
# being accurate to seven digits.
say 'Rational approximations of pi from its continued fraction:';
my @cf = 3, 7, 15, 1, 292, 1, 1;
for 1 .. @cf -> $len {
    my ($num, $den) = 1, 0;
    for @cf[^$len].reverse -> $a {
        ($num, $den) = $a * $num + $den, $num;
    }
    my $approx = $num / $den;
    say sprintf('  %-9s = %.10f  (error %.2e)',
                "$num/$den", $approx, abs($approx - pi));
}
say '';

# Any rational round-trips exactly through its own continued fraction.
sub to-cf(Rat $r is copy) {
    my @terms;
    loop {
        @terms.push: $r.floor;
        my $frac = $r - $r.floor;
        last if $frac == 0;
        $r = 1 / $frac;
    }
    @terms;
}

my $x = 355/113;
say "Continued fraction of {$x.nude.join('/')} is ", to-cf($x).raku;
