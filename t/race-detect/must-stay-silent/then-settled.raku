# race-detect: silent
# edges: promise
# expect: 43
#
# A `.then` registered after its promise has settled runs at once, on the
# registering thread, which has seen the promise settled: the promise edge.
my $x;
my $p = start { $x = 42 };
await $p;
my $q = $p.then({ $x + 1 });
say await $q;
