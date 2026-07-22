# Regression: the quanthash coercer (.Bag/.Set/…) flattens one level through
# a bare List — (@a, %h).Bag takes @a's elements and %h's pairs — but a real
# Array's elements are itemized, so [4,[5,6]].Bag must keep [5,6] whole.
# First version flattened Arrays too and broke ».Bag nodality (hyper.t −6,
# caught by gate sf2, 2026-07-22).

my $ok = True;

my @a = <x y>;
my %x = a => 1, b => 2;
my $flat = (@a, %x).BagHash;
unless $flat.elems == 4 && $flat<x> == 1 && $flat<b> == 2 {
    say 'FAIL: (@a, %h) coercion did not flatten one level';
    $ok = False;
}

my $nodal = [[2, 3], [4, [5, 6]]]».Bag».keys».sort.gist;
unless $nodal eq '((2 3) (4 [5 6]))' {
    say "FAIL: ».Bag nodality broken: $nodal";
    $ok = False;
}

# quanthash arg to .new is ONE element; a plain Hash still iterates its pairs
unless BagHash.new(set <a b c>).elems == 1 {
    say 'FAIL: BagHash.new(set ...) should have 1 element';
    $ok = False;
}
unless Set.new({ foo => 10, bar => 17 }).elems == 2 {
    say 'FAIL: Set.new(%hash) should iterate pairs';
    $ok = False;
}

say 'PASS' if $ok;
