# Regression: the Grand Review, batch C3 — process status, Proc::Async's
# stdin, the socket constructor, big Num → Int (docs/dev/findings/REVIEW-GRAND.md).
# Every case is what Rakudo answers; each used to be a silent wrong answer here.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}
sub dies(&code) { my $lived = False; try { code(); $lived = True }; !$lived }

# 1. A child killed by a signal reports exitcode 0 and the signal (it was −1 and 0).
{
    my $p = run 'sh', '-c', 'kill -9 $$';
    check($p.exitcode, 0,     'a SIGKILLed child has exitcode 0');
    check($p.signal,   9,     '…and signal 9');
    check(?$p,         False, '…and is not a success');
    my $q = run 'sh', '-c', 'exit 3';
    check($q.exitcode, 3,     'a plain exit keeps its code');
    check($q.signal,   0,     '…with signal 0');
    check($q.pid > 0,  True,  'Proc.pid is the real pid');
    check(dies({ run 'sh', '-c', 'kill -15 $$' }), True, 'a signalled Proc that is sunk throws');
}

# 2. Proc::Async :w feeds the child (every write used to answer True and reach nobody).
{
    my $c = Proc::Async.new(:w, 'cat');
    my @got;
    $c.stdout.tap({ @got.push($_) });
    my $cp = $c.start;
    my $w = await $c.print("hello\n");
    check($w.^name, 'Int',        '.print is kept with the byte count');
    await $c.say("x");
    $c.close-stdin;
    my $r = await $cp;
    check(@got.join, "hello\nx\n", 'the child read what we wrote');
    check($r.exitcode, 0,         '…and exited cleanly at EOF');
    check(dies({ Proc::Async.new('cat').print('x') }),   True, 'writing without :w dies');
    check(dies({ Proc::Async.new('cat').close-stdin }),  True, 'close-stdin without :w dies');
}

# 3. .kill delivers a signal the awaited Proc reports.
{
    my $k = Proc::Async.new('sleep', '30');
    my $kp = $k.start;
    sleep 0.3;
    $k.kill;
    my $kr = await $kp;
    check($kr.signal,   1, '.kill sends SIGHUP by default, and .signal says so');
    check($kr.exitcode, 0, '…with exitcode 0');
}

# 4. IO::Socket::INET.new throws on failure (it answered Nil and the program ran on).
check(dies({ IO::Socket::INET.new(:host<127.0.0.1>, :port(1)) }),               True, 'a refused connection dies at .new');
check(dies({ IO::Socket::INET.new(:host<127.0.0.1>, :port(1), :family(99)) }),  True, 'an unknown :family is refused');

# 5. A Num past 2**63 converts to Int exactly.
check(1e19.Int,           10000000000000000000,     '1e19.Int');
check(Int(1e19),          10000000000000000000,     'Int(1e19)');
check((2**70).Num.Int,    1180591620717411303424,   '(2**70).Num.Int');
check(1e300.Int.chars,    301,                      '1e300.Int has 301 digits');
check((-1e19).Int,       -10000000000000000000,     '(-1e19).Int');

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
