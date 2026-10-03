# Regression: when an async socket's reader sees EOF it closes the descriptor —
# and the socket must forget it first. The number is free for the next
# accept() at once; a write still holding it went into that NEW connection.
# A Cro chat server kept broadcasting to a client that had gone, and its
# messages reached whoever connected next.
# Contract: exit 0 + last line PASS.
my @fail;
my $port = 21500 + $*PID % 400;
my @conns;
my $gone = Promise.new; my $gv = $gone.vow;
my $listener = IO::Socket::Async.listen('127.0.0.1', $port).tap(-> $conn {
    my $n = @conns.elems;
    @conns.push($conn);
    $conn.Supply.tap(-> $ {}, done => { $gv.keep(True) if $n == 0 });
});
sleep 0.2;

# the first client comes and goes
my $c1 = await IO::Socket::Async.connect('127.0.0.1', $port);
sleep 0.1;
$c1.close;
await Promise.anyof($gone, Promise.in(5));
@fail.push("no EOF seen") unless $gone;

# the second gets a fresh connection — maybe the very same descriptor number
my $c2 = await IO::Socket::Async.connect('127.0.0.1', $port);
my $got = '';
$c2.Supply.tap(-> $d { $got ~= $d });
sleep 0.2;
@fail.push("second not accepted") unless @conns == 2;

# a write to the departed connection must not reach the new one
my $old = @conns[0].print("LEAK\n");
await Promise.anyof($old, Promise.in(2));
@conns[1].print("OK\n");
sleep 0.3;
@fail.push("leaked: {$got.raku}") if $got.contains('LEAK');
@fail.push("lost: {$got.raku}") unless $got.contains('OK');
@fail.push("write to closed socket kept") if $old.status ~~ Kept;

$c2.close;
$listener.close;
if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
