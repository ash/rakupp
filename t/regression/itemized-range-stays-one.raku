# An itemized Range is one element in a list, as Rakudo keeps it: `my @a =
# (1..2).item` had two elements, `((1..2).item, 3).flat` three and
# `[$(1..2)]` two. `$(…)` itemizes a Range now as it does an Array; the
# one-argument rule of an array literal, `.flat` and an `@` assignment leave
# an itemized Range whole. And a Range inside the list a destructuring takes
# is one value, itemized or not: `my ($a, $b, $c) = (1..2), 3` gives $a 1..2,
# where it was spread over $a and $b. A Range alone still spreads.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @b = (1..2).item;
ck @b.elems, 1, 'an @ assignment';
my @c = $(1..2);
ck @c.elems, 1, '$( ) assigned';
ck ((1..2).item, 3).flat.elems, 2, '.flat';
ck [$(1..2)].elems, 1, 'an array literal';
ck [(1..2).item].raku, '[1..2,]', '…its .raku';
ck [$(1..2), 5].flat.elems, 2, 'an Array .flat';
my $r = (1..2).item;
my @a = $r;
ck @a.elems, 1, 'from a $ variable, as before';
ck [1..2].elems, 2, 'a Range alone spreads';
my @e = 1..3;
ck @e.elems, 3, '…when assigned';
ck ((1..2), 3).flat.elems, 3, '…and under .flat';
my ($x, $y, $z) = (1..2), 3;
ck ($x, $y, $z), (1..2, 3, Any), 'a Range in a destructured list';
my ($f, $g) = (1..2).item, 3;
ck ($f, $g), (1..2, 3), '…an itemized one';
my ($d, $e) = 1..2;
ck ($d, $e), (1, 2), 'a Range alone, destructured';
my ($l, @m) = 1..3;
ck ($l, @m), (1, [2, 3]), '…into a slurpy';

say 'PASS';
