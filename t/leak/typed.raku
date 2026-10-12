# A class whose attributes can only hold values is not tracked by the
# collector (it cannot be on a cycle by itself). Its objects are still freed
# when a cycle runs through them via an object that is tracked, and a typed
# class's own objects are freed as before.
use Test;
my atomicint $freed = 0;
class V { has Int $.n; has str $.s; submethod DESTROY { $freed⚛++ } }
class Holder { has $.v; has $.back is rw; submethod DESTROY { $freed⚛++ } }
for ^1000 { my $v = V.new(n => $_, s => 'x') }
$*VM.request-garbage-collection for ^3;
ok $freed >= 950, "objects of a value-only class are freed ($freed of 1000)";
$freed ⚛= 0;
for ^1000 {
    my $h = Holder.new(v => V.new(n => $_));
    $h.back = $h;
}
$*VM.request-garbage-collection for ^3;
ok $freed >= 1900, "a cycle holding value-only objects is freed with them ($freed of 2000)";
done-testing;
