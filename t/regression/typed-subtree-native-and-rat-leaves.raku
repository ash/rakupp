# A typed subtree (INTERP-SPEED-PLAN task 9) reads native and Rat leaves as
# machine numbers: a native operation wraps and stays native, as
# nativeIntArith and tagNativeNum make it on the general path; a Rat is
# exact in int64 parts and declines past them; a store into a native wraps to
# its width; a Rat stored over a Rat nobody else holds rewrites its parts in
# place, and over one that is shared leaves the other holder's value alone.
# Each case runs twice, so the second pass takes the compiled subtree and the
# fused store rather than the first evaluation's general path.
use Test;
plan 39;

my int $i = 100; my int $j = 3; my int $mx = 9223372036854775807;
my int $z = 4611686018427387904;
my $p = 0;
my num $n = 1.5e0;
my $a = 0.5; my $b = 1.25; my $k = 3; my $l = 4;

for ^2 -> $pass {
    next unless $pass;
    # native integer operations
    is $mx + 0 + 1, -9223372036854775808, 'a native operation wraps';
    is ($mx + $p) + 1, 9223372036854775808, '…but a boxed Int operand grows';
    is ($z * 4) div 2, 0, 'a native result stays native for the next operator';
    is -$mx - 2, 9223372036854775807, 'a negated native wraps';
    is $i * $j + $i, 400, 'native arithmetic';
    ok $i * $j > $i + $j, 'a native comparison';
    # native num
    isa-ok $n * $i + 1e0, Num, 'a native int beside a native num is a Num';
    is $n * $n + 1e0, 3.25, 'native num arithmetic';
}

# stores into natives: wrapped to the width, a num32 rounded
my int8 $s; my uint8 $u; my int16 $w; my num32 $f; my int $c;
for ^2 { $s = $i * $j + 1; $u = $i * $j - 1; $w = $i * $i * $i - 7; $f = 1.1e0 * 3e0 + 0.1e0; }
is $s, 45, 'an int8 wraps';
is $u, 43, 'a uint8 wraps';
is $w, 16953, 'an int16 wraps';
is-approx $f, 3.4000000953674316, 'a num32 rounds';
for ^2 { $c = 9223372036854775807; $c = $c * 2 + 1 }
is $c, -1, 'a native int store of a wrapped result';
for ^2 { $c = 0; $c = $n * $i - 140e0 }
is $c, 10, 'a native num result converts into a native int';
my $x;
for ^2 { $x = $i * $j + 1 }
is $x, 301, 'a $ takes a native result as an Int';
for ^2 { my $y = $mx + 0 + 1; $x = $y }
is $x, -9223372036854775808, '…and so does a declaration';

# Rats
my $r;
for ^2 { $r = $a * $b + $a - 0.1 }
is $r, 1.025, 'Rat arithmetic';
isa-ok $r, Rat, '…is a Rat';
for ^2 { $r = $k / $l + 1 }
is $r.nude, (7, 4), 'Int / Int is a Rat';
for ^2 { $r = $l / 2 + 0 }
isa-ok $r, Rat, '…even when it divides evenly';
for ^2 { $r = $a * 2e0 + 1 }
isa-ok $r, Num, 'a Rat beside a Num is a Num';
ok 0.1 + 0.2 == 0.3, 'Rat equality is exact';
my $t = 0;
for ^2 { $t = 0; $t++ if $a * $b + 1 <= 1.625 }
is $t, 1, 'a Rat comparison as a condition';
my $big = 4611686018427387903;
for ^2 { $r = $big / 7 + 1 }
is $r, 658812288346769701.428571, 'a Rat past the small lane takes the general path';
my $tiny = 1 / 4611686018427387904;
for ^2 { $r = $tiny * $tiny + 0 }
isa-ok $r, Num, 'a denominator past uint64 spills to Num';
my $acc = 0;
for 1..10 -> $m { $acc = $acc + $m / 10 * 0.5 }
is $acc.nude, (11, 4), 'a Rat accumulator';
my FatRat $fr = FatRat.new(1, 3);
for ^2 { $r = $fr * 3 + 0 }
isa-ok $r, FatRat, 'a FatRat leaf keeps the general path';
my $nr = -0.5;
for ^2 { $r = -$nr * 2 + 0 }
is $r, 1, 'a negated Rat';
my int $ni = 7;
for ^2 { $r = $ni / 2 + 1 }
is $r, 4.5, 'a native int over an Int is a Rat';
dies-ok { my $zero = 0; for ^2 { $r = ($a / ($zero * 1)).Str } }, 'a zero divisor is the general path’s';

# a Rat rewritten in place only when nobody else holds it
my $q = 0.5; my $copy = $q;
for ^3 { $q = $q * 2 + 0.25 }
is "$q $copy", '5.75 0.5', 'a copy keeps its value';
my @pushed; $q = 0.5;
for ^4 { $q = $q * $k + 0.25; @pushed.push($q) }
is @pushed, '1.75 5.5 16.75 50.5', 'each pushed value keeps its own';
my $bound := $q;
for ^2 { $q = $q * 2 + 0.5 }
is $bound, 203.5, 'a binding sees the new value';

# `!=` between an unsigned native and a negative signed one is False
# (nativeMixedSignNe, as Roast's S02-types/signed-unsigned-native.t pins it)
my uint8 $un = 255; my int8 $neg = -1; my int $zero = 0;
my @ne;
for ^2 { @ne = $un != $neg - $zero, ($un + $zero) != $neg }
is @ne, 'False True', 'an unsigned leaf keeps the mixed-sign rule; a result is signed';

# two leaves: `$x = $a op $b` and a condition, over Nums, natives and Rats
my num $ni2 = 0e0; my num $nn2 = 10e0; my num $ns2 = 0e0;
while $ni2 < $nn2 { $ni2 = $ni2 + 1e0; $ns2 = $ns2 + $ni2 }
is $ns2, 55, 'a native num counter';
my uint8 $u2 = 250;
for ^10 { $u2 = $u2 + 1 }
is $u2, 4, 'a uint8 wraps through two leaves';
my int $a2 = 7; my int $b2 = 2; my $r2;
for ^2 { $r2 = $a2 / $b2 }
is $r2.nude, (7, 2), 'two natives divided are a Rat';
my $x2 = 0.5; my $y2 = 0.25; my $c2 = 0;
for ^5 { $c2++ if $x2 > $y2 }
is $c2, 5, 'a Rat comparison of two leaves';
my num $f2 = 1.5e0; my int $i2 = 0;
for ^2 { $i2 = $f2 * $f2 }
is $i2, 2, 'a native num product into a native int converts';
