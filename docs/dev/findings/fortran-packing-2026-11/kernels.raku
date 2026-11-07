# kernels.raku KERNEL [PASSES] : the kernels of kernels.f90 in untyped Raku, as
# Raku is usually written: no native types, arrays built with .map, `for`
# loops. Its arrays of Nums pack themselves
# (docs/dev/experiments/SELFPACK-EXPERIMENT.md); RAKUPP_NO_SELFPACK=1 keeps them
# boxed. The arrays are built once and the kernel runs PASSES times (5); prints
# the checksum, then the milliseconds of the fastest pass.
my $which = @*ARGS[0] // '';
my $passes = (@*ARGS[1] // 5).Int;
my $s = 0e0;
my $t0;
my $best = Inf;
if $which eq 'matmul' {
    my $m = 200;
    my @x = (^($m * $m)).map({ ($_ % 7) * 0.25e0 });
    my @y = (^($m * $m)).map({ ($_ % 5) * 0.5e0 });
    my @z = 0e0 xx ($m * $m);
    for ^$passes {
        $t0 = now;
        for ^$m -> $i {
            for ^$m -> $j {
                my $t = 0e0;
                for ^$m -> $k { $t += @x[$i * $m + $k] * @y[$k * $m + $j] }
                @z[$i * $m + $j] = $t;
            }
        }
        $best = min($best, (now - $t0) * 1000);
    }
    for ^($m * $m) -> $i { $s += @z[$i] }
}
else {
    my $n = 10_000_000;
    my @a = (^$n).map({ $_ * 1e-7 });
    my @b = (^$n).map({ -($_ * 1e-7) });
    my @c = 0e0 xx $n;
    die "kernel?" unless $which eq 'sum' | 'dot' | 'axpy';
    for ^$passes {
        $t0 = now;
        if $which eq 'sum' { $s = 0e0; for ^$n -> $i { $s += @a[$i] } }
        elsif $which eq 'dot' { $s = 0e0; for ^$n -> $i { $s += @a[$i] * @b[$i] } }
        else { for ^$n -> $i { @c[$i] = 2e0 * @a[$i] + @b[$i] } }
        $best = min($best, (now - $t0) * 1000);
    }
    if $which eq 'axpy' { $s = 0e0; for ^$n -> $i { $s += @c[$i] } }
}
printf "%.16e\n", $s;
say $best;
