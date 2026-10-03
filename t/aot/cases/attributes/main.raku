# Attributes read and written by native method bodies (t/aot/run.raku).
use Aot::Attr;

my $p = Point.new(x => 1, y => 2);
say $p.read;
say $p.move(2, 3).read;
say $p.move(-1, 1).trail.join(' ');
say $p.bump;
say $p.tag('b', 2);
say $p.tag('a', 1);
say $p.reset-x;
say $p.x.^name;
say $p.set-type-object;
say $p.via-accessor;
say $p.trail-length;
say $p.relabel('new');
say $p.relabel('again');
my $p2 = Point.new(x => 10);
my &add = $p2.adder;
$p2 = Nil;
say add(5);

my $d = Derived.new;
say $d.base-secret;
say $d.derived-secret;
say $d.set-base('B');
say $d.set-derived('D');
say $d.both;
say Base.new.set-base('plain');

my $a = Accessor.new;
say $a.public;
say $a.private;
