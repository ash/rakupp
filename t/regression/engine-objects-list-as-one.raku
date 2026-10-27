# Regression: an engine object kept in a hash was listed as its FIELDS.
#
# `my @a = DateTime.new(0)` held eight Pairs, a Date three, a Channel three, a
# Lock nothing at all, and `DateTime.new(0).elems` was 8: list assignment,
# `.list`, `.flat`, `.elems` and the `|` slip took any tagged hash for a Hash.
# Rakudo lists an object that is neither Iterable nor Associative as ITSELF,
# one element (opaqueHashKind). And a Set, Bag or Mix bound to a `$` parameter
# or loop variable is one item, as a Hash already was (itemizeScalar).

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

for DateTime.new(0), Date.new(2020, 1, 1), Promise.new, Channel.new, Lock.new, Supplier.new -> $o {
    my $n = $o.^name;
    my @a = $o<>;
    ok @a.elems == 1 && @a[0].^name eq $n, "my @a = a $n is one element";
}
my @d = DateTime.new(0);
ok @d.elems == 1 && @d[0] ~~ DateTime, 'my @a = DateTime.new(0), unitemized';
ok DateTime.new(0).list.elems == 1 && DateTime.new(0).flat.elems == 1, '.list and .flat';
ok DateTime.new(0).elems == 1, '.elems';
ok (|DateTime.new(0)).elems == 1 && (|DateTime.new(0))[0] ~~ DateTime, 'a slip of one';
my @two = DateTime.new(0), DateTime.new(1);
ok @two.elems == 2, 'two of them are two';

# a QuantHash in a `$` parameter or loop variable is ONE item…
sub one($x) { my @b = $x; @b.elems }
ok one(set(1, 2, 3)) == 1, 'a Set through a $ parameter';
my @loop;
for set(1, 2, 3), bag(1, 1, 2) -> $v { my @c = $v; @loop.push: @c.elems }
ok @loop eqv [1, 1], 'a Set and a Bag through a loop variable';
# …while a bare one, a Hash and a Map still spread
my @s = set(1, 2, 3);
ok @s.elems == 3, 'a bare Set is its pairs';
my %h = a => 1, b => 2;
my @h = %h;
ok @h.elems == 2, 'a Hash is its pairs';
ok (my @m = Map.new((a => 1, b => 2))).elems == 2, 'a Map is its pairs';

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
