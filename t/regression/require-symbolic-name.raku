# Regression: `require ::($name)` stopped loading anything (found by the
# release's battery gate, which lost Text::Utils and every dist that loads a
# driver or a font by computed name).
#
# `require` EVALUATED its operand, so the symbolic ref was resolved as a
# SYMBOL first — and the module it names is by definition not loaded yet. While
# unknown capitalized names still stubbed themselves into a type object that
# happened to carry the right name, this worked by accident; once they began
# failing softly (the v4 arc's type-registry work, 3e23886) the resulting
# Failure arrived at the loader STRINGIFIED, and the error read
#
#     Could not find No such symbol 'Font::Metrics::courier' in the module search path
#
# `require ::($name)` now takes the NAME the ref spells and hands that to the
# loader, which is what the operand was always for.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

my $dir = $*TMPDIR.add("rakupp-require-{$*PID}");
$dir.add('Dyn').mkdir;
$dir.add('Dyn/Loaded.rakumod').spurt: q:to/MOD/;
    unit class Dyn::Loaded;
    method greet() { 'loaded dynamically' }
    MOD

LEAVE { run 'rm', '-rf', $dir.Str }

sub probe($code) {
    my %env = %*ENV;
    %env<RAKULIB> = $dir.Str;
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err, :%env;
    my $out = $p.out.slurp(:close); $p.err.slurp(:close);
    ($out.trim, $p.exitcode)
}

my ($out, $exit) = probe 'my $n = "Dyn::Loaded"; my \M = (require ::($n)); say M.greet';
ok($exit == 0, "require ::(\$name) exits clean (got $exit)");
ok($out eq 'loaded dynamically', "the required module is usable (got '$out')");

# the literal form must keep working, and so must the failure path. (The
# required name is NOT installed for the compiler, in either engine — the
# module's type comes back as require's value, which is what `\M =` is for.)
my ($o2, $e2) = probe 'my \M = (require ::("Dyn::Loaded")); say M.greet';
ok($e2 == 0 && $o2 eq 'loaded dynamically', "a literal symbolic name loads too (got '$o2')");

my ($o3, $e3) = probe 'my $n = "No::Such::Module::Here"; my $ok = (try require ::($n)).defined; say $ok ?? "loaded" !! "failed"';
ok($e3 == 0 && $o3 eq 'failed', "a missing module still fails, and try catches it (got '$o3')");

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
