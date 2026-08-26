# Regression: an undeclared variable is a COMPILE-time error (issue #32).
#
# rakupp threw X::Undeclared only when execution reached the reference, so
#     my $x = 42; say $x; say $y; say "done";
# printed 42 and then died — where Rakudo refuses to compile the file at all.
# `-c` was worse: it answered "Syntax OK", and `--exe` handed the mistake to the
# C++ compiler, which reported `use of undeclared identifier 'v_sy'` against
# generated code the author never wrote.
#
# Now a whole-unit pass (src/DeclCheck.cpp) asks the question before anything
# runs, on the interpreter path, `-c`, `--cpp` and the three compile modes. It
# is deliberately one-sided: it stands down rather than risk refusing a program
# that works, so the second half of this file is all the shapes that must still
# be allowed through.
#
# Contract: exit 0 + last line PASS.
my @fail;

# The children inherit this process's environment MINUS the escape hatch, so
# the cases below measure what they say they measure even when a developer has
# RAKUPP_NO_DECLCHECK set in their shell — and so this file genuinely fails if
# the check ever stops firing.
sub rakupp(*@args, :%env = %(%*ENV.grep({ .key ne 'RAKUPP_NO_DECLCHECK' }))) {
    my $p = run($*EXECUTABLE, |@args, :out, :err, :%env);
    my %r = out => $p.out.slurp(:close), err => $p.err.slurp(:close);
    %r<exit> = $p.exitcode;
    %r;
}
sub refuses($label, *@args) {
    my %r = rakupp(|@args);
    @fail.push("$label: exit={%r<exit>} err={%r<err>.trim}")
        unless %r<exit> == 1 && %r<err>.contains('===SORRY!===')
                             && %r<err>.contains('is not declared');
    %r;
}
sub allows($label, *@args) {
    my %r = rakupp(|@args);
    @fail.push("$label: exit={%r<exit>} err={%r<err>.trim}")
        if %r<err>.contains('===SORRY!===');
    %r;
}

my $BAD = 'my $x = 42; say $x; say $y; say "done";';

# 1. the reported program: refused, and NOTHING of it ran
my %r1 = refuses('run', '-e', $BAD);
@fail.push("run printed {%r1<out>.perl} before refusing") unless %r1<out> eq '';
@fail.push('run names the variable') unless %r1<err>.contains(q{Variable '$y'});

# 2. -c no longer answers "Syntax OK" for it
my %r2 = refuses('-c', '-c', '-e', $BAD);
@fail.push('-c still says Syntax OK') if %r2<out>.contains('Syntax OK');

# 3. --cpp (and with it --exe/--aot/--bundle, which share the gate) refuses
#    instead of emitting C++ that will not compile
refuses('--cpp', '--cpp', '-e', $BAD);

# 4. the report points at the line and marks the variable in it
my $f = $*TMPDIR.add("rakupp-undeclared-{$*PID}.raku");
$f.spurt(qq:to/END/);
    my \$x = 42;
    say \$x;
    say \$y;
    END
my %r4 = refuses('file', $f.Str);
@fail.push("no location: {%r4<err>.trim}") unless %r4<err>.contains(':3');
@fail.push("no source quote: {%r4<err>.trim}") unless %r4<err>.contains('------>');
$f.unlink;

# 5. the escape hatch, for the day the pass is wrong about a working program
my %r5 = rakupp('-e', $BAD, env => %(%*ENV, RAKUPP_NO_DECLCHECK => '1'));
@fail.push('RAKUPP_NO_DECLCHECK did not disable the check')
    unless %r5<out>.starts-with('42');

# ---- everything below must still be allowed through --------------------

# a plain correct program
my %ok = allows('clean', '-e', 'my $x = 1; say $x');
@fail.push("clean program printed {%ok<out>.perl}") unless %ok<out>.trim eq '1';

# `no strict` is exactly the pragma that makes an undeclared variable legal
allows('no strict', '-e', 'no strict; $y = 5; say $y');

# EVAL and symbolic references can conjure names the pass cannot see, so it
# stands down entirely rather than guess
allows('EVAL', '-e', 'use MONKEY-SEE-NO-EVAL; EVAL q[my $q = 1]; say $y');
allows('::()', '-e', 'my $n = q[$z]; ::($n) = 7; say $z');

# a placeholder parameter IS the declaration of its bare name
allows('placeholder', '-e', 'my &f = { $^bb + $bb }; say f(2)');

# `loop (my $i = …)` builds no implicit block: $i outlives the loop
allows('loop init', '-e', 'loop (my $i = 0; $i < 2; $i++) { }; say $i');

# `our` is a package global, visible from a sibling scope
allows('our', '-e', '{ our @e1 = 1..3 }; say @e1[1]');

# an attribute written with no twigil is read by its bare name inside the class
allows('bare attribute', '-c', '-e', 'class K { has $x; method g { $x } }');

# a binder the parser does not keep (`repeat … -> $x`) must not read as
# undeclared just because the AST lost it
allows('repeat binder', '-c', '-e', 'my $n = 0; repeat until $n >= 1 -> $r { say $r.defined; $n++ }');

# a destructuring pointy signature on `with`, likewise
allows('with destructure', '-c', '-e', 'with (1,) -> (Int() $v is copy) { say $v }');

# ---- imports ------------------------------------------------------------
# A module may export a VARIABLE, so a name the unit does not declare can still
# be perfectly legal. The pass looks in the imported source before reporting —
# and has to match the WHOLE name there: matching a prefix let `$setting` in the
# module clear a bogus `$s` in the program, which cleared most short names for
# every program that imports anything.
my $dir = $*TMPDIR.add("rakupp-undeclared-lib-{$*PID}");
$dir.add('lib').mkdir(:p);
$dir.add('lib/Cfg.rakumod').spurt(qq:to/END/);
    unit module Cfg;
    our \$setting is export = 'on';
    END
my $prog = $dir.add('p.raku');

my $head = "use lib '{$dir.add('lib')}';\nuse Cfg;\n";
$prog.spurt($head ~ 'say $setting;');
my %i1 = allows('imported variable', $prog.Str);
@fail.push("imported variable printed {%i1<out>.perl}") unless %i1<out>.trim eq 'on';

$prog.spurt($head ~ 'say $s;');
refuses('prefix of an imported name', '-c', $prog.Str);

$prog.unlink;
$dir.add('lib/Cfg.rakumod').unlink;
$dir.add('lib').rmdir;
$dir.rmdir;

if @fail {
    note("FAILED: $_") for @fail;
    say 'FAIL';
    exit 1;
}
say 'PASS';
