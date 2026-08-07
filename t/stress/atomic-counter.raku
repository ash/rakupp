# atomicint under real contention: N workers, M bumps each, no lock.
my $N = (@*ARGS[0] // 4).Int;
my $M = (@*ARGS[1] // 5000).Int;
my atomicint $a = 0;
await (^$N).map: {
    start {
        $a⚛++ for ^$M;
    }
};
if $a == $N * $M {
    say 'PASS';
}
else {
    say "FAIL a=$a want={$N * $M}";
    exit 1;
}
