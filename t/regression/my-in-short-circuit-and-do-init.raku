# Regression: a `my` is a compile-time declaration, but rakupp declared it
# only when its statement ran that far. In the right side of a
# short-circuiting `||`, `&&`, `//`, `or`, `and`, `andthen` or `orelse`
# that never ran, the variable did not exist and a later use died
# X::Undeclared — rak picks the first of several context options with
# `my $c := (my $a := …) || (my $b := …) || …` (issue #248). Inside a sub,
# `my $c := do { $c := 2 }` died the same way: the block that initializes
# the declaration names it before the bind declares it, which only worked
# at file scope (issue #249, App::Rak's --sourcery through Sourcery).
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

{ my $x := True  || (my $b := 5); $b := 9; check $b, 9, '||' }
{ my $x := False && (my $c := 5); $c := 9; check $c, 9, '&&' }
{ my $x := 1     // (my $d := 5); $d := 9; check $d, 9, '//' }
{ my $x := True  or (my $g := 5); $g := 9; check $g, 9, 'or' }
{ my $x := False and (my $h := 5); $h := 9; check $h, 9, 'and' }
{ my $x = True || (my $k = 5); check $k, Any, 'an unrun one is undefined' }
{ my $x = False || (my $k = 5); check $k, 5, 'a run one has its value' }
sub in-sub { my $x := True || (my $b := 5); $b := 9; $b }
check in-sub(), 9, 'in a sub';
class C { method m { my $x = True || (my $y = 5); $y = 6; $y } }
check C.m, 6, 'in a method';
sub arr { my $x = True || (my @l = 1, 2); @l.push(3); @l.List }
check arr(), (3,), 'an array';
sub hsh { my $x = 0 || (my %h = a => 1); %h }
check hsh(), {a => 1}, 'a hash';
sub ctx(%n) {
    my $ctx := (my $pt := %n<passthru>:delete) || (my $pc := %n<context>:delete) || 0;
    ($ctx, $pt, $pc)
}
check ctx({context => 3}), (3, Any, 3), 'the rak shape, the second option';
check ctx({passthru => 1}).head(2).List, (1, 1), 'the rak shape, the first option';
sub at   { my $v = 5 andthen (my $z = 7); $z }
check at(), 7, 'andthen declares where it stands, not in its topic scope';
sub oe   { my $v = Any orelse (my $z = 3); $z }
check oe(), 3, 'orelse too';
{ my $x = 5; $x andthen $_++; check $x, 6, 'andthen still aliases its topic' }

sub f() { my $c := do { $c := 2 }; $c }
check f(), 2, 'my $c := do { $c := 2 } in a sub';
sub f2($a) { my $c := do if $a.defined { 1 } else { $c := 2 }; $c }
check f2(Any), 2, '…through do if / else';
check f2(1), 1, '…and the branch that does not name it';
sub f3() { my $c = do { $c = 2 }; $c }
check f3(), 2, 'with = as well';
sub f4() { my ($a, &f) = do { (1, sub { 2 }) }; $a + f() }
check f4(), 3, 'a list declaration with a Callable from do (roast S32-list/skip.t)';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
