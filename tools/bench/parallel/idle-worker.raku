# Does an idle worker slow the main thread? — one long-lived worker that does nothing.
#
# A server keeps a worker live for its whole run: a signal tap, a listener, a
# Supply.interval. This times one loop on the main thread with no worker
# (`alone`) and beside one worker parked on a Promise (`idle`). The worker
# never runs Raku while the loop does, so any difference is a cost the main
# thread pays for the worker being live.
#
#     rakupp tools/bench/parallel/idle-worker.raku kernel alone
#     rakupp tools/bench/parallel/idle-worker.raku kernel idle
#     rakupp tools/bench/parallel/idle-worker.raku interp idle
#
# Two shapes. `kernel` is a loop Raku++ compiles on its own (it reads `$M`
# from outside the routine, so the routine is not compiled whole). `interp`
# keeps its work in `my int` locals, which the loop compiler declines, so it
# measures the interpreted path.

my $shape = @*ARGS[0] // 'kernel';     # kernel | interp
my $mode  = @*ARGS[1] // 'idle';       # alone | idle
my $M     = (@*ARGS[2] // ($shape eq 'kernel' ?? 3_000_000 !! 300_000)).Int;

sub kernel-shape($seed) {
    my $s = 0;
    my $x = $seed;
    for ^$M {
        $x = ($x * 1103515245 + 12345) % 2147483647;
        $s = $s + ($x % 7);
    }
    $s
}

sub interp-shape($seed) {
    my int $s = 0;
    my int $x = $seed;
    for ^$M {
        $x = ($x * 1103515245 + 12345) % 2147483647;
        $s = $s + ($x % 7);
    }
    $s
}

my $release = Promise.new;
my $worker  = $mode eq 'idle' ?? start { await $release; 0 } !! Promise.kept(0);
sleep 0.05 if $mode eq 'idle';        # let the worker park first

my $t0  = now;
my $sum = $shape eq 'kernel' ?? kernel-shape(1) !! interp-shape(1);
my $dt  = now - $t0;

$release.keep;
await $worker;
say sprintf 'idle-worker %-6s %-5s M=%d  %.3fs  sum=%d', $shape, $mode, $M, $dt, $sum;
