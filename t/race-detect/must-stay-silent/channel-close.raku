# race-detect: silent
# edges: channel-close
# expect: 42
#
# A write after the last send is ordered only by the close: the reader that
# sees the channel closed (its `for` over `.list` ends) acquires the
# closer's clock. (`for`, not `my @all = $c.list`: rakupp's Channel.list says
# it is lazy, so a list assignment from it returns at once, where Rakudo's
# waits for the close.)
my $c = Channel.new;
my $x;
start { $c.send(1); $x = 42; $c.close };
for $c.list { }
say $x;
