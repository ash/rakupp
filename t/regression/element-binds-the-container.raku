# `%h<k> := $a` binds the ELEMENT to the CONTAINER, so a later write to `$a` is
# seen through the hash. The element took a copy of the value instead, and
# Hash::MultiValue — whose first test binds an element and then writes through
# the variable — read back the old number. The same promotion the scalar alias
# uses: the source slot becomes a proxy onto a shared cell, and the element
# holds another onto the same cell.
use Test;
plan 5;

my %h;
my $a = 10;
%h<a> := $a;
is %h<a>, 10,           'the element reads the bound value';
$a = 42;
is %h<a>, 42,           '…and a write through the variable is seen';
%h<a> = 7;
is $a, 7,               '…and a write through the element is seen too';

my @arr;
my $x = 1;
@arr[0] := $x;
$x = 5;
is @arr[0], 5,          'a positional element binds the same way';

class Assoc does Associative {
    has %.store;
    method AT-KEY($k) is raw   { %!store.AT-KEY($k) }
    method BIND-KEY($k, \v)    { %!store.BIND-KEY($k, v) }
    method EXISTS-KEY($k)      { %!store.EXISTS-KEY($k) }
}
my %c := Assoc.new;
my $b = 10;
%c<b> := $b;
$b = 42;
is %c<b>, 42,           'a custom BIND-KEY receives the container';
