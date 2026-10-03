# Regression: `state` variables in a program compiled with --exe.
#
# The native backend had no `state`: `state $n = 0` became a fresh C++ local
# on every pass (`for ^2 { state $n = 0; say $n++ }` printed 0 0), and an
# expression-position declaration — `(state $n)++`, or the anonymous `$` in
# `@a[$++]`, which Terminal::UI writes as `@$heights[@changed[$++]]++` —
# named a local nothing declared, so the C++ compile failed and no binary was
# made. A `state` declaration is now refused, which bundles the interpreter.
#
# t/exe/run.raku compiles this file and compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

my @a;
for ^3 { state $n = 0; @a.push: $n++ }
ck @a, [0, 1, 2], 'state $n = 0 in a loop body';

my @b;
for ^3 { @b.push: (state $m)++ }
ck @b, [0, 1, 2], '(state $m)++ in an expression';

my @c = 10, 20, 30;
my @d;
@d.push: @c[$++] for ^3;
ck @d, [10, 20, 30], '@a[$++] under a statement-modifier for';

my $hs = [0, 0, 0];
my @changed = 1, 2;
@$hs[@changed[$++]]++ for ^@changed;
ck $hs, [0, 1, 1], 'the Terminal::UI shape';

sub counter { state $calls = 0; ++$calls }
counter() for ^4;
ck counter(), 5, 'a state variable in a sub keeps its value across calls';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
