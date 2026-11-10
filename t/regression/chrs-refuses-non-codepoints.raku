# `chrs` refuses what is no codepoint, as `chr` does: the sub and the method
# encoded whatever integer they were given, so `chrs(65, -1)` and
# `(65, 0x110000).chrs` were Strs holding four bytes that are no UTF-8.
# Found by the review of src/ (docs/dev/findings/src-review-2026-11-09.md,
# E3). Every line answers the same under Rakudo 2026.09.

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

ck refusal({ chrs(65, -1) }), 'chr codepoint -1 cannot be negative', 'the sub, a negative one';
ck refusal({ chrs(0x110000) }), 'chr codepoint 1114112 (0x110000) is out of bounds', 'the sub, one past the last';
ck refusal({ (65, -1).chrs }), 'chr codepoint -1 cannot be negative', 'the method, a negative one';
ck refusal({ (65, 0x110000).chrs }), 'chr codepoint 1114112 (0x110000) is out of bounds', 'the method, one past the last';
ck chrs(72, 105), 'Hi', 'the sub, as before';
ck (72, 105, 0x1F600).chrs, "Hi\x[1F600]", 'the method';
ck chrs(0x10FFFF).ord, 0x10FFFF, 'the last codepoint is one';

say 'PASS';
