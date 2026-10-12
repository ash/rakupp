# Must NOT be collected: a cycle held only by a call's argument (a temporary
# nothing names), while scope exits inside the call drive collections.
use Test;
my atomicint $freed = 0;
class N { has $.other is rw; has $.id; submethod DESTROY { $freed⚛++ } }
sub make($id) { my $a = N.new(:$id); $a.other = N.new(id => -$id, other => $a); $a }
sub churn($x) {
    for ^3000 { my $t = N.new; $t.other = $t }
    $*VM.request-garbage-collection;
    $x.other.other.id + $x.other.id
}
my $sum = 0;
$sum += churn(make($_)) for 1..3;
is $sum, 0, 'a cycle passed as an argument survives collections inside the call';
$*VM.request-garbage-collection for ^3;
ok $freed >= 8550, "the churn and, after the calls, the arguments were freed ($freed of 9006)";
done-testing;
