# A ring far larger than one walk's budget: freed or not, it must not stall.
use Test;
my atomicint $freed = 0;
class N { has $.next is rw; submethod DESTROY { $freed⚛++ } }
my $t0 = now;
for ^3 {
    my $first = N.new;
    my $cur = $first;
    for ^30000 { my $n = N.new; $cur.next = $n; $cur = $n }
    $cur.next = $first;
}
$*VM.request-garbage-collection for ^3;
ok now - $t0 < 20, "three 30,001-node rings take no pathological time ({(now - $t0).fmt('%.1f')} s)";
diag "freed $freed of 90003";
done-testing;
