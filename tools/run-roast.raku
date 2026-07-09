#!/usr/bin/env rakupp
# Roast test harness, self-hosted in Raku and run by rakupp itself.
#
# Usage:
#   build/rakupp tools/run-roast.raku [PATTERN ...]
#
# With no PATTERN, runs every .t file under $ROOT. A PATTERN is matched as a
# substring against the path.

my $ROOT    = %*ENV<ROAST> // '/Users/ash/roast';   # set $ROAST to your Roast checkout
my $BIN     = ~$*EXECUTABLE;   # test whichever compiler is running this harness
my $TIMEOUT = 10;

# Run a test file, capturing stdout with a hard timeout (idiomatic Proc::Async + Promise).
# Returns (output-string, timed-out-bool).
sub run-with-timeout($bin, $file, $timeout) {
    my $proc = Proc::Async.new($bin, $file);
    my $out = '';
    $proc.stdout.tap(-> $chunk { $out ~= $chunk });
    my $done = $proc.start;
    await Promise.anyof($done, Promise.in($timeout));
    my $timedout = $done.status ne 'Kept';
    $proc.kill if $timedout;
    return ($out, $timedout);
}

# Recursively collect *.t files under $dir.
sub find-t($dir) {
    my @out;
    for dir($dir).sort -> $e {
        if $e.IO.d {
            for find-t($e) -> $x { @out.push($x) }
        }
        elsif $e.ends-with('.t') {
            @out.push($e);
        }
    }
    return @out;
}

# Parse TAP text -> (planned, ran, passed, failed). planned is -1 if absent.
sub parse-tap($out) {
    my $planned = -1;
    my $ran = 0;
    my $passed = 0;
    my $failed = 0;
    for $out.lines -> $ln {
        if $ln.starts-with('1..') {
            if $planned < 0 { $planned = $ln.substr(3).words[0].Int }  # first plan wins
        }
        elsif $ln.starts-with('ok') || $ln.starts-with('not ok') {
            my $isok = !$ln.starts-with('not ok');
            my $lc = $ln.lc;
            my $skip = $lc.contains('# skip') || $lc.contains('# todo');
            $ran++;
            if $isok || $skip {
                $passed++;
            }
            else {
                $failed++;
            }
        }
    }
    return ($planned, $ran, $passed, $failed);
}

# Statically read a file's declared test count from its `plan N;` line, WITHOUT
# running it — so a file that parse-errors before emitting any TAP still has its
# intended test count known. Returns the N, or -1 for a dynamic/absent plan
# (`plan *`, `done-testing`, or none). Anchored at line start to skip `plan`s
# that appear inside quoted is_run bodies.
sub static-plan($file) {
    for $file.IO.lines -> $ln {
        if $ln ~~ /^ \s* 'plan' <.ws> (\d+) / { return +$0 }
        if $ln ~~ /^ \s* 'plan' <.ws> '*'   / { return -1 }  # dynamic — unknowable statically
    }
    return -1;
}

my @patterns = @*ARGS;
my @files;
for find-t($ROOT) -> $f {
    if @patterns.elems == 0 {
        @files.push($f);
    }
    else {
        for @patterns -> $p { if $f.contains($p) { @files.push($f); last } }
    }
}

my $pass = 0;
my $partial = 0;
my $noplan = 0;
my $timeout = 0;
my $tot-ran = 0;
my $tot-pass = 0;
my $tot-plan = 0;
my $notap-declared = 0;   # tests declared by no-TAP files that never emitted a plan (all failing)
my $notap-counted  = 0;   # how many no-TAP files we recovered a static plan from
my $notap-unknown  = 0;   # no-TAP files whose plan is dynamic/absent — uncountable

for @files -> $f {
    my $rel = $f.substr($ROOT.chars + 1);
    my ($out, $timedout) = run-with-timeout($BIN, $f, $TIMEOUT);
    if $timedout {
        $timeout++;
        say "  [TIME]          ", $rel;
        next;
    }
    my ($planned, $ran, $passed, $failed) = parse-tap($out);
    $tot-ran  += $ran;
    $tot-pass += $passed;
    # "planned" denominator: how many tests the file *intended* to run. Where a plan
    # is present we count it (so tests lost to a mid-file abort count as not-passed);
    # where none was emitted we fall back to what ran.
    $tot-plan += ($planned >= 0 ?? $planned !! $ran);
    my $mark;
    if $planned == 0 && $failed == 0 && $out.contains('# SKIP') {
        $pass++;              # genuine `plan skip-all` (emits `1..0 # SKIP …`) is a passing outcome
        $mark = 'PASS';
    }
    elsif $ran == 0 {
        $noplan++;
        $mark = '----';
        # A no-TAP file's tests are all effectively failing. If it emitted a plan
        # before dying, that N is already in $tot-plan; otherwise recover N from
        # source so those tests count against us instead of vanishing.
        if $planned < 0 {
            my $sp = static-plan($f);
            if $sp > 0 { $notap-declared += $sp; $notap-counted++ } else { $notap-unknown++ }
        }
    }
    elsif $failed == 0 && ($planned < 0 || $planned == $ran) {
        $pass++;
        $mark = 'PASS';
    }
    else {
        $partial++;
        $mark = 'part';
    }
    # live per-file result (skip the no-TAP noise, like the Python harness)
    if $mark ne '----' {
        say sprintf('  [%s]  %5s  %s', $mark, "$passed/$ran", $rel);
    }
}

my $declared = $tot-plan + $notap-declared;  # every test any file declares it will run
my $fpct  = @files.elems ?? 100 * $pass     / @files.elems !! 0;
my $rpct  = $tot-ran     ?? 100 * $tot-pass / $tot-ran     !! 0;
my $ppct  = $tot-plan    ?? 100 * $tot-pass / $tot-plan    !! 0;
my $dpct  = $declared    ?? 100 * $tot-pass / $declared    !! 0;
say "";
say "Files: ", @files.elems, "   fully-pass: ", $pass,
    "   partial: ", $partial, "   no-TAP: ", $noplan, "   timeout: ", $timeout;
say sprintf("Files fully passing:  %d / %d  (%.1f%%)", $pass, @files.elems, $fpct);
say sprintf("Assertions passed:    %d / %d  (%.1f%%)  of tests that ran", $tot-pass, $tot-ran, $rpct);
say sprintf("Assertions passed:    %d / %d  (%.1f%%)  of tests planned by files that emitted a plan", $tot-pass, $tot-plan, $ppct);
say sprintf("Assertions passed:    %d / %d  (%.1f%%)  of ALL declared tests (+%d from %d no-TAP files read from source; %d more have no static plan)",
            $tot-pass, $declared, $dpct, $notap-declared, $notap-counted, $notap-unknown);
