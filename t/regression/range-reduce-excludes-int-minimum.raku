# `[max]` and `[min]` over an Int Range that excludes the int64 minimum as
# its end see an empty range: the end was stepped in by one, which wrapped
# to the int64 maximum, so `[max] 5..^(-9223372036854775808)` was
# 9223372036854775807 and `[min]` of it 5, where Rakudo has -Inf and Inf.
# Found by the review of src/ (docs/dev/findings/src-review-2026-11-09.md,
# B1; a range that ends past 9e18 or starts below -9e18 is still taken for
# an endless one, and is left open there). Every line answers the same under Rakudo
# 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my $min = -9223372036854775808;
ck ([max] 5..^$min), -Inf, '[max] of a range that excludes the int64 minimum';
ck ([min] 5..^$min), Inf, '[min] of it';
ck ([max] 1..5), 5, '[max] of a plain range, as before';
ck ([min] 2^..5), 3, '[min] with an excluded start';
ck ([max] 1..^5), 4, '[max] with an excluded end';
ck ([max] 5..1), -Inf, 'an empty range';

say 'PASS';
