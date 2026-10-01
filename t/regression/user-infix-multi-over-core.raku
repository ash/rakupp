# Regression: a user `multi infix:<op>` over CORE operand types was never
# consulted — the Binary fast paths answered `$x + $y` from the built-in for
# two Ints whatever the program declared. In Rakudo the user candidate joins
# the operator's dispatch: narrower than the core candidate (a `where`, a
# subset, a literal, a narrower type, `is default`) it wins; tied with it
# (`Int $a, Int $b` beside the core `Int:D, Int:D`) the call is ambiguous;
# broader (`Numeric`) the core candidate wins.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub dies-ambiguous(&code) {
    code();
    False;
    CATCH { default { return .^name eq 'X::Multi::Ambiguous' } }
}

{
    multi sub infix:<+>(Int:D $a, Int:D $b where * > 0) { 42 }
    my $x = 1; my $y = 2;
    ck $x + $y, 42, 'a where-constrained Int candidate wins over the built-in';
    ck $x + -1, 0, '…and the built-in answers what it refuses';
    my $s = 0;
    for 1..3 { $s = $s + $_ }
    ck $s, 42, '…in a loop, after the first iteration';
}
{
    multi sub infix:<+>(Int $a, Int $b) is default { 42 }
    my $x = 1;
    ck $x + 1, 42, 'an `is default` candidate wins';
    ck 1.5 + 2, 3.5, '…over Int operands only';
}
{
    subset Pos of Int where * > 0;
    multi sub infix:<*>(Pos $a, Pos $b) { 'pos' }
    my $x = 3;
    ck $x * 2, 'pos', 'a subset candidate wins';
    ck $x * 0, 0, '…and lets through what it rejects';
}
{
    multi sub infix:<*>(Int:D $a, 2) { 'twice' }
    my $x = 3;
    ck $x * 2, 'twice', 'a literal parameter wins';
    ck $x * 3, 9, '…for its own value only';
}
{
    multi sub infix:<+>(Int $a, Num $b) { 'int-num' }
    multi sub infix:<+>(Int $a, Rat $b) { 'int-rat' }
    ck 1 + 2e0, 'int-num', 'a mixed-type candidate is narrower than the core Real one';
    ck 1 + 1.5, 'int-rat', '…for each type pair it names';
    ck 1 + 2, 3, '…and is not consulted for two Ints';
}
{
    multi sub infix:<eq>(Int $a, Int $b) { 'int-eq' }
    ck 1 eq 1, 'int-eq', 'a string comparison over Ints is the user candidate';
    ck 'a' eq 'a', True, '…and two Strs still meet the built-in';
}
{
    multi sub infix:<+>(Numeric $a, Numeric $b) { 42 }
    my $x = 1;
    ck $x + 2, 3, 'a broader candidate loses to the built-in';
}
{
    multi sub infix:<+>(Int $a, Int $b) { 42 }
    my $x = 1; my $y = 2;
    ck dies-ambiguous({ $x + $y }), True, 'a candidate tied with the built-in is ambiguous';
    ck dies-ambiguous({ 1 + 2 }), True, '…between literals too';
    ck 1.5 + 2, 3.5, '…and other operand types are untouched';
}
{
    multi sub infix:<==>(Int:D $a, Int:D $b) { 42 }
    my $x = 5;
    ck dies-ambiguous({ $x == 5 }), True, 'a tied comparison is ambiguous';
}
my $x = 1; my $y = 2;
ck $x + $y, 3, 'outside the blocks the built-in answers';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
