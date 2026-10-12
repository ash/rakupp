# A hash that contains itself.
use Test;
my atomicint $freed = 0;
class S { submethod DESTROY { $freed⚛++ } }
for ^2000 {
    my %h = s => S.new;
    %h<me> = %h.item;
}
$*VM.request-garbage-collection for ^3;
ok $freed >= 1900, "a hash that contains itself is freed ($freed of 2000)";
done-testing;
