# A sigilless parameter given no container names the value itself, so a write
# to it is refused the way `my \v = Int; v = 1` already was: "Cannot modify an
# immutable 'Int' type object" for a type object written as a bare name, and
# "Cannot modify an immutable Int (5)" for a literal. `-> \v { v = 1 }(Int)`
# ran, and the literal case died with an untyped "readonly" message. An
# `s///` on such a name is Rakudo's X::AdHoc, not X::Assignment::RO.
use Test;
plan 8;

throws-like { -> \v { v = 1 }(Int) }, X::Assignment::RO,
    message => "Cannot modify an immutable 'Int' type object", 'a type object';
throws-like { sub (\v) { v = 1 }(Str) }, X::Assignment::RO,
    message => "Cannot modify an immutable 'Str' type object", '…in a sub too';
throws-like { -> \v { v = 1 }(5) }, X::Assignment::RO,
    message => 'Cannot modify an immutable Int (5)', 'a literal';

my $i = Int;
is -> \v { v = 1; v }($i), 1,      'a variable holding a type object is a container';
my @a; sub h(\x) { x = 3 }; h(@a[0]);
is @a[0], 3,                       'an element is a container';

sub k(\x) { x ~~ s/a/b/; x }
throws-like { k('a') }, X::AdHoc,
    message => 'Cannot assign to an immutable value', 's/// on a bare value';
sub k2($x) { $x ~~ s/a/b/ }
throws-like { k2('a') }, X::AdHoc,
    message => 'Cannot assign to a readonly variable or a value', 's/// on a readonly parameter';

# …and a scalar BOUND to a definite type object names the smiley it carries
throws-like { my $x := Int:D; $x = 5 }, X::AdHoc,
    message => 'assign requires a concrete object (got a Int:D type object instead)',
    'the :D of a bound type object';
