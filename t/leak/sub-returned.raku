# A cycle built in a sub and returned, then dropped by a caller that ignores
# the result.
use Test;
my atomicint $freed = 0;
class N { has $.other is rw; submethod DESTROY { $freed⚛++ } }
sub make() {
    my $a = N.new;
    my $b = N.new(other => $a);
    $a.other = $b;
    $a
}
make() for ^1500;
$*VM.request-garbage-collection for ^3;
ok $freed >= 2850, "a cycle a caller drops is freed ($freed of 3000)";
done-testing;
