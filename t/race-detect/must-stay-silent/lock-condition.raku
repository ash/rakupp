# race-detect: silent
# edges: lock
# expect: 42
# gil: skip (a condition `.wait` does not wait under RAKUPP_GIL=1: faq/threads.md)
#
# A Lock's condition: `.wait` gives the lock up while it waits and takes it
# back after, so the waiter sees what the signaller did under the lock. The
# edge is the lock's; `.signal` is not one on its own.
my $l = Lock.new;
my $c = $l.condition;
my $ready = False;
my $data;
my $consumer = start { $l.protect: { $c.wait({ $ready }); $data * 2 } };
$l.protect: { $data = 21; $ready = True; $c.signal };
say await $consumer;
