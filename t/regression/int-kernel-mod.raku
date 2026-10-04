# Regression guard: `mod` inside a loop the integer kernel takes. The kernel
# had `%` and `div` but not `mod`, so a loop using it (mutsu's time-parts.raku)
# stayed interpreted: 169 ms against 11 ms once `mod` maps onto the kernel's
# floored modulo. Over Ints the two are the same operator; these pin the
# places they could differ — negative operands, results past int64 (the
# kernel hands those back), a zero divisor (`mod` throws where `%` fails
# softly), and a user `infix:<mod>` (the kernel must step aside).
#
# Every expectation below was checked against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $t = 0;
for ^100000 -> $i {
    my $ts = $i * 97 + 13;
    $t += ($ts mod 86400) div 3600 + $ts mod 60;
}
ck($t, 4097842, 'the time-parts shape');

my $neg = 0;
for -50..50 -> $i { $neg += $i mod 7 + $i mod -7 }
ck($neg, 0, 'negative operands take the divisor\'s sign');

my $big = 0;
for 1..3 -> $i { $big += (2**62 * $i) mod 1000 }
ck($big, 2424, 'operands past int64 still give the exact remainder');

my $z = 0;
my $r = try { my $q = 0; for ^3 -> $i { $q += $i mod $z }; $q };
ck($!.^name, 'X::Numeric::DivideByZero', 'a zero divisor throws');

{
    my $calls = 0;
    multi infix:<mod>(Int $a, Int $b where 1000) { $calls++; 0 }
    my $u = 0;
    for ^10 -> $i { $u += $i mod 1000 }
    ck($calls, 10, 'a user infix:<mod> is called on every iteration');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
