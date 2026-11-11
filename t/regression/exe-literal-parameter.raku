# --exe: a LITERAL parameter outside a multi candidate — `-> 'greet', $name`,
# `sub g('x', $y)` — is part of the routine's signature and its binder.
# Compiled, the binder ignored it (`g('z', 1)` ran the body) and the signature
# reported an untyped `$` with no constraint, so Cro, which builds its route
# matchers from the signature, never matched `get -> 'greet', $name` and
# answered 404. Such a routine now stays interpreted; a multi candidate's
# literal is still checked natively by its dispatcher. Checked both ways by
# t/exe/parity.raku.
# Contract: exit 0 + last line PASS.

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

my &b = -> 'greet', $name { "Hello, $name!" };
my @p = &b.signature.params;
check @p[0].type.^name, 'Str', 'the literal types its parameter';
check @p[0].constraint_list, ('greet',), '…and constrains it';
check b('greet', 'A'), 'Hello, A!', 'the literal matches';
check (try { b('x', 'A') } // $!.^name), 'X::TypeCheck::Binding::Parameter', 'another value is refused';

sub g('x', $y) { "g$y" }
check g('x', 1), 'g1', 'a sub with a literal';
check (try { g('z', 1) } // $!.^name), 'X::TypeCheck::Binding::Parameter', '…refuses another value';

multi f(0) { 'zero' }
multi f($n) { 'n' }
check (f(0), f(1)), ('zero', 'n'), 'a multi candidate still dispatches on its literal';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
