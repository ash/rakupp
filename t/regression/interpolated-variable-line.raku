# A variable inside a string is reported at the line it is on: "Variable
# '$nope' is not declared" in a string past line 200 named line 1. The
# interpolation's sub-parser numbered its tokens from 1, and the report walked
# only 200 lines from there. Its tokens are numbered from the string's own
# line now, the newlines before them counted (a multi-line string's token
# records the line it ENDS on). `$?LINE` in a string gains the same: it was
# one line late after a newline in a string, and one early in a heredoc.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $dir = $*TMPDIR.add("interp-line-{$*PID}");
$dir.mkdir;
END { run 'rm', '-rf', ~$dir if $dir.e }
sub reported($body) {
    my $f = $dir.add('p.raku');
    $f.spurt(("#\n" x 300) ~ $body);
    my $p = run $*EXECUTABLE, '-c', $f, :out, :err;
    $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    ($err ~~ / ':' (\d+) \n / ?? +$0 !! 0)
}

ck reported(qq[say "\$nope";\n]), 301, '"$nope"';
ck reported(qq[say "a \{\$nope} b";\n]), 301, '"a {$nope} b"';
ck reported(qq[say qq/\$nope/;\n]), 301, 'qq/$nope/';
ck reported(qq[say "\$nope.uc()";\n]), 301, 'a method call';
ck reported(qq[say "first\nsecond \$nope";\n]), 302, 'a multi-line string';
ck reported(qq[say "first",\n   "second \$nope";\n]), 302, 'a multi-line statement';

my $lines = "say \"x\n\$?LINE\";\nsay \"a\nb\n\{\$?LINE}\";\nsay qq:to/E/;\n  h1\n  \$?LINE\n  E\n";
my $f = $dir.add('l.raku');
$f.spurt($lines);
my $p = run $*EXECUTABLE, $f, :out;
ck $p.out.slurp(:close).lines.grep(/^\s*\d+$/)».Int.List, (2, 5, 8), '$?LINE in strings and a heredoc';

say 'PASS';
