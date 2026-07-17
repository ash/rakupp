# String comparisons: eq in a hot condition — exercises the string-compare
# dispatch path (inline fast path in --exe; applyArith's chain interpreted).
my $a = "delta";
my $b = "delt" ~ "a";
my $c = 0;
for 1..1_000_000 { $c++ if $a eq $b; $c-- if $a lt $b; }
say $c;
