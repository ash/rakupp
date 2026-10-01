# Regression: a module that fills its EXPORT::TAG packages by hand exported
# nothing. Sparrow6::DSL re-exports its whole DSL this way:
#
#     my package EXPORT::DEFAULT { }
#     BEGIN for <&config &bash …> { EXPORT::DEFAULT::{$_} = ::($_) }
#
# and `use Sparrow6::DSL; bash(…)` died "Undefined routine 'bash'". rakupp
# never read those packages; the program got the names only because every
# routine a module defined or imported also leaked into GLOBAL. 2cc896bd
# ("Imports stay lexical") closed the leak, and the re-export went with it.
# The angle spelling `EXPORT::DEFAULT::<&f> = &f` wrote yet another slot —
# the unqualified `&EXPORT::DEFAULT::f`, not the module's own package.
#
# The table is Rakudo's (rakudo 2026.09): a plain `use` takes DEFAULT and
# MANDATORY, a named tag takes that package and MANDATORY, and `:ALL` reads
# the EXPORT::ALL package only — not the hand-filled DEFAULT or `extra`.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $dir = $*TMPDIR.add("export-hand-built-{$*PID}");
$dir.mkdir;
$dir.add('HandBuiltX.rakumod').spurt: q:to/MOD/;
    unit module HandBuiltX;
    sub angle() { "a" }
    sub curly() { "c" }
    sub xtra()  { "x" }
    sub mand()  { "m" }
    sub plain() is export { "p" }
    my package EXPORT::DEFAULT { }
    my package EXPORT::extra { }
    my package EXPORT::MANDATORY { }
    BEGIN { EXPORT::DEFAULT::<&angle> = &angle; EXPORT::DEFAULT::<KONST> = 42 }
    BEGIN for <&curly> { EXPORT::DEFAULT::{$_} = ::($_) }
    BEGIN for <&xtra> { EXPORT::extra::{$_} = ::($_) }
    BEGIN { EXPORT::MANDATORY::<&mand> = &mand }
    MOD

my @names = <&angle &curly &xtra &mand &plain KONST>;
sub visible(Str $form --> Str) {
    my $code = "my @N = <{@names.join(' ')}>; use HandBuiltX $form; "
             ~ 'print @N.map({ (try ::($_)).defined ?? "Y" !! "-" }).join';
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $code, :out, :err);
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $out
}

#                     angle curly xtra mand plain KONST
check visible(''),          'YY-YYY', 'plain use';
check visible(':DEFAULT'),  'YY-YYY', ':DEFAULT';
check visible(':extra'),    '--YY--', ':extra';
check visible(':ALL'),      '---YY-', ':ALL';

# The import is lexical, and a second `use` in another scope imports again.
my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', q:to/PROG/, :out, :err);
    sub one() { use HandBuiltX; angle() ~ curly() }
    sub two() { use HandBuiltX :extra; xtra() }
    print one(), two(), one(), (try ::('&angle')).defined ?? "LEAK" !! "";
    PROG
check $p.out.slurp(:close), 'acxac', 'lexical, repeated';
$p.err.slurp(:close);

sub nuke(IO::Path $p) {
    if $p.d { nuke($_) for $p.dir; try $p.rmdir }
    else { try $p.unlink }
}
nuke($dir);

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say "PASS";
