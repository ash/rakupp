# A named `is rw` or `is raw` parameter is the caller's container, as a
# positional one is: `sub f(:$x! is rw) { $x = 5 }; f(x => $v)` sets $v. The
# copy-back walked the positional parameters only, so every named one wrote
# into a copy, and a literal argument bound without complaint.
use Test;
plan 13;

my $v;
sub f(:$x! is rw) { $x = 5 }
$v = 1; f(x => $v);  is $v, 5,  'x => $v';
$v = 1; f(:x($v));   is $v, 5,  ':x($v)';
my $x = 2; f(:$x);   is $x, 5,  ':$x';

sub r(:$x is raw) { $x = 9 }
$v = 1; r(x => $v);  is $v, 9,  'is raw, optional';

sub k(:y($x)! is rw) { $x = 11 }
$v = 1; k(y => $v);  is $v, 11, 'through an alias';

my @a = 1, 2; f(x => @a[0]);   is @a[0], 5, 'an array element';
my %h = a => 1; f(x => %h<a>); is %h<a>, 5, 'a hash element';

sub p($pos is rw, :$x! is rw) { $pos = 3; $x = 4 }
my $w = 0; $v = 1; p($w, x => $v);
is "$w $v", '3 4',             'beside a positional rw';

class C { method set(:$x! is rw) { $x = 21 } }
$v = 1; C.set(x => $v);        is $v, 21, 'a method';
my &pb = -> :$x! is rw { $x = 23 };
$v = 1; pb(x => $v);           is $v, 23, 'a pointy block';

sub twice(:$x! is rw) { $x++; $x++ }
$v = 1; twice(x => $v);        is $v, 3,  'every write reaches the caller';

throws-like { f(x => 3) }, X::Parameter::RW, 'a literal is no container';

my %args = x => (my $s = 1);
f(|%args);                     is $s, 1,  'a flattened hash passes values, as in Rakudo';
