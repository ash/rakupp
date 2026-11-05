# Regression: a reduce or a zip over CORE operands meets the program's own
# candidate for the operator, as the same operator written out does
# (2026-11-05, found by V6-PLAN P0's issue #47 driver). `2.0 lcm 6` with a
# `multi infix:<lcm>(Rat:D, Int:D)` declared took the candidate, but `[lcm]`,
# `[gcd]`, `[\lcm]`, `[+]`, `[max]` and `Z+` applied the built-in at each step
# whenever it could answer: applyBinOp asked a user operator only for an
# object operand or after the built-in died. Math::NumberTheory's
# carmichael-lambda ends in `[lcm]` over a Rat and its own Rat candidates, so
# it answered 12 where Rakudo answers 12.0. The metaop steps now go through
# metaInfixStep (InterpreterBinding.cpp).
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# an operator the parser makes a call of (`lcm`, `gcd`, `max`)
{
    multi infix:<lcm>(Rat:D $a, Int:D $b) { "mine-lcm" }
    multi infix:<gcd>(Rat:D $a, Int:D $b) { "mine-gcd" }
    multi infix:<max>(Rat:D $a, Int:D $b) { "mine-max" }
    ck(2.0 lcm 6, "mine-lcm", 'written out, `lcm` takes the candidate (it always did)');
    ck(([lcm] 2.0, 6), "mine-lcm", '`[lcm]` takes it');
    ck(([gcd] 2.0, 6), "mine-gcd", '`[gcd]` takes it');
    ck(([max] 2.0, 6), "mine-max", '`[max]` takes it');
    ck(([\lcm] 2.0, 6).List, (2.0, "mine-lcm"), '`[\lcm]` takes it at its step');
    ck(((2.0, 3.0) Zlcm (6, 7)).List, ("mine-lcm", "mine-lcm"), '`Zlcm` takes it');
    ck(((2.0,) Xgcd (6, 7)).List, ("mine-gcd", "mine-gcd"), '`Xgcd` takes it');
    # …but Rakudo's `Zmax`, `Xmax`, `Zmin` and `Xmin` are the built-ins
    ck(((2.0, 3.0) Zmax (6, 1)).List, (6, 3.0), '`Zmax` stays the built-in, as in Rakudo');
    ck(([lcm] 4, 6), 12, 'no candidate for two Ints: the built-in answers');
    ck(([max] 3, 9, 2), 9, '…for `[max]` too');
    ck(((4, 3) Zlcm (6, 5)).List, (12, 15), '…and at each step of `Zlcm`');
}
{
    # carmichael-lambda's shape: a Rat among the lambdas, and candidates that
    # keep the Rat
    multi infix:<lcm>(Rat:D $a, Int:D $b --> Rat:D) { ($a.Int lcm $b).Rat }
    my $l = [lcm] 2.0, 6, 4;
    ck($l, 12.0, '`[lcm]` over (2.0, 6, 4) is the Rat 12.0');
    ck($l.WHAT, Rat, '…a Rat, not the Int 12');
}
ck(([lcm] 2.0, 6), 6, 'outside the block that declared it, the candidate is not seen');

# an operator the parser keeps as a Binary (`+`): the candidate is judged
# against the built-in the operands meet
{
    multi infix:<+>(Rat:D $a, Int:D $b) { "mine-plus" }
    ck(2.0 + 6, "mine-plus", 'written out, `+` takes the candidate (it always did)');
    ck(([+] 2.0, 6), "mine-plus", '`[+]` takes it');
    ck(([R+] 6, 2.0), "mine-plus", '`[R+]` takes it');
    ck(((2.0, 3.0) Z+ (6, 7)).List, ("mine-plus", "mine-plus"), '`Z+` takes it');
    ck(((2.0,) X+ (6, 7)).List, ("mine-plus", "mine-plus"), '`X+` takes it');
    ck(([+] 1, 2, 3), 6, '`[+]` over Ints is the built-in');
    ck(((1, 2) Z+ (3, 4)).List, (4, 6), '…and so is `Z+`');
}
{
    multi infix:<+>(Int:D $a, Int:D $b where * > 100) { "big" }
    ck(1 + 200, "big", 'a `where` candidate over two Ints, written out');
    ck(([+] 1, 200), "big", '`[+]` takes it where the `where` holds');
    ck(([+] 1, 2), 3, '…and the built-in where it does not');
    ck(((1, 2) Z+ (300, 4)).List, ("big", 6), '`Z+` takes it step by step');
}
{
    # a `+` over the program's own class leaves Int folds to the built-in
    class Pt { has $.x }
    multi infix:<+>(Pt $a, Pt $b) { Pt.new(x => $a.x + $b.x) }
    ck(([+] Pt.new(x => 1), Pt.new(x => 2)).x, 3, '`[+]` over a class takes its candidate');
    ck(([+] 1, 2, 3, 4, 5, 6), 21, '`[+]` over Ints beside it is the built-in');
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
