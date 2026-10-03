# `[ %( … ) ]` and `[ %$h ]` kept the Hash whole.
#
# By the one-argument rule a single Hash in `[ … ]` spreads into its pairs.
# rakupp applies it only to forms that cannot be itemized, and the `%( … )` /
# `%$h` contextualizer is one — its whole job is to strip the item — but only
# a hash literal and a bare `%h` were recognised, so `[ %( a => 1, b => 2 ) ]`
# was one element where Rakudo has two. Every fixture has two or more pairs,
# so the spread and the unspread answers differ.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

my $h = { a => 1, b => 2 };
my @l = a => 1, b => 2;
my %g = c => 3;

check([ %( a => 1, b => 2 ) ].elems, 2, '[ %( … ) ] spreads');
check([ %$h ].elems,                 2, '[ %$h ] spreads');
check([ %(@l) ].elems,               2, '[ %(@l) ] spreads');
check([ %( a => 1, b => 2 ) ].map(*.^name).unique.List, ('Pair',), 'into Pairs');
check([ %$h ].sort(*.key).map(*.key).List, <a b>, 'with the keys');
# what stays whole stays whole
check([ %( a => 1, b => 2 ), ].elems, 1, 'a trailing comma keeps one Hash');
check([ %$h, %g ].elems,             2, 'two Hashes stay two Hashes');
check([ $h ].elems,                  1, 'an itemized hash is one element');
check([ %() ].elems,                 0, 'an empty contextualized hash is empty');

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
