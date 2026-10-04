# Regression: an Array built from an endless integer Range is a lazy Array of
# its elements. `[1..*]` stored the Range as its ONE element — the one-arg
# spread refuses an endless Range — so `[1..*][^5]` read
# `(1..Inf (Any) (Any) (Any) (Any))` and `.is-lazy` was False;
# `(1..*).Array` died "Cannot Array an infinite range"; and under --exe
# `my @a = 1..*` flattened up to a cap and said `@a.is-lazy` False.
#
# Every expectation below was checked against Rakudo 2026.09. (`lazy 1..3`,
# a FINITE lazy Range, is not covered: Rakudo's lazy Array also refuses
# `.elems`, which is not modelled.)

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

ck([1..*].is-lazy, True, '[1..*] is lazy');
ck([1..*][^5].List, (1, 2, 3, 4, 5), 'and indexes as its elements');
ck([1..*].head(3).List, (1, 2, 3), 'and heads as them');
ck([^Inf].is-lazy, True, '[^Inf] is lazy');
ck([0..^Inf][2], 2, 'an excluded end');
ck([5^..*][0], 6, 'an excluded start');
ck([1..3].is-lazy, False, 'a finite Range is not');
ck([1..*, 5].elems, 2, 'two items are two elements, the Range one of them');
my $r = 1..*;
ck([$r].elems, 1, 'a $-held endless Range is one item');

ck((1..*).Array.is-lazy, True, '(1..*).Array is lazy');
ck((1..*).Array[3], 4, 'and indexes');

my @a = 1..*;
ck(@a.is-lazy, True, 'my @a = 1..* is lazy');
ck(@a[^3].List, (1, 2, 3), 'and indexes');
my @d = [1..*];
ck(@d[2], 3, 'an array assigned from [1..*] reads its elements');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
