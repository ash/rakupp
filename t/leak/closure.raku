# An object holding a closure that mentions the object: object -> block ->
# scope -> object: the scope is a walk node of its own.
use Test;
my atomicint $freed = 0;
class N { has &.cb is rw; submethod DESTROY { $freed⚛++ } }
for ^2000 {
    my $n = N.new;
    $n.cb = -> { $n };
}
$*VM.request-garbage-collection for ^3;
ok $freed >= 1900, "an object whose closure mentions it is freed ($freed of 2000)";
done-testing;
