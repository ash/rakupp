# N workers, M increments each, all under one Lock.protect — the classic
# lost-update test. Also pushes to a shared array under the same lock.
my $N = (@*ARGS[0] // 4).Int;
my $M = (@*ARGS[1] // 2000).Int;
my $lock = Lock.new;
my $total = 0;
my @seen;
await (^$N).map: {
    start {
        for ^$M {
            $lock.protect({ $total++; @seen.push($_) });
        }
    }
};
if $total == $N * $M && @seen.elems == $N * $M {
    say 'PASS';
}
else {
    say "FAIL total=$total elems={@seen.elems} (want {$N * $M})";
    exit 1;
}
