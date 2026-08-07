# N workers emit into one Supplier; a single tap collects. Emissions are
# serialized per tap — nothing may be lost or torn.
my $N = (@*ARGS[0] // 4).Int;
my $M = (@*ARGS[1] // 500).Int;
my $s = Supplier.new;
my ($count, $sum) = 0, 0;
my $tap = $s.Supply.tap({ $count++; $sum += $_ });
await (^$N).map: -> $w {
    start {
        $s.emit($w * $M + $_) for ^$M;
    }
};
my $want = ($N * $M) * ($N * $M - 1) div 2;
if $count == $N * $M && $sum == $want {
    say 'PASS';
}
else {
    say "FAIL count=$count sum=$sum (want {$N * $M} / $want)";
    exit 1;
}
