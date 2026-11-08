# A `let` is undone when its routine is left by an exception, even when a
# LEAVE phaser dies during the unwinding: `sub f { let $x = 5; LEAVE die "L";
# die "first" }` kept $x at 5, because the LEAVE's error threw past the
# restore. A routine that returns normally keeps the `let`, a dying LEAVE or
# not, as Rakudo does. For a method as well.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

our $x = 1;
sub f { let $x = 5; LEAVE die 'from LEAVE'; die 'first' }
try f();
ck $x, 1, 'a sub';
ck $!.message, 'from LEAVE', 'the LEAVE\'s error is the one seen';
class C { method m { let $x = 7; LEAVE die 'L'; die 'f' } }
try C.new.m;
ck $x, 1, 'a method';
sub g { let $x = 9; LEAVE die 'x'; 42 }
try g();
ck $x, 9, 'a normal return keeps it';
$x = 1;
sub h { let $x = 3; die 'plain' }
try h();
ck $x, 1, 'no LEAVE (as before)';

say 'PASS';
