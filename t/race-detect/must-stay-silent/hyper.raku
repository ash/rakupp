# race-detect: silent
# edges: hyper promise
# expect: 1498500
#
# `.hyper.map` forks its batches out to workers, which read $factor written
# before (the fork), and joins them before the results are used.
my $factor = 3;
my @r = (^1000).hyper(:batch(50)).map({ $_ * $factor });
say @r.sum;
