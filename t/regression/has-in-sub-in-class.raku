# BROKE: leg 18 — the "has outside a class" check fired for `has` inside a
# sub nested in a class body, which Rakudo tolerates (S12-attributes/
# instance.t died at parse: 110 -> 0, caught by gate dg1).
# FIXED: same leg — the diagnostic requires classDepth == 0. This locks
# that the shape PARSES (the accessor semantics differ from Rakudo still).
class AttrInSub {
    sub f {
        has $.x;
    }
}
die 'class did not survive' unless AttrInSub.^name eq 'AttrInSub';
say 'PASS';
