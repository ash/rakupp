# Must NOT be collected: a cycle held only by an array element (and a hash
# value), while scopes around it come and go.
use Test;
my atomicint $freed = 0;
class N { has $.other is rw; has $.id; submethod DESTROY { $freed⚛++ } }
my @a;
my %h;
for ^1000 -> $i {
    my $x = N.new(id => $i);
    $x.other = N.new(id => $i + 10000, other => $x);
    @a[$i] = $x;
    %h{$i} = $x.other;
}
for ^2000 { my $t = N.new; $t.other = $t }   # garbage beside it, to drive collections
$*VM.request-garbage-collection for ^3;
ok @a.grep({ .other.other === $_ }).elems == 1000, 'cycles held by array elements are intact';
ok %h.values.grep({ .other.other === $_ }).elems == 1000, 'cycles held by hash values are intact';
ok $freed >= 1900 && $freed <= 2000, "only the dropped self-cycles were freed ($freed)";
done-testing;
