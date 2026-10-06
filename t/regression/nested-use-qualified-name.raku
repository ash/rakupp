# Regression: a `use` inside a routine or block kept the WHOLE qualified name
# of what it loaded lexical, so `G::Path` was unreachable after it even where
# `G` itself was in scope. Graph::Classes loads every Graph subclass inside its
# `sub EXPORT`, and `use Graph; use Graph::Classes; Graph::Path.new(…)` died
# "Could not find symbol '&Path' in 'GLOBAL::Graph'" (issue #47: Graph would
# not install). 2cc896bd ("Imports stay lexical") made what a nested `use`
# declares the importing scope's alone.
#
# What stays lexical is the top-level NAME; the package's contents merge into
# GLOBAL. Rakudo 2026.09 on each program below: `G::Path` resolves through an
# `G` visible some other way, and stays unknown when the nested load is the
# only thing that brought `G`.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $dir = $*TMPDIR.add("nested-use-qualified-{$*PID}");
$dir.add('G').mkdir;
$dir.add('G.rakumod').spurt: "class G \{ method hi \{ 'G' } }\n";
$dir.add('G/Path.rakumod').spurt: "use G;\nclass G::Path is G \{ method hi \{ 'G::Path' } }\n";
$dir.add('G/Classes.rakumod').spurt: q:to/MOD/;
    sub EXPORT {
        use G;
        use G::Path;
        Map.new: 'G' => G, 'G::Path' => G::Path
    }
    MOD
$dir.add('G/InBlock.rakumod').spurt: "unit module G::InBlock;\n\{ use G::Path; }\n";

sub run-prog(Str $code --> Str) {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $code, :out, :err);
    my $out = $p.out.slurp(:close).trim;
    my $err = $p.err.slurp(:close);
    $out || ($err.lines.head // '')
}

check run-prog('use G; use G::Classes; print G::Path.new.hi'), 'G::Path', 'sub EXPORT loads it';
check run-prog('use G; use G::InBlock; print G::Path.new.hi'), 'G::Path', 'a block in a module loads it';
check run-prog('use G; { use G::Path; }; print G::Path.new.hi'), 'G::Path', 'a block in the program loads it';
check run-prog('{ use G::Path; }; print G::Path.new.hi').starts-with('Could not find symbol'), True,
      'no outer G: still unknown';

sub nuke(IO::Path $p) {
    if $p.d { nuke($_) for $p.dir; try $p.rmdir }
    else { try $p.unlink }
}
nuke($dir);

if @fail { die "FAIL:\n" ~ @fail.join("\n") }
say "PASS";
