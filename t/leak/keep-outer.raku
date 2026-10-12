# Must NOT be collected: cycles still reachable from an outer array, and the
# ones dropped beside them must be.
use Test;
my atomicint $freed = 0;
class N { has $.other is rw; has $.id; submethod DESTROY { $freed⚛++ } }
my @keep;
for ^2000 -> $i {
    my $a = N.new(id => $i);
    my $b = N.new(id => -$i, other => $a);
    $a.other = $b;
    @keep.push($a) if $i %% 2;
}
$*VM.request-garbage-collection for ^3;
ok $freed <= 2000, "no kept pair was freed ($freed of the 2000 dropped)";
ok $freed >= 1900, "the dropped pairs were ($freed of 2000)";
ok @keep.elems == 1000 && @keep.grep({ .other.other === $_ && .other.id == -.id }).elems == 1000,
    'every kept pair is intact';
done-testing;
