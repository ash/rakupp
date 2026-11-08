# Code cued on the scheduler that dies with nothing to catch it is reported
# and ends the process, as Rakudo's ThreadPoolScheduler does: "Unhandled
# exception in code scheduled on thread N", the error and its backtrace,
# exit status 1. It was silent, and the program ran on. The scheduler's
# `uncaught_handler` gets it when one is set, and the cue's `:catch` before
# that, and then the program goes on; a cue with `:in` dies the same way.
# What the program printed before is not lost.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub ran($code) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close).lines.grep(none /^ \s* ['in ' | \d+ ' |'] /, /libcrypto/, '').join('|');
    ($out, $err.subst(/'thread ' \d+/, 'thread N'), $p.exitcode)
}

ck ran('print "out"; $*SCHEDULER.cue({ die "boom" }); sleep 1; note "end"'),
   ('out', 'Unhandled exception in code scheduled on thread N|boom', 1), 'no handler';
ck ran('$*SCHEDULER.cue({ die "boom" }, :in(0.05)); sleep 1; note "end"'),
   ('', 'Unhandled exception in code scheduled on thread N|boom', 1), ':in';
ck ran('$*SCHEDULER.uncaught_handler = -> $e { note "handled: ", $e.message }; $*SCHEDULER.cue({ die "boom" }); sleep 0.3; note "end"'),
   ('', 'handled: boom|end', 0), 'uncaught_handler';
ck ran('$*SCHEDULER.cue({ die "boom" }, :catch(-> $e { note "caught: ", $e.message })); sleep 0.3; note "end"'),
   ('', 'caught: boom|end', 0), ':catch';
ck ran('$*SCHEDULER.cue({ note "fine" }); sleep 0.3; note "end"'), ('', 'fine|end', 0), 'a cue that lives';

say 'PASS';
