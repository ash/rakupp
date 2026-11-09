# A COMPILED routine binds an `@` or `%` parameter as the interpreter does
# (2026-11-09): what its sigil refuses is refused, an unpassed optional one
# is empty, and an `is copy` one is a fresh container. Codegen bound each
# `unitemized(…)` of whatever came, with or without -O: `sub u(@a)` given
# an Int held the Int, `sub p(%h)` a List of Pairs held the List, `sub
# o(@a?)` given nothing had one element (Any), and `sub c(@a is copy)`
# shared the caller's Array, so its push reached the caller. The generated
# code asks the interpreter's rules now (rtSigilArg: notPositionalArg,
# notAssociativeArg) and copies as `my @b = …` does.
#
# The program must compile natively, with and without -O, and print what
# the interpreter prints, which is what Rakudo 2026.09 prints.
# Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-sigil-param-binding: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-sigil-param-binding-$*PID");
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

my $src = $work.add('sigil.raku');
$src.spurt: q:to/END/;
    sub u(@a) { @a.^name }; sub h(%h) { %h.^name }
    my $x = 1; say (try u($x)) // $!.^name; say (try h($x)) // $!.^name;
    say $!.message;
    sub p(%h) { %h.^name }; say (try p((a => 1, b => 2))) // $!.^name;
    sub d(:%h = (a => 1, b => 2)) { %h.^name }; say (try d()) // $!.^name;
    sub o(@a?) { @a.elems }; say o(); say o([1, 2]); my $any = Any; say (try o($any)) // $!.^name;
    sub n(:@l) { @l.elems }; say n(); say n(:l(1, 2)); say (try n(:l($x))) // $!.^name;
    sub q(@a = (1, 2)) { @a.elems }; say q(); say (try q($x)) // $!.^name;
    say u([1]), ' ', u((1, 2)), ' ', u(1..3), ' ', h(%(a => 1)), ' ', h(Map.new((a => 1)));
    sub c(@a is copy) { @a.push(3); @a.^name ~ " " ~ @a.elems }
    my @y = 1, 2; say c(@y); say @y; say c((1, 2)); my $l = (5, 6); say c($l); say (try c($x)) // $!.^name;
    sub g(%h is copy) { %h<z> = 1; %h.elems }
    my %g = a => 1; say g(%g); say %g;
    END

my $want = (
    'X::TypeCheck::Binding::Parameter', 'X::TypeCheck::Binding::Parameter',
    q[Type check failed in binding to parameter '%h'; expected Associative but got Int],
    'X::TypeCheck::Binding::Parameter', 'X::TypeCheck::Binding::Parameter',
    '0', '2', 'X::TypeCheck::Binding::Parameter',
    '0', '2', 'X::TypeCheck::Binding::Parameter',
    '2', 'X::TypeCheck::Binding::Parameter',
    'Array List Range Hash Map',
    'Array 3', '[1 2]', 'Array 3', 'Array 3', 'X::TypeCheck::Binding::Parameter',
    '2', '{a => 1}',
).join('|');

my $i = run($*EXECUTABLE, $src.Str, :out, :err);
check('interpreted', $i.out.slurp(:close).lines.join('|'), $want);
$i.err.slurp(:close);

for ('', '-O') -> $o {
    my $tag = $o ?? "compiled $o" !! 'compiled';
    my $bin = $work.add('sigil' ~ $o);
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
