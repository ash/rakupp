my int $n = 20_000_000;
my int $s = 0;
my int $i = 0;
while $i < $n { $s = ($s + $i * $i) % 1000003; $i++ }
say $s;
