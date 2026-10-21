# Regression: the integer operators over a Num past the int64 range, and
# -2**63 over -1.
#
# `div`, `mod`, `gcd`, `lcm` and the bitwise `+&` `+|` `+^` take integers,
# and a Num operand of 2**63 or more went through toInt(), which saturates at
# 2**63-1, or through a raw cast, which is undefined there:
#
#   1e20 div 7    # 1317624576693539401, Rakudo 14285714285714285714
#   1e20 +& 255   # 255, Rakudo 0
#   1e20 gcd 10   # 1, Rakudo 10
#
# A double that large has no fraction, so it is now converted to the Int it
# exactly is. And the run-time helpers the compiled tiers share (rtDiv, rtMod,
# rtModOp, rtDivides) divided -2**63 by -1 in C++, which is undefined (SIGFPE
# on x86-64); a divisor of -1 now takes the long way. Found by the 2026-10-21
# review.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck 1e20 div 7, 14285714285714285714, '1e20 div 7';
ck -1e19 div 3, -3333333333333333334, '-1e19 div 3 floors';
ck 1e20 mod 7, 0e0, '1e20 mod 7';
ck 1e20 +& 255, 0, '1e20 +& 255';
ck -1e20 +& 255, 0, '-1e20 +& 255';
ck 1e19 +| 1, 10000000000000000001, '1e19 +| 1';
ck 1e19 +^ 1, 10000000000000000001, '1e19 +^ 1';
ck 1e20 gcd 10, 10, '1e20 gcd 10';
ck -1e19 gcd 4, 4, '-1e19 gcd 4';
ck 1e20 lcm 3, 300000000000000000000, '1e20 lcm 3';
ck 9.3e18 div 1, 9300000000000000000, 'just past 2**63';
ck 2**62 * 1e0 div 3, 1537228672809129301, '…and just inside it, as before';
ck 5e0 div 2, 2, 'a small Num, as before';

my $a = -2**63; my $b = -1;
my ($q, $m, $mo, $dv);
for ^200 { $q = $a div $b; $m = $a % $b; $mo = $a mod $b; $dv = $a %% $b }
ck ($q, $m, $mo, $dv), (9223372036854775808, 0, 0, True), '-2**63 over -1, in a loop';
my $c = 7; my $d = -1;
for ^200 { $q = $c div $d; $m = $c % $d }
ck ($q, $m), (-7, 0), '7 over -1, in a loop';

say $fails ?? "FAIL ($fails)" !! 'PASS';
