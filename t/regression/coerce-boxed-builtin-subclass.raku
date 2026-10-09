# Regression, 2026-10-09: a class deriving a boxed built-in — `class Sym is
# Str` — is coerced to by `T.new(x)` on every route, as the literal `Sym('b')`
# call already was. A type in a variable, a `Sym()` parameter, a role's
# `T($v)` and the SHORT name of a class inside its own package all reached
# the generic coercion, which did not know the rule: "Impossible coercion
# from 'Str' into 'M::Sym'". BSON::Simple decodes a BSON symbol as
# `Symbol(read-string)` inside its module (6 of its tests). Expected values
# are Rakudo 2026.09's.
use Test;
plan 8;

module M {
    class Sym is Str { }
    our sub mk($x) { Sym($x) }
    our sub mk2() { my &rs = -> { "q" }; Sym(rs) }
}
is M::mk('b').^name, 'M::Sym', 'the short name inside its package';
is M::mk('b'), 'b', '…holding the value';
is M::mk2().^name, 'M::Sym', '…with a code variable for the argument';

class Sym is Str { }
my $t = Sym;
is $t('b').^name, 'Sym', 'a type held in a variable';
sub f(Sym() $x) { $x.^name }
is f('c'), 'Sym', 'a coercion parameter';
class I is Int { }
sub g(I() $x) { $x.^name ~ ' ' ~ ($x + 1) }
is g(7), 'I 8', '…an Int subclass';
role R[::T] { method mk($v) { T($v) } }
class C does R[Sym] { }
is C.new.mk('d').^name, 'Sym', 'a role type capture';
is Sym('x').^name, 'Sym', 'the literal call, as before';
