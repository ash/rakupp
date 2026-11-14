# Regression (RAKUDO-ISSUES-PLAN R3; rakudo#2663): `self!name` reaches the
# private method of the package the call is written in.
#
# Before the fix it reached the object's most derived one: with
# `(Puppy.new but Violinist) but Diva`, Puppy's own `greet` bowed as the
# Diva, and so did the Violinist's `fiddle`. Rakudo resolves a private call
# in the class whose method makes it, and a role's method in the class the
# role was composed into.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

role Diva      { method captivate { self!bow }; method !bow { 'diva' } }
role Violinist { method fiddle    { self!bow }; method !bow { 'violin' } }
class Puppy    { method greet     { self!bow }; method !bow { 'woof' } }

my $one = Puppy.new but Violinist;
check 'one mixin: the class method', $one.greet, 'woof';
check 'one mixin: the role method', $one.fiddle, 'violin';

my $two = (Puppy.new but Violinist) but Diva;
check 'two mixins: the class method (rakudo#2663)', $two.greet, 'woof';
check 'two mixins: the first role', $two.fiddle, 'violin';
check 'two mixins: the second role', $two.captivate, 'diva';

# a class's own private method outranks its role's, for the role's method too
class Busker does Violinist { method greet { self!bow }; method !bow { 'busk' } }
check 'a class that does the role', (Busker.new.greet, Busker.new.fiddle), ('busk', 'busk');

# …and a subclass's does not reach into the parent's methods
class A { method t { self!p }; method !p { 'A' } }
class B is A { method !p { 'B' }; method u { self!p } }
check 'a parent class method', B.new.t, 'A';
check 'the subclass method', B.new.u, 'B';
role R { method !h { 'R' }; method t { self!h } }
class C does R { }
class D is C { method !h { 'D' } }
check 'a role method in a parent class', D.new.t, 'R';

# another object of the class
class E { method !p { 'E' }; method t($o) { $o!p } }
check '$other!p', E.new.t(E.new), 'E';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
