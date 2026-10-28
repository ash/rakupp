# Regression (#195): `next`, `last`, `redo` and `return` written as an operand —
# `my $x = f($i) // next`, `f($i) or next`, `my $x = f($i) // return 0` — take
# the cooperative form (a flag the loop or routine reads after the statement)
# instead of throwing a C++ exception through the interpreter's frames, which
# cost 30-65 µs a jump on macOS against ~1 µs for the flag. Three things were
# in the way:
#   - a `next` operand went cooperative only when it WAS the statement, so
#     `f($i) or next` threw although nothing after it uses its value;
#   - a call replaced the statement the walk starts from and never put it
#     back, so after `f($i)` returned even a `return` on the path threw;
#   - an assignment's store would take the Any the flag form hands back (the
#     typed `my Str $s = %h{$_} // next` died of it), so it threw too. The
#     general store path now guards the store and skips it once a flag is up.
# The walk also stopped treating a LEFT operand as on the path:
# `(return 5) or say 'x'` ran the `say` (Rakudo returns 5 and prints nothing).
# Every answer below is Rakudo 2026.09's.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

sub f($i) { $i %% 2 ?? $i !! Nil }

my @o;
for ^5 -> $i { my $x = f($i) // next; @o.push: $x }
check 'my $x = … // next', @o, [0, 2, 4];

# a typed declaration: the skipped store must not see the Any
my %h = a => 'A', c => 'C';
@o = (); for <a b c> { my Str $s = %h{$_} // next; @o.push: $s }
check 'my Str $s = %h{} // next', @o, ['A', 'C'];

# an outer variable keeps its last value; an element is never stored
my $y = 'init'; @o = ();
for ^4 -> $i { $y = f($i) // next; @o.push: $y }
check '$outer = … // next', ($y, @o), (2, [0, 2]);
my Int $ti = 7; for ^3 -> $i { $ti = f($i) // next }
check 'typed outer', $ti, 2;
my %e; for ^4 -> $i { %e{$i} = f($i) // next }
check '%e{$i} = … // next', %e.keys.sort.list, ('0', '2');
my @ea; for ^4 -> $i { @ea[$i] = f($i) // next }
check '@ea[$i] = … // next', @ea.raku, '[0, Any, 2]';
@o = (); for ^4 -> $i { my @a = f($i) // next; @o.push: @a.elems }
check 'my @a = … // next', @o, [1, 1];

# a left operand is not on the path
my @side;
sub r { (return 5) or @side.push('x') }
check '(return 5) or …', (r(), @side), (5, []);
@o = (); for ^2 { (next) or @o.push('y'); @o.push('z') }
check '(next) or …', @o, [];

my $c = 0; for ^6 -> $i { f($i) or next; $c++ }
check 'f() or next', $c, 2;   # f(0) is 0, which is false
class C { method m($i) { $i > 1 ?? "m$i" !! Nil } }
@o = (); for ^4 -> $i { my $x = C.m($i) // next; @o.push: $x }
check 'my $x = C.m() // next', @o, ['m2', 'm3'];

@o = (); for ^6 -> $i { my $x = ($i < 3 ?? $i !! Nil) // last; @o.push: $x }
check '// last', @o, [0, 1, 2];
my $tries = 0; @o = ();
for ^3 -> $i { $tries++; my $x = ($tries == 2 ?? Nil !! $i) // redo; @o.push: $x }
check '// redo', ($tries, @o), (4, [0, 1, 2]);
@o = (); for ^5 -> $i { my $x = $i %% 2 ?? $i !! next; @o.push: $x }
check 'ternary branch', @o, [0, 2, 4];
@o = (); for ^6 -> $i { my $x = $i == 0 ?? "z" !! ($i == 1 ?? (next) !! (f($i) // f($i + 1) // next)); @o.push: $x }
check 'nested ternary and chain', @o, ['z', 2, 4, 4, 6];
@o = (); for ^4 -> $i { my $x = f($i) || next; @o.push: $x }
check '|| next', @o, [2];
@o = (); for ^4 -> $i { my $x = (f($i) andthen $_ + 100) // next; @o.push: $x }
check 'andthen … // next', @o, [100, 102];

sub h($i) { my $x = f($i) // return 0; $x * 10 }
check '// return', (^5).map(&h).list, (0, 0, 20, 0, 40);
# the same assignment node, one activation inside another
sub g($n) { my $x = ($n > 0 ?? g($n - 1) !! Nil) // return "base"; "g($x)" }
check '// return, recursive', g(3), 'g(g(g(base)))';
sub lr { for ^5 -> $i { my $x = ($i < 3 ?? $i !! Nil) // return "ret$i" }; 'end' }
check '// return from a loop', lr(), 'ret3';
sub nv($i) { my $r = (my $x = f($i) // return 'skip'); "v$r" }
check 'an assignment as a value', (^3).map(&nv).list, ('v0', 'skip', 'v2');

# the loop kinds, blocks between, phasers, CATCH, gather
my $i = 0; @o = (); while $i < 5 { $i++; my $x = f($i) // next; @o.push: $x }
check 'while', @o, [2, 4];
@o = (); loop (my $j = 0; $j < 5; $j++) { my $x = f($j) // next; @o.push: $x }
check 'loop', @o, [0, 2, 4];
$i = 0; @o = (); repeat { $i++; my $x = f($i) // next; @o.push: $x } while $i < 5;
check 'repeat', @o, [2, 4];
@o = (); for ^5 -> $i { if True { if True { my $x = f($i) // next; @o.push: $x } } }
check 'two blocks between', @o, [0, 2, 4];
my $lv; @o = ();
for ^3 -> $i { LEAVE { @o.push: "L$i:" ~ ($lv // 'Any') }; $lv = f($i) // next; @o.push: "B$lv" }
check 'LEAVE', @o, ['B0', 'L0:0', 'L1:0', 'B2', 'L2:2'];
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

# The speed: the expression forms against the statement form, in one process.
# A ratio, so the machine's speed cancels; set far above the cooperative path's
# ~1-2x, far below the exception's 30-60x on macOS (a cheaper unwinder may not
# reach it, which is a pass, not a flake).
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
