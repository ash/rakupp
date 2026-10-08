# CONTRACT (PARALLEL-SCALING-PLAN P3), as private-start-capture.raku: EVAL reached through `&EVAL`
# held in a variable, naming the variable in its text.
#
# The owner stores into `$v` and copies it while the worker runs,
# while the worker stores pointer-carrying values into it through that route.
# Under RAKUPP_PRIVATE_SLOTS=all (every slot treated as private) this program
# fails within a few runs; by default it never may.
my $M = (@*ARGS[0] // 20000).Int;
sub owner() {
    my $v = 'start';
    my &e = &EVAL;
    my $w = start { for ^$M -> $i { e(q{$v = $i %% 2 ?? "x-$i" !! [1, 2, $i]}) } };
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
