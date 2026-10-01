# Regression: closing an IO::Socket::Async listen tap dropped the connections
# the kernel had already completed but the accept worker had not yet taken off
# the backlog. The client's `connect` succeeded and its write and close went
# through, and the tap's callback never ran for it. Roast's
# S32-io/IO-Socket-Async.t ("both receivers finished without exception")
# hung on it in about one full sweep in ten when another sweep shared the
# machine. The closer now takes what is waiting before it shuts the listener
# down, and the worker delivers those, as Rakudo's eagerly-accepting event loop
# does.
#
# Made deterministic here: the first connection's callback keeps the accept
# worker busy, so the second connection is still in the backlog when the tap
# closes.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $lis = IO::Socket::Async.listen('127.0.0.1', 0);
my @got;
my $lock = Lock.new;
my $both = Promise.new;
my $tap = $lis.tap: -> $conn {
    my $text = $conn.Supply.list.join;
    my $n = $lock.protect: { @got.push($text); @got.elems };
    sleep 1 if $n == 1;            # the worker is in here, not in accept()
    $both.keep if $n == 2;
}
my $port = $tap.socket-port.result;
for <one two> -> $word {
    my $c = await IO::Socket::Async.connect('127.0.0.1', $port);
    await $c.write($word.encode);
    $c.close;
}
sleep 0.3;                         # the first callback is asleep; the second connection waits
$tap.close;
await Promise.anyof($both, Promise.in(5));
ck $both.status, Kept, 'a connection made before the tap closed still reaches the callback';
ck @got.sort.List, <one two>, '…with what its client wrote';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
