# Regression: `.msb` / `.lsb` and the `msb` / `lsb` subs on an Int past 64
# bits were QUADRATIC. The method halved the number one bit per BigInt divmod,
# each a pass over every limb, so `(2 ** 1_000_000).msb` ran for about forty
# minutes; the subs stepped 32 bits per divmod and took about two. BigInt now
# reads both answers off its limbs — the top three bound log2 to within a hair,
# the low eight give the value mod 2**64 — and converts to binary only when
# those cannot settle it: right at a power of two, or divisible by a large one.
# That conversion is subquadratic (23 ms for 2 ** 1_000_000), and those are
# exactly the numbers below.
#
# The sign rule is the one numeric-coercions-and-refusals.raku and
# msb-lsb-builtins.raku pin for small numbers: msb of a negative is two's
# complement, the bit length of |n| - 1, so msb(-2**N) is N but msb(-2**N - 1)
# is N + 1. lsb ignores the sign.
#
# Contract: exit 0 + last line PASS. Passes under both engines (Rakudo
# 2026.08), so it doubles as an oracle.

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

# 1. QUADRATIC DETECTORS, not benchmarks: a few milliseconds now, against 51 s
#    for the old methods at 100,000 bits and 21 s for the old subs at 300,000.
#    The bound sits far above the first, for a slow CI runner, and well under
#    the second. A miss ends the file, since the million-bit cases in §4 would
#    take the better part of an hour to say the same thing.
{
    my $p = 2 ** 100_000;
    my $t0 = now;
    my ($msb, $lsb) = $p.msb, $p.lsb;
    my $methods = now - $t0;
    my $q = 2 ** 300_000;
    $t0 = now;
    my ($smsb, $slsb) = msb($q), lsb($q);
    my $subs = now - $t0;
    check '(2**100_000).msb', $msb, 100_000;
    check '(2**100_000).lsb', $lsb, 100_000;
    check 'msb(2**300_000)', $smsb, 300_000;
    check 'lsb(2**300_000)', $slsb, 300_000;
    check 'the methods are not quadratic', $methods < 5, True;
    check 'the subs are not quadratic', $subs < 5, True;
    if $methods >= 5 || $subs >= 5 {
        note "methods {$methods.round(0.01)} s, subs {$subs.round(0.01)} s; skipping the rest";
        say 'FAIL';
        exit 1;
    }
}

# 2. Powers of two and their neighbours, which the leading limbs can never
#    tell apart — from just past a machine word up through widths where the
#    conversion splits many times.
for 64, 128, 200, 1_000, 10_000, 100_000 -> $k {
    my $p = 2 ** $k;
    check "(2**$k).msb",          $p.msb,             $k;
    check "(2**$k).lsb",          $p.lsb,             $k;
    check "(2**$k - 1).msb",      ($p - 1).msb,       $k - 1;
    check "(2**$k - 1).lsb",      ($p - 1).lsb,       0;
    check "(2**$k + 1).msb",      ($p + 1).msb,       $k;
    check "(2**$k + 1).lsb",      ($p + 1).lsb,       0;
    check "(-2**$k).msb",         (-$p).msb,          $k;
    check "(-2**$k - 1).msb",     (-$p - 1).msb,      $k + 1;
    check "(-2**$k + 1).msb",     (-$p + 1).msb,      $k;
    check "(-2**$k).lsb",         (-$p).lsb,          $k;
    check "msb(2**$k)",           msb($p),            $k;
    check "lsb(2**$k)",           lsb($p),            $k;
    check "msb(-2**$k)",          msb(-$p),           $k;
    check "msb(-2**$k - 1)",      msb(-$p - 1),       $k + 1;
    check "lsb(3 * 2**$k)",       lsb(3 * $p),        $k;
    check "(10**9 * 2**$k).lsb",  (10 ** 9 * $p).lsb, $k + 9;
}

# 3. Numbers the leading or trailing limbs settle outright, and one divisible
#    by a power of two without being one, whose lsb converts only its low end.
{
    my $t = 3 ** 50_000;
    check '(3**50_000).msb', $t.msb, 79_248;
    check '(3**50_000).lsb', $t.lsb, 0;
    my $u = $t * 2 ** 777;
    check '(3**50_000 * 2**777).lsb', $u.lsb, 777;
    check 'lsb(3**50_000 * 2**777)', lsb($u), 777;
    check '(3**50_000 * 2**777).msb', $u.msb, 80_025;
    check '(-(3**50_000 * 2**777)).msb is one more: not a power of two', (-$u).msb, 80_026;
    my $d = 10 ** 20_000;
    check '(10**20_000).msb', $d.msb, 66_438;
    check '(10**20_000).lsb', $d.lsb, 20_000;
    check 'lsb(-10**20_000)', lsb(-$d), 20_000;
}

# 4. The numbers from the report.
{
    my $big = 2 ** 1_000_000;
    check '(2 ** 1_000_000).msb',         $big.msb,         1_000_000;
    check '((2 ** 1_000_000) + 1).lsb',   ($big + 1).lsb,   0;
    check '(2 ** 1_000_000).lsb',         $big.lsb,         1_000_000;
    check '((2 ** 1_000_000) - 1).msb',   ($big - 1).msb,   999_999;
    check '(-(2 ** 1_000_000)).msb',      (-$big).msb,      1_000_000;
    check 'msb(-(2 ** 1_000_000) - 1)',   msb(-$big - 1),   1_000_001;
}

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
