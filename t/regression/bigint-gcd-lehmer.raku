# Regression: `gcd` of big Ints was Euclid over long division, one base-1e9
# divmod per quotient, and a gcd of n-bit numbers takes about 0.58·n of
# them: (3**30_000 + 1) gcd 2**47_500 took 0.91 s (Rakudo: 7 ms), and every
# Rat and FatRat construction reduces by a gcd, so
# FatRat.new(3**300_000 + 1, 2**475_000) ran for minutes. Past 64 bits it is
# Lehmer's algorithm over the binary form now, after taking out the factors
# of two (src/BigInt.cpp, gcdWords), and those are milliseconds.
#
# Expected values never come from `gcd`: they are built in. p·m and p·n for
# m and n coprime by construction have gcd p and lcm p·m·n; 2**m - 1 and
# 2**n - 1 have gcd 2**gcd(m, n) - 1, repunits likewise in base ten, and
# Fibonacci numbers F(m), F(n) have gcd F(gcd(m, n)) — with m and n chosen so
# that gcd(m, n) is plain from their factors.
#
# Contract: exit 0 + last line PASS. Passes under both engines (Rakudo
# 2026.09), so it doubles as an oracle.

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eqv $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got.raku.substr(0, 200)}] WANT [{$want.raku.substr(0, 200)}]";
    }
}

# A family of cases reports one line, naming the first case that missed.
my (%miss, %count);
sub agree(Str $family, Str $case, $got, $want) {
    %count{$family}++;
    %miss{$family} //= "$case: GOT [{$got.raku.substr(0, 120)}] WANT [{$want.raku.substr(0, 120)}]"
        unless $got eqv $want;
}
sub family-done(Str $family) {
    if %miss{$family}:exists {
        $fails++;
        say "not ok - $family";
        note %miss{$family};
    }
    else {
        say "ok - $family ({%count{$family} // 0} cases)";
    }
}

# 1. QUADRATIC DETECTORS, not benchmarks. Each took seconds to minutes
#    before and takes milliseconds now; the bounds sit far above the second,
#    for a slow CI runner, and under the first. Every operand is odd, so the
#    factors of two cannot shortcut what is being timed — except in the
#    FatRat, which is the case as it was reported.
{
    my $p = 7 ** 8_000 + 2;                      # odd, 22,459 bits
    my $x = $p * 3 ** 45_000;                    # about 94,000 bits each
    my $y = $p * 5 ** 31_000;
    my $t0 = now;
    my $g = $x gcd $y;
    my $mid = now - $t0;
    $t0 = now;
    my $r = (2 ** 100_000 - 1) gcd (2 ** 70_000 - 1);
    my $ones = now - $t0;
    $t0 = now;
    my $f = FatRat.new(3 ** 300_000 + 1, 2 ** 475_000);
    my $fat = now - $t0;
    check 'p·3**45_000 gcd p·5**31_000 is p', $g, $p;
    check '(2**100_000 - 1) gcd (2**70_000 - 1) is 2**10_000 - 1', $r, 2 ** 10_000 - 1;
    check 'FatRat.new(3**300_000 + 1, 2**475_000) cancels a 2', $f.denominator, 2 ** 474_999;
    check 'and the numerator is half of 3**300_000 + 1', $f.numerator * 2, 3 ** 300_000 + 1;
    check 'the 94,000-bit gcd is not quadratic', $mid < 0.5, True;
    check 'the 100,000-bit gcd is not quadratic', $ones < 0.5, True;
    check 'the FatRat is not quadratic', $fat < 1, True;
    if $mid >= 0.5 || $ones >= 0.5 || $fat >= 1 {
        note "gcds {$mid.round(0.001)} s and {$ones.round(0.001)} s, FatRat {$fat.round(0.001)} s; skipping the rest";
        say 'FAIL';
        exit 1;
    }
}

# 2. p·m and p·n, m and n coprime by construction: powers of distinct primes,
#    a power of two against an odd number (the factors of two come out before
#    Lehmer runs), and n = m·k + 1. Sizes from one word past the u64 path to
#    thousands of words, either sign, either order.
{
    my @p = 2 ** 64 + 13, 10 ** 30 + 7, 3 ** 500 + 2, 7 ** 3_000 + 4, 2 ** 64 * 11;
    my @mn = (3 ** 40, 5 ** 30), (3 ** 2_000, 5 ** 1_500), (2 ** 9_000, 3 ** 5_000 + 2),
             (2 ** 65, 2 ** 64 + 1), (11 ** 4_000, 11 ** 4_000 * 13 ** 50 + 1),
             (2 ** 128 - 1, 2 ** 128 + 1), (10 ** 1_000 + 1, 10 ** 1_000 - 1);
    for @p -> $p {
        for @mn -> ($m, $n) {
            my $case = "p={$p.msb + 1} bits, m={$m.msb + 1} bits, n={$n.msb + 1} bits";
            agree 'p·m gcd p·n is p', $case, ($p * $m) gcd ($p * $n), $p;
            agree 'p·m gcd p·n is p', $case, ($p * $n) gcd -($p * $m), $p;
            agree 'p·m gcd p·n is p', $case, -($p * $n) gcd -($p * $m), $p;
            agree 'p·m lcm p·n is p·m·n', $case, ($p * $m) lcm ($p * $n), $p * $m * $n;
        }
    }
}
family-done 'p·m gcd p·n is p';
family-done 'p·m lcm p·n is p·m·n';

# 3. Runs of ones, in binary and in decimal: (2**m - 1) gcd (2**n - 1) is
#    2**gcd(m, n) - 1, and the repunits R(m), R(n) have gcd R(gcd(m, n)).
#    Each pair's gcd(m, n) is the factor written out.
for (100, 70, 10), (128, 64, 64), (129, 86, 43), (6_000, 4_000, 2_000), (7_919, 7_907, 1),
    (12_288, 8_192, 4_096), (30_000, 21_000, 3_000), (65, 64, 1) -> ($m, $n, $d) {
    agree '2**m - 1 gcd 2**n - 1', "m=$m n=$n", (2 ** $m - 1) gcd (2 ** $n - 1), 2 ** $d - 1;
    agree '2**m - 1 gcd 2**n - 1', "m=$m n=$n, negated", (1 - 2 ** $n) gcd (2 ** $m - 1), 2 ** $d - 1;
}
family-done '2**m - 1 gcd 2**n - 1';
for (60, 45, 15), (300, 200, 100), (1_001, 1_000, 1), (3_000, 1_800, 600), (6_007, 6_000, 1) -> ($m, $n, $d) {
    agree 'repunits', "m=$m n=$n", ('1' x $m).Int gcd ('1' x $n).Int, ('1' x $d).Int;
}
family-done 'repunits';

# 4. Fibonacci numbers: every Euclid quotient between neighbours is 1, the
#    most steps per bit there are, and gcd(F(m), F(n)) is F(gcd(m, n)).
{
    my %need = (3_000, 6_000, 9_000, 12_000, 12_001, 12_002, 15_000, 18_000).map(* => True);
    my %F;
    my ($a, $b) = 0, 1;
    for 1 .. 18_000 -> $i { ($a, $b) = $b, $a + $b; %F{$i} = $a if %need{$i} }
    for (12_001, 12_000, 1), (12_002, 12_000, 2), (18_000, 12_000, 6_000), (15_000, 9_000, 3_000),
        (18_000, 15_000, 3_000), (12_000, 9_000, 3_000) -> ($m, $n, $d) {
        my $fd = $d == 1 || $d == 2 ?? 1 !! %F{$d};
        agree 'Fibonacci', "F($m) gcd F($n)", %F{$m} gcd %F{$n}, $fd;
        agree 'Fibonacci', "-F($n) gcd F($m)", -%F{$n} gcd %F{$m}, $fd;
    }
}
family-done 'Fibonacci';

# 5. A quotient too big for the leading bits to decide — the step Lehmer
#    takes by a full division — and the operands that make it: one short and
#    one long, and the long one an exact multiple of the short.
{
    my $s = 3 ** 100 + 2;                        # odd and prime to 3
    for 1, 2, 5, 64, 1_000, 40_000 -> $k {
        my $big = 3 ** $k * 2 ** 7 * $s;
        agree 'one short, one long', "3**$k·2**7·s gcd s", $big gcd $s, $s;
        agree 'one short, one long', "3**$k·2**7·s + 1 gcd s", ($big + 1) gcd $s, 1;
        agree 'one short, one long', "3**$k·2**7·s gcd 2**9·s", $big gcd (2 ** 9 * $s), 2 ** 7 * $s;
        agree 'one short, one long', "3**$k gcd 3**($k + 1)", 3 ** $k gcd 3 ** ($k + 1), 3 ** $k;
        agree 'one short, one long', "x gcd 0", $big gcd 0, $big;
        agree 'one short, one long', "0 gcd -x", 0 gcd -$big, $big;
        agree 'one short, one long', "x gcd x", $big gcd $big, $big;
        agree 'one short, one long', "x gcd 1", $big gcd 1, 1;
    }
}
family-done 'one short, one long';

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
