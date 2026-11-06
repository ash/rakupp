# A call inside a kernel passes each argument as the interpreter's value of it
# would be, native tags and all (SPEED-BACKLOG 51): `f($n % 3)` with `my int $n`
# hands f a native int, and a multi's `int` candidate takes it, as Rakudo's
# does. Boxed plain, the kernel's argument went to the `Int` candidate from the
# tier-up on — and missed the dispatch cache the interpreted iterations had
# filled with the native shape, which made a `where`-constrained multi 52%
# slower under --cnp. Sub calls, method calls and a method's `where`; a native
# variable read bare, a native operation, a literal beside a native, a native
# beside a boxed Int, and `+$n`, which boxes. The interpreter is the oracle;
# checked against Rakudo 2026.09.
multi f(int $x) { "int" }
multi f(Int $x) { "Int" }
multi f(num $x) { "num" }
multi f(Num $x) { "Num" }
sub tally(%c) { %c.sort.map({ "{.key}={.value}" }).join(" ") }
{ my int $n = 0; my %c; while $n < 20 { %c{f($n)}++; $n = $n + 1 }; say "N1 ", tally(%c) }
{ my int $n = 0; my %c; while $n < 20 { %c{f($n % 3)}++; $n = $n + 1 }; say "N2 ", tally(%c) }
{ my int $n = 0; my %c; while $n < 20 { %c{f(-$n)}++; %c{f($n div 2)}++; $n = $n + 1 }; say "N3 ", tally(%c) }
{ my int $n = 0; my $u = 7; my %c; while $n < 20 { %c{f($n + $u)}++; %c{f(+$n)}++; $n = $n + 1 }; say "N4 ", tally(%c) }
{ my num $d = 0e0; my int $i = 0; my %c; while $i < 20 { %c{f($d)}++; %c{f($d * 2e0)}++; $d = $d + 1e0; $i = $i + 1 }; say "N5 ", tally(%c) }
class K { proto method m(|) {*}; multi method m(int $x) { "int" }; multi method m(Int $x) { "Int" } }
{ my $k = K.new; my int $n = 0; my %c; while $n < 20 { %c{$k.m($n % 3)}++; %c{$k.m($n + $n)}++; $n = $n + 1 }; say "N6 ", tally(%c) }
class W { proto method m(|) {*}; multi method m(Int $x where * > 0) { $x }; multi method m(Int $x) { 0 } }
{ my $w = W.new; my $t = 0; my int $n = 0; while $n < 300 { $t = $t + $w.m($n % 3); $n = $n + 1 }; say "N7 $t" }
