# BROKE: leg 18 — the new "has outside a class" diagnostic fired inside
# anonymous-class bodies reached through colon-args (`Seq.new: class :: ...`),
# which parse their statements via parseStatement, not parseClass: the whole
# file died at parse (S32-list/tail.t 51/57 -> GONE, caught by gate dg1).
# FIXED: same leg — the check only fires at true program top level.
# (The construct's RUNTIME shape is still degenerate in rakupp; what this
# case locks is that the file PARSES — that is what regressed.)
sub make-thing {
    Seq.new: class :: does Iterable {
        has $!i;
        method thing { 42 }
    }.new
}
say 'PASS';
