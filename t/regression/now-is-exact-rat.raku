# `now` is an exact Rat of nanoseconds, as Rakudo's is, so its `.raku` reads
# back `eqv`: it was a Num, `Instant.from-posix(1793331244.935708e0)`, and
# `(EVAL now.raku) eqv now` was False. A binary embeds `constant T = now`
# only when the value's `.raku` reads back, so it computed it at start.
# Every line answers the same under Rakudo 2026.09.

use MONKEY-SEE-NO-EVAL;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $i = now;
ck (EVAL $i.raku) eqv $i, True, '.raku reads back eqv';
ck $i.raku.contains('e'), False, '.raku writes no Num';
ck $i ~~ Instant, True, 'it is an Instant';
ck $i.Rat.^name, 'Rat', '.Rat';
ck $i.Num.^name, 'Num', '.Num';
ck $i.tai.^name, 'Rat', '.tai';
my $d = now - $i;
ck $d ~~ Duration, True, 'a difference is a Duration';
ck $d >= 0, True, '…and not negative';
ck now > $i, True, 'time moves on';
ck (now.to-posix[0] - time).abs < 2, True, '.to-posix is POSIX time';
ck Instant.from-posix(1.5).raku, 'Instant.from-posix(1.5)', 'a Rat from-posix';
ck DateTime.new($i).Instant eqv $i, True, 'round trip through a DateTime';

say 'PASS';
