# Regression: Rat.base-repeating, two ways it differed from Rakudo, found by
# t/oracle/rat.raku (ORACLE-PLAN P2) with Python's Fraction as the oracle.
#
#   - The expansion stopped at 10,000 fractional digits and answered what it
#     had with an EMPTY cycle, so any denominator with a longer period came out
#     wrong: 1/10007 has a 10,006-digit cycle. The cap is now a million digits,
#     and past it the call fails instead of answering.
#   - An integral value kept a trailing point: (4/1) gave ("4.", ""), Rakudo
#     ("4", ""). (In other bases Rakudo answers the DECIMAL digits, (4/1) in
#     base 2 as "4"; here it is "100", the base's own digits. That one case is
#     a deliberate difference and is not asserted against Rakudo below.)
#
# Expectations checked against Rakudo 2026.09 (all but the base-2 one).

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck (4/1).base-repeating(10),  ("4", ""),   'an integral Rat has no point';
ck (0/1).base-repeating(10),  ("0", ""),   'zero';
ck (-1/1).base-repeating(10), ("-1", ""),  'a negative integral Rat';
ck (1/4).base-repeating(10),  ("0.25", ""), 'a terminating expansion keeps its point';
ck (1/7).base-repeating(10),  ("0.", "142857"), 'a pure cycle';
ck (1/6).base-repeating(16),  ("0.2", "A"), 'a cycle after a digit, base 16';
ck (-1/3).base-repeating(10), ("-0.", "3"), 'a negative cycle';

{
    my ($nonrep, $cycle) = (1/10007).base-repeating(10);
    ck $nonrep, "0.", '1/10007 has no digits before its cycle';
    ck $cycle.chars, 10006, '1/10007 has its whole 10,006-digit cycle';
    ck $cycle.substr(0, 12), "000099930048", 'and it starts where the division does';
}
{
    my ($nonrep, $cycle) = (1/10009).base-repeating(10);
    ck $cycle.chars, 5004, '1/10009: a 5,004-digit cycle, under the old cap, still right';
}

say $fails ?? 'FAIL' !! 'PASS';
