# A Proc::Async promise moved only when something awaited it.
#
# `.start` spawned the child, but draining its pipes, feeding the taps and
# reaping it all happened inside an `await`. So a program that did anything
# else first saw `.status` stay Planned and its taps stay silent, and
# `Promise.anyof($proc-promise, Promise.in($t))` — a timeout — broke the
# process promise when the timer won, leaving `.kill` nothing to report.
# A worker now drives every started process, as one already did inside a
# `supply` block. nige123/cli.321.do waited in a helper `start` block to get
# round it. Also: a stream asked for before the start keeps what the child
# wrote until its first tap arrives (roast S17-procasync/basic.t), with its
# `done` if the stream has already ended.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# settles with nobody awaiting
{
    my $p = Proc::Async.new('sh', '-c', 'echo hi');
    my $out = '';
    $p.stdout.tap({ $out ~= $_ });
    my $prom = $p.start;
    for ^100 { last if $prom.status ~~ Kept; sleep 0.05 }
    check($prom.status, Kept, 'the promise is Kept without an await');
    check($out, "hi\n", 'and the tap ran');
}
# a timeout in anyof leaves the process alone
{
    my $p = Proc::Async.new('sleep', '30');
    my $prom = $p.start;
    await Promise.anyof($prom, Promise.in(0.3));
    check($prom.status, Planned, 'anyof timing out does not break the process');
    $p.kill(SIGTERM);
    my $res = await $prom;
    check($res.signal, 15, 'the kill is reported as the signal');
}
# anyof the other way round: the process wins
{
    my $p = Proc::Async.new('sh', '-c', 'exit 3');
    my $prom = $p.start;
    await Promise.anyof($prom, Promise.in(10));
    check($prom.status, Kept, 'anyof returns when the process ends');
    check($prom.result.exitcode, 3, 'with its exit code');
}
# a stream tapped after the start loses nothing, and still ends
{
    my $p = Proc::Async.new('sh', '-c', 'printf "a\nb\nc"');
    my $lines = $p.stdout.lines;
    my $prom = $p.start;
    sleep 0.3;
    my @got; my $done = 0;
    $lines.tap({ @got.push($_) }, done => { $done++ });
    await $prom;
    check(@got.List, <a b c>, 'a late tap gets every line');
    check($done, 1, 'and the done of a stream that already ended');
}

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
