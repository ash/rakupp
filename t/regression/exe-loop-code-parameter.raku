# Regression: a `&`-sigiled pointy loop variable in a program compiled with
# --exe. `for &a, &b -> &f { f("x") }` called `f` by name, found no routine and
# died "Undefined routine 'f'": only `my &f` and a sub's `&f` parameter were
# known to be callable through the variable.
#
# t/exe/run.raku compiles this file and compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

sub a($x) { "a$x" }
sub b($x) { "b$x" }
my @r;
for &a, &b -> &f { @r.push: f("x") }
ck @r.join(','), 'ax,bx', 'for LIST -> &f { f(…) }';

my @s;
for &abs, &sqrt -> &g { @s.push: g(16) }
ck @s.join(','), '16,4', 'built-ins through a loop &-variable';

my @t;
for (&a, 1), (&b, 2) -> (&h, $n) { @t.push: h($n) }
ck @t.join(','), 'a1,b2', 'a &-name in a destructuring loop signature';

sub outer { my @o; for &a -> &f { @o.push: f(1) }; @o.join }
ck outer(), 'a1', 'inside a sub';

if @fail { .say for @fail; say 'FAIL' }
else     { say 'PASS' }
