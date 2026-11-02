my int $n = 1000;
my num @a = (^$n).map({ $_ * 0.5e0 });
my num @b = (^$n).map({ 1e0 / ($_ + 1) });
my num $t = 0e0;
loop (my int $r = 0; $r < 5000; $r++) {
    my num $s = 0e0;
    loop (my int $i = 0; $i < $n; $i++) { $s += @a[$i] * @b[$i] }
    $t += $s;
}
say $t;
