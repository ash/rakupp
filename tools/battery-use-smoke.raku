#!/usr/bin/env rakupp
# The battery use smoke: does `use` of any module in the battery crash or hang?
#
#   rakupp tools/battery-use-smoke.raku                        # the rakupp Gate picks
#   RAKUPP=/path/to/rakupp rakupp tools/battery-use-smoke.raku # a named binary
#
# Options: --cap=SECONDS per module (default 60), --only=TEXT (distributions
# whose directory name contains TEXT), --out=FILE (every module's result as a TSV).
#
# Every module a battery distribution's META6.json `provides` is loaded with
# `use` in a fresh process: the distribution's own lib first, every other
# distribution's lib on RAKULIB, HOME in a scratch directory, stdin closed, and
# on macOS inside the battery's sandbox. A module that dies with an error is
# counted and not judged here — the engine refused it cleanly, and whether
# Rakudo loads it is the ecosystem sweep's question. What this gate judges is a
# process that ends by a SIGNAL (a segfault, an abort, a bus error) or does not
# end within the cap. `use Text::Markdown` exiting 139 is the case it was
# written for (V5-PLAN B2).
#
# Sequential by design.
#
# Exit 0: no signals and no hangs. Exit 1: some, listed. Exit 2: could not
# judge — never a pass.

use lib $?FILE.IO.parent.add('lib').Str;
use Gate;
use Battery;

my $TOOL = 'battery-use-smoke';
my ($out-file, $only) = Str, Str;
my $cap = 60;
for @*ARGS -> $a {
    if    $a ~~ / ^ '--cap=' (\d+) $ / { $cap = +$0 }
    elsif $a ~~ / ^ '--only=' (.+) $ / { $only = ~$0 }
    elsif $a ~~ / ^ '--out=' (.+) $ /  { $out-file = ~$0 }
    else { note "$TOOL: unknown argument: $a"; exit 2 }
}

my $ROOT = battery-root(:tool($TOOL));
my %PICK = pick-rakupp($?FILE.IO.parent.parent);
require-native(%PICK, :tool($TOOL), :verdict<INCONCLUSIVE>);
my $ENGINE = %PICK<path>.IO.absolute;

my @dists = battery-dists($ROOT);
@dists = @dists.grep(*.basename.contains($only)) if $only;
my @jobs = @dists.map(-> $d { |provided-modules($d).map({ %( dist => $d, module => $_ ) }) });
my @no-meta = @dists.grep({ !provided-modules($_) });
unless @jobs { note "$TOOL: no modules found in the META6.json files under $ROOT/dists"; exit 2 }

my $git = run('git', '-C', $ROOT.Str, 'log', '-1', '--format=%h', :out, :err);
my $REV = $git.out.slurp(:close).trim || 'unknown'; $git.err.slurp(:close);

say provenance-line($TOOL, %PICK);
say "$TOOL: battery $ROOT ($REV), {@dists.elems} distributions, {@jobs.elems} modules, cap {$cap} s";
say "$TOOL: {@no-meta.elems} distributions name no modules: {@no-meta.map(*.basename).join(', ')}" if @no-meta;

my $WORK = $*TMPDIR.add("battery-use-smoke-{$*PID}");
my $HOME = $WORK.add('home');
.mkdir for $WORK, $HOME;
my $PROBE = $WORK.add('probe.raku');
my $SANDBOX = $ROOT.add('harness').add('battery.sb');
my $RAKULIB = battery-rakulib($ROOT);
my $MARK = 'USE-SMOKE-LOADED';

my @rows;
my %count = loaded => 0, error => 0, signal => 0, hang => 0;
my $t0 = now;
for @jobs.kv -> $i, %j {
    $PROBE.spurt: "use {%j<module>};\nprint '$MARK';\n";
    my %r = run-capped([$ENGINE, '-I', %j<dist>.add('lib').absolute, $PROBE.absolute],
        :$cap, :work($WORK), :sandbox($SANDBOX),
        :env(HOME => $HOME.absolute, RAKULIB => $RAKULIB, TZ => 'UTC'));
    my ($verdict, $note) =
        %r<hung>             ?? ('HANG',   "no answer in {$cap} s") !!
        %r<signal>           ?? ('SIGNAL', signal-name(%r<signal>)) !!
        %r<out>.contains($MARK) ?? ('loaded', '') !!
                                ('error',  first-line(%r));
    %count{$verdict.lc}++;
    @rows.push: [%j<dist>.basename, %j<module>, $verdict, %r<ms>, $note];
    if $verdict eq 'HANG' | 'SIGNAL' {
        say "{$verdict.fmt('%-7s')} {%j<module>}  ({%j<dist>.basename}): $note";
        say "        last words: {first-line(%r)}" if first-line(%r);
    }
    note "$TOOL: {$i + 1}/{@jobs.elems} modules, {%count<signal>} signals, {%count<hang>} hangs, "
       ~ "{((now - $t0) / 60).round(0.1)} min"
        if ($i + 1) %% 250;
}

if $out-file {
    my $fh = open $out-file, :w;
    $fh.say: "dist\tmodule\tverdict\tms\tnote";
    $fh.say: .join("\t") for @rows;
    $fh.close;
    say "$TOOL: every module's result in $out-file";
}
run 'rm', '-rf', $WORK.absolute;

my $bad = %count<signal> + %count<hang>;
say "$TOOL: {@jobs.elems} modules in {((now - $t0) / 60).round(0.1)} min — "
  ~ "{%count<loaded>} load, {%count<error>} refuse with an error, "
  ~ "{%count<signal>} end by a signal, {%count<hang>} hang";
say $bad ?? "$TOOL: RED — {$bad} of {@jobs.elems} modules crash or hang on `use`"
         !! "$TOOL: GREEN — no module crashes or hangs on `use`";
exit $bad ?? 1 !! 0;
