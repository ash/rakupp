# Regression: in a program `--exe` bundles with the interpreter, the routines
# it still compiles natively (module mode, one at a time) judge a store into
# an element of a native array as the interpreter does (nativeElemStore), as
# a compiled program's do since 80ea9030. They wrote the element's place,
# which takes anything: a Str, a Rat or a Failure into an `int` or `num`
# array, an Int into a `num` one, an Int too wide; a Bool stayed one, a step
# at int64's top did not wrap, and an `int8` element did not wrap at all.
# Module mode has the module's declarations of any width (AotNames), and a
# store tests at run time that the array is native: a plain array of the
# name, or a List the interpreter kept, keeps module mode's own path.
#
# Each routine must compile natively (RAKUPP_AOT_REPORT), and the answers
# must be the interpreter's and Rakudo 2026.09's, plain and -O.
# Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-native-elem-store-bundled: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-native-elem-store-bundled-$*PID");
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

my $src = $work.add('bundled.raku');
$src.spurt: q:to/END/;
    my num @v = 0e0 xx 3;
    my int @w = 0 xx 3;
    my int8 @b = 0 xx 3;
    my str @s = '' xx 2;
    my @plain = 1, 2, 3;
    sub t(&f, $what) { f(); CATCH { default { say "$what: ", .^name } } }
    sub sv($x) { @v[1] = $x; return }
    sub sw($x) { @w[1] = $x; return }
    sub op($x) { @w[1] += $x; return }
    sub st() { @w[2] = 9223372036854775807; @w[2]++; return }
    sub pre() { ++@w[0]; return }
    sub b8($x) { @b[0] = $x; return }
    sub ss($x) { @s[0] = $x; return }
    sub pl($x) { @plain[0] = $x; return }
    sub val($x) { my $r = (@v[0] = $x); return $r }
    t({ sv("abc"); say "str ", @v }, 'str');
    t({ sv(1/0); say "failure ", @v }, 'failure');
    t({ sw(2.5); say "rat ", @w }, 'rat');
    t({ sv(3); say "int into num ", @v }, 'int into num');
    t({ sw(True); say "bool ", @w }, 'bool');
    t({ sw(2**70); say "wide ", @w }, 'wide');
    t({ op(2.5); say "op ", @w }, 'op');
    t({ st(); say "step ", @w }, 'step');
    t({ pre(); say "pre ", @w }, 'pre');
    t({ b8(200); say "int8 ", @b }, 'int8');
    t({ ss(5); say "str elem ", @s }, 'str elem');
    t({ pl("x"); say "plain ", @plain }, 'plain');
    t({ say "val ", val(4e0), ' ', @v }, 'val');
    END
my $want = ('str: X::TypeCheck', 'failure: X::TypeCheck', 'rat: X::TypeCheck', 'int into num: X::TypeCheck',
            'bool [0 1 0]', 'wide: X::AdHoc', 'op: X::AdHoc', 'step [0 1 -9223372036854775808]',
            'pre [1 1 -9223372036854775808]', 'int8 [-56 0 0]', 'str elem: X::TypeCheck', 'plain [x 2 3]',
            'val 4 [4 0 0]').join('|');

my $i = run($*EXECUTABLE, $src.Str, :out, :err);
check('interpreted', $i.out.slurp(:close).lines.join('|'), $want);
$i.err.slurp(:close);

for ('', '-O') -> $o {
    my $tag = $o ?? "compiled $o" !! 'compiled';
    my $bin = $work.add('bundled' ~ $o);
    my $report = $work.add('bundled' ~ $o ~ '.aot');
    my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $src.Str, :out, :err,
                :env(%*ENV, RAKUPP_AOT_REPORT => $report.Str));
    my $bout = $b.out.slurp(:close) ~ $b.err.slurp(:close);
    if $b.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $bout; next }
    check("$tag: the program is bundled", $bout.contains('bundling the program'), True);
    my $rep = $report.e ?? $report.slurp !! '';
    for <sv sw op st pre b8 ss pl val> -> $r {
        my $line = $rep.lines.first({ (.split("\t")[3] // '') eq "sub $r" }) // '';
        check("$tag: sub $r native", $line.split("\t")[4] // 'not in the report', 'native');
    }
    my $p = run($bin.Str, :out, :err);
    check("$tag: agrees", $p.out.slurp(:close).lines.join('|'), $want);
    $p.err.slurp(:close);
}

$work.dir».unlink;
$work.rmdir;
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
