# race-detect: silent
# edges: start
# expect: 5050 5050
#
# A `start` block reads what its spawner wrote before the `start`. The fork
# edge orders the spawner's past before the block (ASYNC.md: a `start` block
# sees what was done before it was started); `Promise.start` is the same
# spawn. The results come back through `await`, the promise edge, but what
# this file tests is the read of @data inside each block.
my @data = 1..100;
my $p = start { @data.sum };
my $q = Promise.start({ @data.sum });
say (await $p), ' ', (await $q);
