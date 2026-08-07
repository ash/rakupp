# N producers push M items each into one Channel; one consumer drains it.
# Every item must arrive exactly once — the Channel is the synchronization.
my $N = (@*ARGS[0] // 4).Int;
my $M = (@*ARGS[1] // 500).Int;
my $ch = Channel.new;
my @producers = (^$N).map: -> $p {
    start {
        $ch.send($p * $M + $_) for ^$M;
    }
};
await @producers;
$ch.close;
my ($count, $sum) = 0, 0;
for $ch.list -> $v {
    $count++;
    $sum += $v;
}
my $want = ($N * $M) * ($N * $M - 1) div 2;
if $count == $N * $M && $sum == $want {
    say 'PASS';
}
else {
    say "FAIL count=$count (want {$N * $M}) sum=$sum (want $want)";
    exit 1;
}
