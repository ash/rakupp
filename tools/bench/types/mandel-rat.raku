# The Mandelbrot kernel written with decimal literals (`2.0`, `4.0`), as most
# Raku code is. Those are exact Rats, so every step is rational arithmetic.
# It is here to measure the Rat work of TYPES-PLAN phase N6. Its checksum differs
# from mandel-plain's on purpose: exact arithmetic escapes at different points.
my $w = (@*ARGS[0] // 150).Int;
my $h = (@*ARGS[1] // 130).Int;
my $sum = 0;
my $y = 0;
while $y < $h {
    my $ci = $y * 4.0 / $h - 2.0;
    my $x = 0;
    while $x < $w {
        my $cr = $x * 4.0 / $w - 2.0;
        my $zr = 0.0;
        my $zi = 0.0;
        my $k = 0;
        while $k < 24 {
            my $t = $zr * $zr - $zi * $zi + $cr;
            $zi = 2.0 * $zr * $zi + $ci;
            $zr = $t;
            last if $zr * $zr + $zi * $zi > 10.0;
            $k = $k + 1;
        }
        $sum = $sum + $k;
        $x = $x + 1;
    }
    $y = $y + 1;
}
say "sum=$sum";
