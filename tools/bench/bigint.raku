# Arbitrary-precision arithmetic: build factorial(5000) as a running product,
# which overflows a machine word almost immediately and spends the rest of the
# run in the BigInt (base-1e9) multiply path. Prints the digit count so the
# output stays small but the full number must be computed.
my $f = 1;
$f *= $_ for 1 .. 5000;
say $f.chars;
