# A native int operator takes literals an operator folds as it takes a written
# literal, as Rakudo folds `2**62` at compile time: `sub f(int $x) { $x * 2**62
# }; f(3)` answered 13835058055282163712, where Rakudo wraps to
# -4611686018427387904. `1 +< 62` and `-2**62` fold the same way; an Int in a
# variable still takes the Int candidate and grows, and so does a folded value
# wider than 64 bits. Interpreted, and compiled natively, plain and `-O`.
# Every line answers the same under Rakudo 2026.09 (Rakudo has no `--exe`).

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $src = q:to/R/;
    sub f(int $x) { $x * 2**62 }; say f(3);
    my int $i = 3; say $i * 2**62;
    my int $k = 3; say $k * 4611686018427387904;
    my int $j = 3; my $b = 2**62; say $j * $b;
    say $i * (1 +< 62);
    say $i * 2**64;
    say $i * -2**62;
    say 2**62 * $i;
    R
my @want = q:w[-4611686018427387904 -4611686018427387904 -4611686018427387904 13835058055282163712
             -4611686018427387904 55340232221128654848 4611686018427387904 -4611686018427387904];
my $f = $*TMPDIR.add("native-fold-{$*PID}.raku");
$f.spurt($src);
LEAVE { $f.unlink if $f.e }
my $p = run $*EXECUTABLE, $f, :out;
ck $p.out.slurp(:close).lines.List, @want.List, 'interpreted';
if $*RAKU.compiler.name eq 'Raku++' {
    for (), ('-O',) -> @o {
        my $bin = $*TMPDIR.add("native-fold-{$*PID}-bin").Str;
        my $b = run $*EXECUTABLE, '--exe', |@o, $f, '-o', $bin, :out, :err;
        $b.out.slurp(:close);
        ck $b.err.slurp(:close).contains('(native)'), True, "built natively {@o}";
        my $r = run $bin, :out;
        ck $r.out.slurp(:close).lines.List, @want.List, "compiled {@o}";
        unlink $bin;
    }
}

say 'PASS';
