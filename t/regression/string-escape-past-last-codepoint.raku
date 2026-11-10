# A `\x[…]` escape past the last codepoint is a compile error, as on Rakudo:
# `"\x[110000]"` was a Str of four bytes that are no UTF-8 (its `.ords` said
# 1114112). Found by the review of src/
# (docs/dev/findings/src-review-2026-11-09.md, G8). Every line answers the
# same under Rakudo 2026.09 — which, like this, lets a surrogate through.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub compiling(Str $code) {
    EVAL $code;
    return 'compiled';
    CATCH { default { return .message.lines.first(*.contains('codepoint')) // .message.lines[0] } }
}

ck compiling(Q[my $s = "\x[110000]";]).contains('chr codepoint 1114112 (0x110000) is out of bounds'), True, '\x[110000]';
ck compiling(Q[my $s = "a\x[41, 110000]b";]).contains('is out of bounds'), True, '…in a list of them';
ck compiling(Q[my $s = "\o[4200000]";]).contains('is out of bounds'), True, '\o past the last codepoint';
ck compiling(Q[my $s = "\x[10FFFF]";]), 'compiled', 'the last codepoint is one';
ck "\x[41]\x[1F600]".ords.List, (65, 128512), 'codepoints, as before';
ck "\x[41, 42]\c[67]\o[104]", 'ABCD', '…in each spelling';

say 'PASS';
