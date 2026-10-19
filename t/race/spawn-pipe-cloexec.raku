# A captured child's EOF waited for an unrelated sibling to exit.
#
#   build/rakupp t/race/spawn-pipe-cloexec.raku              # 150 spawns
#   build/rakupp t/race/spawn-pipe-cloexec.raku --spawns=500
#
# The binary that runs this file is the one under test.
#
# spawnChildStart (src/Builtins.cpp) made the capture pipe, forked, and only
# then marked the parent's read end close-on-exec; the write end never was. A
# second thread forking in that window carried the write end across its own
# execvp, so the first child's EOF did not arrive until the SIBLING exited:
# the Roast harness saw finished files with their whole TAP captured end as
# [TIME], and run-roast.raku took a Lock around its spawns to hide it. Every
# pipe end is now close-on-exec before the fork.
#
# Three threads spawn `sleep 2` back to back while the main thread times a
# captured `echo`. Before the fix one to five of 150 echoes took the length of
# a sleep (2 s) on every run; after it, none takes more than a fraction of a
# second. A capture slower than 1 s is the failure.
#
# `run(…, :in)` goes through spawnWithInput, which made its pipes with plain
# pipe() and set the flag only after the fork; the second loop times a `cat`
# fed through `.in` the same way. That window was only pipe() to fork, a few
# microseconds, and neither 150 nor 600 spawns caught it before the fix; the
# loop fails if the `.in` write end loses the flag altogether, as the echo
# capture's write end once had.
#
# The last line is PASS or FAIL, and the exit code is 0 or 1, as in t/stress.

sub MAIN(Int :$spawns = 150) {
    my $stop = False;
    my @sleepers = (^3).map: { start { until $stop { run 'sleep', '2' } } };
    my ($slow, $max) = 0, 0;
    sub time-it(&spawn) {
        my $t = now;
        spawn;
        my $d = now - $t;
        $max = $d if $d > $max;
        $slow++ if $d > 1;
    }
    for ^$spawns { time-it { run('echo', 'x', :out).out.slurp(:close) } }
    for ^$spawns {
        time-it {
            my $p = run 'cat', :in, :out;
            $p.in.print: "x\n";
            $p.in.close;
            $p.out.slurp(:close);
        }
    }
    $stop = True;
    await @sleepers;
    my $all = 2 * $spawns;
    say "captures slower than 1 s: $slow of $all (slowest {$max.round(0.01)} s)";
    say $slow ?? 'FAIL' !! 'PASS';
    exit $slow ?? 1 !! 0;
}
