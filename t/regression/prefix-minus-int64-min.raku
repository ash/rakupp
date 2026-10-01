# Regression: prefix `-` on the minimum int64 wrapped back to itself instead of
# promoting to a big Int. `-(-9223372036854775808)` printed
# -9223372036854775808; it is 9223372036854775808. Every spelling of the
# negation goes through Interpreter::prefixNumeric, which now promotes that one
# value.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $lm = -9223372036854775807 - 1;
ck -($lm * 1), 9223372036854775808, 'negating an expression that yields int64 min';
ck -$lm, 9223372036854775808, 'negating a variable holding int64 min';
ck (-$lm).WHAT, Int, '…is an Int';
ck -$lm - 1, 9223372036854775807, '…and arithmetic on it is exact';
ck -"-9223372036854775808", 9223372036854775808, 'negating a Str of int64 min';
ck @$lm.map(-*).List, (9223372036854775808,), 'a curried prefix minus';
ck -(-$lm), $lm, 'negating back gives int64 min';
ck -(9223372036854775807), -9223372036854775807, 'int64 max negates as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
