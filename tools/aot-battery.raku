#!/usr/bin/env rakupp
# The native-module-bodies battery: do the module battery's own test files
# behave the same with `--exe`'s native module bodies as without them?
#
#   rakupp tools/aot-battery.raku                        # the distributions in tools/aot-battery.list
#   rakupp tools/aot-battery.raku --all                  # every distribution in the battery
#   rakupp tools/aot-battery.raku --only=JSON            # those whose directory name contains TEXT
#   rakupp tools/aot-battery.raku --out=FILE             # every test file as a TSV row; the outputs
#                                                        # of each one that differs in FILE.d/
#   rakupp tools/aot-battery.raku --one DIST t/FILE      # one test file: the three runs, and their diffs
#   rakupp tools/aot-battery.raku --bisect DIST t/FILE   # which native bodies make it differ
#   RAKUPP=/path/to/rakupp rakupp tools/aot-battery.raku # a named binary (the Gate's rules)
#
# A PRE-RELEASE gate, not an every-commit one: the default set is 49
# distributions, 436 test files, about an hour on one core (each file is a
# C++ compile). Sequential by design. t/aot/run.raku is the every-commit check
# of the same machinery; this one is what finds what t/aot does not know to ask.
#
# Each test file of each distribution is compiled with `--exe` (in a copy of
# the distribution, its lib first and every other distribution's lib on
# RAKULIB), and the binary is run twice — with its native module bodies, and
# with RAKUPP_NO_AOT=1, which runs the same binary's modules interpreted — and
# the program is run once more by the interpreter. Every run has HOME in a
# scratch directory, TZ=UTC, stdin closed, a time cap, and on macOS the
# battery's sandbox. Outputs are compared on exit code and stdout, with object
# addresses (`URI<51227…>`) normalized away.
#
# Verdicts, per test file:
#   same      the native bodies change nothing
#   AOTDIFF   they do — THE FAILURE (re-run once; a file whose RAKUPP_NO_AOT=1
#             output differs between two runs is FLAKY instead)
#   benign    an AOTDIFF the known-benign list below explains
#   FLAKY     the output is not stable run to run, so it says nothing
#   NOBIN     `--exe` did not build the file (counted, not judged here)
# and, informational only, EXEDIFF: the RAKUPP_NO_AOT=1 run differs from the
# interpreter — the compiled program itself, not its module bodies.
#
# --bisect finds the native bodies behind an AOTDIFF: RAKUPP_AOT_RANGE attaches
# only the bodies whose ordinal is in a range, so the smallest failing prefix
# 0-K and then the largest failing suffix J-K bound the culprits; the routines
# at J and K are named (RAKUPP_AOT_TRACE).
#
# Exit 0: no unexplained AOTDIFF. Exit 1: some, listed. Exit 2: could not judge.

use lib $?FILE.IO.parent.add('lib').Str;
use Gate;
use Battery;

my $TOOL = 'aot-battery';

# Differences that are not the native bodies' fault, by battery directory and
# test file. A listed file that stops differing is reported, so the list does
# not outlive its reasons.
my %BENIGN =
    "Digest-1.1.0\tt/md5.t"
        => 'names its tests with random strings: no two runs print the same',
    "Markdown--Lex-0.1.0\tt/04-streaming.rakutest"
        => 'prints its own timings',
    "Markdown--Lex-0.1.0\tt/05-pathological.rakutest"
        => 'prints its own timings',
    "Markdown--Lex-0.1.0\tt/01-blocks.rakutest"
        => 'the native body gives Rakudo\'s answer and the interpreter does not (an interpreter bug)',
    ;

my ($out-file, $only, $all, $mode) = Str, Str, False, 'battery';
my ($run-cap, $build-cap) = 60, 300;
my @pos;
for @*ARGS -> $a {
    if    $a ~~ / ^ '--only=' (.+) $ /  { $only = ~$0 }
    elsif $a ~~ / ^ '--out=' (.+) $ /   { $out-file = ~$0 }
    elsif $a ~~ / ^ '--cap=' (\d+) $ /  { $run-cap = +$0 }
    elsif $a eq '--all'                 { $all = True }
    elsif $a eq '--one'                 { $mode = 'one' }
    elsif $a eq '--bisect'              { $mode = 'bisect' }
    elsif $a.starts-with('--')          { note "$TOOL: unknown argument: $a"; exit 2 }
    else                                { @pos.push: $a }
}
if %*ENV<RAKUPP_NO_AOT> || %*ENV<RAKUPP_AOT_RANGE> {
    note "$TOOL: RAKUPP_NO_AOT / RAKUPP_AOT_RANGE are set; the comparison would be meaningless. Unset them.";
    exit 2;
}

my $ROOT = battery-root(:tool($TOOL));
my %PICK = pick-rakupp($?FILE.IO.parent.parent);
require-native(%PICK, :tool($TOOL), :verdict<INCONCLUSIVE>);
my $ENGINE = %PICK<path>.IO.absolute;
my $RAKULIB = battery-rakulib($ROOT);
my $SANDBOX = $ROOT.add('harness/battery.sb');

my $WORK = $*TMPDIR.add("aot-battery-{$*PID}");
$WORK.mkdir;
END { run 'rm', '-rf', $WORK.Str if $WORK.e }

# ---- one test file ----------------------------------------------------------

sub norm(Str $s) { $s.subst(/ '<' \d ** 6..* '>' /, '<A>', :g) }

#| A fresh copy of a distribution to build and run in.
sub stage(IO::Path $dist --> IO::Path) {
    my $w = $WORK.add($dist.basename);
    run 'rm', '-rf', $w.Str if $w.e;
    $w.mkdir;
    run 'cp', '-R', $dist.add('.').Str, $w.add('dist').Str;
    $w.add('home').mkdir;
    $w
}

sub env-for(IO::Path $w, *%extra) {
    %( HOME => $w.add('home').absolute, TZ => 'UTC',
       RAKULIB => $w.add('dist/lib').absolute ~ ',' ~ $RAKULIB, |%extra )
}

#| Compile one test file: the binary (or Nil) and the native-routine count.
sub build(IO::Path $w, Str $t) {
    my $bin = $w.add('bin-' ~ $t.subst(/<[/.]>/, '_', :g));
    my %r = run-capped([$ENGINE, '--exe', '-o', $bin.absolute, $t], :cap($build-cap),
                       :work($w.add('dist')), :env(env-for($w)));
    my $count = %r<err> ~~ / (\d+ ' of ' \d+) ' module routines compiled natively' / ?? ~$0 !! '';
    ($bin.x ?? $bin !! Nil, $count, %r)
}

sub run-one(IO::Path $w, @cmd, *%extra) {
    my %r = run-capped(@cmd, :cap($run-cap), :work($w.add('dist')), :env(env-for($w, |%extra)),
                       :sandbox($SANDBOX));
    %r<key> = "{%r<rc> // 'none'}/{%r<signal>}\n" ~ norm(%r<out>);
    %r
}
sub rc-of(%r) { %r<hung> ?? 'hung' !! %r<signal> ?? signal-name(%r<signal>) !! ~(%r<rc> // '?') }

#| Judge one test file. A Hash: verdict, exediff, rcs, routines, and the runs.
sub judge(IO::Path $dist, IO::Path $w, Str $t) {
    my ($bin, $count, $b) = build($w, $t);
    return %( verdict => 'NOBIN', routines => $count, why => first-line($b) ) without $bin;
    my %aot  = run-one($w, [$bin.absolute]);
    my %off  = run-one($w, [$bin.absolute], :RAKUPP_NO_AOT<1>);
    my %int  = run-one($w, [$ENGINE, $t]);
    my $verdict = %aot<key> eq %off<key> ?? 'same' !! 'AOTDIFF';
    if $verdict eq 'AOTDIFF' {
        # once more, both ways: an output that will not repeat proves nothing
        my %off2 = run-one($w, [$bin.absolute], :RAKUPP_NO_AOT<1>);
        my %aot2 = run-one($w, [$bin.absolute]);
        if %off2<key> ne %off<key> || %aot2<key> ne %aot<key> { $verdict = 'FLAKY' }
        $verdict = 'benign' if %BENIGN{"{$dist.basename}\t$t"};
    }
    %( :$verdict, routines => $count, :%aot, :%off, :%int,
       exediff => %off<key> eq %int<key> ?? '' !! 'EXEDIFF',
       rcs => "{rc-of(%aot)}/{rc-of(%off)}/{rc-of(%int)}" )
}

#| The test files of a staged distribution: t/ and one level below it.
sub test-files(IO::Path $w --> List) {
    my $t = $w.add('dist/t');
    return () unless $t.d;
    my @f;
    for $t.dir -> $e {
        if $e.d { @f.append: $e.dir.grep(*.f) }
        else    { @f.push: $e }
    }
    @f.grep(*.basename ~~ / '.' [ t | rakutest | t6 ] $ /)
      .map(*.relative($w.add('dist')))
      .sort.List
}

sub dist-named(Str $name --> IO::Path) {
    my $d = $ROOT.add('dists').add($name);
    unless $d.d { note "$TOOL: no distribution $name in {$ROOT.add('dists')}"; exit 2 }
    $d
}

say provenance-line($TOOL, %PICK);

# ---- --one ------------------------------------------------------------------

if $mode eq 'one' {
    unless @pos == 2 { note "$TOOL: --one DIST t/FILE"; exit 2 }
    my $dist = dist-named(@pos[0]);
    my $w = stage($dist);
    my %j = judge($dist, $w, @pos[1]);
    say "{@pos[0]} {@pos[1]}: {%j<verdict>} {%j<exediff>}  exits {%j<rcs> // '-'}  ({%j<routines> || 'no'} module routines native)";
    say "  {%j<why>}" if %j<why>;
    if %j<aot> {
        for <aot off>, <off int> -> $pair {
            my ($x, $y) = @$pair;
            next if %j{$x}<key> eq %j{$y}<key>;
            $w.add("$x.out").spurt: %j{$x}<key>;
            $w.add("$y.out").spurt: %j{$y}<key>;
            say "--- $x  +++ $y";
            my $d = run('diff', $w.add("$x.out").Str, $w.add("$y.out").Str, :out, :err);
            .say for $d.out.slurp(:close).lines.head(40);
            $d.err.slurp(:close);
        }
    }
    exit %j<verdict> eq 'AOTDIFF' ?? 1 !! 0;
}

# ---- --bisect ---------------------------------------------------------------

if $mode eq 'bisect' {
    unless @pos == 2 { note "$TOOL: --bisect DIST t/FILE"; exit 2 }
    my $dist = dist-named(@pos[0]);
    my $w = stage($dist);
    my ($bin, $count) = build($w, @pos[1]);
    without $bin { note "$TOOL: --exe did not build {@pos[1]}"; exit 2 }
    my $n = $count ~~ / ^ (\d+) / ?? +$0 !! 0;
    unless $n { note "$TOOL: no native module bodies to bisect"; exit 2 }
    my $ref = run-one($w, [$bin.absolute], :RAKUPP_NO_AOT<1>)<key>;
    unless run-one($w, [$bin.absolute], :RAKUPP_NO_AOT<1>)<key> eq $ref {
        note "$TOOL: the RAKUPP_NO_AOT=1 output is not stable run to run; nothing to bisect";
        exit 2;
    }
    my $tries = 0;
    sub fails(Str $range) { $tries++; run-one($w, [$bin.absolute], :RAKUPP_AOT_RANGE($range))<key> ne $ref }
    my $hi = $n - 1;
    unless fails("0-$hi") { say "$TOOL: no difference with all $n bodies attached"; exit 0 }
    # K: the smallest prefix 0-K that differs; J: the largest J with J-K differing
    my ($lo, $up) = 0, $hi;
    while $lo < $up { my $mid = ($lo + $up) div 2; if fails("0-$mid") { $up = $mid } else { $lo = $mid + 1 } }
    my $k = $lo;
    ($lo, $up) = 0, $k;
    while $lo < $up { my $mid = ($lo + $up + 1) div 2; if fails("$mid-$k") { $lo = $mid } else { $up = $mid - 1 } }
    my $j = $lo;
    say "$TOOL: the difference needs the bodies $j..$k ({$tries} runs)";
    my $range = $j == $k ?? "$k-$k" !! "$j-$j,$k-$k";
    say $j == $k ?? "$TOOL: one body alone reproduces it:"
                 !! (fails($range) ?? "$TOOL: the pair {$j} and {$k} reproduces it:"
                                   !! "$TOOL: {$j} and {$k} alone do not; something between them is needed too:");
    my %t = run-one($w, [$bin.absolute], :RAKUPP_AOT_RANGE($range), :RAKUPP_AOT_TRACE<1>);
    say "  $_" for %t<err>.lines.grep(/ '[aot] #' /);
    exit 1;
}

# ---- the battery ------------------------------------------------------------

my @dists;
if $all { @dists = battery-dists($ROOT) }
else {
    my @names = $?FILE.IO.parent.add('aot-battery.list').lines.map(*.trim).grep({ $_ && !.starts-with('#') });
    @dists = @names.map(&dist-named);
}
@dists = @dists.grep(*.basename.contains($only)) if $only;
unless @dists { note "$TOOL: no distributions selected"; exit 2 }

my $git = run('git', '-C', $ROOT.Str, 'log', '-1', '--format=%h', :out, :err);
my $REV = $git.out.slurp(:close).trim || 'unknown'; $git.err.slurp(:close);
say "$TOOL: battery $ROOT ($REV), {@dists.elems} distributions; caps {$run-cap} s a run, {$build-cap} s a build";

my $OUT  = $out-file ?? $out-file.IO.open(:w) !! Nil;
my $DIFFS = $out-file ?? "$out-file.d".IO !! Nil;
$OUT.say: <dist test verdict exediff exits routines>.join("\t") if $OUT;

my %count;
my @bad;
my %benign-seen;
my $t0 = now;
for @dists -> $dist {
    my $w = stage($dist);
    my @tests = test-files($w);
    my %here;
    for @tests -> $t {
        my %j = judge($dist, $w, $t);
        %count{%j<verdict>}++;
        %count<EXEDIFF>++ if %j<exediff>;
        %here{%j<verdict>}++;
        my $key = "{$dist.basename}\t$t";
        %benign-seen{$key} = True if %j<verdict> eq 'benign';
        $OUT.say: ($dist.basename, $t, %j<verdict>, %j<exediff> // '', %j<rcs> // '', %j<routines> // '').join("\t") if $OUT;
        if %j<verdict> eq 'AOTDIFF' {
            @bad.push: $key;
            say "  AOTDIFF {$dist.basename} $t  exits {%j<rcs>}  ({%j<routines>} native)";
            say "    rakupp tools/aot-battery.raku --one {$dist.basename} $t";
        }
        if $DIFFS && %j<verdict> ne 'same' && %j<aot> {
            $DIFFS.mkdir;
            my $f = "{$dist.basename}." ~ $t.subst('/', '_', :g);
            $DIFFS.add("$f.aot").spurt: %j<aot><key>;
            $DIFFS.add("$f.noaot").spurt: %j<off><key>;
        }
    }
    say sprintf('%-32s %3d files  %s', $dist.basename, +@tests,
                %here.sort.map({ "{.key} {.value}" }).join(', ') || 'no test files');
    run 'rm', '-rf', $w.Str;
}
$OUT.close if $OUT;

my @stale = %BENIGN.keys.grep({ my $d = .split("\t")[0]; @dists.first(*.basename eq $d) && !%benign-seen{$_} }).sort;
say '';
say "$TOOL: {%count.values.sum - (%count<EXEDIFF> // 0)} test files in {((now - $t0) / 60).round(0.1)} min";
say sprintf('  %-8s %d', $_, %count{$_} // 0) for <same AOTDIFF benign FLAKY NOBIN>;
say "  EXEDIFF  {%count<EXEDIFF> // 0}   (informational: the compiled program differs from the interpreter)";
say "$TOOL: the known-benign entry for {.subst("\t", ' ')} no longer differs; drop it" for @stale;
if @bad {
    say "$TOOL: FAIL — {+@bad} test files answer differently with native module bodies";
    exit 1;
}
say "$TOOL: PASS";
exit 0;
