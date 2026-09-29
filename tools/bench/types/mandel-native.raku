# The Mandelbrot kernel of docs/dev/plans/NATIVE-MATH-PLAN.md, over declared
# `my int` / `my num` variables.
# The pair of mandel-plain.raku: same arithmetic, same checksum.
my int $w = (@*ARGS[0] // 300).Int;
my int $h = (@*ARGS[1] // 260).Int;
my int $sum = 0;
my int $y = 0;
while $y < $h {
    my num $ci = $y * 4e0 / $h - 2e0;
    my int $x = 0;
    while $x < $w {
        my num $cr = $x * 4e0 / $w - 2e0;
        my num $zr = 0e0;
        my num $zi = 0e0;
        my int $k = 0;
        while $k < 112 {
            my num $t = $zr * $zr - $zi * $zi + $cr;
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
