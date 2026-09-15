# `Slip.from-iterator($it)` must answer a Slip. It drained the iterator into a
# plain List here, so a class building `method Slip { Slip.from-iterator(self
# .iterator) }` — the shape the whole Agnostic role family uses — failed
# `is-deeply @a.Slip, (1,2,3).Slip` with both sides rendering identically.
# A Slip's `.raku` now says so too: Rakudo writes `slip(1, 2, 3)`.
use Test;
plan 5;

my $slip = Slip.from-iterator((1, 2, 3).iterator);
is $slip.^name, 'Slip',                 'the container keeps the type it was asked for';
ok $slip eqv (1, 2, 3).Slip,            '…so it compares equal to a Slip';
nok $slip eqv (1, 2, 3),                '…and unequal to the same elements as a List';

is (1, 2, 3).Slip.raku, 'slip(1, 2, 3)', 'a Slip says what it is in .raku';
is List.from-iterator((1, 2).iterator).^name, 'List', 'a List still answers List';
