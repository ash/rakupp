# `exit` runs the END phasers and ends without unwinding, so no LEAVE runs,
# as on Rakudo: `for ^2 -> $i { LEAVE { say "leave$i" }; exit 0 if $i == 0 }`
# printed leave0, interpreted and compiled alike (the compiled LEAVE guards
# were destroyed as the exit unwound). A LEAVE still runs when its block is
# left by a `die`, a `return` or a `last`; and an END that calls a routine
# with a LEAVE still runs that LEAVE. The compiled build is checked to be
# native, and without an END, which `--exe` skips on an exit
# (TODO-FOLLOW-UPS.md).
# Every line answers the same under Rakudo 2026.09 (Rakudo has no `--exe`).

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $dir = $*TMPDIR.add("exit-no-leave-{$*PID}");
$dir.mkdir;
END { run 'rm', '-rf', ~$dir if $dir && $dir.e }
sub ran($src) {
    my $f = $dir.add('p.raku');
    $f.spurt($src);
    my $p = run $*EXECUTABLE, $f, :out, :err;
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    ($out.lines.join('|'), $p.exitcode)
}

ck ran(q:to/R/), ('end', 0), 'a loop body';
    for ^2 -> $i { LEAVE { say "leave$i" }; exit 0 if $i == 0 }
    END say "end";
    R
ck ran(q:to/R/), ('a|end', 3), 'a sub, a block and a method';
    class C { method m { LEAVE say "method"; { LEAVE say "block"; f() } } }
    sub f { LEAVE say "sub"; exit 3 }
    say "a";
    C.m;
    END say "end";
    R
ck ran(q:to/R/), ('g|after|h|i|e|l', 0), 'a die, a return, a last and an END still leave';
    sub g { LEAVE say "g"; die "x" }
    try g(); say "after";
    sub h { LEAVE say "h"; return 1 }
    h();
    for ^3 { LEAVE say "i"; last }
    sub l { LEAVE say "l"; 1 }
    END { say "e"; l() }
    exit;
    R

if $*RAKU.compiler.name eq 'Raku++' {
    # compiled: natively, the LEAVE guards stand aside for an exit too
    for q:to/R/, ('', 0), q:to/S/, ('a', 3) -> $src, $want {
        for ^2 -> $i { LEAVE { say "leave$i" }; exit 0 if $i == 0 }
        R
        sub f { LEAVE say "sub"; { LEAVE say "block"; exit 3 } }
        say "a";
        f();
        S
        my $f = $dir.add('n.raku');
        $f.spurt($src);
        my $bin = $dir.add('n-bin').Str;
        my $b = run $*EXECUTABLE, '--exe', $f, '-o', $bin, :out, :err;
        $b.out.slurp(:close);
        ck $b.err.slurp(:close).contains('(native)'), True, 'built natively';
        my $p = run $bin, :out, :err;
        my $out = $p.out.slurp(:close);
        $p.err.slurp(:close);
        ck ($out.lines.join('|'), $p.exitcode), $want, "compiled: $src.lines.head()";
    }
}

say 'PASS';
