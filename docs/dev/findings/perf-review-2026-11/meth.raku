class P { has $.x; has $.y; method sum { $!x + $!y } }
my $p = P.new(x => 1, y => 2);
my $s = 0;
for ^3_000_000 { $s += $p.sum }
say $s;
