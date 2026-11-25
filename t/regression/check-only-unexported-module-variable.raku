# Regression: `-c` refuses a variable that a `use`d module declares but does
# not export (#282).
#
# The compile-time declaration check cleared a name as soon as an imported
# module's source mentioned it anywhere, so `use Z; say $Q` with Z's plain
# `our $Q` was "Syntax OK" under `-c` and died X::Undeclared only when run.
# Only an exported name reaches the importer, and only `is export` clears one
# now (a module with no `unit` package puts its `our` names in GLOBAL, which
# the importer sees, so there a mention still clears one). Rakudo refuses the
# unexported name at compile time, as `-c` does here.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
my $dir = $*TMPDIR.add("check-unexported-{$*PID}");
$dir.mkdir;
$dir.add('Z.rakumod').spurt: "unit module Z;\nour \$Q = 1;\nour \$E is export = 2;\n";
$dir.add('A.rakumod').spurt: "unit module A;\nour \$X is export = 1;\n";
$dir.add('Bare.rakumod').spurt: "our \$LEVEL = 8;\n";   # no package: GLOBAL, seen by the importer
$dir.add('B.rakumod').spurt: "unit module B;\nuse A;\nsub b() \{ say \$X }\n";
my %case =
    'Y.rakumod'  => ["unit module Y;\nuse Z;\nsub y() \{ say \$Q }\n",       False],  # not exported
    'Y2.rakumod' => ["unit module Y2;\nuse Z;\nsub y() \{ say \$E }\n",      True],   # exported
    'C.rakumod'  => ["unit module C;\nuse B;\nsub c() \{ say \$X }\n",       False],  # not re-exported
    'W.rakumod'  => ["unit module W;\nuse Z;\nsub w() \{ say \$NOSUCH }\n",  False],  # nowhere
    'p.raku'     => ["use Z;\nsay \$Q;\n",                                  False],
    'q.raku'     => ["use Bare;\nsay \$LEVEL;\n",                           True];   # a package-less module
for %case.sort -> (:key($file), :value([$src, $ok])) {
    $dir.add($file).spurt: $src;
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-c', ~$dir.add($file), :out, :err);
    my $out = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    check $out.contains('Syntax OK'), $ok, "$file says Syntax OK";
    check $out.contains('is not declared'), !$ok, "$file reports the variable";
}
sub rm-tree(IO::Path $p) { if $p.d { rm-tree($_) for $p.dir; $p.rmdir } else { $p.unlink } }
rm-tree($dir);   # (the modules' .precomp too)

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
