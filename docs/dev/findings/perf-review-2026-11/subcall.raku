sub sum($a, $b) { $a + $b }
my $s = 0;
for ^3_000_000 { $s += sum($_, 2) }
say $s;
