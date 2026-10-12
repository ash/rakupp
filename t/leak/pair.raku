# The #151 pair: two objects that refer to each other, dropped at the end of
# every iteration. Both must be freed.
use Test;
my atomicint $freed = 0;
class N { has $.other is rw; has @.pad; submethod DESTROY { $freed⚛++ } }
for ^2000 {
    my $a = N.new(pad => (^20).list);
    my $b = N.new(other => $a, pad => (^20).list);
    $a.other = $b;
}
$*VM.request-garbage-collection for ^3;
ok $freed >= 3800, "pairs that refer to each other are freed ($freed of 4000)";
done-testing;
