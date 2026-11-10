# `polymod` treats a divisor the same whatever the invocant and however the
# divisors come: its five loops had different rules. Over a big Int a zero
# divisor ended the list quietly — `(2**70).polymod(0)` was (2**70) — a
# divisor of 1 did not end it, and one past an int64 divided as the int64
# maximum; over a lazy list of divisors a zero dropped what was left, so
# `5.polymod(0 xx *)` was (). Found by the review of src/
# (docs/dev/findings/src-review-2026-11-09.md, D5). Every line answers the
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

my $big = 2**70;
ck refusal({ $big.polymod(0) }), 'Attempt to divide 1180591620717411303424 by zero using polymod', 'a big Int by zero';
ck refusal({ 120.polymod(0) }), 'Attempt to divide 120 by zero using polymod', 'an Int by zero, as before';
ck refusal({ 5.polymod(0 xx *) }), 'Attempt to divide 5 by zero using polymod', 'an Int by a lazy list of zeros';
ck refusal({ $big.polymod(0 xx *) }), 'Attempt to divide 1180591620717411303424 by zero using polymod', 'a big Int by one';

ck $big.polymod(1, 10).List, (1180591620717411303424,), 'a divisor of 1 ends a big Int';
ck 120.polymod(1, 10).List, (120,), '…as it ends an Int';
ck (2**100).polymod(2**70).List, (0, 1073741824), 'a divisor past an int64';
ck $big.polymod(10, 10).List, (4, 2, 11805916207174113034), 'small divisors of a big Int, as before';
ck 0xFFFF_FFFF_FFFF_FFFF.polymod(256 xx 7).List, (255, 255, 255, 255, 255, 255, 255, 255), '…and bytes';
ck 1234.polymod(10 xx *).List, (4, 3, 2, 1), 'a lazy list of divisors';

say 'PASS';
