# Regression: the sequence operator keeps Rats Rats (6.c/MISC/bug-coverage.t,
# 2026-09-27). A lone Rat seed steps by .succ/.pred — `1.0 ... 3` is
# (1.0, 2.0, 3.0), where the double walk made it (1.0, 2e0, 3e0) — and a Rat
# step makes the seed it was deduced up to a Rat as well: an element is the
# one before it plus the step, so `0.1, 2 ... 3` is (0.1, 2.0).
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/raku — NOT
# the bare name `raku`, which on this box has pointed at rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck((1.0 ... 3).raku, '(1.0, 2.0, 3.0).Seq', 'a lone Rat seed up to an Int');
ck((1.0 ... 3.0).raku, '(1.0, 2.0, 3.0).Seq', '…up to a Rat');
ck((3.0 ... 1).raku, '(3.0, 2.0, 1.0).Seq', '…downwards');
ck((0.5 ... 3).raku, '(0.5, 1.5, 2.5).Seq', '…stopping short of the end');
ck((0.1, 2 ... 3).raku, '(0.1, 2.0).Seq', 'an Int seed after a Rat one, under a Rat step');
ck((1.5, 2 ... 4).raku, '(1.5, 2.0, 2.5, 3.0, 3.5, 4.0).Seq', '…continuing');
ck((1, 1.5 ... 3).raku, '(1, 1.5, 2.0, 2.5, 3.0).Seq', 'an Int FIRST seed stays an Int');
ck((1, 2 ... 3.0).raku, '(1, 2, 3).Seq', 'Int seeds up to a Rat stay Ints');
ck((1e0 ... 3).raku, '(1e0, 2e0, 3e0).Seq', 'a Num seed stays a Num');
my @a; for 0.1, 2 ... 3 -> $x { @a.push: $x }
ck(@a, [0.1, 2.0], 'the same through a `for`');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
