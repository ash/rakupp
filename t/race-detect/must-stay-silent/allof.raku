# race-detect: silent
# edges: promise
# expect: 0 10 20 30
#
# `await Promise.allof(@p)` orders every member's past before what follows.
# The workers write distinct elements of a pre-sized array, which is not a
# race (no shape changes); the mainline reads them all after the allof.
my @r = 0 xx 4;
my @p = (^4).map: -> $i { start { @r[$i] = $i * 10 } };
await Promise.allof(@p);
say @r.join(' ');
