# race-detect: report
# location: attribute $!count
#
# Two workers bump one object's attribute through a method, no lock.
class Counter {
    has $.count = 0;
    method bump { $!count++ }
}
my $c = Counter.new;
await (^2).map: { start { $c.bump for ^2_000 } };
say $c.count <= 4_000;
