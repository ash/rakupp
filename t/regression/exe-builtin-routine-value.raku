# --exe: a built-in routine as a value (`&say`, `&trait_mod:<is>`) is the
# engine's one Callable for it, as the interpreter's is. The compiled program
# handed out an anonymous closure forwarding to the built-in by name, so
# `&say.name` was "", `&say === &say` False, and `&trait_mod:<is>.candidates`
# had one candidate (Test::Assertion counts them; core-trait-candidates-test-
# assertion.raku holds that). Found by t/exe/parity.raku.
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

check &say.name, 'say', '&say.name';
check &sqrt.name, 'sqrt', '&sqrt.name';
check (&say === &say), True, 'one Callable';
my &s = &sqrt;
check (1, 4, 9).map(&s).List, (1e0, 2e0, 3e0), 'and it still calls';
check &trait_mod:<is>.name, 'trait_mod:<is>', '&trait_mod:<is> by name';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
