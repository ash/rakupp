# Float-dense arithmetic: the shape the Mandelbrot kernel measures.
my $zr = 0e0; my $zi = 0e0; my $n = 0;
loop (my $i = 0; $i < 100000; $i++) {
    $zr = $zr * 0.5e0 + 1e0;
    $zi = $zi + $zr / 3e0;
    $n = $n + 1;
}
say $zr.fmt('%.9f') ~ " " ~ $zi.fmt('%.6f') ~ " $n";
