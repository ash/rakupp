# race-detect: report
# location: array element @a[0]
#
# Two workers store into the SAME element of a pre-sized array. The shape
# never changes; the element is written by both with nothing between.
my @a = 0 xx 4;
await (^2).map: -> $w { start { @a[0] = $w for ^2_000 } };
say @a[0] ~~ 0 | 1;
