# race-detect: silent
# edges: atomic
# expect: 2000
#
# `cas` with a block is a compare-and-swap retry loop: atomic, like `⚛`.
my $n = 0;
await (^4).map: { start { for ^500 { cas $n, -> $v { $v + 1 } } } };
say $n;
