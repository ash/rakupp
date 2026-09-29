# An integer accumulator loop over declared `my int` variables.
# The pair of intloop-plain.raku: same arithmetic, same checksum.
my int $n = (@*ARGS[0] // 5_000_000).Int;
my int $i = 0;
my int $s = 0;
while $i < $n {
    $s = $s + ($i % 7) * 3;
    $i = $i + 1;
}
say "sum=$s";
