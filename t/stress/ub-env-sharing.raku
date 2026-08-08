# CONTRACT test for the plan's named risk: two start blocks CLOSE OVER one
# outer scope and hammer its variables — reads, writes, and fresh `my`
# declarations inside loops that share the outer Env chain. Unsynchronized
# closure state is the user's race; the Env machinery must survive it.
my $N = (@*ARGS[0] // 4).Int;
my $M = (@*ARGS[1] // 2000).Int;
my $shared-counter = 0;
my $shared-str = 'x';
await (^$N).map: -> $w {
    start {
        for ^$M -> $i {
            $shared-counter = $shared-counter + 1;   # shared slot write
            $shared-str = "w$w-$i";                  # shared pointer-carrying write
            my $local = $shared-counter + $i;        # fresh my in the loop, reads shared
            $local++;
        }
    }
};
say "survived: counter=$shared-counter str=$shared-str (values are UB; dying is not)";
say 'PASS';
