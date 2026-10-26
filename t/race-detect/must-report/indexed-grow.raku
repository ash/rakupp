# race-detect: report
# location: array shape @r
# par: skip (the stale element pointer crashes it under the detector about one run in 20; see below)
#
# The plan's table, row 3: four workers store into distinct indices of an
# array that starts EMPTY. A store past the end grows the array, which is a
# write of its shape, so the workers race on it (Rakudo loses writes; rakupp
# crashed on it until 90bc6e23, and the stale element pointer is still open:
# docs/dev/experiments/STALE-ELEMENT-POINTER.md). Kept small, so that the
# engine survives it long enough to be told. Under the detector it still dies
# about one run in 20 in parallel mode: the detector's work at a store runs
# between lvalue() and the write, which is the window that bug lives in. The
# GIL leg, where nothing grows the array during that window, is the check.
my @r;
await (^4).map: -> $w { start { @r[$w * 500 + $_] = $_ for ^500 } };
say @r.elems;
