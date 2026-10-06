# Regression: the in-place appends of `$s ~= X` and `$s = $s ~ X` (issue #130)
# step aside for a user's `infix:<~>`, which takes the operands it is narrower
# for — in a loop a kernel takes and in one it does not, compiled as
# interpreted. (Its own file: a user `~` anywhere in a program sends every
# append down the general path, so it cannot share one with the timing checks
# of cat-assign-shapes.raku.)
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

multi infix:<~>(Str:D $a, Str:D $b where 'b') { "$a+$b" }

{ my $s = 'a'; for ^2 { $s = $s ~ 'b'; $s.say if False }; ck($s, 'a+b+b', '$s = $s ~ X outside a kernel') }
{ my $s = 'a'; $s = $s ~ 'b' for ^2; ck($s, 'a+b+b', '$s = $s ~ X in a modifier loop') }
{ my $s = 'a'; $s ~= 'b' for ^2; ck($s, 'a+b+b', '$s ~= X') }
{ my $s = 'a'; $s ~= 'c'; $s = $s ~ 'd'; ck($s, 'acd', 'operands it does not take join as ever') }

say $fails ?? "FAILED $fails" !! "PASS";
