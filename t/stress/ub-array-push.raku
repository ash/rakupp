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
# …and the sub forms, which the builtins do in place: `push`, `pop` and
# `shift` took no stripe until 2026-10-25, and this phase crashed every run
# of the binary before (6 of 6)
my @sub;
await (^$N).map: { start { push @sub, $_ for ^$M } };
await (^$N).map: { start { pop @sub for ^($M div 4); shift @sub for ^($M div 4) } };
say "survived: elems={@out.elems} of {$N * $M}, {@sub.elems} after the sub forms (data loss is allowed; dying is not)";
say 'PASS';
