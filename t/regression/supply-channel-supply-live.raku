# Regression: `$supplier.Supply.Channel.Supply` on a LIVE supplier forwards
# emits instead of snapshotting an empty list. IO::Socket::Async::SSL exposes its
# decrypted read stream this way (`$!bytes-received.Supply.Channel.Supply`), so
# without live forwarding an HTTPS response body is lost.
#   - Supply.Channel on a supplier-backed Supply carries the supplier (live)
#   - Channel.Supply on such a channel is a live Supply that taps the channel
# The values come through the channel on ANOTHER thread, after the emit has
# returned (Rakudo's channel supply runs on the scheduler; Cro's WebSocket
# handler depends on that hop), so each check waits — boundedly — for them.
# Contract: exit 0 + last line PASS.
my @fail;
# wait up to 2 s for @a to hold $n values
sub settle(@a, $n) { for ^200 { last if @a >= $n; sleep 0.01 } }

# the full chain forwards emits
{
    my $s = Supplier.new;
    my @got;
    $s.Supply.Channel.Supply.tap(-> $v { @got.push($v) });
    $s.emit('A'); $s.emit('B'); $s.emit('C');
    settle(@got, 3);
    @fail.push("chain (@got.join(','))") unless @got eqv ['A', 'B', 'C'];
}

# Channel.Supply of a live channel is a Supply (not a List) and taps
{
    my $s = Supplier.new;
    my $sup = $s.Supply.Channel.Supply;
    @fail.push('chain-type') unless $sup ~~ Supply;
}

# emits after the tap still arrive (live, not a one-time snapshot)
{
    my $s = Supplier.new;
    my @got;
    $s.Supply.Channel.Supply.tap(-> $v { @got.push($v) });
    $s.emit('x');
    settle(@got, 1);
    @fail.push('live-after-tap') unless @got eqv ['x'];
    $s.emit('y');
    settle(@got, 2);
    @fail.push('still-live') unless @got eqv ['x', 'y'];
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
