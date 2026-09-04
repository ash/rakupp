# Regression: issue #66 — the REPL echoed a dim `(Any)` under every loop.
#
# Two bugs, one symptom.
#
# 1. A loop STATEMENT evaluated to an undefined `Any`. In Rakudo it is `Nil`,
#    and that is true wherever the value is visible — a sub whose last statement
#    is a loop, an EVAL ending in one — not only at the prompt. Values survive
#    only where the loop was written as an expression (`do for`, `(for …)`),
#    which the AST marks with asExpr and which must keep working.
#
# 2. The REPL echoed that value even when the statement had already printed. The
#    rule copied from Rakudo: echo the value UNLESS the statement wrote at least
#    one byte to stdout — `say 42` shows 42 and nothing else, `for 1..10 { say
#    $_ }` shows only the ten numbers. It is about the BYTES, so `print ""`
#    wrote nothing and its True is still echoed; and it is about stdout, so
#    `note` to stderr does not suppress anything.
#
# Contract: exit 0 + last line PASS.
my @fail;

# ---- 1. the value of a loop statement -------------------------------------
sub s-for    { for 1..3 { $_ * 2 } }
sub s-while  { while 0 { } }
sub s-loop   { loop (my $i = 0; $i < 2; $i++) { $i } }
sub s-repeat { repeat { } while 0 }

@fail.push("for -> {s-for().raku}")       unless s-for().raku    eq 'Nil';
@fail.push("while -> {s-while().raku}")   unless s-while().raku  eq 'Nil';
@fail.push("loop -> {s-loop().raku}")     unless s-loop().raku   eq 'Nil';
@fail.push("repeat -> {s-repeat().raku}") unless s-repeat().raku eq 'Nil';
@fail.push("EVAL -> {EVAL('for 1..3 { $_ }').raku}") unless EVAL('for 1..3 { $_ }').raku eq 'Nil';

# an EXPRESSION loop still collects — this is what asExpr is for
my @doubled = do for 1..3 { $_ * 2 };
@fail.push("do for -> {@doubled.raku}") unless @doubled.raku eq '[2, 4, 6]';
my @kept = (for 1..3 { $_ + 10 });
@fail.push("(for) -> {@kept.raku}") unless @kept.raku eq '[11, 12, 13]';

# ---- 2. the REPL echo rule -------------------------------------------------
# Drive a real session: RAKUPP_REPL=1 forces one with stdin on a pipe.
%*ENV<RAKUPP_REPL> = '1';

sub repl(*@lines) {
    my $p = run($*EXECUTABLE, '-q', :in, :out, :err);
    $p.in.print(@lines.join("\n") ~ "\n");
    $p.in.close;
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    # the prompt and the echo are coloured; the test is about the text
    $out.subst(/\e '[' <[0..9;]>* 'm'/, '', :g).subst('> ', '', :g)
}

sub check(Str $desc, Str $line, Str $want) {
    my $got = repl($line);
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eq $want;
}

# printed, so the value is a by-product and is not echoed
check('for + say',   'for 1..3 { say $_ }', "1\n2\n3\n");
check('say',         'say 42',              "42\n");
check('print',       'print "z"',           "z");
check('say then val','say 42; 1+1',         "42\n");

# nothing printed, so the value IS the answer
check('sum',         '1+1',                 "2\n");
check('bare for',    'for 1..3 { $_ * 2 }', "Nil\n");
check('bare while',  'while 0 { }',         "Nil\n");
check('Any',         'Any',                 "(Any)\n");

# zero bytes is not output: `print ""` still shows its True
check('empty print', 'print ""',            "True\n");

# stderr is not stdout: `note` writes on the other stream, so it suppresses
# nothing — the True is echoed and the message is nowhere in the stdout capture.
# (Asserted on stdout alone on purpose: `run(:in, :out, :err)` currently loses
# the stderr pipe, a separate bug that would make this case fail for the wrong
# reason.)
check('note', 'note "err"', "True\n");

# ---- verdict ---------------------------------------------------------------
if @fail {
    note "FAIL: $_" for @fail;
    say 'FAIL';
    exit 1;
}
say 'PASS';
