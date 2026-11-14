# Regression (rakudo#4853, rakudo#3686): two ways a `react` never ended.
#
# 1. `whenever $ch.closed { done }` never fired when the channel closed after
#    the react had started. Closing kept the `.closed` Promise for `await`
#    (it set the state and woke the condition variable) but never ran its
#    `.then` continuations, and those are how a react hears of a promise. The
#    reader a `whenever $ch` runs, having taken the last value of a closed
#    channel, did not keep `.closed` at all.
# 2. A `whenever $promise` body waits until no other body of its react runs.
#    The reader behind `whenever $ch` counted as running for as long as it
#    polled, so with an empty channel beside it the promise's body never ran:
#    `react { whenever $stop { done }; whenever $c { } }` hung once $stop was
#    kept. The reader now holds the react only while one of its bodies runs.
#
# Each case runs under a 10 s guard, so a hang fails here instead of wedging
# the suite. Contract: exit 0 + last line PASS.
my @fail;
sub within(Str $what, &code, $want) {
    my $p = start { code() };
    await Promise.anyof($p, Promise.in(10));
    if $p.status ~~ Planned  { @fail.push("$what: still running after 10 s") }
    elsif $p.status ~~ Broken { @fail.push("$what: died: {$p.cause.message}") }
    elsif $p.result !eqv $want { @fail.push("$what: got {$p.result.raku}, want {$want.raku}") }
}

within 'closed later, whenever $ch.closed', {
    my $ch = Channel.new; my @log;
    start { sleep 0.2; $ch.close };
    react { whenever $ch.closed { @log.push('closed'); done } }
    @log
}, ['closed'];
within 'closed later, beside an interval', {
    my $ch = Channel.new; my @log;
    start { sleep 0.2; $ch.close };
    react { whenever Supply.interval(5) { @log.push('tick') }; whenever $ch.closed { @log.push('closed'); done } }
    @log
}, ['tick', 'closed'];
within 'whenever $ch first, then $ch.closed', {
    my $ch = Channel.new; my @log;
    start { sleep 0.2; $ch.send(1); $ch.close };
    react { whenever $ch -> $d { @log.push("got $d") }; whenever $ch.closed { @log.push('closed'); done } }
    @log
}, ['got 1', 'closed'];
within 'two reacts end on one close', {
    my $ch = Channel.new; my @log;
    my $a = start react { whenever $ch.closed { @log.push('A'); done } };
    my $b = start react { whenever $ch.closed { @log.push('B'); done } };
    sleep 0.2; $ch.close; await $a, $b;
    @log.sort.List
}, ('A', 'B');
within 'a reader that drains a closed channel keeps .closed', {
    my $ch = Channel.new;
    start { $ch.send(1); $ch.send(2); $ch.close };
    my @got;
    react { whenever $ch -> $d { @got.push($d) } }
    await $ch.closed;
    (@got.List, $ch.closed.status)
}, ((1, 2), Kept);
within 'a failed channel breaks .closed', {
    my $ch = Channel.new; my $msg = '';
    start { sleep 0.1; $ch.send(1); $ch.fail("boom") };
    react { whenever $ch -> $d { QUIT { default { $msg = .message } } } }
    ($msg, $ch.closed.status)
}, ('boom', Broken);
within 'whenever $promise beside an empty channel', {
    my $stop = Promise.new; my $c = Channel.new; my @log;
    start { sleep 0.3; $stop.keep };
    react { whenever $stop { @log.push('stop'); done }; whenever $c { } }
    @log
}, ['stop'];

# The issue's own shape: a react re-entered in a loop, one value per pass, until
# a promise says stop. Rakudo 2026.09 drops values here (the open issue's bug).
if $*RAKU.compiler.name eq 'Raku++' {
    within 'one value per react, until stopped', {
        my @c = Channel.new xx 2;
        @c[0].send(0) for ^3; @c[1].send(1) for ^3;
        my $stop = Promise.new; my @r;
        start { sleep 0.5; $stop.keep };
        while $stop.status == Planned {
            react { for @c -> $ch { whenever $ch { @r[$_]++; done } }; whenever $stop { done } }
        }
        @r.List
    }, (3, 3);
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
