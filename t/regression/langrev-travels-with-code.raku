# The language revision belongs to the code, not to the process.
#
# A sub compiled under `use v6.e.PREVIEW` keeps 6.e semantics when a 6.d
# program calls it, and a 6.d program does NOT acquire 6.e semantics by
# loading a 6.e module. Rakudo does the second one differently: `use`-ing a
# 6.e module loads CORE.e into the process, so its 6.d mainline starts
# returning Complex from sqrt too — even on lines above the `use`. That is a
# deliberate divergence, recorded in docs/dev/plans/6E-PLAN.md.
use Test;
plan 4;

my $dir = $*TMPDIR.add("langrev-{$*PID}");
$dir.mkdir;
my $mod = $dir.add('SixERev.rakumod');
$mod.spurt: q:to/MOD/;
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
say ("az".."bc").elems;
MAIN

my $out = run($*EXECUTABLE, ~$main, :out).out.slurp(:close).lines;
is $out[0], '0+2i',        'a 6.e module keeps 6.e semantics when a 6.d program calls it';
is $out[1], 'az,ba,bb,bc', '…including the ones that are not about numbers';
is $out[2], 'NaN',         'the 6.d mainline is not infected by loading a 6.e module';
# Still on by default: the .succ string range is one of the thirteen 6.e
# behaviours phase 2 of 6E-PLAN.md has to put behind the pragma.
todo 'string ranges are 6.e-by-default until P2 lands';
is $out[3], '52',          '…nor are its string ranges';

$dir.add($_).unlink for <SixERev.rakumod main.raku>;
$dir.rmdir;
