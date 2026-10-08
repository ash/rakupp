# `.close-stdin` right after `.start` lost the race with the process's worker.
#
#   build/rakupp t/race/proc-async-close-stdin.raku               # 1000 spawns
#   build/rakupp t/race/proc-async-close-stdin.raku --spawns=3000 --busy=16
#
# The binary that runs this file is the one under test.
#
# After `.start`, a worker (runProcPromise, src/Builtins.cpp) waits for the
# child and, when it exits, inserts the end marks and the exit status into the
# Proc::Async's hash. `.close-stdin` on the main thread looks up `w` and
# `stdin-wfd` in that same hash. In parallel mode nothing ordered the two, and
# an insert that rehashed the map made the lookup miss: "The process's standard
# input is already closed", "Process must be opened for writing with :w", or a
# SIGSEGV. Both sides now take the hash's stripe.
#
# This is S17-procasync/no-runaway-file-limit.t with load added. The child,
# `cat /dev/null`, exits without reading its stdin, so its worker races the
# `.close-stdin` every time. Busy `start` threads keep the cores loaded so the
# main thread is descheduled between the two calls. On a RISC-V board (8
# cores) the old binary failed 3 of 4 runs of the Roast file under outside
# load. A spawn that throws, or a crash, is the failure.
#
# The last line is PASS or FAIL, and the exit code is 0 or 1, as in t/stress.

sub MAIN(Int :$spawns = 1000, Int :$busy = $*KERNEL.cpu-cores) {
    my $stop = False;
    my @busy = (^$busy).map: { start { my $n = 0; until $stop { $n++ } } };
    my $failed = 0;
    my $first;
    for ^$spawns {
        my $proc = Proc::Async.new('cat', '/dev/null', :w);
        $proc.stdout.tap(-> $ {});
        my $p = $proc.start;
        $proc.close-stdin;
        await $p;
        CATCH { default { $failed++; $first //= .message } }
    }
    $stop = True;
    await @busy;
    say "spawns that threw: $failed of $spawns" ~ ($first ?? " (first: $first)" !! '');
    say $failed ?? 'FAIL' !! 'PASS';
    exit $failed ?? 1 !! 0;
}
