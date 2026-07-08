# Integer power and modulo in a hot loop — exercises the inline `**`
# (power-by-squaring, overflow → bignum) and `%` fast paths.
my $acc = 0;
for 1 .. 1_000_000 { $acc += ($_ ** 3) % 1000; }
say $acc;
