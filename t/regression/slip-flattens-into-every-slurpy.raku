# Regression: a Slip flattens into a slurpy even when it is itemized.
# Assigning one to a scalar itemizes it (`my $s = @a.Slip`), and the slurpy
# binder skipped every itemized value — so `f($s)` bound ONE element where
# Rakudo binds three. Dissolving into the surrounding list is the entire
# purpose of a Slip, and it holds for all three slurpy kinds: `*@` flattens
# Iterables anyway, and `**@`/`+@` decline to dissolve an Array but must
# still dissolve a Slip.
#
# BinaryHeap builds a heap from one with `.new(+values)`, and got a heap
# holding a single nested list.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub star(*@v)  { @v.elems }
sub nonf(**@v) { @v.elems }
sub plus(+@v)  { @v.elems }

my @a = 1, 2, 3;
my $slip = @a.Slip;

ck(star($slip), 3, 'an itemized Slip flattens into *@');
ck(nonf($slip), 3, '…into **@');
ck(plus($slip), 3, '…and into +@');

ck(star(@a.Slip),   3, 'an un-itemized Slip still does');
ck(star(slip(@a)),  3, 'however it was made');
my $bound := @a.Slip;
ck(star($bound),    3, 'and when bound rather than assigned');

# mixed with other arguments, the Slip dissolves and they do not
ck(star(1, $slip, 5), 5, 'a Slip between other arguments dissolves');
ck(nonf(1, $slip, 5), 5, '…in the non-flattening slurpy too');

# what must NOT flatten still does not
ck(nonf(@a),        1, '**@ keeps a plain Array whole');
ck(nonf([1, 2, 3]), 1, '…and an itemized one');
my $arr = [1, 2, 3];
ck(nonf($arr),      1, '…however it is held');
ck(star([[1, 2], [3, 4]]), 2, '*@ stops at Arrays, as before');

# the value itself is unchanged by being passed
ck($slip.elems,  3,      'the Slip still has its elements');
ck($slip.^name,  'Slip', 'and is still a Slip');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
