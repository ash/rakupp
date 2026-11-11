# Regression: issue #16 (2026-11-10).
#
#   A server sending a line per `Supply.interval($delay)` tick to each client
#   it accepted sent five lines at once and then nothing more: the ticks of an
#   interval started inside a connection's handler were not spaced by the
#   delay. Here a client reads the lines and the ticks' times are recorded.
#
# Expected answers are Rakudo 2026.09's (2026-11-10).

my $fails = 0;
sub check($got, $want, $what) {
    return if $got eqv $want;
    $fails++;
    say "not ok - $what\n  got:  {$got.raku}\n  want: {$want.raku}";
}

my @ticks;
my $start = now;
my $served = Promise.new;
my $served-v = $served.vow;
my $listener = IO::Socket::Async.listen('127.0.0.1', 0).tap: -> $conn {
    start react {
        whenever Supply.interval(0.2) {
            @ticks.push: now - $start;
            await $conn.print: "L$_\r\n";
            if $_ == 3 { $conn.close; $served-v.keep(True); done }
        }
        whenever Promise.in(10) { $served-v.keep(False); done }
    }
}
my $port = await $listener.socket-port;
my $got = '';
my $client = await IO::Socket::Async.connect('127.0.0.1', $port);
react {
    # The server closes the connection after tick 3, which ends the Supply.
    whenever $client.Supply { $got ~= $_; LAST done }
    whenever Promise.in(10) { done }
}
$listener.close;

await Promise.anyof($served, Promise.in(5));
check $served.status ~~ Kept && $served.result, True, 'the handler saw tick 3 and ended';
check $got.lines.head(4).List, <L0 L1 L2 L3>, 'the client got one line per tick, in order';
check @ticks.elems, 4, 'four ticks, no burst';
# Ticks 0 and 3 are three intervals apart: 0.6 s; the bug sent them together.
# A loaded machine can only stretch the gap, so only a floor is checked.
check (@ticks[3] - @ticks[0]) >= 0.45, True, 'the ticks are spaced by the interval';

if $fails { say "$fails failure(s)"; exit 1 }
say 'PASS';
