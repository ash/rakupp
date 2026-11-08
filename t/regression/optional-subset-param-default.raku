# An optional parameter typed by a subset and not passed holds the subset's
# nominal type object, and the subset has to accept it, as Rakudo's binder
# checks: `sub f(CN :$x)` with CN `of Str where /^\w+$/` bound CN's own type
# object unchecked; Rakudo refuses the implicit Str. And `CN ~~ CN` is True.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my subset CN of Str where { .defined && /^ \w+ $/ };
sub f(CN :$x) { $x }
try f();
ck $!.^name, 'X::TypeCheck::Binding::Parameter', 'an unpassed CN :$x';
ck $!.message.lines[0], "Constraint type check failed in binding to parameter '\$x'; expected CN but got Str (Str)",
   '…the message';
ck $!.message.lines[1], 'The parameter is optional and was not passed an argument, so its', '…and its explanation';
ck CN ~~ CN, True, 'CN ~~ CN';
ck Str ~~ CN, False, 'Str ~~ CN';

subset M of Str where { !.defined || .chars };
sub g(M :$x) { $x.raku }
ck g(), 'Str', 'a subset that takes the type object binds the nominal type';
sub g2(M $y?) { $y.raku }
ck g2(), 'Str', 'positional too';
subset N of M where { True };
sub h(N :$z) { $z.raku }
ck h(), 'Str', 'through two subsets';
subset P where { True };
sub k(P :$w) { $w.raku }
ck k(), 'Any', 'of nothing: Any';
subset Q of Int:D where * > 0;
sub q(Q :$v) { $v }
try q();
ck $!.message.lines[0], "Constraint type check failed in binding to parameter '\$v'; expected Q but got Int (Int)",
   'a definite base refuses the type object';
sub u(UInt :$u) { $u.raku }
ck u(), 'Int', 'UInt is Int';
sub d(CN :$x = 'ab') { $x }
ck d(), 'ab', 'a default (as before)';
ck f(:x<a1>), 'a1', 'passed (as before)';

say 'PASS';
