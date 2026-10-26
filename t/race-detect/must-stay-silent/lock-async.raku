# race-detect: silent
# edges: lock-async
# expect: 10
#
# Lock::Async found free: the holders take turns (the sleeps keep them apart,
# so nobody queues), and each `.lock` that finds the lock free acquires what
# the last `.unlock` released into the lock. The sleeps order nothing; the
# lock does.
my $l = Lock::Async.new;
my @log;
my $w = start { for ^5 { $l.protect: { @log.push('w') }; sleep 0.05 } };
sleep 0.025;
for ^5 { $l.protect: { @log.push('m') }; sleep 0.05 }
await $w;
say @log.elems;
