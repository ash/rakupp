# Issue #202: a package's Stash held its `our` variables as inert cell
# holders. An `our` variable's slot is its package's one cell (ourCell), and
# `.WHO` copied the slot itself into the Stash, so `Circle::{'$pi'}` read
# (Any) where `$Circle::pi` and `Circle::<$pi>` read 3.14 — and so did every
# other way into the Stash: a computed key, `.AT-KEY`, `.pairs`, a Stash held
# in a variable. The Stash now holds the variable's container, so a write
# through it reaches the variable too.
#
# The same report: a module that exports nothing listed an EXPORT package
# (`Circle::.keys` was ($pi EXPORT)). The loader named `Mod::EXPORT` for every
# module; Rakudo's EXPORT package is made by `is export`, so only a module
# that exports something has one.
#
# Contract: exit 0 + last line PASS. Every case is oracle-checked against
# Rakudo 2026.09 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# the issue's own module and program, verbatim
my $dir = $*TMPDIR.add("issue202-{$*PID}");
$dir.mkdir;
$dir.add('I202Circle.rakumod').spurt: q:to/END/;
    unit module I202Circle;

    our $pi = 3.14;
    END
my $lookup = q:to/END/;
    use I202Circle;
    say I202Circle::{'$pi'};
    say I202Circle::.keys.elems;
    say I202Circle::.keys.sort;
    END
for <cold cached> -> $run {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $lookup, :out, :err);
    my $err = $p.err.slurp(:close);
    check $p.out.slurp(:close).lines.List, ('3.14', '1', '($pi)'),
        "the issue's lookup, $run run{$err ?? " ($err.lines.head())" !! ''}";
}

# every way into the Stash reads the variable, and a write through it is one
{
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', q:to/END/, :out, :err);
        use I202Circle;
        my $k = '$pi';
        my $st = I202Circle::;
        print I202Circle::{$k}, ' ', I202Circle::.AT-KEY('$pi'), ' ', $st{'$pi'}, ' ',
              I202Circle::.pairs.map({ .key ~ '=' ~ .value }), ' ', I202Circle::.values;
        I202Circle::{'$pi'} = 3;
        print ' ', $I202Circle::pi, ' ', I202Circle::<$pi>;
        $I202Circle::pi = 4;
        say ' ', I202Circle::{'$pi'}, ' ', $st<$pi>;
        END
    my $err = $p.err.slurp(:close);
    check $p.out.slurp(:close).trim, '3.14 3.14 3.14 $pi=3.14 3.14 3 3 4 4',
        "reads and writes through the Stash{$err ?? " ($err.lines.head())" !! ''}";
}

# …and compiled: native code made a type object named `I202Circle::`, whose
# `.keys` was () (rakupp only — `--exe` is ours)
if $*RAKU.compiler.name eq 'Raku++' && !$*DISTRO.is-win {
    my $src = $dir.add('lookup.raku');
    $src.spurt: $lookup;
    my $exe = $dir.add('lookup.bin');
    my $c = run($*EXECUTABLE, '--exe', '-I', ~$dir, ~$src, '-o', ~$exe, :out, :err);
    my $note = $c.out.slurp(:close) ~ $c.err.slurp(:close);
    check so($note ~~ /'Compiled (native)'/), True, "the lookup compiles natively (said: $note.trim())";
    if $exe.e {
        my $r = run(~$exe, :out, :err);
        my $err = $r.err.slurp(:close);
        check $r.out.slurp(:close).lines.List, ('3.14', '1', '($pi)'),
            "the issue's lookup, compiled{$err ?? " ($err.lines.head())" !! ''}";
    }
}

# …in a package of the program's own, for each sigil
module I202Local { our $v = 7; our @a = 1, 2; our %h = a => 1 }
check I202Local::{'$v'}, 7, 'a local package: $';
check I202Local::{'@a'}.List, (1, 2), 'a local package: @';
check I202Local::{'%h'}<a>, 1, 'a local package: %';
I202Local::{'$v'} = 8;
check $I202Local::v, 8, 'a local package: a Stash write is the variable';

# EXPORT is listed exactly when the module exports something
my %modules =
    I202None   => ('our $x = 1;',                         False),
    I202Sub    => ('sub f() is export { 1 }',             True),
    I202Var    => ('our $x is export = 1;',               True),
    I202Const  => ('constant K is export = 3;',           True),
    I202Class  => ('class C is export { }',               True),
    I202Enum   => ('enum E is export <P Q>;',             True),
    I202Subset => ('subset S of Int is export where 1;',  True),
    I202Tagged => ('sub t() is export(:tag) { 1 }',       True),
    I202Braced => ('module Inner { sub g() is export { 1 } }', True),
    I202Own    => ('sub EXPORT { Map.new }',              False),
    ;
for %modules.kv -> $name, ($body, $) { $dir.add("$name.rakumod").spurt: "unit module $name;\n$body\n" }
my $uses = %modules.keys.sort.map({ "use $_;" }).join(' ');
for <cold cached> -> $run {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e',
        $uses ~ ' for @*ARGS { say ::($_).WHO<EXPORT>:exists }', |%modules.keys.sort, :out, :err);
    my $err = $p.err.slurp(:close);
    my @got = $p.out.slurp(:close).lines;
    for %modules.keys.sort.kv -> $i, $name {
        check @got[$i] // '', ~%modules{$name}[1],
            "$name has {%modules{$name}[1] ?? 'an' !! 'no'} EXPORT, $run run{$err ?? " ($err.lines.head())" !! ''}";
    }
}

for $dir.dir { .d ?? run('rm', '-rf', ~$_) !! .unlink }
$dir.rmdir;

.note for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
