# Regression: inside a routine compiled to a native C++ function (Codegen
# planNative), an element of an array declared native is read and written as
# its machine word while the array is packed, and a `/` of Nums by something
# other than a literal is a double division while the divisor is not zero.
# Each statement or condition holding such a part runs its native form
# behind those tests and its boxed form otherwise, so these must answer as
# the interpreter (and Rakudo 2026.09) does:
#
#   · reads in conditions, declarations, assignments and `+=`; stores of
#     element arithmetic; int elements compared and stored; NaN and -0e0;
#   · a zero divisor, an element of the wrong kind, an index out of range or
#     negative, a store past the end: the boxed form's answer or exception;
#   · file-scope natives set from elements; an array of the routine's own
#     (`my num @z` there was taken for a native scalar, and held 3);
#   · a routine ending in a loop, which answers Nil and now has its native
#     form; slices of native arrays (native arrays of the same type);
#   · the same routines once the generic runtime has unpacked the arrays.
#
# The C++ `--cpp -O` writes must hold the native functions and the word reads
# and writes, and a loop over packed arrays must leave them packed
# (RAKUPP_PACKED_CENSUS reports none). Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-native-elements: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-native-elements-$*PID");
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

my $src = $work.add('elements.raku');
$src.spurt: q:to/END/;
    my num @x = 1e0, 2e0, 4e0, 0e0, -0e0, NaN;
    my int @n = 3, 9223372036854775807, -2, 7;
    my num @y = 0e0 xx 6;
    my int @m = 0 xx 4;
    my num $g = 0e0;
    my int $gi = 0;
    sub sum(int $k --> num) {
        my num $s = 0e0;
        loop (my int $t = 0; $t < $k; $t++) {
            if @x[$t] > 1.5e0 { $s += @x[$t] } else { $s = $s - @x[$t] }
        }
        $s
    }
    sub ratio(int $k, num $d) {
        loop (my int $t = 0; $t < $k; $t++) { @y[$t] = (@x[$t] / $d) * 2e0 }
    }
    sub inv(int $k) {
        loop (my int $t = 0; $t < $k; $t++) { my num $r = 1e0 / @x[$t]; @y[$t] = $r }
    }
    sub ints(int $a, int $b --> int) {
        my int $c = 0;
        loop (my int $t = 0; $t < 4; $t++) { if @n[$t] == $a || @n[$t] < $b { $c++ } }
        @m[0] = @n[0]; @m[1] = @n[3]; @m[2] = $c;
        $c
    }
    sub edge(int $i) { my num $v = @x[$i]; say "edge $i $v"; return }
    sub past(int $i) { @y[$i] = 5e0; return }
    sub glob(int $i) { $g = @x[$i] * 3e0; $gi = @n[$i]; return }
    sub tail(int $k) { my int $t = 0; while $t < $k { @m[3] = @m[3] + $t; $t++ } }
    sub cmp(int $i, int $j --> int) { @x[$i] == @x[$j] ?? 1 !! @x[$i] < @x[$j] ?? 2 !! 3 }
    sub own(int $k --> num) {
        my num @w = 0e0 xx $k;
        loop (my int $t = 0; $t < $k; $t++) { @w[$t] = $t * 0.5e0 }
        my num $s = 0e0;
        loop (my int $u = 0; $u < $k; $u++) { $s = $s + @w[$u] }
        say @w[1 .. 2], " ", @w[2 .. $k - 1];
        $s
    }
    sub wrong(int $i) { my num $v = @n[$i]; say "wrong $v"; return }
    sub wrong2(int $i) { my int $v = @x[$i]; say "wrong2 $v"; return }
    sub mine(int $k) { my num @z = 1e0, 2e0, 3e0; my int @q = 4, 5; say @z[^$k], " ", @z[1], " ", @z, " ", @q[1]; return }
    say sum(3), " ", sum(6);
    ratio(3, 2e0); say @y;
    { ratio(3, 0e0); say @y; CATCH { default { say "ratio 0: ", .^name } } }
    { inv(3); say @y; inv(4); say @y; CATCH { default { say "inv: ", .^name } } }
    say ints(-2, 0), " ", @m;
    edge(2); edge(9);
    { edge(-1); CATCH { default { say "edge -1 died" } } }
    past(8); say @y.elems, " ", @y[8];
    glob(2); say "$g $gi";
    say tail(4).raku, " ", @m[3];
    say cmp(0, 0), cmp(5, 5), cmp(3, 4), cmp(0, 2), cmp(2, 0);
    say own(4), " ", own(3);
    { wrong(0); CATCH { default { say "wrong: ", .^name } } }
    { wrong2(0); CATCH { default { say "wrong2: ", .^name } } }
    say @x[1 .. 2], " ", @n[2 .. *];
    mine(2);
    @x.push(8e0);
    @n.push(11);
    say sum(3), " ", ints(-2, 0), " ", @m;
    ratio(3, 4e0); say @y[^3];
    END
my $want = ('5 NaN', '[1 2 4 0 0 0]', 'ratio 0: X::Numeric::DivideByZero', '[1 0.5 0.25 0 0 0]',
            'inv: X::AdHoc', '1 [3 7 1 0]', 'edge 2 4', 'edge 9 0', 'edge -1 died', '9 5', '12 -2',
            'Nil 6', '13123', '[0.5 1] [1 1.5]', '[0.5 1] [1]', '3 1.5', 'wrong: X::AdHoc',
            'wrong2: X::AdHoc', '[2 4] [-2 7]', '[1 2] 2 [1 2 3] 5', '5 1 [3 7 1 6]', '[0.5 1 2]').join('|');

my $i = run($*EXECUTABLE, $src.Str, :out, :err);
my $interp = $i.out.slurp(:close).lines.join('|');
$i.err.slurp(:close);
check('interpreted', $interp, $want);

my $cpp = $work.add('elements.cpp');
my $c = run($*EXECUTABLE, '--cpp', '-O', '-o', $cpp.Str, $src.Str, :out, :err);
$c.out.slurp(:close); $c.err.slurp(:close);
my $text = $cpp.e ?? $cpp.slurp !! '';
for <sum ratio inv ints edge past glob tail cmp own wrong wrong2 mine> -> $r {
    check("n_$r is a native function", so($text ~~ / ^^ 'static ' ['int64_t'|'double'|'void'] ' n_' $r '(' /), True);
}
for <__nWN( __nWI( __nSetWN( __nSetWI( __nPkW(> -> $w {
    check("the C++ has $w", $text.contains($w), True);
}
check('a divisor test', so($text ~~ / '(nv_sd) != 0.0)' /), True);

for ('', '-O') -> $o {
    my $tag = $o ?? "compiled $o" !! 'compiled';
    my $bin = $work.add('elements' ~ $o);
    my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $src.Str, :out, :err);
    my $bout = $b.out.slurp(:close) ~ $b.err.slurp(:close);
    if $b.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $bout; next }
    my $p = run($bin.Str, :out, :err);
    my $out = $p.out.slurp(:close).lines.join('|');
    $p.err.slurp(:close);
    check("$tag: agrees", $out, $interp);
}

# Routines over packed arrays, compiled: the arrays stay packed.
my $loop = $work.add('loop.raku');
$loop.spurt: q:to/END/;
    my num @v = 0e0 xx 32;
    my int @c = 0 xx 32;
    sub fill(int $n) { loop (my int $t = 0; $t < $n; $t++) { @v[$t] = $t * 0.25e0; @c[$t] = $t * 3 } }
    sub total(int $n --> num) {
        my num $s = 0e0;
        loop (my int $t = 0; $t < $n; $t++) {
            if @c[$t] > 40 { $s = $s - @v[$t] } else { $s = $s + @v[$t] / 2e0 }
        }
        $s
    }
    fill(32);
    say total(32);
    END
for ('', '-O') -> $o {
    my $tag = $o ?? "loop $o" !! 'loop';
    my $bin = $work.add('loop' ~ $o);
    my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $loop.Str, :out, :err);
    $b.out.slurp(:close); $b.err.slurp(:close);
    without $bin.e { $fails++; say "not ok - $tag: compiles"; next }
    my $p = run($bin.Str, :out, :err, :env(%*ENV, RAKUPP_PACKED_CENSUS => '1'));
    my $out = $p.out.slurp(:close).trim;
    my $err = $p.err.slurp(:close);
    check("$tag: answer", $out, '-89.875');
    check("$tag: no array unpacked", $err.contains('packed census') ?? $err.trim !! 'none', 'none');
}

$work.dir».unlink;
$work.rmdir;
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
