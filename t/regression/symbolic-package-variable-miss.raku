# Regression: a symbolic lookup of a PACKAGE-QUALIFIED variable that nobody
# declared is a miss, and a miss is X::NoSuchSymbol — not an undefined Any.
# `$::('Names::xx::dow')` read as the "unset slot" answer `Foo::<bar>` gives,
# so Date::Names, asked for a language it does not ship, built its table out
# of an Any and carried on. A declared symbol still reads as its value, an
# unqualified `$::('x')` was already a Failure, and the stash spelling still
# finds what `.WHO` installed.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub kind($v) { $v ~~ Failure ?? 'Failure(' ~ $v.exception.^name ~ ')' !! $v.^name }

module Shipped { our $dow = 'Monday'; our @names = <a b>; our %by = x => 1; }

ck(kind($::('Shipped::dow')),      'Str',   'a declared package scalar reads through $::()');
ck($::('Shipped::dow'),            'Monday', '…as its value');
ck(kind(@::('Shipped::names')),    'Array', 'a declared package array');
ck(@::('Shipped::names').elems,    2,       '…with its elements');
ck(%::('Shipped::by')<x>,          1,       'a declared package hash');
ck(kind(::('Shipped::$dow')),      'Str',   'the sigil-on-the-last-part spelling too');

ck(kind($::('Shipped::nope')),     'Failure(X::NoSuchSymbol)', 'an undeclared scalar in a real package is a miss');
ck(kind($::('No::Such::dow')),     'Failure(X::NoSuchSymbol)', 'and in a package that does not exist');
ck(kind(::('No::Such::$dow')),     'Failure(X::NoSuchSymbol)', 'the other spelling of the same miss');
ck(kind(@::('No::Such::list')),    'Failure(X::NoSuchSymbol)', 'an array miss is a miss, not an empty array');
ck(kind($::('dow-nope')),          'Failure(X::NoSuchSymbol)', 'an unqualified miss stays a miss');

# the miss is soft until used — that is what makes it catchable at the use
my $m = $::('No::Such::dow');
ck($m.defined, False, 'the miss is undefined');
ck((try { $m.elems; 'used' }) // $!.^name, 'X::NoSuchSymbol', 'and throws when used');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
