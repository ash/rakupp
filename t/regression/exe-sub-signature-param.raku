# A COMPILED program with a sub-signature on a routine's or a pointy block's
# parameter builds and answers as the interpreter does (2026-11-09). The
# generated C++ named the sub-signature's variables, `sub f(($a, $b))` or
# `.map(-> ($k, @n) { … })`, without declaring them, so clang refused the
# program ("use of undeclared identifier 'v_sa'") and no binary was written.
# Codegen refuses the parameter now, as it refuses a `where` on one, and the
# program is bundled with the interpreter; a `for` loop's `-> ($a, $b)`,
# which unpacks its elements itself, stays native.
#
# Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-sub-signature-param: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-sub-signature-param-$*PID");
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

my $src = $work.add('subsig.raku');
$src.spurt: q:to/END/;
    say ((1, <a b>), (2, <c d>)).map(-> ($k, @n) { "$k:@n[]" }).join(' ');
    sub f(($a, $b)) { $a + $b }
    say f((1, 2)), ' ', f([3, 4]);
    sub g(($h, *@t)) { "$h|@t[]" }
    say g((1, 2, 3));
    try f((1, 2, 3));
    say $!.message;
    END
my $loop = $work.add('loop.raku');
$loop.spurt: q:to/END/;
    for (1, 2), (3, 4) -> ($a, $b) { say $a + $b }
    END

my $want = (
    '1:a b 2:c d',
    '3 7',
    '1|2 3',
    'Too many positionals passed; expected 2 arguments but got 3 in sub-signature',
).join('|');

my $i = run($*EXECUTABLE, $src.Str, :out, :err);
check('interpreted', $i.out.slurp(:close).lines.join('|'), $want);
$i.err.slurp(:close);

for ('', '-O') -> $o {
    my $tag = $o ?? "compiled $o" !! 'compiled';
    my $bin = $work.add('subsig' ~ $o);
    my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $src.Str, :out, :err);
    my $bout = $b.out.slurp(:close) ~ $b.err.slurp(:close);
    if $b.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $bout; next }
    my $p = run($bin.Str, :out, :err);
    check("$tag: agrees", $p.out.slurp(:close).lines.join('|'), $want);
    $p.err.slurp(:close);

    my $lbin = $work.add('loop' ~ $o);
    my $l = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $lbin.Str, $loop.Str, :out, :err);
    my $lout = $l.out.slurp(:close) ~ $l.err.slurp(:close);
    check("$tag: a loop's sub-signature stays native", so($l.exitcode == 0 && $lout.contains('Compiled (native)')), True);
    my $q = run($lbin.Str, :out, :err);
    check("$tag: the loop agrees", $q.out.slurp(:close).lines.join('|'), '3|7');
    $q.err.slurp(:close);
}

for $work.dir { .unlink }
$work.rmdir;
say $fails ?? 'FAIL' !! 'PASS';
