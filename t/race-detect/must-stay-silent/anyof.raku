# race-detect: silent
# edges: promise
# expect: 42
#
# `await Promise.anyof(…)` acquires the member that settled it. The timer
# member is 30 seconds out, so the start block is the one that wins.
my $x;
my $p = start { $x = 42 };
await Promise.anyof($p, Promise.in(30));
say $x;
