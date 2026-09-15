# `@a[*-1]` on a container class resolves the Whatever against the CONTAINER's
# own `.elems` — Rakudo calls `.elems` and hands `AT-POS` the resolved Int. The
# raw WhateverCode was passed straight through instead, so a class's
# `method AT-POS($p)` received a closure where an index was due and read element
# zero: Array::Agnostic's suite asserts `@a[* - $_]` ten times per subtest, and
# every one of them answered the first element.
use Test;
plan 5;

class MyArr does Positional {
    has @!array;
    method AT-POS($p) is raw { @!array.AT-POS($p) }
    method elems             { @!array.elems }
    method STORE(\values)    { @!array = values; self }
}

my @a is MyArr = 1 .. 10;

is @a[0],    1,     'a plain index is unchanged';
is @a[*-1], 10,     'the last element comes from .elems';
is @a[*-2],  9,     '…and the one before it';
is-deeply @a[2..4].List, (3, 4, 5), 'a range subscript still works';

my @plain = 1 .. 10;
is @plain[*-1], 10, 'an ordinary Array is unaffected';
