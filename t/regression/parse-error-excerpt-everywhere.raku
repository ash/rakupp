# A parse error shows the line it names, with a `^` under where the parse
# stopped, under `-e`, `-c` and `--lint` as for a file run: `-e` showed the
# header and `at -e:1` with no line (its source was seeded only after a
# successful parse), and `-c` and `--lint` the header alone. Most errors name
# a column now, the start of the token the parse stopped on or the end of the
# one before when the source ran out (`say 1 +`). This is rakupp's own form
# (Rakudo writes the position as `say 1 +<HERE>`), so under another compiler
# the file checks nothing.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub err(*@args) {
    my $p = run $*EXECUTABLE, |@args, :out, :err;
    $p.out.slurp(:close);
    $p.err.slurp(:close).lines.grep(/'|'/).map(*.trim-trailing).List
}

unless $*RAKU.compiler.name eq 'Raku++' { say 'PASS'; exit }
my $f = $*TMPDIR.add("parse-excerpt-{$*PID}.raku");
$f.spurt("my \$x = 1;\nsay \$x +;\n");
LEAVE { $f.unlink if $f.e }
my @want = '      2 | say $x +;', '        |         ^';
ck err($f), @want.List, 'a file run';
ck err('-c', $f), @want.List, '-c';
ck err('--lint', $f), @want.List, '--lint';
ck err('-e', "my \$x = 1;\nsay \$x +;"), @want.List, '-e';
ck err('-e', 'say 1 +'), ('      1 | say 1 +', '        |        ^'), 'the source ran out';
ck err('-c', '-e', 'say (1, 2'), ('      1 | say (1, 2', '        |          ^'), '-c -e';
ck err('-e', 'if 1 { } say 2'), ('      1 | if 1 { } say 2', '        |          ^'), 'text after a block';
my $p = run $*EXECUTABLE, '-c', '-e', 'say 1', :out, :err;
ck $p.out.slurp(:close).trim, 'Syntax OK', 'a clean -c';
$p.err.slurp(:close);

say 'PASS';
