# race-detect: silent
# edges: lock
# expect: 4000
#
# The same counter with explicit `.lock` / `.unlock`.
my $l = Lock.new;
my $n = 0;
await (^4).map: { start { for ^1000 { $l.lock; $n++; $l.unlock } } };
say $n;
