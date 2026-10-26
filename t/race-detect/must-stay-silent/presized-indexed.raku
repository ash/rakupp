# race-detect: silent
# edges: promise
# expect: 4000
#
# The plan's table, row 4: four workers store into distinct elements of a
# pre-sized array. Nobody changes its shape, so the stores never race, on
# either engine (row 3, the same into an EMPTY array, is a race on the
# shape: must-report/indexed-grow.raku).
my @r = Nil xx 4000;
await (^4).map: -> $w { start { @r[$w * 1000 + $_] = $_ for ^1000 } };
say @r.grep(*.defined).elems;
