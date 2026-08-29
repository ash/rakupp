#!/usr/bin/env rakupp
# Plant a known defect, run the release gate UNMODIFIED, and confirm it goes red.
#
# Every gate in RELEASING.md is trusted to catch regressions, and none of them
# had ever been asked to prove it. v3.21.0 is what that costs: all seven gates
# passed and the release shipped `Digest`'s RIPEMD returning wrong hashes for
# every input. One gate of seven saw it, and the first reading of that gate was
# wrong too. A gate nobody has seen fail is a ritual, not a measurement.
#
# So: for each gate, inject a defect it is supposed to catch, run the gate's own
# documented command with no modification, and require a RED result. A gate that
# stays green with a planted defect is broken, and this run says so.
#
#   rakupp tools/prove-gates.raku                # the gates that run in minutes
#   rakupp tools/prove-gates.raku --all          # …including the hour-long ones
#   rakupp tools/prove-gates.raku --gate=2,4b    # named gates only
#   rakupp tools/prove-gates.raku --list         # what exists, and what it costs
#
# EVERY plant is reverted, including on failure and on Ctrl-C: each one records
# how to undo itself before it does anything, and the undo runs from a LEAVE.
# If this program is killed with SIGKILL it cannot clean up — `git status` and
# `git checkout` are then the recovery, and every plant is a tracked-file edit
# or a file under t/regression/, so nothing hides.

my $ROOT = $?FILE.IO.parent.parent;
my $RAKUPP = $*EXECUTABLE.absolute;

my $all = False;
my $list = False;
my @want;
for @*ARGS -> $a {
    if    $a eq '--all'  { $all = True }
    elsif $a eq '--list' { $list = True }
    elsif $a ~~ / ^ '--gate=' (.+) $ / { @want = (~$0).split(',')>>.trim }
    else { note "unknown argument: $a"; exit 2 }
}

# ---------------------------------------------------------------------------
# plumbing: run a command, and edit-then-restore a file
# ---------------------------------------------------------------------------

sub sh(*@cmd, :$cwd = $ROOT.Str, :%env) {
    my $p = %env ?? run(|@cmd, :out, :err, :$cwd, :env(%( |%*ENV, |%env )))
                 !! run(|@cmd, :out, :err, :$cwd);
    my $o = $p.out.slurp(:close);
    my $e = $p.err.slurp(:close);
    ($p.exitcode, $o, $e)
}

# Replace $old with $new in $file, returning a closure that puts it back.
sub plant-edit(IO::Path $file, Str $old, Str $new) {
    my $before = $file.slurp;
    die "prove-gates: the text to plant into {$file.basename} is not there —\n"
      ~ "  the file has changed and this plant needs rewriting:\n  $old"
        unless $before.contains($old);
    $file.spurt($before.subst($old, $new));
    return sub { $file.spurt($before) };
}

# Append to a file, returning a closure that restores it exactly.
sub plant-append(IO::Path $file, Str $extra) {
    my $before = $file.slurp;
    $file.spurt($before ~ $extra);
    return sub { $file.spurt($before) };
}

sub plant-file(IO::Path $file, Str $content) {
    die "prove-gates: $file already exists; refusing to overwrite it" if $file.e;
    $file.spurt($content);
    return sub { $file.unlink if $file.e };
}

# ---------------------------------------------------------------------------
# the gates
# ---------------------------------------------------------------------------
# Each: name, what it plants, the gate command as RELEASING.md writes it, and
# what "red" means for that command. `slow` ones need a rebuild or an hour.

my @gates;

sub gate(:$id, :$name, :$defect, :$cost, :$slow = False, :&run) {
    @gates.push: %( :$id, :$name, :$defect, :$cost, :$slow, :&run );
}

# --- 2. the local suite ----------------------------------------------------
gate
  id     => '2',
  name   => 'local suite (t/run.raku)',
  defect => 'a t/regression case that returns the wrong answer',
  cost   => '~10 min',
  run    => sub {
      my $f = $ROOT.add('t/regression/zzz-prove-gates-planted.raku');
      my $undo = plant-file($f, q:to/CASE/);
          # PLANTED by tools/prove-gates.raku. If this file is still here, that
          # run was killed before it could clean up — delete it.
          my @fail;
          @fail.push("2 + 2 came out 5") unless 2 + 2 == 5;
          say @fail ?? @fail.join("\n") ~ "\nFAIL" !! 'PASS';
          exit @fail ?? 1 !! 0;
          CASE
      LEAVE $undo();
      my ($rc, $out, $err) = sh($RAKUPP, $ROOT.add('t/run.raku').Str);
      my $named = ($out ~ $err).contains('zzz-prove-gates-planted');
      ( $rc != 0, "exit $rc" ~ ($named ?? ', and it names the file' !! ', but it does NOT name the file') )
  };

# --- 3. performance --------------------------------------------------------
# The faithful plant would be a genuinely slower build; this doctors the
# BASELINE by the same 10% instead, which exercises the identical comparison at
# a fraction of the cost. It proves the gate's arithmetic and its red path — NOT
# that a real slowdown is measurable above noise. Say so when quoting it.
gate
  id     => '3',
  name   => 'perf-guard --check',
  defect => 'the recorded baseline doctored 10% faster (proxy for a slower build)',
  cost   => '~3 min',
  run    => sub {
      my $bl = $ROOT.add('tools/perf-baseline.raku');
      my $text = $bl.slurp;
      # first `fib` figure in the baseline table
      $text ~~ / 'fib' <-[\n]>*? (\d+ ['.' \d+]?) /
        or return (False, 'could not find a fib baseline figure to doctor');
      my $was = ~$0;
      my $now = ($was.Num * 0.9).fmt('%.1f');
      my $undo = plant-edit($bl, $was, $now);
      LEAVE $undo();
      my ($rc, $out, $err) = sh($RAKUPP, $ROOT.add('tools/perf-guard.raku').Str, '--check');
      # 1 = regression (what we want); 2 = inconclusive (machine too busy to say)
      return (False, "INCONCLUSIVE (exit 2) — machine too loaded to judge; re-run idle")
          if $rc == 2;
      ( $rc == 1, "exit $rc; baseline fib $was -> $now" )
  };

# --- 4b. slim differential -------------------------------------------------
# The faithful plant is a BUG IN THE CUTTING MACHINERY, not in a program: `--slim`
# (auto) only removes what it has proven unused, so a correct implementation can
# never differ, and no corpus file alone can make it. So drop `uniname` from the
# set of names that FORCE unicode-names to be kept — then a program calling
# `uniname` gets the feature cut out from under it, which is exactly the class of
# defect SLIM-PLAN's defence 5 exists to stop. Costs two rebuilds.
gate
  id     => '4b',
  name   => 'slim-diff (behaviour)',
  defect => 'the cut analysis wrongly proves a USED feature unused',
  cost   => '~6 min (2 rebuilds)',
  slow   => True,
  run    => sub {
      my $f = $ROOT.add('t/regression/zzz-prove-gates-slim.raku');
      my $undo-file = plant-file($f, q:to/CASE/);
          # PLANTED by tools/prove-gates.raku — delete if a killed run left it.
          say 'A'.uniname;
          say 'PASS';
          CASE
      LEAVE $undo-file();
      my $scan = $ROOT.add('src/SlimScan.cpp');
      my $undo-src = plant-edit($scan,
          '{"uniname", "uninames", "uniparse",',
          '{"uninames", "uniparse",');   # uniname no longer forces the feature
      LEAVE { $undo-src(); sh('make', '-C', $ROOT.add('build-arm64').Str, '-j4', 'rakupp') }
      my ($brc, $bo, $be) = sh('make', '-C', $ROOT.add('build-arm64').Str, '-j4', 'rakupp');
      return (False, "the rebuild with the plant failed: exit $brc") unless $brc == 0;
      my ($rc, $out, $err) = sh($RAKUPP, $ROOT.add('tools/slim-diff.raku').Str, $f.Str);
      ( $rc != 0 || $out.contains('DIFFERENT'),
        "exit $rc: {$out.lines.grep({ .contains('slim-diff:') || .contains('DIFFERENT') }).head // 'no summary'}" )
  };

# --- 4b'. slim differential leaves nothing behind --------------------------
gate
  id     => '4b-reap',
  name   => 'slim-diff (leaves no processes)',
  defect => 'a program that outlives the cap with live children',
  cost   => '~2 min',
  run    => sub {
      my $f = $ROOT.add('t/regression/zzz-prove-gates-leak.raku');
      my $undo = plant-file($f, q:to/CASE/);
          # PLANTED by tools/prove-gates.raku — delete if a killed run left it.
          # Bounded on purpose: three sleeps, no self-spawning, so this tests the
          # reaper without reproducing the 1,253-process chain it exists to stop.
          my @kids = (^3).map({ Proc::Async.new('/bin/sleep', '120') });
          .start for @kids;
          sleep 120;
          CASE
      LEAVE $undo();
      my $before = +qqx{pgrep -f 'sleep 120' 2>/dev/null | wc -l}.trim;
      my ($rc, $out, $err) = sh($RAKUPP, $ROOT.add('tools/slim-diff.raku').Str, $f.Str);
      my $after = +qqx{pgrep -f 'sleep 120' 2>/dev/null | wc -l}.trim;
      # here RED means "the reaper worked": no descendant outlived the run
      ( $after <= $before,
        "sleeps before $before, after $after — the run must leave none behind" )
  };

# --- 4. the compiler agrees with the interpreter ---------------------------
# NOTE: `@benches` in run-optbench.raku is a HARDCODED list, not a directory
# scan — dropping a file into tools/optbench/ does nothing. (That cost this
# harness one false "MISSED" before it was noticed.) So the plant edits an
# EXISTING kernel instead, appending a line whose answer differs between the
# interpreter and a compiled binary — a real divergence between the two modes
# this gate exists to compare, needing no engine change.
gate
  id     => '4',
  name   => 'optbench (modes agree)',
  defect => 'an existing kernel whose answer differs between interpreter and --exe',
  cost   => '~3 min',
  run    => sub {
      my $k = $ROOT.add('tools/optbench/intsum.raku');
      my $undo = plant-append($k, "\nsay \$*EXECUTABLE.basename;   # PLANTED by prove-gates\n");
      LEAVE $undo();
      my ($rc, $out, $err) = sh($RAKUPP, $ROOT.add('tools/run-optbench.raku').Str);
      my $named = ($out ~ $err).contains('intsum') && ($out ~ $err).contains('MISMATCH');
      ( $rc != 0, "exit $rc" ~ ($named ?? ', naming intsum as the mismatch' !! ', but intsum is not flagged') )
  };

# --- 6. the distribution battery -------------------------------------------
gate
  id     => '6',
  name   => 'battery (against the COMMITTED baseline)',
  defect => 'a committed baseline saying a dist passed that now does not',
  cost   => '~4 min',
  run    => sub {
      my $bat = '/Users/ash/raku-module-battery'.IO;
      return (False, 'raku-module-battery is not at the expected path') unless $bat.d;
      # Ask the runner to compare a dist that currently DIFFs against a baseline
      # claiming PASS. We cannot doctor the committed file without a commit, so
      # this reads the runner's own comparison of a real dist and checks it is
      # reported against git HEAD rather than against the file just written.
      my ($rc, $out, $err) = sh('/usr/local/bin/raku', 'tier2/run-dist-tests.raku',
                                '--only=Digest', :cwd($bat.Str),
                                :env(%( RAKUPP => $RAKUPP )));
      my $names-git = ($out ~ $err).contains('COMMITTED baseline (git HEAD');
      ( $names-git, $names-git
          ?? 'the run states its verdict changes against git HEAD, not against the file it wrote'
          !! 'the run does NOT name the committed baseline — it may be comparing against itself' )
  };

# --- 1. Roast (slow) -------------------------------------------------------
gate
  id     => '1',
  name   => 'Roast file list',
  defect => 'the fully-passing list compared against a doctored predecessor',
  cost   => '~5 min (subset)',
  slow   => True,
  run    => sub {
      my $roast = (%*ENV<ROAST> // '/Users/ash/roast').IO;
      return (False, 'no Roast checkout — set ROAST=') unless $roast.d;
      my $tmp = $*TMPDIR.add("prove-gates-{$*PID}");
      $tmp.mkdir;
      LEAVE { .unlink for $tmp.dir; $tmp.rmdir }
      my $list = $tmp.add('now.list');
      my ($rc, $out, $err) = sh($RAKUPP, $ROOT.add('tools/run-roast.raku').Str,
                                '--workers=4', "--list=$list", 'S02-',
                                :env(%( ROAST => $roast.Str )));
      return (False, "the subset run failed: exit $rc") unless $list.e && $list.lines;
      # a predecessor with one extra file is a file that stopped fully passing
      my $prev = $tmp.add('prev.list');
      # NOTE the `|`: `($list.lines, 'x')` is a TWO-element list (a Seq and a Str),
      # so sorting it sorts two items and joining stringifies the Seq's gist. That
      # cost this harness a false "MISSED" on gate 1 before it was spotted.
      $prev.spurt((|$list.lines, 'S02-PLANTED/never-existed.t').sort.join("\n") ~ "\n");
      my ($crc, $cout, $cerr) = sh('comm', '-23', $prev.Str, $list.Str);
      my $regressed = $cout.trim.lines.grep(*.trim).elems;
      # …and the same list must contain no corrupted paths
      my $bad = $list.lines.grep({ !.ends-with('.t') }).elems;
      ( $regressed == 1 && $bad == 0,
        "comm found $regressed regression (want 1); {$list.lines.elems} paths, $bad malformed (want 0)" )
  };

# --- 5. a second toolchain (slow) ------------------------------------------
gate
  id     => '5',
  name   => 'GCC build',
  defect => 'code Clang accepts and GCC does not',
  cost   => '~10 min',
  slow   => True,
  run    => sub {
      my $bd = $ROOT.add('build-gcc16');
      return (False, 'no build-gcc16/ — configure a GCC build first') unless $bd.d;
      my $src = $ROOT.add('src/BuildInfo.cpp');
      # A designated initializer out of order: Clang accepts it as an extension
      # in C++, GCC rejects it. Narrow, and in a file that compiles fast.
      my @planted =
          'namespace rakupp {',
          'struct ProveGatesPlanted { int a; int b; };',
          'static ProveGatesPlanted proveGatesPlanted = { .b = 2, .a = 1 };';
      my $undo = plant-edit($src, 'namespace rakupp {', @planted.join("\n"));
      # restore the SOURCE and the GCC build, so the tree is left as it was found
      LEAVE { $undo(); sh('make', '-C', $bd.Str, '-j4', 'rakupp') }
      my ($rc, $out, $err) = sh('make', '-C', $bd.Str, '-j4', 'rakupp');
      ( $rc != 0, "GCC build exit $rc" )
  };

# ---------------------------------------------------------------------------
# drive
# ---------------------------------------------------------------------------

if $list {
    say sprintf('%-8s %-16s %s', 'gate', 'cost', 'planted defect');
    say '-' x 8 ~ ' ' ~ '-' x 16 ~ ' ' ~ '-' x 58;
    for @gates -> %g {
        say sprintf('%-8s %-16s %s%s', %g<id>, %g<cost>, %g<defect>,
                    %g<slow> ?? '   [--all only]' !! '');
    }
    exit 0;
}

my @run = @gates.grep({
    @want ?? %^g<id> (elem) @want.Set !! ($all || !%^g<slow>)
});
unless @run {
    note "prove-gates: no gate selected. Try --list.";
    exit 2;
}

say "prove-gates: {@run.elems} gate{@run.elems == 1 ?? '' !! 's'}, each given a defect it must catch";
say "";
my @results;
for @run -> %g {
    print sprintf('%-8s %-38s ', %g<id>, %g<name>);
    $*OUT.flush;
    my ($caught, $detail);
    my $t0 = now;
    try {
        ($caught, $detail) = %g<run>();
        CATCH { default { $caught = False; $detail = "the plant itself failed: {.message}" } }
    }
    my $secs = ((now - $t0) / 1).round;
    say ($caught ?? 'CAUGHT  ' !! 'MISSED  ') ~ "({$secs}s)  $detail";
    @results.push([%g<id>, $caught, $detail]);
}

say "";
my @missed = @results.grep({ !.[1] });
if @missed {
    say "{@missed.elems} of {@results.elems} gates did NOT catch their planted defect:";
    say "  gate {.[0]}: {.[2]}" for @missed;
    say "";
    say "A gate that stays green with a defect in front of it is not measuring anything.";
    exit 1;
}
say "every gate caught its planted defect ({@results.elems} of {@results.elems}).";
exit 0;
