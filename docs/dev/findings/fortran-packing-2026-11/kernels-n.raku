# kernels-n.raku KERNEL [PASSES] : the kernels of kernels.f90 written the
# Fortran way in Raku: `num` arrays, `num` and `int` scalars, C-style loops.
# The arrays are built once and the kernel runs PASSES times (5); prints the
# checksum, then the milliseconds of the fastest pass.
my $which = @*ARGS[0] // '';
my int $passes = (@*ARGS[1] // 5).Int;
my num $s = 0e0;
my $t0;
my $best = Inf;
if $which eq 'matmul' {
    my int $m = 200;
    my num @x = (^($m * $m)).map({ ($_ % 7) * 0.25e0 });
    my num @y = (^($m * $m)).map({ ($_ % 5) * 0.5e0 });
    my num @z = 0e0 xx ($m * $m);
    loop (my int $r = 0; $r < $passes; $r++) {
        $t0 = now;
        loop (my int $i = 0; $i < $m; $i++) {
            loop (my int $j = 0; $j < $m; $j++) {
                my num $t = 0e0;
                loop (my int $k = 0; $k < $m; $k++) { $t += @x[$i * $m + $k] * @y[$k * $m + $j] }
                @z[$i * $m + $j] = $t;
            }
        }
        $best = min($best, (now - $t0) * 1000);
    }
    loop (my int $q = 0; $q < $m * $m; $q++) { $s += @z[$q] }
}
else {
    my int $n = 10_000_000;
    my num @a = (^$n).map({ $_ * 1e-7 });
    my num @b = (^$n).map({ -($_ * 1e-7) });
    my num @c = 0e0 xx $n;
    die "kernel?" unless $which eq 'sum' | 'dot' | 'axpy';
    loop (my int $r = 0; $r < $passes; $r++) {
        $t0 = now;
        if $which eq 'sum' { $s = 0e0; loop (my int $i = 0; $i < $n; $i++) { $s += @a[$i] } }
        elsif $which eq 'dot' { $s = 0e0; loop (my int $i = 0; $i < $n; $i++) { $s += @a[$i] * @b[$i] } }
        else { loop (my int $i = 0; $i < $n; $i++) { @c[$i] = 2e0 * @a[$i] + @b[$i] } }
        $best = min($best, (now - $t0) * 1000);
    }
    if $which eq 'axpy' { $s = 0e0; loop (my int $q = 0; $q < $n; $q++) { $s += @c[$q] } }
}
printf "%.16e\n", $s;
say $best;
