# Regression: natives in a COMPILED program's multi dispatch (2026-11-06),
# as the interpreter has them (native-multi-dispatch.raku,
# native-array-elements.raku) and as Rakudo 2026.09 answers. A compiled multi
# dispatcher, of subs and of methods, never matched a native candidate: its
# guards were rtTypeMatch's, which knows no native type, so `multi a(int $x)`
# beside `multi a(Int $x)` took `Int` for a `my int`, and a lone native
# candidate refused every argument. Now
#
#   · a native parameter's guard is rtNatArg (a native of its family only),
#     and a candidate with more of them is tried first;
#   · an element of a native array handed to a routine, a List literal, or
#     a `for`'s topic is the native it holds (rtNatElem, nativeElemTag);
#   · a boxed `$` parameter boxes a native — a declared one, a named, a
#     slurpy's elements, a placeholder, a loop variable — and a native one
#     holds it (rtBoxedItem / rtNatItem, and the -O direct path's In forms);
#   · a routine answers a native's value boxed (rtBoxedNum), and a `$`
#     variable, an element of an Array or a Hash, or an Array copy holds it
#     boxed (`my $s = $n + 1`, `@a[0] = $n`: coerceFor);
#   · natives of one family and two widths (`int8` beside `int`, which
#     Rakudo finds ambiguous) leave the program to the interpreter.
#
# The source must compile natively (no interpreter bundled), with and without
# -O, and answer as the interpreter and Rakudo do. Contract: exit 0 + last
# line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-native-multi-dispatch: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-native-multi-dispatch-$*PID");
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

my $src = $work.add('dispatch.raku');
$src.spurt: q:to/END/;
    multi a(int $x) { "int" }; multi a(Int $x) { "Int" }
    multi b(Int $x) { "Int" }; multi b(int $x) { "int" }
    multi c(uint $x) { "uint" }; multi c(int $x) { "int" }; multi c(Int $x) { "Int" }
    multi d(num $x) { "num" }; multi d(Num $x) { "Num" }
    multi f1(int $x) { "int" }; multi f1($x) { "any" }
    multi f3(int $x) { "int" }; multi f3(Numeric $x) { "Numeric" }
    multi f11(int $x) { "int" }; multi f11(num $x) { "num" }
    multi z(int $x) { "z-int" }
    multi w(num $x) { "w-num" }
    multi m2(int $a, Str $b) { "int,Str" }; multi m2(Int $a, Str $b) { "Int,Str" }
    class C { multi method m(Int $x) { "Int" }; multi method m(int $x) { "int" }; multi method m(Str $x) { "Str" } }
    class D { multi method n(int $x) { "int" } }
    sub only(int $x) { "only-int" }
    sub r { my int $x = 5; $x }
    sub r2(--> int) { 5 }
    sub t(&c) { my $r = try c(); $r // "ERR:" ~ ($!.^name.subst(/^X\:\:/, "")) }
    sub f($x) { a($x) }; sub g(Int $x) { a($x) }; sub k(*@x) { a(@x[0]) }; sub k3(+@x) { a(@x[0]) }
    sub q(:$x) { a($x) }; sub q2(Int :$x) { a($x) }; sub cp($x is copy) { a($x) }; sub opt($x?) { a($x) }; sub df($x = 1) { a($x) }
    sub nat(int $x) { a($x) }; sub nn(num $x) { d($x) }; sub n8(int8 $x) { a($x) }; sub nu(uint $x) { a($x) }
    my int $n = 3; my uint $u = 3; my num $d = 1.5e0; my int @ia = 1, 2; my num @na = 1e0, 2e0; my $v = 3;
    say "subs   ", (t({a($n)}), t({a($v)}), t({a(3)}), t({b($n)}), t({b(3)}), t({c($n)}), t({c($u)}), t({c(3)})).join(" ");
    say "num    ", (t({d($d)}), t({d(1.5e0)}), t({f11($n)}), t({f11($d)}), t({f11(3)}), t({w($d)}), t({w(1.5e0)})).join(" ");
    say "boxed  ", (t({f1(3)}), t({f1($n)}), t({f3(3)}), t({f3($n)}), t({z($n)}), t({z(3)}), t({z($v)})).join(" ");
    say "ops    ", (t({a($n % 3)}), t({a($n + 1)}), t({a(-$n)}), t({a(+$n)}), t({z($n * 2)})).join(" ");
    say "elems  ", (t({a(@ia[0])}), t({a(@ia[*-1])}), t({z(@ia[1])}), t({d(@na[0])}), t({a(@ia.elems)}), t({a(@ia[0] + 1)})).join(" ");
    say "topic  ", (@ia.map({ a($_) }).join(","), @ia.map(-> $x { a($x) }).join(","), @ia.map(-> int $x { a($x) }).join(","),
                    @ia.grep({ a($_) eq "int" }).elems, a(@ia.first({ $_ > 1 })), @ia.map({ a($^x) }).join(",")).join(" ");
    my @l = $n, 4; my %h = k => $n; my $s = $n; my @copy = @ia;
    say "boxing ", (a(r()), a(r2()), a($s), a(@l[0]), a(%h<k>), a(@copy[0]), a(($n, 4)[0])).join(" ");
    my $s1 = $n + 1; my $s3; $s3 = $n * 2; my @arr = 0, 0; @arr[0] = $n; @arr[1] = $n + 1; my %hh; %hh<k> = $n; %hh<j> = $n % 2;
    say "stores ", (a($s1), a($s3), a(@arr[0]), a(@arr[1]), a(%hh<k>), a(%hh<j>)).join(" ");
    say "params ", (f($n), g($n), k($n), k3($n), q(:x($n)), q2(:x($n)), cp($n), opt($n), df($n), f(@ia[0]), { a($^x) }($n)).join(" ");
    say "native ", (nat(3), nat($n), nn(1.5e0), nn(@na[0]), n8(3), n8(300), nu(3)).join(" ");
    my @r; for @ia { @r.push(a($_)) }; for @ia -> $x { @r.push(a($x)) }; for @ia -> int $x { @r.push(a($x)) };
    for ($n, 4) -> $x { @r.push(a($x)) }; for @na -> num $x { @r.push(d($x)) }; say "loops  ", @r.join(" ");
    say "multi  ", (t({m2($n, "x")}), t({m2(3, "x")})).join(" ");
    say "meth   ", (t({C.m($n)}), t({C.m(3)}), t({C.m("s")}), t({C.m(@ia[0])}), t({D.n($n)}), t({D.n(3)})).join(" ");
    say "plain  ", (t({only(3)}), t({only($n)})).join(" ");
    my int $i = 0; my $o = ""; while $i < 3 { $o ~= z($i) ~ " "; $i = $i + 1 }; say "loop   $o.trim()";
    END

# Rakudo 2026.09's answers
my $want = (
    'subs   int Int Int int Int int uint Int',
    'num    num Num int num ERR:Multi::NoMatch w-num ERR:Multi::NoMatch',
    'boxed  any int Numeric int z-int ERR:Multi::NoMatch ERR:Multi::NoMatch',
    'ops    int int int Int z-int',
    'elems  int int z-int num Int Int',
    'topic  int,int Int,Int int,int 2 int Int,Int',
    'boxing Int Int Int Int Int Int int',
    'stores Int Int Int Int Int Int',
    'params Int Int Int Int Int Int Int Int Int Int Int',
    'native int int num num int int Int',
    'loops  int int Int Int int int Int Int num num',
    'multi  int,Str Int,Str',
    'meth   int Int Str int int ERR:Multi::NoMatch',
    'plain  only-int only-int',
    'loop   z-int z-int z-int',
).join('|');

my $i = run($*EXECUTABLE, $src.Str, :out, :err);
check('interpreted', $i.out.slurp(:close).lines.join('|'), $want);
$i.err.slurp(:close);

# the dispatchers' native guards, the elements' tags and the binds' boxing
# are in the C++, with and without -O
for ('', '-O') -> $o {
    my $c = run($*EXECUTABLE, '--cpp', |($o ?? ($o,) !! ()), $src.Str, :out, :err);
    my $text = $c.out.slurp(:close); $c.err.slurp(:close);
    for <rtNatArg( rtNatElem( rtBoxedItem( rtNatItem( rtBoxedNum( nativeElemTag(> -> $h {
        check("C++ $o has $h", so($text.contains($h)), True);
    }
}

for ('', '-O') -> $o {
    my $tag = $o ?? "compiled $o" !! 'compiled';
    my $bin = $work.add('dispatch' ~ $o);
    my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $src.Str, :out, :err);
    my $bout = $b.out.slurp(:close) ~ $b.err.slurp(:close);
    if $b.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $bout; next }
    check("$tag: natively, no interpreter bundled", so($bout.contains('Compiled (native)') && !$bout.contains('bundling')), True);
    my $p = run($bin.Str, :out, :err);
    check("$tag: agrees", $p.out.slurp(:close).lines.join('|'), $want);
    $p.err.slurp(:close);
}

# `int8` beside `int`: Rakudo's ambiguity, kept by bundling the interpreter
my $tie = $work.add('tie.raku');
$tie.spurt: q:to/END/;
    multi h(int8 $x) { "int8" }; multi h(int $x) { "int" }; multi h(Int $x) { "Int" }
    sub t(&c) { my $r = try c(); $r // "ERR:" ~ ($!.^name.subst(/^X\:\:/, "")) }
    my int $n = 3; my int8 $b = 3;
    say (t({h($n)}), t({h($b)}), t({h(3)})).join(" ");
    END
for ('', '-O') -> $o {
    my $tag = $o ?? "two widths $o" !! 'two widths';
    my $bin = $work.add('tie' ~ $o);
    my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $tie.Str, :out, :err);
    my $bout = $b.out.slurp(:close) ~ $b.err.slurp(:close);
    if $b.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $bout; next }
    check("$tag: bundles the interpreter", so($bout.contains('natives of one family and two widths')), True);
    my $p = run($bin.Str, :out, :err);
    check("$tag: agrees", $p.out.slurp(:close).trim, 'ERR:Multi::Ambiguous ERR:Multi::Ambiguous Int');
    $p.err.slurp(:close);
}

$work.dir».unlink;
$work.rmdir;
say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
