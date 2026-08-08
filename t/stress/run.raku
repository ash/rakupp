# The concurrency stress suite (v3 parallelism campaign, phase P1 —
# docs/dev/plans/PARALLEL-PLAN.md). Every program runs in BOTH modes (GIL
# and RAKUPP_PARALLEL=1) under the same binary that runs this file; a
# program passes when it exits 0 with a final line of PASS.
#
#   build/rakupp t/stress/run.raku            # the whole matrix
#   build/rakupp t/stress/run.raku --only=lock-counter
#
# THE RATCHET: %known-bad lists case IDs ("program/mode") that fail today —
# the campaign's visible backlog. A known-bad case that FAILS is reported
# and tolerated; one that PASSES is an error too — the list must shrink in
# the same commit that fixes the case, so it can never go stale. A failure
# NOT on the list fails the suite outright.
#
# Under a ThreadSanitizer build, run with TSAN_OPTIONS=exitcode=66 (the CI
# job does): a detected race turns into a non-zero exit and lands here as
# an ordinary failure. RAKUPP_STRESS_TSAN=1 additionally consults
# %tsan-parallel-skip below — the per-program ratchet that replaced the
# original blanket parallel-skip once P2 drove the interpreter's own races
# to zero on the correctness programs. An entry leaves that list the same
# way known-bad entries leave theirs: by being fixed. (History note: the
# blanket skip existed because a tolerated TSan run buys no signal, costs
# minutes of slowdown, and once wedged this suite for 25 minutes when a
# channel hang outlived run()'s :timeout under TSan scheduling.)

# The suite's FIRST RUN (2026-08-07) put three entries here — the plan's P4
# phase ("some primitives serialize via the GIL today") had named them in
# advance. ALL THREE were fixed the day they were found: atomic-counter (the
# lexer used to DROP the ⚛ marker; real striped-lock atomics now),
# channel-pipeline (the queue was a bare vector — every Channel op now runs
# under the channel's stripe), and supply-fanin (Supplier emissions are now
# genuinely serialized per supplier, the Rakudo contract). The hash stands
# empty, waiting for the next honest entry.
my %known-bad;
# Nondeterministic cases: a crash that only SOMETIMES happens. Tolerated in
# both directions — a lucky pass proves nothing, so it neither fails the
# suite nor stales the list. Promotion out requires the fix, not a green
# run — and that is how the P3 pair LEFT this list (2026-08-08): array
# growth and hash find-or-insert now run under the container's stripe in
# parallel mode (ParStripe), so the unguarded races those two programs
# stage can garble values but no longer abort the runtime (40/40 hammer
# runs). Residual crash shapes the CONTRACT does not yet cover — and the
# next stress programs should stage: iteration during mutation, object
# attribute races, and same-slot torn copies of pointer-carrying values.
my %known-flaky;
# The TSan-parallel ratchet (2026-08-08): the blanket parallel-skip is GONE —
# seven correctness programs now run STRICT under ThreadSanitizer in parallel
# mode (atomic-counter, channel-pipeline, hash-guarded, lock-counter,
# parallel-map, supply-fanin — all zero-report). What still skips, and why:
my %tsan-parallel-skip =
    'promise-chain'          => 'P2 residue: 3 reports left in eval — the last of the class',
    # the ub-* family stages DELIBERATE user races: under TSan every run is a
    # detected race by design, so the sanitizer leg proves nothing by running
    # them (the NATIVE matrix is where their no-crash contract is enforced)
    'ub-array-push'          => 'stages a deliberate race',
    'ub-hash-write'          => 'stages a deliberate race',
    'ub-iterate-during-push' => 'stages a deliberate race',
    'ub-object-attrs'        => 'stages a deliberate race',
    'ub-torn-values'         => 'stages a deliberate race',
    'ub-env-sharing'         => 'stages a deliberate race',
;
# Run-and-tolerate under TSan (2026-08-08): cases that RUN in the TSan leg so
# every log carries their SUMMARY lines, but whose reports are known and
# tolerated until fixed. Same shrink-only rule as %known-bad: coming back
# clean is an error that forces the entry out. First entry: Linux TSan (the
# CI runner's scheduler — macOS TSan is clean here) reports a race in
# atomic-counter/parallel while the COUNTER ITSELF is exact (stdout PASS) —
# the promise/await handshake class, promise-chain's sibling.
my %tsan-parallel-racy =
    'atomic-counter' => 'P2 residue: await-handshake report on Linux TSan; counter exact',
;

my $dir  = $?FILE.IO.parent;
my $only = @*ARGS.first(*.starts-with('--only='));
$only .= substr(7) with $only;

my @programs = dir($dir).map(*.Str)
    .grep(*.ends-with('.raku'))
    .grep({ !.ends-with('run.raku') })
    .sort;

my ($ran, $failed, $tolerated, $stale) = 0, 0, 0, 0;
for @programs -> $prog {
    my $name = $prog.IO.basename.subst('.raku', '');
    next if $only && $name ne $only;
    for <gil parallel> -> $mode {
        my $id = "$name/$mode";
        if %*ENV<RAKUPP_STRESS_TSAN> && $mode eq 'parallel'
           && (%tsan-parallel-skip{$name}:exists) {
            say "ok - $id # SKIP under TSan ({%tsan-parallel-skip{$name}})";
            next;
        }
        my $tsan-racy = %*ENV<RAKUPP_STRESS_TSAN> && $mode eq 'parallel'
                        && (%tsan-parallel-racy{$name}:exists);
        $ran++;
        my %env = %*ENV;
        %env<RAKUPP_PARALLEL> = '1' if $mode eq 'parallel';
        %env<RAKUPP_PARALLEL>:delete if $mode eq 'gil';
        my $p = run($*EXECUTABLE, $prog, :out, :err, :env(%env), :timeout(30));
        my $out  = $p.out.slurp(:close);
        my $err  = $p.err.slurp(:close);
        my $ok   = $p.exitcode == 0 && ($out.lines.tail // '') eq 'PASS';
        # surface the sanitizer's own verdict wherever there is one — a TSan
        # failure in a CI log must NAME its site without a rerun
        my @tsan-summaries = $err.lines.grep(*.starts-with('SUMMARY: ThreadSanitizer'));
        if $tsan-racy {
            # these entries are PLATFORM-dependent (Linux TSan schedules races
            # macOS never sees), so a clean local run is not proof — it passes
            # with a note, and the Linux CI log is the referee for removal
            $tolerated++;
            say "ok - $id # TSAN-KNOWN-RACY, {$ok && !@tsan-summaries ?? 'clean this run' !! 'reported'} ({%tsan-parallel-racy{$name}})";
            note "  --- $_" for @tsan-summaries.head(3);
            next;
        }
        if %known-flaky{$id}:exists {
            $tolerated++;
            say "ok - $id # FLAKY-KNOWN-BAD, {$ok ?? 'passed this time' !! 'failed this time'} ({%known-flaky{$id}})";
            next;
        }
        if $ok && !(%known-bad{$id}:exists) {
            say "ok - $id";
        }
        elsif !$ok && (%known-bad{$id}:exists) {
            $tolerated++;
            say "ok - $id # KNOWN-BAD ({%known-bad{$id}})";
        }
        elsif $ok && (%known-bad{$id}:exists) {
            $stale++;
            say "not ok - $id # PASSES but is on the known-bad list — remove it";
        }
        else {
            $failed++;
            say "not ok - $id (exit {$p.exitcode})";
            note "  --- stdout: {$out.lines.tail // ''}";
            note "  --- stderr: {$err.lines.head // ''}" if $err;
            note "  --- $_" for @tsan-summaries.head(3);
        }
    }
}
say "# $ran cases: {$ran - $failed - $tolerated - $stale} pass, "
    ~ "$tolerated known-bad, $failed new failures, $stale stale list entries";
exit(($failed || $stale) ?? 1 !! 0);
