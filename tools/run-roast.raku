#!/usr/bin/env rakupp
# Roast test harness, self-hosted in Raku and run by rakupp itself.
#
# Usage:
#   build/rakupp tools/run-roast.raku [--workers=N] [--list=FILE] [PATTERN ...]
#
# With no PATTERN, runs every .t file under $ROOT. A PATTERN is matched as a
# substring against the path. --workers=N runs N test files at a time: each
# file's subprocess runs from a `start` worker, and the interpreter parks the
# GIL while a worker waits on its child, so the children genuinely overlap.
# Results are tallied and printed in file order regardless of N.
#
# --list=FILE writes the fully-passing file paths, one per line, sorted. THAT is
# what a release diff should compare — RELEASING.md calls the file list the gate,
# and reconstructing it with `grep '[PASS]' | awk '{print $NF}'` over decorated
# human-readable output makes the gate only as reliable as that output's framing.
# It was not reliable: see the stderr note in run-with-timeout.

my $ROOT    = (%*ENV<ROAST> // '/Users/ash/roast').IO.absolute;  # set $ROAST to your Roast checkout
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
    'S17-supply/batch.t'   => 30,  # batch(:seconds(5)): aligns to 5 s periods, twice
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

# Run a test file, capturing stdout with a hard timeout (idiomatic Proc::Async + Promise).
# Returns (output-string, timed-out-bool).
sub run-with-timeout($bin, $file, $timeout) {
    my $proc = Proc::Async.new($bin, $file);
    my $out = '';
    $proc.stdout.tap(-> $chunk { $out ~= $chunk });
    # …and STDERR, which must be captured even though nothing reads it. An
    # untapped Proc::Async stderr is INHERITED, so at --workers=4 the children's
    # TAP diagnostics were written straight into the parent's own stream and
    # spliced mid-line into its per-file status lines — consistently four a run:
    #     [PASS]    4/4  S03-smar# Failed test '$obj ~~ Pair, nonexistent, …
    # The tallies survived that (they are computed from $out), but RELEASING.md
    # calls the file LIST the gate, and that list is `awk '{print $NF}'` over
    # these lines — so four files a run silently lost their path and read as
    # regressions, and the site's roast map inherited the undercount.
    $proc.stderr.tap(-> $chunk { });
    my $done = $proc.start(:cwd($SCRATCH.absolute));
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

my $WORKERS = 1;
my $LISTFILE;
my @patterns;
for @*ARGS -> $a {
    if $a ~~ /^ '--workers=' (\d+) $/ { $WORKERS = (+$0) max 1 }
    elsif $a ~~ /^ '--list=' (.+) $/  { $LISTFILE = ~$0 }
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
my $tot-ran = 0;
my $tot-pass = 0;
my $tot-plan = 0;
my $notap-declared = 0;   # tests declared by no-TAP files that never emitted a plan (all failing)
my $notap-counted  = 0;   # how many no-TAP files we recovered a static plan from
my $notap-unknown  = 0;   # no-TAP files whose plan is dynamic/absent — uncountable
# Per-section rollups for the by-synopsis table.
my (%sec-full, %sec-part, %sec-time, %sec-notap, %sec-pass, %sec-tot);

# Run the files in batches of $WORKERS. Each batch's subprocesses overlap (the
# GIL is parked while a worker waits on its child); parsing and tallying happen
# on the main thread afterwards, in file order, so output and totals match a
# sequential run.
my @fullypassing;   # the release gate's file LIST, collected as data not as text
my $next = 0;
while $next < @files.elems {
    my $hi = ($next + $WORKERS) min @files.elems;
    my @batch = @files[$next ..^ $hi];
    # Each worker runs its file AND parses the TAP, so parsing overlaps with the
    # other workers' child processes instead of serialising between batches.
    my sub run-one($f) {
        my $rel = $f.substr($ROOT.chars + 1);
        my ($out, $timedout) = run-with-timeout($BIN, $f, %SLOW-FILES{$rel} // $TIMEOUT);
        my ($planned, $ran, $passed, $failed) = parse-tap($out);
        [$timedout, $planned, $ran, $passed, $failed, $out.contains('# SKIP')]; # an Array stays one item
    }
    my @outs;
    if $WORKERS > 1 && @batch.elems > 1 {
        my @promises = @batch.map(-> $f { start run-one($f) });
        @outs = await @promises;
    }
    else {
        @outs.push(run-one($_)) for @batch; # push keeps each tuple one item
    }
    for ^@batch.elems -> $k {
    my $f = @batch[$k];
    my $rel = $f.substr($ROOT.chars + 1);
    my $sec = seckey($rel);
    my $r = @outs[$k];
    my ($timedout, $planned, $ran, $passed, $failed, $has-skip) = $r[0], $r[1], $r[2], $r[3], $r[4], $r[5];
    if $timedout {
        $timeout++;
        %sec-time{$sec}++;
        say "  [TIME]          ", $rel;
        next;
    }
    $tot-ran  += $ran;
    $tot-pass += $passed;
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
    $next = $hi;
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
say sprintf("Files fully passing:  %d / %d  (%.1f%%)", $pass, @files.elems, $fpct);
say sprintf("Assertions passed:    %d / %d  (%.1f%%)  of tests that ran", $tot-pass, $tot-ran, $rpct);
say sprintf("Assertions passed:    %d / %d  (%.1f%%)  of tests planned by files that emitted a plan", $tot-pass, $tot-plan, $ppct);
say sprintf("Assertions passed:    %d / %d  (%.1f%%)  of ALL declared tests (+%d from %d no-TAP files read from source; %d more have no static plan)",
            $tot-pass, $declared, $dpct, $notap-declared, $notap-counted, $notap-unknown);

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
say "| Section | Theme | Full | Part | Time | No-TAP | Assertions | % |";
say "|---|---|---:|---:|---:|---:|---:|---:|";
for @secs -> $s {
    my $a = %sec-pass{$s} // 0;
    my $b = %sec-tot{$s}  // 0;
    my $pct = $b ?? sprintf('%d%%', (100 * $a / $b).round) !! '—';
    say sprintf('| %s | %s | %d | %d | %d | %d | %d/%d | %s |',
        $s, (%theme{$s} // '—'),
        (%sec-full{$s} // 0), (%sec-part{$s} // 0), (%sec-time{$s} // 0), (%sec-notap{$s} // 0),
        $a, $b, $pct);
}
