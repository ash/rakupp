# An array that contains itself, holding an object that counts its own
# destruction.
use Test;
my atomicint $freed = 0;
class S { submethod DESTROY { $freed⚛++ } }
for ^2000 {
    my @a = S.new;
    @a.push: @a.item;
}
$*VM.request-garbage-collection for ^3;
ok $freed >= 1900, "an array that contains itself is freed ($freed of 2000)";
done-testing;
