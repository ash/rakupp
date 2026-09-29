#!/usr/bin/env rakupp
# The battery compile scan: which module files that the last release compiles
# does the candidate refuse?
#
#   rakupp tools/battery-scan.raku                        # candidate: the rakupp Gate picks; reference: the latest release
#   rakupp tools/battery-scan.raku --ref-release=v5.0.0   # a named release as the reference
#   rakupp tools/battery-scan.raku --ref=/path/to/rakupp  # any binary as the reference
#   RAKUPP=/path/to/rakupp rakupp tools/battery-scan.raku # a named candidate
#
# Options: --cap=SECONDS per file (default 30), --only=TEXT (distributions whose
# directory name contains TEXT), --out=FILE (every file's result as a TSV).
#
# Every `lib/` module file of every distribution in the module battery (see
# tools/lib/Battery.rakumod for where it is looked for) is compiled with `-c`,
# in a fresh process, with the distribution's own lib first and every other
# distribution's lib on RAKULIB, HOME in a scratch directory so no installed
# module can answer, and on macOS inside the battery's sandbox. The candidate
# runs on every file; the reference only on the files the candidate fails,
# which is all the comparison needs and halves the time.
#
# A REGRESSION is a file the reference compiles and the candidate does not. The
# reference is the latest release's own downloaded asset, not a build of the
# tag, because that binary is the one users run (V5-PLAN B0). It is fetched once
# into a cache with `gh`, and checked against its published sha256.
#
# This is the gate for any change that adds a check to the parser or the
# compiler: parse-time strictness added while chasing Roast files is how four
# ecosystem regressions arrived before v5 (V5-PLAN, "Regressions").
#
# What `-c` covers differs between the engines. Rakudo's loads every `use`d
# module and runs BEGIN blocks; rakupp's parses the file and loads nothing, so
# a `use` of a module that does not exist still says "Syntax OK". The scan is
# therefore a PARSER gate, which is the question the checks it guards raise.
# Whether the modules load is tools/battery-use-smoke.raku's question.
#
# Sequential by design: it is meant to run beside other work. The battery is
# about 2,800 module files, and the scan takes a few minutes.
#
# Exit 0: no regressions. Exit 1: regressions, listed. Exit 2: could not judge
# (no battery, no binary, no release) — never a pass.

use lib $?FILE.IO.parent.add('lib').Str;
use Gate;
use Battery;

my $TOOL = 'battery-scan';
my ($ref, $ref-release, $out-file, $only) = Str, Str, Str, Str;
my $cap = 30;
for @*ARGS -> $a {
    if    $a ~~ / ^ '--ref=' (.+) $ /         { $ref = ~$0 }
    elsif $a ~~ / ^ '--ref-release=' (.+) $ / { $ref-release = ~$0 }
    elsif $a ~~ / ^ '--cap=' (\d+) $ /        { $cap = +$0 }
    elsif $a ~~ / ^ '--only=' (.+) $ /        { $only = ~$0 }
    elsif $a ~~ / ^ '--out=' (.+) $ /         { $out-file = ~$0 }
    else { note "$TOOL: unknown argument: $a"; exit 2 }
}

my $ROOT = battery-root(:tool($TOOL));
my %PICK = pick-rakupp($?FILE.IO.parent.parent);
require-native(%PICK, :tool($TOOL), :verdict<INCONCLUSIVE>);
my $CAND = %PICK<path>.IO.absolute;

without $ref {
    my $tag = $ref-release // latest-release();
    unless $tag {
        note "$TOOL: cannot tell the latest release (is `gh` installed and signed in?);";
        note "pass --ref-release=vX.Y.Z or --ref=/path/to/rakupp.";
        exit 2;
    }
    $ref = release-binary($tag, :tool($TOOL));
}
unless $ref.IO.x { note "$TOOL: the reference $ref is not runnable"; exit 2 }
$ref = $ref.IO.absolute;

my @dists = battery-dists($ROOT);
@dists = @dists.grep(*.basename.contains($only)) if $only;
my @files = @dists.map({ |lib-files($_) });
unless @files { note "$TOOL: no module files found under $ROOT/dists"; exit 2 }

my $git = run('git', '-C', $ROOT.Str, 'log', '-1', '--format=%h', :out, :err);
my $REV = $git.out.slurp(:close).trim || 'unknown'; $git.err.slurp(:close);

say provenance-line($TOOL, %PICK);
say "$TOOL: reference $ref (rakupp {binary-version($ref)})";
say "$TOOL: battery $ROOT ($REV), {@dists.elems} distributions, {@files.elems} module files, cap {$cap} s";

my $WORK = $*TMPDIR.add("battery-scan-{$*PID}");
my %HOME = cand => $WORK.add('home-cand'), ref => $WORK.add('home-ref');
.mkdir for $WORK, |%HOME.values;
my $SANDBOX = $ROOT.add('harness').add('battery.sb');
my $RAKULIB = battery-rakulib($ROOT);

sub compile(Str $engine, Str $which, IO::Path $file --> Hash) {
    my $lib = $ROOT.add('dists').add($file.relative($ROOT.add('dists')).split('/')[0]).add('lib');
    my %r = run-capped([$engine, '-c', '-I', $lib.absolute, $file.absolute],
        :$cap, :work($WORK), :sandbox($SANDBOX),
        :env(HOME => %HOME{$which}.absolute, RAKULIB => $RAKULIB, TZ => 'UTC'));
    %r<ok> = %r<rc> == 0 && %r<signal> == 0;
    %r<note> = %r<hung> ?? "(no answer in {$cap} s)"
            !! %r<signal> ?? "(killed by {signal-name(%r<signal>)})"
            !! first-line(%r);
    %r
}

my @rows;
my ($n-ok, $n-both, $n-reg) = 0, 0, 0;
my $t0 = now;
for @files.kv -> $i, $f {
    my $rel = $f.relative($ROOT.add('dists'));
    my %c = compile($CAND, 'cand', $f);
    if %c<ok> {
        $n-ok++;
        @rows.push: [$rel, 'ok', '', ''];
    }
    else {
        my %r = compile($ref, 'ref', $f);
        if %r<ok> {
            $n-reg++;
            @rows.push: [$rel, 'REGRESSION', %c<note>, 'Syntax OK'];
            say "REGRESSION  $rel";
            say "            {%c<note>}";
        }
        else {
            $n-both++;
            @rows.push: [$rel, 'fails-both', %c<note>, %r<note>];
        }
    }
    note "$TOOL: {$i + 1}/{@files.elems} files, {$n-reg} regressions, {((now - $t0) / 60).round(0.1)} min"
        if ($i + 1) %% 250;
}

if $out-file {
    my $fh = open $out-file, :w;
    $fh.say: "file\tverdict\tcandidate {%PICK<version>}\treference {binary-version($ref)}";
    $fh.say: .join("\t") for @rows;
    $fh.close;
    say "$TOOL: every file's result in $out-file";
}
run 'rm', '-rf', $WORK.absolute;

say "$TOOL: {@files.elems} files in {((now - $t0) / 60).round(0.1)} min — "
  ~ "{$n-ok} compile, {$n-both} fail under both, {$n-reg} regressions";
say $n-reg ?? "$TOOL: RED — {$n-reg} files the reference compiles fail under the candidate"
           !! "$TOOL: GREEN — no file the reference compiles fails under the candidate";
exit $n-reg ?? 1 !! 0;
