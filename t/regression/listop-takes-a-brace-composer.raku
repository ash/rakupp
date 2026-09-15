# `%{ … }` and `@{ … }` are composers, and a listop argument position has no
# left operand — so the `%` there cannot be infix modulo and the brace cannot
# be a block. Reading it as one made `make %{'k' => 1}` a nullary `make`
# followed by a stray block, storing nothing: FEN::Grammar builds its castling
# rights that way, so every parsed chess position lost all four flags.
use Test;
plan 5;

sub one(|c) { c.list.elems }
is one(%{'k' => 1}), 1,  'a brace composer with parens was always fine';
is (one %{'k' => 1}), 1, '…and now without them';

grammar G { token TOP { 'x' } }
class A { method TOP($/) { make %{'k' => 1, 'q' => 2} } }
my $made = G.parse('x', actions => A.new).made;
ok $made.defined,        'make %{ … } stores something';
is $made<k>, 1,          '…the hash it was given';
is $made<q>, 2,          '…with every key';
