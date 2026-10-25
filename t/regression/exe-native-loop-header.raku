# `loop (my int $i = 0; …)` and `my num $s` under `--exe -O`: the unboxed
# loop lane refused a native declared in a loop HEADER, because the name
# outlives the lane and its store-back rebuilt the box as a plain Int, losing
# the native's width. So the loop FORTRAN-LESSONS times (1b), a `my num`
# accumulator over a `loop (my int $i …)`, ran boxed: 48x slower than the
# same loop untyped. The store-back now builds the native as its
# declaration does (rtNativeStoreK), and the lane takes the loop; `$i` after
# the loop is still an `int` that wraps.
#
# This file holds the interpreter, `--exe` and `--exe -O` to Rakudo 2026.09's
# lines, and counts the lanes `-O` emits, so the loops cannot drift back to
# the boxed path unnoticed. A loop in a sub whose parameters are all natives
# runs in its native form (planNative: a C++ loop over int64_t and double,
# one `__nnext` label each) when its Value function calls that form; such a
# loop counts as a lane does.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-exe-native-loop-header-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }
my $src = q:to/END/;
    sub f(int $nn --> num) { my num $s = 0e0; loop (my int $i = 0; $i < $nn; $i++) { $s = $s + $i * 1.5e0 }; $s }
    sub after() { my $c = 0; loop (my int $i = 0; $i < 10; $i++) { $c = $c + $i }; $i = $i + 9223372036854775800; "$c $i" }
    sub wrap() { my $c = 0; loop (my int $k = 9223372036854775800; $k > 0; $k = $k + 3) { $c = $c + 1 }; "$c $k" }
    sub numh() { my $n = 0; loop (my num $x = 0e0; $x < 1e0; $x = $x + 0.25e0) { $n = $n + 1 }; "$n $x" }
    sub nested(int $m) { my int $t = 0; loop (my int $i = 0; $i < $m; $i++) { loop (my int $j = 0; $j < $m; $j++) { $t = $t + $i * $j } }; $t }
    sub mixed(int $m) { my num $s = 0e0; my int $c = 0; loop (my int $i = 0; $i < $m; $i++) { $c = $c + 1; $s = $s + $c / 2 }; "$c $s" }
    say f(1000);
    say after();
    say wrap();
    say numh();
    say nested(20);
    say mixed(10);
    END

# What Rakudo 2026.09 prints for $src, line for line.
my @want =
    '749250',                       # f: FORTRAN-LESSONS' loop
    '45 -9223372036854775806',      # after: $i is still a native, and wraps
    '3 -9223372036854775807',       # wrap: the step wraps, and the loop ends
    '4 1',                          # numh: a `num` header
    '36100',                        # nested
    '10 27.5';                      # mixed

sub held($what, $out) {
    my @got = $out.lines;
    for ^max(+@want, +@got) -> $k {
        @fail.push("$what, line {$k + 1}: got {(@got[$k] // '(nothing)').raku}, want {(@want[$k] // '(nothing)').raku}")
            if (@got[$k] // '') ne (@want[$k] // '');
    }
}

my $f = $dir.add("nativehdr.raku");
$f.spurt($src);
my $interp = run($bin, $f.absolute, :out, :err);
held('interpreted', $interp.out.slurp(:close)); $interp.err.slurp(:close);

# A count that falls means a loop went back to boxed.
my $cpp = run($bin, '--cpp', '-O', $f.absolute, :out, :err);
my $text = $cpp.out.slurp(:close); $cpp.err.slurp(:close);
my $lanes = +$text.comb('unboxed loop lane') + +$text.comb(/ ^^ \s* '__nnext' \d+ ': ;' /);
@fail.push("only $lanes loops carry a lane or run natively, want 6") unless $lanes >= 6;

for (('--exe', '-O'), ('--exe',)) -> @mode {
    my $exe = $dir.add("nativehdr-{+@mode}.bin");
    my $c = run($bin, |@mode, $f.absolute, '-o', $exe.absolute, :out, :err);
    $c.out.slurp(:close); my $cerr = $c.err.slurp(:close);
    if !$exe.e { @fail.push("{@mode} did not compile: $cerr"); next }
    @fail.push("{@mode} bundled the program: $cerr") if $cerr.contains('bundling');
    my $r = run($exe.absolute, :out, :err);
    held(@mode.join(' '), $r.out.slurp(:close)); $r.err.slurp(:close);
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
