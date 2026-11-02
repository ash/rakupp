# K3: dot product over two arrays, repeated
my $n = 1000;
my @a = (^$n).map({ $_ * 0.5e0 });
my @b = (^$n).map({ 1e0 / ($_ + 1) });
my $t = 0e0;
for ^5000 {
    my $s = 0e0;
    for ^$n -> $i { $s += @a[$i] * @b[$i] }
    $t += $s;
}
say $t;
