# Regression: `&?ROUTINE` in a program compiled with --exe. The C++ backend
# emitted it as a call to a builtin named "?ROUTINE", so `&?ROUTINE.name` was ""
# and `&?ROUTINE($n - 1)` died "Undefined routine '?ROUTINE'". A named sub now
# gets its own Code value (the one `&name` gives); every other place — a method,
# a multi candidate, an anonymous or lexical sub, the mainline — is refused, and
# the program bundles the interpreter. A plain block inside a sub is not a
# routine: `&?ROUTINE` there is still the sub around it.
#
# t/exe/run.raku compiles this file and compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

sub named-self { &?ROUTINE.name }
sub fact($n) { $n <= 1 ?? 1 !! $n * &?ROUTINE($n - 1) }
sub in-block { for 1 { return &?ROUTINE.name } }
sub as-value { my &me = &?ROUTINE; &me.name }

ck named-self(), 'named-self', '&?ROUTINE.name in a named sub';
ck fact(6),      720,          '&?ROUTINE(...) recursion';
ck in-block(),   'in-block',   '&?ROUTINE inside a block is the enclosing sub';
ck as-value(),   'as-value',   '&?ROUTINE stored in a variable';

if @fail { .say for @fail; say 'FAIL' }
else     { say 'PASS' }
