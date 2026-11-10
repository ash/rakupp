# `.base`'s digit count and `.indent`'s amount are refused past what a Str
# has graphemes, as Rakudo refuses them and in its words:
# `255.base(16, 99999999999)` appended zeros, and `"abc".indent(99999999999)`
# spaces, until the process was killed. Found by the review of src/
# (docs/dev/findings/src-review-2026-11-09.md, D2). Every line answers the
# same under Rakudo 2026.09.

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

my $cap = 'cannot be greater than max allowed number of graphemes 4294967295';
ck refusal({ 255.base(16, 99999999999) }), "Repeat count (99999999999) $cap", '.base with 99999999999 digits';
ck refusal({ 255.base(16, 4294967296) }), "Repeat count (4294967296) $cap", '.base one past the cap';
ck refusal({ "abc".indent(99999999999) }), "Repeat count (99999999999) $cap", '.indent by 99999999999';
ck refusal({ "abc".indent(4294967296) }), "Repeat count (4294967296) $cap", '.indent one past the cap';
ck 255.base(16, 3), 'FF.000', '.base with digits, as before';
ck 1.5.base(2, 4), '1.1000', '…of a Rat';
ck 255.base(16), 'FF', '…and with none';
ck "abc".indent(2), '  abc', '.indent, as before';
ck " abc".indent(-1), 'abc', '…and outdenting';

say 'PASS';
