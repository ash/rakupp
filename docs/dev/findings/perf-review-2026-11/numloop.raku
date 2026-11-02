# K2: float accumulate (Leibniz pi)
my $n = 20_000_000;
my $s = 0e0;
my $sign = 1e0;
for 0..^$n -> $k { $s += $sign / (2e0 * $k + 1e0); $sign = -$sign }
say $s * 4e0;
