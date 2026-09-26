#!/usr/bin/env rakupp
# Roast test harness, self-hosted in Raku and run by rakupp itself.
#
# Usage:
#   build/rakupp tools/run-roast.raku [-j=N] [--workers=N] [--cpu=N] [--list=FILE] [--times=FILE] [--failed[=FILE]] [--fudge=IMPL|none] [PATTERN ...]
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
#
# --failed prints, after the summary, every file that did not fully pass —
# partial, no-TAP, timed out or lost — one per line with its category and
# passed/ran count, sorted by path, and under each file the source lines of its
# failing tests (the first ten), located from Test's `Failed test … line N`
# diagnostics on the child's stderr. --failed=FILE writes the bare paths to FILE
# instead, one per line, sorted: the complement of --list, ready to feed back
# in as PATTERNs (`build/rakupp tools/run-roast.raku $(cat FILE)`).
#
# --fudge=IMPL names the implementation Roast's own `fudge` rewrites the files
# for when the engine under test is NOT rakupp — default rakudo.moar, the set of
# directives rakupp's lexer applies — and --fudge=none runs the raw files. Under
# rakupp the flag does nothing: the lexer applies the directives itself, and a
# file fudge already rewrote would be fudged twice. The note above the fudge
# pass, before the provenance line, has the measurements.

my $ROOT    = (%*ENV<ROAST> // ((%*ENV<HOME> // '.') ~ '/roast')).IO.absolute;  # set $ROAST to your Roast checkout
use lib $?FILE.IO.parent.add('lib').Str;
use Gate;
my $BIN     = $*EXECUTABLE.absolute;   # test whichever compiler is running this harness

# WHICH engine is being measured. `$*EXECUTABLE` is whatever ran this file, and
# running the harness under a foreign compiler is a supported, useful thing to do
# — `rakudo tools/run-roast.raku` scores Rakudo on exactly this bar. But two
# things here are calibrated for RAKUPP specifically: the ceiling below and the
# roast.times demand estimates. Applied to another engine they do not merely go
# stale, they silently truncate its run. So identify the engine once, here, and
# let both adapt.
#
# binary-version() recognises only the Raku++ banner and returns 'unknown' for
# anything else — that is the detection. The second spawn happens on the foreign
# path alone, so a rakupp run still pays exactly one --version, as before.
sub engine-id($path) {
    my $v = binary-version($path);
    return ('rakupp', $v) unless $v eq 'unknown';
    my $p = run($path, '--version', :out, :err);
    my $t = $p.out.slurp(:close); $p.err.slurp(:close);
    return ('rakudo', ~$<v>) if $t ~~ / 'Rakudo' \D*? $<v>=[ 'v'? \d+ ['.' \d+]* ] /;
    return ('other', ($t.lines[0] // '').trim || 'unknown');
}
my ($ENGINE, $ENGINE-VER) = engine-id($BIN);
my $FOREIGN = $ENGINE ne 'rakupp';
# mutsu is the one foreign engine this harness actually gets pointed at, and it
# has a fudge switch of its own — MUTSU_FUDGE=1, OFF by default, set by its own
# runner (COUNTING.md's comparison table). Measured without it, mutsu is scored
# on a bar neither engine uses: 1,419 of 1,464 files with it, far less without.
# That single missing environment variable is the likeliest reason a mutsu run
# comes back with a number nobody expects, so name it in the run's own output.
my $MUTSU       = $ENGINE-VER.lc.contains('mutsu');
my $MUTSU-FUDGE = ((%*ENV<MUTSU_FUDGE> // '') ne '') && ((%*ENV<MUTSU_FUDGE> // '') ne '0');
# raw .t path -> the file the engine is actually handed: the sidecar Roast's
# `fudge` wrote for it (see the fudge pass, before the provenance line). Only
# files fudge rewrote have an entry; everything reported keeps the .t path.
my %RUN-AS;

# The ceiling is a RAKUPP budget and it does not travel. Measured serially on an
# idle 8-core box: the 81 S15 files take 10 s of wall under rakupp and 160 s
# under Rakudo — 16x — and the heavy ones land at 3-4 s each (nfkd-9.t 4.1 s,
# nfd-9.t 4.1 s, nfc-9.t 3.9 s), with nfc-concat.t at 23 s. At the default two
# workers per core, a 3-4 s file needs only ~2.5x of contention slowdown to cross
# 10 s — and S15 alone is 85,079 of the suite's 146,380 statically declared
# tests. That is how a Rakudo run reported 76,285 declared tests for a suite
# that declares ~216,000: not because its files are slow, because they are
# ordinary files measured against another engine's stopwatch. A foreign engine
# gets 6x the budget; ROAST_TIMEOUT still wins if it is set.
my $TIME-SCALE = $FOREIGN ?? 6 !! 1;
my $TIMEOUT = (%*ENV<ROAST_TIMEOUT> // (10 * $TIME-SCALE)).Int; # parallel-mode legs need headroom:
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
    'S17-scheduler/in.t'   => 30,  # cues :in(1)/:in(2) and sleeps 3 s, four times:
    'S17-scheduler/at.t'   => 30,  # ~12 s here, ~28 s on Rakudo (each)
    'S17-scheduler/every.t' => 45, # the same with :every, twice over: ~24 s
    'S32-io/lock.t'        => 60,  # each blocking check runs a child that sleeps
                                   # $SLEEP (1 s) while it waits on the lock: ~36 s
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
# Stderr is captured, never printed: only --failed reads it, for the failing
# tests' line numbers (see failed-lines). Inherited, at --workers=4 the children's TAP
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
    my $err = '';
    $proc.stdout.tap(-> $chunk { $out ~= $chunk });
    $proc.stderr.tap(-> $chunk { $err ~= $chunk });
    my $done = $SPAWN.protect({
        my $d = $proc.start(:cwd($SCRATCH.absolute));
        $proc.close-stdin;
        $d
    });
    await Promise.anyof($done, Promise.in($timeout));
    my $timedout = $done.status ne 'Kept';
    $proc.kill if $timedout;
    return ($out, $timedout, $err);
}

# The source lines of the failing tests in $file, from Test's diagnostics on
# stderr: `# Failed test 'x' at PATH line N` (rakupp) or the same with `at PATH
# line N` on the next `#` line (Rakudo), either one indented inside a subtest.
# Only locations in $file itself count — not a module a test called into — and
# each line is reported once, in order, so a failing subtest and its failing
# inner test (which both point at the same line) print it once. A foreign
# engine ran fudge's sidecar, so Test names THAT file; fudge keeps one line in
# for one line out, so its line numbers are the .t file's own.
sub failed-lines($err, $file) {
    my $base = $file.IO.basename;
    my $side = %RUN-AS{$file} ?? %RUN-AS{$file}.IO.basename !! '';
    my @n;
    my $armed = False;   # the previous line was a `Failed test` with no location
    for $err.lines -> $ln {
        my $t = $ln.trim-leading;
        next unless $t.starts-with('#');
        my $failed = $t.contains('Failed test');
        if ($failed || $armed) && $t ~~ / 'at ' (\S+) ' line ' (\d+) / {
            my $b = $0.IO.basename;
            @n.push(+$1) if $b eq $base || $b eq $side;
            $armed = False;
        }
        else {
            $armed = $failed;
        }
    }
    @n.unique.List;
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
    my $todo-passed = 0;
    for $out.lines -> $ln {
        # `ok`/`not ok` is the overwhelmingly common line, so test it first — and
        # pay for `.lc` and the directive scan only on a line that carries a `#`.
        # A whole S15 normalization file is thousands of plain `ok N - …` lines
        # with no directive at all, and lowercasing every one of them was this
        # loop's own cost: measured at 4.9 µs/line before, 2.0 µs/line after, on
        # the same input and to the same counts. Order against the `1..` branch
        # does not matter — a plan line starts with neither `ok` nor `not ok`.
        my $isok = $ln.starts-with('ok');
        if $isok || $ln.starts-with('not ok') {
            $ran++;
            if $ln.contains('#') {
                my $lc = $ln.lc;
                my $is-skip = $lc.contains('# skip');
                my $is-todo = $lc.contains('# todo');
                $skipped++     if $is-skip;
                # One kind per line, skip first: the same rule tap-mark applies,
                # so the fudge table's rows add up to these totals.
                $todo-failed++ if $is-todo && !$isok && !$is-skip;
                $todo-passed++ if $is-todo && $isok && !$is-skip;
                if $isok || $is-skip || $is-todo {
                    $passed++;
                }
                else {
                    $failed++;
                }
            }
            elsif $isok {
                $passed++;
            }
            else {
                $failed++;
            }
        }
        elsif $planned < 0 && $ln.starts-with('1..') {
            $planned = $ln.substr(3).words[0].Int;   # first plan wins
        }
    }
    return ($planned, $ran, $passed, $failed, $skipped, $todo-failed, $todo-passed);
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

# The `#?rakudo` fudge directives a file carries, and what became of the tests
# each one covers: verb => [directives, passed, failed, skipped, no-result].
# Only the directives rakupp's lexer applies are read (applyRakudoFudge in
# src/Lexer.cpp): bare `#?rakudo` and `#?rakudo.moar`.
#
# A test is tied to its directive by the reason, which the rewrite carries into
# the TAP: `#?rakudo todo 'X'` becomes `todo('X')` and its tests end in
# `# TODO X`; `#?rakudo skip 'X'` (and eval/try, applied the same way) end in
# `# skip X`. That keeps the suite's own skip()/todo() calls out, unless one
# happens to share a directive's reason. A todo test that passed is a directive
# the engine may have outgrown. "No result" counts directives none of whose
# tests appeared — the file died or timed out first. `emit` pastes code and
# covers no tests, so only its count means anything.
sub fudge-directives($file, $out --> Hash) {
    my @d;   # [verb, reason]
    my @lines = try { $file.IO.lines } // ();
    for @lines -> $ln {
        next unless $ln ~~ /^ \s* '#?rakudo' ['.' (\S+)]? \s+ [\d+ \s+]? (\w+) \s* (.*) /;
        next if $0.defined && ~$0 ne 'moar';
        my ($verb, $arg) = ~$1, (~$2).trim;   # before the match below resets $/
        # A double-quoted reason interpolates, so only its text up to the first
        # sigil or brace is known before the run; that part is matched as a prefix.
        my ($why, $prefix) = $arg, False;
        if $arg ~~ /^ (<['"]>) (.*?) $0 / {
            ($why, $prefix) = ~$1, False;
            if ~$0 eq '"' && $why ~~ /<[$@%&{]>/ { $why = $why.substr(0, $/.from).trim; $prefix = True }
        }
        @d.push([$verb, $why, $prefix]);
    }
    return {} unless @d;
    my %seen;   # "todo|reason" / "skip|reason" -> [passed, failed, skipped]
    for $out.lines -> $ln {
        next unless $ln.starts-with('ok') || $ln.starts-with('not ok');
        next unless $ln.contains('#');
        # parse-tap's rule exactly (`# skip` wins over `# todo`), so what the
        # directives are credited with never exceeds the file's own totals
        my $lc = $ln.lc;
        my $todo;
        if    $lc.contains('# skip') { $todo = False }
        elsif $lc.contains('# todo') { $todo = True }
        else                         { next }
        my $why = $todo ?? ($ln ~~ / .* '# ' [:i todo] $<why>=(.*) $ /)
                        !! ($ln ~~ / .* '# ' [:i skip] $<why>=(.*) $ /);
        my $c = %seen{($todo ?? 'todo' !! 'skip') ~ '|' ~ ($why ?? (~$<why>).trim !! '')} //= [0, 0, 0];
        if !$todo                   { $c[2]++ }
        elsif $ln.starts-with('ok') { $c[0]++ }
        else                        { $c[1]++ }
    }
    my %v;
    my %used;   # directives sharing a reason share its tests; count them once
    # eval is a skip under rakupp's lexer but a todo under Roast's fudge (it wraps
    # the code in EVAL and todo()s the tests), so on a sidecar run its tests end
    # in `# TODO reason`; try, which fudge makes a plain flunk, does not occur in
    # the suite.
    my $roast-fudged = %RUN-AS{$file}.defined;
    for @d -> [$verb, $why, $prefix] {
        my $row = %v{$verb} //= [0, 0, 0, 0, 0];
        $row[0]++;
        next if $verb eq 'emit';
        my $as-todo = $verb eq 'todo' || ($verb eq 'eval' && $roast-fudged);
        my $key = ($as-todo ?? 'todo' !! 'skip') ~ '|' ~ $why;
        $key = %seen.keys.first(*.starts-with($key)) // $key if $prefix && !%seen{$key};
        my $c = %seen{$key};
        if !$c        { $row[4]++ }
        elsif !%used{$key}++ { $row[$_ + 1] += $c[$_] for ^3 }
    }
    %v
}

my $T0          = now;                            # the run's own wall clock, for the summary
my $WORKERS     = 2 * (($*KERNEL.cpu-cores // 4) max 1);  # threads; most park in a child — see the scheduling note
my $CPU         = ($*KERNEL.cpu-cores // 2) max 1;        # cores the running files may add up to (-j=N / --cpu=N)
my $LISTFILE;
my $TIMESFILE   = $?FILE.IO.parent.parent.add('docs/status/roast-lists/roast.times').Str;
my $TIMES-GIVEN = False;                          # --times=FILE names the file to read AND rewrite
my $FAILED      = False;                          # --failed: list the non-passing files at the end
my $FAILEDFILE;                                   # --failed=FILE: write their paths there instead
my $FAILED-SHOW = 10;                             # failing source lines shown per file by --failed
my $FUDGE-IMPL  = 'rakudo.moar';                  # --fudge=IMPL: whom Roast's fudge rewrites for; 'none' runs raw
my $FUDGE-GIVEN = False;
my @patterns;
for @*ARGS -> $a {
    if $a ~~ /^ '--workers=' (\d+) $/ { $WORKERS = (+$0) max 1 }
    elsif $a ~~ /^ '--list=' (.+) $/  { $LISTFILE = ~$0 }
    elsif $a ~~ /^ '--fudge=' (.+) $/ { $FUDGE-IMPL = ~$0; $FUDGE-GIVEN = True }
    elsif $a ~~ /^ '--cpu=' (\d+) $/    { $CPU = (+$0) max 1 }
    elsif $a ~~ /^ '-j' '='? (\d+) $/ { $CPU = (+$0) max 1; $WORKERS = 2 * $CPU }
    elsif $a ~~ /^ '--times=' (.*) $/ { $TIMESFILE = ~$0; $TIMES-GIVEN = True }
    elsif $a eq '--failed'            { $FAILED = True }
    elsif $a ~~ /^ '--failed=' (.+) $/ { $FAILED = True; $FAILEDFILE = ~$0 }
    else { @patterns.push($a) }
}
# roast.times describes rakupp. Under another engine its wall times and CPU
# samples are not just stale, they are actively harmful: they report the S15
# normalization files at ~0.0 s of CPU (true — rakupp runs all 81 in 10 s), so
# the admission controller admits every one of them at once, which is precisely
# the contention that pushes a 3-4 s Rakudo file past the ceiling. The estimates
# and the ordering both come from that file, so a foreign engine gets neither
# unless --times was given explicitly.
if $FOREIGN && !$TIMES-GIVEN {
    $TIMESFILE = '';
    note "run-roast: measuring $ENGINE, not rakupp — ignoring rakupp's roast.times "
       ~ "(pass --times=FILE to record and reuse this engine's own).";
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

# Rakudo does not apply `#?rakudo` fudge directives itself — its own spectest
# runs Roast's `fudge` over the files first (t/harness6: `fudgeall
# --keep-exit-code rakudo.moar`, in batches of 200) and runs whatever paths that
# prints. Raku++ applies the same directives in its lexer (applyRakudoFudge in
# src/Lexer.cpp), so it runs the raw .t files, and it MUST: fudge leaves the
# directive comment in place above the rewritten test, so a fudged file fed to
# the lexer is fudged twice, the second todo leaking onto the next test. A
# foreign engine handed the raw files is scored on a bar neither engine uses.
# Measured 2026-09-26 over the 280 files fudge rewrites: Rakudo 2026.08 fails
# 260 of them raw and passes 277 fudged.
#
# So on the foreign path do what Rakudo's own harness does. Run Roast's `fudge`
# on every file that carries a directive line and hand the engine the sidecar
# it writes — `X.rakudo.moar` beside `X.t`, a pattern Roast's .gitignore lists,
# and a file in the same directory keeps the tests' `$*PROGRAM.parent(2)`
# package lookups valid. Everything reported — the per-file lines, --list,
# --failed, the directive table — keeps the .t path. `rakudo.moar` is the
# implementation for every engine because it is the bar the lexer applies (bare
# `#?rakudo` and `#?rakudo.moar`, never .jvm/.js); --fudge=IMPL changes it and
# --fudge=none runs the raw files, to measure the unfudged bar on purpose. A .t
# that already IS fudge's output (a worktree fudged by hand and renamed) carries
# fudge's `# FUDGED!` trailer and is left alone. mutsu applies the directives
# itself, in the interpreter, when MUTSU_FUDGE=1 is set — then the raw files are
# the right input and nothing is rewritten. Its preprocessor and Roast's fudge
# agreed on 279 of the 280 files (the odd one out is a `#?rakudo eval` block
# mutsu runs partway before dying, which fudge's EVAL wrapper then over-counts).
# Foreign path only: a rakupp run reads nothing extra.
my $FUDGED    = 0;   # files fudge rewrote for this run
my $PREFUDGED = 0;   # files that already were fudge's output
if !$FOREIGN && $FUDGE-GIVEN {
    note "run-roast: --fudge is ignored under rakupp — the lexer applies the #?rakudo "
       ~ "directives itself, and a file fudge rewrote would be fudged twice.";
}
if $FOREIGN && @files {
    my @carry = @files.grep(-> $f {
        (try { $f.IO.lines } // ()).first({ .trim-leading.starts-with('#?') }).defined
    });
    my $fudge = $ROOT.IO.add('fudge');
    if $MUTSU && $MUTSU-FUDGE {
        note "run-roast: mutsu with MUTSU_FUDGE=1 — it applies the #?rakudo directives "
           ~ "itself, so the raw files run and the fudged bar is the one being measured. "
           ~ "Unset it to have this harness apply Roast's own fudge instead "
           ~ "(docs/status/COUNTING.md).";
    }
    elsif $FUDGE-IMPL eq 'none' {
        note "run-roast: --fudge=none — {@carry.elems} of {@files.elems} files carry "
           ~ "#?rakudo directives and $ENGINE runs them as they are. Unless \$ROAST is a "
           ~ "checkout fudged by hand, this measures the UNFUDGED bar, which no published "
           ~ "figure is on (docs/status/COUNTING.md)."
            if @carry;
    }
    elsif !$fudge.e {
        note "run-roast: no `fudge` in $ROOT, so $ENGINE runs {@carry.elems} "
           ~ "directive-carrying files raw and the figures will understate it badly "
           ~ "(Rakudo fails 260 of the 280 such files raw). Point \$ROAST at a full "
           ~ "Roast checkout."
            if @carry;
    }
    else {
        for @carry -> $f {
            if $f.IO.slurp.contains("\nsay \"# FUDGED!\";") { $PREFUDGED += 1; next }
            # Two-argument fudge: `fudge IMPL FILE.t` writes FILE.IMPL beside it when a
            # directive applies and prints the path to run — the sidecar, or FILE.t
            # itself when nothing applied. --version is the language version the
            # `#?v6…` directives compare against; v6.d is what Rakudo targets and what
            # docs/status/roast-lists/rakudo-2026.08.list was measured with.
            my $p = run('perl', $fudge.Str, '--keep-exit-code', '--version=v6.d',
                        $FUDGE-IMPL, $f, :out, :err);
            my $picked = $p.out.slurp(:close).trim;
            my $e      = $p.err.slurp(:close).trim;
            if $picked && $picked ne $f && $picked.IO.e { %RUN-AS{$f} = $picked; $FUDGED += 1 }
            elsif $e { note "run-roast: fudge failed on $f: $e" }
        }
        note "run-roast: applied Roast's own fudge (--version=v6.d $FUDGE-IMPL): $FUDGED of "
           ~ "{@files.elems} files rewritten to a .$FUDGE-IMPL sidecar and run from there"
           ~ ($PREFUDGED ?? "; $PREFUDGED already fudged by hand, left alone" !! '') ~ ".";
    }
}

my $PROVENANCE = "{$ENGINE} {$ENGINE-VER} ($BIN) | roast {roast-revision()} ($ROOT)"
                ~ ($BEFORE ?? " + {$BEFORE.elems} untracked" !! '')
                ~ ($FUDGED ?? ", fudged with roast's fudge --version=v6.d $FUDGE-IMPL ($FUDGED files rewritten)" !! '')
                ~ ($PREFUDGED ?? ", $PREFUDGED files fudged by hand" !! '')
                ~ ($FOREIGN && $MUTSU && $MUTSU-FUDGE ?? ", MUTSU_FUDGE=1" !! '')
                ~ ($FOREIGN && $FUDGE-IMPL eq 'none' && !($MUTSU && $MUTSU-FUDGE) ?? ", --fudge=none (raw files)" !! '')
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
my $tot-todopass = 0;    # `ok … # todo` lines: real passes, but of tests marked as expected to fail
my $tot-ran = 0;
my $tot-pass = 0;
my $tot-plan = 0;
my $notap-declared = 0;   # tests declared by no-TAP files that never emitted a plan (all failing)
my $notap-counted  = 0;   # how many no-TAP files we recovered a static plan from
# Fudge directives over the run: verb -> [directives, passed, failed, skipped,
# no-result, files] (see fudge-directives).
my %fz;
my $fz-files = 0;   # distinct files with any directive (a file can have several verbs)
# Skip/todo tests no directive was credited with — the test code's own
# skip()/todo() calls, in any file: [todo-passed, todo-failed, skipped, files].
# With the directive rows they add up to the summary's left-out count.
my @fz-own = 0, 0, 0, 0;
my $notap-unknown  = 0;   # no-TAP files whose plan is dynamic/absent — uncountable
my $timeout-declared = 0; # tests declared by timed-out files that never emitted a plan
my $timeout-counted  = 0; # how many timed-out files we recovered a static plan from
my $timeout-unknown  = 0; # timed-out files with no static plan to recover
# Per-section rollups for the by-synopsis table.
#
# Every one of these is written with `+= 1`, never `++`. That is not a style
# choice: this harness is MEANT to be run under a foreign engine (see engine-id
# above), and mutsu 0.23.0 — the one foreign engine anybody actually points at
# it — silently drops `%h{$k}++` when it appears inside a named sub. The tally
# below is a named sub, so under mutsu every one of these hashes stayed empty
# while `$pass` and friends, being scalars, counted correctly: a run that looked
# right in every headline and printed a by-synopsis table with nothing in it.
# `%h{$k} += 1` works there and means the same thing everywhere.
my (%sec-full, %sec-part, %sec-time, %sec-notap, %sec-pass, %sec-tot);

# Files the harness never managed to measure. A missing result is not a failing
# file and not a passing one; it is a hole in the run, and the only wrong thing
# to do with it is nothing. They are counted, named, and their declared tests
# are charged to the denominator — see the final flush after the workers.
my $lost = 0;            # files with no result, or whose run threw
my $lost-declared = 0;   # tests those files declare, recovered from source
my @lost-files;

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
my @notpassing;     # [rel, mark, "passed/ran"] for every other file, for --failed
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
# Tests passed / tests discovered over the files finished so far, in completion
# order like $completed. "Discovered" is the file's plan where it emitted one,
# else what ran — the same per-file denominator tally() adds to $tot-plan.
my $live-pass = 0;
my $live-seen = 0;
# The per-file lines come out in FILE ORDER, and the first file in that order
# is a bulk file that starts late, so a terminal shows nothing for most of the
# run. This line, on stderr and only when stderr is a terminal, says what is
# happening in the meantime; the flush below wipes it before printing.
sub progress() {
    my ($d, $r, $tp, $ts) = $lock.protect({ ($completed, $running, $live-pass, $live-seen) });
    $*ERR.print(sprintf("\r  %d/%d done, %d running, %d/%d tests passed, %.0f s ",
                        $d, @files.elems, $r, $tp, $ts, (now - $T0).Num));
}
sub wipe-progress() { $*ERR.print("\r" ~ (' ' x 80) ~ "\r") }
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
    # %SLOW-FILES values are rakupp seconds too: a spec-driven wall time still has
    # the engine's own work wrapped around it, so they scale with everything else.
    my $cap = %SLOW-FILES{$rel} ?? %SLOW-FILES{$rel} * $TIME-SCALE !! $TIMEOUT;
    # a foreign engine gets fudge's sidecar where there is one; see the fudge pass
    my ($out, $timedout, $err) = run-with-timeout($BIN, %RUN-AS{$f} // $f, $cap);
    my $cpu = $lock.protect({ %cpu-sample{$f} });   # the last look the sampler took while it ran
    my ($planned, $ran, $passed, $failed, $skipped, $todofail, $todopass) = parse-tap($out);
    # New fields go on the END: the unpack below is positional. [9] is the
    # worker's error slot, set only when this sub throws.
    [$timedout, $planned, $ran, $passed, $failed, $out.contains('# SKIP'),
     $skipped, $todofail, $cpu, Nil,
     ($FAILED && !$FAILEDFILE && ($failed || $timedout) ?? failed-lines($err, $f) !! ()), # an Array stays one item
     fudge-directives($f, $out), $todopass];
}

# Record a file the harness could not measure: no result at all, or a run that
# threw. Its declared tests still go into the denominator — a file that leaves
# the ratio entirely is the one outcome that IMPROVES the headline, which is
# exactly the hole COUNTING.md's measure 4 was built to close for parse errors.
# Add one file's fudge-directives() to the run's totals.
my sub fudge-tally(%v, $skipped = 0, $todofail = 0, $todopass = 0) {
    $fz-files++ if %v;
    my @credited = 0, 0, 0;   # todo-passed, todo-failed, skipped
    for %v.kv -> $verb, @c {
        my $t = %fz{$verb} //= [0 xx 6];
        $t[$_] += @c[$_] for ^5;
        $t[5]++;
        @credited[$_] += @c[$_ + 1] for ^3;
    }
    my @own = $todopass - @credited[0], $todofail - @credited[1], $skipped - @credited[2];
    @fz-own[$_] += @own[$_] for ^3;
    @fz-own[3]++ if @own.any > 0;
}

my sub lose($k, $why) {
    my $rel = @files[$k].substr($ROOT.chars + 1);
    $lost += 1;
    @lost-files.push("$rel — $why");
    my $sp = static-plan(@files[$k]);
    $lost-declared += $sp if $sp > 0;
    fudge-tally(fudge-directives(@files[$k], ''));
    @notpassing.push([$rel, 'LOST', '—']);
    say sprintf('  [LOST]  %5s  %s', '—', $rel);
}

# Tally and print file $k. Called in file order, under $lock.
my sub tally($k) {
    my $f = @files[$k];
    my $rel = $f.substr($ROOT.chars + 1);
    my $sec = seckey($rel);
    my $r = @result[$k];
    my ($timedout, $planned, $ran, $passed, $failed, $has-skip) = $r[0], $r[1], $r[2], $r[3], $r[4], $r[5];
    my ($skipped, $todofail, $todopass) = $r[6] // 0, $r[7] // 0, $r[12] // 0;
    if ($r[9] // Nil).defined {   # run-one threw; the worker caught it and said so here
        lose($k, ~$r[9]);
        return;
    }
    fudge-tally($r[11] // {}, $skipped, $todofail, $todopass);
    if $timedout {
        $timeout++;
        %sec-time{$sec} += 1;
        # A timed-out file used to `return` right here, contributing nothing to
        # the numerator AND nothing to any denominator — its tests did not count
        # against the engine, they ceased to exist. That made the headline depend
        # on how much of the suite the engine could finish in time: kill enough
        # files and the ratio improves. It is the same hole measure 4 was built to
        # close for parse errors (see COUNTING.md), left open for the clock.
        #
        # A timeout is now scored exactly like a mid-plan abort: credit what it
        # emitted before the kill (run-with-timeout returns that output, and
        # run-one has already parsed it), and charge the rest of its plan. The
        # file bar is untouched — a timeout has never been a fully-passing file
        # and still is not, so the --list gate diffs the same as before.
        $tot-ran  += $ran;
        $tot-pass += $passed;
        $tot-skip += $skipped;
        $tot-todofail += $todofail;
        $tot-todopass += $todopass;
        %sec-pass{$sec} += $passed;
        %sec-tot{$sec}  += $ran;
        if $planned >= 0 {
            $tot-plan += $planned;          # it announced N before the clock ran out
        }
        else {
            # Killed before it could announce a plan. Recover N from source, the
            # way the no-TAP branch does; measure 3 is defined over files that
            # emitted a plan, so this lands in measure 4 only.
            my $sp = static-plan($f);
            if $sp > 0 { $timeout-declared += $sp; $timeout-counted++ }
            else       { $tot-plan += $ran; $timeout-unknown++ }
        }
        @notpassing.push([$rel, 'TIME', "$passed/$ran", $k]);
        say sprintf('  [TIME]  %5s  %s', "$passed/$ran", $rel);
        return;
    }
    $tot-ran  += $ran;
    $tot-pass += $passed;
    $tot-skip += $skipped;
    $tot-todofail += $todofail;
    $tot-todopass += $todopass;
    %sec-pass{$sec} += $passed;
    %sec-tot{$sec}  += $ran;
    # "planned" denominator: how many tests the file *intended* to run. Where a plan
    # is present we count it (so tests lost to a mid-file abort count as not-passed);
    # where none was emitted we fall back to what ran.
    $tot-plan += ($planned >= 0 ?? $planned !! $ran);
    my $mark;
    if $planned == 0 && $failed == 0 && $has-skip {
        $pass++;              # genuine `plan skip-all` (emits `1..0 # SKIP …`) is a passing outcome
        %sec-full{$sec} += 1;
        $mark = 'PASS';
    }
    elsif $ran == 0 {
        $noplan++;
        %sec-notap{$sec} += 1;
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
        %sec-full{$sec} += 1;
        $mark = 'PASS';
    }
    else {
        $partial++;
        %sec-part{$sec} += 1;
        $mark = 'part';
    }
    if $mark eq 'PASS'    { @fullypassing.push($rel) }
    elsif $mark eq 'part' { @notpassing.push([$rel, 'part', "$passed/$ran", $k]) }
    else                  { @notpassing.push([$rel, 'noTAP', '—']) }
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
# A generation counter, bumped whenever a file is taken or one finishes. When a
# scan finds nothing that fits the budget it records the generation it gave up
# at, and every later call returns -1 at once until something actually changes.
# Without it a full run rescans the whole ordered queue on each of its thousands
# of budget misses — 1.3–1.9 M queue entries walked over a 1,464-file suite, all
# of it under $lock. The gate is exact: $gen advances on exactly the two events
# that can change what fits, both of which happen under $lock, as take-next does.
my $gen = 0; my $miss-gen = -1;
my sub take-next() {
    while $head < @queue.elems && @taken[@queue[$head]] { $head++ }
    return Nil if $head >= @queue.elems;
    return -1 if $miss-gen == $gen;   # nothing freed since the last miss: don't rescan
    my $i = $head;
    while $i < @queue.elems {
        my $k = @queue[$i];
        # A file that only WAITS — near-zero estimated demand — is admitted
        # whatever the budget: it holds no core, so queuing it behind CPU-bound
        # work only defers a spec sleep that should have been ticking from second
        # zero. This is the fix that lets batch.t (36 s) and sleep.t (18 s) of
        # pure wall start at once instead of after the two S17-procasync spawners
        # free the budget ~6.8 s in — worth ~6 s off the whole run, which is
        # otherwise floor-bound by batch.t's own wait. `< 0.25` is the same
        # "known to wait" threshold the run's opening summary line reports.
        # (`$load < 1e-6`, not `== 0`: $load is a float sum of demands, and the
        # adds and subtracts in whatever order the files finish can leave a
        # residual like 1e-16 — then a file whose demand alone exceeds $CPU is
        # never admitted and every worker spins on -1 forever)
        if !@taken[$k] && (@demand[$k] < 0.25 || $load < 1e-6 || $load + @demand[$k] <= $CPU) {
            @taken[$k] = True;
            $running++;
            $load += @demand[$k];
            $gen++;
            return $k;
        }
        $i++;
    }
    $miss-gen = $gen;
    return -1;
}
my sub worker() {
    loop {
        my $k = $lock.protect({ take-next() });
        last if !$k.defined;
        if $k == -1 { sleep 0.02; next }
        my $t0 = now;
        # If run-one throws, the `start` block dies, `await` below rethrows, and
        # a four-minute run ends with no summary at all — one file taking the
        # whole measurement down with it. Catch it here instead: the file is
        # scored as unmeasured (see lose) and the worker goes back to the queue.
        my $r   = try { run-one(@files[$k]) };
        my $why = $r.defined ?? Nil !! ($! ?? ~$!.message !! 'run-one failed');
        $r = [False, -1, 0, 0, 0, False, 0, 0, Nil, $why] unless $r.defined;
        my $dt = (now - $t0).Num;
        $lock.protect({
            $load -= @demand[$k];
            $gen++;                 # budget freed: let a parked scan try again
            @wall[$k]   = $dt;
            @result[$k] = $r;
            $running--;
            $completed++;
            $live-pass += $r[3];
            $live-seen += $r[1] >= 0 ?? $r[1] !! $r[2];
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

# The workers' flush advances only over a CONTIGUOUS defined prefix of the file
# list, because the per-file lines have to come out in file order. That is right
# while the run is in flight and wrong the moment it ends: one file whose result
# never arrived hides every file behind it, and those files then left the tally
# as ABSENCES — not counted as failures, not counted at all, gone from both
# sides of every ratio and from the by-synopsis table. A mutsu run reported
# 1,037 + 181 + 22 + 4 = 1,244 files of 1,464 that way, with nothing in the
# output saying the other 220 were missing. Nothing is running now, so tally
# whatever is left, and count what has no result rather than skip past it.
$lock.protect({
    while $flushed < @files.elems {
        if @result[$flushed].defined { tally($flushed) }
        else                         { lose($flushed, 'no result: the worker that took it never came back') }
        $flushed += 1;
    }
});
if $lost {
    note "";
    note "run-roast: $lost file{$lost == 1 ?? '' !! 's'} produced no result. The run is INCOMPLETE: "
       ~ "{$lost == 1 ?? 'its' !! 'their'} declared";
    note "  tests are charged to the denominator, but nothing else about "
       ~ "{$lost == 1 ?? 'it' !! 'them'} was measured.";
    note "  $_" for @lost-files.head(8);
    note "  …and {@lost-files.elems - 8} more" if @lost-files > 8;
    # Where the missing results come from, when they come from anywhere: the work
    # queue is shared mutable state across the worker threads, and a foreign
    # engine's threads are its own. mutsu 0.23.0 hands the same file to several
    # workers and leaves others unrun — instrumented 2026-09-21 on
    # S17-supply/watch-path.t, a file whose 60 s timeout leaves a worker parked
    # long enough for the shared cursor to drift. One worker has no queue to race.
    note $FOREIGN
        ?? "  A foreign engine's threads are its own: mutsu 0.23.0 hands one file to "
         ~ "several workers and leaves others unrun. Re-run with --workers=1 for a "
         ~ "figure worth quoting."
        !! "  Re-run those files to measure them.";
}

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

my $declared = $tot-plan + $notap-declared + $timeout-declared + $lost-declared;  # every test any file declares it will run
my $fpct  = @files.elems ?? 100 * $pass     / @files.elems !! 0;
my $rpct  = $tot-ran     ?? 100 * $tot-pass / $tot-ran     !! 0;
my $ppct  = $tot-plan    ?? 100 * $tot-pass / $tot-plan    !! 0;
my $dpct  = $declared    ?? 100 * $tot-pass / $declared    !! 0;
say "";
say "Files: ", @files.elems, "   fully-pass: ", $pass,
    "   partial: ", $partial, "   no-TAP: ", $noplan, "   timeout: ", $timeout,
    ($lost ?? "   LOST: $lost" !! '');
# Every file lands in exactly one of those buckets, so they add up to the file
# count — and when they do not, every figure below is over a subset of the suite
# that nothing else in the output names. Say so on stderr, next to the line that
# is wrong, rather than leaving the arithmetic to the reader.
{
    my $seen = $pass + $partial + $noplan + $timeout + $lost;
    if $seen != @files.elems {
        note "run-roast: ACCOUNTING CHECK FAILED — $seen files categorised of {@files.elems}. "
           ~ "The figures below cover {$seen} files, not the suite.";
    }
}
say sprintf("Wall time:            %.1f s  (%d workers)", (now - $T0).Num, $WORKERS);
say sprintf("Files fully passing:  %d / %d  (%.2f%%)", $pass, @files.elems, $fpct);
say sprintf("Assertions passed:    %d / %d  (%.2f%%)  of tests that ran", $tot-pass, $tot-ran, $rpct);
say sprintf("Assertions passed:    %d / %d  (%.2f%%)  of tests planned by files that emitted a plan", $tot-pass, $tot-plan, $ppct);
say sprintf("Assertions passed:    %d / %d  (%.2f%%)  of ALL declared tests (+%d from %d no-TAP and +%d from %d timed-out files, read from source; %d more have no static plan)%s",
            $tot-pass, $declared, $dpct, $notap-declared, $notap-counted,
            $timeout-declared, $timeout-counted, $notap-unknown + $timeout-unknown,
            ($lost ?? sprintf(" — and +%d from %d LOST file%s, which %s not measured at all", $lost-declared, $lost, $lost == 1 ?? '' !! 's', $lost == 1 ?? 'was' !! 'were') !! ''));
# Only the tests with no skip or todo on them, on both sides. The headline counts
# `ok … # skip` and `not ok … # todo` as passes; here every skipped or
# todo-marked test (a todo that passes too) leaves the passed count AND the
# declared total. Of the tests expected to pass, how many do. The fudge table
# below splits the same left-out tests by where their skip/todo came from.
my $fudged    = $tot-skip + $tot-todofail + $tot-todopass;
my $must-pass = $declared - $fudged;
my $do-pass   = $tot-pass - $fudged;
say sprintf("Assertions passed without skip/todo: %d / %d  (%.2f%%)  left out: %d skipped + %d todo-failed + %d todo-passed = %d",
            $do-pass, $must-pass, $must-pass ?? 100 * $do-pass / $must-pass !! 0,
            $tot-skip, $tot-todofail, $tot-todopass, $fudged);

# ---- Where the left-out tests' skip/todo came from: each #?rakudo verb, and
# the test code's own skip()/todo() calls (anything no directive was credited
# with, in any file). Counted with parse-tap's rule, so the Total row is the
# summary's left-out breakdown exactly. Tests, not directive lines: `#?rakudo 3
# skip` is three. A todo that PASSES is a directive the engine has outgrown.
say "";
{
    my @verbs = %fz.keys.sort({ -(%fz{$_}[1] + %fz{$_}[2] + %fz{$_}[3]), $_ });
    my @fh = <Source Files Skipped Todo-failed Todo-passed Tests No-result>;
    my @fr;
    my @sum = 0, 0, 0, 0;   # skipped, todo-failed, todo-passed, no-result
    for @verbs -> $v {
        my ($n, $p, $fl, $sk, $nr, $files) = @(%fz{$v});
        if $v eq 'emit' { @fr.push(["#?rakudo emit", ~$files, '—', '—', '—', '—', '—']); next }
        @fr.push(["#?rakudo $v", ~$files, ~$sk, ~$fl, ~$p, ~($sk + $fl + $p), ~$nr]);
        @sum[0] += $sk; @sum[1] += $fl; @sum[2] += $p; @sum[3] += $nr;
    }
    my ($op, $of, $os, $ofiles) = @fz-own;
    @fr.push(['skip()/todo() in test code', ~$ofiles, ~$os, ~$of, ~$op, ~($os + $of + $op), '—']);
    my @t = @sum[0] + $os, @sum[1] + $of, @sum[2] + $op;
    @fr.push(['Total', '—', ~@t[0], ~@t[1], ~@t[2], ~([+] @t), ~@sum[3]]);
    my @fw = (^@fh).map(-> $i { (@fh[$i], |@fr.map(*[$i])).map(*.chars).max });
    # a named sub, not a pointy block in a `&row` variable: mutsu 0.23.0 binds the
    # array argument of the latter empty and printed this table as `|  |` (the
    # harness has to run on the engine it measures — see COUNTING.md)
    sub row(@c) { '| ' ~ (^@c).map(-> $i { $i == 0 ?? @c[$i] ~ ' ' x (@fw[$i] - @c[$i].chars)
                                                  !! ' ' x (@fw[$i] - @c[$i].chars) ~ @c[$i] }).join(' | ') ~ ' |' }
    say "Skipped and todo tests by source ({$fz-files} of {@files.elems} files carry #?rakudo directives"
        ~ (!%fz && $FOREIGN ?? '; a pre-fudged checkout?' !! '') ~ "):";
    say row(@fh);
    say '|' ~ (^@fh).map({ $_ == 0 ?? '-' x (@fw[$_] + 2) !! ('-' x (@fw[$_] + 1)) ~ ':' }).join('|') ~ '|';
    say row($_) for @fr;
    say "Todo-passed is a todo the engine has outgrown; No-result, directive lines whose tests never appeared "
      ~ "in the output. Files for the test-code row: files with a skip/todo no directive accounts for.";
}

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
    my $pct = $b ?? sprintf('%.2f%%', 100 * $a / $b) !! '—';
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

# ---- --failed: every file that did not fully pass, printed last so it is the
# thing left on screen. Sorted by path, the order --list uses.
if $FAILED {
    my @nf = @notpassing.sort(*[0]);
    if $FAILEDFILE {
        $FAILEDFILE.IO.spurt(@nf.map(*[0]).join("\n") ~ (@nf ?? "\n" !! ''));
        say "";
        say "Not-passing file list ({@nf.elems} paths) -> $FAILEDFILE";
    }
    else {
        say "";
        say "Files not fully passing ({@nf.elems}):";
        for @nf -> $e {
            say sprintf('  %-7s  %9s  %s', "[$e[1]]", $e[2], $e[0]);
            next unless $e[3].defined;
            my @n = @(@result[$e[3]][10] // ());
            next unless @n;
            my @src = @files[$e[3]].IO.lines;
            for @n.head($FAILED-SHOW) -> $n {
                say sprintf('        %5d: %s', $n, (@src[$n - 1] // '').trim);
            }
            say "               …and {@n.elems - $FAILED-SHOW} more failing line{@n.elems - $FAILED-SHOW == 1 ?? '' !! 's'}"
                if @n > $FAILED-SHOW;
        }
    }
}
