# A COMPILED program's slurpy flattens as the interpreter's does (2026-11-06).
# The `--exe` runtime bound every positional slurpy through rtSlurpyPos, which
# collected the arguments as they came — `**@a` for all of them — so a native
# `sub f(*@a)` handed `f(@x)` one Array where the interpreter and Rakudo bind
# @x's elements, `f(%h)` one Hash, and `+@a` the same. Now the generated code
# passes the slurpy's kind and rtSlurpyPos binds through the binder's own
# helpers (slurpySpread, slurpyLoneIterable/Elements, slurpyLazyTail).
#
# The program must compile natively, with and without -O, and print what the
# interpreter prints (slurpy-flattens-a-hash.raku checks that against
# Rakudo's answers). Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-slurpy-flattens: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-slurpy-flattens-$*PID");
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

my $src = $work.add('slurpy.raku');
$src.spurt: q:to/END/;
    sub names(@a) { @a.map(*.^name).join(',') }
    sub flat(*@a) { names(@a) }
    sub one(+@a)  { names(@a) }
    sub keep(**@a) { names(@a) }
    sub lazy(*@a) { @a.is-lazy ?? 'lazy' !! 'eager' }
    my %h = a => 1, b => 2;
    my $item = %h;
    my @x = 1, 2, 3;
    say 'flat ', flat(%h), ' | ', flat(@x), ' | ', flat(@x, (4, 5)), ' | ', flat($item), ' | ', flat(1, |@x);
    say 'one  ', one(%h), ' | ', one(@x), ' | ', one(@x, @x), ' | ', one((1, (2, 3)));
    say 'keep ', keep(@x, %h), ' | ', keep(|@x);
    say 'lazy ', lazy(1..*), ' ', lazy(1..3), ' ', lazy(gather { take 1 for ^3 }.lazy);
    END

my $want = (
    'flat Pair,Pair | Int,Int,Int | Int,Int,Int,Int,Int | Hash | Int,Int,Int,Int',
    'one  Pair,Pair | Int,Int,Int | Array,Array | Int,List',
    'keep Array,Hash | Int,Int,Int',
    'lazy lazy eager lazy',
).join('|');

my $i = run($*EXECUTABLE, $src.Str, :out, :err);
check('interpreted', $i.out.slurp(:close).lines.join('|'), $want);
$i.err.slurp(:close);

for ('', '-O') -> $o {
    my $tag = $o ?? "compiled $o" !! 'compiled';
    my $bin = $work.add('slurpy' ~ $o);
    my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $src.Str, :out, :err);
    my $bout = $b.out.slurp(:close) ~ $b.err.slurp(:close);
    if $b.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $bout; next }
    check("$tag: natively, no interpreter bundled", so($bout.contains('Compiled (native)') && !$bout.contains('bundling')), True);
    my $p = run($bin.Str, :out, :err);
    check("$tag: agrees", $p.out.slurp(:close).lines.join('|'), $want);
    $p.err.slurp(:close);
}

for $work.dir { .unlink }
$work.rmdir;
say $fails ?? 'FAIL' !! 'PASS';
