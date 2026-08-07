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
# an ordinary failure. Additionally, RAKUPP_STRESS_TSAN=1 SKIPS the
# parallel-mode cases entirely: the interpreter's own internals still race
# under parallelism (phase P2 — first named item: the one-shot call
# registers topicWriteback_/pendingRwSlots_/noAutothread_/loopPhaserCtl_
# at Interpreter.cpp ~8912 are plain members, not thread-locals), so under
# TSan only GIL mode is held strict until P2 lands. Skipped, not
# run-and-tolerated: a tolerated run buys no signal, costs minutes of
# TSan slowdown, and once WEDGED this suite for 25 minutes when the
# channel hang outlived run()'s :timeout under TSan scheduling — itself a
# noted P2-adjacent finding. Native runs enforce both modes; nothing is
# masked, one flag states the truth.

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
# suite nor stales the list. Promotion out of here requires the P3 fix, not
# a green run.
my %known-flaky =
    'ub-array-push/parallel' => 'P3: unguarded array race can abort the runtime',
    'ub-hash-write/parallel' => 'P3: unguarded hash race can abort the runtime',
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
        if %*ENV<RAKUPP_STRESS_TSAN> && $mode eq 'parallel' {
            say "ok - $id # SKIP under TSan (P2 backlog: runtime internals race under parallelism)";
            next;
        }
        $ran++;
        my %env = %*ENV;
        %env<RAKUPP_PARALLEL> = '1' if $mode eq 'parallel';
        %env<RAKUPP_PARALLEL>:delete if $mode eq 'gil';
        my $p = run($*EXECUTABLE, $prog, :out, :err, :env(%env), :timeout(30));
        my $out  = $p.out.slurp(:close);
        my $err  = $p.err.slurp(:close);
        my $ok   = $p.exitcode == 0 && ($out.lines.tail // '') eq 'PASS';
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
        }
    }
}
say "# $ran cases: {$ran - $failed - $tolerated - $stale} pass, "
    ~ "$tolerated known-bad, $failed new failures, $stale stale list entries";
exit(($failed || $stale) ?? 1 !! 0);
