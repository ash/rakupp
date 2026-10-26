# race-detect: silent
# edges: promise
# expect: 2000
#
# Lock::Async under contention: `.protect` awaits the lock, runs the block,
# and hands the lock on. A waiter's `.lock` answered a Promise, which the
# unlocker keeps when it hands the lock over: that keep and the waiter's
# await are the edge between two holders (lock-async.raku has the other
# path, a lock found free).
my $l = Lock::Async.new;
my $n = 0;
await (^4).map: { start { for ^500 { $l.protect: { $n++ } } } };
say $n;
