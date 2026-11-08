# `--doc` renders the program's Pod and runs nothing past INIT, as Rakudo's
# does: it ran the whole program first (mainline, MAIN and END) and then
# printed the Pod. A `DOC` phaser is the phaser it prefixes under `--doc`,
# and nothing otherwise: `DOC INIT {…}` ran in every run, and `DOC BEGIN {…}`
# never ran under `--doc`.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $f = $*TMPDIR.add("doc-run-{$*PID}.raku");
END { $f.unlink if $f.e }
$f.spurt: q:to/P/;
    BEGIN say "begin";
    INIT say "init";
    DOC INIT { say "doc-init" }
    DOC BEGIN { say "doc-begin" }
    say "main";
    END say "end";
    sub MAIN { say "MAIN" }

    =para w
    P
my $doc = run $*EXECUTABLE, '--doc', $f, :out;
ck $doc.out.slurp(:close), "begin\ndoc-begin\ninit\ndoc-init\nw\n", '--doc';
my $plain = run $*EXECUTABLE, $f, :out;
ck $plain.out.slurp(:close), "begin\ninit\nmain\nMAIN\nend\n", 'without --doc no DOC phaser runs';

say 'PASS';
