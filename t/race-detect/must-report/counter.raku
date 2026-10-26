# race-detect: report
# location: scalar $n
#
# The plan's table, row 1: `$n++` from four workers with nothing between
# them. Each `++` reads and writes $n; no Lock, await, Channel, Supply or ⚛
# orders one worker's against another's.
my $n = 0;
await (^4).map: { start { $n++ for ^10_000 } };
say $n;
