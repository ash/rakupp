# Native `int` / `num` variables in a kernel (docs/dev/plans/TYPES-PLAN.md).
# The interpreter is the oracle, as for every case here; its answers were
# checked against Rakudo 2026.09 when this was written. What the kernel must
# match: a native int operation WRAPS at 64 bits; a store into a native is
# checked BEFORE it lands, so a refused one leaves the old value for a CATCH;
# a native of the other kind converts (num into int truncates toward zero,
# saturates, NaN is 0); a literal counts as native only beside a native of its
# own kind; `/` is never native.
{ my int $s = 0; my int $i = 0; while $i < 1000 { $s = $s * 3000000007 + $i; $i = $i + 1 }; say "C1 $s" }
{ my int $s = 9223372036854775000; my int $i = 0; while $i < 1000 { $s = $s + 1; $i++ }; say "C2 $s" }
{ my int $s = 9223372036854775000; my int $i = 0; while $i < 1000 { $s += 1; $i++ }; say "C3 $s" }
{ my int $s = 0; my int $i = 0; my int $m = -9223372036854775807 - 1; while $i < 1000 { $s = -$m; $i++ }; say "C4 $s" }
{ my int $x = 0; my int $i = 0; try { while $i < 1000 { $i = $i + 1; if $i == 500 { $x = $i / 3 } }; CATCH { default { say "C5 dies ", .^name, " i=$i x=$x" } } } }
{ my num $n = 0e0; my int $i = 0; try { while $i < 1000 { $i = $i + 1; if $i == 500 { $n = $i } }; CATCH { default { say "C6 dies ", .^name, " i=$i" } } } }
{ my int $x = 0; my int $i = 0; while $i < 1000 { $i = $i + 1; $x = True }; say "C7 $x" }
{ my int $t = 0; my int $i = 0; while $i < 1000 { my int $k; $k = $k + $i; $t = $t + $k; $i++ }; say "C8 $t" }
{ my num $t = 0e0; my int $i = 0; while $i < 1000 { my num $z; $t = $t + $z + 1e0; $i++ }; say "C9 $t" }
{ my int $t = 0; my int $i = 1; while $i < 1000 { $t = $t + ($i ** -1); $i++ }; say "C10 $t" }
{ my int $s = 9223372036854775000; my $b = 0; my int $i = 0; while $i < 1000 { $b = $s + $i; $i++ }; say "C11 $b" }
{ my int $s = 0; my int $i = 0; while $i < 1000 { $s = $s + $i * 3 div 2 - $i % 7; $i++ }; say "C12 $s" }
{ my num $q = 2.5e0; my int $t = 0; my int $i = 0; try { while $i < 1000 { $t = $q + 1; $i++ }; say "D1 $t"; CATCH { default { say "D1 dies i=$i t=$t" } } } }
{ my num $q = 2.5e0; my int $t = 0; my int $i = 0; while $i < 1000 { $t = $q + 1e0; $i++ }; say "D2 $t" }
{ my num $q = 2.5e0; my int $t = 0; my int $i = 3; my int $n = 0; while $n < 1000 { $t = $q * $i; $n++ }; say "D3 $t" }
{ my num $q = 2.5e0; my int $t = 0; my int $i = 0; try { while $i < 1000 { $t = $q / 2e0; $i++ }; say "D4 $t"; CATCH { default { say "D4 dies i=$i t=$t" } } } }
{ my num $q = 2.5e0; my int $t = 0; my int $i = 0; while $i < 1000 { $t = -$q; $i++ }; say "D5 $t" }
{ my int $i = 0; my num $u = 0e0; while $i < 1000 { $u = $i + 1; $i++ }; say "D6 $u" }
{ my int $m = 5; my num $q = 3.7e0; my int $i = 0; while $i < 1000 { $m = 5; $m += $q; $i++ }; say "D7 $m" }
{ my int $m = 5; my int $i = 0; try { while $i < 1000 { $m = 5; $m += 1.5e0; $i++ }; say "D8 $m"; CATCH { default { say "D8 dies i=$i m=$m" } } } }
{ my num $big = 1e30; my num $nan = NaN; my int $a = 0; my int $b = 1; my int $i = 0; while $i < 1000 { $a = $big; $b = $nan; $i++ }; say "D9 $a $b" }
{ my int $x = 0; my num $s = 0e0; while $x < 1000 { $s = $s + $x * 0.5e0; $x++ }; say "D10 $s" }
