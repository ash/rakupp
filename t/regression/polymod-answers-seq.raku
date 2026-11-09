# `.polymod` answers a Seq, as Rakudo's iterator makes it: `10.polymod(3,
# 2).raku` is `(1, 1, 1).Seq`, where it was a List, `(1, 1, 1)`; for an
# Int, a big Int, a Real, an infinite divisor list and a Junction alike.
# Its values are as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck 10.polymod(3, 2).raku, '(1, 1, 1).Seq', 'an Int';
ck (2**70).polymod(1000 xx 2).^name, 'Seq', 'a big one';
ck 2.5.polymod(2).^name, 'Seq', 'a Real';
ck 1234.polymod(10 xx *).raku, '(4, 3, 2, 1).Seq', 'an infinite list';
ck 1.polymod(2 & 3).raku, '(all(1, 1), 0).Seq', 'a Junction';
my $p = 10.polymod(3, 2);
ck ($p[1], $p.elems, $p.reverse.List), (1, 3, (1, 1, 1)), 'read as a list';
my @b = 10.polymod(3);
ck @b.raku, '[1, 3]', 'into an Array';

say 'PASS';
