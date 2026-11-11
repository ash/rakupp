# Regression: in the REPL, a `my` a branch buries — the right side of a
# short-circuit that did not run, a statement modifier that did not fire —
# was gone by the next line: `my $x := True || (my $s := 5);` and then
# `$s := 9` died "Variable '$s' is not declared". The same lines in a file,
# or on one REPL line, worked. Each REPL line is an EVAL'd unit, which ran
# its statements without the declaration pass a file's mainline and a block
# get (issue #248, reported in the REPL after the file case was fixed).
#
# Contract: exit 0 + last line PASS.
my @fail;
%*ENV<RAKUPP_REPL> = '1';   # a session even with stdin on a pipe

sub repl(*@lines) {
    my $p = run($*EXECUTABLE, '-q', :in, :out, :err);
    $p.in.print(@lines.join("\n") ~ "\n");
    $p.in.close;
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $out.subst(/\e '[' <[0..9;]>* 'm'/, '', :g).subst('> ', '', :g).lines.List
}
sub check($desc, @lines, $want) {
    my $got = repl(|@lines).tail;
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

check '||', ('my $x := True || (my $s := 5);', '$s := 9;', 'say $s'), '9';
check '&&', ('my $x := False && (my $s := 5);', '$s := 9;', 'say $s'), '9';
check '//', ('my $x := 1 // (my $s := 5);', 'say $s.raku'), 'Any';
check 'or', ('my $x = True or (my $s = 5);', '$s = 7;', 'say $s'), '7';
check 'a modifier', ('my $a = "u" if False;', '$a ~= "host";', 'say $a'), 'host';
check 'a ternary', ('my $t = True ?? 1 !! (my $f = 2);', 'say $f.raku'), 'Any';
check 'a run branch keeps its value', ('my $y = False || (my $k = 5);', 'say $k'), '5';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
