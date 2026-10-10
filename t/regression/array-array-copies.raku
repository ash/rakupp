# Regression: `@a.Array` is a NEW Array, never the invocant. Raku++ handed back
# the same Array, so a write through the result — `.push`, `.unshift`, an
# element assignment — changed the source too, and `@a.Array === @a` was True.
# A List's `.Array` shared the List's elements the same way, so pushing onto
# `(1, 2).Array` grew the "immutable" List.
#
# Prettier::Table 1.1.3 sorts with `@rows = @rows.map(*.Array)` and then
# unshifts the sort key onto each row. With no copy that unshift grew the
# table's stored rows, so every later sort saw extra leading columns and sorted
# by the first one (t/pretty-table-sorting.t, 'reverse-sort'), and
# `rakupp install Prettier::Table` refused to install.
#
# Every expectation below was checked against Rakudo 2026.09.
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Mu $got, Mu $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my $a = [1, 2];
my $b = $a.Array;
$b.push(3);
check $a, [1, 2], 'pushing onto $a.Array leaves $a alone';
check $a === $b, False, '…and it is a different Array';

my @x = 1, 2;
my $y = @x.Array;
$y.push(3);
@x[0] = 7;
check @x, [7, 2], 'pushing onto @x.Array leaves @x alone';
check $y, [1, 2, 3], '…and writing @x leaves the copy alone';

my $l = (1, 2);
$l.Array.push(3);
check $l, (1, 2), 'a List is not grown through its .Array';

# the Prettier::Table shape
my @rows = [3, 'c'], [1, 'a'], [2, 'b'];
my @keyed = @rows.map(*.Array).map(-> $row { $row.unshift($row[1]); $row });
check @rows, [[3, 'c'], [1, 'a'], [2, 'b']], 'unshifting onto mapped .Array rows keeps the rows';
check @keyed[0], ['c', 3, 'c'], '…and the copies carry the key';

# the copy is shallow, and its elements keep their item containers
my @n = 1, [2, 3];
my $nc = @n.Array;
$nc[1].push(4);
check @n.raku, '[1, [2, 3, 4]]', 'nested Arrays are shared, not cloned';
check (1, $(2, 3)).Array.flat.elems, 2, 'an itemized element stays one item';
check [[1, 2], [3, 4]].Array.flat.elems, 2, '…as does an Array element';

# a slot bound to a variable copies as its value
my $v = 1;
my @bound;
@bound[0] := $v;
my $bc = @bound.Array;
$bc[0] = 5;
check $v, 1, 'writing the copy does not reach a bound variable';

# the copy is a plain Array, whatever the source was
my Int @t = 1, 2;
check @t.Array.WHAT, Array, 'a typed Array copies to a plain Array';
check @t.Array.of, Mu, '…of Mu';
my @s[2] = 1, 2;
my $sc = @s.Array;
$sc.push(3);
check $sc, [1, 2, 3], 'a shaped Array copies to a growable one';
check @s.raku, 'Array.new(:shape(2,), [1, 2])', '…and keeps its own shape';

# a lazy Array copies lazily, into a buffer of its own
my @lz = 1 .. *;
my $lc = @lz.Array;
check $lc.is-lazy, True, 'a lazy Array copies to a lazy one';
$lc[1] = 50;
check @lz[^3], (1, 2, 3), 'writing the lazy copy leaves the source alone';
check $lc[^3], (1, 50, 3), '…and the copy holds the write';
my @lg = lazy gather { take $_ for 1 .. 4 };
my $gc = @lg.Array;
$gc[0] = 0;
check @lg[^4], (1, 2, 3, 4), 'a lazy gather Array is not written through its copy';
check $gc[^4], (0, 2, 3, 4), '…and the copy reads the rest of the gather';

if @fail { .say for @fail; say "{+@fail} check(s) failed"; exit 1 }
say 'PASS';
