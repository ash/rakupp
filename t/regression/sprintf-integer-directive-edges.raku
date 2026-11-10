# sprintf's integer directives at the edges of what they are given, each
# found by the review of src/ (docs/dev/findings/src-review-2026-11-09.md,
# E2 and E3):
#  - a Num past an int64 prints its exact integer: `sprintf("%d", 1e30)` was
#    9223372036854775807, and `%x`, `%o`, `%b`, `%u` saturated the same way;
#  - an infinite or NaN one is refused, where it printed the int64 maximum
#    and 0;
#  - `%c` refuses what is no codepoint, where -1 and 0x110000 became four
#    bytes that are no UTF-8.
# Every line answers the same under Rakudo 2026.09 (whose refusals say the
# directive and the type, as these start).

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub refusal(&code) {
    code();
    return 'no refusal';
    CATCH { default { return .message.lines[0].substr(0, 49).trim } }
}

ck sprintf("%d", 1e30), '1000000000000000019884624838656', '%d of 1e30';
ck sprintf("%d", -1e30), '-1000000000000000019884624838656', '%d of -1e30';
ck sprintf("%x", 1e30), 'c9f2c9cd04675000000000000', '%x of 1e30';
ck sprintf("%5d|", 1e20), '100000000000000000000|', 'with a width';
ck sprintf("%d", 1e15), '1000000000000000', 'a Num an int64 holds, as before';
ck sprintf("%d", 3.7e0), '3', '…and a fractional one';
ck sprintf("%d", 2**70), '1180591620717411303424', '…and a big Int';

ck refusal({ sprintf("%d", Inf) }), 'Directive %d not applicable for value of type Num', '%d of Inf';
ck refusal({ sprintf("%d", NaN) }), 'Directive %d not applicable for value of type Num', '%d of NaN';
ck refusal({ sprintf("%x", -Inf) }), 'Directive %x not applicable for value of type Num', '%x of -Inf';

ck refusal({ sprintf("%c", -1) }), 'Directive %c not applicable for value of type Int', '%c of -1';
ck refusal({ sprintf("%c", 0x110000) }), 'Directive %c not applicable for value of type Int', '%c past the last codepoint';
ck sprintf("%c%c", 72, 0x1F600), "H\x[1F600]", '%c of codepoints, as before';

say 'PASS';
