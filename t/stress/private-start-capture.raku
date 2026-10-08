# CONTRACT (PARALLEL-SCALING-PLAN P3): a routine's `my` that a `start` block
# closes over is SHARED. A slot no other thread can reach is read and written
# without the stripe while workers are live; this one can be reached, so the
# owner's own reads and writes keep the stripe, and a worker storing
# pointer-carrying values cannot tear the owner's copy.
#
# RAKUPP_PRIVATE_SLOTS=all treats every slot as private. This program then
# crashes or corrupts a refcount within a few runs: that is how it proves it
# can fail. Which value each copy saw is undefined; dying is not allowed.
my $M = (@*ARGS[0] // 30000).Int;
sub owner() {
    my $v = 'start';
    my $w = start {
        for ^$M -> $i { $v = $i %% 2 ?? "w-$i" !! [1, 2, $i] }
    };
    my $n = 0;
    # (bounded: under the GIL the worker cannot run until this loop ends)
    for ^($M * 20) {
        last unless $w.status == Planned;
        $v = "o-" ~ $n;         # the owner's store
        my $c = $v;             # the owner's copy
        $n++ if $c.defined;
    }
    await $w;
    $n
}
say "survived: {owner()} copies";
say 'PASS';
