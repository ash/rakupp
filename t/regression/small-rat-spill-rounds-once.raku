# Regression: a Rat result that spills to Num is the nearest double.
#
# A Rat whose denominator would pass 2**64 becomes a Num. The small-Rat lane
# (parts under 2**62, exact in __int128) made that Num as `(double)n /
# (double)d`: two conversions and a division, three roundings, and one spill
# in four came out an ulp off, `1/4611686018427387903 + 3/999999999999999989`
# 3.2168404344971007e-18 where Rakudo, and our own BigInt arms, give
# 3.216840434497101e-18 (ledger O33 of the 2026-10 parallel review). It now
# rounds once, as Rat.Num does. The want is the same sum taken in FatRat,
# which does not spill, converted at the end.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck 1/4611686018427387903 + 3/999999999999999989, 3.216840434497101e-18, 'the example';
ck 1/4611686018427387903 * 3/1000000000000000003, 6.505213034913026e-37, 'a product';

my @d = 4611686018427387903, 4611686018427387847, 3037000493, 2305843009213693951,
        1000000000000000003, 999999999999999989;
my @n = 1, 3, -7, 123456789012345, 4611686018427387000, -2305843009213693900;
my ($bad, $all) = 0, 0;
for @n -> $n1 { for @d -> $d1 { for @n -> $n2 { for @d -> $d2 {
    next if $d1 == $d2;
    my $a = $n1 / $d1;
    my $b = $n2 / $d2;
    for <+ - * /> -> $op {
        my $r = do given $op { when '+' { $a + $b }; when '-' { $a - $b }; when '*' { $a * $b }; default { $a / $b } };
        next unless $r ~~ Num;
        my ($fa, $fb) = $a.FatRat, $b.FatRat;
        my $exact = do given $op { when '+' { $fa + $fb }; when '-' { $fa - $fb }; when '*' { $fa * $fb }; default { $fa / $fb } };
        $all++;
        next if $r == $exact.Num;
        $bad++;
        say "FAIL: $n1/$d1 $op $n2/$d2 — {$r.raku} vs {$exact.Num.raku}" if $bad <= 5;
    }
}}}}
ck $bad, 0, "every one of $all spills is the nearest double";
ck $all > 1000, True, '…and there were spills to check';

# in a loop, where the typed lanes and the kernels hand the spill on
my ($q1, $q2, $s) = 1/4611686018427387903, 3/999999999999999989;
for ^3 { $s = $q1 + $q2 }
ck ($s, $s.WHAT), (3.216840434497101e-18, Num), 'a spill in a loop';

say $fails ?? "FAIL ($fails)" !! 'PASS';
