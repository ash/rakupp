# .nude, .numerator and .denominator belong to a Rat or a FatRat only
# (2026-11-06), as in Rakudo 2026.09. rakupp answered them for every value:
# `Any.nude` was (0 1), `5.denominator` 1, `'x'.numerator` 0 and `Rat.nude`
# (0 1), so a typo or an unexpected Int read as a fraction instead of failing
# where it happened. Now anything else is X::Method::NotFound, and a Rat type
# object dies looking up its parts (X::AdHoc). Found writing showcase/ledger.
#
# The same file runs in t/js's corpus, where the interpreter is the oracle.
# (The JavaScript runtime has no FatRat, so FatRat's lines are not in it.)
# Contract: exit 0 + last line PASS.

my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}
sub dies(&c) { my $r = try c(); $! ?? $!.^name !! "lived: {$r.raku}" }

check (5/2).nude.join('/'),        '5/2',  'a Rat has .nude';
check (5/2).numerator,             5,      '…and .numerator';
check (5/2).denominator,           2,      '…and .denominator';
check <1/3>.nude.join('/'),        '1/3',  'a Rat literal';
check (1/3).nude.WHAT.^name,       'List', '.nude is a List';

for (Any, Mu, Int, 5, 1.5e0, '3', 'x', True, Num) -> \v {
    my $what = v.^name ~ (v.defined ?? '' !! ':U');
    check dies({ v.nude }),        'X::Method::NotFound', "$what has no .nude";
    check dies({ v.numerator }),   'X::Method::NotFound', "$what has no .numerator";
    check dies({ v.denominator }), 'X::Method::NotFound', "$what has no .denominator";
}
check dies({ Rat.nude }),          'X::AdHoc', 'a Rat type object has no parts';
check dies({ Rat.numerator }),     'X::AdHoc', '…no numerator';

# …but X::Numeric::DivideByZero has a numerator ATTRIBUTE, and Str-coercing a
# zero-denominator Rat sets it, normalised (S32-num/rat.t asks). Answering
# .numerator for anything had hidden that this exception was built without it.
try { say 42 / (.1 + .2 - .3) }
check $!.^name,                    'X::Numeric::DivideByZero', 'Str of 42/0 dies';
check $!.numerator,                1,      '…carrying the numerator, normalised';
try { say -3 / 0 }
check $!.numerator,                -1,     '…with its sign';

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
