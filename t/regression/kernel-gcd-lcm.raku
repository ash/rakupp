# Regression: `gcd` and `lcm` in the loop and sub kernels.
#
# The kernels declined `operator gcd`, so a loop over `$i gcd 7` ran in the
# interpreter at ~1,170 instructions an iteration where `%` runs at ~70. They
# now compute both on int64s as applyArith's Int arm does (rtIntWordOp, on
# the magnitudes), and bail where the answer needs a BigInt: -2**63 gcd 0,
# -2**63 gcd -2**63, an lcm past int64. A Rat or Num operand is declined.
# Every answer is Rakudo 2026.09's.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my ($g, $l) = 0, 0;
for ^200 -> $i { $g = ($i - 100) gcd 12; $l = ($i - 100) lcm 8 }
ck ($g, $l), (3, 792), 'negative and positive operands';
my $x = -9223372036854775808;
my ($r, $r2) = 0, 0;
for ^3 { $r = $x gcd 0; $r2 = $x gcd $x }
ck ($r, $r2), (9223372036854775808, 9223372036854775808), '-2**63 gcd 0 and gcd itself: big Ints';
my ($y, $z) = 9223372036854775807, 4611686018427387904;
my ($s, $s2) = 0, 0;
for ^3 { $s = $y lcm 2; $s2 = $z lcm 3 }
ck ($s, $s2), (18446744073709551614, 13835058055282163712), 'an lcm past int64';
my @small;
for ^3 { @small = 0 gcd 0, 0 lcm 5, -4 lcm -6, -12 gcd -18 }
ck @small, [0, 0, 12, 6], 'zeros and two negatives';
sub gk($a, $b) { $a gcd $b }
sub lk($a, $b) { $a lcm $b }
my $t = 0;
for 1..100 -> $i { $t += gk($i, 36) }
ck ($t, lk(4, 6), lk(0, 3), lk(-4, 6)), (450, 12, 0, 12), 'in a sub kernel';
my $c = 0;
for 1..1000 -> $i { $c++ if ($i gcd 30) == 1 }
ck $c, 266, 'in a condition';
my ($sum, $j) = 0, 7;
for 1..1000 -> $i { $sum = $sum + ($i lcm $j) }
ck $sum, 3077074, 'summed';
my $mix = 0;
for ^200 -> $i { $mix = $mix + ($i gcd 7) + ($i lcm 3) - ($i gcd $mix) }
ck $mix, 42484, 'over a variable the loop writes';
my ($e, $f) = 0, 0;
for ^3 { $e = 1.5 gcd 3; $f = 6e0 gcd 4 }
ck ($e, $f, $f.WHAT), (1, 2, Int), 'a Rat or a Num operand, as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
