# Regression: what a `try` block's `use fatal` reaches, in a program compiled
# with --exe. The binary made a `fail` die in ANY routine called while a
# compiled `try` ran (a dynamic counter), where the interpreter and Rakudo
# make the CALL SITES written inside the try block throw the Failure they get
# back — not in a boolean or definedness context. So
# `try { rgfail() // 'caught' }` answered Any compiled, not 'caught', and a
# Failure built as an argument (`try f(Failure.new("x"))`) blew up before the
# binder could refuse it. A `fail` written inside the try block still dies.
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

sub q($n) { fail "neg" if $n < 0; $n }
ck (try { q(-3) // 'c' }), 'c', 'a call under `//` in a try block keeps its Failure';
my $b = try { my $x = q(-1); 7 };
ck $b, Any, 'an assigned call result throws inside the try block';
ck $!.message, 'neg', '…and the try catches it';
sub v { try { fail "inner" }; $!.message }
ck v(), 'inner', 'a fail written inside the try block dies';
sub w { try { my $y = q(-2) // 'dflt'; $y } }
ck w(), 'dflt', 'a try block inside a routine';
sub f(Int $x) { }
ck ((try f(Failure.new("x"))) // $!.^name), 'X::TypeCheck::Binding::Parameter',
   'a blockless try leaves an argument Failure to the binder';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
