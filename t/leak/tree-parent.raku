# A tree whose children point back at their parent.
use Test;
my atomicint $freed = 0;
class T { has $.parent; has @.kids; submethod DESTROY { $freed⚛++ } }
sub grow($parent, $depth) {
    my $t = T.new(:$parent);
    if $depth > 0 { $t.kids.push: grow($t, $depth - 1) for ^2 }
    $t
}
for ^200 {
    my $root = grow(Nil, 3);   # 15 nodes
    $root.kids.elems;
}
$*VM.request-garbage-collection for ^3;
ok $freed >= 2850, "a tree with parent links is freed ($freed of 3000)";
done-testing;
