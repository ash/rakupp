# Subs a kernel compiles in place (copy-and-patch): untyped `$` parameters
# with no traits, and a body that is one expression over them alone. What must
# hold: a sub reading an outer variable, a typed parameter, a recursive sub, a
# wrong argument count or an overloaded operator is NOT compiled in place and
# still answers as the interpreter does. The interpreter is the oracle.
sub f($x) { $x * 2 + 1 }
{ my $s = 0; my $i = 0; while $i < 1000 { $s = $s + f($i); $i = $i + 1 }; say "I1 $s" }
sub clamp($v, $lo, $hi) { $v < $lo ?? $lo !! ($v > $hi ?? $hi !! $v) }
{ my $s = 0; my $i = -50; while $i < 50 { $s = $s + clamp($i, -10, 10); $i = $i + 1 }; say "I2 $s" }
sub both($a, $b) { $a > 0 && $b > 0 }
{ my $n = 0; my $i = -5; while $i < 5 { $n = $n + 1 if both($i, 5 - $i); $i = $i + 1 }; say "I3 $n" }
my $k = 3;
sub scaled($x) { $x * $k }
{ my $s = 0; my $i = 0; while $i < 10 { $s = $s + scaled($i); $i = $i + 1 }; say "I4 $s" }
sub typed(Int $x) { $x + 1 }
{ my $s = 0; my $i = 0; while $i < 10 { $s = $s + typed($i); $i = $i + 1 }; say "I5 $s" }
sub fact($n) { $n <= 1 ?? 1 !! $n * fact($n - 1) }
{ my $s = 0; my $i = 0; while $i < 10 { $s = $s + fact($i); $i = $i + 1 }; say "I6 $s" }
sub two($a, $b) { $a + $b }
{ my $i = 0; try { while $i < 5 { my $x = two($i); $i = $i + 1 }; CATCH { default { say "I7 dies ", .^name, " i=$i" } } } }
{ my $s = 0e0; my $i = 1; while $i < 100 { $s = $s + f($i / 4) + f(0.5e0 * $i); $i = $i + 1 }; say "I8 $s" }
sub less($a, $b) { $a < $b }
{ my $n = 0; my $i = 0; while $i < 10 { $n = $n + 1 if less($i, 5); $i = $i + 1 }; say "I9 $n" }
