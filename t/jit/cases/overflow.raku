# The `-O` int lane overflows into the bignum path INSIDE the kernel, through
# the same runtime the interpreter uses. The answer has to be exact.
my $p = 1; my $i = 1;
while $i < 40 { $p = $p * 3; $i = $i + 1 }
say $p;
my $big = 9223372036854775000; my $k = 0;
while $k < 2000 { $big = $big + 1000; $k = $k + 1 }
say $big;
