# Regression: native `int` / `num` semantics that a COMPILED program keeps too.
# No try/CATCH here on purpose, so that `--exe` compiles this natively rather
# than bundling the interpreter, and t/exe/run.raku compares the two. Values
# checked against Rakudo 2026.09. Before this, `--exe` compiled a native as a
# plain Int: `my int8` in a loop counted to 300 instead of wrapping to 44.
# Contract: exit 0 + last line PASS.
my @fail;
sub is($got, $want, $label) { @fail.push("$label: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my int $i = 5; my num $n = 0e0;
$n = $i;      is $n, 5e0, 'native int into num';
$n = $i + 1;  is $n, 6e0, 'native int result into num';
my num $m = $i; is $m, 5e0, 'declared from a native int';
my num $q = 3.7e0; my num $r = -3.7e0; my int $j = 0;
$j = $q;      is $j, 3, 'native num into int truncates';
$j = $r;      is $j, -3, '…toward zero';
my num $big = 1e30; $j = $big; is $j, 9223372036854775807, '…saturating';
my num $nan = NaN;  $j = $nan; is $j, 0, 'NaN into int is 0';
my int $k = 7; my num $n2 = 0e0; $n2 += $k; is $n2, 7e0, 'num += native int';
my int $mm = 5; $mm += $q;     is $mm, 8, 'int += native num converts';
my num $p = 2.5e0; my int $t = 0;
$t = $p + 1e0; is $t, 3, 'num + Num literal is native';
$t = $p * $i;  is $t, 12, 'num * int is native';
$t = -$p;      is $t, -2, 'negated native num';
my num $u = 0e0; $u = $i + 1; is $u, 6e0, 'int + Int literal into num';
my int8 $c = 127; $c = $c + 1; is $c, -128, 'int8 wraps';
my uint8 $b = 255; $b++;       is $b, 0, 'uint8 ++ wraps';
my int $w = 9223372036854775807; $w++; is $w, -9223372036854775808, 'int ++ wraps';
my int $z = 2**62; is ($z * 4) div 2, 0, 'a native chain wraps in the middle';
my int $s = 0; my int $e = 0;
while $e < 10 { $s = $s * 3000000000 + 7; $e = $e + 1 }
is $s, 5371122695617286663, 'a wrapping accumulator in a loop';
my int8 $h = 0; my $g = 0;
while $g < 300 { $h = $h + 1; $g = $g + 1 }
is $h, 44, 'int8 in a loop wraps at its width';
my num $acc = 0e0; my int $x = 0;
while $x < 1000 { my num $t2 = $x * 0.5e0; $acc = $acc + $t2; $x++ }
is $acc, 249750e0, 'a native num declared inside a loop';

say @fail ?? "FAIL: {@fail.join('; ')}" !! 'PASS';
exit @fail ?? 1 !! 0;
