# Regression: multiplying two big Ints was QUADRATIC, and so were `**`, `+<`
# and `+>`, which are built on it. BigInt's operator* ran a schoolbook loop that
# split every limb product with a `% BASE` and a `/ BASE`: `2 ** 1_000_000`
# took 1.46 s, `3 ** 2_000_000` ran out of the power's work budget after
# 5.6 s and failed with Overflow, `1 +< 1_000_000` took 1.44 s and
# `(2 ** 1_000_000 - 1) +> 500_000` 2.7 s (a long division by 2 ** 500_000).
# Products past 96 limbs are Karatsuba now, over a schoolbook kernel that
# carries once every 18 rows; `+>` by more than a few limbs goes through
# binary (BigInt::toBinary/fromBinary) instead of dividing. All four are
# tens of milliseconds at those sizes.
#
# Expected values never come from the operation under test: decimal closed
# forms built as strings, residues against `expmod`, the low 64 bits against
# products of 64-bit numbers, and sums of powers of two for the shifts.
# Rakudo is slow at three things this file avoids at a million bits: `%` by a
# small number, `.msb`/`.lsb`, and converting between Int and Str.
#
# Contract: exit 0 + last line PASS. Passes under both engines (Rakudo
# 2026.09), so it doubles as an oracle.

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eqv $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got.raku.substr(0, 200)}] WANT [{$want.raku.substr(0, 200)}]";
    }
}

# A family of cases reports one line, naming the first case that missed.
my (%miss, %count);
sub agree(Str $family, Str $case, $got, $want) {
    %count{$family}++;
    %miss{$family} //= "$case: GOT [{$got.raku.substr(0, 120)}] WANT [{$want.raku.substr(0, 120)}]"
        unless $got eqv $want;
}
sub family-done(Str $family) {
    if %miss{$family}:exists {
        $fails++;
        say "not ok - $family";
        note %miss{$family};
    }
    else {
        say "ok - $family ({%count{$family} // 0} cases)";
    }
}

my $P = 1_000_000_007;
my $W = 0xFFFF_FFFF_FFFF_FFFF;                   # the low 64 bits, by +&

# 1. QUADRATIC DETECTORS, not benchmarks: each line below took 1.4 to 3.7 s
#    before and takes under a tenth of a second now. The bounds sit far above
#    the second, for a slow CI runner, and under the first. A miss ends the
#    file, since what follows would take minutes to say the same thing.
{
    my $x = 3 ** 631_000;                        # about a million bits each
    my $y = 7 ** 356_000;
    my $t0 = now;
    my $prod = $x * $y;
    my $mul = now - $t0;
    $t0 = now;
    my $pow = 3 ** 1_000_000;
    my $power = now - $t0;
    my $m = 2 ** 1_000_000 - 1;
    $t0 = now;
    my $left  = 1 +< 1_000_000;
    my $right = $m +> 500_000;
    my $shifts = now - $t0;
    check 'a million-bit product, low 64 bits', $prod +& $W, (($x +& $W) * ($y +& $W)) +& $W;
    check 'a million-bit product, mod p', $prod % $P, expmod(3, 631_000, $P) * expmod(7, 356_000, $P) % $P;
    check '3 ** 1_000_000, low 64 bits', $pow +& $W, expmod(3, 1_000_000, 2 ** 64);
    check '1 +< 1_000_000 is 2 ** 1_000_000', $left, $m + 1;
    check '(2**1_000_000 - 1) +> 500_000 is 2**500_000 - 1', $right, 2 ** 500_000 - 1;
    check 'the product is not quadratic', $mul < 1, True;
    check 'the power is not quadratic', $power < 1, True;
    check 'the shifts are not quadratic', $shifts < 1.5, True;
    if $mul >= 1 || $power >= 1 || $shifts >= 1.5 {
        note "product {$mul.round(0.01)} s, power {$power.round(0.01)} s, shifts {$shifts.round(0.01)} s; skipping the rest";
        say 'FAIL';
        exit 1;
    }
}

# 2. Products of numbers made of nines, both sides of the Karatsuba size (96
#    limbs, 864 digits) and the slices an unbalanced pair is cut into, against
#    their decimal closed form: (10**a - 1)(10**b - 1) for a >= b is
#    9{b-1} 8 9{a-b} 0{b-1} 1, and a square when a == b.
sub nines(Int $n) { ('9' x $n).Int }
sub nines-product(Int $a, Int $b) {
    '9' x ($b - 1) ~ '8' ~ '9' x ($a - $b) ~ '0' x ($b - 1) ~ '1'
}
for (1, 1), (9, 9), (10, 3), (863, 863), (864, 864), (865, 865), (1_727, 1_727),
    (1_728, 1_728), (3_000, 3_000), (10_000, 10_000), (10_000, 900), (10_000, 500),
    (9_999, 865), (2_610, 855), (2_601, 855), (5_000, 2), (4_321, 1_234) -> ($a, $b) {
    my $case = "$a x $b digits";
    my ($x, $y) = nines($a), nines($b);
    agree 'nines times nines, both orders and signs', $case, ($x * $y).Str, nines-product($a, $b);
    agree 'nines times nines, both orders and signs', $case, ($y * $x).Str, nines-product($a, $b);
    agree 'nines times nines, both orders and signs', $case, ((-$x) * $y).Str, '-' ~ nines-product($a, $b);
    agree 'nines times nines, both orders and signs', $case, ($y * -$x).Str, '-' ~ nines-product($a, $b);
    agree 'nines times nines, both orders and signs', $case, ((-$x) * (-$y)).Str, nines-product($a, $b);
}
family-done 'nines times nines, both orders and signs';

# 3. Products of powers at mixed widths, against residues and the low word.
for (40_000, 20_000), (6_000, 6_000), (2_000, 30_000), (700, 25_000), (100, 1_000) -> ($i, $j) {
    my ($x, $y) = 3 ** $i, 7 ** $j;
    my $case = "3**$i x 7**$j";
    my $prod = $x * $y;
    agree 'powers times powers', $case, $prod % $P, expmod(3, $i, $P) * expmod(7, $j, $P) % $P;
    agree 'powers times powers', $case, $prod +& $W, (($x +& $W) * ($y +& $W)) +& $W;
    agree 'powers times powers', $case, $prod, $y * $x;
    agree 'powers times powers', $case, ($prod div $x), $y if $i <= 6_000;
}
family-done 'powers times powers';

# 4. `**` on top of it: exact answers past the old budget, residues against
#    expmod, and the decimal length 10 ** N has by construction.
{
    my $p = 3 ** 2_000_000;                      # Overflow after 5.6 s, before
    check '3 ** 2_000_000 is an Int', $p.WHAT, Int;
    check '3 ** 2_000_000, low 64 bits', $p +& $W, expmod(3, 2_000_000, 2 ** 64);
    check '(2 ** 70_001) +> 70_000 is 2', (2 ** 70_001) +> 70_000, 2;
    check '10 ** 20_000 has 20,001 digits', (10 ** 20_000).Str.chars, 20_001;
    check '(-3) ** 300_001 is negative', ((-3) ** 300_001).sign, -1;
    check '(-3) ** 300_001 mod p', ((-3) ** 300_001) % $P, ($P - expmod(3, 300_001, $P)) % $P;
}

# 5. `+>`: sums of powers of two shifted right, against the sum of the terms
#    that survive, at counts from one bit to past the top, so that both ways
#    of shifting (long division for a few limbs, binary past that) run. A
#    negative operand floors: one more below when a set bit falls off.
for (1_000_000, 777_777, 12_345), (200_000, 100_000, 64), (5_000, 4_000, 3_000), (300, 200, 100) -> ($a, $b, $c) {
    my $x = 2 ** $a + 2 ** $b + 2 ** $c;
    for 1, 29, 30, 63, 64, 65, 100, 128, 1_000, $c - 1, $c, $c + 1, $b, ($a + $b) div 2, $a - 1, $a, $a + 1, 10 ** 7 -> $s {
        next if $s < 0;
        my $kept = [+] ($a, $b, $c).grep(* >= $s).map({ 2 ** ($_ - $s) });
        my $exact = $c >= $s;                    # nothing set falls off
        my $case = "2**$a + 2**$b + 2**$c +> $s";
        agree '+> of a sum of powers', $case, $x +> $s, $kept;
        agree '+> of a negative floors', $case, (-$x) +> $s, $exact ?? -$kept !! -$kept - 1;
    }
}
family-done '+> of a sum of powers';
family-done '+> of a negative floors';

# 6. `+<`: shifted back with `+>` it is the number again, its low bits are
#    clear, and its residue is the operand's times 2**s.
for 3 ** 30, 3 ** 1_000, -(3 ** 1_000), 3 ** 40_000 + 1, -(7 ** 20_000) -> $x {
    my $what = "{$x.sign < 0 ?? 'negative' !! 'positive'} {$x.abs.chars}-digit x";
    for 1, 29, 30, 63, 64, 65, 1_000, 33_333, 250_000 -> $s {
        my $l = $x +< $s;
        my $case = "$what, s=$s";
        agree '+< then +> is the number', $case, $l +> $s, $x;
        agree '+< clears the low bits', $case, $l +& (2 ** $s - 1), 0;
        agree '+< is times 2**s', $case, $l % $P, ($x % $P) * expmod(2, $s, $P) % $P;
        agree '+< of the negation is the negation', $case, (-$x) +< $s, -$l;
    }
}
family-done '+< then +> is the number';
family-done '+< clears the low bits';
family-done '+< is times 2**s';
family-done '+< of the negation is the negation';

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
