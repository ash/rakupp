# `EXPR for CONTAINER` drains a user Iterator, exactly as the block form does.
# Only the block form did: the statement-modifier path took the iterator OBJECT
# as its single topic and ran once. Array::Agnostic's family — every class with
# its own `.iterator` — iterates that way, and `is $_, ++$value for @a` compared
# ten expected values against one iterator.
use Test;
plan 4;

class It does Iterator {
    has @.items;
    has Int $!i = 0;
    method pull-one { $!i < @!items.elems ?? @!items[$!i++] !! IterationEnd }
}
class MyArr does Positional does Iterable {
    has @!array;
    method AT-POS($p) is raw { @!array.AT-POS($p) }
    method elems             { @!array.elems }
    method STORE(\values)    { @!array = values; self }
    method iterator          { It.new(items => @!array) }
}

my @a is MyArr = 1 .. 3;

my @mod;   @mod.push($_) for @a;
is-deeply @mod, [1, 2, 3],      'the modifier form drains the iterator';

my @blk;   for @a { @blk.push($_) }
is-deeply @blk, [1, 2, 3],      '…and so does the block form';

my $sum = 0; $sum += $_ for @a;
is $sum, 6,                     'the topic is each value, not the iterator';

is @a.map({ $_ * 2 }).List, (2, 4, 6), '.map is unchanged';
