sub sum(num $a, num $b --> num) { $a + $b }
my num $s = 0e0;
for ^3_000_000 { $s += sum(1e0, 2e0) }
say $s;
