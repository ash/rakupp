# race-detect: silent
# edges: channel
# expect: 0 1 4 9 16
#
# Each Channel item carries its sender's clock: the producer fills @slot[$i]
# and then sends $i, so the consumer that receives $i may read @slot[$i]. The
# array is pre-sized, so the producer's later stores touch other elements
# and never its shape.
my @slot = Nil xx 5;
my $c = Channel.new;
start { for ^5 -> $i { @slot[$i] = $i * $i; $c.send($i) }; $c.close };
say $c.list.map({ @slot[$_] }).join(' ');
