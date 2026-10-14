# Regression: `gcd` and `lcm` on machine Ints took std::llabs of each operand,
# which has no long long answer for -2**63 and gave -2**63 back, so the result
# came out NEGATIVE: `-2**63 gcd 0`, `-2**63 gcd -2**63`, `-2**63 lcm 1` and
# every lcm with -2**63 whose answer passed 64 bits. Found by the first minute
# of t/oracle/int.raku (ORACLE-PLAN P2), Python's int as the oracle. The
# magnitudes are unsigned now.
#
# Expectations checked against Rakudo 2026.09. Green on both, and the same
# with RAKUPP_NO_KERNELS=1, under --jit=sync,threshold=0 and in an --exe.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $m = -9223372036854775808;            # -2**63, the one int64 without a positive twin
my $p = 9223372036854775808;

ck $m gcd 0,  $p, 'gcd of -2**63 and 0';
ck 0 gcd $m,  $p, 'gcd of 0 and -2**63';
ck $m gcd $m, $p, 'gcd of -2**63 with itself';
ck $m gcd 6,  2,  'gcd of -2**63 and 6';
ck $m lcm 1,  $p, 'lcm of -2**63 and 1';
ck 1 lcm $m,  $p, 'lcm of 1 and -2**63';
ck $m lcm $m, $p, 'lcm of -2**63 with itself';
ck $m lcm -1, $p, 'lcm of -2**63 and -1';
ck $m lcm 315594409110038029, 2910844647973230869622472361149202432,
   'lcm of -2**63 past 64 bits';
ck (2**63 - 1) lcm (2**63 - 2), 85070591730234615838173535747377725442,
   'lcm of the two largest int64s, past 64 bits';
ck -4 gcd 6,  2,  'gcd of a negative';
ck -4 lcm 6,  12, 'lcm of a negative';
ck 0 lcm 5,   0,  'lcm with 0';
ck 0 gcd 0,   0,  'gcd of zeros';

sub g($a, $b) { $a gcd $b }               # a plain sub, which the integer kernels take
sub l($a, $b) { $a lcm $b }
ck g($m, 0), $p, 'gcd of -2**63 and 0, in a sub';
ck l($m, 1), $p, 'lcm of -2**63 and 1, in a sub';

say $fails ?? 'FAIL' !! 'PASS';
