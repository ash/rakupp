# Regression: a child process's output must appear WHILE it runs — issue #51.
#
# run/shell without `:out` used to capture the child's stdout into a pipe and
# echo the whole thing once the child had exited, and a Proc::Async `.stdout`
# tap fired once, at the end, with everything. Both made a runner that relays a
# long job's progress relay it all after the job. The child now inherits our own
# descriptor (which is also what makes it see a terminal when we do), and taps
# are fed each chunk as it is read.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want }

my $RAKUPP = $*EXECUTABLE.absolute;

# 1. ORDER. Our own output and the child's share one descriptor, so what we
#    wrote first must land first — which means flushing before the child starts.
#    Checked through a nested run so the outer stdout is a pipe, not a terminal:
#    a terminal is line-buffered anyway and would hide the bug.
my $prog = 'print "A"; run $*EXECUTABLE.absolute, "-e", Q[print "B"]; print "C"';
my $p = run $RAKUPP, '-e', $prog, :out;
check($p.out.slurp(:close), 'ABC', 'interleaved-order');

# 2. LIVE. The child prints, sleeps, prints; the relay reads its stdout as
#    lines. The first line has to arrive while the child is still sleeping.
my $async = Proc::Async.new('bash', '-c', 'echo first; sleep 2; echo second');
my @seen;
my @at;
my $t0 = now;
react {
    whenever $async.stdout.lines { @seen.push($_); @at.push(now - $t0) }
    whenever $async.start { done }
}
check(@seen.join(','), 'first,second', 'both-lines');
check(@at[0] < 1, True, "first-line-is-live (arrived at {@at[0].round(0.01)}s)");
check(@at[1] >= 1.5, True, 'second-line-after-the-sleep');

# 3. the adverbs still mean what they meant: :out captures (and then the
#    output is NOT on our stdout), :!out discards
my $cap = run $RAKUPP, '-e', Q[say "captured"], :out;
check($cap.out.slurp(:close), "captured\n", 'out-captures');
my $quiet = run $RAKUPP, '-e', Q[say "noisy"], :!out;
check($quiet.exitcode, 0, 'not-out-runs');

# 4. an un-adverbed run has nothing to hand back — the bytes went to our
#    descriptor, not into a buffer. Rakudo goes further and makes `.out` a bare
#    IO::Pipe type object, so the same line DIES there; answering "" is the
#    gentler half of the same fact, and the only check in this file Rakudo does
#    not also pass.
my $through = run $RAKUPP, '-e', Q[print ""];
check($through.out.slurp(:close), '', 'passthrough-captures-nothing');

# 5. a stderr tap gets its own stream, and an unterminated last line still lands
my $both = Proc::Async.new('bash', '-c', 'echo out1; echo err1 >&2; printf tail');
my (@o, @e);
react {
    whenever $both.stdout.lines { @o.push($_) }
    whenever $both.stderr.lines { @e.push($_) }
    whenever $both.start { done }
}
check(@o.join(','), 'out1,tail', 'stdout-tap-incl-unterminated');
check(@e.join(','), 'err1',      'stderr-tap');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL'; exit 1 } else { say 'PASS' }
