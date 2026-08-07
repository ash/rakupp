# THE CONTRACT TEST, not a correctness test: N workers push to one array
# with NO lock. That is a data race — the VALUES are undefined behavior and
# nothing here checks them. What the memory model does promise is that the
# RUNTIME survives: no crash, no abort. This program passes if it reaches
# the final say alive; the element count is printed as data, not asserted.
my $N = (@*ARGS[0] // 4).Int;
my $M = (@*ARGS[1] // 2000).Int;
my @out;
await (^$N).map: {
    start {
        @out.push($_) for ^$M;
    }
};
say "survived: elems={@out.elems} of {$N * $M} (data loss is allowed; dying is not)";
say 'PASS';
