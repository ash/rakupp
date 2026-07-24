# Regression: signal(SIGINT,…) as a Supply — the standard Cro Ctrl-C shutdown
# `react { whenever signal(SIGINT) { …; done } }`. Contract: exit 0 + last
# line PASS. Self-sends the signal from a worker so the test is deterministic.
my @fail;

# 1. Signal enum members resolve with their OS numbers
@fail.push('SIGINT-value')  unless SIGINT.value  == 2;
@fail.push('SIGTERM-value') unless SIGTERM.value == 15;
@fail.push('SIGINT-name')   unless SIGINT.Str eq 'SIGINT';
@fail.push('SIGINT-isa')    unless SIGINT ~~ Signal;

# 2. react + whenever signal(): a self-sent SIGINT runs the block and `done`
#    exits the react cleanly
my $got = -1;
start { sleep 0.5; run 'kill', '-INT', ~$*PID }
react {
    whenever signal(SIGINT) {
        $got = $_.value;
        done;
    }
}
@fail.push("react-signal (got=$got)") unless $got == 2;

# 3. multi-signal signal(SIGINT, SIGTERM) — a SIGTERM also fires
my $got2 = -1;
start { sleep 0.5; run 'kill', '-TERM', ~$*PID }
react {
    whenever signal(SIGINT, SIGTERM) {
        $got2 = $_.value;
        done;
    }
}
@fail.push("multi-signal (got=$got2)") unless $got2 == 15;

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
