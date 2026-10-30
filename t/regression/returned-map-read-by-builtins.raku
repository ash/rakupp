# Regression: a map or grep a routine returns is a lazy Seq since c0691dff,
# and the built-in subs and operators that walk the elements an argument
# holds found none: it holds nothing until something drains it. `flat(f(),
# $x)`, `slip`, `cache`, `zip`, `cross`, `any`, `first`, infix `X` and the
# triangle reduce `[\+]` all saw an empty list; methods, and `flat` alone,
# already drained it.
#
# Cro's CompositeConnector builds its pipeline as `flat(before, $con-tran,
# after)` with `before` a `.map` from a sub, so a ConnectionConditional ran
# none of its transforms, and Cro::Core's connection-conditional test got
# its own input message back ("No such method 'message' for invocant of type
# 'TestMessageA'").
#
# Expectations checked against Rakudo 2026.09
# (~/.rakubrew/versions/moar-2026.09/install/bin/rakudo). Green on both, and
# on the build before c0691dff.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @c = 3, 1, 2;
sub colon-map(@x) { @x.map: -> $v { $v } }
sub paren-map(@x) { @x.map({ $_ * 10 }) }
sub returned(@x) { return @x.map: -> $v { $v + 1 } }
sub pairs-of(@x) { @x.map: -> $v { ($v, $v).flat } }
sub positive(@x) { @x.grep: * > 0 }

# flat with more than one argument (Cro)
ck flat(colon-map(@c), 'x').List, (3, 1, 2, 'x'), 'flat: then another argument';
ck flat('w', paren-map(@c)).List, ('w', 30, 10, 20), 'flat: after another argument';
ck flat(returned(@c), returned(@c)).List, (4, 2, 3, 4, 2, 3), 'flat: two of them';
ck flat(pairs-of(@c), 'x').List, (3, 3, 1, 1, 2, 2, 'x'), 'flat: each element a list';
ck flat((colon-map(@c), 1), 'x').List, (3, 1, 2, 1, 'x'), 'flat: one inside a list argument';
ck flat(positive(@c), 'x').List, (3, 1, 2, 'x'), 'flat: a returned grep';
my \before = True ?? colon-map(@c) !! @c;
ck flat(before, 'x').List, (3, 1, 2, 'x'), 'flat: bound to a sigilless name, as Cro does';
ck flat(colon-map(@c)).List, (3, 1, 2), 'flat: alone, as before';

# the other built-in subs
ck (slip(colon-map(@c)), 'x').List, (3, 1, 2, 'x'), 'slip';
ck cache(colon-map(@c)).List, (3, 1, 2), 'cache';
ck zip(colon-map(@c), <a b c>).List, ((3, 'a'), (1, 'b'), (2, 'c')), 'zip';
ck cross(colon-map(@c), <a>).List, ((3, 'a'), (1, 'a'), (2, 'a')), 'cross';
ck so(any(colon-map(@c)) == 2), True, 'any';
ck first(* > 1, colon-map(@c)), 3, 'first';

# operators
ck (colon-map(@c) X <a>).List, ((3, 'a'), (1, 'a'), (2, 'a')), 'infix X';
ck (colon-map(@c) Z <a b c>).List, ((3, 'a'), (1, 'b'), (2, 'c')), 'infix Z, as before';
ck ([\+] colon-map(@c)).List, (3, 4, 6), 'the triangle reduce';
ck ([+] colon-map(@c)), 6, 'the reduce, as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
