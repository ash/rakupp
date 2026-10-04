# A type object is an undefined value of its OWN type, so it binds a typed
# parameter only where that type conforms. The binder let every type object
# through every nominal type: `sub g(Str $x) {}; g(Any)` ran, and so did
# `g(Int)`, for positional and named parameters, pointy blocks and methods.
# The same gap hid three more: an enum-typed parameter took any value
# (`f(5)` into `Color $c`), `Array[Int] $a` took a plain `[1, 2]`, and the
# Allomorph type object was not a Str.
use Test;
plan 22;

# the arguments go through variables: Rakudo refuses a literal `g(Int)` at
# compile time, which is a different check
my ($Any, $Int, $Str, $Mu, $Cool) = Any, Int, Str, Mu, Cool;

sub g(Str $x) { 'ran' }
is g($Str), 'ran',                     'Str binds a Str parameter';
throws-like { g($Any) }, X::TypeCheck::Binding::Parameter, 'Any does not';
throws-like { g($Int) }, X::TypeCheck::Binding::Parameter, 'Int does not';
throws-like { g($Cool) }, X::TypeCheck::Binding::Parameter, 'nor does a wider type';
is g(Allomorph), 'ran',                'Allomorph is a Str';
is Allomorph.^mro.map(*.^name).join(' '), 'Allomorph Str Cool Any Mu', '…by its MRO';

sub h(Str :$x) { 'ran' }
throws-like { h(x => $Int) }, X::TypeCheck::Binding::Parameter, 'a named parameter refuses it too';

sub j(Cool $x) { 'ran' }
is j($Int), 'ran',                     'a narrower type object binds a wider type';
throws-like { j($Any) }, X::TypeCheck::Binding::Parameter, 'Any is not Cool';

sub k($x) { 'ran' }
is k($Int), 'ran',                     'an untyped parameter takes any type object';

my &pointy = -> Str $s { 'ran' };
throws-like { pointy($Int) }, X::TypeCheck::Binding::Parameter, 'a pointy block checks it';
class A { method m(Str $s) { 'ran' } }
throws-like { A.m($Int) }, X::TypeCheck::Binding::Parameter, 'and a method';

enum Color <Red Green>;
sub c(Color $c) { 'ran' }
is c(Red), 'ran',                      'an enum value binds its enum';
is c(Color), 'ran',                    '…and the enum type object';
throws-like { c(5) }, X::TypeCheck::Binding::Parameter, 'a plain Int does not';
throws-like { c($Int) }, X::TypeCheck::Binding::Parameter, 'nor the Int type object';
sub i(Int $x) { 'ran' }
is i(Color), 'ran',                    'an Int-valued enum type object is an Int';

sub arr(Array[Int] $a) { 'ran' }
my Int @typed = 1, 2;
is arr(@typed), 'ran',                 'Array[Int] takes a typed array';
is arr(Array[Int].new(1)), 'ran',      '…and Array[Int].new';
throws-like { arr([1, 2]) }, X::TypeCheck::Binding::Parameter, 'not an untyped one';
my Str @strs = 'a';
throws-like { arr(my $ = @strs) }, X::TypeCheck::Binding::Parameter, 'nor one of another type';

sub hsh(Hash[Int] $h) { 'ran' }
throws-like { hsh(%(a => 1)) }, X::TypeCheck::Binding::Parameter, 'Hash[Int] refuses an untyped hash';
