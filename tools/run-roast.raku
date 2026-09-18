#!/usr/bin/env rakupp
# Roast test harness, self-hosted in Raku and run by rakupp itself.
#
# Usage:
#   build/rakupp tools/run-roast.raku [-j=N] [--workers=N] [--cpu=N] [--list=FILE] [--times=FILE] [PATTERN ...]
#
# With no PATTERN, runs every .t file under $ROOT. A PATTERN is matched as a
# substring against the path. The files run from one work queue, longest
# first, on --workers=N `start` threads (default: two per core), admitted
# against a CPU budget of --cpu=N cores (default: every core the machine has):
# a file that only waits — a spec sleep, a timeout that hangs at zero
# CPU — starts at once, a file that computes starts when a core's worth of
# estimated demand is free. The interpreter parks the GIL while a worker waits
# on its child, so the children genuinely overlap. Results are tallied and
# printed in file order regardless of N.
#
# -j=N is the one knob worth reaching for: it sets the CPU budget and sizes the
# worker pool to match (2N), the way `make -j` reads. `-jN` is accepted too.
# It defaults to the whole machine, so a gate run finishes as fast as the box
# allows; pass a smaller -j to keep cores free for something else. --cpu and
# --workers still set the two halves independently, and win if given after -j.
#
# --times=FILE reads FILE for the ordering and the demand estimates (wall time
# and a CPU sample per file from the previous run) and rewrites it with this
# run's, the way --list writes its list. Without the flag the committed
# docs/status/roast-lists/roast.times is read and nothing is written;
# `--times=` (empty) reads nothing, so every file is assumed to need a core
# and the queue carries no ordering information. The scheduling note above the
# queue has the measurements.
#
# --list=FILE writes the fully-passing file paths, one per line, sorted. THAT is
# what a release diff should compare — RELEASING.md calls the file list the gate,
# and reconstructing it with `grep '[PASS]' | awk '{print $NF}'` over decorated
# human-readable output makes the gate only as reliable as that output's framing.
# It was not reliable: see the stderr note in run-with-timeout.

my $ROOT    = (%*ENV<ROAST> // ((%*ENV<HOME> // '.') ~ '/roast')).IO.absolute;  # set $ROAST to your Roast checkout
use lib $?FILE.IO.parent.add('lib').Str;
use Gate;
my $BIN     = $*EXECUTABLE.absolute;   # test whichever compiler is running this harness
my $TIMEOUT = (%*ENV<ROAST_TIMEOUT> // 10).Int; # parallel-mode legs need headroom:
    # under RAKUPP_PARALLEL a thread-spawning file pays real contention (cas
    # retries, worker scheduling) that the GIL leg never sees — thread.t takes
    # ~20 s there and PASSES. The GIL baseline keeps the default 10.

# Files whose SPEC requires more wall time than the default allows — sleep.t
# genuinely sleeps ~15 s of mainline (it asserts sleep 3 takes >= 2 s; issue #41
# made sleeps real, so the file went from partial-in-1s to passing-in-18s).
# Values are seconds; everything else keeps $TIMEOUT.
my %SLOW-FILES =
    'S29-context/sleep.t'  => 30,  # mainline sleep 3 × asserted-real, 4 blocks
    'S17-supply/batch.t'   => 60,  # batch(:seconds(5)): aligns to 5 s periods, four
                                   # times — ~36 s here, and ~38 s on Rakudo
    'S17-supply/throttle.t' => 30, # sleep 6 + sleep 3 of mainline, then a 10 × .5 s
                                   # paced stream: ~13 s on Rakudo here too
    'S17-supply/unique.t'  => 45,  # :expires(2) asserted against real sleeps, ×8
;

# The I/O tests write RELATIVE paths, so they land in whatever directory the
# harness was started from — the repo root. Several never clean up (open.t's
# `create_this_file`/`create_this_file2`, file-tests.t's `symlink-existing`/
# `symlink-nonexisting`, chmod.t's `temp_<epoch>`, evalfile.t's
# `temp-evalfile.<pid>.<n>`, local.t's `t/spec/S22-package-format/`), and the
# per-run ones piled up: dozens of untracked files, which is also why .gitignore
# carries a list of them. Roast is an upstream checkout we do not patch, so the
# fix belongs here — every child runs from a per-run scratch directory under
# $*TMPDIR, removed when the run ends. ($ROOT and $BIN are absolutized above for
# the same reason: a relative one would not resolve from the scratch directory.)
#
# The scratch directory is NOT empty. Roast is written to run from an
# implementation's repo root and a handful of tests read what is there: dir.t
# asserts the listing contains a `t/` and indexes `dir('t').[0]` ("see roast's
# README as for why there is always a t/ available"), filetest.t file-tests `t`
# and `README.md`, local.t builds `t/spec/…` under the cwd. Handed a bare
# directory they silently lose those assertions, so the scratch root carries the
# same entries the repo root gave them — with this, the whole-suite tally is
# unchanged.
my $SCRATCH = $*TMPDIR.add("rakupp-roast-{$*PID}");
$SCRATCH.mkdir;
$SCRATCH.add('t').mkdir;
$SCRATCH.add('t/placeholder.t').spurt("# keeps t/ non-empty: dir.t reads dir('t').[0]\n");
$SCRATCH.add('README.md').spurt("Scratch working directory for a rakupp Roast run.\n");
sub rmtree($p) {
    return unless $p.e || $p.l;          # .e is False for a DANGLING symlink
    if $p.d && !$p.l {
        rmtree($_.IO) for dir($p);
        rmdir($p);
    }
    else { try unlink($p) }
}
END { rmtree($SCRATCH) }

# Run a test file, capturing stdout, with a hard timeout. Returns
# (output-string, timed-out-bool).
#
# ONE FORK AT A TIME. The engine's spawn leaves a new child's pipe ends
# inheritable for a moment, and a sibling forked from another thread in that
# moment keeps a copy of the child's stdout write end until the SIBLING exits.
# The child then finishes, its whole TAP is captured, and its EOF never comes:
# the harness waits the full timeout and files it as [TIME]. Measured with
# three files from three threads (at.t, in.t, sleep.t): stuck in 4 of 10
# rounds, with either Proc::Async or run(:timeout), and `lsof` on the running
# sleep.t child showed four pipe descriptors that were never its own. This is
# the noise the old harness lived with — the release snapshots record 12 to
# 22 timeouts across passes of the same build. Serialising the fork, and the
# close of the stdin pipe with it, removes the window; the wait itself still
# overlaps, because the lock is released as soon as the child is started.
#
# The child's stdin is a pipe closed at once, so it reads EOF whatever the
# harness itself was started with. Inherited, a stdin that is an open pipe
# nobody closes (a CI step, a backgrounded shell) parks every file that reads
# it: prompt.t and S16-filehandles/io.t sat at their first read until the
# timeout, and five files that finish in 0.1 s were counted as timeouts.
#
# Stderr is captured and dropped. Inherited, at --workers=4 the children's TAP
# diagnostics were written straight into the harness's own stream and spliced
# mid-line into its per-file status lines, and RELEASING.md's gate is `awk
# '{print $NF}'` over those lines — four files a run silently lost their path
# and read as regressions.
#
# The child's promise is awaited exactly once: Promise.anyof leaves its losing
# promise Broken, so a second wait on it returns at once.
my $SPAWN = Lock.new;
sub run-with-timeout($bin, $file, $timeout) {
    my $proc = Proc::Async.new($bin, $file, :w);
    my $out = '';
    $proc.stdout.tap(-> $chunk { $out ~= $chunk });
    $proc.stderr.tap(-> $chunk { });
    my $done = $SPAWN.protect({
        my $d = $proc.start(:cwd($SCRATCH.absolute));
        $proc.close-stdin;
        $d
    });
    await Promise.anyof($done, Promise.in($timeout));
    my $timedout = $done.status ne 'Kept';
    $proc.kill if $timedout;
    return ($out, $timedout);
}

# ps prints cputime as [dd-]hh:mm:ss on Linux and as mm:ss.cc on macOS.
sub parse-cputime($s) {
    my ($days, $rest) = $s.contains('-') ?? $s.split('-', 2) !! (0, $s);
    my $secs = 0;
    $secs = $secs * 60 + +$_ for $rest.split(':');
    return $days * 86400 + $secs;
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

# Parse TAP text -> (planned, ran, passed, failed, skipped, todo-failed).
# planned is -1 if absent.
#
# `passed` counts a `# skip` line and a `# todo` line as passing, which is right
# — a skip is not a failure, and a TODO failure is an expected one — but it means
# the headline assertion figure is a SHIELDED number, and nothing used to say by
# how much. The last two return values are that disclosure:
#
#   skipped      `ok N # skip …`      — never executed
#   todo-failed  `not ok N # todo …`  — executed, genuinely failed, counted as a
#                                       pass because the suite marks it expected
#
# `ok N # todo …` is deliberately in neither: it ran and it passed, so it needs
# no shield. Both counts include the suite's OWN skip()/todo() calls as well as
# the ones our fudge rewriting produces, because TAP cannot tell them apart —
# see COUNTING.md for the static split.
sub parse-tap($out) {
    my $planned = -1;
    my $ran = 0;
    my $passed = 0;
    my $failed = 0;
    my $skipped = 0;
    my $todo-failed = 0;
    for $out.lines -> $ln {
        if $ln.starts-with('1..') {
            if $planned < 0 { $planned = $ln.substr(3).words[0].Int }  # first plan wins
        }
        elsif $ln.starts-with('ok') || $ln.starts-with('not ok') {
            my $isok = !$ln.starts-with('not ok');
            my $lc = $ln.lc;
            my $is-skip = $lc.contains('# skip');
            my $is-todo = $lc.contains('# todo');
            my $skip = $is-skip || $is-todo;
            $ran++;
            $skipped++     if $is-skip;
            $todo-failed++ if $is-todo && !$isok;
            if $isok || $skip {
                $passed++;
            }
            else {
                $failed++;
            }
        }
    }
    return ($planned, $ran, $passed, $failed, $skipped, $todo-failed);
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

my $T0          = now;                            # the run's own wall clock, for the summary
my $WORKERS     = 2 * (($*KERNEL.cpu-cores // 4) max 1);  # threads; most park in a child — see the scheduling note
my $CPU         = ($*KERNEL.cpu-cores // 2) max 1;        # cores the running files may add up to (-j=N / --cpu=N)
my $LISTFILE;
my $TIMESFILE   = $?FILE.IO.parent.parent.add('docs/status/roast-lists/roast.times').Str;
my $TIMES-GIVEN = False;                          # --times=FILE names the file to read AND rewrite
my @patterns;
for @*ARGS -> $a {
    if $a ~~ /^ '--workers=' (\d+) $/ { $WORKERS = (+$0) max 1 }
    elsif $a ~~ /^ '--list=' (.+) $/  { $LISTFILE = ~$0 }
    elsif $a ~~ /^ '--cpu=' (\d+) $/    { $CPU = (+$0) max 1 }
    elsif $a ~~ /^ '-j' '='? (\d+) $/ { $CPU = (+$0) max 1; $WORKERS = 2 * $CPU }
    elsif $a ~~ /^ '--times=' (.*) $/ { $TIMESFILE = ~$0; $TIMES-GIVEN = True }
    else { @patterns.push($a) }
}
# ---------------------------------------------------------------------------
# Provenance. A run of this harness produces the release's headline figure and
# the file list the NEXT release diffs against, and until now it recorded
# neither of its two inputs: which rakupp was measured, and which Roast.
#
# The binary matters because `$*EXECUTABLE` is whatever ran this file, and
# RELEASING.md writes the gate as `rakupp tools/run-roast.raku` — a PATH lookup.
# On the machine of record `rakupp` resolves to three different binaries in
# order: build-arm64/ (v3.22.0), /usr/local/bin (v1.0.0) and /opt/homebrew/bin
# (v0.5.1). The first is correct by PATH ordering alone, and the other two would
# produce a plausible, much lower number with nothing in the output to say so.
#
# Roast matters because it is an upstream checkout that moves. Gate 1 is a DIFF
# against the previous release's list; if Roast changed between the two runs,
# files appear and disappear and the diff charges every one of them to the
# engine. Nothing in this repo recorded the revision, so no past release's
# measurement can be reproduced.
sub roast-revision(--> Str) {
    my $p = run('git', '-C', $ROOT, 'rev-parse', '--short', 'HEAD', :out, :err);
    my $r = $p.out.slurp(:close).trim; $p.err.slurp(:close);
    $r || 'not-a-git-checkout'
}
# (binary-version lives in tools/lib/Gate.rakumod — the harness tests whatever
# ran it, so there is no CHOICE to make here, only a version to report.)

my @files;
for find-t($ROOT) -> $f {
    if @patterns.elems == 0 {
        @files.push($f);
    }
    else {
        for @patterns -> $p { if $f.contains($p) { @files.push($f); last } }
    }
}

# What the Roast checkout looks like BEFORE the run. Some tests write beside
# their own .t file rather than into the working directory — S16-io/lines.t does
# `$*PROGRAM.sibling('lines.testing')` — so the per-run scratch directory above
# cannot catch them: the path is absolute and derived from the file's location in
# an upstream tree we do not patch. The residue is reported instead, because the
# provenance line now names a Roast REVISION, and a revision does not describe a
# checkout that has files in it the revision never had.
sub roast-untracked(--> Set) {
    my $p = run('git', '-C', $ROOT, 'status', '--porcelain', '--untracked-files=all',
                :out, :err);
    my $o = $p.out.slurp(:close); $p.err.slurp(:close);
    $o.lines.grep(*.starts-with('?? ')).map(*.substr(3)).Set
}
my $BEFORE = roast-untracked();

my $PROVENANCE = "rakupp {binary-version($BIN)} ($BIN) | roast {roast-revision()} ($ROOT)"
                ~ ($BEFORE ?? " + {$BEFORE.elems} untracked" !! '')
                ~ " | {@files.elems} files | workers $WORKERS";
say "run-roast: $PROVENANCE";
say "";

# Map a file's relative path to its synopsis/section key (matches the ROAST.md table).
sub seckey($rel) {
    return ~$0                if $rel ~~ / ^ (S\d\d) '-' /;
    return '6.c'              if $rel.starts-with('6.c/');
    return '6.d'              if $rel.starts-with('6.d/');
    return 'integration'      if $rel.starts-with('integration/');
    return 'APPENDICES'       if $rel.starts-with('APPENDICES/');
    return 'MISC / t'         if $rel.starts-with('MISC/') || $rel.starts-with('t/');
    return $rel.split('/')[0];
}
# Section themes (the one hand-kept column; everything else is computed).
my %theme =
    S01 => 'Overview', S02 => 'Literals, types, magicals', S03 => 'Operators',
    S04 => 'Blocks, statements, phasers', S05 => 'Regexes & grammars',
    S06 => 'Subroutines & signatures', S07 => 'Iterators', S09 => 'Data structures',
    S10 => 'Packages', S11 => 'Modules', S12 => 'Objects & classes', S13 => 'Overloading',
    S14 => 'Roles', S15 => 'Unicode / strings / NFG', S16 => 'I/O',
    S17 => 'Concurrency (supply/promise/async)', S19 => 'Command-line',
    S22 => 'Package format', S24 => 'Testing', S26 => 'Documentation (POD)',
    S28 => 'Special variables', S29 => 'Builtins & context',
    S32 => 'Standard types (str/list/num/…)', 'integration' => 'Cross-feature programs',
    '6.c' => 'v6.c language snapshot', '6.d' => 'v6.d language snapshot';

my $pass = 0;
my $partial = 0;
my $noplan = 0;
my $timeout = 0;
my $tot-skip = 0;        # `ok … # skip` lines counted as passes
my $tot-todofail = 0;    # `not ok … # todo` lines counted as passes
my $tot-ran = 0;
my $tot-pass = 0;
my $tot-plan = 0;
my $notap-declared = 0;   # tests declared by no-TAP files that never emitted a plan (all failing)
my $notap-counted  = 0;   # how many no-TAP files we recovered a static plan from
my $notap-unknown  = 0;   # no-TAP files whose plan is dynamic/absent — uncountable
# Per-section rollups for the by-synopsis table.
my (%sec-full, %sec-part, %sec-time, %sec-notap, %sec-pass, %sec-tot);

# ---------------------------------------------------------------------------
# Scheduling: a work queue, longest file first.
#
# The files used to run in lockstep batches of $WORKERS — a batch waited for
# its slowest member before the next batch started. That is where the minutes
# went, not into process startup: rakupp cold-starts in 3 ms, so spawning the
# whole suite's children costs ~4.5 s of CPU in total. Measured one file at a
# time (2026-09-17, wall and child CPU per file), 1,326 of the 1,464 files
# finish under 50 ms, 43 take a second or more, and the 25 slowest are 77% of
# the 283 s of summed wall. Of that wall 177 s is WAITING — sleep.t and
# batch.t sleep ~18 s each by spec, seven of the twelve timeouts hang at zero
# CPU — and only 106 s is CPU. Any batch holding one of the ~40 slow files
# stalled every worker in it, and the slow files are scattered through the
# list. Simulated over the measured times, lockstep took 259 s at 2 workers
# and still 196 s at 8.
#
# Here every worker pulls the next file the moment it is free, and the queue
# is ordered longest first from the previous run's recorded wall times
# ($TIMESFILE), so the tail overlaps with the bulk instead of following it.
# Simulated makespans in seconds, from the same measurement:
#
#     workers                 2     4     8    12    16
#     lockstep, file order  259   243   196   183   171
#     queue, file order     142    75    41    32    27
#     queue, longest first  141    71    35    24    19
#
# Measured, all of the below in place: 26-27 s for the whole suite on the
# 8-core machine of record, verdicts identical to a one-file-at-a-time run.
#
# The floor is 18.6 s, batch.t's own wait; 106 s of CPU over this machine's
# cores comes to about the same. Within the queue the order protects the
# files that need fidelity: a CPU-bound file that finishes inside the 10 s
# timeout with room to spare — concat-stable.t needs 6.7 s of CPU,
# hyperrace/basics.t 5.1 s — becomes a timeout if contention slows it 2×, so
# the long finishers go first, onto the idle machine. A file that timed out
# last run sorts as 1.99 s: it needs no fidelity, so the timeouts overlap with
# the bulk afterwards (the sidecar reader below has the note). A file with no
# recorded time, new in Roast, is assumed to take 1 s: ahead of the bulk,
# behind the tail.
#
# Admission by CPU demand. A file that only WAITS — the two spec sleeps, a
# timeout that hangs at zero CPU, a Promise.in test — needs no core, so it
# should not hold a worker's slot: measured, those files are 150 of the ~330
# slot-seconds an 8-worker run spends. A separate lane for them was measured
# first and rejected: with the waiters gone from the head of the queue, the
# eight CPU-heaviest finishers started together at second zero (one of them,
# cas-int.t, runs three busy threads) and two or three of them were slowed
# past the 10 s timeout in every run. So there is one queue and a budget:
# each file carries an estimated demand in cores — CPU seconds over wall
# seconds from the previous run's sample, 1.0 when it has none — and a worker
# takes the first queued file whose demand fits under --cpu. Waiters start at
# once, CPU-bound files are admitted as cores free up, and the threads are
# plentiful (two per core) because most of them are parked in a child.
#
# Results are tallied and printed in FILE ORDER regardless of scheduling, so
# the output and the totals are those of a sequential run. A worker parks its
# result under a lock and flushes the completed prefix of the file list — its
# own result and any earlier ones that were waiting on it.
my %prior;   # rel path -> { wall, timeout, cpu } from the previous run
if $TIMESFILE && $TIMESFILE.IO.e {
    for $TIMESFILE.IO.lines -> $ln {
        next if !$ln || $ln.starts-with('#');
        my @c = $ln.split("\t");
        next if @c.elems < 2;
        %prior{@c[0]} = %( wall    => +@c[1],
                           timeout => (@c[2] // '') eq 'timeout',
                           cpu     => (@c[3] // '') ne '' ?? +@c[3] !! Nil );
    }
}
my (@key, @demand);
for ^@files.elems -> $k {
    my $p = %prior{@files[$k].substr($ROOT.chars + 1)};
    # S17-procasync tests spawn processes by the dozen. That load is latency,
    # not CPU: the grandchildren live milliseconds, so no sample ever sees them,
    # and admitted as nearly free beside seven cores of work the three heavy
    # ones (stress.t, no-runaway-file-limit.t, many-processes-no-close-stdin.t;
    # 1.6 to 5.7 s alone) went past the timeout in every full run. They go
    # first, onto the idle machine, two at a time.
    my $spawner = @files[$k].contains('/S17-procasync/');
    @key[$k]    = $spawner ?? 100 + ($p ?? $p<wall> !! 1.0)
                !! !$p ?? 1.0 !! $p<timeout> ?? 1.99 !! $p<wall>;
    @demand[$k] = $spawner ?? ($CPU div 2) max 1
                !! $p && $p<cpu>.defined && $p<wall> > 0 ?? min(4, $p<cpu> / $p<wall>) !! 1.0;
}
my @queue   = (^@files.elems).sort({ @key[$^b] <=> @key[$^a] });
my $sampled = @demand.grep({ $_ != 1.0 }).elems;
say "run-roast: cpu budget $CPU cores over $WORKERS workers; {@files.elems - $sampled} files assumed to need a core, "
  ~ "{@demand.grep({ $_ < 0.25 }).elems} known to wait (last run's CPU samples)";

my @fullypassing;   # the release gate's file LIST, collected as data not as text
my @result;         # per file position, set by whichever worker ran it
my @wall;           # per file position, this run's wall seconds: the next run's ordering
my $lock    = Lock.new;
my $flushed = 0;    # files [0 ..^ $flushed) are tallied and printed

# --- the CPU sampler ---------------------------------------------------------
# %cpu-sample: absolute file path -> CPU seconds its child had used at the last
# look. One `ps` a second over the whole run, from a thread of its own, is what
# tells a file that computes from one that waits (the demand estimate above).
# It is a thread and not a timer per file because Promise.anyof leaves its
# losing promise Broken, so a worker cannot wait on its child twice — a
# one-second wait, a sample, then the rest turned every file over a second old
# into a timeout. Its `ps` is spawned under the same lock as the children (see
# run-with-timeout): a fork is a fork.
my $running   = 0;   # children alive right now
my $completed = 0;   # children finished, in any order ($flushed lags: it is in file order)
# The per-file lines come out in FILE ORDER, and the first file in that order
# is a bulk file that starts late, so a terminal shows nothing for most of the
# run. This line, on stderr and only when stderr is a terminal, says what is
# happening in the meantime; the flush below wipes it before printing.
sub progress() {
    my ($d, $r) = $lock.protect({ ($completed, $running) });
    $*ERR.print(sprintf("\r  %d/%d done, %d running, %.0f s ", $d, @files.elems, $r, (now - $T0).Num));
}
sub wipe-progress() { $*ERR.print("\r" ~ (' ' x 48) ~ "\r") }
my %cpu-sample;
my $sampling = True;
sub sample-children() {
    my $p = $SPAWN.protect({ run('ps', '-axo', 'ppid=,cputime=,command=', :out, :err) });
    my $o = $p.out.slurp(:close); $p.err.slurp(:close);
    my %seen;
    for $o.lines -> $ln {
        my @w = $ln.words;
        next unless @w.elems >= 3 && @w[0] eq ~$*PID && @w[*-1].ends-with('.t');
        %seen{@w[*-1]} = parse-cputime(@w[1]);
    }
    $lock.protect({ %cpu-sample{$_} = %seen{$_} for %seen.keys });
}
my $sampler = start {
    my $tick = 0;
    while $sampling {
        sleep 0.25;
        progress() if $*ERR.t;
        sample-children() if ++$tick %% 4;
    }
};

# Run one file AND parse its TAP on the worker, so parsing overlaps with the
# other workers' child processes instead of serialising afterwards.
my sub run-one($f) {
    my $rel = $f.substr($ROOT.chars + 1);
    my ($out, $timedout) = run-with-timeout($BIN, $f, %SLOW-FILES{$rel} // $TIMEOUT);
    my $cpu = $lock.protect({ %cpu-sample{$f} });   # the last look the sampler took while it ran
    my ($planned, $ran, $passed, $failed, $skipped, $todofail) = parse-tap($out);
    # New fields go on the END: the unpack below is positional.
    [$timedout, $planned, $ran, $passed, $failed, $out.contains('# SKIP'),
     $skipped, $todofail, $cpu]; # an Array stays one item
}

# Tally and print file $k. Called in file order, under $lock.
my sub tally($k) {
    my $f = @files[$k];
    my $rel = $f.substr($ROOT.chars + 1);
    my $sec = seckey($rel);
    my $r = @result[$k];
    my ($timedout, $planned, $ran, $passed, $failed, $has-skip) = $r[0], $r[1], $r[2], $r[3], $r[4], $r[5];
    my ($skipped, $todofail) = $r[6] // 0, $r[7] // 0;
    if $timedout {
        $timeout++;
        %sec-time{$sec}++;
        say "  [TIME]          ", $rel;
        return;
    }
    $tot-ran  += $ran;
    $tot-pass += $passed;
    $tot-skip += $skipped;
    $tot-todofail += $todofail;
    %sec-pass{$sec} += $passed;
    %sec-tot{$sec}  += $ran;
    # "planned" denominator: how many tests the file *intended* to run. Where a plan
    # is present we count it (so tests lost to a mid-file abort count as not-passed);
    # where none was emitted we fall back to what ran.
    $tot-plan += ($planned >= 0 ?? $planned !! $ran);
    my $mark;
    if $planned == 0 && $failed == 0 && $has-skip {
        $pass++;              # genuine `plan skip-all` (emits `1..0 # SKIP …`) is a passing outcome
        %sec-full{$sec}++;
        $mark = 'PASS';
    }
    elsif $ran == 0 {
        $noplan++;
        %sec-notap{$sec}++;
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
        %sec-full{$sec}++;
        $mark = 'PASS';
    }
    else {
        $partial++;
        %sec-part{$sec}++;
        $mark = 'part';
    }
    @fullypassing.push($rel) if $mark eq 'PASS';
    # live per-file result (skip the no-TAP noise, like the Python harness)
    if $mark ne '----' {
        say sprintf('  [%s]  %5s  %s', $mark, "$passed/$ran", $rel);
    }
}

# Admission: a worker takes the first untaken file from the head of the queue
# whose demand fits the budget. Waiters always fit, so they start at once; a
# CPU-bound file starts when a core's worth of estimated demand is free; an
# idle machine admits anything. -1 means nothing fits yet.
my @taken; my $head = 0; my $load = 0;   # $load: cores the running files are estimated to use
my sub take-next() {
    while $head < @queue.elems && @taken[@queue[$head]] { $head++ }
    return Nil if $head >= @queue.elems;
    my $i = $head;
    while $i < @queue.elems {
        my $k = @queue[$i];
        if !@taken[$k] && ($load == 0 || $load + @demand[$k] <= $CPU) {
            @taken[$k] = True;
            $running++;
            $load += @demand[$k];
            return $k;
        }
        $i++;
    }
    return -1;
}
my sub worker() {
    loop {
        my $k = $lock.protect({ take-next() });
        last if !$k.defined;
        if $k == -1 { sleep 0.02; next }
        my $t0 = now;
        my $r  = run-one(@files[$k]);
        my $dt = (now - $t0).Num;
        $lock.protect({
            $load -= @demand[$k];
            @wall[$k]   = $dt;
            @result[$k] = $r;
            $running--;
            $completed++;
            wipe-progress() if $*ERR.t && @result[$flushed].defined;
            while $flushed < @files.elems && @result[$flushed].defined {
                tally($flushed);
                $flushed++;
            }
        });
    }
}
if $WORKERS > 1 && @files.elems > 1 {
    my @workers;
    @workers.push(start { worker() }) for ^($WORKERS min @files.elems);
    await @workers;
}
else {
    worker();
}
$sampling = False;
await $sampler;
wipe-progress() if $*ERR.t;

# The gate's file list, as DATA. Written before the summary so a run that dies
# formatting its own tables still leaves the thing a release actually diffs.
# A self-check comes with it: every path here must end in `.t`. If one does not,
# something has interleaved with output that is supposed to be ours alone, and
# the list is not trustworthy — say so loudly rather than write a quiet lie.
if $LISTFILE {
    my @bad = @fullypassing.grep({ !.ends-with('.t') });
    $LISTFILE.IO.spurt(@fullypassing.sort.join("\n") ~ "\n");
    if @bad {
        note "run-roast: {@bad.elems} fully-passing entr{@bad.elems == 1 ?? 'y does' !! 'ies do'} not end in .t —";
        note "  the file list is corrupted, not just cosmetically: {@bad.head(4).join(', ')}";
    }
    else {
        # A sidecar, not a header line: the .list file is diffed with `comm`,
        # which would report a differing comment line as a changed path.
        my $meta = "$LISTFILE.meta";
        $meta.IO.spurt("$PROVENANCE\nfully-passing {@fullypassing.elems}\ngenerated {DateTime.now.truncated-to('second')}\n");
        say "";
        say "Fully-passing file list ({@fullypassing.elems} paths) -> $LISTFILE";
        say "Provenance -> $meta";
    }
}

# The timing sidecar: this run's per-file wall time, the next run's ordering
# key. Written only when --times=FILE names it — the committed default is read,
# never rewritten behind anyone's back — and only by a full run, because a
# filtered run knows nothing about the files it did not visit.
if $TIMES-GIVEN && $TIMESFILE {
    if @patterns {
        note "run-roast: --times not written: a filtered run cannot time the whole suite.";
    }
    else {
        my @rows = (^@files.elems).map(-> $k {
            my $cpu = @result[$k][8];
            sprintf("%s\t%.3f\t%s\t%s", @files[$k].substr($ROOT.chars + 1), @wall[$k] // 0,
                    @result[$k][0] ?? 'timeout' !! '', $cpu.defined ?? sprintf('%.2f', $cpu) !! '') });
        $TIMESFILE.IO.spurt("# path\twall-seconds\tnote\tcpu-seconds | $PROVENANCE\n" ~ @rows.sort.join("\n") ~ "\n");
        say "Per-file wall times ({@rows.elems} rows) -> $TIMESFILE";
    }
}

# Files this run left in the Roast checkout. Not fatal — Roast is upstream and
# we do not patch it — but a run that dirties its own input should say so.
{
    my @new = (roast-untracked() (-) $BEFORE).keys.sort;
    if @new {
        note "";
        note "run-roast: this run left {@new.elems} file(s) in the Roast checkout:";
        note "  $_" for @new.head(8);
        note "  …and {@new.elems - 8} more" if @new > 8;
        note "  (tests that write beside their own .t file, e.g. S16-io/lines.t's";
        note "   \$*PROGRAM.sibling — the per-run scratch dir cannot intercept an";
        note "   absolute path. Remove them so the next run starts from the revision";
        note "   the provenance line names.)";
    }
    elsif $BEFORE {
        note "";
        note "run-roast: the Roast checkout already had {$BEFORE.elems} untracked file(s) "
           ~ "before this run — the provenance line says so.";
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
say sprintf("Wall time:            %.1f s  (%d workers)", (now - $T0).Num, $WORKERS);
say sprintf("Files fully passing:  %d / %d  (%.1f%%)", $pass, @files.elems, $fpct);
say sprintf("Assertions passed:    %d / %d  (%.1f%%)  of tests that ran", $tot-pass, $tot-ran, $rpct);
say sprintf("Assertions passed:    %d / %d  (%.1f%%)  of tests planned by files that emitted a plan", $tot-pass, $tot-plan, $ppct);
say sprintf("Assertions passed:    %d / %d  (%.1f%%)  of ALL declared tests (+%d from %d no-TAP files read from source; %d more have no static plan)",
            $tot-pass, $declared, $dpct, $notap-declared, $notap-counted, $notap-unknown);
# What the pass count is SHIELDED by. Both categories are legitimately counted as
# passes above; this line says how many, so the headline can be read net.
my $shielded = $tot-skip + $tot-todofail;
my $net      = $tot-pass - $shielded;
say sprintf("  of which shielded:  %d skipped + %d todo-failed = %d (%.2f%% of the pass count)",
            $tot-skip, $tot-todofail, $shielded, $tot-pass ?? 100 * $shielded / $tot-pass !! 0);
say sprintf("Assertions passed NET of skip/todo: %d / %d  (%.1f%%)  of ALL declared tests",
            $net, $declared, $declared ?? 100 * $net / $declared !! 0);

# ---- Per-synopsis breakdown, formatted paste-ready for the ROAST.md table ----
sub sec-order($s) {
    return +$0 if $s ~~ / ^ S (\d\d) $ /;
    my %tail = 'integration' => 100, '6.c' => 101, '6.d' => 102, 'APPENDICES' => 103, 'MISC / t' => 104;
    return %tail{$s} // 200;
}
my @secs = (%sec-full.keys, %sec-part.keys, %sec-time.keys, %sec-notap.keys)
           .flat.unique.sort({ sec-order($^a) <=> sec-order($^b) });
say "";
say "By synopsis (paste into the ROAST.md table):";
my @head = <Section Theme Full Part Time No-TAP Assertions %>;
my @rows;
for @secs -> $s {
    my $a = %sec-pass{$s} // 0;
    my $b = %sec-tot{$s}  // 0;
    my $pct = $b ?? sprintf('%d%%', (100 * $a / $b).round) !! '—';
    @rows.push([ $s, (%theme{$s} // '—'),
                 ~(%sec-full{$s} // 0), ~(%sec-part{$s} // 0), ~(%sec-time{$s} // 0), ~(%sec-notap{$s} // 0),
                 "$a/$b", $pct ]);
}
# Padded to column width: readable in a terminal, and still the same markdown
# table once pasted — a padded cell and a longer dash rule are both fine there.
my @w;
for ^@head.elems -> $i {
    my $m = @head[$i].chars;
    for @rows -> $r { $m = $r[$i].chars if $r[$i].chars > $m }
    @w[$i] = $m;
}
sub cell($v, $i) { $i < 2 ?? $v ~ (' ' x (@w[$i] - $v.chars)) !! (' ' x (@w[$i] - $v.chars)) ~ $v }  # text left, numbers right
say '| ' ~ (^@head.elems).map({ cell(@head[$_], $_) }).join(' | ') ~ ' |';
say '|' ~ (^@head.elems).map({ $_ < 2 ?? '-' x (@w[$_] + 2) !! ('-' x (@w[$_] + 1)) ~ ':' }).join('|') ~ '|';
for @rows -> $r {
    say '| ' ~ (^@head.elems).map({ cell($r[$_], $_) }).join(' | ') ~ ' |';
}
