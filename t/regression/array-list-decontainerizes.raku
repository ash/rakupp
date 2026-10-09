# Regression: `@a.List` DECONTAINERIZES. An Array keeps each element in a
# container, so `@a[0] = [5, 6]` stores an itemized Array — but `.List` hands
# back the values, and Rakudo's `@a.List` is `([5, 6], [3, 4])`. Raku++ kept
# the `$` on a slot written by `@a[i] = …` (never on one filled by `my @a = …`).
#
# That stayed invisible until `cross` and `zip` began taking an itemized
# argument as one item (v5.3.0). Math::NIntegrate builds its Cartesian error
# weights with `cross(|@w.List)` after `@w[$_] = …`: one axis collapsed to a
# single point, every 2-D Gauss-Kronrod estimate went wrong, and the adaptive
# strategy bisected to its limit — 0.5 s became 207 s, with a wrong integral.
#
# Every expectation below was checked against Rakudo.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my @w = [1, 2], [3, 4];
@w[0] = [5, 6];
check @w.List.raku, '([5, 6], [3, 4])', '.List holds the values, not the containers';
check cross(|@w.List).List, ((5, 3), (5, 4), (6, 3), (6, 4)), 'cross over a slipped .List';
check zip(|@w.List).List, ((5, 3), (6, 4)), 'zip over a slipped .List';
check @w.List.flat.List, (5, 6, 3, 4), '.List.flat spreads every element';

my @h = 1, $[2, 3], 4;
check @h.List.raku, '(1, [2, 3], 4)', 'an itemized literal comes out too';
check @h.List.flat.elems, 4, '…and flattens';

my @e;
@e[2] = [7];
check @e.List.raku, '(Nil, Nil, [7])', 'holes are Nil, the written slot is the value';

# the Math::NIntegrate shape: one axis of weights replaced by an element write
my @components = [1, 2, 3], [1, 2, 3];
my @ew = (^2).map: -> $axis {
    my @c = @components;
    @c[$axis] = [10, 20, 30];
    cross(|@c.List).map({ [*] |$_ }).List
};
check @ew[0], (10, 20, 30, 20, 40, 60, 30, 60, 90), 'error weights along axis 0';
check @ew[1], (10, 20, 30, 20, 40, 60, 30, 60, 90), '…and along axis 1';

# what keeps its containers is unchanged
check @w.list.raku, '[[5, 6], [3, 4]]', '.list is the Array itself';
check (1, $[2, 3]).List.raku, '(1, $[2, 3])', 'a List is its own .List';
check @w.flat.raku, '($[5, 6], $[3, 4]).Seq', '.flat of the Array keeps its items';
check cross($[1, 2], [3, 4]).List, (($[1, 2], 3), ($[1, 2], 4)), 'an itemized argument is still one item';
check (try { @w.List[0] = 9; 'wrote' }) // 'refused', 'refused', '.List is not writable';

if @fail { .say for @fail; say "{+@fail} check(s) failed"; exit 1 }
say 'PASS';
