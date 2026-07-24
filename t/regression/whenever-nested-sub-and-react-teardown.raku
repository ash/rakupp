# Regression: two react/supply fixes exercised by Cro's dependencies —
#   1. `whenever` inside a `sub` that is lexically nested in a supply/react block
#      parses (Log::Timeline::Output::Socket, IO::Socket::Async::SSL). A `whenever`
#      in a TOP-LEVEL sub must still be a parse error.
#   2. A react block's OS-signal taps are torn down when the block ends, so a
#      second signal after `done` does NOT re-fire the handler (a double Ctrl-C
#      calling `$server.stop` twice used to die "service was not started").
# Contract: exit 0 + last line PASS.
use nqp;
my @fail;

# 1a. whenever in a sub nested in a supply — parses and runs
my $ok = 0;
my $s = supply {
    sub helper() { whenever Supply.interval(0.1).head(1) { emit 'x' } }
    helper();
}
$s.tap({ $ok++ });
sleep 0.4;
@fail.push('nested-sub-whenever') unless $ok >= 1;

# 1b. whenever in a TOP-LEVEL sub is still rejected (parse error, caught via EVAL)
my $rejected = False;
{
    use MONKEY-SEE-NO-EVAL;
    try { EVAL 'sub f() { whenever Supply.interval(1) {} }' }
    $rejected = True if $! && ~$! ~~ /whenever/;
}
@fail.push('toplevel-sub-whenever-still-errors') unless $rejected;

# 2. react signal tap teardown: a second SIGINT after `done` must NOT re-run the
#    handler. Send two signals; the handler must fire exactly once.
my $fires = 0;
start {
    sleep 0.4; run 'kill', '-INT', ~$*PID;
    sleep 0.3; run 'kill', '-INT', ~$*PID;   # second one must be a no-op
}
react {
    whenever signal(SIGINT) {
        $fires++;
        done;
    }
}
sleep 0.4;   # give the stray second signal time to (not) fire
@fail.push("signal-fires-once (got=$fires)") unless $fires == 1;

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
