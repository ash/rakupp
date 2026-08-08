# CONTRACT test (see ub-array-push.raku): one worker iterates an array while
# others push to it. The iteration's VALUES are undefined behavior — it may
# see any prefix, garbage counts are allowed — but iterator invalidation
# during vector growth must not crash the runtime.
my $N = (@*ARGS[0] // 3).Int;
my $M = (@*ARGS[1] // 2000).Int;
my @shared = 1, 2, 3;
my @writers = (^$N).map: {
    start {
        @shared.push($_) for ^$M;
    }
};
my $reader = start {
    my $sum = 0;
    for ^50 {
        $sum += $_ for @shared;   # iterate while writers grow the vector (rw-alias path)
        for @shared -> $x { $sum += $x }   # block form (snapshot path) — both raced push's realloc
        $sum += @shared.elems;
    }
    $sum
};
await @writers;
my $r = await $reader;
say "survived: reader-sum=$r elems={@shared.elems} (values are UB; dying is not)";
say 'PASS';
