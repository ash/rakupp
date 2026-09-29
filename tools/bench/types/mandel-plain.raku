# The Mandelbrot kernel of docs/dev/plans/NATIVE-MATH-PLAN.md: `Num` literals
# throughout (`2e0`, not `2.0`, which would be a Rat), plain `my $` variables.
# The pair of mandel-native.raku: same arithmetic, same checksum.
my $w = (@*ARGS[0] // 300).Int;
my $h = (@*ARGS[1] // 260).Int;
my $sum = 0;
my $y = 0;
while $y < $h {
    my $ci = $y * 4e0 / $h - 2e0;
    my $x = 0;
    while $x < $w {
        my $cr = $x * 4e0 / $w - 2e0;
        my $zr = 0e0;
        my $zi = 0e0;
        my $k = 0;
        while $k < 112 {
            my $t = $zr * $zr - $zi * $zi + $cr;
            $zi = 2e0 * $zr * $zi + $ci;
            $zr = $t;
            last if $zr * $zr + $zi * $zi > 1e1;
            $k = $k + 1;
        }
        $sum = $sum + $k;
        $x = $x + 1;
    }
    $y = $y + 1;
}
say "sum=$sum";
