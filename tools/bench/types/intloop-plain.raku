# An integer accumulator loop over plain `my $` variables.
# The pair of intloop-native.raku: same arithmetic, same checksum.
my $n = (@*ARGS[0] // 5_000_000).Int;
my $i = 0;
my $s = 0;
while $i < $n {
    $s = $s + ($i % 7) * 3;
    $i = $i + 1;
}
say "sum=$s";
