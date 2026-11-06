# Regression: a `constant` bound to a type names that type in a signature
# (issue #220).
#
# Rakudo resolves a constant at compile time, so `constant Alias = Foo::Bar;
# sub g(Alias $x)` has a Foo::Bar parameter. The binder and the return check
# already resolved the name per call, but introspection read the constraint
# as written: `.type` answered a type named Alias that no expression could
# name (`=== Foo::Bar` was False), and the signature rendered `:(Alias $x)`.
# So did a method's, a `:(…)` literal's, a smiley, a coercion, an @-parameter's
# element type, a role parameter's, and `--> Alias`. And the binder dropped a
# parameterization: `constant AI = Array[Int]; sub f(AI $x)` took any Array.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub is(\got, \want, $what) { @fail.push("$what: got {got.raku} want {want.raku}") unless got eqv want }
sub ok($c, $what) { @fail.push($what) unless $c }

class Foo::Bar { }
my constant Alias = Foo::Bar;
constant IntA = Int;
constant AI = Array[Int];
constant AU = UInt;

# the issue's rows
sub g(Alias $x) { $x.^name }
ok Alias === Foo::Bar,                          'the alias is the type outside a signature';
is &g.signature.params[0].type.^name, 'Foo::Bar', '.type.^name';
ok &g.signature.params[0].type === Foo::Bar,    '.type is the aliased type object';
is &g.signature.raku, ':(Foo::Bar $x)',         '.signature.raku';
is &g.signature.gist, '(Foo::Bar $x)',          '.signature.gist';
is &g.signature.params[0].gist, 'Foo::Bar $x',  'the parameter alone';

# …and it still binds as the type it names
is g(Foo::Bar.new), 'Foo::Bar', 'binds an instance';
my $v = 42;
ok !(try g($v)).defined && $!.message.contains('expected Foo::Bar'), 'refuses anything else, by the type';

# a core type, a smiley, an @-parameter, a coercion, a parameterization, a subset
sub h(IntA $n) { }
is &h.signature.raku, ':(Int $n)', 'an alias of Int';
ok &h.signature.params[0].type === Int, '…whose .type is Int';
sub d(Alias:D $x) { }
is &d.signature.raku, ':(Foo::Bar:D $x)', 'with a smiley';
is &d.signature.params[0].type.^name, 'Foo::Bar', '…whose .type is the nominal type';
sub a(Alias @a) { }
is &a.signature.raku, ':(Foo::Bar @a)', 'an @-parameter';
ok &a.signature.params[0].type.of === Foo::Bar, '…whose .type.of is the aliased type';
sub c(Alias() $x) { }
is &c.signature.raku, ':(Foo::Bar(Any) $x)', 'a coercion';
sub ai(AI $x) { 'ok' }
is &ai.signature.raku, ':(Array[Int] $x)', 'an alias of a parameterized type';
my Int @typed = 1, 2;
my @untyped = 1, 2;
is ai(@typed), 'ok', '…binds an Array[Int]';
ok !(try ai(@untyped)).defined, '…and refuses an untyped Array, as `Array[Int] $x` does';
multi md(AI $x) { 'ai' }
multi md(@x)    { 'any' }
is md(@typed) ~ md(@untyped), 'aiany', '…and so does multi dispatch';
sub u(AU $x) { }
sub u2(UInt $x) { }
is &u.signature.raku, &u2.signature.raku, 'an alias of a subset renders as the subset does';

# the return type
sub r(--> Alias) { Foo::Bar }
is &r.signature.raku, ':( --> Foo::Bar)', '--> Alias';
ok &r.returns === Foo::Bar, '&r.returns';
ok &r.signature.returns === Foo::Bar, '&r.signature.returns';

# a method, a signature literal, a nested class's alias, a lexical constant
class C { method m(Alias $z) { } }
is C.^lookup('m').signature.raku, ':(C $:: Foo::Bar $z, *%_)', 'a method';
is :(Alias $p).raku, ':(Foo::Bar $p)', 'a signature literal';
class Outer { class Inner { }; constant I = Inner; method m(I $x) { } }
is Outer.^lookup('m').signature.raku, ':(Outer $:: Outer::Inner $x, *%_)', 'an alias in a class body';
{
    my constant L = Str;
    sub f(L $s) { }
    is &f.signature.raku, ':(Str $s)', 'a lexical constant';
}

# a role's type parameter, as the class that does it sees it
role R[::T] { method m(T $x) { } }
class RI does R[Int] { }
ok RI.^lookup('m').signature.params.first(*.name eq '$x').type === Int, "a role parameter's type";

# …and a name that is no alias stays as it was written
sub t(::T $x, T $y) { 'ok' }
is &t.signature.params[1].type.^name, 'T', 'a type capture';
ok t(1, 2) eq 'ok' && !(try t(1, 'a')).defined, '…which still captures';
{
    my class Lex { }
    sub l(Lex $x) { }
    is &l.signature.raku, ':(Lex $x)', 'a lexical class';
}

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
