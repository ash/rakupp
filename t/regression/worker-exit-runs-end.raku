# An `exit` on a worker thread runs the END phasers before it ends the
# process, as Rakudo's does: `END say "end"; start { exit 3 }; sleep 1`
# printed nothing and ended with status 3, where Rakudo prints "end" first.
# The status is the worker's, an `exit` inside an END there included, and
# an unhandled error in cued code (status 1) runs them too. A mainline
# `exit` is as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub child($code) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    my $out = $p.out.slurp(:close); $p.err.slurp(:close);
    ($out.lines.List, $p.exitcode)
}

ck child(q[END say "end"; start { exit 3 }; sleep 2; say "main"]), (("end",), 3), 'a worker';
ck child(q[END { say "end"; exit 5 }; start { exit 3 }; sleep 2]), (("end",), 3), "an END's exit keeps the worker's status";
ck child(q[END say "e1"; END say "e2"; start { exit 0 }; sleep 2]), (<e2 e1>, 0), 'every END, last first';
ck child(q[END say "end"; await start { exit 4 }; say "main"]), (("end",), 4), 'awaited';
ck child(q[END say "end"; $*SCHEDULER.cue({ die "x" }); sleep 2; say "main"]), (("end",), 1), 'an unhandled error in cued code';
ck child(q[END say "end"; say "main"; exit 2]), (<main end>, 2), 'the mainline, as before';

say 'PASS';
