# K4: dense matmul, flat row-major arrays
my $n = 120;
my @a = (^($n*$n)).map({ ($_ % 7) * 0.25e0 });
my @b = (^($n*$n)).map({ ($_ % 5) * 0.5e0 });
my @c = 0e0 xx ($n*$n);
for ^$n -> $i {
    for ^$n -> $j {
        my $s = 0e0;
        for ^$n -> $k { $s += @a[$i*$n + $k] * @b[$k*$n + $j] }
        @c[$i*$n + $j] = $s;
    }
}
say [+] @c;
