# Regression: a `loop` or `while` at the program's top level that reads or
# writes an element of a native array, or calls a routine compiled to a
# native function, is emitted as a native routine's loop is (Codegen
# nrMainline): its header's `my int` a C++ local for the loop's extent, its
# arrays' facts checked on entry, a native routine called directly. These
# must answer as the interpreter (and Rakudo 2026.09) does:
#
#   · the header's variable read after the loop (the boxed variable takes
#     the local's value), and the same name declared by a second loop;
#   · a native routine's call that writes a file-scope native the loop reads;
#   · a boxed statement in the loop (`say`), `next` and `last`;
#   · `while` over a file-scope counter, with an element in the condition;
#   · a store past the end and a read past the end;
#   · a loop whose body declares a boxed variable, which keeps its old
#     emission, with the same answers;
#   · an exception from inside the loop, which ends the program as before.
#
# A program with an END block keeps every top-level loop as it was (the
# block may read a header's variable), and still answers. The `--cpp -O` C++
# must hold the native loops. Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-native-mainline: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-native-mainline-$*PID");
mkdir $work;
my $fails = 0;

sub check(Str $desc, $got, $want) {
    if $got eq $want { say "ok - $desc" }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT  [{$got}]\nWANT [{$want}]";
    }
}
# stdout's lines and the exit code (stderr's wording differs between engines)
sub outcome(*@cmd) {
    my $p = run(|@cmd, :out, :err);
    my $o = $p.out.slurp(:close).lines.join('|');
    $p.err.slurp(:close);
    "$o|rc={$p.exitcode}"
}

my $src = $work.add('mainline.raku');
$src.spurt: q:to/END/;
    my num @v = 0e0 xx 8;
    my int @w = 0 xx 8;
    my int $count = 0;
    my num $sum = 0e0;
    sub bump(int $k) { $count = $count + $k; return }
    sub half(num $x --> num) { $x / 2e0 }
    loop (my int $i = 0; $i < 8; $i++) { @v[$i] = $i * 1.5e0; @w[$i] = $i * $i }
    say "after $i ", @v[7], ' ', @w[7];
    loop (my int $i = 0; $i < 5; $i++) { bump($i); $sum = $sum + half(@v[$i]) }
    say "again $i $count $sum";
    loop (my int $j = 0; $j < 8; $j++) {
        next if @w[$j] % 2;
        last if @w[$j] > 30;
        say "even $j {@w[$j]}";
    }
    say "j $j";
    my int $k = 0;
    while $k < 8 && @w[$k] < 20 { @w[$k] = @w[$k] + 100; $k++ }
    say "k $k ", @w;
    loop (my int $m = 0; $m < 3; $m++) { @v[$m + 10] = @v[$m] + @v[$m + 20] }
    say @v.elems, ' ', @v[10..12];
    loop (my int $n = 0; $n < 2; $n++) { my $s = "n$n"; say $s, ' ', @w[$n] }
    loop (my int $p = 0; $p < 3; $p++) { @v[$p] = 1e0 / ($p - 1); say "p $p" }
    say "not reached";
    END
my $want = ('after 8 10.5 49', 'again 5 10 7.5', 'even 0 0', 'even 2 4', 'even 4 16', 'j 6',
            'k 5 [100 101 104 109 116 25 36 49]', '13 [0 1.5 3]', 'n0 100', 'n1 101', 'p 0', 'rc=1').join('|');

my $ends = $work.add('ends.raku');
$ends.spurt: q:to/END/;
    my int @w = 1, 2, 3;
    loop (my int $i = 0; $i < 3; $i++) { @w[$i] = @w[$i] * 2 }
    END { say "end $i ", @w }
    END
my $wantEnds = 'end 3 [2 4 6]|rc=0';

check('interpreted', outcome($*EXECUTABLE, $src.Str), $want);
check('END: interpreted', outcome($*EXECUTABLE, $ends.Str), $wantEnds);

my $cpp = $work.add('mainline.cpp');
run($*EXECUTABLE, '--cpp', '-O', '-o', $cpp.Str, $src.Str, :out, :err).out.slurp(:close);
my $text = $cpp.e ?? $cpp.slurp !! '';
my $main = $text.substr(($text.index('static int __rakupp_main_body') // 0));
for <i j m p> -> $h {
    check("loop over \$$h is native", so($main ~~ / 'int64_t nv_s' $h '_' /), True);
}
check('a loop that declares a boxed variable keeps its emission', so($main ~~ / 'int64_t nv_sn_' /), False);
check('a native routine is called directly', so($main ~~ / 'n_bump(' /), True);
my $ecpp = $work.add('ends.cpp');
run($*EXECUTABLE, '--cpp', '-O', '-o', $ecpp.Str, $ends.Str, :out, :err).out.slurp(:close);
check('END: no native top-level loop', so(($ecpp.e ?? $ecpp.slurp !! '') ~~ / 'int64_t nv_si_' /), False);

for ('', '-O') -> $o {
    for ($src, $want), ($ends, $wantEnds) -> ($f, $w) {
        my $tag = ($o ?? "compiled $o" !! 'compiled') ~ ($f eq $ends ?? ' END' !! '');
        my $bin = $work.add($f.basename ~ $o ~ '.bin');
        my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $f.Str, :out, :err);
        my $bout = $b.out.slurp(:close) ~ $b.err.slurp(:close);
        if $b.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $bout; next }
        check("$tag: agrees", outcome($bin.Str), $w);
    }
}

$work.dir».unlink;
$work.rmdir;
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
