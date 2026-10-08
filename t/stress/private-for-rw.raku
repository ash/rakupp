# CONTRACT (PARALLEL-SCALING-PLAN P3), as private-start-capture.raku: a `for` over the variable with
# an `is rw` loop variable, written on a worker.
#
# The owner stores into `$v` and copies it while the worker runs,
# while the worker stores pointer-carrying values into it through that route.
# Under RAKUPP_PRIVATE_SLOTS=all (every slot treated as private) this program
# fails within a few runs; by default it never may.
my $M = (@*ARGS[0] // 30000).Int;
sub owner() {
    my $v = 'start';
    my $w;
    for $v -> $x is rw { $w = start { for ^$M -> $i { $x = $i %% 2 ?? "f-$i" !! [1, 2, $i] } } }
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
