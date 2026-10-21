# Regression: `.sum` and a junction's eigenstates take an object through its
# .Numeric (2026-10-21). `$a + $b` over two objects with a `method Numeric`
# answered through it, but `.sum` (and `sum(…)`, which delegates to it) folded
# with the built-in `+` over the object taken as 0, and a junction ran its
# operator per eigenstate the same way: `($a, $b).sum` was 0 and
# `any($a, $b) == 9` any(False, False) where Rakudo says 19 and
# any(False, True). `.sum` is CORE's, so a user `multi infix:<+>` does not
# take part, here or in Rakudo.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

class W { has $.x; method Numeric { $!x } }
my ($a, $b) = W.new(x => 10), W.new(x => 9);

ck(($a, $b).sum, 19, '`.sum` over objects');
ck(sum($a, $b), 19, '`sum(…)` over objects');
ck(($a, 5, 2.5).sum, 17.5, '`.sum` over an object, an Int and a Rat');
ck([$a, $b].sum, 19, '`.sum` over an Array of objects');
ck(($a, $b).sum.WHAT, Int, '…an Int when the numbers are Ints');

class R does Real { has $.v; method Bridge { $!v.Num } }
ck((R.new(v => 1), R.new(v => 2)).sum, 3e0, '`.sum` over Real objects goes by .Bridge');
class MyInt is Int { }
ck((MyInt.new(3), MyInt.new(4)).sum, 7, '`.sum` over `is Int` objects');

# CORE's `+`: a user multi is not consulted by `.sum`
{
    multi sub infix:<+>(W $p, W $q) { "plus" }
    ck(($a, $b).sum, 19, '`.sum` does not take a user `multi infix:<+>`');
}

ck((any($a, $b) == 9).raku, 'any(Bool::False, Bool::True)', 'an object eigenstate under `==`');
ck(so(9 == any($a, $b)), True, '…on either side');
ck((all($a, $b) != 9).raku, 'Bool::True', '`!=` collapses as a negated `==`');
ck((any($a, $b) + 1).raku, 'any(11, 10)', 'an object eigenstate under `+`');
ck(((1 | $a) * 2).raku, 'any(2, 20)', 'a mixed junction under `*`');
ck(($a == any(10, 11)).raku, 'any(Bool::True, Bool::False)', 'an object against a junction');
ck(so(none($a, $b) == 8), True, '`none` over objects');
ck((one($a, $b) == 10).raku, 'one(Bool::True, Bool::False)', '`one` over objects');
ck((any($a, $b) - any(1, 2)).raku, 'any(any(9, 8), any(8, 7))', 'two junctions, objects in the left');
ck(so(any($a, $b) ~~ 9), True, 'a smartmatch against a number (it already worked)');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
