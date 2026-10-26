# race-detect: report
# location: array shape @out
#
# t/stress/ub-array-push.raku's shape, small: two workers push to one array.
# Every push writes the shape.
my @out;
await (^2).map: { start { @out.push($_) for ^2_000 } };
say @out.elems <= 4_000;
