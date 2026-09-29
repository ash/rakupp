# A floating-point accumulator loop over plain `my $` variables, Num literals.
# The pair of numloop-native.raku: same arithmetic, same checksum.
my $n = (@*ARGS[0] // 5_000_000).Int;
my $i = 0;
my $s = 0e0;
while $i < $n {
    $s = $s + $i * 0.5e0;
    $i = $i + 1;
}
say "sum=$s";
