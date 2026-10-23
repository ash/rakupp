# BROKE: tools/run-roast.raku took a run that printed no plan for a pass when
# none of its tests had failed. Test prints the plan first (`plan N`) or last
# (`done-testing`), so such a run is one that died before its `done-testing`:
# S32-str/indices.t died at its 19th test (`422.indices(2)`, no such method)
# and was `[PASS] 18/18` in every Raku++ run of Roast, and a fattened
# S32-str/val.t that Rakudo died in was `[PASS] 93/93` (found 2026-10-23).
# FIXED: a run is PASS only when it printed a plan and ran that many tests.
#
# Contract: exit 0 + last line PASS.

my @fail;
my $root = $*TMPDIR.add("run-roast-no-plan-$*PID");
$root.add('S01').mkdir;
$root.add('spectest.data').spurt("S01/done.t\nS01/dies.t\n");
$root.add('S01/done.t').spurt("use Test;\nok 1, 'a';\nok 1, 'b';\ndone-testing;\n");
$root.add('S01/dies.t').spurt("use Test;\nok 1, 'a';\nok 1, 'b';\ndie 'out';\ndone-testing;\n");
my $list = $root.add('pass.list');
my $tool = $?FILE.IO.parent.parent.parent.add('tools/run-roast.raku');
my $p = run $*EXECUTABLE, $tool, "--list=$list", :out, :err, :env(|%*ENV, ROAST => $root.Str);
my $out = $p.out.slurp(:close);
$p.err.slurp(:close);

@fail.push("the run finishes, got exit {$p.exitcode}") unless $p.exitcode == 0;
@fail.push("done.t is PASS: {$out.lines.grep(/'done.t'/).join}") unless $out ~~ / '[PASS]' \s+ '2/2' \s+ 'S01/done.t' /;
@fail.push("dies.t is part: {$out.lines.grep(/'dies.t'/).join}") unless $out ~~ / '[part]' \s+ '2/2' \s+ 'S01/dies.t' /;
@fail.push("the list holds done.t alone: {$list.e ?? $list.slurp.raku !! 'none'}")
    unless $list.e && $list.slurp.lines.List eqv ('S01/done.t',);

for $root.add('S01').dir { .unlink }
$root.add('S01').rmdir;
.unlink for $root.dir;
$root.rmdir;

if @fail { note $_ for @fail; say 'FAIL' }
else { say 'PASS' }
