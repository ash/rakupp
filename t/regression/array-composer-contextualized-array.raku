# `[ @$v ]`, `[ @($v) ]` and `[ $v<> ]` kept the Array in $v whole.
#
# By the one-argument rule a single list in `[ … ]` spreads into its elements,
# and rakupp applies it only to forms that cannot be itemized. A bare `@a` was
# one of them, but the contextualizers that exist to take an array OUT of its
# Scalar — `@$v`, `@( … )`, and the decont `<>` — were not, so `[ @$v ]` was a
# one-element array holding the array. nige123/cli.321.do wrote `[ |@$v ]`
# with the note "Raku++: [ @$v ] nests". `[ $h<> ]` spreads a hash's pairs,
# as `[ %$h ]` already did.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

my $v = [4, 5, 6];
my $h = { a => 1, b => 2 };
my %o;

check([ @$v ].elems,      3, '[ @$v ] spreads');
check([ @($v) ].elems,    3, '[ @($v) ] spreads');
check([ $v<> ].elems,     3, '[ $v<> ] spreads');
check([ @$v ].List,  (4, 5, 6), 'into the elements');
%o<k> = [ @$v ];
check(%o<k>.elems,        3, 'stored in a hash, as 321 does');
check([ $h<> ].elems,     2, '[ $h<> ] spreads a hash into its pairs');
# what stays whole stays whole
check([ @$v, ].elems,     1, 'a trailing comma keeps one Array');
check([ $v ].elems,       1, 'an itemized $v stays one element');
check([ @$v, @$v ].elems, 2, 'two arrays stay two arrays');
my $s = 7;
check([ $s<> ].List,   (7,), 'a decont scalar is one element');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
