# Regression: `my num @a` / `my int @a` (full width) under --exe. A typed
# array declaration used to bundle the whole program with the interpreter;
# now a native array compiles, filled by the same routine the interpreter's
# `=` uses (Interpreter::arrayStore: each element unboxed or refused, a lazy
# list refused, the words kept), and an element read or a statement store of
# one uses its words while it is packed (rtIndexGetP, rtIndexStore) instead
# of unpacking it.
#
# The program runs interpreted and compiled, with and without -O; each must
# print Rakudo 2026.09's lines, the compile must not bundle, and the element
# loop must leave its arrays packed (RAKUPP_PACKED_CENSUS reports none).
# Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-native-arrays: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-native-arrays-$*PID");
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

my $src = $work.add('arrays.raku');
$src.spurt: q:to/END/;
    my num @a = 1e0, 2.5e0, 4e0;
    my int @i = 1 .. 5;
    my num @c = @a;
    say @a, " ", @i, " ", @c, " ", @a.of.^name, " ", @i.^name;
    say @a[1], " ", @a[7], " ", @i[9], " ", @i.elems;
    @a[1] = 7.25e0;
    @i[0] = 40;
    @i[2] = @i[3] + @i[4];
    say @a, " ", @i, " ", @c;
    @a.push(9e0);
    @i = 3, 2, 1;
    say @a.elems, " ", @a[3], " ", @i;
    my $sum = 0e0;
    for @a -> $x { $sum = $sum + $x }
    say $sum;
    { my int @bad = 1, "x"; say @bad; CATCH { default { say "bad: ", .message } } }
    { my num @bad = 1, 2; say @bad; CATCH { default { say "bad2: ", .message } } }
    { my int @lazy = 1 .. *; say @lazy[0]; CATCH { default { say "lazy refused" } } }
    { my int @big = 2 ** 70; CATCH { default { say "big refused" } } }
    my num @t[4] = 1e0, 2e0;
    say @t, " ", @t.shape;
    @t[3] = 5e0;
    say @t;
    my int @e;
    say @e.elems, " ", @e.raku;
    @e[3] = 7;
    say @e;
    sub fill(int $n) { my int @w = 0 xx $n; loop (my int $k = 0; $k < $n; $k++) { @w[$k] = $k * $k }; @w.sum }
    say fill(10);
    END
my $want = ('[1 2.5 4] [1 2 3 4 5] [1 2.5 4] num array[int]', '2.5 0 0 5',
            '[1 7.25 4] [40 2 9 4 5] [1 2.5 4]', '4 9 [3 2 1]', '21.25',
            'bad: This type cannot unbox to a native integer: P6opaque, Str',
            'bad2: This type cannot unbox to a native number: P6opaque, Int',
            'lazy refused', 'big refused', '[1 2 0 0] (4)', '[1 2 0 5]', '0 array[int].new()',
            '[0 0 0 7]', '285').join('|');

my $i = run($*EXECUTABLE, $src.Str, :out, :err);
my $interp = $i.out.slurp(:close).lines.join('|');
$i.err.slurp(:close);
check('interpreted', $interp, $want);
for ('', '-O') -> $o {
    my $tag = $o ?? "compiled $o" !! 'compiled';
    my $bin = $work.add('arrays' ~ $o);
    my $c = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $src.Str, :out, :err);
    my $cout = $c.out.slurp(:close) ~ $c.err.slurp(:close);
    if $c.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $cout; next }
    check("$tag: not bundled", $cout.contains('bundling') ?? $cout.trim !! 'native', 'native');
    my $p = run($bin.Str, :out, :err);
    my $out = $p.out.slurp(:close).lines.join('|');
    $p.err.slurp(:close);
    check("$tag: agrees", $out, $want);
}

# An element loop over native arrays, compiled: its arrays stay packed.
my $loop = $work.add('loop.raku');
$loop.spurt: q:to/END/;
    my num @x = 0e0 xx 64;
    my int @n = 0 xx 64;
    loop (my int $k = 0; $k < 64; $k++) { @x[$k] = $k * 0.5e0; @n[$k] = $k }
    my num $s = 0e0;
    my int $m = 0;
    loop (my int $k = 0; $k < 64; $k++) { $s = $s + @x[$k]; $m = $m + @n[$k] }
    say "$s $m";
    END
for ('', '-O') -> $o {
    my $tag = $o ?? "loop $o" !! 'loop';
    my $bin = $work.add('loop' ~ $o);
    my $c = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $loop.Str, :out, :err);
    $c.out.slurp(:close); $c.err.slurp(:close);
    without $bin.e { $fails++; say "not ok - $tag: compiles"; next }
    my $p = run($bin.Str, :out, :err, :env(%*ENV, RAKUPP_PACKED_CENSUS => '1'));
    my $out = $p.out.slurp(:close).trim;
    my $err = $p.err.slurp(:close);
    check("$tag: answer", $out, '1008 2016');
    check("$tag: no array unpacked", $err.contains('packed census') ?? $err.trim !! 'none', 'none');
}

$work.dir».unlink;
$work.rmdir;
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
