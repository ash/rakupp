#!/usr/bin/env raku
# Lightweight interpreter performance guard — run before/after a change to catch
# per-iteration regressions in the eval/exec hot path. Unlike tools/run-bench.raku
# (which also times --exe and Rakudo across the full kernel set), this times only
# the interpreter on a few tight-loop kernels, best-of-3, in a couple of seconds.
#
# The kernels deliberately stress the paths a batch is most likely to slow down:
#   fib     — recursive sub calls + int arithmetic (dispatch + applyArith)
#   asg     — a plain scalar `=` assignment loop
#   loopsum — a `+=` compound assignment over a Range with the `$_` topic
#   hash    — hash-index post-increment in a loop
# (loopsum/hash were added after a ~8-22% interp regression on exactly these
# shapes slipped past the old fib+asg-only guard over the 0.7.1->0.9.0 cycle.)
#
# Usage — A/B two binaries:
#     RAKUPP=/path/to/old rakupp tools/perf-guard.raku
#     RAKUPP=/path/to/new rakupp tools/perf-guard.raku
# Default RAKUPP is ./build/rakupp. It also runs under Rakudo (`raku`), since it
# only spawns the target binary as a subprocess and times it.

my $repo   = $*PROGRAM.absolute.IO.parent.parent;
my $RAKUPP = %*ENV<RAKUPP> // $repo.add('build/rakupp').Str;
my $RUNS   = 4;   # 1 warm-up (discarded) + 3 measured

my %kernels =
    fib     => 'sub fib($n) { $n < 2 ?? $n !! fib($n-1) + fib($n-2) }; say fib(29);',
    asg     => 'my $x = 0; for ^2_000_000 { $x = $x + 1 }; say $x;',
    loopsum => 'my $t = 0; for 1 .. 1_000_000 { $t += $_ }; say $t;',
    hash    => 'my %c; for 1 .. 100_000 { %c{$_ % 1_000}++ }; say %c.elems;';

sub measure(Str $code --> Numeric) {
    my $tmp = $*TMPDIR.add("perf-guard-{$*PID}-{1e6.rand.Int}.raku");
    $tmp.spurt($code);
    my @ms;
    for ^$RUNS {
        my $t0 = now;
        my $p  = run($RAKUPP, $tmp.Str, :out, :err);
        $p.out.slurp(:close); $p.err.slurp(:close);
        @ms.push: ((now - $t0) * 1000).round(0.1);
    }
    $tmp.unlink;
    @ms.skip(1).min;   # best of the measured runs
}

say "perf-guard: $RAKUPP";
say "kernel      best (ms)";
say "-" x 24;
for <fib asg loopsum hash> -> $k {
    printf "%-10s %8.1f\n", $k, measure(%kernels{$k});
}
