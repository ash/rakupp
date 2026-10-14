# Regression: a Rat or FatRat with parts wider than 2**53 converted to Num an
# ulp LOW whenever its value sat exactly halfway between two doubles, or a
# hair past halfway. The conversion long-divided to 19 decimal digits and let
# strtod round that: the division truncated first, so the tie or the hair was
# gone before the one rounding that was meant to decide it. Found by
# t/oracle/rat.raku (ORACLE-PLAN P2), Python's float(Fraction) as the oracle,
# which rounds correctly; Rakudo does too. BigInt::ratioToDouble now rounds
# once, in binary, ties to even.
#
# Expectations checked against Rakudo 2026.09: green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got === $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# n, d, the nearest double (ties to even), from Python's float(Fraction(n, d))
my @halfway = (
    (1004159042751543290853549751568171008, 1, 1.0041590427515434e36),
    (13493738785449705, 452312848583266388373324160190187140051835877600158453279131187530910662656, 2.9832755862927113e-59),
    (11832788668044407, 98079714615416886934934209737619787751599303819750539264, 1.2064460744448826e-40),
    (17457198415983359, 562949953421312, 31.01021380299926e0),
    (9243441777624755, 1208925819614629174706176, 7.645995831713893e-09),
    (14041909453991129, 41538374868278621028243970633760768, 3.380466736726967e-19),
    (16022682722970251, 348449143727040986586495598010130648530944, 4.598284430143896e-26),
    (262773616232279483651382048241092850328276960698167544111955968, 1, 2.6277361623227946e62),
);
for @halfway -> ($n, $d, $want) {
    ck FatRat.new($n, $d).Num, $want, "FatRat $n/$d is a tie, rounded to even";
    # a Rat's denominator stops at 2**64; past that `/` makes a Num already
    ck ($n / $d).Num, $want, "Rat $n/$d, the same" if $d < 2**64;
}

# A hair either side of a tie, with parts too long for 19 digits to see it.
ck FatRat.new(45035996273704975000000000000000000000031525197391593483, 10000000000000000000000000000000000000007).Num, 4503599627370498e0,
   'a hair above halfway rounds up';
ck FatRat.new(45035996273704975000000000000000000000031525197391593482, 10000000000000000000000000000000000000007).Num, 4503599627370497e0,
   'a hair below halfway rounds down';

# Still right where it always was.
ck (1/3).Num, 0.3333333333333333e0, '1/3';
ck FatRat.new(10**30 + 1, 7).Num, 1.4285714285714285e29, 'wide over narrow';
ck FatRat.new(-1, 10**400).Num, -0e0, 'underflow keeps its sign';
ck FatRat.new(3**30_000 + 2, 3**29_990 * 2**10 + 1).Num, 57.6650390625e0, 'parts of 14,000 digits';
ck FatRat.new(10**400, 3).Num, Inf, 'overflow';

say $fails ?? 'FAIL' !! 'PASS';
