# Regression: issue #103 (2026-11-10).
#
#   In `my ($a, $b, $c) = (@x.List, 'T', 'U')` the List on the right was
#   spread over the variables: "x" "y" "T". A List there is one item, not a
#   Slip, so $a takes it whole -- from a literal, a sub's return, hash values,
#   a hash slice, a <x y> literal and an explicit $( ) alike.
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

my @x = <x y>;
my $want = (('x', 'y'), 'T', 'U');
sub abc($a, $b, $c) { ($a, $b, $c) }

my ($a, $b, $c) = (@x.List, 'T', 'U');
check abc($a, $b, $c), $want, '(@x.List, ...) literal';

sub f() { (@x.List, 'T', 'U') }
($a, $b, $c) = f();
check abc($a, $b, $c), $want, 'returned from a sub';

my %h = l => @x.List, t => 'T', u => 'U';
($a, $b, $c) = %h<l>, %h<t>, %h<u>;
check abc($a, $b, $c), $want, 'hash values in a comma list';

($a, $b, $c) = %h<l t u>;
check abc($a, $b, $c), $want, 'a hash slice';

($a, $b, $c) = (<x y>, 'T', 'U');
check abc($a, $b, $c), $want, 'a <x y> literal';

($a, $b, $c) = ($(@x.List), 'T', 'U');
check abc($a, $b, $c), $want, 'itemized with $( )';

($a, $b, $c) = (("x", "y"), "T", "U");
check abc($a, $b, $c), $want, 'double-quoted literals';

($a, $b, $c) = (|@x.List, 'T');
check abc($a, $b, $c), ('x', 'y', 'T'), 'a Slip is spread';

($a, $b, $c) = (@x, 'T', 'U');
check abc($a, $b, $c), (['x', 'y'], 'T', 'U'), 'an Array element stays whole too';

my ($p, @rest) = (@x.List, 'T', 'U');
check ($p, @rest.List), (('x', 'y'), ('T', 'U')), 'an @ target after a List';

my $l = %h<l>;
check $l, ('x', 'y'), 'one by one';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
