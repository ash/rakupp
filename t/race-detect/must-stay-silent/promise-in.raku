# race-detect: silent
# edges: start promise
# expect: 2
#
# A timer's `.then`: the continuation runs on a thread of its own once the
# timer fires, spawned by the registering thread, so it sees $x (the fork);
# its value comes back through the continuation's promise.
my $x = 1;
my $q = Promise.in(0.05).then({ $x + 1 });
say await $q;
