# race-detect: silent
# edges: start promise
# expect: 42
#
# `Thread.start` forks; `.finish` joins, acquiring everything the thread did.
# (rakupp runs a Thread through the promise machinery, so its join is the
# thread's promise settling.)
my $x = 0;
my $t = Thread.start({ $x = 42 });
$t.finish;
say $x;
