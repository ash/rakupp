# An object whose attribute is BOUND to the variable that holds the object
# (`$!o := x`, x the caller's `$v`): attribute and variable share one cell,
# and the cell holds the object. The shared-cell proxy used to box its cell in
# an opaque `ext` handle the collector cannot see through, so every such cycle
# leaked; it holds the cell as a cell holder now, an edge like any other.
use Test;
my atomicint $freed = 0;
class N {
    has $.o;
    has @.pad;
    method link(\x) { $!o := x }
    submethod DESTROY { $freed⚛++ }
}
for ^2000 {
    my $v = N.new(pad => (^20).list);
    $v.link($v);
}
$*VM.request-garbage-collection for ^3;
ok $freed >= 1900, "an object bound into its own attribute is freed ($freed of 2000)";

# …and one still reachable is not: the variable outlives the loop
my $keep = N.new;
$keep.link($keep);
$*VM.request-garbage-collection for ^3;
ok $keep.o === $keep, 'a live one keeps its binding';
$keep.o.pad.push(1);
is $keep.pad.elems, 1, '…and is the same object through it';
done-testing;
