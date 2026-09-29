# The Rat lane (TYPES-PLAN N6): a Rat whose parts fit an int64 is a register
# pair in a kernel, and + - * / and every comparison stay exact on it; any
# overflow, a Rat beside a Num, and a zero divisor go to applyArith, which
# degrades to a Num where Raku does. `0.1 + 0.2 == 0.3` must stay True.
# The interpreter is the oracle; checked against Rakudo 2026.09.
{ my $s = 0; my $i = 0; while $i < 10 { $s = $s + 0.1; $i = $i + 1 }; say "R1 $s {$s.WHAT.^name} {$s == 1}" }
{ my $x = 0.1 + 0.2; my $n = 0; my $i = 0; while $i < 3 { $n = $n + 1 if 0.1 + 0.2 == 0.3; $i = $i + 1 }; say "R2 $n {$x.raku}" }
{ my $s = 0; my $i = 1; while $i <= 20 { $s = $s + 1 / $i; $i = $i + 1 }; say "R3 {$s.raku}" }
{ my $r = 6 / 3; my $i = 0; while $i < 3 { $r = $r * 1; $i = $i + 1 }; say "R4 {$r.raku} {$r.^name}" }
{ my $x = 0.5; my $n = 0; my $i = 0; while $i < 10 { $n = $n + 1 if $x < $i / 4; $n = $n + 1 if $x > 0; $i = $i + 1 }; say "R5 $n" }
{ my $r = 1/3; my $i = 0; while $i < 40 { $r = $r * $r + 1/7; $i = $i + 1 }; say "R6 {$r.^name} {$r.Str.substr(0,12)}" }
{ my $r = 1/3; my $s = 0e0; my $i = 0; while $i < 5 { $s = $s + $r * 1.5e0; $i = $i + 1 }; say "R7 $s" }
{ my $z = 0; my $i = 0; try { while $i < 3 { my $q = 1 / $z; $i = $i + 1 }; say "R8 ok i=$i"; CATCH { default { say "R8 dies ", .^name } } } }
{ my $r = 0.25; my $s = 0; my $i = 0; while $i < 5 { $s = $s + -$r; $r = $r + 1; $i = $i + 1 }; say "R9 {$s.raku} {$r.raku}" }
{ my $r = 0.5; my $s = 0.0; my $i = 0; while $i < 5 { $s += 0.5; $r++; $i = $i + 1 }; say "R10 {$s.raku} {$r.raku}" }
{ my $r = 0.0; my $n = 0; my $i = 0; while $i < 4 { $n = $n + 1 if $r; $r = $r + 0.5; $i = $i + 1 }; say "R11 $n" }
{ my $r = 7/2; my $n = 0; my $i = 0; while $i < 10 { $n = $n + 1 if $i > $r; $n = $n + 1 if $r == 3.5; $i = $i + 1 }; say "R12 $n" }
{ my $s = 0; my $i = 0; while $i < 100 { $s = $s + $i / 7 - $i / 11; $i = $i + 1 }; say "R13 {$s.raku}" }
# A million additions of a cent, and a running check against the exact total
my $total = 0; my $i = 0; my $bad = 0;
while $i < 10_000 { $total = $total + 0.01; $i = $i + 1; $bad = $bad + 1 if $total * 100 != $i }
say "$total $bad {$total.^name}";
