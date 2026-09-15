# `|*` is a Whatever-curry, not a spread: the one-level flattener that
# `@aoa.map(|*)` uses. Evaluated eagerly it slipped the Whatever ITSELF and
# the map produced nothing, so Data::Tree's `flatten` returned only the root
# and `levels` only the first level — silently, on every tree.
use Test;
plan 8;

my @nested = [1, 2], [3, 4];
is @nested.map(|*).raku, '(1, 2, 3, 4).Seq', '.map(|*) flattens one level';
is [1, |@nested.map(|*)].raku, '[1, 1, 2, 3, 4]', 'the flattened Seq slips into a list literal';
my &slipper = |*;
is &slipper.^name, 'WhateverCode', '|* on its own is a WhateverCode';
is [1, slipper([3, 4])].raku, '[1, 3, 4]', '…that slips its argument into the list around it';
is @nested.map(*.Slip).raku, '(1, 2, 3, 4).Seq', '*.Slip agrees with |*';

# …and the ordinary spread meanings of `|` are untouched
sub count(*@a) { @a.elems }
my @l = <a b>;
is count(|@l), 2, '|@list still spreads positionally';
is count(|(1, 2)), 2, '|(list) still spreads positionally';
sub named(:$x) { $x }
my %h = x => 7;
is named(|%h), 7, '|%hash still spreads as named arguments';
