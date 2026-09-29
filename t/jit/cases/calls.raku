# Calls inside a kernel (docs/dev/plans/TYPES-PLAN.md, phase N5). The kernel
# writes the loop's variables back before each call, runs it through the
# interpreter (a builtin through its function), and reloads them after. What
# it must match: a callee that closes over a loop variable sees it live and
# may change it, even to another type; a `last` / `next` the callee raises
# leaves / continues the innermost loop; a `die` leaves the values a CATCH
# outside reads; an `is rw` callee keeps the loop interpreted. The interpreter
# is the oracle; its answers were checked against Rakudo 2026.09.
sub sq($x) { $x * $x }
{ my $s = 0; my $i = 0; while $i < 1000 { $s = $s + sq($i); $i = $i + 1 }; say "K1 $s" }
{ my $i = 0; while $i < 3 { say "K2 line $i"; $i = $i + 1 } }
my $total = 0;
sub add($x) { $total = $total + $x }
{ my $i = 0; while $i < 100 { add($i); $total = $total + 1; $i = $i + 1 }; say "K3 $total" }
sub stop($i) { last if $i == 7; $i }
{ my $s = 0; my $i = 0; while $i < 20 { $s = $s + stop($i); $i = $i + 1 }; say "K4 s=$s i=$i" }
sub skip($i) { next if $i %% 2; $i }
{ my $s = 0; my $i = 0; while $i < 20 { $i = $i + 1; $s = $s + skip($i) }; say "K5 s=$s i=$i" }
sub boom($i) { die "boom at $i" if $i == 42; $i }
{ my $s = 0; my $i = 0; try { while $i < 100 { $s = $s + boom($i); $i = $i + 1 }; CATCH { default { say "K6 ", .message, " s=$s i=$i" } } } }
sub inc($x is rw) { $x = $x + 1 }
{ my $i = 0; my $n = 0; while $i < 10 { inc($n); $i = $i + 1 }; say "K7 $n" }
{ my $s = 0e0; my $i = 1; while $i < 1000 { $s = $s + sqrt($i) + abs(-$i); $i = $i + 1 }; say "K8 $s" }
sub g(int $x) { $x + 1 }
{ my int $i = 0; my int $s = 0; while $i < 1000 { $s = $s + g($i); $i = $i + 1 }; say "K9 $s" }
my $v = 0;
sub retype() { $v = "done" if $v == 5 }
{ my $i = 0; while $i < 10 { $v = $i; retype(); $i = $i + 1 }; say "K10 $v" }
{ my $o = 0; my $j = 0; while $j < 5 { my $i = 0; while $i < 10 { $o = $o + stop($i); $i = $i + 1 }; $j = $j + 1 }; say "K11 o=$o j=$j" }
multi mm(Int $x) { "int" }; multi mm(Num $x) { "num" }
{ my $s = ""; my $i = 0; while $i < 3 { $s = $s ~ mm($i) ~ mm($i + 0e0); $i = $i + 1 }; say "K12 $s" }
sub outer($i) { stop($i) }
{ my $s = 0; my $i = 0; while $i < 20 { $s = $s + outer($i); $i = $i + 1 }; say "K13 s=$s i=$i" }
