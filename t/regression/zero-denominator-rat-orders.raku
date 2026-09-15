# A zero-denominator Rat is EQUALITY-compared as its Num — `<0/0>` is NaN, so
# `$z == $z` is False — but ORDERED by the ordinary cross-multiplication.
# Routing the ordering through NaN made `<0/0> <= 1` False where Rakudo says
# True, and Algorithm::KDimensionalTree prunes with exactly that test: the
# Canberra distance between two axis-projected vectors is `<0/0>`, the other
# branch was never searched, and the k nearest neighbours came back wrong on
# 184 of 400 queries.
use Test;
plan 10;

my $z = <0/0>;
ok  ($z <= 1),   '<0/0> <= 1 cross-multiplies to 0 <= 0';
ok  ($z >= 1),   '…and >= likewise';
nok ($z <  1),   '…while < is 0 < 0, which is False';
nok ($z >  1),   '…and > likewise';
is  ($z <=> 1).gist, 'Same', '<=> is Same';
is  ($z <=> $z).gist, 'Same', '…against itself too';

# equality keeps NaN semantics
nok ($z == $z),  'but == is NaN semantics: False against itself';
ok  ($z != $z),  '…and != is True';

# an infinite Rat still orders the obvious way
my $i = <1/0>;
ok  ($i > 5),    '<1/0> is greater than everything finite';
ok  (<-1/0> < -5), '…and <-1/0> is less';
