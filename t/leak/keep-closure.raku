# Must NOT be collected: a cycle reachable only from a running closure's scope.
use Test;
my atomicint $freed = 0;
class N { has $.other is rw; has $.id; submethod DESTROY { $freed⚛++ } }
my @f = (^200).map: -> $i {
    my $a = N.new(id => $i);
    $a.other = N.new(id => -$i, other => $a);
    -> { $a.other.other.id }
};
for ^3000 { my $t = N.new; $t.other = $t }
$*VM.request-garbage-collection for ^3;
ok @f.map({ .() }).sum == (^200).sum, 'cycles captured by closures are intact';
ok $freed >= 2850 && $freed <= 3000, "only the dropped self-cycles were freed ($freed)";
done-testing;
