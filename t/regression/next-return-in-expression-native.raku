# Regression (#195, the native half): `--exe` compiles `next`, `last` and
# `return` written as an operand — `my $x = f($i) // next`, `f($i) or next`,
# `my $x = f($i) // return 0`, a ternary branch — to the statement form's own
# jump. A `next` there was an exception (rtThrowNext, ~3 µs a skip against
# 0.1 for `continue`) and a `return` was refused, which sent the whole
# program to --aot. Every shape below compiles natively: t/run.raku checks
# that this file does, and t/exe/run.raku compares it, with `-O` and without,
# against the interpreter. (next-return-in-expression-cooperative.raku holds
# the interpreter's own shapes, one of which the backend does not lift.)
# Every answer below is Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

sub f($i) { $i %% 2 ?? $i !! Nil }

my @o;
for ^5 -> $i { my $x = f($i) // next; @o.push: $x }
check 'my $x = … // next', @o, [0, 2, 4];
my %h = a => 'A', c => 'C';
@o = (); for <a b c> { my Str $s = %h{$_} // next; @o.push: $s }
check 'my Str $s = %h{} // next', @o, ['A', 'C'];
my $y = 'init'; @o = ();
for ^4 -> $i { $y = f($i) // next; @o.push: $y }
check q{$outer = … // next}, @o, [0, 2];
check q{$outer keeps its last value}, $y, 2;
my Int $ti = 7; for ^3 -> $i { $ti = f($i) // next }
check 'typed outer', $ti, 2;
@o = (); for ^4 -> $i { my @a = f($i) // next; @o.push: @a.elems }
check 'my @a = … // next', @o, [1, 1];

# the left operand: the jump, and the right side never runs
my @side;
sub r { (return 5) or @side.push('x') }
check q{(return 5) or …}, r(), 5;
check q{(return 5) or … runs no right side}, @side, [];
@o = (); for ^2 { (next) or @o.push('y'); @o.push('z') }
check '(next) or …', @o, [];

my $c = 0; for ^6 -> $i { f($i) or next; $c++ }
check 'f() or next', $c, 2;   # f(0) is 0, which is false
class C { method m($i) { $i > 1 ?? "m$i" !! Nil } }
@o = (); for ^4 -> $i { my $x = C.m($i) // next; @o.push: $x }
check 'my $x = C.m() // next', @o, ['m2', 'm3'];
@o = (); for ^4 -> $i { C.m($i) or next; @o.push: $i }
check 'C.m() or next', @o, [2, 3];
@o = (); for <ab cd ae> { /a/ or next; @o.push: $_ }
check 'a regex on the left', @o, ['ab', 'ae'];
@o = (); for <ab cd ae> { /a/ && next; @o.push: $_ }
check 'a regex on the left of &&', @o, ['cd'];

@o = (); for ^6 -> $i { my $x = ($i < 3 ?? $i !! Nil) // last; @o.push: $x }
check '// last', @o, [0, 1, 2];
@o = (); for ^5 -> $i { my $x = $i %% 2 ?? $i !! next; @o.push: $x }
check 'ternary branch', @o, [0, 2, 4];
@o = (); for ^6 -> $i { my $x = $i == 0 ?? "z" !! ($i == 1 ?? (next) !! (f($i) // f($i + 1) // next)); @o.push: $x }
check 'nested ternary and chain', @o, ['z', 2, 4, 4, 6];
@o = (); for ^4 -> $i { my $x = f($i) || next; @o.push: $x }
check '|| next', @o, [2];

sub h($i) { my $x = f($i) // return 0; $x * 10 }
check '// return', (^5).map(&h).list, (0, 0, 20, 0, 40);
sub g($n) { my $x = ($n > 0 ?? g($n - 1) !! Nil) // return "base"; "g($x)" }
check '// return, recursive', g(3), 'g(g(g(base)))';
sub lr { for ^5 -> $i { my $x = ($i < 3 ?? $i !! Nil) // return "ret$i" }; 'end' }
check '// return from a loop', lr(), 'ret3';
sub orr($i) { f($i) or return 'no'; 'yes' }
check 'or return', (^3).map(&orr).list, ('no', 'no', 'yes');
# a routine's last statement: its value is the left side's when nothing jumps
sub tail-or($i) { f($i) or return 'no' }
check 'or return, last statement', (^4).map(&tail-or).list, ('no', 'no', 2, 'no');
sub tail-dor($i) { f($i) // return 'no' }
check '// return, last statement', (^3).map(&tail-dor).list, (0, 'no', 2);

my $i = 0; @o = (); while $i < 5 { $i++; my $x = f($i) // next; @o.push: $x }
check 'while', @o, [2, 4];
@o = (); loop (my $j = 0; $j < 5; $j++) { my $x = f($j) // next; @o.push: $x }
check 'loop', @o, [0, 2, 4];
$i = 0; @o = (); repeat { $i++; my $x = f($i) // next; @o.push: $x } while $i < 5;
check 'repeat', @o, [2, 4];
@o = (); for ^5 -> $i { if True { if True { my $x = f($i) // next; @o.push: $x } } }
check 'two blocks between', @o, [0, 2, 4];
# (no LEAVE here: compiled, a loop body's LEAVE does not run on a `next` of
# any form, the statement one included — TODO-FOLLOW-UPS.md)
@o = (); for ^4 -> $i { CATCH { default { @o.push: 'caught' } }; my $x = f($i) // next; @o.push: $x }
check 'CATCH does not see it', @o, [0, 2];
check 'gather', (gather for ^6 -> $i { my $x = f($i) // next; take $x }).list, (0, 2, 4);
sub sk($n) { my $t = 0; for ^$n -> $i { my $x = f($i) // next; $t += $x }; $t || Nil }
@o = (); for ^5 -> $n { my $x = sk($n) // next; @o.push: $x }
check 'a skipping loop inside the right side', @o, [2, 2];
@o = (); for ^4 -> $i { my $x = f($i) // do { @o.push("s$i"); next }; @o.push: $x }
check '// do { next }', @o, [0, 's1', 2, 's3'];
my @cl; for ^5 -> $i { my $x = f($i) // next; @cl.push: { $x } }
check 'closures', @cl.map({ $_() }).list, (0, 2, 4);

# The speed, as a ratio against the statement form in the same process (see
# next-return-in-expression-cooperative.raku): compiled, all of them are jumps.
sub best(&code) { (^3).map({ my $t = now; code(); now - $t }).min }
my $n = 20_000;
sub hh($i) { my $x = f($i) // return 0; $x }
my $stmt = best({ for ^$n -> $i { my $x = f($i); next without $x } });
my %expr =
    '// next'   => best({ for ^$n -> $i { my $x = f($i) // next } }),
    'or next'   => best({ for ^$n -> $i { f($i) or next } }),
    '// return' => best({ for ^$n -> $i { hh($i) } });
for %expr.sort -> (:key($k), :value($t)) {
    @fail.push("$k is {($t / $stmt).round(0.1)}x the statement form (bound 8x)") if $t > 8 * $stmt;
}

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
