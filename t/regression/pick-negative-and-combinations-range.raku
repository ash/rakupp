# Two list methods given a count below zero, each found by the review of
# src/ (docs/dev/findings/src-review-2026-11-09.md, D8 and D7):
#  - `.pick(-1)` of a Range of 1024 or more is none of it, where the count
#    was read as unsigned and clamped to ALL of it — `(1..5000).pick(-1)`
#    was the 5000, shuffled; and of a list it is refused, as on Rakudo,
#    where it was an empty Seq;
#  - `.combinations` over a Range of sizes starts at size 0, where it
#    stepped through every size from the range's start —
#    `(1,2,3).combinations(-9223372036854775808..1)` did not come back.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub refusal(&code) {
    code();
    return 'no refusal';
    CATCH { default { return .message.lines[0] } }
}

ck (1..5000).pick(-1).elems, 0, 'a big Range, .pick(-1)';
ck (1..5).pick(-1).elems, 0, 'a small one';
ck refusal({ (1, 2, 3).pick(-1) }), 'Coercion to UInt out of range. Is: -1, should be in 0..^Inf', 'a list';
ck (1..5000).pick(3).elems, 3, '.pick(3), as before';
ck (1..5000).pick(*).elems, 5000, '.pick(*)';
ck (1, 2, 3).pick(2).elems, 2, 'of a list';
ck (1, 2, 3).pick(*).sort.List, (1, 2, 3), '…all of it';

ck (1, 2, 3).combinations(-9223372036854775808..1).elems, 4, 'sizes from the int64 minimum to 1';
ck (1, 2, 3).combinations(-5..1).List, ((), (1,), (2,), (3,)), 'sizes -5 to 1';
ck (1, 2, 3).combinations(1..2).elems, 6, 'sizes 1 to 2, as before';
ck (1, 2, 3).combinations(2..9223372036854775807).elems, 4, 'sizes 2 to the int64 maximum';

say 'PASS';
