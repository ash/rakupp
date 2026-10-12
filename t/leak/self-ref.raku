# An object whose attribute holds the object itself.
use Test;
my atomicint $freed = 0;
class N { has $.me is rw; submethod DESTROY { $freed⚛++ } }
for ^3000 {
    my $n = N.new;
    $n.me = $n;
}
$*VM.request-garbage-collection for ^3;
ok $freed >= 2850, "an object that refers to itself is freed ($freed of 3000)";
done-testing;
