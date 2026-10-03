# A multi whose candidates are split native/interpreted (t/aot/run.raku).
use Aot::Multi;

say describe(42);
say describe('hi');
say describe([7, 8, 9]);
say describe(3/4);
say describe(1e0);
say wrapped(5);
say wrapped('abc');
say chain(1);
say chain(2.5);
say chain('x');
my $s = Shape.new(name => 'S');
say $s.area(3);
say $s.area(2, 5);
say $s.area('circle');
