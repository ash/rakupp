# CONTRACT test: the nastiest same-slot shape — workers overwrite ONE shared
# scalar with POINTER-CARRYING values (strings, arrays, hashes) while a
# reader copies it. A torn copy corrupts refcounts and crashes; the runtime
# must keep every copy whole. Which value wins is undefined behavior.
my $N = (@*ARGS[0] // 3).Int;
my $M = (@*ARGS[1] // 3000).Int;
my $slot = 'initial';
my @writers = (^$N).map: -> $w {
    start {
        for ^$M -> $i {
            $slot = $w == 0 ?? "str-$i"
                 !! $w == 1 ?? [1, 2, $i]
                 !! { k => $i };
        }
    }
};
my $reader = start {
    my $n = 0;
    for ^($M * 2) {
        my $copy = $slot;      # copy while writers overwrite
        $n++ if $copy.defined;
    }
    $n
};
await @writers;
my $r = await $reader;
say "survived: reads=$r (which values were seen is UB; dying is not)";
say 'PASS';
