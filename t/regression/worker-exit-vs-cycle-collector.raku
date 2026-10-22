# A worker thread's count in liveWorkers_ is given back when the thread has
# let go of everything, not at the end of its body. After the body, the
# closure's captures go, and at the thread's exit its ExecContext and frame
# pools: releases that free containers and note cycle candidates. With the
# count already 0, the cycle collector (it runs on the main thread when no
# worker is live) walked containers a finishing worker was freeing, and the
# process died of SIGSEGV in hashEdges. Roast's S07-hyperrace/basics.t did,
# about one run in six; this shape, with a collection at every scope exit
# (RAKUPP_CYCLE_TRIGGER=1), about two runs in three.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $prog = q:to/END/;
    sub outer($reps) {
        my atomicint $done = 0;
        my sub does-fail($seq) {
            my $starter = Promise.new; my @ready; my @w;
            for ^10 -> $widx {
                @ready.push: my $worker_ready = Promise.new;
                @w.push: start { $worker_ready.keep; await $starter; my $rc = try $seq.iterator; with $rc { 1 } else { 0 } }
            }
            await @ready; $starter.keep;
            await Promise.allof(@w).then({ ++⚛$done });
        }
        does-fail((^10).race) for ^$reps;
        $done
    }
    say outer(6000);
    END
my %env = %*ENV;
%env<RAKUPP_CYCLE_TRIGGER> = 1;
for ^6 -> $run {
    my $p = run($*EXECUTABLE.absolute, '-e', $prog, :out, :err, :%env);
    my $out = $p.out.slurp(:close).trim;
    $p.err.slurp(:close);
    @fail.push("run $run: exit {$p.exitcode} signal {$p.signal}, {$out.raku}")
        unless $p.exitcode == 0 && $out eq '6000';
}
if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
