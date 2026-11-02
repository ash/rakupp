my int $n = 120;
my num @a = (^($n*$n)).map({ ($_ % 7) * 0.25e0 });
my num @b = (^($n*$n)).map({ ($_ % 5) * 0.5e0 });
my num @c = 0e0 xx ($n*$n);
loop (my int $i = 0; $i < $n; $i++) {
    loop (my int $j = 0; $j < $n; $j++) {
        my num $s = 0e0;
        loop (my int $k = 0; $k < $n; $k++) { $s += @a[$i*$n + $k] * @b[$k*$n + $j] }
        @c[$i*$n + $j] = $s;
    }
}
say [+] @c;
