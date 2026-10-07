# Regression: a call that can never bind is a COMPILE-time error, as on Rakudo.
#
# Raku Koans (08-subroutines/02-signature, 05-slurpy-and-named-parameters)
# teach `dies-ok { say-hello(42) }` against `sub say-hello(Str $name)`, and
# `dies-ok { needs() }` against `sub needs(:$name!)`. Rakudo refuses both files
# before running a line: "Calling say-hello(Int) will never work with declared
# signature (Str $name)". rakupp ran them, the binder refused the call inside
# `dies-ok`, and the test PASSED — so did `try f(42)` around any such call.
#
# Now a whole-unit pass (src/CallCheck.cpp) asks Rakudo's optimizer's question
# before anything runs: the call names a sub the unit declares, every argument
# is positional and of a type the compiler knows (a literal, a type object, a
# variable declared with a nominal type), and the signature cannot take them —
# by Rakudo's trial binder, or its compile-time multi dispatch. Rakudo leaves a
# great many shapes to run time, and so must this: the second half of the file
# is the shapes that have to be let through.
#
# Contract: exit 0 + last line PASS.
my @fail;

# The children inherit this process's environment MINUS the escape hatch, so a
# developer with RAKUPP_NO_CALLCHECK set still measures the check.
sub child(*@args, :%env = %(%*ENV.grep({ .key ne 'RAKUPP_NO_CALLCHECK' }))) {
    my $p = run($*EXECUTABLE, |@args, :out, :err, :%env);
    my %r = out => $p.out.slurp(:close), err => $p.err.slurp(:close);
    %r<exit> = $p.exitcode;
    %r;
}
# Refused before anything ran, with this message.
sub refuses($label, $code, $message) {
    my %r = child('-e', $code);
    @fail.push("$label: exit={%r<exit>} out={%r<out>.raku} err={%r<err>.trim}")
        unless %r<exit> == 1 && %r<out> eq '' && %r<err>.contains('===SORRY!===')
               && %r<err>.contains($message);
    %r;
}
# Compiled and ran to the end (each case prints its own marker last).
sub allows($label, $code, $want) {
    my %r = child('-e', $code);
    @fail.push("$label: exit={%r<exit>} out={%r<out>.raku} err={%r<err>.trim}")
        unless %r<exit> == 0 && %r<out>.lines.tail eq $want;
    %r;
}

# ---- refused ---------------------------------------------------------------

# 1. the koans' two shapes, bare and inside `try`
refuses 'typed param, literal arg',
    'say "start"; sub f(Str $x) { }; try f(42); say "done";',
    'Calling f(Int) will never work with declared signature (Str $x)';
refuses 'missing required named',
    'say "start"; sub needs(:$name!) { }; try needs(); say "done";',
    'Calling needs() will never work with declared signature (:$name!)';
refuses 'inside dies-ok',
    'use Test; plan 1; sub greet(Str $name) { "hi $name" }; dies-ok { greet(42) }, "typed";',
    'Calling greet(Int) will never work with declared signature (Str $name)';

# 2. counts, the other literal types, and the sub declared further down —
#    in a routine that is never even called
refuses 'too many', 'sub f($x) { }; f(1, 2);',
    'Calling f(Int, Int) will never work with declared signature ($x)';
refuses 'post-declared, never called', 'sub g { f(1.5) }; sub f(Int $x) { }',
    'Calling f(Rat) will never work with declared signature (Int $x)';
refuses 'a Num literal', 'sub f(Int $x) { }; f(1e0);',
    'Calling f(Num) will never work with declared signature (Int $x)';
refuses 'a bare name is a call', 'sub f($x) { }; f;',
    'Calling f() will never work with declared signature ($x)';

# 3. typed variables, and an array where a Str is wanted
refuses 'typed variable', 'sub f(Str $x) { }; my Int $n = 1; f($n);',
    'Calling f(Int) will never work with declared signature (Str $x)';
refuses 'typed parameter', 'sub f(Str $x) { }; sub g(Int $n) { f($n) }',
    'Calling f(Int) will never work with declared signature (Str $x)';
refuses 'an array', 'sub f(Str $x) { }; my @a; f(@a);',
    'Calling f(Positional) will never work with declared signature (Str $x)';

# 4. a subset is checked by its base type; natives by a lone literal's kind
refuses 'subset param', 'subset Pos of Int where * > 0; sub f(Pos $x) { }; f("x");',
    'Calling f(Str) will never work with declared signature (Int $x where { ... })';
refuses 'native param', 'sub f(int $x) { }; f("x");',
    'Calling f(Str) will never work with declared signature (int $x)';

# 5. multis and protos
refuses 'multi', 'multi m(Int $x) { }; multi m(Str $x) { }; m(1.5);',
    "Calling m(Rat) will never work with any of these multi signatures:\n    (Int \$x)\n    (Str \$x)";
refuses 'proto', 'proto p(Int $) {*}; multi p($x) { }; p("x");',
    'Calling p(Str) will never work with signature of the proto (Int)';
refuses 'a literal is no container', 'multi f(Str $x is rw) { }; f("x");',
    'Calling f(Str) will never work with any of these multi signatures';

# 6. EVAL'd code is compiled the same way: nothing of it runs
{
    sub run-it { EVAL q[sub g(Str $x) { }; say "side effect"; g(42)] }
    try run-it();
    @fail.push("EVAL: {$!.^name}") unless $! ~~ X::TypeCheck::Argument;
    @fail.push("EVAL message: {$!.message}")
        unless $!.message eq 'Calling g(Int) will never work with declared signature (Str $x)';
    @fail.push("EVAL objname: {$!.objname}") unless $!.objname eq 'g';
}
{
    my %r = child('-e', 'EVAL q[sub g(Str $x) { }; say "side effect"; g(42)]');
    @fail.push("EVAL ran its statement first: {%r<out>.raku}") if %r<out>.contains('side effect');
}

# ---- allowed through ---------------------------------------------------------

# what Rakudo's optimizer does not know the type of
allows 'a word list', 'sub f(Int $x) { }; try f(<x>); say "ok"', 'ok';
allows 'a negative literal is a call', 'sub f(Str $x) { }; try f(-1); say "ok"', 'ok';
allows 'an untyped variable', 'sub f(Str $x) { }; my $v = 1; try f($v); say "ok"', 'ok';
allows 'a subset-typed variable', 'subset P of Int; sub f(Str $x) { }; my P $p = 1; try f($p); say "ok"', 'ok';
allows 'a dynamic variable', 'sub f(Str $x) { }; my Int $*d = 1; try f($*d); say "ok"', 'ok';
allows 'True', 'sub f(Str $x) { }; try f(True); say "ok"', 'ok';
allows 'a named argument', 'sub f(Str $x) { }; try f(42, :foo); say "ok"', 'ok';

# parameters the trial binder does not analyse
allows 'a literal default', 'sub f(Str $x = "a") { }; try f(42); say "ok"', 'ok';
allows 'a smiley', 'sub f(Int:D $x) { }; try f(Int); say "ok"', 'ok';
allows 'a coercion', 'sub f(Int() $x) { say $x + 1 }; f("3")', '4';
allows 'a & parameter', 'sub f(&c) { }; try f(42); say "ok"', 'ok';
allows 'a named parameter takes a positional', 'sub f(:$x) { }; try f(1); say "ok"', 'ok';

# calls that are not to a sub of the unit's, as Rakudo sees them
allows 'a method', 'class A { method m(Str $x) { } }; try A.m(42); say "ok"', 'ok';
allows 'through a variable', 'sub f(Str $x) { }; my &g = &f; try g(42); say "ok"', 'ok';
allows 'a multi beside the setting\'s', 'multi say(Int $x, Int $y) { print "two\n" }; say("x"); say(1, 2)', 'two';
allows 'an inner sub shadows', 'sub f(Str $x) { say "outer" }; { sub f(Int $x) { say "inner" }; f(42) }', 'inner';

# a wider candidate behind a narrower one: Rakudo stops at the first tier
allows 'two tiers', 'multi f(Int $x) { }; multi f(Numeric $x) { }; try f("x"); say "ok"', 'ok';

# a Junction that does not fit may auto-thread instead, so it is never ruled
# out here (roast S03-junctions/misc.t wants the binder's own exceptions)
allows 'a Junction', 'sub foo($) { }; try foo(Junction); say "ok"', 'ok';
allows 'a Junction, multi', 'multi foo($) { }; try foo(Junction); say "ok"', 'ok';

# ---- rakupp's own surface ------------------------------------------------------

if $*RAKU.compiler.name eq 'Raku++' {
    my $code = 'say "start"; sub f(Str $x) { }; try f(42); say "done";';

    # -c reports it instead of "Syntax OK", and points at the call
    my %c = child('-c', '-e', $code);
    @fail.push("-c: exit={%c<exit>} out={%c<out>.trim}")
        unless %c<exit> == 1 && !%c<out>.contains('Syntax OK') && %c<err>.contains('will never work');
    @fail.push("-c points at: {%c<err>.trim}") unless %c<err>.contains("try \x[23CF]f(42)");

    # --lint makes it an error
    my %l = child('--lint', '-e', $code);
    @fail.push("--lint: exit={%l<exit>} out={%l<out>.trim}")
        unless %l<exit> == 2 && %l<out>.contains('[will-never-work]');

    # the escape hatch, for the day the pass is wrong about a working program
    my %e = child('-e', $code, env => %(%*ENV, RAKUPP_NO_CALLCHECK => '1'));
    @fail.push("RAKUPP_NO_CALLCHECK: {%e<out>.raku}") unless %e<out> eq "start\ndone\n";

    # Rakudo also refuses this one — its optimizer types `my @a` as Positional,
    # which is not Iterable — but an Array IS Iterable and the call binds at
    # run time, so rakupp lets it run (CallCheck.h: "truly can never bind").
    allows 'an Array is Iterable', 'sub f(Iterable $x) { say "ok" }; my @a; f(@a)', 'ok';
}

if @fail {
    note("FAILED: $_") for @fail;
    say 'FAIL';
    exit 1;
}
say 'PASS';
