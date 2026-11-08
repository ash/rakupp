# A tapper that dies on a value a supply delivers from `Supply.interval` or a
# Channel hands its error to the scheduler, as Rakudo's does: it is reported
# and ends the process with status 1, or goes to `uncaught_handler` when one
# is set. Here it quit the supply instead, and the program ran on. With a
# handler a Channel goes on delivering, and an interval stops ticking, as
# Rakudo's `:every` cue does. A die in the supply's own `whenever` still
# quits it.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub ran($code) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    $p.out.slurp(:close);
    my $err = $p.err.slurp(:close).lines.grep(none /^ \s* ['in ' | \d+ ' |'] /, /libcrypto/, '').join('|');
    ($err.subst(/'thread ' \d+/, 'thread N'), $p.exitcode)
}
my $un = 'Unhandled exception in code scheduled on thread N';

ck ran('supply { whenever Supply.interval(0.02) { emit $_ } }.tap(-> $ { die "tapper" }, quit => { note "quit" }); sleep 1; note "end"'),
   ("$un|tapper", 1), 'an interval';
ck ran('my $c = Channel.new; supply { whenever $c { emit $_ } }.tap(-> $ { die "tapper" }, quit => { note "quit" }); $c.send(1); sleep 1; note "end"'),
   ("$un|tapper", 1), 'a Channel';
ck ran('$*SCHEDULER.uncaught_handler = -> $e { note "h: ", $e.message }; my $c = Channel.new; supply { whenever $c { emit $_ } }.tap(-> $v { die "tapper $v" }, quit => { note "quit" }); $c.send(1); $c.send(2); sleep 0.5; note "end"'),
   ('h: tapper 1|h: tapper 2|end', 0), 'a Channel with a handler goes on';
ck ran('my $n = 0; $*SCHEDULER.uncaught_handler = -> $e { $n++ }; supply { whenever Supply.interval(0.02) { emit $_ } }.tap(-> $ { die "tapper" }, quit => { note "quit" }); sleep 0.5; note $n; note "end"'),
   ('1|end', 0), 'an interval with a handler stops';
ck ran('supply { whenever Supply.interval(0.02) { die "own" } }.tap(-> $ { }, quit => { note "quit" }); sleep 0.3; note "end"'),
   ('quit|end', 0), "the supply's own die quits it";

say 'PASS';
