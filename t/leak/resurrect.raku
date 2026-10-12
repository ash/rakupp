# A DESTROY that stores `self` somewhere keeps the object alive, and DESTROY
# runs once per object, however often it is found unreachable again.
use Test;
my @saved;
my %calls;
class N {
    has $.other is rw; has $.id;
    submethod DESTROY { %calls{$!id}++; @saved.push(self) if $!id %% 10 }
}
for ^1000 -> $i {
    my $a = N.new(id => 2 * $i);
    my $b = N.new(id => 2 * $i + 1, other => $a);
    $a.other = $b;
}
$*VM.request-garbage-collection for ^3;
ok %calls.elems >= 1900, "DESTROY ran for the dropped pairs ({%calls.elems} of 2000)";
ok %calls.values.max == 1, 'and at most once for each';
ok @saved.elems >= 190, "resurrected objects were kept ({@saved.elems})";
ok @saved.grep({ .other.defined && .other.other === $_ }).elems == @saved.elems,
    'a resurrected object keeps its links';
my $before = %calls.elems;
@saved = ();
$*VM.request-garbage-collection for ^3;
ok %calls.values.max == 1, 'dropping the resurrected objects runs no DESTROY again';
done-testing;
