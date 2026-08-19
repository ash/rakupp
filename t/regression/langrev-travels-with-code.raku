# Regression: the language revision belongs to the code, not to the process.
#
# A sub compiled under `use v6.e.PREVIEW` keeps 6.e semantics when a 6.d program
# calls it, and a 6.d program does NOT acquire 6.e semantics by loading a 6.e
# module. Rakudo does the second one differently: `use`-ing a 6.e module loads
# CORE.e into the process, so its 6.d mainline starts returning Complex from
# sqrt too — even on lines above the `use`. Deliberate divergence, recorded in
# docs/dev/plans/6E-PLAN.md.
#
# Not asserted here: ("az".."bc") still gives 4 elements under 6.d where Rakudo
# gives 52. 6.d's answer is a per-position cross product, an algorithm this
# engine does not have (the succ chain it uses is 6.e's), so there is nothing to
# gate yet. Tracked in the plan.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $dir = $*TMPDIR.add("langrev-{$*PID}");
$dir.mkdir;
$dir.add('SixERev.rakumod').spurt: q:to/MOD/;
use v6.e.PREVIEW;
unit module SixERev;
sub sixe-sqrt($n) is export { $n.sqrt }
sub sixe-range() is export { ("az".."bc").join(",") }
MOD

my $main = $dir.add('main.raku');
$main.spurt: qq:to/MAIN/;
use lib '$dir';
use SixERev;
say sixe-sqrt(-4);
say sixe-range();
say (-4).sqrt;
MAIN

my @out = run($*EXECUTABLE, ~$main, :out).out.slurp(:close).lines;
check @out[0], '0+2i',        'a 6.e module keeps 6.e semantics when a 6.d program calls it';
check @out[1], 'az,ba,bb,bc', 'including the ones that are not about numbers';
check @out[2], 'NaN',         'the 6.d mainline is not infected by loading a 6.e module';

$dir.add($_).unlink for <SixERev.rakumod main.raku>;
$dir.rmdir;

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
