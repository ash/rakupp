# `+<` refuses a count past 2**31, as Rakudo does and in its words:
# `1 +< 9223372036854775807` asked BigInt for two to that power and took all
# the memory there was until it was killed. Found by the review of src/
# (docs/dev/findings/src-review-2026-11-09.md, A8). Every line answers the
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

my $max = 9223372036854775807;
ck refusal({ 1 +< $max }), 'Error in mp_mul_2d: Value out of range', 'a count of the int64 maximum';
ck refusal({ 1 +< 2147483648 }), 'Error in mp_mul_2d: Value out of range', 'one of 2**31';
ck refusal({ my $x = 1; $x +<= $max }), 'Error in mp_mul_2d: Value out of range', '+<=';
ck (1 +< 100).msb, 100, 'a count that is not past it';
ck 3 +< 70, 3541774862152233910272, 'a big result';
ck -1 +< 65, -36893488147419103232, 'a negative operand';
ck 1 +< -1, 0, 'a negative count shifts right';

say 'PASS';
