# Same contract as ub-array-push: unguarded hash writes from N workers,
# including the same hot keys (rehash during insert during read). Values
# are UB; surviving is the requirement.
my $N = (@*ARGS[0] // 4).Int;
my $M = (@*ARGS[1] // 2000).Int;
my %h;
await (^$N).map: -> $w {
    start {
        for ^$M -> $i {
            %h{"k" ~ ($i % 53)}++;
            %h{"w$w-$i"} = $i;
        }
    }
};
say "survived: keys={%h.keys.elems} (data loss is allowed; dying is not)";
say 'PASS';
