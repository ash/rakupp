# Regression: a `.first` or `.head` over a map/grep runs the blocks only until
# it has its answer, as Rakudo's lazy Seq does. fez picks its HTTP client with
# `@handlers.map({ … .able … }).grep(*.so).first`, which asks curl and stops;
# Raku++ ran the map over every handler first, so on a machine without wget the
# wget probe's failed spawn killed `use Fez::API`, and `rakupp install fez`.
# Also: `(1..*).map(…).first` with no matcher answered Nil without running the
# block, and `(1..*).grep(…).first` ran it a million times.

use Test;

plan 18;

my @seen;

# fez's own shape: the second and third handlers are never asked
is <a b c>.map({ @seen.push($_); $_ eq 'a' }).grep(*.so).first, True,
    'map → grep(*.so) → first answers the first truthy result';
is-deeply @seen, ['a'], '…and asks only the first handler';

# the probe that would die is never run
lives-ok { <ok boom>.map({ die "ran $_" if $_ eq 'boom'; $_ }).first },
    'a block for an element past the answer is never called';

@seen = ();
my @h = <a b c>;
is @h.map({ @seen.push($_); $_ }).first, 'a', 'map → first over an Array';
is-deeply @seen, ['a'], '…maps one element';

@seen = ();
is <a b c>.grep({ @seen.push($_); True }).first, 'a', 'grep → first';
is-deeply @seen, ['a'], '…tests one element';

@seen = ();
is (1..5).map({ @seen.push($_); $_ * 2 }).first(* > 4), 6, 'a Range, with a matcher';
is-deeply @seen, [1, 2, 3], '…maps up to the match';

@seen = ();
is-deeply (1..5).map({ @seen.push($_); $_ }).head(2), (1, 2), '.head(2)';
is-deeply @seen, [1, 2], '…maps two';

@seen = ();
is (1..*).map({ @seen.push($_); $_ }).first, 1, 'an endless map → first with no matcher is its first element';
is (1..*).grep({ @seen.push($_); .so }).first, 1, 'an endless grep → first with no matcher';
is-deeply @seen, [1, 1], '…each runs its block once';

my @w = 1, 2, 3;
@w.map({ $_ *= 10 }).first;
is-deeply @w, [10, 2, 3], 'a write to $_ reaches the element, and only the one read';

is <x y>.map({ |($_, $_) }).head(3), <x x y>, 'a Slip answered by the block spreads';

# what is not a prefix read stays eager, as in Rakudo
@seen = ();
my @all = <a b c>.map({ @seen.push($_); $_ });
is-deeply @seen, [<a b c>], 'a map assigned to an Array runs over every element';
is <a b c>.map(*.uc).first(:end), 'C', '.first(:end) reads from the other end';
