# Regression: a nested list on the left of a plain assignment destructured
# its value instead of flattening. Rakudo's List.STORE flattens the left
# side one level into a list of containers; only a `my (…)` declaration
# destructures. The JS backend refused the nested target until this was
# settled, and now flattens the same way.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged; every
# expectation was checked against Rakudo 2026.08 on 2026-10-19.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my ($a, $b, $c, $d);
($a, ($b, $c), $d) = 1, 2, 3, 4;
ck(($a, $b, $c, $d).raku, '(1, 2, 3, 4)', 'the nested targets take one value each');
($a, ($b, $c), $d) = 1, (2, 3), 4;
ck(($a, $b, $c, $d).raku, '(1, $(2, 3), 4, Any)', 'a nested list value is one item');
($a, ($b, ($c, $d))) = 5, 6, 7, 8;
ck(($a, $b, $c, $d).raku, '(5, 6, 7, Any)', 'one level only: the inner list takes one value');
(my $m, ($b, $c)) = 1, 2, 3;
ck(($m, $b, $c).raku, '(1, 2, 3)', 'a declaration inside the parens still flattens');
my @arr;
($a, (@arr)) = 1, 2, 3;
ck(@arr, [2, 3], 'a nested array slurps the rest');
my ($x, ($y, $z)) = 7, [8, 9];
ck(($x, $y, $z).raku, '(7, 8, 9)', 'my (…) destructures an Array');
my ($p, ($q, $r)) = 7, (8, 9);
ck(($p, $q, $r).raku, '(7, 8, 9)', 'and a List');
my ($s, $t) := (1, (2, 3));
ck($t.raku, '$(2, 3)', 'binding is unchanged');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
