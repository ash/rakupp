# `nqp::div_i` and `nqp::mod_i` of the int64 minimum by -1 answer what
# MoarVM answers, on every machine: the division was C's, which traps there
# on x86-64 (an arm64 machine wrapped, and so never showed it). Found by the
# review of src/ (docs/dev/findings/src-review-2026-11-09.md, E5). Every
# line answers the same under Rakudo 2026.09.

use nqp;

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $min = -9223372036854775808;
ck nqp::div_i($min, -1), $min, 'div_i of the int64 minimum by -1 wraps';
ck nqp::mod_i($min, -1), 0, 'mod_i of it is 0';
ck nqp::div_i(7, -1), -7, 'div_i by -1';
ck nqp::mod_i(7, -1), 0, 'mod_i by -1';
ck nqp::div_i(-7, 2), -4, 'div_i floors, as before';
ck nqp::mod_i(-7, 2), -1, 'mod_i truncates';

say 'PASS';
