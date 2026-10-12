# A doubly linked list, built by a sub and kept only in a local variable.
use Test;
my atomicint $freed = 0;
class Node { has $.prev is rw; has $.next is rw; has $.v; submethod DESTROY { $freed⚛++ } }
class List { has $.head is rw; has $.tail is rw;
    method push($v) {
        my $n = Node.new(:$v, prev => $!tail);
        $!tail.next = $n if $!tail;
        $!head //= $n;
        $!tail = $n;
    }
}
for ^300 {
    my $l = List.new;
    $l.push($_) for ^10;
}
$*VM.request-garbage-collection for ^3;
ok $freed >= 2850, "a doubly linked list's nodes are freed ($freed of 3000)";
done-testing;
