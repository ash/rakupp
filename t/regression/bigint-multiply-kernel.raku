# BigInt torture: every edge a multiply/add kernel can get wrong.
# Runs on BOTH engines and must print byte-identical output. Contract:
# exit 0 with PASS as the last line.
my @fail;
sub ck($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want;
}

# ---- zero and identity ------------------------------------------------
my $B = 1000000000;              # the limb base
my $huge = $B ** 7;              # 7 limbs exactly, top limb 1
ck $huge * 0,      0,        'huge * 0';
ck 0 * $huge,      0,        '0 * huge';
ck $huge * 1,      $huge,    'huge * 1';
ck $huge + 0,      $huge,    'huge + 0';
ck $huge - $huge,  0,        'huge - huge';
ck (-$huge) + $huge, 0,      '-huge + huge';
ck ($huge * 0).Str, "0",     'zero stringifies as 0';

# ---- sign combinations ------------------------------------------------
my $a = 123456789012345678901234567890;
my $b = 987654321098765432109876543;
ck   $a  *   $b , 121932631137021795226185032707696997639644871231852004270, 'pos * pos';
ck (-$a) *   $b , -121932631137021795226185032707696997639644871231852004270, 'neg * pos';
ck   $a  * (-$b), -121932631137021795226185032707696997639644871231852004270, 'pos * neg';
ck (-$a) * (-$b), 121932631137021795226185032707696997639644871231852004270, 'neg * neg';
ck ($a - $a).Str, "0", 'a - a is bare zero';
ck (-$a + $a) * $b, 0, 'zero times big';

# ---- carry out of the top limb ---------------------------------------
# 999_999_999 is BASE-1: the worst carry a base-1e9 limb can make.
my $ones = $B - 1;
ck $ones * $ones, 999999998000000001, 'max-limb squared';
my $all9 = 10 ** 45 - 1;                        # five full limbs of 9s
ck $all9 * $ones, 999999998999999999999999999999999999999999999000000001,
                                                 'full-9s times max limb';
ck ($all9 * $all9).chars, 90,                   'full-9s squared digit count';
ck $all9 * ($B ** 3), 999999999999999999999999999999999999999999999000000000000000000000000000,
                                                 'shift by three whole limbs';

# ---- results with an interior / leading zero limb ---------------------
# (BASE**2 + 1) * BASE == BASE**3 + BASE: limb 1 and limb 2 are zero.
my $sparse = ($B ** 2 + 1) * $B;
ck $sparse, 1000000000000000001000000000, 'product with interior zero limbs';
ck $sparse.chars, 28, 'sparse product digit count';
# A product whose TOP limb comes out zero must be trimmed, not left dangling.
ck ($B ** 2) * $B, $B ** 3, 'exact power product';
ck (($B ** 2) * $B).Str.chars, 28, 'exact power digit count';

# ---- single limb x multi limb (the mulLimb path) ----------------------
my $acc = 1;
$acc *= $_ for 1 .. 200;
ck $acc.chars, 375, '200! digit count';
ck $acc % 1000000007, 722479105, '200! mod 1e9+7';
ck ($acc / (2 ** 197)).WHAT.^name, 'Rat', '200! over a power of two is a Rat';

# ---- self-multiplication and aliasing --------------------------------
my $s = 3 ** 100;
ck $s * $s, 3 ** 200, 'square via a * a';
my $t = 3 ** 100;
$t = $t * $t;
ck $t, 3 ** 200, 'square via t = t * t';
my $u = 3 ** 100;
$u *= $u;
ck $u, 3 ** 200, 'square via u *= u';
# THE aliasing case: a copy must not move when the original does.
my $orig = 10 ** 40;
my $copy = $orig;
$orig *= 7;
ck $copy, 10 ** 40, 'copy unchanged after original *=';
ck $orig, 7 * 10 ** 40, 'original updated by *=';
my @arr = 10 ** 40 xx 3;
@arr[0] *= 3;
ck @arr[1], 10 ** 40, 'sibling array element unchanged after *=';
my %h = a => 10 ** 40;
my $peek = %h<a>;
%h<a> *= 5;
ck $peek, 10 ** 40, 'hash read-out unchanged after *=';
sub bump($x is copy) { $x *= 9; $x }
my $arg = 10 ** 40;
ck bump($arg), 9 * 10 ** 40, 'is copy parameter multiplies';
ck $arg, 10 ** 40, 'caller value unchanged after is-copy *=';

# ---- in-place *= against every multiplier shape ----------------------
# (a `mulLimbInPlace` kernel has to handle each of these without a fresh
#  allocation, and must not disturb any Value that shares the magnitude.)
my $z = 10 ** 40; $z *= 0;
ck $z, 0, 'big *= 0 collapses to zero';
ck $z.WHAT.^name, 'Int', 'collapsed product is an Int';
my $o = 10 ** 40; $o *= 1;
ck $o, 10 ** 40, 'big *= 1 is identity';
my $n1 = 10 ** 40; $n1 *= -1;
ck $n1, -(10 ** 40), 'big *= -1 flips sign';
my $n7 = 10 ** 40; $n7 *= -7;
ck $n7, -7 * 10 ** 40, 'big *= -7';
my $neg = -(10 ** 40); $neg *= -7;
ck $neg, 7 * 10 ** 40, 'negative big *= negative';
my $maxlimb = 10 ** 40; $maxlimb *= 999999999;
ck $maxlimb, 9999999990000000000000000000000000000000000000000, 'big *= BASE-1';
my $atbase = 10 ** 40; $atbase *= 1000000000;
ck $atbase, 10 ** 49, 'big *= BASE (two-limb multiplier)';
my $overbase = 10 ** 40; $overbase *= 1000000001;
ck $overbase, 10000000010000000000000000000000000000000000000000, 'big *= BASE+1';
my $big64 = 10 ** 40; $big64 *= 18446744073709551615;
ck $big64, 184467440737095516150000000000000000000000000000000000000000, 'big *= u64max';
# the multiplicand shrinking to a machine word must re-box as a plain Int
my $shrink = 10 ** 30; $shrink = $shrink div (10 ** 25);
ck $shrink, 100000, 'big divided back into a small Int';
ck $shrink.WHAT.^name, 'Int', 'reboxed small is an Int';
# a shared magnitude reached through a closure and through a binding
my $shared = 10 ** 40;
my &peek = { $shared };
my $bound := $shared;
my $snap = peek();
$shared *= 11;
ck $snap, 10 ** 40, 'closure-captured snapshot unchanged after *=';
ck $shared, 11 * 10 ** 40, 'the multiplied variable moved';
# Rat parts share magnitudes too
my $rat = (10 ** 40) / 3;
my $ratcopy = $rat;
my $scaled = $rat * 7;
ck $ratcopy, (10 ** 40) / 3, 'Rat copy unchanged by a scaling';
ck $scaled, (7 * 10 ** 40) / 3, 'Rat scaled exactly';
ck $scaled.numerator, 7 * 10 ** 40, 'scaled Rat numerator';
ck $scaled.denominator, 3, 'scaled Rat denominator';
# reduction / cross-product forms
ck ([*] 1 .. 25), 15511210043330985984000000, 'reduce-multiply to 25!';
ck ((10 ** 20, 10 ** 21) X* (2, 3)).join(','),
   '200000000000000000000,300000000000000000000,2000000000000000000000,3000000000000000000000',
   'cross-multiply over bigs';

# ---- fitsU64 / fitsU128 boundaries -----------------------------------
my $u64max  = 18446744073709551615;      # 2**64 - 1
my $u128max = 340282366920938463463374607431768211455;
ck $u64max * 1, $u64max, 'u64max * 1';
ck $u64max * $u64max, 340282366920938463426481119284349108225, 'u64max squared';
ck ($u64max + 1) * ($u64max + 1), 340282366920938463463374607431768211456, '2**64 squared';
ck $u128max + 1, 340282366920938463463374607431768211456, 'u128max + 1';
ck $u128max * 2, 680564733841876926926749214863536422910, 'u128max doubled';
ck ($u128max + 1) * ($u128max + 1), 2 ** 256, '2**128 squared';
# The 5-limb valU128 guard band: mag[4] == 339 passes, 340 must not.
ck 339999999999999999999999999999999999999 + 1, 340000000000000000000000000000000000000, 'just under the 339 guard';
ck 340000000000000000000000000000000000000 * 3, 1020000000000000000000000000000000000000, 'just over the 339 guard';
ck (2 ** 63 - 1) * 2, 18446744073709551614, 'int64 max doubled';
ck (2 ** 63) * -1, -9223372036854775808, 'int64 min via negation';
ck (-(2 ** 63)) - 1, -9223372036854775809, 'one below int64 min';

# ---- the +=/-= and running-sum lanes ---------------------------------
my $sum = 0;
$sum += 10 ** 30 for ^10;
ck $sum, 10 ** 31, 'ten big additions';
my $dn = 10 ** 31;
$dn -= 10 ** 30 for ^10;
ck $dn, 0, 'ten big subtractions back to zero';

# ---- division / gcd / Rat round-trips --------------------------------
my $p = $a * $b;
ck $p div $a, $b, 'product div a';
ck $p div $b, $a, 'product div b';
ck $p % $a, 0, 'product mod a';
ck ($a * 6) gcd ($a * 10), $a * 2, 'gcd of two big multiples';
ck ($a * 6) lcm ($a * 10), $a * 30, 'lcm of two big multiples';
my $r = ($a * 3) / ($a * 6);
ck $r, 1/2, 'big Rat reduces';
ck $r.WHAT.^name, 'Rat', 'reduced big Rat is a Rat';
ck (1 / $huge).Num > 0e0, True, 'reciprocal of huge is positive';

# ---- rendering paths -------------------------------------------------
ck $p.Str.chars, 57, 'product digit count';
ck (2 ** 200).base(16), '100000000000000000000000000000000000000000000000000', '2**200 in hex';
ck (2 ** 100).polymod(256 xx 4).join(','), '0,0,0,0,295147905179352825856', '2**100 polymod bytes';
ck (10 ** 30).Numeric, 10 ** 30, 'huge .Numeric is itself';
ck (255 * 2 ** 120).base(2).chars, 128, 'shifted byte in binary';
ck (-(10 ** 30)).abs, 10 ** 30, 'abs of a big negative';
# NB: `.succ`/`.pred` on a big Int are BROKEN in rakupp today (they saturate
# through toInt(); MethodCallPart3.cpp:905/920) — unrelated to the multiply
# kernel, so this file uses ++/-- , which take the exact path.
my $inc = 10 ** 30; $inc++;
ck $inc, 1000000000000000000000000000001, 'increment of a big';
my $dec = 10 ** 30; $dec--;
ck $dec, 999999999999999999999999999999, 'decrement of a big';

# ---- bitwise (base-2**32 limb path) ----------------------------------
ck (2 ** 100) +| 1, 1267650600228229401496703205377, 'big bitwise or';
ck (2 ** 100) +& (2 ** 100), 2 ** 100, 'big bitwise and with self';
ck ((2 ** 100) +^ (2 ** 100)), 0, 'big xor with self';
ck (-(2 ** 100)) +& (2 ** 100 - 1), 0, 'negative big and';
ck (1 +< 100), 2 ** 100, 'big left shift';
ck ((2 ** 100) +> 99), 2, 'big right shift';

# ---- exponent ladder (pow uses base = base * base) -------------------
ck (7 ** 0), 1, '7**0';
ck (7 ** 1), 7, '7**1';
ck (7 ** 64).chars, 55, '7**64 digit count';
ck ((-7) ** 65) < 0, True, 'odd power of a negative is negative';
ck ((-7) ** 64) > 0, True, 'even power of a negative is positive';
ck (0 ** 0), 1, '0**0';
ck (1 ** (10 ** 6)), 1, '1 to a huge power';

# ---- comparison / equality across the boundary -----------------------
ck ($u64max == $u64max + 0), True, 'big equality';
ck ($u64max < $u64max + 1), True, 'big less-than';
ck ($huge <=> $huge * 2), Order::Less, 'big spaceship';
ck ($huge == $huge.Str.Int), True, 'round trip through Str';
ck (10 ** 30).WHAT.^name, 'Int', 'big is still an Int';

# ---- magnitude LENGTH sweep: the segmented single-limb kernel -------
# BigInt.cpp switches from one carry chain to eight segmented chains at
# n >= 32 limbs (mulRun / mulRunK<8>, q = n/8), and pays back K-1 carries
# with addLimbAt. So the answer has to be checked either side of 32, at
# every n % 8 residue, and where a segment boundary q changes. 9 decimal
# digits = 1 limb.
#
# Two independent identities per length, neither of which the kernel can
# satisfy by accident:
#   * x*m - x*(m-1) == x                     (a dropped carry breaks it)
#   * x*m == x*(m+BASE) - x*BASE             (single-limb kernel vs the
#     general schoolbook one: m+BASE and BASE are both TWO limbs, so the
#     right-hand side never touches the single-limb path)
my $BASE = 1000000000;
for (28 .. 42).Slip, 47, 48, 49, 63, 64, 65, 100, 257 -> $limbs {
    my $x = 10 ** (9 * $limbs) - 1;          # $limbs limbs of all-nines
    for 2, 3, 999999999, 536870912, 1000000 -> $m {
        ck $x * $m - $x * ($m - 1), $x, "carry: {$limbs}L x $m";
        ck $x * $m, $x * ($m + $BASE) - $x * $BASE,
           "single-limb vs schoolbook: {$limbs}L x $m";
        ck ($x * $m) div $m, $x, "round trip: {$limbs}L x $m";
        ck ($x * $m) % $m, 0, "exact: {$limbs}L x $m";
    }
    # a magnitude with an interior zero limb, so a rippling fold-back has
    # somewhere to ripple THROUGH
    my $sparse2 = 10 ** (9 * $limbs) + 1;
    ck $sparse2 * 999999999 - $sparse2 * 999999998, $sparse2, "sparse carry: {$limbs}L";
}

# ---- the benchmark kernel itself, checked ----------------------------
my $fact = 1;
$fact *= $_ for 1 .. 5000;
ck $fact.chars, 16326, '5000! digit count';
ck $fact % 1000000007, 541108809, '5000! mod 1e9+7';
ck $fact.Str.comb.map(*.Int).sum, 67698, '5000! digit sum';
ck $fact.Str.substr(0, 20), '42285779266055435222', '5000! leading digits';
ck $fact.Str.substr(*- 20), '00000000000000000000', '5000! trailing digits';
ck ($fact div (5000 * 4999)) * (5000 * 4999), $fact, '5000! divides back exactly';
my $f1000 = 1;
$f1000 *= $_ for 1 .. 1000;
ck $f1000.chars, 2568, '1000! digit count';
ck $f1000 % 1000000007, 641419708, '1000! mod 1e9+7';
ck $f1000.Str.comb.map(*.Int).sum, 10539, '1000! digit sum';
# the same product built RIGHT to left must agree
my $rev = 1;
$rev *= $_ for (1 .. 1000).reverse;
ck $rev, $f1000, '1000! is order-independent';
# …and built by a general (multi-limb x multi-limb) split
my $lo = 1; $lo *= $_ for 1 .. 500;
my $hi = 1; $hi *= $_ for 501 .. 1000;
ck $lo * $hi, $f1000, '1000! as a product of two halves';

if @fail { note $_ for @fail; say "FAIL" ; exit 1 }
say "PASS";
