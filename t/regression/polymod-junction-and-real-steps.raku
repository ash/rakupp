# `.polymod` runs Rakudo's steps (Rakudo::Iterator's Polymod): a Junction
# divisor is taken whole, so `1.polymod(2 & 3)` is `(all(1, 1) 0)` where it
# threaded the call, `all((1 0), (1 0))`; a Real invocant stops at a divisor
# of 1 (`2.5.polymod(1, 2)` was `(0.5 0 1)`), takes an infinite list, fails a
# negative invocant and names polymod in its divide-by-zero; and only an
# infinite divisor list stops early on 0 (`lazy 2, 2, 2` is `2, 2, 2`).
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try c().elems; "{$!.^name}: {$!.message}" }

ck 1.polymod(2 & 3).gist, '(all(1, 1) 0)', 'an all';
ck 10.polymod(3 | 4).gist, '(any(1, 2) any(3, 2))', 'an any';
ck 7.polymod(2, 1 | 2).gist, '(1 any(0, 1) any(3, 1))', 'one among others';
ck 6.polymod(2 | 3).gist, '(0 any(3, 2))', 'a false step is 0';
ck 2.5.polymod(2 | 3).gist, '(any(0.5, 2.5) any(any(1, 0.666667), any(0, 0)))', 'a Real';
ck msg({ 4.polymod(2 & 3, 0) }), 'X::Numeric::DivideByZero: Attempt to divide 2 by zero using polymod', 'its zero';

ck 2.5.polymod(1, 2).List, (2.5,), 'a Real stops at 1';
ck 2.5.polymod(2 xx *).List, (0.5, 1.0), 'an infinite list';
ck 2.5.polymod(1..*).List, (2.5,), 'and a Range';
ck 2.5.polymod(lazy 2, 3).List, (0.5, 1.0, 0), 'a finite lazy list is a list';
ck 4.polymod(lazy 2, 2, 2).List, (0, 0, 1, 0), 'for an Int too';
ck msg({ 5.5.polymod(0) }), 'X::Numeric::DivideByZero: Attempt to divide 5.5 by zero using polymod', "a Real's zero";
ck (-2.5).polymod(2).exception.message, 'invocant to polymod out of range. Is: -2.5, should be in 0..^Inf', 'a negative Real';

ck (120.polymod(10, 10), 7.5.polymod(2, 3), 12.polymod(14 xx *), 100.polymod(2..*))».List,
   ((0, 2, 1), (1.5, 0, 1.0), (12,), (0, 2, 0, 4)), 'as before';

say 'PASS';
