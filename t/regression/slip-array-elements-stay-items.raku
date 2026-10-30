# Regression: `|@a` passed an Array's elements without the item each holds,
# so a `*@` slurpy flattened the lists among them — `f(|@a)` for
# `@a = 1, [2, 3]` was `[1, 2, 3]`. Reading `@a[1]` gave the item; the slip
# did not. Cro::RPC::JSON hands a method's JSON arguments on with
# `$obj.$method(|@arg-pos)`, and a JSON array reached `*@pos` in pieces.
# A List's elements are not in Scalars, and those still flatten.
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub f(*@p) { @p.raku }
my @a = 1, [2, 3];
ck f(|@a), '[1, [2, 3]]', "a slipped Array's list element stays whole in *@";
ck f(@a[0], @a[1]), '[1, [2, 3]]', '…as it does read one by one';
ck f(|(1, [2, 3])), '[1, 2, 3]', "a List's flattens";
ck f(1, [2, 3]), '[1, 2, 3]', '…as a bare argument does';
my @h = 1, {a => 1};
ck f(|@h), '[1, {:a(1)}]', 'a hash element stays whole too';
my @c = "plain"; @c.append: ([1, 2],).List;
ck f(|@c), '["plain", [1, 2]]', 'an appended array element';
sub h(+@p) { @p.raku }
ck h(|@a), '[1, [2, 3]]', 'the +@ slurpy keeps it whole as well';
sub g(@x, $y) { "{@x.raku} {$y.raku}" }
my @gg = [1, 2], [3];
ck g(|@gg), '[1, 2] $[3]', 'a slipped element binds to @ and to $ as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
