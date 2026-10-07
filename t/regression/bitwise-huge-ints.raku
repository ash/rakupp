# Regression: the bitwise operators +&, +| and +^ on an Int past 64 bits were
# QUADRATIC. Each operand went to binary 32 bits per BigInt divmod, every one a
# pass over all its limbs, and the answer came back 32 bits per multiply:
# `($x + $y) +& $y` for $x = 2 ** 100_000, $y = $x - 1 took 0.8 s, at twice
# the bits 3.3 s, at a million bits 83 s (0.1 s now). Both conversions are
# divide and conquer with Karatsuba joins now (BigInt::toBinary/fromBinary),
# and only the bits the SHORTER operand spans are combined at all: above them
# the answer is 0, -1, the longer operand or its complement, which is plain
# arithmetic on the longer operand.
#
# The operators work on the infinite two's complement, so every case comes in
# both signs, and in both operand orders, since the shorter one is picked.
# No expected value uses the operators under test on a big operand: they are
# closed forms over powers of two, `div` and `mod` by a power of two, or a
# reference that walks both operands 32 bits at a time and combines digits
# small enough for any engine's native path.
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

# 1. QUADRATIC DETECTOR, not a benchmark: the three operators on 300,000-bit
#    operands, about 35 ms now against 22 s before. The bound sits far above
#    the first, for a slow CI runner, and well under the second. A miss ends
#    the file, since the million-bit cases in §6 would take minutes to say
#    the same thing.
{
    my $x = 2 ** 300_000;
    my $y = $x - 1;
    my $s = $x + $y;                                 # 2 ** 300_001 - 1, all ones
    my $t0 = now;
    my ($and, $or, $xor) = $s +& $y, $s +| $y, $s +^ $y;
    my $elapsed = now - $t0;
    check '(2**300_001 - 1) +& (2**300_000 - 1)', $and, $y;
    check '(2**300_001 - 1) +| (2**300_000 - 1)', $or,  $s;
    check '(2**300_001 - 1) +^ (2**300_000 - 1)', $xor, $x;
    check 'the operators are not quadratic', $elapsed < 5, True;
    if $elapsed >= 5 {
        note "{$elapsed.round(0.01)} s; skipping the rest";
        say 'FAIL';
        exit 1;
    }
}

# 2. Sums and negatives of powers of two, whose answers are closed forms. The
#    positions run from the bottom word through both edges of the 128-bit
#    window to widths where the conversions split.
my @pos = 0, 1, 63, 64, 65, 127, 128, 129, 192, 1_000, 1_024, 4_099, 20_011;
my %pow = @pos.map({ $_ => 2 ** $_ });
for @pos -> $a {
    for @pos -> $b {
        for @pos -> $c {
            next if $a == $b || $b == $c || $a == $c;
            my $p = %pow{$a} + %pow{$b};
            my $q = %pow{$b} + %pow{$c};
            agree 'two powers +& two powers', "a=$a b=$b c=$c", $p +& $q, %pow{$b};
            agree 'two powers +| two powers', "a=$a b=$b c=$c", $p +| $q, %pow{$a} + %pow{$b} + %pow{$c};
            agree 'two powers +^ two powers', "a=$a b=$b c=$c", $p +^ $q, %pow{$a} + %pow{$c};
        }
    }
}
family-done 'two powers +& two powers';
family-done 'two powers +| two powers';
family-done 'two powers +^ two powers';

for @pos -> $a {
    for @pos -> $b {
        my ($pa, $pb) = %pow{$a}, %pow{$b};
        my $case = "a=$a b=$b";
        agree '-2**a +& 2**b, both orders', $case, (-$pa) +& $pb, $b >= $a ?? $pb !! 0;
        agree '-2**a +& 2**b, both orders', $case, $pb +& (-$pa), $b >= $a ?? $pb !! 0;
        agree '-2**a +| 2**b, both orders', $case, (-$pa) +| $pb, $b >= $a ?? -$pa !! -$pa + $pb;
        agree '-2**a +| 2**b, both orders', $case, $pb +| (-$pa), $b >= $a ?? -$pa !! -$pa + $pb;
        agree '-2**a +^ 2**b, both orders', $case, (-$pa) +^ $pb, $b >= $a ?? -$pa - $pb !! -$pa + $pb;
        agree '-2**a +^ 2**b, both orders', $case, $pb +^ (-$pa), $b >= $a ?? -$pa - $pb !! -$pa + $pb;
        agree '-2**a +& -2**b', $case, (-$pa) +& (-$pb), -(2 ** max($a, $b));
        agree '-2**a +| -2**b', $case, (-$pa) +| (-$pb), -(2 ** min($a, $b));
        agree '-2**a +^ -2**b', $case, (-$pa) +^ (-$pb), 2 ** max($a, $b) - 2 ** min($a, $b);
    }
    my $pa = %pow{$a};
    agree '-2**a against 2**a - 1', "a=$a", (-$pa) +& ($pa - 1), 0;
    agree '-2**a against 2**a - 1', "a=$a", (-$pa) +| ($pa - 1), -1;
    agree '-2**a against 2**a - 1', "a=$a", (-$pa) +^ ($pa - 1), -1;
    for $pa + 1, -$pa - 1, $pa * 3 + 7 -> $x {
        agree 'x against itself, 0 and -1', "x={$x.msb}-bit", $x +& $x, $x;
        agree 'x against itself, 0 and -1', "x={$x.msb}-bit", $x +| $x, $x;
        agree 'x against itself, 0 and -1', "x={$x.msb}-bit", $x +^ $x, 0;
        agree 'x against itself, 0 and -1', "x={$x.msb}-bit", $x +& 0, 0;
        agree 'x against itself, 0 and -1', "x={$x.msb}-bit", $x +| 0, $x;
        agree 'x against itself, 0 and -1', "x={$x.msb}-bit", $x +^ 0, $x;
        agree 'x against itself, 0 and -1', "x={$x.msb}-bit", $x +& -1, $x;
        agree 'x against itself, 0 and -1', "x={$x.msb}-bit", -1 +| $x, -1;
        agree 'x against itself, 0 and -1', "x={$x.msb}-bit", $x +^ -1, -$x - 1;
    }
    # x +& -x isolates the lowest set bit
    agree 'x +& -x is the lowest set bit', "a=$a", ($pa * 3) +& -($pa * 3), $pa;
    agree 'x +& -x is the lowest set bit', "a=$a", -($pa * 5) +& ($pa * 5), $pa;
}
family-done '-2**a +& 2**b, both orders';
family-done '-2**a +| 2**b, both orders';
family-done '-2**a +^ 2**b, both orders';
family-done '-2**a +& -2**b';
family-done '-2**a +| -2**b';
family-done '-2**a +^ -2**b';
family-done '-2**a against 2**a - 1';
family-done 'x against itself, 0 and -1';
family-done 'x +& -x is the lowest set bit';

# 3. Masks, against `mod` and `div` by 2**N: x's low N bits are x mod 2**N for
#    either sign, and the rest is x div 2**N. The widths run from one word to
#    past the operand, so either side can be the shorter one.
my @xs = 3 ** 40_000, -(3 ** 40_000), 7 ** 9_000 + 12_345, -(7 ** 9_000) - 1,
         2 ** 70 + 1, -(2 ** 70) + 3, 2 ** 64 * 3 ** 1_000;
for @xs -> $x {
    for 1, 64, 100, 128, 1_000, 2_048, 25_265, 25_266, 63_398, 63_399, 70_000 -> $n {
        my $m = 2 ** $n;
        my ($lo, $hi) = $x mod $m, $x div $m;
        my $case = "x={$x.msb}-bit {$x < 0 ?? 'negative' !! 'positive'}, N=$n";
        agree 'x +& (2**N - 1) is x mod 2**N', $case, $x +& ($m - 1), $lo;
        agree 'x +& (2**N - 1) is x mod 2**N', $case, ($m - 1) +& $x, $lo;
        agree 'x +| (2**N - 1)', $case, $x +| ($m - 1), $hi * $m + $m - 1;
        agree 'x +| (2**N - 1)', $case, ($m - 1) +| $x, $hi * $m + $m - 1;
        agree 'x +^ (2**N - 1)', $case, $x +^ ($m - 1), $hi * $m + ($m - 1 - $lo);
        agree 'x +^ (2**N - 1)', $case, ($m - 1) +^ $x, $hi * $m + ($m - 1 - $lo);
        agree 'x +& -2**N clears the low N bits', $case, $x +& -$m, $hi * $m;
        agree 'x +& -2**N clears the low N bits', $case, -$m +& $x, $hi * $m;
        agree 'x +| -2**N', $case, $x +| -$m, -$m + $lo;
        agree 'x +| -2**N', $case, -$m +| $x, -$m + $lo;
        agree 'x +^ -2**N', $case, $x +^ -$m, (-$hi - 1) * $m + $lo;
        agree 'x +^ -2**N', $case, -$m +^ $x, (-$hi - 1) * $m + $lo;
    }
}
family-done 'x +& (2**N - 1) is x mod 2**N';
family-done 'x +| (2**N - 1)';
family-done 'x +^ (2**N - 1)';
family-done 'x +& -2**N clears the low N bits';
family-done 'x +| -2**N';
family-done 'x +^ -2**N';

# 4. Mixed widths and signs against a reference that walks both operands 32
#    bits at a time: floor `div` and `mod` give the two's complement digits of
#    either sign, the operator combines digits under 2**32, and once both
#    operands are down to 0 or -1 their sign bits combine the same way.
sub reference(Int $a is copy, Int $b is copy, &op) {
    my $place = 1;
    my $sum = 0;
    until ($a == 0 || $a == -1) && ($b == 0 || $b == -1) {
        $sum += op($a mod 4_294_967_296, $b mod 4_294_967_296) * $place;
        $a = $a div 4_294_967_296;
        $b = $b div 4_294_967_296;
        $place *= 4_294_967_296;
    }
    $sum + op($a, $b) * $place
}
my @ops = '+&', -> $a, $b { $a +& $b }, '+|', -> $a, $b { $a +| $b }, '+^', -> $a, $b { $a +^ $b };
my @vals = (1, 63, 64, 65, 128, 129, 200, 1_000, 1_025, 2_100, 3_000).map:
    { 2 ** ($_ - 1) + (3 ** ($_ + 7)) mod 2 ** ($_ - 1) };    # exactly $_ bits
for @vals -> $p {
    for @vals -> $q {
        for $p, -$p -> $x {
            for $q, -$q -> $y {
                for @ops -> $name, &op {
                    agree "mixed widths and signs, $name", "{$x.msb}-bit {$x.sign} against {$y.msb}-bit {$y.sign}",
                          op($x, $y), reference($x, $y, &op);
                }
            }
        }
    }
}
family-done "mixed widths and signs, $_" for <+& +| +^>;

# 5. Identities between the three at widths where every conversion splits many
#    times and the joins are Karatsuba products in both bases; the low word is
#    checked against the operators on that word alone.
{
    my $p = 2 ** 99_999 + (3 ** 70_000) mod 2 ** 99_999;
    my $q = 2 ** 54_321 + (7 ** 19_000) mod 2 ** 54_321;
    my $w = 2 ** 64;
    for $p, -$p -> $x {
        for $q, -$q, $p - 12_345, -$p + 67_890 -> $y {
            my ($and, $or, $xor) = $x +& $y, $x +| $y, $x +^ $y;
            my $case = "{$x.sign} {$x.msb}-bit against {$y.sign} {$y.msb}-bit";
            agree 'identities at 100,000 bits', $case, $and + $or, $x + $y;
            agree 'identities at 100,000 bits', $case, $or - $and, $xor;
            agree 'identities at 100,000 bits', $case, $xor +^ $y, $x;
            agree 'identities at 100,000 bits', $case, ($y +& $x, $y +| $x, $y +^ $x), ($and, $or, $xor);
            agree 'identities at 100,000 bits', $case, $and mod $w, ($x mod $w) +& ($y mod $w);
            agree 'identities at 100,000 bits', $case, $or  mod $w, ($x mod $w) +| ($y mod $w);
            agree 'identities at 100,000 bits', $case, $xor mod $w, ($x mod $w) +^ ($y mod $w);
        }
    }
}
family-done 'identities at 100,000 bits';

# 6. The numbers from the report, at a million bits: x = 2 ** 1_000_000,
#    y = x - 1, so x + y is a million and one 1 bits.
{
    my $x = 2 ** 1_000_000;
    my $y = $x - 1;
    my $s = $x + $y;
    check '(x + y) +& y',          $s +& $y,          $y;
    check '(x + y) +| y',          $s +| $y,          $s;
    check '(x + y) +^ y',          $s +^ $y,          $x;
    check '-(x + y) +& y',         (-$s) +& $y,       1;
    check '-(x + y) +| y',         (-$s) +| $y,       -$x - 1;
    check '-(x + y) +^ y',         (-$s) +^ $y,       -$x - 2;
    check '-(x + y) +& -y',        (-$s) +& (-$y),    -$s;
    check 'y +^ -(x + y)',         $y +^ (-$s),       -$x - 2;
    check 'one byte of (x + y)',   $s +& 0xFF,        255;
    check 'x +| 1',                $x +| 1,           $x + 1;
    check '(x + y) +^ 1',          $s +^ 1,           $s - 1;
    check '-x +& 0xFFFF',          (-$x) +& 0xFFFF,   0;
}

# 7. Answers that fit a machine word come back as plain Ints, and the
#    assignment and reduction forms go the same way.
{
    my $big = 2 ** 200 + 0b1011;
    check '(2**200 + 11) +& 0xF',        $big +& 0xF, 11;
    check '...and that is an Int',       ($big +& 0xF).WHAT, Int;
    check '(2**200 + 11) +^ 2**200',     $big +^ 2 ** 200, 11;
    check '-(2**200) +| (2**200 - 1)',   -(2 ** 200) +| (2 ** 200 - 1), -1;
    my $v = 2 ** 300 + 5;
    $v +&= 2 ** 300 + 3;
    check '+&=',                         $v, 2 ** 300 + 1;
    $v +|= 2 ** 400;
    check '+|=',                         $v, 2 ** 400 + 2 ** 300 + 1;
    $v +^= 2 ** 400 + 1;
    check '+^=',                         $v, 2 ** 300;
    check '[+^] over three',             [+^](2 ** 300, 2 ** 300 + 1, 2 ** 150), 2 ** 150 + 1;
    check '[+|] with a negative',        [+|](-(2 ** 300), 2 ** 299, 1), -(2 ** 300) + 2 ** 299 + 1;
    check '[+&] down to a bit',          [+&](2 ** 300 + 2 ** 7, 2 ** 301 + 2 ** 7, -(2 ** 7)), 2 ** 7;
}

# 8. A Buf write of a big Int stores its low 64 bits, read straight off its
#    limbs now. Between 2**63 and 2**64 an Int is a big one to Raku++ but
#    fits the word, so both engines store it.
for 2 ** 63, 2 ** 63 + 5, 2 ** 64 - 1, 0xDEADBEEFCAFEBABE -> $v {
    for LittleEndian, BigEndian -> $e {
        my $b = buf8.new;
        $b.write-uint64(0, $v, $e);
        agree 'write-uint64 of a big Int reads back', "$v $e", $b.read-uint64(0, $e), $v;
    }
}
family-done 'write-uint64 of a big Int reads back';

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
