# Regression: floor/ceiling/round as SUBS agree with the methods on a wide Rat.
#
# `$x.floor` divided exactly; `floor($x)` went through `.toNum()` first, so a
# rational too wide for a double was rounded before it was floored. For
# `FatRat.new(10**55 - 1, 10**55)` — a number strictly below 1 — the sub answered
# 1 and the method answered 0. Rat::Precise renders exactly such a FatRat, and
# the whole decimal expansion came out wrong with no error anywhere.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $just-under = FatRat.new(10**55 - 1, 10**55);   # 0.999…9, 55 nines
my $just-over  = FatRat.new(10**55 + 1, 10**55);   # 1.000…01

check floor($just-under),    0, 'floor of a FatRat just under 1';
check ceiling($just-under),  1, 'ceiling of the same';
check truncate($just-under), 0, 'truncate of the same';
check floor($just-over),     1, 'floor of a FatRat just over 1';
check ceiling($just-over),   2, 'ceiling of the same';

check floor(-$just-under),   -1, 'floor of its negative';
check ceiling(-$just-under),  0, 'ceiling of its negative';

# the sub and the method are the same question
check floor($just-under),   $just-under.floor,   'sub floor agrees with the method';
check ceiling($just-under), $just-under.ceiling, 'sub ceiling agrees with the method';
check round($just-under),   $just-under.round,   'sub round agrees with the method';

# what must not change: ordinary Rats, Nums and Ints
check floor(2.5),    2, 'floor(2.5)';
check floor(-2.5),  -3, 'floor(-2.5)';
check ceiling(2.5),  3, 'ceiling(2.5)';
check ceiling(-2.5), -2, 'ceiling(-2.5)';
check round(2.5),    3, 'round(2.5) is half-up';
check round(-2.5),  -2, 'round(-2.5) is half-up too';
check round(2.4),    2, 'round(2.4)';
check floor(7/2),    3, 'floor of a Rat from division';
check ceiling(7/2),  4, 'ceiling of the same';
check floor(2.5e0),  2, 'floor of a Num is untouched';
check ceiling(-0.5e0), 0, 'ceiling of a Num too';
check round(0.5e0),  1, 'round of a Num too';
check floor(7),      7, 'floor of an Int is itself';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
