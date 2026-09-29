# A floating-point accumulator loop over declared `my int` / `my num` variables.
# The pair of numloop-plain.raku: same arithmetic, same checksum.
my int $n = (@*ARGS[0] // 5_000_000).Int;
my int $i = 0;
my num $s = 0e0;
while $i < $n {
    $s = $s + $i * 0.5e0;
    $i = $i + 1;
}
say "sum=$s";
