# The QuantHash family — Baggy, Setty, Mixy — was known to `~~` and unknown to
# parameter BINDING, so `multi sub mode(Baggy $x)` could not be called with a
# Bag at all: Stats declares exactly that candidate beside a `List` one, and the
# Bag half of its own test suite reached no candidate. Both paths now answer
# from the one does-table.
use Test;
plan 7;

sub takes-baggy(Baggy $x)     { $x.elems }
sub takes-setty(Setty $x)     { $x.elems }
sub takes-mixy(Mixy $x)       { $x.elems }
sub takes-quant(QuantHash $x) { $x.elems }

is takes-baggy((1, 2, 2).Bag), 2,   'a Bag binds Baggy';
is takes-setty((1, 2).Set), 2,      'a Set binds Setty';
is takes-mixy((1, 2).Mix), 2,       'a Mix binds Mixy';
is takes-quant((1, 2).Bag), 2,      '…and a Bag binds QuantHash';
is takes-baggy((1, 2).Mix), 2,      'a Mix is Baggy too';

multi md(Baggy $x) { 'baggy' }
multi md(List $x)  { 'list'  }
is md((3, 5, 3).Bag), 'baggy',      'multi dispatch reaches the Baggy candidate';
is md((3, 5, 3)),     'list',       '…and the List one still wins for a list';
