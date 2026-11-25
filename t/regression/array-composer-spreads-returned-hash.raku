# Regression: `[ f() ]` spreads a Hash a call returns into its pairs (#283).
#
# The one-arg rule spread a Hash only in the forms that cannot be an item —
# `[%h]`, `[{…}]`, `[%(…)]` — so `[ h() ]`, `[ %h.clone ]` and
# `[ Hash.new(…) ]` held the Hash as one element where Rakudo has its pairs:
# `%(errors => [ record() ])` was an array of one Hash. A call's result sits
# in no container, as the Array arm already says (callSpread). A `$` item,
# a trailing comma, an element fetched from an array and a Set stay whole.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
sub h() { %(a => 1, b => 2) }
my %e = a => 1, b => 2;
my $s = %e;
my @r = %e;
class O { has %.attr = :x(1), :y(2) }

check [ h() ].elems, 2, '[ h() ]';
check [ h() ].map(*.^name).List, <Pair Pair>, '…holds Pairs';
check [ %e.clone ].elems, 2, '[ %e.clone ]';
check [ Hash.new(:a(1), :b(2)) ].elems, 2, '[ Hash.new(…) ]';
check [ Map.new(:a(1), :b(2)) ].elems, 2, '[ Map.new(…) ]';
check [ %e.Map ].elems, 2, '[ %e.Map ]';
check [ (h()) ].elems, 2, '[ (h()) ]';
check [ O.new.attr ].elems, 2, '[ $o.attr ]';
check [ h()».Str ].elems, 2, '[ h()».Str ]';
check %(errors => [ h() ])<errors>.elems, 2, 'a record list in a hash';
check [ %e ].elems, 2, '[ %e ] (unchanged)';
check [ $s ].elems, 1, '[ $s ] stays one';
check [ %e, ].elems, 1, '[ %e, ] stays one';
check [ $(h()) ].elems, 1, '[ $(h()) ] stays one';
check [ h(), h() ].elems, 2, '[ h(), h() ] is two Hashes';
check [ @r[0] ].elems, 1, '[ @r[0] ] stays one';
check [ set(1, 2) ].elems, 1, '[ set(…) ] stays one';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
