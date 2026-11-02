my int $n = 20_000_000;
my num $s = 0e0;
my num $sign = 1e0;
loop (my int $k = 0; $k < $n; $k++) { $s += $sign / (2e0 * $k + 1e0); $sign = -$sign }
say $s * 4e0;
