# A sunk `start` whose code dies is reported and ends the process, as code
# cued on the scheduler does: "Unhandled exception in code scheduled on
# thread N", the error, status 1. It was silent and the program ran on. The
# scheduler's `uncaught_handler` gets it when one is set; a `start` that is
# kept, assigned or awaited is not reported; and only a `start`'s promise is,
# as Rakudo marks it: a sunk `Promise.start` or a sub's `Promise.broken` stay
# silent. A sunk call that answers a `start` and `start … for …` are sunk too.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub ran($code) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close).lines.grep(none /^ \s* ['in ' | \d+ ' |'] /, /libcrypto/, /Potential|Useless|'------>'|^ \s* 'at '/, '').join('|');
    ($out, $err.subst(/'thread ' \d+/, 'thread N'), $p.exitcode)
}
my $un = 'Unhandled exception in code scheduled on thread N';

ck ran('print "out"; start { die "s" }; sleep 1; note "end"'), ('out', "$un|s", 1), 'a sunk start';
ck ran('sub f { start { die "r" } }; f(); sleep 1; note "end"'), ('', "$un|r", 1), 'a sunk call answering one';
ck ran('start { die "s" } for ^1; sleep 1; note "end"'), ('', "$un|s", 1), 'start … for';
ck ran('start { fail "f" }; sleep 1; note "end"'), ('', "$un|f", 1), 'a fail';
ck ran('start { sleep 0.05; die "late" }; sleep 1; note "end"'), ('', "$un|late", 1), 'one that dies later';
ck ran('$*SCHEDULER.uncaught_handler = -> $e { note "h: ", $e.message }; start { die "s" }; sleep 0.5; note "end"'),
   ('', 'h: s|end', 0), 'uncaught_handler';
ck ran('my $p = start { die "s" }; sleep 0.3; note "end"'), ('', 'end', 0), 'assigned';
ck ran('start { 42 }; sleep 0.3; note "end"'), ('', 'end', 0), 'kept';
ck ran('sub g { Promise.broken("x") }; g(); sleep 0.3; note "end"'), ('', 'end', 0), "a sub's Promise.broken";
ck ran('Promise.start({ die "y" }); sleep 0.3; note "end"'), ('', 'end', 0), 'Promise.start';

say 'PASS';
