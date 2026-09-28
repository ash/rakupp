# Regression: `.msb` and `.lsb` under `--target=js`.
#
# The JavaScript runtime measured msb as the length of the number's binary
# STRING, whose '-' counted as a digit: (-1).msb was 1 and (-256).msb 9, where
# the two's-complement answer is 0 and 8 (msb(-n) is the length of |n| - 1).
# It came out right for every negative that is not minus a power of two, which
# is how it hid. lsb shifted the number one bit per step, each shift a pass
# over all of it: 5.7 s for (1 +< 1_000_000).lsb.
#
# Only the METHOD forms appear, because the msb/lsb subs are outside the
# JavaScript core and a single call would keep the whole file from
# transpiling. As it is, t/js/run.raku transpiles it and compares the two
# engines. msb-lsb-huge-ints.raku covers the subs and the interpreter's own
# speed.
#
# Contract: exit 0 + last line PASS. Passes under both engines and under
# Rakudo 2026.08, where every check but the one timing bound in §4 runs.

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eqv $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got.raku}] WANT [{$want.raku}]";
    }
}

# 1. A canary first. Should msb/lsb go quadratic again on either engine, the
#    million-bit cases in §4 would take hours to say so; this says it at once.
{
    my $t0 = now;
    my ($m, $l) = (1 +< 100_000).msb, (1 +< 100_000).lsb;
    my $spent = now - $t0;
    check '(1 +< 100_000).msb', $m, 100_000;
    check '(1 +< 100_000).lsb', $l, 100_000;
    if $spent >= 5 {
        say "not ok - msb and lsb at 100,000 bits took {$spent.round(0.1)} s";
        say 'FAIL';
        exit 1;
    }
}

# 2. msb of a negative is the length of |n| - 1: minus a power of two needs no
#    more bits than the power itself. Either side of 2**53, where the JS
#    runtime moves from a Number to a BigInt, and of 2**63 and 2**64, where the
#    interpreter leaves a machine word.
check '(-1).msb',   (-1).msb,   0;
check '(-2).msb',   (-2).msb,   1;
check '(-3).msb',   (-3).msb,   2;
check '(-255).msb', (-255).msb, 8;
check '(-256).msb', (-256).msb, 8;
check '(-257).msb', (-257).msb, 9;
for 52, 53, 54, 63, 64, 200 -> $k {
    my $p = 2 ** $k;
    check "(2**$k).msb",      $p.msb,        $k;
    check "(2**$k - 1).msb",  ($p - 1).msb,  $k - 1;
    check "(-2**$k).msb",     (-$p).msb,     $k;
    check "(-2**$k + 1).msb", (-$p + 1).msb, $k;
    check "(-2**$k - 1).msb", (-$p - 1).msb, $k + 1;
}

# 3. lsb ignores the sign, and zero has no set bit at all.
check '(-1).lsb',          (-1).lsb,           0;
check '(-8).lsb',          (-8).lsb,           3;
check '(-12).lsb',         (-12).lsb,          2;
check '(-2**64).lsb',      (-(2 ** 64)).lsb,   64;
check '(3 * 2**100).lsb',  (3 * 2 ** 100).lsb, 100;
check '0.msb is Nil',      0.msb,              Nil;
check '0.lsb is Nil',      0.lsb,              Nil;

# 4. A million bits, built with `+<`: the JS runtime still turns a power whose
#    exponent is over 100,000 into a Num. The lsb calls are timed, as a
#    QUADRATIC DETECTOR, not a benchmark: under a tenth of a second on either
#    Raku++ engine now, against 5.7 s each in the old JS runtime. The bound is
#    ours alone: Rakudo's own lsb is quadratic too, 2.8 s a call at this size
#    (and its msb most of a second, which is why msb is not timed at all).
{
    my $big = 1 +< 1_000_000;
    my $t0 = now;
    my @lsb = $big.lsb, (-$big).lsb, (3 * $big).lsb, ($big + 1).lsb;
    my $spent = now - $t0;
    check '(1 +< 1_000_000).lsb',        @lsb[0],         1_000_000;
    check '(-(1 +< 1_000_000)).lsb',     @lsb[1],         1_000_000;
    check '(3 * (1 +< 1_000_000)).lsb',  @lsb[2],         1_000_000;
    check '((1 +< 1_000_000) + 1).lsb',  @lsb[3],         0;
    check 'lsb of a million bits is not quadratic', $spent < 5, True
        if $*VM.name ne 'moar';
    check '(1 +< 1_000_000).msb',        $big.msb,        1_000_000;
    check '((1 +< 1_000_000) - 1).msb',  ($big - 1).msb,  999_999;
    check '(-(1 +< 1_000_000)).msb',     (-$big).msb,     1_000_000;
    check '(-(1 +< 1_000_000) - 1).msb', (-$big - 1).msb, 1_000_001;
}

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
