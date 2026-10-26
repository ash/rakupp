# race-detect: report
# location: hash shape %seen
#
# The plan's table, row 2: every worker inserts keys no other worker uses.
# The entries are distinct, but each insert of a new key writes the hash's
# shape, so the workers race on it. rakupp survives this (hash insert is
# striped) and MoarVM dies on it; it is a race on both.
my %seen;
await (^4).map: -> $w { start { %seen{"$w-$_"} = True for ^2_500 } };
say %seen.elems;
