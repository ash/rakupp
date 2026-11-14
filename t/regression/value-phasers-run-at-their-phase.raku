# Regression (RAKUDO-ISSUES-PLAN R2; rakudo#4351 and the 23 issues that time
# themselves with `now - ENTER now` or `now - INIT now`): a phaser in TERM
# position is its value as computed at ITS phase, not where the expression
# stands.
#
# Before the fix:
#   - `say "body"; my $x = INIT say "init"` said body first, as did BEGIN,
#     CHECK and ENTER, in block and expression form;
#   - `say now - INIT now` printed about -0.00003;
#   - `CHECK <expr>` and `FIRST <expr>` were "Undefined routine";
#   - `FIRST { … }` in a loop ran on every iteration;
#   - an INIT parameter default ran on every call.
#
# Each case is a program of its own, since a top-level phaser runs before
# that program's mainline: the ORDER of its output is what is tested.
# Contract: exit 0 + last line PASS.
my @fail;
sub prints(Str $code, Str $want) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    my $got = $p.out.slurp(:close);
    $p.err.slurp(:close);
    @fail.push("{$code.raku}: got {$got.raku}, want {$want.raku}") unless $got eq $want;
}

# BEGIN, CHECK and INIT run before the code around them, at the top level too
prints q[say "body"; my $x = INIT say "init"; say "end"], "init\nbody\nend\n";
prints q[say "body"; my $x = INIT { say "init"; 1 }; say "end $x"], "init\nbody\nend 1\n";
prints q[say "body"; my $x = BEGIN say "begin"; say "end"], "begin\nbody\nend\n";
prints q[say "body"; my $x = BEGIN { say "begin"; 2 }; say "end $x"], "begin\nbody\nend 2\n";
prints q[say "body"; my $x = CHECK say "check"; say "end"], "check\nbody\nend\n";
prints q[say "body"; my $x = CHECK { say "check"; 3 }; say "end $x"], "check\nbody\nend 3\n";
prints q[{ say "body"; my $x = BEGIN say "begin"; say "end" }], "begin\nbody\nend\n";
prints q[sleep 0.05; say (now - INIT now) > 0], "True\n";
# …once: in a loop and in a routine called twice
prints q[my $n = 0; for ^3 { $n += (BEGIN { print "B "; 10 }) }; say $n], "B 30\n";
prints q[sub f { (BEGIN [1,2,3])[1] }; say f(), f()], "22\n";
# an INIT default runs at program start, before any call has bound $x
prints q[sub g($x, $y = INIT { $x }) { $y }; say g(1), g(2)], "(Any)(Any)\n";
prints q[sub g(Int $x, $y = INIT { $x }) { $y }; say g(1)], "(Int)\n";
prints q[sub g($y = INIT { 42 }) { $y }; say g(5); say g()], "5\n42\n";
prints q[sub g($x, $y = BEGIN { 42 }) { $y }; say g(1), g(2)], "4242\n";

# ENTER runs at its block's entry, every entry
prints q[sub f { say "in f"; my $v = ENTER say "enter"; say "v=$v" }; f()], "enter\nin f\nv=True\n";
prints q[sub f { say "in f"; my $v = ENTER { say "enter"; 7 }; say "v=$v" }; f()], "enter\nin f\nv=7\n";
prints q[my $s = 0; for ^3 { $s += ENTER $_ }; say $s], "3\n";
prints q[sub r($n) { my $e = ENTER $n; $n ?? r($n - 1) ~ $e !! "0" }; say r(3)], "0123\n";
prints q[{ for ^10000 { }; say (now - ENTER now) > 0 }], "True\n";

# …and at the top level, before the mainline: FIRSTs, then ENTERs, each in
# source order, whatever their form (rakudo#4504 times a `.race` this way)
prints q[say "body"; my $x = ENTER say "enter"; say "end"], "enter\nbody\nend\n";
prints q[sleep 0.05; say (now - ENTER now) > 0], "True\n";
prints q[say "a"; ENTER say "s"; my $x = ENTER say "v"; FIRST say "f"; say "b"], "f\ns\nv\na\nb\n";
prints q[say "a"; my $x = FIRST say "f"; say "b"], "f\na\nb\n";
# …and in EVAL'd code, whose ENTERs ran in place in either form
prints q[EVAL q{say "a"; ENTER say "e"; my $x = ENTER say "v"; say "b"}], "e\nv\na\nb\n";
prints q[say EVAL q{1; ENTER 7}], "7\n";

# FIRST runs once, at a loop's first iteration, and every iteration reads it
prints q[say FIRST 2], "2\n";
prints q[for ^3 { say FIRST 10 + $_ }], "10\n10\n10\n";
prints q[for ^3 { my $v = FIRST { $_ * 10 }; say $v }], "0\n0\n0\n";
prints q[for ^2 { for ^2 { print FIRST "a$_ " } }; say ""], "a0 a0 a0 a0 \n";
prints q[my @r = (^3).map({ FIRST $_ * 100 }); say @r], "[0 0 0]\n";

# In this process too, so that the --exe parity gate compares a compiled
# build (a value phaser bundles it with the interpreter) with this run.
my $top = now - ENTER now;   # (ENTER ran before the cases above)
sub took { sleep 0.01; now - ENTER now }
my @firsts; for ^3 { @firsts.push: FIRST $_ * 10 }
@fail.push("in process: now - ENTER now is $top") unless $top > 0;
@fail.push("in process: a sub's now - ENTER now is {took()}") unless took() > 0;
@fail.push("in process: FIRST gave {@firsts.raku}") unless @firsts eqv [0, 0, 0];

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
