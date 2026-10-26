# race-detect: report
# location: scalar $x
#
# A missing edge: the producer writes $x AFTER its send. The receive acquires
# only what happened before the item was sent, so the consumer's read of $x
# is not ordered after the write.
my $c = Channel.new;
my $x = 0;
my $p = start { $c.send(1); $x = 42 };
my $v = $c.receive;
say $x;
await $p;
