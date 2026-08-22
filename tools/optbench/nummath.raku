# Floating-point arithmetic in a tight loop — a Mandelbrot escape count, all
# `Num` multiplies, adds and comparisons, printing an integer so every engine's
# output is comparable byte for byte.
#
# This kernel is here to be the one the optimizer does NOT yet help. Pass 3's
# native lanes are int64 only — "Everything else — Nums, strings, Rats,
# bignums, array elements, method calls — fails the lane" — so `-O` buys this
# program roughly nothing today, and the row will say so. `Num` lanes are the
# next lever named in OPTIMIZATION.md's "Limits and what's next"; this is the
# measuring stick for when they land, and until then it keeps the showcase
# table honest about what `-O` does not reach.
my $count = 0;
my $n = 0;
while $n < 100_000 {
    my $cr = -2e0 + ($n % 400) * 0.01e0;
    my $ci = -1e0 + ($n div 400) * 0.004e0;
    my $zr = 0e0;
    my $zi = 0e0;
    my $i = 0;
    while $i < 30 && $zr * $zr + $zi * $zi <= 4e0 {
        my $t = $zr * $zr - $zi * $zi + $cr;
        $zi = 2e0 * $zr * $zi + $ci;
        $zr = $t;
        $i = $i + 1;
    }
    $count = $count + $i;
    $n = $n + 1;
}
say $count;
