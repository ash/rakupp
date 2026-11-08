# A runtime error in a `{…}` of a multi-line string names the line the block
# is on, as Rakudo's does, where it named the statement's first line: a block
# of one expression is inlined into the string, and nothing moved the line
# for it. A plain `$x.nope()` interpolated on a later line still names the
# statement's line, as on Rakudo, and so does an ordinary multi-line
# statement.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub line-of($src) {
    my $f = $*TMPDIR.add("interp-block-line-{$*PID}.raku");
    $f.spurt($src);
    LEAVE $f.unlink;
    my $p = run $*EXECUTABLE, $f, :out, :err;
    $p.out.slurp(:close);
    my $e = $p.err.slurp(:close);
    $e ~~ / 'block <unit>' .*? ' line ' (\d+) / ?? +$0 !! $e
}

ck line-of(qq[my \$x = 1;\nsay "a\n  \{\$x.nope\}";\n]), 3, 'a method call in a block';
ck line-of(qq[say "a\n  b\n  \{die q[boom]\}";\n]), 3, 'a die, two lines on';
ck line-of(qq[my \$x = 1;\nsay qq:to/E/;\n  a\n  \{\$x.nope\}\n  E\n]), 4, 'a heredoc';
ck line-of(qq[my \$s = "a\n  \{die 1\} b";\n]), 2, 'an assignment';
ck line-of(qq[my \$x = 1;\nsay "a\n  \$x.nope()";\n]), 2, 'no block: the statement';
ck line-of(qq[say 1,\n  2.nope;\n]), 1, 'a multi-line statement';

say 'PASS';
