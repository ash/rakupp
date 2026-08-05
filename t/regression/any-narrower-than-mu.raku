# Regression: `Any` is NARROWER than `Mu` in multi dispatch. Both matched
# everything and scored the same, so declaration order decided — Data::Dump
# writes `multi Dump(Mu $obj) { $obj.gist }` ahead of the real
# `multi Dump(Any $obj)`, and every call got the gist.
# The exceptions matter: a Junction is Mu but NOT Any, and neither is Mu itself.
# Every expectation checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

multi f(Mu $x)  { 'Mu' }
multi f(Any $x) { 'Any' }

ck f(42),      'Any', 'an Int is an Any';
ck f({a => 1}), 'Any', 'so is a Hash';
ck f('s'),     'Any', 'and a Str';
ck f((1, 2)),  'Any', 'and a List';
ck f(Int),     'Any', 'a TYPE object is an Any too';
ck f(Any),     'Any', 'Any itself';
ck f(Mu),      'Mu',  'but Mu is not an Any';
ck f(1 | 2),   'Mu',  'and neither is a Junction';

# declaration order must not matter
multi g(Any $x) { 'Any' }
multi g(Mu $x)  { 'Mu' }
ck g(42), 'Any', 'the other declaration order agrees';

# a real type still beats Any, and Any still beats Mu
multi h(Mu $x)   { 'Mu' }
multi h(Any $x)  { 'Any' }
multi h(Cool $x) { 'Cool' }
ck h(42), 'Cool', 'a nominal type is narrower than either';
ck h(Mu), 'Mu',   'and Mu still catches what nothing else does';

# the shape Data::Dump actually uses: a `Mu` catch-all declared FIRST
multi dump-it(Mu $o, :$color = False) { $o.gist }
multi dump-it(Any $o, :$color = False) { 'formatted' }
ck dump-it(%(a => 1)), 'formatted', 'a Hash reaches the Any candidate';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
