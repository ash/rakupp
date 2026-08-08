# The same fan-out, but the workers all have to end up in one counter — and the
# three ways of arranging that scale very differently.
#
#   contended — every iteration does `$a⚛++` on one shared `atomicint`. Correct,
#               and the reason it does not scale: N threads serialise on one
#               cache line and one stripe mutex (Interpreter::atomicStripe), so
#               the loop is contention-bound, not CPU-bound.
#   sharded   — each worker counts in a thread-local native `int` and folds its
#               total into the shared counter with a single atomic-fetch-add.
#               Same answer, one atomic op per worker instead of M.
#   counters  — N separate counters, one per worker (`@count[$i]++`, a plain
#               non-atomic `++`), summed after the join. No atomics at all: no
#               two workers ever write the same variable, so there is nothing to
#               synchronise. This is the shape to reach for first.
#
#     ./build/rakupp tools/bench/parallel/atomic-counter.raku 4 300000 contended serial
#     RAKUPP_PARALLEL=1 ./build/rakupp tools/bench/parallel/atomic-counter.raku 4 300000 counters parallel
#
# Every run prints PASS/FAIL against the expected N*M, in both modes: a speed-up
# that loses increments is not a speed-up.
#
# See docs/guide/PARALLEL-SPEEDUP.md for the measured table.

my $N        = (@*ARGS[0] // 4).Int;             # units of work / worker threads
my $M        = (@*ARGS[1] // 300_000).Int;       # increments per unit
my $strategy = @*ARGS[2] // 'counters';          # contended | sharded | counters
my $mode     = @*ARGS[3] // 'parallel';          # serial | parallel

my atomicint $total = 0;                         # used by contended + sharded
my @count = 0 xx $N;                             # used by counters: one slot per worker

# All three strategies do the same arithmetic per iteration. They differ only in
# where the increment lands. `$i` is both the LCG seed and the worker's index.
my &unit = do given $strategy {
    when 'contended' {
        sub ($i) {
            my int $x = $i;
            for ^$M {
                $x = ($x * 1103515245 + 12345) % 2147483647;
                $total⚛++;                        # M writes to the shared counter
            }
        }
    }
    when 'sharded' {
        sub ($i) {
            my int $x = $i;
            my int $local = 0;
            for ^$M {
                $x = ($x * 1103515245 + 12345) % 2147483647;
                $local = $local + 1;              # thread-local native, uncontended
            }
            atomic-fetch-add($total, $local);     # one write to the shared counter
        }
    }
    when 'counters' {
        sub ($i) {
            my int $x = $i;
            for ^$M {
                $x = ($x * 1103515245 + 12345) % 2147483647;
                @count[$i]++;                     # this worker's own slot — no atomics
            }
        }
    }
    default { die "unknown strategy '$strategy' (contended | sharded | counters)" }
};

my $t0 = now;
if $mode eq 'parallel' {
    await (^$N).map({ start unit($_) });
}
else {
    unit($_) for ^$N;
}
my $dt = now - $t0;

# The counters strategy adds its N per-worker tallies up here, after the join —
# single-threaded, so a plain sum is all it takes.
$total = @count.sum if $strategy eq 'counters';

my $want = $N * $M;
say sprintf '%-9s %-8s N=%d M=%d  %.3fs  total=%d  %s',
    $strategy, $mode, $N, $M, $dt, $total,
    ($total == $want ?? 'PASS' !! "FAIL (want $want)");
exit 1 unless $total == $want;
