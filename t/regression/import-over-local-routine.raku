# A routine a `use` imports into a scope that declares the same name is
# X::Redeclaration, in either order (issue #230): `use Other; sub run-word {…}`
# in a module compiled and ran, the import stepped aside for the local sub, and
# in a larger module a call reached the wrong one. Not a clash: two multis
# (they merge), an imported stub the local sub completes, a selective export
# the `use` did not ask for, `need` and `use M ()`, a `use` in another scope,
# and two imports of one name (in the mainline the last wins). The mainline's
# own routines are reached through child processes, as an EVAL is a scope of
# its own.
# Every line answers the same under Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my $dir = $*TMPDIR.add("import-over-local-{$*PID}");
$dir.mkdir;
END { run 'rm', '-rf', ~$dir if $dir.e }
my %mods =
    'Other'   => q:to/M/,
        unit module Other;
        sub run-word(Str $n --> Str) is export { "other:$n" }
        M
    'Other2'  => q:to/M/,
        unit module Other2;
        sub run-word(Str $n --> Str) is export { "other2:$n" }
        M
    'Mine'    => q:to/M/,
        unit module Mine;
        use Other;
        sub run-word(Str $s --> Str) { "mine:$s" }
        sub speak(--> Str) is export { run-word('x') }
        M
    'Kinds'   => q:to/M/,
        unit module Kinds;
        sub only-one($x) is export { "imp:$x" }
        multi sub mul(Int $x) is export { "imp-int:$x" }
        sub stubbed($x) is export { ... }
        sub tagged($x) is export(:extra) { "imp-tagged:$x" }
        M
    'ViaExport' => q:to/M/,
        sub EXPORT() { %( '&via' => sub ($x) { "exp:$x" } ) }
        M
    ;
# (one module per first load: a module's first `use` and a later one import by two roads)
%mods{"Fresh$_"} = "unit module Fresh$_;\nsub run-word(\$x) is export \{ 'fresh' }\n" for 1..4;
$dir.add("$_.key().rakumod").spurt($_.value) for %mods;
my $inc = ~$dir;

# the redeclared name as the message gives it ('&f' for a symbol, 'f' for a
# routine), or 'lived' and the program's answer. (By the message: Rakudo wraps
# a module's compile error in the `use` that loaded it, or groups it.)
sub ev(Str $code) {
    my $r = try EVAL "use lib '$inc'; $code";
    $! ?? ($!.message ~~ / 'Redeclaration of ' \w+ " '" (<-[']>+) "'" / ?? ~$0 !! $!.^name) !! "lived:$r"
}

check(ev('use Fresh1; sub run-word($x) { "loc" }; run-word(1)'), '&run-word', 'use, then sub (first load)');
check(ev('sub run-word($x) { "loc" }; use Fresh2; run-word(1)'), '&run-word', 'sub, then use (first load)');
check(ev('use Fresh1; sub run-word($x) { "loc" }; run-word(1)'), '&run-word', 'use, then sub (module loaded)');
check(ev('sub run-word($x) { "loc" }; use Fresh2; run-word(1)'), '&run-word', 'sub, then use (module loaded)');
check(ev('use Fresh3; say run-word(1); sub run-word($x) { "loc" }'), '&run-word', 'a sub declared after its first call');
check(ev('use Other; my sub run-word($x) { "loc" }; 1'), '&run-word', 'a `my sub`');
check(ev('use Other; our sub run-word($x) { "loc" }; 1'), '&run-word', 'an `our sub`');
check(ev('use Mine; speak()'), '&run-word', "the issue's module: its own sub beside the import");
check(ev('sub f { use Other; sub run-word($x) { "loc" }; run-word(1) }; f()'), '&run-word', 'in a routine body');
check(ev('class C { use Other; sub run-word($x) { "loc" }; method m { run-word(1) } }; C.m'), '&run-word',
      'in a class body');
check(ev('module M { use Fresh4; sub run-word($x) { "loc" } }; 1'), '&run-word', 'in a package block');
check(ev('module M { sub run-word($x) is export { "M" } }; import M; sub run-word($x) { "loc" }; 1'),
      '&run-word', '`import` of a package');

# what the `use` did not import is no clash
check(ev('need Other; sub run-word($x) { "loc" }; run-word(1)'), 'lived:loc', '`need` imports nothing');
check(ev('use Other (); sub run-word($x) { "loc" }; run-word(1)'), 'lived:loc', '`use M ()` imports nothing');
check(ev('use Kinds; sub tagged($x) { "loc" }; tagged(1)'), 'lived:loc', 'a tag the `use` did not ask for');
check(ev('use Kinds :extra; sub tagged($x) { "loc" }; 1'), '&tagged', 'a tag it asked for');
check(ev('use Kinds :ALL; sub tagged($x) { "loc" }; 1'), '&tagged', ':ALL');
check(ev('{ use Other; sub run-word($x) { "loc" }; run-word(1) }'), '&run-word', 'both in one inner block');
check(ev('sub run-word($x) { "loc" }; (do { use Other; run-word("a") }) ~ "," ~ run-word(2)'), 'lived:other:a,loc',
      'a `use` in an inner block shadows the outer sub');
check(ev('use Other; (do { sub run-word($x) { "loc" }; run-word(1) }) ~ "," ~ run-word("b")'), 'lived:loc,other:b',
      'a sub in an inner block shadows the import');
check(ev('use Other; use Other; run-word("a")'), 'lived:other:a', 'the same import twice');

# multis, protos and stubs
check(ev('use Kinds; multi sub mul(Str $s) { "loc:$s" }; mul(1) ~ "," ~ mul("a")'), 'lived:imp-int:1,loc:a',
      'two multis merge');
check(ev('use Kinds; sub mul($x) { "loc" }; 1'), '&mul', 'a sub beside an imported multi');
check(ev('use Kinds; multi sub only-one(Int $x) { "loc" }; 1'), 'only-one', 'a multi beside an imported sub');
check(ev('use Kinds; proto sub mul($) {*}; multi sub mul(Str $s) { "loc" }; 1'), 'mul',
      'a proto beside an imported multi');
check(ev('use Kinds; sub stubbed($x) { "loc:$x" }; stubbed(1)'), 'lived:loc:1', 'a local sub completes an imported stub');
check(ev('use Kinds; sub only-one($x) { ... }; 1'), '&only-one', 'a local stub beside a real import');
check(ev('use ViaExport; sub via($x) { "loc" }; 1'), '&via', 'a sub EXPORT');
check(ev('use ViaExport; sub via($x) { "loc" }; 1'), '&via', 'a sub EXPORT (module loaded)');

{
    try EVAL "use lib '$inc'; use Kinds; sub only-one(\$x) \{ }";
    check(($!.^name, $!.symbol, $!.what, $!.message), ('X::Redeclaration', '&only-one', 'symbol',
          "Redeclaration of symbol '&only-one'."), 'the exception');
    try EVAL "use lib '$inc'; use Kinds; multi sub only-one(Int \$x) \{ }";
    check(($!.symbol, $!.what, $!.message.starts-with("Redeclaration of routine 'only-one'.")),
          ('only-one', 'routine', True), "a local multi's exception");
}

# the mainline: the program's own routines
my $prog = $dir.add('prog.raku');
sub main(Str $code) {
    $prog.spurt($code);
    my $p = run $*EXECUTABLE, '-I', $inc, ~$prog, :out, :err;
    my $err = $p.err.slurp(:close);
    my $out = $p.out.slurp(:close).trim;
    $p.exitcode == 0 ?? "lived:$out" !! $err.contains("Redeclaration of symbol '&run-word'.") ?? 'redeclared' !! $err
}
check(main("use Other;\nsub run-word(\$x) \{ 'loc' }\nsay run-word(1);\n"), 'redeclared', 'mainline: use, then sub');
check(main("sub run-word(\$x) \{ 'loc' }\nuse Other;\nsay run-word(1);\n"), 'redeclared', 'mainline: sub, then use');
check(main("\{ use Other; }\nuse Other;\nsub run-word(\$x) \{ 'loc' }\n"), 'redeclared',
      'mainline: a repeat use');
check(main("use Mine;\nsay speak();\n"), 'redeclared', "mainline: the issue's program");
check(main("use Kinds;\nsub tagged(\$x) \{ 'loc' }\nsay tagged(1);\n"), 'lived:loc', 'mainline: a tag not asked for');
check(main("use Other;\nsay run-word('m');\n"), 'lived:other:m', 'mainline: an import alone');
check(main("use Other;\nuse Other2;\nsay run-word('m');\n"), 'lived:other2:m', 'mainline: two imports, the last wins');

say @fail ?? "FAIL\n" ~ @fail.join("\n") !! 'PASS';
exit(@fail ?? 1 !! 0);
