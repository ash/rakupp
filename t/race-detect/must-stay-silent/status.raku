# race-detect: silent
# edges: promise
# expect: 42
#
# Polling `.status` until the promise is no longer Planned, then reading what
# the worker wrote. Provisionally an edge (RACE-DETECTOR-PLAN open question
# 1): Rakudo sets the status after the result, and a status read that says
# Kept is taken as an acquire.
my $x;
my $p = start { $x = 42 };
sleep 0.01 while $p.status ~~ Planned;
say $x;
