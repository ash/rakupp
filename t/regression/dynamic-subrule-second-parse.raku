# A `<{ code }>` pattern that calls a rule of its grammar parses a second
# time: its compiled regex is kept for the interpreter's life, and the
# `<foo>` in it remembered where the FIRST parse's matcher had found foo —
# a matcher that parse had since destroyed — so the second `G.parse` read
# freed storage and the process died with a segfault. The node's cache now
# says which matcher it belongs to. Found by the review of src/
# (docs/dev/findings/src-review-2026-11-09.md, F2). Every line answers the
# same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

grammar G { token TOP { <{ q[<foo>] }> }; token foo { a } }
grammar H { token TOP { <{ q[<foo>] }> }; token foo { b } }
ck ~G.parse("a"), 'a', 'the first parse';
ck ~G.parse("a"), 'a', 'the second';
ck ~H.parse("b"), 'b', "the same pattern text in another grammar finds that grammar's rule";
ck ~G.parse("a"), 'a', '…and the first grammar still finds its own';
ck H.parse("a"), Nil, '…which the other does not match';

grammar L { token TOP { <x>+ }; token x { <{ q[<y>] }> }; token y { \d } }
ck ~L.parse("123"), '123', "a dynamic subrule under a quantifier, parse $_" for ^3;

say 'PASS';
